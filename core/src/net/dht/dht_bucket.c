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
 * Kademlia bucket implementation for Bowie DHT.
 *
 * A bucket is a container for DHT nodes. Kademlia uses buckets
 * to organize nodes by XOR distance from our own node ID.
 *
 * Bucket invariants:
 *   - Nodes are stored in order of last seen (most recent first)
 *   - Bucket has a maximum size (K = 8 by default)
 *   - When full, new nodes may replace bad nodes
 *   - Bucket range is [first, next->first)
 *
 * Bucket splitting:
 *   - Buckets split when full and when the split point is in range
 *   - Splitting creates two buckets, each with half the range
 *   - Nodes are redistributed based on their ID
 *
 * Node state:
 *   - GOOD:    Replied within last 30 minutes
 *   - DUBIOUS: Pinged but no reply, or no reply for 15+ minutes
 *   - BAD:     Pinged 3+ times with no reply
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_bucket.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Bucket (concrete).
 */
struct wingo_dht_bucket {
    /* ----- Identity ----- */
    wingo_dht_id_t      first;          /* First ID in range */
    wingo_addr_family_t family;         /* Address family */

    /* ----- Nodes ----- */
    wingo_dht_node_t  **nodes;          /* Node array */
    wingo_size          count;          /* Number of nodes */
    wingo_size          capacity;       /* Allocated capacity */
    wingo_size          max_count;      /* Maximum nodes (K) */

    /* ----- Cached node ----- */
    wingo_dht_node_t   *cached;         /* Cached node (for replacement) */

    /* ----- Timestamps ----- */
    wingo_i64           last_changed;   /* Last modification time */

    /* ----- Linked list ----- */
    struct wingo_dht_bucket *next;      /* Next bucket */
    struct wingo_dht_bucket *prev;      /* Previous bucket */
};

/* ============================================================================
 * INTERNAL HELPERS — NODE MANAGEMENT
 * ============================================================================ */

/*
 * Find node index by ID.
 *
 * @return Index, or -1 if not found
 */
static int bucket_find_index(const wingo_dht_bucket_t *bucket,
                              const wingo_dht_id_t *id)
{
    wingo_size i;

    for (i = 0; i < bucket->count; i++) {
        if (wingo_dht_node_has_id(bucket->nodes[i], id)) {
            return (int)i;
        }
    }

    return -1;
}

/*
 * Check if bucket can add a node.
 *
 * Returns:
 *   1  = can add (not full)
 *   0  = full but can replace bad node
 *  -1  = full and cannot replace
 */
static int bucket_can_add(const wingo_dht_bucket_t *bucket)
{
    if (bucket->count < bucket->max_count) {
        return 1;
    }

    /* Full — check if we can replace a bad node */
    if (bucket->count > 0) {
        wingo_size i;
        for (i = 0; i < bucket->count; i++) {
            if (wingo_dht_node_is_bad(bucket->nodes[i])) {
                return 0;  /* Can replace */
            }
        }
    }

    return -1;  /* Cannot add */
}

/*
 * Remove node at index.
 */
static void bucket_remove_at(wingo_dht_bucket_t *bucket, wingo_size index)
{
    if (index >= bucket->count) {
        return;
    }

    /* Free node */
    wingo_dht_node_free(bucket->nodes[index]);

    /* Shift remaining nodes */
    if (index < bucket->count - 1) {
        memmove(&bucket->nodes[index],
                &bucket->nodes[index + 1],
                (bucket->count - index - 1) * sizeof(wingo_dht_node_t *));
    }

    bucket->count--;
    bucket->nodes[bucket->count] = NULL;
    bucket->last_changed = wingo_time_now();
}

/*
 * Find worst node (last in list).
 */
static wingo_size bucket_worst_index(const wingo_dht_bucket_t *bucket)
{
    /* Worst node is the last in the list (least recently seen) */
    if (bucket->count == 0) {
        return 0;
    }

    return bucket->count - 1;
}

/* ============================================================================
 * INTERNAL HELPERS — ID COMPARISON
 * ============================================================================ */

/*
 * Compute common prefix length between two IDs.
 *
 * Returns number of bits in common prefix (0-160).
 */
