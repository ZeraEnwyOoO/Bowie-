/*
 * Wingo — P2P Internet Sharing Tool
 * Copyright (C) 2024 Wingo Project
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

#ifndef WINGO_UTIL_HASHMAP_H
#define WINGO_UTIL_HASHMAP_H

/*
 * ============================================================================
 * WINGO HASH MAP
 * ============================================================================
 *
 * This header provides:
 *   - Hash map (string keys)
 *   - Hash map (binary keys)
 *   - Hash map (integer keys)
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * HASH MAP (STRING KEYS)
 * ============================================================================ */

/*
 * Hash map entry.
 */

typedef struct wingo_hashmap_entry {
    char *key;
    void *value;
    struct wingo_hashmap_entry *next;
} wingo_hashmap_entry_t;

/*
 * Hash map.
 */

typedef struct {
    wingo_hashmap_entry_t **buckets;
    wingo_size capacity;
    wingo_size count;
    void (*free_key)(void *key);
    void (*free_value)(void *value);
} wingo_hashmap_t;

/*
 * Create a new hash map.
 *
 * @param capacity  Initial capacity (0 for default)
 * @param free_key  Function to free keys (NULL if not needed)
 * @param free_value Function to free values (NULL if not needed)
 * @return          New hash map, or NULL on error
 */

wingo_hashmap_t *wingo_hashmap_new(wingo_size capacity,
                                   void (*free_key)(void *key),
                                   void (*free_value)(void *value));

/*
 * Free a hash map.
 *
 * @param map       Hash map to free
 */

void wingo_hashmap_free(wingo_hashmap_t *map);

/*
 * Set a key-value pair.
 *
 * @param map       Hash map
 * @param key       Key (string)
 * @param value     Value
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_hashmap_set(wingo_hashmap_t *map, const char *key, void *value);

/*
 * Get a value by key.
 *
 * @param map       Hash map
 * @param key       Key (string)
 * @return          Value, or NULL if not found
 */

void *wingo_hashmap_get(const wingo_hashmap_t *map, const char *key);

/*
 * Check if a key exists.
 *
 * @param map       Hash map
 * @param key       Key (string)
 * @return          true if exists, false otherwise
 */

bool wingo_hashmap_has(const wingo_hashmap_t *map, const char *key);

/*
 * Remove a key-value pair.
 *
 * @param map       Hash map
 * @param key       Key (string)
 * @return          Removed value, or NULL if not found
 */

void *wingo_hashmap_remove(wingo_hashmap_t *map, const char *key);

/*
 * Clear the hash map.
 *
 * @param map       Hash map
 */

void wingo_hashmap_clear(wingo_hashmap_t *map);

/*
 * Get hash map count.
 *
 * @param map       Hash map
 * @return          Number of entries
 */

static inline wingo_size wingo_hashmap_count(const wingo_hashmap_t *map)
{
    return map == NULL ? 0 : map->count;
}

/*
 * Iterate over a hash map.
 *
 * Usage:
 *   wingo_hashmap_entry_t *entry;
 *   WINGO_HASHMAP_FOREACH(map, entry) {
 *       printf("%s = %p\n", entry->key, entry->value);
 *   }
 */

#define WINGO_HASHMAP_FOREACH(map, entry) \
    for (wingo_size _i = 0; _i < (map)->capacity; _i++) \
        for ((entry) = (map)->buckets[_i]; (entry) != NULL; (entry) = (entry)->next)

/* ============================================================================
 * HASH MAP (BINARY KEYS)
 * ============================================================================ */

/*
 * Binary hash map entry.
 */

typedef struct wingo_hashmap_bin_entry {
    void *key;
    wingo_size key_len;
    void *value;
    struct wingo_hashmap_bin_entry *next;
} wingo_hashmap_bin_entry_t;

/*
 * Binary hash map.
 */

typedef struct {
    wingo_hashmap_bin_entry_t **buckets;
    wingo_size capacity;
    wingo_size count;
    void (*free_key)(void *key);
    void (*free_value)(void *value);
} wingo_hashmap_bin_t;

/*
 * Create a new binary hash map.
 *
 * @param capacity  Initial capacity (0 for default)
 * @param free_key  Function to free keys (NULL if not needed)
 * @param free_value Function to free values (NULL if not needed)
 * @return          New hash map, or NULL on error
 */

wingo_hashmap_bin_t *wingo_hashmap_bin_new(wingo_size capacity,
                                           void (*free_key)(void *key),
                                           void (*free_value)(void *value));

/*
 * Free a binary hash map.
 *
 * @param map       Hash map to free
 */

void wingo_hashmap_bin_free(wingo_hashmap_bin_t *map);

/*
 * Set a key-value pair.
 *
 * @param map       Hash map
 * @param key       Key (binary)
 * @param key_len   Key length
 * @param value     Value
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_hashmap_bin_set(wingo_hashmap_bin_t *map,
                                    const void *key, wingo_size key_len,
                                    void *value);

/*
 * Get a value by key.
 *
 * @param map       Hash map
 * @param key       Key (binary)
 * @param key_len   Key length
 * @return          Value, or NULL if not found
 */

void *wingo_hashmap_bin_get(const wingo_hashmap_bin_t *map,
                            const void *key, wingo_size key_len);

/*
 * Check if a key exists.
 *
 * @param map       Hash map
 * @param key       Key (binary)
 * @param key_len   Key length
 * @return          true if exists, false otherwise
 */

bool wingo_hashmap_bin_has(const wingo_hashmap_bin_t *map,
                           const void *key, wingo_size key_len);

/*
 * Remove a key-value pair.
 *
 * @param map       Hash map
 * @param key       Key (binary)
 * @param key_len   Key length
 * @return          Removed value, or NULL if not found
 */

void *wingo_hashmap_bin_remove(wingo_hashmap_bin_t *map,
                               const void *key, wingo_size key_len);

/*
 * Clear the hash map.
 *
 * @param map       Hash map
 */

void wingo_hashmap_bin_clear(wingo_hashmap_bin_t *map);

/*
 * Get hash map count.
 *
 * @param map       Hash map
 * @return          Number of entries
 */

static inline wingo_size wingo_hashmap_bin_count(const wingo_hashmap_bin_t *map)
{
    return map == NULL ? 0 : map->count;
}

/* ============================================================================
 * HASH FUNCTIONS
 * ============================================================================ */

/*
 * DJB2 hash function for strings.
 *
 * @param str       String to hash
 * @return          Hash value
 */

wingo_u32 wingo_hash_str(const char *str);

/*
 * FNV-1a hash function for binary data.
 *
 * @param data      Data to hash
 * @param len       Length of data
 * @return          Hash value
 */

wingo_u32 wingo_hash_data(const void *data, wingo_size len);

/*
 * FNV-1a hash function for 32-bit integer.
 *
 * @param value     Value to hash
 * @return          Hash value
 */

wingo_u32 wingo_hash_u32(wingo_u32 value);

/*
 * FNV-1a hash function for 64-bit integer.
 *
 * @param value     Value to hash
 * @return          Hash value
 */

wingo_u32 wingo_hash_u64(wingo_u64 value);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_HASHMAP_H */

