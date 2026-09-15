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
/* ============================================================================
 * QUERY
 * ============================================================================ */

/*
 * Get Bencode value type.
 */
wingo_bencode_type_t wingo_bencode_type(const wingo_bencode_t *value)
{
    if (value == NULL) {
        return WINGO_BENCODE_INTEGER;  /* Safe default */
    }

    return value->type;
}

/*
 * Check if Bencode value is integer.
 */
bool wingo_bencode_is_integer(const wingo_bencode_t *value)
{
    return value != NULL && value->type == WINGO_BENCODE_INTEGER;
}

/*
 * Check if Bencode value is string.
 */
bool wingo_bencode_is_string(const wingo_bencode_t *value)
{
    return value != NULL && value->type == WINGO_BENCODE_STRING;
}

/*
 * Check if Bencode value is list.
 */
bool wingo_bencode_is_list(const wingo_bencode_t *value)
{
    return value != NULL && value->type == WINGO_BENCODE_LIST;
}

/*
 * Check if Bencode value is dict.
 */
bool wingo_bencode_is_dict(const wingo_bencode_t *value)
{
    return value != NULL && value->type == WINGO_BENCODE_DICT;
}

/* ============================================================================
 * INTEGER
 * ============================================================================ */

/*
 * Create a Bencode integer.
 */
wingo_bencode_t *wingo_bencode_new_integer(wingo_i64 value)
{
    wingo_bencode_t *v = bencode_alloc(WINGO_BENCODE_INTEGER);
    if (v == NULL) {
        return NULL;
    }

    v->v.integer = value;
    return v;
}

/*
 * Get integer value.
 */