static int id_common_bits(const wingo_dht_id_t *a, const wingo_dht_id_t *b)
{
    wingo_size i;
    int common_bits = 0;

    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        wingo_u8 x = a->bytes[i] ^ b->bytes[i];

        if (x == 0) {
            common_bits += 8;
            continue;
        }

        /* Find highest set bit */
        while ((x & 0x80) == 0) {
            common_bits++;
            x <<= 1;
        }

        break;
    }

    return common_bits;
}

/*
 * Set the nth bit in an ID.
 *
 * @param id        ID
 * @param bit       Bit index (0 = MSB)
 * @param value     Bit value (0 or 1)
 */
static void id_set_bit(wingo_dht_id_t *id, int bit, int value)
{
    int byte_index;
    int bit_index;
    wingo_u8 mask;

    if (bit < 0 || bit >= (int)(WINGO_DHT_ID_SIZE * 8)) {
        return;
    }

    byte_index = bit / 8;
    bit_index = 7 - (bit % 8);  /* MSB first */
    mask = (wingo_u8)(1 << bit_index);

    if (value) {
        id->bytes[byte_index] |= mask;
    } else {
        id->bytes[byte_index] &= ~mask;
    }
}

/*
 * Get the nth bit in an ID.
 *
 * @param id        ID
 * @param bit       Bit index (0 = MSB)
 * @return          Bit value (0 or 1)
 */
static int id_get_bit(const wingo_dht_id_t *id, int bit)
{
    int byte_index;
    int bit_index;
    wingo_u8 mask;

    if (bit < 0 || bit >= (int)(WINGO_DHT_ID_SIZE * 8)) {
        return 0;
    }

    byte_index = bit / 8;
    bit_index = 7 - (bit % 8);
    mask = (wingo_u8)(1 << bit_index);

    return (id->bytes[byte_index] & mask) ? 1 : 0;
}

/* ============================================================================
 * INTERNAL HELPERS — MEMORY
 * ============================================================================ */

/*
 * Allocate node array.
 */
static wingo_dht_node_t **bucket_alloc_nodes(wingo_size capacity)
{
    wingo_dht_node_t **nodes;

    if (capacity == 0) {
        capacity = WINGO_DHT_BUCKET_DEFAULT_SIZE;
    }

    nodes = calloc(capacity, sizeof(wingo_dht_node_t *));
    if (nodes == NULL) {
        return NULL;
    }

    return nodes;
}

/*
 * Grow node array.
 */
static wingo_error_t bucket_grow(wingo_dht_bucket_t *bucket)
{
    wingo_size new_cap;
    wingo_dht_node_t **new_nodes;

    /* Already at max count? */
    if (bucket->capacity >= bucket->max_count) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Double capacity, but not beyond max_count */
    new_cap = bucket->capacity * 2;
    if (new_cap > bucket->max_count) {
        new_cap = bucket->max_count;
    }

    if (new_cap < bucket->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_nodes = realloc(bucket->nodes, new_cap * sizeof(wingo_dht_node_t *));
    if (new_nodes == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    if (new_cap > bucket->capacity) {
        memset(&new_nodes[bucket->capacity], 0,
               (new_cap - bucket->capacity) * sizeof(wingo_dht_node_t *));
    }

    bucket->nodes = new_nodes;
    bucket->capacity = new_cap;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * BUCKET LIFECYCLE
 * ============================================================================ */

/*
 * Create a new bucket.
 */
wingo_dht_bucket_t *wingo_dht_bucket_new(const wingo_dht_id_t *first,
                                          wingo_addr_family_t af,
                                          wingo_size max_count)
{
    wingo_dht_bucket_t *bucket;

    if (first == NULL) {
        return NULL;
    }

    bucket = calloc(1, sizeof(wingo_dht_bucket_t));
    if (bucket == NULL) {
        return NULL;
    }

    /* Copy first ID */
    memcpy(&bucket->first, first, sizeof(wingo_dht_id_t));

    /* Set family */
    bucket->family = af;

    /* Set max count */
    if (max_count == 0) {
        max_count = WINGO_DHT_BUCKET_DEFAULT_SIZE;
    }
    if (max_count < WINGO_DHT_BUCKET_MIN_SIZE) {
        max_count = WINGO_DHT_BUCKET_MIN_SIZE;
    }

    bucket->max_count = max_count;

    /* Allocate nodes */
    bucket->capacity = max_count;
    bucket->nodes = bucket_alloc_nodes(bucket->capacity);
    if (bucket->nodes == NULL) {
        free(bucket);
        return NULL;
    }

    bucket->count = 0;
    bucket->cached = NULL;
    bucket->last_changed = wingo_time_now();
    bucket->next = NULL;
    bucket->prev = NULL;

    return bucket;
}

/*
 * Free a bucket.
 *
 * NOTE: This does NOT free the nodes — caller must handle them.
 *       Use wingo_dht_bucket_free_all() to free nodes too.
 */
void wingo_dht_bucket_free(wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return;
    }

    if (bucket->nodes != NULL) {
        free(bucket->nodes);
    }

    if (bucket->cached != NULL) {
        wingo_dht_node_free(bucket->cached);
    }

    free(bucket);
}

/*
 * Free a bucket and all its nodes.
 */
void wingo_dht_bucket_free_all(wingo_dht_bucket_t *bucket)
{
    wingo_size i;

    if (bucket == NULL) {
        return;
    }

    /* Free all nodes */
    for (i = 0; i < bucket->count; i++) {
        wingo_dht_node_free(bucket->nodes[i]);
    }

    wingo_dht_bucket_free(bucket);
}

/* ============================================================================
 * BUCKET QUERY
 * ============================================================================ */

/*
 * Get bucket first ID.
 */
const wingo_dht_id_t *wingo_dht_bucket_first(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return NULL;
    }

    return &bucket->first;
}

/*
 * Get bucket address family.
 */
wingo_addr_family_t wingo_dht_bucket_family(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return WINGO_ADDR_UNSPEC;
    }

    return bucket->family;
}

