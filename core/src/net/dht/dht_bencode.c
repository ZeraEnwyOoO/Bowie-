/*
 * Wingo — P2P Internet Sharing Tool (Repo: Bowie)
 * Copyright (C) 2024 ASBM Team
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * Bencode implementation for Bowie DHT.
 *
 * Bencode is the encoding format used by BitTorrent DHT (BEP 5).
 * It is defined in BEP 3.
 *
 * Encoding rules:
 *   - Integers:    i<number>e            (e.g., i42e)
 *   - Strings:     <length>:<data>       (e.g., 5:hello)
 *   - Lists:       l<items>e             (e.g., l5:helloi42ee)
 *   - Dicts:       d<key><value>...e     (e.g., d3:foo3:bare)
 *
 * Bencode is a binary-safe format: strings can contain any byte.
 * Keys in dictionaries are always strings and must be sorted
 * lexicographically for canonical encoding (though we accept
 * unsorted input when decoding).
 *
 * This implementation uses an opaque tree structure:
 *
 *   wingo_bencode_t
 *   ├── type (INTEGER, STRING, LIST, DICT)
 *   ├── integer value (for INTEGER)
 *   ├── string data (for STRING)
 *   ├── list of children (for LIST)
 *   └── list of key-value pairs (for DICT)
 *
 * Memory model:
 *   - All values are heap-allocated.
 *   - Lists and dicts own their children.
 *   - Freeing a value frees all children recursively.
 *   - When you add a child to a list/dict, ownership transfers
 *     to the parent.
 */

#include "wingo/net/dht/dht_bencode.h"
#include "wingo/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Dictionary entry.
 */
typedef struct {
    char            *key;       /* Key (null-terminated) */
    wingo_size       key_len;   /* Key length (may include null) */
    wingo_bencode_t *value;     /* Value */
} bencode_dict_entry_t;

/*
 * Bencode value (concrete).
 */
struct wingo_bencode {
    wingo_bencode_type_t type;

    union {
        /* INTEGER */
        wingo_i64 integer;

        /* STRING */
        struct {
            void       *data;
            wingo_size  len;
        } string;

        /* LIST */
        struct {
            wingo_bencode_t **items;
            wingo_size        count;
            wingo_size        capacity;
        } list;

