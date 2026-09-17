
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
#define _POSIX_C_SOURCE 200809L

#include "wingo/util/hashmap.h"

#include <string.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Default initial capacity for new hash maps.
 * 16 buckets is a common starting size — good balance between
 * memory usage and collision rate for small maps.
 */
#define HASHMAP_DEFAULT_CAPACITY    16

/*
 * Maximum capacity. Prevents overflow and huge allocations.
 */
#define HASHMAP_MAX_CAPACITY        (1U << 30)  /* 1 billion buckets */

/*
 * Load factor threshold for resizing.
 *
 * When count / capacity exceeds this, we resize.
 * 0.75 is the standard choice (used by Java HashMap, etc.) —
 * good balance between time and space.
 */
#define HASHMAP_LOAD_FACTOR_NUM     3
#define HASHMAP_LOAD_FACTOR_DEN     4

/*
 * Growth factor when resizing.
 * Doubling is standard — gives amortized O(1) insert.
 */
#define HASHMAP_GROWTH_FACTOR       2

/* ============================================================================
 * HASH FUNCTIONS
 * ============================================================================ */

/*
 * DJB2 hash function for strings.
 *
 * This is Dan Bernstein's classic hash. Simple, fast, and
 * surprisingly good distribution for typical string keys.
 *
 * Algorithm:
 *   hash = 5381
 *   for each byte:
 *       hash = hash * 33 + byte
 */
wingo_u32 wingo_hash_str(const char *str)
{
    wingo_u32 hash = 5381;
    int c;

    if (str == NULL) {
        return 0;
    }

    while ((c = (unsigned char)*str++) != 0) {
        hash = ((hash << 5) + hash) + (wingo_u32)c;  /* hash * 33 + c */
    }

    return hash;
}

/*
 * FNV-1a hash function for binary data.
 *
 * Fowler-Noll-Vo hash, variant 1a. Excellent distribution for
 * short binary keys (like 20-byte DHT IDs).
 *
 * Algorithm:
 *   hash = 2166136261
 *   for each byte:
 *       hash = hash XOR byte
 *       hash = hash * 16777619
 */
wingo_u32 wingo_hash_data(const void *data, wingo_size len)
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    wingo_u32 hash = 2166136261U;  /* FNV offset basis */
    wingo_size i;

    if (data == NULL) {
        return 0;
    }

    for (i = 0; i < len; i++) {
        hash ^= (wingo_u32)bytes[i];
        hash *= 16777619U;  /* FNV prime */
    }

    return hash;
}

/*
 * FNV-1a hash for 32-bit integer.
 */
wingo_u32 wingo_hash_u32(wingo_u32 value)
{
    return wingo_hash_data(&value, sizeof(value));
}

/*
 * FNV-1a hash for 64-bit integer.
 */
wingo_u32 wingo_hash_u64(wingo_u64 value)
{
    return wingo_hash_data(&value, sizeof(value));
}

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Check if two string keys are equal.
 */
static bool key_str_equal(const char *a, const char *b)
{
    if (a == b) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }
    return strcmp(a, b) == 0;
}

/*
 * Check if two binary keys are equal.
 */
static bool key_bin_equal(const void *a, wingo_size a_len,
                          const void *b, wingo_size b_len)
{
    if (a_len != b_len) {
        return false;
    }
    if (a == b) {
        return true;
    }
    if (a == NULL || b == NULL) {
        return false;
    }
    return memcmp(a, b, a_len) == 0;
}

/*
 * Calculate bucket index from hash and capacity.
 *
 * We use bitwise AND instead of modulo because capacity is
 * always a power of two. This is faster.
 */
static wingo_size hash_to_index(wingo_u32 hash, wingo_size capacity)
{
    return (wingo_size)hash & (capacity - 1);
}

/*
 * Check if map needs to grow based on load factor.
 */