/*
 * Get bucket node count.
 */
wingo_size wingo_dht_bucket_count(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return 0;
    }

    return bucket->count;
}

/*
 * Get bucket max count.
 */
wingo_size wingo_dht_bucket_max_count(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return 0;
    }

    return bucket->max_count;
}

/*
 * Check if bucket is empty.
 */
bool wingo_dht_bucket_is_empty(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return true;
    }

    return bucket->count == 0;
}

/*
 * Check if bucket is full.
 */
bool wingo_dht_bucket_is_full(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return false;
    }

    return bucket->count >= bucket->max_count;
}

/*
 * Check if an ID is in bucket range.
 *
 * Bucket range is [first, next->first).
 * Since we don't have access to next bucket here, we check if
 * the ID is >= first (assuming next bucket has higher first).
 */
bool wingo_dht_bucket_contains(const wingo_dht_bucket_t *bucket,
                                const wingo_dht_id_t *id)
{
    if (bucket == NULL || id == NULL) {
        return false;
    }

    /* ID must be >= first */
    if (memcmp(id->bytes, bucket->first.bytes, WINGO_DHT_ID_SIZE) < 0) {
        return false;
    }

    /* If there's a next bucket, ID must be < next->first */
    if (bucket->next != NULL) {
        if (memcmp(id->bytes, bucket->next->first.bytes,
                   WINGO_DHT_ID_SIZE) >= 0) {
            return false;
        }
    }

    return true;
}

/*
 * Get bucket last changed time.
 */
wingo_i64 wingo_dht_bucket_last_changed(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return 0;
    }

    return bucket->last_changed;
}

/*
 * Update bucket last changed time.
 */
void wingo_dht_bucket_update_changed(wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return;
    }

    bucket->last_changed = wingo_time_now();
}

/* ============================================================================
 * BUCKET NODE MANAGEMENT
 * ============================================================================ */

/*
 * Add a node to bucket.
 */