wingo_error_t wingo_bencode_get_integer(const wingo_bencode_t *value,
                                         wingo_i64 *out)
{
    if (value == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (value->type != WINGO_BENCODE_INTEGER) {
        return WINGO_ERR_INVALID_ARG;
    }

    *out = value->v.integer;
    return WINGO_SUCCESS;
}

/* ============================================================================
 * STRING
 * ============================================================================ */

/*
 * Create a Bencode string.
 */
wingo_bencode_t *wingo_bencode_new_string(const void *data, wingo_size len)
{
    wingo_bencode_t *v;

    if (data == NULL && len > 0) {
        return NULL;
    }

    if (len > BENCODE_MAX_STRING_LEN) {
        return NULL;
    }

    v = bencode_alloc(WINGO_BENCODE_STRING);
    if (v == NULL) {
        return NULL;
    }

    if (len > 0) {
        v->v.string.data = malloc(len);
        if (v->v.string.data == NULL) {
            free(v);
            return NULL;
        }
        memcpy(v->v.string.data, data, len);
    } else {
        v->v.string.data = NULL;
    }

    v->v.string.len = len;
    return v;
}

/*
 * Create a Bencode string from C string.
 */
wingo_bencode_t *wingo_bencode_new_cstring(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    return wingo_bencode_new_string(str, strlen(str));
}

/*
 * Get string data.
 */
wingo_error_t wingo_bencode_get_string(const wingo_bencode_t *value,
                                        const void **out,
                                        wingo_size *len)
{
    if (value == NULL || out == NULL || len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (value->type != WINGO_BENCODE_STRING) {
        return WINGO_ERR_INVALID_ARG;
    }

    *out = value->v.string.data;
    *len = value->v.string.len;
    return WINGO_SUCCESS;
}

/*
 * Get string as C string (null-terminated).
 */
wingo_error_t wingo_bencode_get_cstring(const wingo_bencode_t *value,
                                         char *buf,
                                         wingo_size size)
{
    wingo_size copy_len;

    if (value == NULL || buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (value->type != WINGO_BENCODE_STRING) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Need at least len + 1 for null terminator */
    if (value->v.string.len >= size) {
        return WINGO_ERR_OVERFLOW;
    }

    copy_len = value->v.string.len;

    if (copy_len > 0 && value->v.string.data != NULL) {
        memcpy(buf, value->v.string.data, copy_len);
    }

    buf[copy_len] = '\0';

    return WINGO_SUCCESS;
}

/*
 * Compare string with another string.
 */
bool wingo_bencode_string_eq(const wingo_bencode_t *value,
                              const void *str,
                              wingo_size len)
{
    if (value == NULL || str == NULL) {
        return false;
    }

    if (value->type != WINGO_BENCODE_STRING) {
        return false;
    }

    if (value->v.string.len != len) {
        return false;
    }

    if (len == 0) {
        return true;
    }

    return memcmp(value->v.string.data, str, len) == 0;
}

/* ============================================================================
 * LIST
 * ============================================================================ */

/*
 * Create an empty Bencode list.
 */
wingo_bencode_t *wingo_bencode_new_list(void)
{
    return bencode_alloc(WINGO_BENCODE_LIST);
}

/*
 * Append a value to list.
 *
 * Ownership of value transfers to the list.
 */
wingo_error_t wingo_bencode_list_append(wingo_bencode_t *list,
                                         wingo_bencode_t *value)
{
    if (list == NULL || value == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (list->type != WINGO_BENCODE_LIST) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Grow if needed */
    if (list->v.list.count >= list->v.list.capacity) {
        wingo_error_t rc = bencode_list_grow(list);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    list->v.list.items[list->v.list.count++] = value;

    return WINGO_SUCCESS;
}

/*
 * Get list length.
 */
wingo_size wingo_bencode_list_len(const wingo_bencode_t *list)
{
    if (list == NULL || list->type != WINGO_BENCODE_LIST) {
        return 0;
    }

    return list->v.list.count;
}

/*
 * Get list item at index.
 */
const wingo_bencode_t *wingo_bencode_list_get(const wingo_bencode_t *list,
                                               wingo_size index)
{
    if (list == NULL || list->type != WINGO_BENCODE_LIST) {
        return NULL;
    }

    if (index >= list->v.list.count) {
        return NULL;
    }

    return list->v.list.items[index];
}

/* ============================================================================
 * DICTIONARY
 * ============================================================================ */

/*
 * Create an empty Bencode dictionary.
 */
wingo_bencode_t *wingo_bencode_new_dict(void)
{
    return bencode_alloc(WINGO_BENCODE_DICT);
}

/*
 * Find a dict entry by key.
 */
static bencode_dict_entry_t *bencode_dict_find(wingo_bencode_t *dict,
                                                const char *key)
{
    wingo_size i;
    wingo_size key_len;

    if (dict == NULL || key == NULL) {
        return NULL;
    }

    key_len = strlen(key);

    for (i = 0; i < dict->v.dict.count; i++) {
        bencode_dict_entry_t *entry = &dict->v.dict.entries[i];

        if (entry->key_len == key_len &&
            memcmp(entry->key, key, key_len) == 0) {
            return entry;
        }
    }

    return NULL;
}

/*
 * Set a key-value pair in dictionary.
 *
 * If key already exists, the old value is freed and replaced.
 * Ownership of value transfers to the dict.
 */
wingo_error_t wingo_bencode_dict_set(wingo_bencode_t *dict,
                                      const char *key,
                                      wingo_bencode_t *value)
{
    bencode_dict_entry_t *entry;
    wingo_size key_len;
    char *key_copy;

    if (dict == NULL || key == NULL || value == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (dict->type != WINGO_BENCODE_DICT) {
        return WINGO_ERR_INVALID_ARG;
    }

    key_len = strlen(key);

    /* Check if key already exists */
    entry = bencode_dict_find(dict, key);
    if (entry != NULL) {
        /* Replace value */
        wingo_bencode_free(entry->value);
        entry->value = value;
        return WINGO_SUCCESS;
    }

    /* Grow if needed */
    if (dict->v.dict.count >= dict->v.dict.capacity) {
        wingo_error_t rc = bencode_dict_grow(dict);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    /* Copy key */
    key_copy = malloc(key_len + 1);
    if (key_copy == NULL) {
        return WINGO_ERR_NOMEM;
    }

    memcpy(key_copy, key, key_len);
    key_copy[key_len] = '\0';

    /* Add entry */
    entry = &dict->v.dict.entries[dict->v.dict.count];
    entry->key = key_copy;
    entry->key_len = key_len;
    entry->value = value;

    dict->v.dict.count++;

    return WINGO_SUCCESS;
}

/*
 * Get a value from dictionary.
 */
const wingo_bencode_t *wingo_bencode_dict_get(const wingo_bencode_t *dict,
                                               const char *key)
{
    bencode_dict_entry_t *entry;

    if (dict == NULL || key == NULL) {
        return NULL;
    }

    if (dict->type != WINGO_BENCODE_DICT) {
        return NULL;
    }

    entry = bencode_dict_find((wingo_bencode_t *)dict, key);
    if (entry == NULL) {
        return NULL;
    }

    return entry->value;
}

/*
 * Check if dictionary has a key.
 */
bool wingo_bencode_dict_has(const wingo_bencode_t *dict,
                             const char *key)
{
    if (dict == NULL || key == NULL) {
        return false;
    }

    if (dict->type != WINGO_BENCODE_DICT) {
        return false;
    }

    return bencode_dict_find((wingo_bencode_t *)dict, key) != NULL;
}

/*
 * Get dictionary size.
 */
wingo_size wingo_bencode_dict_len(const wingo_bencode_t *dict)
{
    if (dict == NULL || dict->type != WINGO_BENCODE_DICT) {
        return 0;
    }

    return dict->v.dict.count;
}

/*
 * Get dictionary key at index.
 */
const char *wingo_bencode_dict_key(const wingo_bencode_t *dict,
                                    wingo_size index)
{
    if (dict == NULL || dict->type != WINGO_BENCODE_DICT) {
        return NULL;
    }

    if (index >= dict->v.dict.count) {
        return NULL;
    }

    return dict->v.dict.entries[index].key;
}

/*
 * Get dictionary value at index.
 */
const wingo_bencode_t *wingo_bencode_dict_value(const wingo_bencode_t *dict,
                                                 wingo_size index)
{
    if (dict == NULL || dict->type != WINGO_BENCODE_DICT) {
        return NULL;
    }

    if (index >= dict->v.dict.count) {
        return NULL;
    }

    return dict->v.dict.entries[index].value;
}

/* ============================================================================
 * ENCODE
 * ============================================================================ */

/*
 * Forward declaration.
 */
static wingo_error_t bencode_encode_internal(const wingo_bencode_t *value,
                                              wingo_buf_t *buf,
                                              int depth);

/*
 * Encode an integer.
 */
static wingo_error_t bencode_encode_integer(const wingo_bencode_t *value,
                                             wingo_buf_t *buf)
{
    char tmp[32];
    int n;

    n = snprintf(tmp, sizeof(tmp), "i%llde", (long long)value->v.integer);
    if (n < 0 || (wingo_size)n >= sizeof(tmp)) {
        return WINGO_ERR_OVERFLOW;
    }

    return wingo_buf_append(buf, tmp, (wingo_size)n);
}

/*
 * Encode a string.
 */
static wingo_error_t bencode_encode_string(const wingo_bencode_t *value,
                                            wingo_buf_t *buf)
{
    char len_str[32];
    int n;
    wingo_error_t rc;

    /* Write length */
    n = snprintf(len_str, sizeof(len_str), "%zu:", value->v.string.len);
    if (n < 0 || (wingo_size)n >= sizeof(len_str)) {
        return WINGO_ERR_OVERFLOW;
    }

    rc = wingo_buf_append(buf, len_str, (wingo_size)n);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Write data */
    if (value->v.string.len > 0) {
        rc = wingo_buf_append(buf, value->v.string.data, value->v.string.len);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Encode a list.
 */
static wingo_error_t bencode_encode_list(const wingo_bencode_t *value,
                                          wingo_buf_t *buf,
                                          int depth)
{
    wingo_size i;
    wingo_error_t rc;

    /* Write 'l' */
    rc = wingo_buf_append_byte(buf, 'l');
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Encode items */
    for (i = 0; i < value->v.list.count; i++) {
        rc = bencode_encode_internal(value->v.list.items[i], buf, depth + 1);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    /* Write 'e' */
    return wingo_buf_append_byte(buf, 'e');
}

/*
 * Compare dict entries for sorting (canonical bencode requires
 * sorted keys).
 */
static int bencode_dict_entry_cmp(const void *a, const void *b)
{
    const bencode_dict_entry_t *ea = (const bencode_dict_entry_t *)a;
    const bencode_dict_entry_t *eb = (const bencode_dict_entry_t *)b;

    wingo_size min_len = ea->key_len < eb->key_len ? ea->key_len : eb->key_len;
    int rc;

    rc = memcmp(ea->key, eb->key, min_len);
    if (rc != 0) {
        return rc;
    }

    if (ea->key_len < eb->key_len) return -1;
    if (ea->key_len > eb->key_len) return 1;
    return 0;
}

/*
 * Encode a dict.
 *
 * Keys must be sorted lexicographically for canonical encoding.
 */
static wingo_error_t bencode_encode_dict(const wingo_bencode_t *value,
                                          wingo_buf_t *buf,
                                          int depth)
{
    wingo_error_t rc;
    bencode_dict_entry_t *sorted = NULL;
    wingo_size i;

    /* Write 'd' */
    rc = wingo_buf_append_byte(buf, 'd');
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Sort entries (canonical encoding) */
    if (value->v.dict.count > 1) {
        sorted = malloc(value->v.dict.count * sizeof(bencode_dict_entry_t));
        if (sorted == NULL) {
            return WINGO_ERR_NOMEM;
        }

        memcpy(sorted, value->v.dict.entries,
               value->v.dict.count * sizeof(bencode_dict_entry_t));

        qsort(sorted, value->v.dict.count,
              sizeof(bencode_dict_entry_t), bencode_dict_entry_cmp);
    } else {
        sorted = (bencode_dict_entry_t *)value->v.dict.entries;
    }

    /* Encode entries */
    for (i = 0; i < value->v.dict.count; i++) {
        /* Encode key as string */
        wingo_bencode_t key_value;
        memset(&key_value, 0, sizeof(key_value));
        key_value.type = WINGO_BENCODE_STRING;
        key_value.v.string.data = sorted[i].key;
        key_value.v.string.len = sorted[i].key_len;

        rc = bencode_encode_string(&key_value, buf);
        if (rc != WINGO_SUCCESS) {
            if (sorted != value->v.dict.entries) {
                free(sorted);
            }
            return rc;
        }

        /* Encode value */
        rc = bencode_encode_internal(sorted[i].value, buf, depth + 1);
        if (rc != WINGO_SUCCESS) {
            if (sorted != value->v.dict.entries) {
                free(sorted);
            }
            return rc;
        }
    }

    if (sorted != value->v.dict.entries) {
        free(sorted);
    }

    /* Write 'e' */
    return wingo_buf_append_byte(buf, 'e');
}

/*
 * Internal encode with depth tracking.
 */
static wingo_error_t bencode_encode_internal(const wingo_bencode_t *value,
                                              wingo_buf_t *buf,
                                              int depth)
{
    if (value == NULL || buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (depth > BENCODE_MAX_DEPTH) {
        return WINGO_ERR_OVERFLOW;
    }

    switch (value->type) {
    case WINGO_BENCODE_INTEGER:
        return bencode_encode_integer(value, buf);

    case WINGO_BENCODE_STRING:
        return bencode_encode_string(value, buf);

    case WINGO_BENCODE_LIST:
        return bencode_encode_list(value, buf, depth);

    case WINGO_BENCODE_DICT:
        return bencode_encode_dict(value, buf, depth);

    default:
        return WINGO_ERR_INVALID_ARG;
    }
}

/*
 * Encode a Bencode value to buffer.
 */
wingo_error_t wingo_bencode_encode(const wingo_bencode_t *value,
                                    wingo_buf_t *buf)
{
    if (value == NULL || buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return bencode_encode_internal(value, buf, 0);
}

/*
 * Encode a Bencode value to newly allocated buffer.
 */
wingo_buf_t *wingo_bencode_encode_new(const wingo_bencode_t *value)
{
    wingo_buf_t *buf;
    wingo_error_t rc;

    if (value == NULL) {
        return NULL;
    }

    buf = wingo_buf_new(256);
    if (buf == NULL) {
        return NULL;
    }

    rc = wingo_bencode_encode(value, buf);
    if (rc != WINGO_SUCCESS) {
        wingo_buf_free(buf);
        return NULL;
    }

    return buf;
}

/*
 * Get encoded length of a Bencode value.
 *
 * This is computed by encoding to a temporary buffer.
 */
wingo_size wingo_bencode_encoded_len(const wingo_bencode_t *value)
{
    wingo_buf_t *buf;
    wingo_size len;

    if (value == NULL) {
        return 0;
    }

    buf = wingo_bencode_encode_new(value);
    if (buf == NULL) {
        return 0;
    }

    len = buf->len;
    wingo_buf_free(buf);

    return len;
}

/* ============================================================================
 * CONVENIENCE
 * ============================================================================ */

/*
 * Create a Bencode integer and set in dictionary.
 */
wingo_error_t wingo_bencode_dict_set_integer(wingo_bencode_t *dict,
                                              const char *key,
                                              wingo_i64 value)
{
    wingo_bencode_t *v;

    if (dict == NULL || key == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    v = wingo_bencode_new_integer(value);
    if (v == NULL) {
        return WINGO_ERR_NOMEM;
    }

    return wingo_bencode_dict_set(dict, key, v);
}

/*
 * Create a Bencode string and set in dictionary.
 */
wingo_error_t wingo_bencode_dict_set_string(wingo_bencode_t *dict,
                                             const char *key,
                                             const void *data,
                                             wingo_size len)
{
    wingo_bencode_t *v;

    if (dict == NULL || key == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    v = wingo_bencode_new_string(data, len);
    if (v == NULL) {
        return WINGO_ERR_NOMEM;
    }

    return wingo_bencode_dict_set(dict, key, v);
}

/*
 * Get integer from dictionary.
 */
wingo_error_t wingo_bencode_dict_get_integer(const wingo_bencode_t *dict,
                                              const char *key,
                                              wingo_i64 *out)
{
    const wingo_bencode_t *v;

    if (dict == NULL || key == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    v = wingo_bencode_dict_get(dict, key);
    if (v == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    return wingo_bencode_get_integer(v, out);
}

/*
 * Get string from dictionary.
 */
wingo_error_t wingo_bencode_dict_get_string(const wingo_bencode_t *dict,
                                             const char *key,
                                             const void **out,
                                             wingo_size *len)
{
    const wingo_bencode_t *v;

    if (dict == NULL || key == NULL || out == NULL || len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    v = wingo_bencode_dict_get(dict, key);
    if (v == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    return wingo_bencode_get_string(v, out, len);
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

/*
 * Print Bencode value (for debugging).
 */
void wingo_bencode_print(const wingo_bencode_t *value, FILE *f, int indent)
{
    wingo_size i;
    int j;

    if (f == NULL) {
        f = stderr;
    }

    if (value == NULL) {
        fprintf(f, "%*s(null)\n", indent, "");
        return;
    }

    switch (value->type) {
    case WINGO_BENCODE_INTEGER:
        fprintf(f, "%*s%d\n", indent, "", (int)value->v.integer);
        break;

    case WINGO_BENCODE_STRING:
        fprintf(f, "%*s\"", indent, "");
        for (i = 0; i < value->v.string.len; i++) {
            u_char c = ((u_char *)value->v.string.data)[i];
            if (isprint(c)) {
                fputc(c, f);
            } else {
                fprintf(f, "\\x%02x", c);
            }
        }
        fprintf(f, "\" (%zu bytes)\n", value->v.string.len);
        break;

    case WINGO_BENCODE_LIST:
        fprintf(f, "%*s[\n", indent, "");
        for (i = 0; i < value->v.list.count; i++) {
            wingo_bencode_print(value->v.list.items[i], f, indent + 2);
        }
        fprintf(f, "%*s]\n", indent, "");
        break;

    case WINGO_BENCODE_DICT:
        fprintf(f, "%*s{\n", indent, "");
        for (i = 0; i < value->v.dict.count; i++) {
            fprintf(f, "%*s\"%s\":\n", indent + 2, "",
                    value->v.dict.entries[i].key);
            wingo_bencode_print(value->v.dict.entries[i].value, f, indent + 4);
        }
        fprintf(f, "%*s}\n", indent, "");
        break;

    default:
        fprintf(f, "%*s(unknown type %d)\n", indent, "", value->type);
        break;
    }

    (void)j;
}

/*
 * Duplicate a Bencode value.
 */
wingo_bencode_t *wingo_bencode_dup(const wingo_bencode_t *value)
{
    wingo_bencode_t *copy;
    wingo_size i;

    if (value == NULL) {
        return NULL;
    }

    switch (value->type) {
    case WINGO_BENCODE_INTEGER:
        return wingo_bencode_new_integer(value->v.integer);

    case WINGO_BENCODE_STRING:
        return wingo_bencode_new_string(value->v.string.data,
                                         value->v.string.len);

    case WINGO_BENCODE_LIST:
        copy = wingo_bencode_new_list();
        if (copy == NULL) {
            return NULL;
        }

        for (i = 0; i < value->v.list.count; i++) {
            wingo_bencode_t *item = wingo_bencode_dup(value->v.list.items[i]);
            if (item == NULL) {
                wingo_bencode_free(copy);
                return NULL;
            }

            if (wingo_bencode_list_append(copy, item) != WINGO_SUCCESS) {
                wingo_bencode_free(item);
                wingo_bencode_free(copy);
                return NULL;
            }
        }

        return copy;

    case WINGO_BENCODE_DICT:
        copy = wingo_bencode_new_dict();
        if (copy == NULL) {
            return NULL;
        }

        for (i = 0; i < value->v.dict.count; i++) {
            bencode_dict_entry_t *entry = &value->v.dict.entries[i];
            wingo_bencode_t *item = wingo_bencode_dup(entry->value);

            if (item == NULL) {
                wingo_bencode_free(copy);
                return NULL;
            }

            if (wingo_bencode_dict_set(copy, entry->key, item) != WINGO_SUCCESS) {
                wingo_bencode_free(item);
                wingo_bencode_free(copy);
                return NULL;
            }
        }

        return copy;

    default:
        return NULL;
    }
}

/*
 * Compare two Bencode values.
 */
bool wingo_bencode_eq(const wingo_bencode_t *a, const wingo_bencode_t *b)
{
    wingo_size i;

    if (a == NULL && b == NULL) {
        return true;
    }

    if (a == NULL || b == NULL) {
        return false;
    }

    if (a->type != b->type) {
        return false;
    }

    switch (a->type) {
    case WINGO_BENCODE_INTEGER:
        return a->v.integer == b->v.integer;

    case WINGO_BENCODE_STRING:
        if (a->v.string.len != b->v.string.len) {
            return false;
        }
        if (a->v.string.len == 0) {
            return true;
        }
        return memcmp(a->v.string.data, b->v.string.data,
                      a->v.string.len) == 0;

    case WINGO_BENCODE_LIST:
        if (a->v.list.count != b->v.list.count) {
            return false;
        }
        for (i = 0; i < a->v.list.count; i++) {
            if (!wingo_bencode_eq(a->v.list.items[i],
                                   b->v.list.items[i])) {
                return false;
            }
        }
        return true;

    case WINGO_BENCODE_DICT:
        if (a->v.dict.count != b->v.dict.count) {
            return false;
        }
        for (i = 0; i < a->v.dict.count; i++) {
            const wingo_bencode_t *va;
            const wingo_bencode_t *vb;

            va = wingo_bencode_dict_get(a, a->v.dict.entries[i].key);
            vb = wingo_bencode_dict_get(b, a->v.dict.entries[i].key);

            if (!wingo_bencode_eq(va, vb)) {
                return false;
            }
        }
        return true;

    default:
        return false;
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