        /* DICT */
        struct {
            bencode_dict_entry_t *entries;
            wingo_size            count;
            wingo_size            capacity;
        } dict;
    } v;
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial capacity for lists and dicts.
 */
#define BENCODE_INIT_CAPACITY   8

/*
 * Growth factor.
 */
#define BENCODE_GROW_FACTOR     2

/*
 * Maximum nesting depth (to prevent stack overflow from malicious input).
 */
#define BENCODE_MAX_DEPTH       64

/*
 * Maximum string length (16 MB).
 */
#define BENCODE_MAX_STRING_LEN  (16 * 1024 * 1024)

/*
 * Maximum integer digits.
 */
#define BENCODE_MAX_INT_DIGITS  20

/* ============================================================================
 * INTERNAL HELPERS — VALIDATION
 * ============================================================================ */

/*
 * Check if a byte is a digit.
 */
static bool is_digit(u_char c)
{
    return c >= '0' && c <= '9';
}

/*
 * Check if a byte is a valid bencode integer char.
 */
static bool is_int_char(u_char c)
{
    return is_digit(c) || c == '-';
}

/* ============================================================================
 * INTERNAL HELPERS — MEMORY
 * ============================================================================ */

/*
 * Allocate a new bencode value.
 */
static wingo_bencode_t *bencode_alloc(wingo_bencode_type_t type)
{
    wingo_bencode_t *value = calloc(1, sizeof(wingo_bencode_t));
    if (value == NULL) {
        return NULL;
    }

    value->type = type;
    return value;
}

/*
 * Grow a list's capacity.
 */
static wingo_error_t bencode_list_grow(wingo_bencode_t *list)
{
    wingo_size new_cap;
    wingo_bencode_t **new_items;

    if (list->v.list.capacity == 0) {
        new_cap = BENCODE_INIT_CAPACITY;
    } else {
        new_cap = list->v.list.capacity * BENCODE_GROW_FACTOR;
    }

    /* Overflow check */
    if (new_cap < list->v.list.capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_items = realloc(list->v.list.items,
                        new_cap * sizeof(wingo_bencode_t *));
    if (new_items == NULL) {
        return WINGO_ERR_NOMEM;
    }

    list->v.list.items = new_items;
    list->v.list.capacity = new_cap;

    return WINGO_SUCCESS;
}

/*
 * Grow a dict's capacity.
 */
static wingo_error_t bencode_dict_grow(wingo_bencode_t *dict)
{
    wingo_size new_cap;
    bencode_dict_entry_t *new_entries;

    if (dict->v.dict.capacity == 0) {
        new_cap = BENCODE_INIT_CAPACITY;
    } else {
        new_cap = dict->v.dict.capacity * BENCODE_GROW_FACTOR;
    }

    /* Overflow check */
    if (new_cap < dict->v.dict.capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_entries = realloc(dict->v.dict.entries,
                          new_cap * sizeof(bencode_dict_entry_t));
    if (new_entries == NULL) {
        return WINGO_ERR_NOMEM;
    }

    dict->v.dict.entries = new_entries;
    dict->v.dict.capacity = new_cap;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DECODE
 * ============================================================================ */

/*
 * Forward declaration.
 */
static wingo_bencode_t *bencode_decode_internal(const u_char *data,
                                                 wingo_size len,
                                                 wingo_size *pos,
                                                 int depth);

/*
 * Decode an integer: i<number>e
 */
static wingo_bencode_t *bencode_decode_integer(const u_char *data,
                                                wingo_size len,
                                                wingo_size *pos)
{
    wingo_bencode_t *value;
    wingo_size start;
    wingo_size i;
    bool negative = false;
    wingo_i64 result = 0;
    wingo_i64 digit_count = 0;
    bool has_digits = false;

    /* Skip 'i' */
    (*pos)++;

    start = *pos;

    /* Check for negative sign */
    if (*pos < len && data[*pos] == '-') {
        negative = true;
        (*pos)++;
    }

    /* Parse digits */
    i = *pos;
    while (i < len && is_digit(data[i])) {
        has_digits = true;
        digit_count++;

        if (digit_count > BENCODE_MAX_INT_DIGITS) {
            return NULL;  /* Too many digits */
        }

        /* Check overflow */
        if (result > (INT64_MAX - (data[i] - '0')) / 10) {
            return NULL;  /* Overflow */
        }

        result = result * 10 + (data[i] - '0');
        i++;
    }

    /* Must have at least one digit */
    if (!has_digits) {
        return NULL;
    }

    /* Must end with 'e' */
    if (i >= len || data[i] != 'e') {
        return NULL;
    }

    /* Check for leading zeros (not allowed: "i03e" is invalid) */
    if (digit_count > 1 && data[start] == '0') {
        return NULL;
    }

    /* "-0" is invalid */
    if (negative && result == 0) {
        return NULL;
    }

    *pos = i + 1;  /* Skip 'e' */

    value = bencode_alloc(WINGO_BENCODE_INTEGER);
    if (value == NULL) {
        return NULL;
    }

    value->v.integer = negative ? -result : result;

    return value;
}

/*
 * Decode a string: <length>:<data>
 */
static wingo_bencode_t *bencode_decode_string(const u_char *data,
                                               wingo_size len,
                                               wingo_size *pos)
{
    wingo_bencode_t *value;
    wingo_size str_len = 0;
    wingo_size i;
    wingo_size start;
    bool has_digits = false;
    wingo_i64 digit_count = 0;

    start = *pos;

    /* Parse length */
    i = *pos;
    while (i < len && is_digit(data[i])) {
        has_digits = true;
        digit_count++;

        if (digit_count > BENCODE_MAX_INT_DIGITS) {
            return NULL;
        }

        if (str_len > (BENCODE_MAX_STRING_LEN / 10)) {
            return NULL;  /* Too large */
        }

        str_len = str_len * 10 + (data[i] - '0');
        i++;
    }

    /* Must have at least one digit */
    if (!has_digits) {
        return NULL;
    }

    /* Must have ':' */
    if (i >= len || data[i] != ':') {
        return NULL;
    }

    i++;  /* Skip ':' */

    /* Check if we have enough data */
    if (str_len > len - i) {
        return NULL;  /* Not enough data */
    }

    /* Check max length */
    if (str_len > BENCODE_MAX_STRING_LEN) {
        return NULL;
    }

    /* Create value */
    value = bencode_alloc(WINGO_BENCODE_STRING);
    if (value == NULL) {
        return NULL;
    }

    /* Copy string data */
    if (str_len > 0) {
        value->v.string.data = malloc(str_len);
        if (value->v.string.data == NULL) {
            free(value);
            return NULL;
        }
        memcpy(value->v.string.data, data + i, str_len);
    } else {
        value->v.string.data = NULL;
    }

    value->v.string.len = str_len;

    *pos = i + str_len;

    (void)start;  /* Suppress unused warning */

    return value;
}

/*
 * Decode a list: l<items>e
 */
static wingo_bencode_t *bencode_decode_list(const u_char *data,
                                             wingo_size len,
                                             wingo_size *pos,
                                             int depth)
{
    wingo_bencode_t *list;
    wingo_bencode_t *item;

    /* Skip 'l' */
    (*pos)++;

    list = bencode_alloc(WINGO_BENCODE_LIST);
    if (list == NULL) {
        return NULL;
    }

    /* Decode items until 'e' */
    while (*pos < len && data[*pos] != 'e') {
        item = bencode_decode_internal(data, len, pos, depth + 1);
        if (item == NULL) {
            wingo_bencode_free(list);
            return NULL;
        }

        /* Grow if needed */
        if (list->v.list.count >= list->v.list.capacity) {
            if (bencode_list_grow(list) != WINGO_SUCCESS) {
                wingo_bencode_free(item);
                wingo_bencode_free(list);
                return NULL;
            }
        }

        list->v.list.items[list->v.list.count++] = item;
    }

    /* Must end with 'e' */
    if (*pos >= len || data[*pos] != 'e') {
        wingo_bencode_free(list);
        return NULL;
    }

    (*pos)++;  /* Skip 'e' */

    return list;
}

/*
 * Decode a dict: d<key><value>...e
 */
static wingo_bencode_t *bencode_decode_dict(const u_char *data,
                                             wingo_size len,
                                             wingo_size *pos,
                                             int depth)
{
    wingo_bencode_t *dict;
    wingo_bencode_t *key;
    wingo_bencode_t *value;
    bencode_dict_entry_t *entry;

    /* Skip 'd' */
    (*pos)++;

    dict = bencode_alloc(WINGO_BENCODE_DICT);
    if (dict == NULL) {
        return NULL;
    }

    /* Decode key-value pairs until 'e' */
    while (*pos < len && data[*pos] != 'e') {
        /* Key must be a string */
        key = bencode_decode_internal(data, len, pos, depth + 1);
        if (key == NULL) {
            wingo_bencode_free(dict);
            return NULL;
        }

        if (key->type != WINGO_BENCODE_STRING) {
            wingo_bencode_free(key);
            wingo_bencode_free(dict);
            return NULL;
        }

        /* Value */
        value = bencode_decode_internal(data, len, pos, depth + 1);
        if (value == NULL) {
            wingo_bencode_free(key);
            wingo_bencode_free(dict);
            return NULL;
        }

        /* Grow if needed */
        if (dict->v.dict.count >= dict->v.dict.capacity) {
            if (bencode_dict_grow(dict) != WINGO_SUCCESS) {
                wingo_bencode_free(key);
                wingo_bencode_free(value);
                wingo_bencode_free(dict);
                return NULL;
            }
        }

        /* Add entry */
        entry = &dict->v.dict.entries[dict->v.dict.count];

        /* Copy key as null-terminated string */
        entry->key = malloc(key->v.string.len + 1);
        if (entry->key == NULL) {
            wingo_bencode_free(key);
            wingo_bencode_free(value);
            wingo_bencode_free(dict);
            return NULL;
        }

        if (key->v.string.len > 0) {
            memcpy(entry->key, key->v.string.data, key->v.string.len);
        }
        entry->key[key->v.string.len] = '\0';
        entry->key_len = key->v.string.len;
        entry->value = value;

        dict->v.dict.count++;

        /* Free key value (we copied its data) */
        wingo_bencode_free(key);
    }

    /* Must end with 'e' */
    if (*pos >= len || data[*pos] != 'e') {
        wingo_bencode_free(dict);
        return NULL;
    }

    (*pos)++;  /* Skip 'e' */

    return dict;
}

/*
 * Internal decode with depth tracking.
 */
static wingo_bencode_t *bencode_decode_internal(const u_char *data,
                                                 wingo_size len,
                                                 wingo_size *pos,
                                                 int depth)
{
    /* Depth check */
    if (depth > BENCODE_MAX_DEPTH) {
        return NULL;
    }

    /* Bounds check */
    if (*pos >= len) {
        return NULL;
    }

    switch (data[*pos]) {
    case 'i':
        return bencode_decode_integer(data, len, pos);

    case 'l':
        return bencode_decode_list(data, len, pos, depth);

    case 'd':
        return bencode_decode_dict(data, len, pos, depth);

    default:
        if (is_digit(data[*pos])) {
            return bencode_decode_string(data, len, pos);
        }
        return NULL;
    }
}

/*
 * Decode a Bencode value from buffer.
 */
wingo_bencode_t *wingo_bencode_decode(const void *data, wingo_size len)
{
    wingo_size pos = 0;
    wingo_bencode_t *value;

    if (data == NULL || len == 0) {
        return NULL;
    }

    value = bencode_decode_internal((const u_char *)data, len, &pos, 0);

    /* Must consume entire input */
    if (value != NULL && pos != len) {
        wingo_bencode_free(value);
        return NULL;
    }

    return value;
}

/*
 * Decode a Bencode value from buffer with position.
 */
wingo_bencode_t *wingo_bencode_decode_at(const void *data, wingo_size len,
                                          wingo_size *pos)
{
    if (data == NULL || len == 0 || pos == NULL) {
        return NULL;
    }

    if (*pos >= len) {
        return NULL;
    }

    return bencode_decode_internal((const u_char *)data, len, pos, 0);
}

/*
 * Free a Bencode value.
 */
void wingo_bencode_free(wingo_bencode_t *value)
{
    wingo_size i;

    if (value == NULL) {
        return;
    }

    switch (value->type) {
    case WINGO_BENCODE_INTEGER:
        /* Nothing to free */
        break;

    case WINGO_BENCODE_STRING:
        if (value->v.string.data != NULL) {
            /* Zero before free (may contain sensitive data) */
            memset(value->v.string.data, 0, value->v.string.len);
            free(value->v.string.data);
        }
        break;

    case WINGO_BENCODE_LIST:
        for (i = 0; i < value->v.list.count; i++) {
            wingo_bencode_free(value->v.list.items[i]);
        }
        if (value->v.list.items != NULL) {
            free(value->v.list.items);
        }
        break;

    case WINGO_BENCODE_DICT:
        for (i = 0; i < value->v.dict.count; i++) {
            bencode_dict_entry_t *entry = &value->v.dict.entries[i];

            if (entry->key != NULL) {
                memset(entry->key, 0, entry->key_len);
                free(entry->key);
            }

            wingo_bencode_free(entry->value);
        }
        if (value->v.dict.entries != NULL) {
            free(value->v.dict.entries);
        }
        break;
    }

    free(value);
}