wingo_error_t wingo_dht_bucket_add_node(wingo_dht_bucket_t *bucket,
                                         wingo_dht_node_t *node)
{
    int can_add;
    wingo_size worst_idx;

    if (bucket == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Check if node already exists */
    if (bucket_find_index(bucket, wingo_dht_node_id(node)) >= 0) {
        /* Refresh existing node's last seen */
        wingo_dht_node_update_seen(node);
        return WINGO_SUCCESS;
    }

    can_add = bucket_can_add(bucket);

    if (can_add == 1) {
        /* Room available — just add */
        if (bucket->count >= bucket->capacity) {
            wingo_error_t rc = bucket_grow(bucket);
            if (rc != WINGO_SUCCESS) {
                return rc;
            }
        }

        bucket->nodes[bucket->count++] = node;
        bucket->last_changed = wingo_time_now();

        return WINGO_SUCCESS;
    }

    if (can_add == 0) {
        /* Full, but can replace a bad node */
        wingo_size i;
        for (i = 0; i < bucket->count; i++) {
            if (wingo_dht_node_is_bad(bucket->nodes[i])) {
                wingo_dht_node_free(bucket->nodes[i]);
                bucket->nodes[i] = node;
                bucket->last_changed = wingo_time_now();
                return WINGO_SUCCESS;
            }
        }
    }

    /* Cannot add — cache the node for later */
    if (bucket->cached != NULL) {
        wingo_dht_node_free(bucket->cached);
    }

    worst_idx = bucket_worst_index(bucket);
    if (worst_idx < bucket->count) {
        bucket->cached = wingo_dht_node_clone(bucket->nodes[worst_idx]);
    } else {
        bucket->cached = wingo_dht_node_clone(node);
    }

    return WINGO_ERR_BUSY;
}

/*
 * Remove a node from bucket.
 */
wingo_error_t wingo_dht_bucket_remove_node(wingo_dht_bucket_t *bucket,
                                            const wingo_dht_id_t *id)
{
    int index;

    if (bucket == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    index = bucket_find_index(bucket, id);
    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    bucket_remove_at(bucket, (wingo_size)index);

    return WINGO_SUCCESS;
}

/*
 * Remove a node from bucket (by pointer).
 */
wingo_error_t wingo_dht_bucket_remove_node_ptr(wingo_dht_bucket_t *bucket,
                                                wingo_dht_node_t *node)
{
    wingo_size i;

    if (bucket == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    for (i = 0; i < bucket->count; i++) {
        if (bucket->nodes[i] == node) {
            bucket_remove_at(bucket, i);
            return WINGO_SUCCESS;
        }
    }

    return WINGO_ERR_NOT_FOUND;
}

/*
 * Find a node in bucket.
 */
wingo_dht_node_t *wingo_dht_bucket_find_node(const wingo_dht_bucket_t *bucket,
                                              const wingo_dht_id_t *id)
{
    int index;

    if (bucket == NULL || id == NULL) {
        return NULL;
    }

    index = bucket_find_index(bucket, id);
    if (index < 0) {
        return NULL;
    }

    return bucket->nodes[index];
}

/*
 * Get node at index.
 */
wingo_dht_node_t *wingo_dht_bucket_get_node(const wingo_dht_bucket_t *bucket,
                                             wingo_size index)
{
    if (bucket == NULL || index >= bucket->count) {
        return NULL;
    }

    return bucket->nodes[index];
}

/*
 * Get first node.
 */
wingo_dht_node_t *wingo_dht_bucket_first_node(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL || bucket->count == 0) {
        return NULL;
    }

    return bucket->nodes[0];
}

/*
 * Get last node.
 */
wingo_dht_node_t *wingo_dht_bucket_last_node(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL || bucket->count == 0) {
        return NULL;
    }

    return bucket->nodes[bucket->count - 1];
}

/*
 * Get a random node from bucket.
 */
wingo_dht_node_t *wingo_dht_bucket_random_node(const wingo_dht_bucket_t *bucket)
{
    wingo_u32 index;

    if (bucket == NULL || bucket->count == 0) {
        return NULL;
    }

    index = wingo_rand_range(0, (wingo_u32)(bucket->count - 1));

    return bucket->nodes[index];
}

/*
 * Get next node in bucket.
 */
wingo_dht_node_t *wingo_dht_bucket_next_node(const wingo_dht_bucket_t *bucket,
                                              const wingo_dht_node_t *node)
{
    wingo_size i;

    if (bucket == NULL) {
        return NULL;
    }

    if (node == NULL) {
        return wingo_dht_bucket_first_node(bucket);
    }

    for (i = 0; i < bucket->count; i++) {
        if (bucket->nodes[i] == node) {
            if (i + 1 < bucket->count) {
                return bucket->nodes[i + 1];
            }
            return NULL;
        }
    }

    return NULL;
}
/* ============================================================================
 * BUCKET CACHED NODE
 * ============================================================================ */

/*
 * Set cached node.
 *
 * The cached node is a node that couldn't be added because the
 * bucket was full. When a bad node is found later, the cached
 * node can replace it.
 */
wingo_error_t wingo_dht_bucket_set_cached(wingo_dht_bucket_t *bucket,
                                           const wingo_dht_node_t *node)
{
    if (bucket == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Free old cached node */
    if (bucket->cached != NULL) {
        wingo_dht_node_free(bucket->cached);
    }

    /* Clone new cached node */
    bucket->cached = wingo_dht_node_clone(node);
    if (bucket->cached == NULL) {
        return WINGO_ERR_NOMEM;
    }

    return WINGO_SUCCESS;
}

/*
 * Get cached node.
 */
wingo_dht_node_t *wingo_dht_bucket_get_cached(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return NULL;
    }

    return bucket->cached;
}

/*
 * Clear cached node.
 */
void wingo_dht_bucket_clear_cached(wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return;
    }

    if (bucket->cached != NULL) {
        wingo_dht_node_free(bucket->cached);
        bucket->cached = NULL;
    }
}