static bool hashmap_needs_grow(const wingo_hashmap_t *map)
{
    if (map == NULL || map->capacity == 0) {
        return false;
    }

    /*
     * count / capacity > LOAD_FACTOR
     * Equivalent to: count * DEN > capacity * NUM
     */
    return (map->count * HASHMAP_LOAD_FACTOR_DEN) >
           (map->capacity * HASHMAP_LOAD_FACTOR_NUM);
}

/*
 * Grow the hash map (rehash all entries).
 *
 * This is called when load factor is exceeded.
 * We double capacity and rehash all entries.
 */
static wingo_error_t hashmap_grow(wingo_hashmap_t *map)
{
    wingo_hashmap_entry_t **new_buckets;
    wingo_hashmap_entry_t *entry, *next;
    wingo_size new_capacity;
    wingo_size i, new_index;
    wingo_u32 hash;

    if (map == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Calculate new capacity */
    new_capacity = map->capacity * HASHMAP_GROWTH_FACTOR;
    if (new_capacity < map->capacity) {
        /* Overflow */
        return WINGO_ERR_OVERFLOW;
    }
    if (new_capacity > HASHMAP_MAX_CAPACITY) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Allocate new bucket array */
    new_buckets = calloc(new_capacity, sizeof(wingo_hashmap_entry_t *));
    if (new_buckets == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /*
     * Rehash all existing entries.
     *
     * For each old bucket, walk the chain and move each entry
     * to the correct new bucket.
     */
    for (i = 0; i < map->capacity; i++) {
        entry = map->buckets[i];

        while (entry != NULL) {
            next = entry->next;

            /* Compute new index */
            hash = wingo_hash_str(entry->key);
            new_index = hash_to_index(hash, new_capacity);

            /* Insert at head of new bucket chain */
            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next;
        }
    }

    /* Free old bucket array (not the entries!) */
    free(map->buckets);

    map->buckets  = new_buckets;
    map->capacity = new_capacity;

    return WINGO_SUCCESS;
}

/*
 * Free a single entry.
 */
static void hashmap_entry_free(wingo_hashmap_t *map, wingo_hashmap_entry_t *entry)
{
    if (entry == NULL) {
        return;
    }

    if (entry->key != NULL) {
        if (map->free_key != NULL) {
            map->free_key(entry->key);
        } else {
            free(entry->key);
        }
    }

    if (entry->value != NULL && map->free_value != NULL) {
        map->free_value(entry->value);
    }

    free(entry);
}

/* ============================================================================
 * STRING-KEY HASH MAP
 * ============================================================================ */

wingo_hashmap_t *wingo_hashmap_new(wingo_size capacity,
                                   void (*free_key)(void *key),
                                   void (*free_value)(void *value))
{
    wingo_hashmap_t *map;

    /* Use default if 0 */
    if (capacity == 0) {
        capacity = HASHMAP_DEFAULT_CAPACITY;
    }

    /* Round up to power of two */
    if (!WINGO_IS_POWER_OF_TWO(capacity)) {
        wingo_size pow2 = 1;
        while (pow2 < capacity) {
            pow2 <<= 1;
        }
        capacity = pow2;
    }

    if (capacity > HASHMAP_MAX_CAPACITY) {
        return NULL;
    }

    /* Allocate map struct */
    map = calloc(1, sizeof(wingo_hashmap_t));
    if (map == NULL) {
        return NULL;
    }

    /* Allocate bucket array */
    map->buckets = calloc(capacity, sizeof(wingo_hashmap_entry_t *));
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }

    map->capacity   = capacity;
    map->count      = 0;
    map->free_key   = free_key;
    map->free_value = free_value;

    return map;
}

void wingo_hashmap_free(wingo_hashmap_t *map)
{
    wingo_size i;
    wingo_hashmap_entry_t *entry, *next;

    if (map == NULL) {
        return;
    }

    /* Free all entries in all buckets */
    if (map->buckets != NULL) {
        for (i = 0; i < map->capacity; i++) {
            entry = map->buckets[i];
            while (entry != NULL) {
                next = entry->next;
                hashmap_entry_free(map, entry);
                entry = next;
            }
        }
        free(map->buckets);
    }

    free(map);
}

wingo_error_t wingo_hashmap_set(wingo_hashmap_t *map, const char *key, void *value)
{
    wingo_hashmap_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;
    char *key_copy;

    if (map == NULL || key == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Compute hash and index */
    hash  = wingo_hash_str(key);
    index = hash_to_index(hash, map->capacity);

    /*
     * Check if key already exists.
     * If so, update value in place.
     */
    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_str_equal(entry->key, key)) {
            /* Key exists — replace value */
            if (entry->value != NULL && map->free_value != NULL) {
                map->free_value(entry->value);
            }
            entry->value = value;
            return WINGO_SUCCESS;
        }
        entry = entry->next;
    }

    /*
     * Key doesn't exist — check if we need to grow before inserting.
     */
    if (hashmap_needs_grow(map)) {
        wingo_error_t rc = hashmap_grow(map);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
        /* Recompute index after grow */
        index = hash_to_index(hash, map->capacity);
    }

    /* Copy the key (we own it) */
    key_copy = strdup(key);
    if (key_copy == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Create new entry */
    entry = calloc(1, sizeof(wingo_hashmap_entry_t));
    if (entry == NULL) {
        free(key_copy);
        return WINGO_ERR_NOMEM;
    }

    entry->key   = key_copy;
    entry->value = value;
    entry->next  = map->buckets[index];

    map->buckets[index] = entry;
    map->count++;

    return WINGO_SUCCESS;
}

void *wingo_hashmap_get(const wingo_hashmap_t *map, const char *key)
{
    wingo_hashmap_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;

    if (map == NULL || key == NULL) {
        return NULL;
    }

    hash  = wingo_hash_str(key);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_str_equal(entry->key, key)) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

bool wingo_hashmap_has(const wingo_hashmap_t *map, const char *key)
{
    wingo_hashmap_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;

    if (map == NULL || key == NULL) {
        return false;
    }

    hash  = wingo_hash_str(key);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_str_equal(entry->key, key)) {
            return true;
        }
        entry = entry->next;
    }

    return false;
}

void *wingo_hashmap_remove(wingo_hashmap_t *map, const char *key)
{
    wingo_hashmap_entry_t *entry, *prev;
    wingo_u32 hash;
    wingo_size index;
    void *value;

    if (map == NULL || key == NULL) {
        return NULL;
    }

    hash  = wingo_hash_str(key);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    prev  = NULL;

    while (entry != NULL) {
        if (key_str_equal(entry->key, key)) {
            /* Unlink from chain */
            if (prev == NULL) {
                map->buckets[index] = entry->next;
            } else {
                prev->next = entry->next;
            }

            /* Save value */
            value = entry->value;

            /* Free key */
            if (entry->key != NULL) {
                if (map->free_key != NULL) {
                    map->free_key(entry->key);
                } else {
                    free(entry->key);
                }
            }

            /* Free entry (but not value — we return it) */
            free(entry);

            map->count--;

            return value;
        }

        prev  = entry;
        entry = entry->next;
    }

    return NULL;
}

void wingo_hashmap_clear(wingo_hashmap_t *map)
{
    wingo_size i;
    wingo_hashmap_entry_t *entry, *next;

    if (map == NULL) {
        return;
    }

    for (i = 0; i < map->capacity; i++) {
        entry = map->buckets[i];
        while (entry != NULL) {
            next = entry->next;
            hashmap_entry_free(map, entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }

    map->count = 0;
}

/* ============================================================================
 * BINARY-KEY HASH MAP
 * ============================================================================ */

/*
 * Binary hash map entry is same struct as string entry,
 * but key is void* and we store key_len separately.
 *
 * We use a wrapper struct to store key_len alongside the entry.
 */
typedef struct {
    wingo_hashmap_bin_entry_t entry;
    /* key_len is already in entry */
} bin_entry_wrapper_t;

/*
 * Note: wingo_hashmap_bin_entry_t already has key_len field,
 * so we can use it directly without wrapper.
 *
 * We just need helper functions that use key_len.
 */

wingo_hashmap_bin_t *wingo_hashmap_bin_new(wingo_size capacity,
                                           void (*free_key)(void *key),
                                           void (*free_value)(void *value))
{
    wingo_hashmap_bin_t *map;

    if (capacity == 0) {
        capacity = HASHMAP_DEFAULT_CAPACITY;
    }

    if (!WINGO_IS_POWER_OF_TWO(capacity)) {
        wingo_size pow2 = 1;
        while (pow2 < capacity) {
            pow2 <<= 1;
        }
        capacity = pow2;
    }

    if (capacity > HASHMAP_MAX_CAPACITY) {
        return NULL;
    }

    map = calloc(1, sizeof(wingo_hashmap_bin_t));
    if (map == NULL) {
        return NULL;
    }

    map->buckets = calloc(capacity, sizeof(wingo_hashmap_bin_entry_t *));
    if (map->buckets == NULL) {
        free(map);
        return NULL;
    }

    map->capacity   = capacity;
    map->count      = 0;
    map->free_key   = free_key;
    map->free_value = free_value;

    return map;
}

void wingo_hashmap_bin_free(wingo_hashmap_bin_t *map)
{
    wingo_size i;
    wingo_hashmap_bin_entry_t *entry, *next;

    if (map == NULL) {
        return;
    }

    if (map->buckets != NULL) {
        for (i = 0; i < map->capacity; i++) {
            entry = map->buckets[i];
            while (entry != NULL) {
                next = entry->next;

                if (entry->key != NULL) {
                    if (map->free_key != NULL) {
                        map->free_key(entry->key);
                    } else {
                        free(entry->key);
                    }
                }

                if (entry->value != NULL && map->free_value != NULL) {
                    map->free_value(entry->value);
                }

                free(entry);
                entry = next;
            }
        }
        free(map->buckets);
    }

    free(map);
}

/*
 * Grow binary hash map.
 */
static wingo_error_t hashmap_bin_grow(wingo_hashmap_bin_t *map)
{
    wingo_hashmap_bin_entry_t **new_buckets;
    wingo_hashmap_bin_entry_t *entry, *next;
    wingo_size new_capacity;
    wingo_size i, new_index;
    wingo_u32 hash;

    if (map == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    new_capacity = map->capacity * HASHMAP_GROWTH_FACTOR;
    if (new_capacity < map->capacity) {
        return WINGO_ERR_OVERFLOW;
    }
    if (new_capacity > HASHMAP_MAX_CAPACITY) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    new_buckets = calloc(new_capacity, sizeof(wingo_hashmap_bin_entry_t *));
    if (new_buckets == NULL) {
        return WINGO_ERR_NOMEM;
    }

    for (i = 0; i < map->capacity; i++) {
        entry = map->buckets[i];

        while (entry != NULL) {
            next = entry->next;

            hash = wingo_hash_data(entry->key, entry->key_len);
            new_index = hash_to_index(hash, new_capacity);

            entry->next = new_buckets[new_index];
            new_buckets[new_index] = entry;

            entry = next;
        }
    }

    free(map->buckets);

    map->buckets  = new_buckets;
    map->capacity = new_capacity;

    return WINGO_SUCCESS;
}

/*
 * Check if binary map needs to grow.
 */
static bool hashmap_bin_needs_grow(const wingo_hashmap_bin_t *map)
{
    if (map == NULL || map->capacity == 0) {
        return false;
    }

    return (map->count * HASHMAP_LOAD_FACTOR_DEN) >
           (map->capacity * HASHMAP_LOAD_FACTOR_NUM);
}

wingo_error_t wingo_hashmap_bin_set(wingo_hashmap_bin_t *map,
                                    const void *key, wingo_size key_len,
                                    void *value)
{
    wingo_hashmap_bin_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;
    void *key_copy;

    if (map == NULL || key == NULL || key_len == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    hash  = wingo_hash_data(key, key_len);
    index = hash_to_index(hash, map->capacity);

    /* Check if key exists */
    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_bin_equal(entry->key, entry->key_len, key, key_len)) {
            /* Key exists — replace value */
            if (entry->value != NULL && map->free_value != NULL) {
                map->free_value(entry->value);
            }
            entry->value = value;
            return WINGO_SUCCESS;
        }
        entry = entry->next;
    }

    /* Grow if needed */
    if (hashmap_bin_needs_grow(map)) {
        wingo_error_t rc = hashmap_bin_grow(map);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
        index = hash_to_index(hash, map->capacity);
    }

    /* Copy key */
    key_copy = malloc(key_len);
    if (key_copy == NULL) {
        return WINGO_ERR_NOMEM;
    }
    memcpy(key_copy, key, key_len);

    /* Create entry */
    entry = calloc(1, sizeof(wingo_hashmap_bin_entry_t));
    if (entry == NULL) {
        free(key_copy);
        return WINGO_ERR_NOMEM;
    }

    entry->key     = key_copy;
    entry->key_len = key_len;
    entry->value   = value;
    entry->next    = map->buckets[index];

    map->buckets[index] = entry;
    map->count++;

    return WINGO_SUCCESS;
}

void *wingo_hashmap_bin_get(const wingo_hashmap_bin_t *map,
                            const void *key, wingo_size key_len)
{
    wingo_hashmap_bin_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;

    if (map == NULL || key == NULL || key_len == 0) {
        return NULL;
    }

    hash  = wingo_hash_data(key, key_len);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_bin_equal(entry->key, entry->key_len, key, key_len)) {
            return entry->value;
        }
        entry = entry->next;
    }

    return NULL;
}