/*
 * Check if bucket has cached node.
 */
bool wingo_dht_bucket_has_cached(const wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return false;
    }

    return bucket->cached != NULL;
}

/* ============================================================================
 * BUCKET SPLIT
 * ============================================================================ */

/*
 * Calculate bucket middle ID.
 *
 * The middle ID is the midpoint between this bucket's first ID
 * and the next bucket's first ID. If there's no next bucket,
 * we set the bit at position (depth) to 1.
 *
 * NOTE: Without knowing the bucket depth, we approximate by
 *       computing the midpoint using common bits.
 */
wingo_error_t wingo_dht_bucket_middle(const wingo_dht_bucket_t *bucket,
                                       wingo_dht_id_t *out)
{
    wingo_dht_id_t mid;
    wingo_size i;
    int carry;

    if (bucket == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* If we have a next bucket, midpoint is easy */
    if (bucket->next != NULL) {
        /* Compute average of first and next->first */
        carry = 0;
        for (i = WINGO_DHT_ID_SIZE; i-- > 0; ) {
            int a = bucket->first.bytes[i];
            int b = bucket->next->first.bytes[i];
            int sum = a + b + carry;
            mid.bytes[i] = (wingo_u8)(sum & 0xFF);
            carry = (sum >> 8) & 1;
        }

        /* Divide by 2: shift right by 1 */
        wingo_u8 prev_high = 0;
        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            wingo_u8 cur = mid.bytes[i];
            mid.bytes[i] = (wingo_u8)((cur >> 1) | (prev_high << 7));
            prev_high = cur & 1;
        }

        memcpy(out->bytes, mid.bytes, WINGO_DHT_ID_SIZE);
        return WINGO_SUCCESS;
    }

    /* No next bucket — split at current depth.
     *
     * We find the first bit position where our bucket's first ID
     * can be flipped. This is essentially finding the common prefix
     * with 0x00...00 and setting the next bit.
     *
     * Simplified approach: set the lowest bit of the first byte
     * that is not 0xFF.
     */
    memcpy(mid.bytes, bucket->first.bytes, WINGO_DHT_ID_SIZE);

    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        if (mid.bytes[i] != 0xFF) {
            /* Set the highest unset bit in this byte */
            wingo_u8 b = mid.bytes[i];
            wingo_u8 mask = 0x80;

            while (mask != 0) {
                if ((b & mask) == 0) {
                    mid.bytes[i] |= mask;
                    /* Zero out remaining bits */
                    mask >>= 1;
                    while (mask != 0) {
                        mid.bytes[i] &= ~mask;
                        mask >>= 1;
                    }
                    memcpy(out->bytes, mid.bytes, WINGO_DHT_ID_SIZE);
                    return WINGO_SUCCESS;
                }
                mask >>= 1;
            }
        }
    }

    /* All bits set — shouldn't happen */
    return WINGO_ERR_OUT_OF_RANGE;
}

/*
 * Calculate random ID in bucket range.
 *
 * The ID must be >= bucket->first and < next bucket->first (if any).
 */
wingo_error_t wingo_dht_bucket_random_id(const wingo_dht_bucket_t *bucket,
                                          wingo_dht_id_t *out)
{
    wingo_dht_id_t mid;
    wingo_dht_id_t random_id;
    wingo_error_t rc;

    if (bucket == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Get middle ID */
    rc = wingo_dht_bucket_middle(bucket, &mid);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Generate random ID */
    rc = wingo_dht_random_id(&random_id);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /*
     * Constrain random_id to [first, mid).
     *
     * We use: result = first + (random % (mid - first))
     *
     * But since these are 160-bit numbers, we approximate by
     * masking the random_id with the bits that differ between
     * first and mid, then adding to first.
     */
    {
        wingo_size i;
        wingo_dht_id_t result;
        bool in_range = false;

        /* Try up to 8 times to get an ID in range */
        for (i = 0; i < 8; i++) {
            /* result = first | (random & (mid ^ first)) */
            wingo_size j;
            for (j = 0; j < WINGO_DHT_ID_SIZE; j++) {
                wingo_u8 diff = mid.bytes[j] ^ bucket->first.bytes[j];
                result.bytes[j] = bucket->first.bytes[j] |
                                  (random_id.bytes[j] & diff);
            }

            /* Check if in range */
            if (memcmp(result.bytes, bucket->first.bytes,
                       WINGO_DHT_ID_SIZE) >= 0 &&
                memcmp(result.bytes, mid.bytes, WINGO_DHT_ID_SIZE) < 0) {
                in_range = true;
                break;
            }

            /* Retry with new random */
            rc = wingo_dht_random_id(&random_id);
            if (rc != WINGO_SUCCESS) {
                return rc;
            }
        }

        if (!in_range) {
            /* Fall back to first */
            memcpy(result.bytes, bucket->first.bytes, WINGO_DHT_ID_SIZE);
        }

        memcpy(out->bytes, result.bytes, WINGO_DHT_ID_SIZE);
    }

    return WINGO_SUCCESS;
}

/*
 * Split bucket into two.
 *
 * The new bucket (returned) contains the upper half of the range.
 * The original bucket keeps the lower half.
 */
wingo_dht_bucket_t *wingo_dht_bucket_split(wingo_dht_bucket_t *bucket)
{
    wingo_dht_bucket_t *new_bucket;
    wingo_dht_id_t mid;
    wingo_dht_node_t **keep_nodes;
    wingo_dht_node_t **move_nodes;
    wingo_size keep_count = 0;
    wingo_size move_count = 0;
    wingo_size i;
    wingo_error_t rc;

    if (bucket == NULL) {
        return NULL;
    }

    /* Compute midpoint */
    rc = wingo_dht_bucket_middle(bucket, &mid);
    if (rc != WINGO_SUCCESS) {
        return NULL;
    }

    /* Create new bucket with mid as first ID */
    new_bucket = wingo_dht_bucket_new(&mid, bucket->family,
                                       bucket->max_count);
    if (new_bucket == NULL) {
        return NULL;
    }

    /* Allocate temporary arrays for redistribution */
    keep_nodes = calloc(bucket->count, sizeof(wingo_dht_node_t *));
    move_nodes = calloc(bucket->count, sizeof(wingo_dht_node_t *));

    if (keep_nodes == NULL || move_nodes == NULL) {
        free(keep_nodes);
        free(move_nodes);
        wingo_dht_bucket_free(new_bucket);
        return NULL;
    }

    /* Redistribute nodes */
    for (i = 0; i < bucket->count; i++) {
        wingo_dht_node_t *node = bucket->nodes[i];
        const wingo_dht_id_t *node_id = wingo_dht_node_id(node);

        if (memcmp(node_id->bytes, mid.bytes, WINGO_DHT_ID_SIZE) < 0) {
            keep_nodes[keep_count++] = node;
        } else {
            move_nodes[move_count++] = node;
        }
    }

    /* Update original bucket */
    memcpy(bucket->nodes, keep_nodes, keep_count * sizeof(wingo_dht_node_t *));
    memset(&bucket->nodes[keep_count], 0,
           (bucket->count - keep_count) * sizeof(wingo_dht_node_t *));
    bucket->count = keep_count;

    /* Add moved nodes to new bucket */
    for (i = 0; i < move_count; i++) {
        wingo_dht_bucket_add_node(new_bucket, move_nodes[i]);
    }

    /* Link new bucket after original */
    new_bucket->next = bucket->next;
    new_bucket->prev = bucket;
    if (bucket->next != NULL) {
        bucket->next->prev = new_bucket;
    }
    bucket->next = new_bucket;

    /* Free temp arrays */
    free(keep_nodes);
    free(move_nodes);

    return new_bucket;
}

/* ============================================================================
 * BUCKET NODE FILTERING
 * ============================================================================ */

/*
 * Get good nodes from bucket.
 */
wingo_size wingo_dht_bucket_good_nodes(const wingo_dht_bucket_t *bucket,
                                        wingo_dht_node_t **nodes,
                                        wingo_size max)
{
    wingo_size i;
    wingo_size count = 0;

    if (bucket == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    for (i = 0; i < bucket->count && count < max; i++) {
        if (wingo_dht_node_is_good(bucket->nodes[i])) {
            nodes[count++] = bucket->nodes[i];
        }
    }

    return count;
}

/*
 * Compare nodes by XOR distance to target.
 *
 * Used for sorting closest nodes.
 */
typedef struct {
    wingo_dht_node_t   *node;
    wingo_dht_id_t      distance;
} node_distance_t;

/*
 * Compare two node distances (for qsort).
 */
static int node_distance_cmp(const void *a, const void *b)
{
    const node_distance_t *da = (const node_distance_t *)a;
    const node_distance_t *db = (const node_distance_t *)b;

    return memcmp(da->distance.bytes, db->distance.bytes,
                  WINGO_DHT_ID_SIZE);
}

/*
 * Get nodes closest to target ID.
 *
 * Returns nodes sorted by XOR distance to target (closest first).
 */
wingo_size wingo_dht_bucket_closest_nodes(const wingo_dht_bucket_t *bucket,
                                           const wingo_dht_id_t *target,
                                           wingo_dht_node_t **nodes,
                                           wingo_size max)
{
    node_distance_t *distances;
    wingo_size i;
    wingo_size count;
    wingo_size result_count;

    if (bucket == NULL || target == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    if (bucket->count == 0) {
        return 0;
    }

    count = bucket->count;

    /* Allocate distance array */
    distances = calloc(count, sizeof(node_distance_t));
    if (distances == NULL) {
        return 0;
    }

    /* Compute XOR distance for each node */
    for (i = 0; i < count; i++) {
        const wingo_dht_id_t *node_id = wingo_dht_node_id(bucket->nodes[i]);
        wingo_size j;

        distances[i].node = bucket->nodes[i];

        for (j = 0; j < WINGO_DHT_ID_SIZE; j++) {
            distances[i].distance.bytes[j] =
                node_id->bytes[j] ^ target->bytes[j];
        }
    }

    /* Sort by distance */
    qsort(distances, count, sizeof(node_distance_t), node_distance_cmp);

    /* Copy to output */
    result_count = (count < max) ? count : max;
    for (i = 0; i < result_count; i++) {
        nodes[i] = distances[i].node;
    }

    free(distances);

    return result_count;
}

/*
 * Expire old nodes from bucket.
 *
 * A node is expired if:
 *   - It is marked BAD, OR
 *   - It has been pinged MAX_PING times with no reply, OR
 *   - Its last seen time is too old
 */
wingo_size wingo_dht_bucket_expire(wingo_dht_bucket_t *bucket)
{
    wingo_size expired = 0;
    wingo_size i = 0;

    if (bucket == NULL) {
        return 0;
    }

    while (i < bucket->count) {
        wingo_dht_node_t *node = bucket->nodes[i];

        if (wingo_dht_node_is_expired(node)) {
            wingo_dht_node_free(node);
            bucket_remove_at(bucket, i);
            expired++;
            /* Don't increment i — the next node shifted into position i */
        } else {
            i++;
        }
    }

    return expired;
}

/*
 * Check if bucket needs refresh.
 *
 * A bucket needs refresh if it hasn't been changed in max_age_s seconds.
 */
bool wingo_dht_bucket_needs_refresh(const wingo_dht_bucket_t *bucket,
                                     wingo_i64 max_age_s)
{
    wingo_i64 now;
    wingo_i64 age;

    if (bucket == NULL) {
        return false;
    }

    now = wingo_time_now();
    age = now - bucket->last_changed;

    return age >= max_age_s;
}

/* ============================================================================
 * BUCKET STATISTICS
 * ============================================================================ */

/*
 * Get bucket statistics.
 */
wingo_error_t wingo_dht_bucket_get_stats(const wingo_dht_bucket_t *bucket,
                                          wingo_dht_bucket_stats_t *stats)
{
    wingo_size i;
    wingo_i64 now;

    if (bucket == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    stats->count = bucket->count;
    stats->max_count = bucket->max_count;
    stats->cached = (bucket->cached != NULL) ? 1 : 0;

    for (i = 0; i < bucket->count; i++) {
        wingo_dht_node_t *node = bucket->nodes[i];

        if (wingo_dht_node_is_good(node)) {
            stats->good++;
        } else if (wingo_dht_node_is_dubious(node)) {
            stats->dubious++;
        } else if (wingo_dht_node_is_bad(node)) {
            stats->bad++;
        }
    }

    now = wingo_time_now();
    stats->age = now - bucket->last_changed;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * BUCKET UTILITY
 * ============================================================================ */

/*
 * Convert ID to hex string (local helper).
 */
static void id_to_hex_local(const wingo_dht_id_t *id, char *buf)
{
    static const char hex[] = "0123456789abcdef";
    wingo_size i;

    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        buf[i * 2]     = hex[(id->bytes[i] >> 4) & 0x0F];
        buf[i * 2 + 1] = hex[id->bytes[i] & 0x0F];
    }
    buf[WINGO_DHT_ID_SIZE * 2] = '\0';
}

/*
 * Print bucket.
 */
void wingo_dht_bucket_print(const wingo_dht_bucket_t *bucket, FILE *f)
{
    char first_hex[WINGO_DHT_ID_HEX_SIZE];
    wingo_i64 now;

    if (f == NULL) {
        f = stderr;
    }

    if (bucket == NULL) {
        fprintf(f, "Bucket: (null)\n");
        return;
    }

    id_to_hex_local(&bucket->first, first_hex);
    now = wingo_time_now();

    fprintf(f, "Bucket:\n");
    fprintf(f, "  First:      %s\n", first_hex);
    fprintf(f, "  Family:     %s\n",
            bucket->family == WINGO_ADDR_IPV6 ? "IPv6" : "IPv4");
    fprintf(f, "  Nodes:      %zu / %zu\n", bucket->count, bucket->max_count);
    fprintf(f, "  Cached:     %s\n", bucket->cached ? "yes" : "no");
    fprintf(f, "  Age:        %llds\n",
            (long long)(now - bucket->last_changed));
}

/*
 * Print bucket nodes.
 */
void wingo_dht_bucket_print_nodes(const wingo_dht_bucket_t *bucket, FILE *f)
{
    wingo_size i;
    char id_hex[WINGO_DHT_ID_HEX_SIZE];
    char addr_str[WINGO_ADDR_STR_MAX];

    if (f == NULL) {
        f = stderr;
    }

    if (bucket == NULL) {
        fprintf(f, "Bucket: (null)\n");
        return;
    }

    fprintf(f, "Bucket nodes (%zu):\n", bucket->count);

    for (i = 0; i < bucket->count; i++) {
        wingo_dht_node_t *node = bucket->nodes[i];
        const wingo_dht_id_t *id = wingo_dht_node_id(node);
        const wingo_addr_t *addr = wingo_dht_node_addr(node);
        wingo_dht_node_state_t state = wingo_dht_node_state(node);
        const char *state_name = wingo_dht_node_state_name(state);

        id_to_hex_local(id, id_hex);

        if (addr != NULL) {
            wingo_addr_str(addr, addr_str, sizeof(addr_str));
        } else {
            snprintf(addr_str, sizeof(addr_str), "(null)");
        }

        fprintf(f, "  [%zu] %s  %s  %s\n",
                i, id_hex, addr_str, state_name);
    }

    if (bucket->cached != NULL) {
        const wingo_dht_id_t *id = wingo_dht_node_id(bucket->cached);
        id_to_hex_local(id, id_hex);
        fprintf(f, "  [cached] %s\n", id_hex);
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