bool wingo_hashmap_bin_has(const wingo_hashmap_bin_t *map,
                           const void *key, wingo_size key_len)
{
    wingo_hashmap_bin_entry_t *entry;
    wingo_u32 hash;
    wingo_size index;

    if (map == NULL || key == NULL || key_len == 0) {
        return false;
    }

    hash  = wingo_hash_data(key, key_len);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    while (entry != NULL) {
        if (key_bin_equal(entry->key, entry->key_len, key, key_len)) {
            return true;
        }
        entry = entry->next;
    }

    return false;
}

void *wingo_hashmap_bin_remove(wingo_hashmap_bin_t *map,
                               const void *key, wingo_size key_len)
{
    wingo_hashmap_bin_entry_t *entry, *prev;
    wingo_u32 hash;
    wingo_size index;
    void *value;

    if (map == NULL || key == NULL || key_len == 0) {
        return NULL;
    }

    hash  = wingo_hash_data(key, key_len);
    index = hash_to_index(hash, map->capacity);

    entry = map->buckets[index];
    prev  = NULL;

    while (entry != NULL) {
        if (key_bin_equal(entry->key, entry->key_len, key, key_len)) {
            /* Unlink */
            if (prev == NULL) {
                map->buckets[index] = entry->next;
            } else {
                prev->next = entry->next;
            }

            value = entry->value;

            /* Free key */
            if (entry->key != NULL) {
                if (map->free_key != NULL) {
                    map->free_key(entry->key);
                } else {
                    free(entry->key);
                }
            }

            free(entry);
            map->count--;

            return value;
        }

        prev  = entry;
        entry = entry->next;
    }

    return NULL;
}

void wingo_hashmap_bin_clear(wingo_hashmap_bin_t *map)
{
    wingo_size i;
    wingo_hashmap_bin_entry_t *entry, *next;

    if (map == NULL) {
        return;
    }

    for (i = 0; i < map->capacity; i++) {
        entry = map->buckets[i];
        while (entry != NULL) {
            next = entry->next;

            if (entry->key != NULL) {
                if (map->free_key != NULL) {
                    map->free_key(entry->key);
                } else {
                    free(entry->key);
                }
            }

            if (entry->value != NULL && map->free_value != NULL) {
                map->free_value(entry->value);
            }

            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }

    map->count = 0;
}
