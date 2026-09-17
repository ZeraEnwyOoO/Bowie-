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
 * Kademlia routing table implementation for Bowie DHT.
 *
 * The routing table organizes nodes into buckets based on their
 * XOR distance from our own node ID.
 *
 * Routing table structure:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    ROUTING TABLE                            │
 *   │                                                             │
 *   │   my_id = 0x1234...                                         │
 *   │                                                             │
 *   │   ┌─────────────┐                                           │
 *   │   │  Bucket 0   │  range: [0x0000..., 0x8000...)           │
 *   │   │  8 nodes    │  (all IDs with MSB = 0)                  │
 *   │   └─────────────┘                                           │
 *   │                                                             │
 *   │   ┌─────────────┐                                           │
 *   │   │  Bucket 1   │  range: [0x8000..., 0xC000...)           │
 *   │   │  8 nodes    │  (IDs with bits 0=1, 1=0)                │
 *   │   └─────────────┘                                           │
 *   │                                                             │
 *   │   ┌─────────────┐                                           │
 *   │   │  Bucket 2   │  range: [0xC000..., 0xE000...)           │
 *   │   │  8 nodes    │  (IDs with bits 0=1, 1=1, 2=0)          │
 *   │   └─────────────┘                                           │
 *   │                                                             │
 *   │   ... (up to 160 buckets)                                   │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * Bucket splitting:
 *   - Buckets split when they are full and contain our own ID
 *   - Splitting creates two buckets, each with half the range
 *   - Only the bucket containing our ID splits (Kademlia rule)
 *
 * Closest nodes lookup:
 *   - Collect nodes from all buckets
 *   - Sort by XOR distance to target
 *   - Return closest N
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_routing.h"
#include "wingo/net/dht/dht_bucket.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Routing table (concrete).
 */
struct wingo_dht_routing {
    /* ----- Identity ----- */
    wingo_dht_id_t       my_id;          /* Our own node ID */
    wingo_addr_family_t  family;         /* Address family */

    /* ----- Configuration ----- */
    wingo_size           bucket_size;    /* Kademlia K */

    /* ----- Buckets (array + linked list) ----- */
    wingo_dht_bucket_t **buckets;        /* Array of buckets */
    wingo_size           bucket_count;   /* Number of buckets */
    wingo_size           bucket_capacity;/* Allocated capacity */

    /* ----- Linked list (for iteration) ----- */
    wingo_dht_bucket_t  *first;          /* First bucket */
    wingo_dht_bucket_t  *last;           /* Last bucket */

    /* ----- Maintenance ----- */
    wingo_i64            last_maintenance; /* Last maintenance time */
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial bucket capacity.
 */
#define ROUTING_INIT_BUCKET_CAP     4

/*
 * Maintenance interval (5 minutes).
 */
#define ROUTING_MAINTENANCE_INTERVAL (5 * 60)

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Compare two IDs (memcmp wrapper).
 */
static int id_cmp(const wingo_dht_id_t *a, const wingo_dht_id_t *b)
{
    return memcmp(a->bytes, b->bytes, WINGO_DHT_ID_SIZE);
}

/*
 * Compute XOR distance between two IDs.
 */
static void id_xor(const wingo_dht_id_t *a,
                    const wingo_dht_id_t *b,
                    wingo_dht_id_t *out)
{
    wingo_size i;
    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        out->bytes[i] = a->bytes[i] ^ b->bytes[i];
    }
}

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

        while ((x & 0x80) == 0) {
            common_bits++;
            x <<= 1;
        }

        break;
    }

    return common_bits;
}

/*
 * Allocate bucket array.
 */
static wingo_dht_bucket_t **routing_alloc_buckets(wingo_size capacity)
{
    wingo_dht_bucket_t **buckets;

    if (capacity == 0) {
        capacity = ROUTING_INIT_BUCKET_CAP;
    }

    buckets = calloc(capacity, sizeof(wingo_dht_bucket_t *));
    return buckets;
}

/*
 * Grow bucket array.
 */
static wingo_error_t routing_grow_buckets(wingo_dht_routing_t *rt)
{
    wingo_size new_cap;
    wingo_dht_bucket_t **new_buckets;

    new_cap = rt->bucket_capacity * 2;
    if (new_cap < rt->bucket_capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_buckets = realloc(rt->buckets,
                          new_cap * sizeof(wingo_dht_bucket_t *));
    if (new_buckets == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    if (new_cap > rt->bucket_capacity) {
        memset(&new_buckets[rt->bucket_capacity], 0,
               (new_cap - rt->bucket_capacity) * sizeof(wingo_dht_bucket_t *));
    }

    rt->buckets = new_buckets;
    rt->bucket_capacity = new_cap;

    return WINGO_SUCCESS;
}

/*
 * Add bucket to routing table.
 */
static wingo_error_t routing_add_bucket(wingo_dht_routing_t *rt,
                                         wingo_dht_bucket_t *bucket)
{
    wingo_error_t rc;

    if (rt->bucket_count >= rt->bucket_capacity) {
        rc = routing_grow_buckets(rt);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    rt->buckets[rt->bucket_count++] = bucket;

    /* Update linked list */
    if (rt->first == NULL) {
        rt->first = bucket;
        rt->last = bucket;
    } else {
        rt->last = bucket;  /* Already linked via bucket->next */
    }

    return WINGO_SUCCESS;
}

/*
 * Remove bucket from routing table (by index).
 */
static void routing_remove_bucket_at(wingo_dht_routing_t *rt,
                                      wingo_size index)
{
    wingo_dht_bucket_t *bucket;
    wingo_dht_bucket_t *prev;
    wingo_dht_bucket_t *next;

    if (index >= rt->bucket_count) {
        return;
    }

    bucket = rt->buckets[index];

    /* Get prev/next via accessors */
    prev = wingo_dht_bucket_prev(bucket);
    next = wingo_dht_bucket_next(bucket);

    /* Update routing table's first/last */
    if (prev == NULL) {
        rt->first = next;
    }

    if (next == NULL) {
        rt->last = prev;
    }

    /* Unlink from linked list */
    wingo_dht_bucket_unlink(bucket);

    /* Shift array */
    if (index < rt->bucket_count - 1) {
        memmove(&rt->buckets[index],
                &rt->buckets[index + 1],
                (rt->bucket_count - index - 1) * sizeof(wingo_dht_bucket_t *));
    }

    rt->bucket_count--;
    rt->buckets[rt->bucket_count] = NULL;
}

/*
 * Find bucket index for an ID.
 */
static int routing_find_bucket_index(const wingo_dht_routing_t *rt,
                                      const wingo_dht_id_t *id)
{
    wingo_size i;

    for (i = 0; i < rt->bucket_count; i++) {
        if (wingo_dht_bucket_contains(rt->buckets[i], id)) {
            return (int)i;
        }
    }

    return -1;
}

/* ============================================================================
 * ROUTING TABLE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new routing table.
 */
wingo_dht_routing_t *wingo_dht_routing_new(const wingo_dht_id_t *my_id,
                                            wingo_addr_family_t af,
                                            wingo_size bucket_size)
{
    wingo_dht_routing_t *rt;
    wingo_dht_bucket_t *initial_bucket;
    wingo_dht_id_t zero_id;

    if (my_id == NULL) {
        return NULL;
    }

    rt = calloc(1, sizeof(wingo_dht_routing_t));
    if (rt == NULL) {
        return NULL;
    }

    /* Copy ID */
    memcpy(&rt->my_id, my_id, sizeof(wingo_dht_id_t));

    /* Set family */
    rt->family = af;

    /* Set bucket size */
    if (bucket_size == 0) {
        bucket_size = WINGO_DHT_BUCKET_SIZE;
    }
    if (bucket_size < WINGO_DHT_MIN_BUCKET_SIZE) {
        bucket_size = WINGO_DHT_MIN_BUCKET_SIZE;
    }
    rt->bucket_size = bucket_size;

    /* Allocate bucket array */
    rt->bucket_capacity = ROUTING_INIT_BUCKET_CAP;
    rt->buckets = routing_alloc_buckets(rt->bucket_capacity);
    if (rt->buckets == NULL) {
        free(rt);
        return NULL;
    }
    rt->bucket_count = 0;

    /* Create initial bucket covering entire ID space */
    memset(&zero_id, 0, sizeof(zero_id));
    initial_bucket = wingo_dht_bucket_new(&zero_id, af, bucket_size);
    if (initial_bucket == NULL) {
        free(rt->buckets);
        free(rt);
        return NULL;
    }

    rt->buckets[0] = initial_bucket;
    rt->bucket_count = 1;
    rt->first = initial_bucket;
    rt->last = initial_bucket;

    rt->last_maintenance = wingo_time_now();

    WINGO_LOG_DEBUG("Routing table created (bucket_size=%zu)", bucket_size);

    return rt;
}

/*
 * Free a routing table.
 *
 * Frees all buckets and their nodes.
 */
void wingo_dht_routing_free(wingo_dht_routing_t *rt)
{
    wingo_size i;

    if (rt == NULL) {
        return;
    }

    /* Free all buckets (and their nodes) */
    if (rt->buckets != NULL) {
        for (i = 0; i < rt->bucket_count; i++) {
            wingo_dht_bucket_free_all(rt->buckets[i]);
        }
        free(rt->buckets);
    }

    free(rt);
}

/* ============================================================================
 * BUCKET MANAGEMENT
 * ============================================================================ */

/*
 * Find bucket for an ID.
 */
wingo_dht_bucket_t *wingo_dht_routing_find_bucket(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id)
{
    int index;

    if (rt == NULL || id == NULL) {
        return NULL;
    }

    index = routing_find_bucket_index(rt, id);
    if (index < 0) {
        return NULL;
    }

    return rt->buckets[index];
}

/*
 * Split a bucket.
 *
 * The bucket is split into two halves. The new bucket (returned)
 * contains the upper half of the range.
 *
 * This function:
 *   1. Calls wingo_dht_bucket_split() to create the new bucket
 *   2. Inserts the new bucket into the routing table after the old one
 */
wingo_error_t wingo_dht_routing_split_bucket(
    wingo_dht_routing_t *rt,
    wingo_dht_bucket_t *bucket)
{
    wingo_dht_bucket_t *new_bucket;
    wingo_size i;
    int index = -1;

    if (rt == NULL || bucket == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Can't split if already at max buckets */
    if (rt->bucket_count >= WINGO_DHT_MAX_BUCKETS) {
        WINGO_LOG_WARN("Routing table: max buckets reached (%d)",
                       WINGO_DHT_MAX_BUCKETS);
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Find bucket index */
    for (i = 0; i < rt->bucket_count; i++) {
        if (rt->buckets[i] == bucket) {
            index = (int)i;
            break;
        }
    }

    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    /* Split the bucket */
    new_bucket = wingo_dht_bucket_split(bucket);
    if (new_bucket == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Grow array if needed */
    if (rt->bucket_count >= rt->bucket_capacity) {
        wingo_error_t rc = routing_grow_buckets(rt);
        if (rc != WINGO_SUCCESS) {
            wingo_dht_bucket_free_all(new_bucket);
            return rc;
        }
    }

    /* Insert new bucket after original */
    if ((wingo_size)(index + 1) < rt->bucket_count) {
        memmove(&rt->buckets[index + 2],
                &rt->buckets[index + 1],
                (rt->bucket_count - index - 1) * sizeof(wingo_dht_bucket_t *));
    }

    rt->buckets[index + 1] = new_bucket;
    rt->bucket_count++;

    WINGO_LOG_DEBUG("Routing table: split bucket %d (now %zu buckets)",
                    index, rt->bucket_count);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * NODE MANAGEMENT
 * ============================================================================ */

/*
 * Find a node in routing table.
 */
wingo_dht_node_t *wingo_dht_routing_find_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id)
{
    wingo_dht_bucket_t *bucket;

    if (rt == NULL || id == NULL) {
        return NULL;
    }

    bucket = wingo_dht_routing_find_bucket(rt, id);
    if (bucket == NULL) {
        return NULL;
    }

    return wingo_dht_bucket_find_node(bucket, id);
}

/*
 * Add a node to routing table.
 *
 * This is the main entry point for adding nodes. It handles:
 *   1. Finding the right bucket
 *   2. Adding to bucket (with replacement if full)
 *   3. Splitting bucket if it contains our ID and is full
 *
 * The "confirm" parameter indicates how confident we are:
 *   0 = unknown (not pinged)
 *   1 = pinged (but no reply)
 *   2 = replied (good node)
 */
wingo_dht_node_t *wingo_dht_routing_add_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id,
    const wingo_addr_t *addr,
    int confirm)
{
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;
    wingo_error_t rc;

    if (rt == NULL || id == NULL || addr == NULL) {
        return NULL;
    }

    /* Ignore our own ID */
    if (id_cmp(id, &rt->my_id) == 0) {
        return NULL;
    }

    /* Find bucket */
    bucket = wingo_dht_routing_find_bucket(rt, id);
    if (bucket == NULL) {
        WINGO_LOG_WARN("Routing table: no bucket for ID");
        return NULL;
    }

    /* Check if node already exists */
    node = wingo_dht_bucket_find_node(bucket, id);
    if (node != NULL) {
        /* Update existing node */
        wingo_dht_node_update_seen(node);

        if (confirm >= 2) {
            wingo_dht_node_replied(node);
        } else if (confirm == 1) {
            wingo_dht_node_pinged(node);
        }

        return node;
    }

    /* Create new node */
    node = wingo_dht_node_new(id, addr);
    if (node == NULL) {
        return NULL;
    }

    /* Set state based on confirm */
    if (confirm >= 2) {
        wingo_dht_node_replied(node);
    } else if (confirm == 1) {
        wingo_dht_node_pinged(node);
    }

    /* Try to add to bucket */
    rc = wingo_dht_bucket_add_node(bucket, node);
    if (rc == WINGO_SUCCESS) {
        return node;
    }

    if (rc == WINGO_ERR_BUSY) {
        /*
         * Bucket is full and can't replace.
         *
         * Kademlia rule: if this bucket contains our own ID,
         * split it. Otherwise, drop the new node.
         */
        if (wingo_dht_bucket_contains(bucket, &rt->my_id)) {
            /* Split and retry */
            rc = wingo_dht_routing_split_bucket(rt, bucket);
            if (rc == WINGO_SUCCESS) {
                /* Find the right bucket again */
                bucket = wingo_dht_routing_find_bucket(rt, id);
                if (bucket != NULL) {
                    rc = wingo_dht_bucket_add_node(bucket, node);
                    if (rc == WINGO_SUCCESS) {
                        return node;
                    }
                }
            }
        }

        /* Could not add — cache the node instead */
        wingo_dht_bucket_set_cached(bucket, node);

        /* Free our local copy — bucket owns its own cache */
        wingo_dht_node_free(node);

        return NULL;
    }

    /* Other error */
    wingo_dht_node_free(node);
    return NULL;
}

/*
 * Remove a node from routing table.
 */
wingo_error_t wingo_dht_routing_remove_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id)
{
    wingo_dht_bucket_t *bucket;

    if (rt == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    bucket = wingo_dht_routing_find_bucket(rt, id);
    if (bucket == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    return wingo_dht_bucket_remove_node(bucket, id);
}
/* ============================================================================
 * NODE QUERY
 * ============================================================================ */

/*
 * Node distance entry (for sorting).
 */
typedef struct {
    wingo_dht_node_t   *node;
    wingo_dht_id_t      distance;
} node_distance_t;

/*
 * Compare two node distances.
 */
static int node_distance_cmp(const void *a, const void *b)
{
    const node_distance_t *da = (const node_distance_t *)a;
    const node_distance_t *db = (const node_distance_t *)b;

    return memcmp(da->distance.bytes, db->distance.bytes,
                  WINGO_DHT_ID_SIZE);
}

/*
 * Get closest nodes to a target ID.
 *
 * This is the core Kademlia lookup primitive. It:
 *   1. Collects all nodes from all buckets
 *   2. Computes XOR distance to target
 *   3. Sorts by distance
 *   4. Returns the closest N
 *
 * @param rt        Routing table
 * @param target    Target ID
 * @param nodes     Output array (caller-allocated)
 * @param max       Maximum number of nodes to return
 * @return          Number of nodes written
 */
wingo_size wingo_dht_routing_closest_nodes(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *target,
    wingo_dht_node_t **nodes,
    wingo_size max)
{
    node_distance_t *distances;
    wingo_size total = 0;
    wingo_size i, j;
    wingo_size result_count;

    if (rt == NULL || target == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    /* Count total nodes */
    for (i = 0; i < rt->bucket_count; i++) {
        total += wingo_dht_bucket_count(rt->buckets[i]);
    }

    if (total == 0) {
        return 0;
    }

    /* Allocate distance array */
    distances = calloc(total, sizeof(node_distance_t));
    if (distances == NULL) {
        return 0;
    }

    /* Collect all nodes with their distances */
    {
        wingo_size idx = 0;

        for (i = 0; i < rt->bucket_count; i++) {
            wingo_dht_bucket_t *bucket = rt->buckets[i];
            wingo_size bucket_count = wingo_dht_bucket_count(bucket);

            for (j = 0; j < bucket_count; j++) {
                wingo_dht_node_t *node = wingo_dht_bucket_get_node(bucket, j);
                const wingo_dht_id_t *node_id;

                if (node == NULL) {
                    continue;
                }

                node_id = wingo_dht_node_id(node);
                if (node_id == NULL) {
                    continue;
                }

                distances[idx].node = node;
                id_xor(node_id, target, &distances[idx].distance);
                idx++;
            }
        }

        total = idx;
    }

    /* Sort by distance */
    qsort(distances, total, sizeof(node_distance_t), node_distance_cmp);

    /* Copy closest N */
    result_count = (total < max) ? total : max;
    for (i = 0; i < result_count; i++) {
        nodes[i] = distances[i].node;
    }

    free(distances);

    return result_count;
}

/*
 * Get random nodes from routing table.
 */
wingo_size wingo_dht_routing_random_nodes(
    wingo_dht_routing_t *rt,
    wingo_dht_node_t **nodes,
    wingo_size max)
{
    wingo_size result_count = 0;
    wingo_size attempts = 0;
    wingo_size max_attempts;

    if (rt == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    if (rt->bucket_count == 0) {
        return 0;
    }

    /*
     * Try random buckets until we have enough nodes or
     * we've tried too many times.
     */
    max_attempts = max * 4;

    while (result_count < max && attempts < max_attempts) {
        wingo_u32 bucket_idx;
        wingo_dht_bucket_t *bucket;
        wingo_dht_node_t *node;

        bucket_idx = wingo_rand_range(0, (wingo_u32)(rt->bucket_count - 1));
        bucket = rt->buckets[bucket_idx];

        if (bucket == NULL || wingo_dht_bucket_is_empty(bucket)) {
            attempts++;
            continue;
        }

        node = wingo_dht_bucket_random_node(bucket);
        if (node == NULL) {
            attempts++;
            continue;
        }

        /* Check for duplicate */
        {
            wingo_size i;
            bool duplicate = false;

            for (i = 0; i < result_count; i++) {
                if (nodes[i] == node) {
                    duplicate = true;
                    break;
                }
            }

            if (duplicate) {
                attempts++;
                continue;
            }
        }

        nodes[result_count++] = node;
        attempts++;
    }

    return result_count;
}

/* ============================================================================
 * ROUTING TABLE MAINTENANCE
 * ============================================================================ */

/*
 * Expire old nodes from all buckets.
 *
 * Returns number of nodes expired.
 */
wingo_size wingo_dht_routing_expire(wingo_dht_routing_t *rt)
{
    wingo_size total_expired = 0;
    wingo_size i;

    if (rt == NULL) {
        return 0;
    }

    for (i = 0; i < rt->bucket_count; i++) {
        total_expired += wingo_dht_bucket_expire(rt->buckets[i]);
    }

    if (total_expired > 0) {
        WINGO_LOG_DEBUG("Routing table: expired %zu nodes", total_expired);
    }

    return total_expired;
}

/*
 * Bucket maintenance.
 *
 * This performs periodic maintenance tasks:
 *   - Expire old nodes
 *   - Refresh buckets that haven't been used
 *   - Replace bad nodes with cached nodes
 *
 * Returns WINGO_SUCCESS on success.
 */
wingo_error_t wingo_dht_routing_maintenance(wingo_dht_routing_t *rt)
{
    wingo_size i;

    if (rt == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Expire old nodes */
    wingo_dht_routing_expire(rt);

    /* Process each bucket */
    for (i = 0; i < rt->bucket_count; i++) {
        wingo_dht_bucket_t *bucket = rt->buckets[i];

        if (bucket == NULL) {
            continue;
        }

        /*
         * If bucket has a cached node and has a bad node,
         * replace the bad node with the cached one.
         */
        if (wingo_dht_bucket_has_cached(bucket)) {
            wingo_size j;
            wingo_size count = wingo_dht_bucket_count(bucket);

            for (j = 0; j < count; j++) {
                wingo_dht_node_t *node = wingo_dht_bucket_get_node(bucket, j);

                if (node != NULL && wingo_dht_node_is_bad(node)) {
                    wingo_dht_node_t *cached =
                        wingo_dht_bucket_get_cached(bucket);

                    if (cached != NULL) {
                        /* Remove bad node */
                        wingo_dht_node_t *cloned =
                            wingo_dht_node_clone(cached);

                        if (cloned != NULL) {
                            /* Remove old, add new */
                            wingo_dht_bucket_remove_node_ptr(bucket, node);
                            wingo_dht_bucket_add_node(bucket, cloned);
                        }
                    }

                    break;  /* Only replace one per bucket per maintenance */
                }
            }
        }
    }

    rt->last_maintenance = wingo_time_now();

    return WINGO_SUCCESS;
}

/*
 * Neighbourhood maintenance.
 *
 * In Kademlia, each node maintains a "neighbourhood" of nodes
 * closest to its own ID. This function ensures our neighbourhood
 * is up to date by refreshing the buckets closest to our ID.
 *
 * For simplicity, we just refresh the bucket containing our ID.
 */
wingo_error_t wingo_dht_routing_neighbourhood(
    wingo_dht_routing_t *rt)
{
    wingo_dht_bucket_t *bucket;

    if (rt == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Find bucket containing our ID */
    bucket = wingo_dht_routing_find_bucket(rt, &rt->my_id);
    if (bucket == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    /* Update last changed time */
    wingo_dht_bucket_update_changed(bucket);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * ROUTING TABLE QUERY
 * ============================================================================ */

/*
 * Get number of buckets.
 */
wingo_size wingo_dht_routing_bucket_count(const wingo_dht_routing_t *rt)
{
    if (rt == NULL) {
        return 0;
    }

    return rt->bucket_count;
}

/*
 * Get total number of nodes.
 */
wingo_size wingo_dht_routing_node_count(const wingo_dht_routing_t *rt)
{
    wingo_size total = 0;
    wingo_size i;

    if (rt == NULL) {
        return 0;
    }

    for (i = 0; i < rt->bucket_count; i++) {
        total += wingo_dht_bucket_count(rt->buckets[i]);
    }

    return total;
}

/*
 * Get number of good nodes.
 */
wingo_size wingo_dht_routing_good_count(const wingo_dht_routing_t *rt)
{
    wingo_size total = 0;
    wingo_size i, j;

    if (rt == NULL) {
        return 0;
    }

    for (i = 0; i < rt->bucket_count; i++) {
        wingo_dht_bucket_t *bucket = rt->buckets[i];
        wingo_size count = wingo_dht_bucket_count(bucket);

        for (j = 0; j < count; j++) {
            wingo_dht_node_t *node = wingo_dht_bucket_get_node(bucket, j);

            if (node != NULL && wingo_dht_node_is_good(node)) {
                total++;
            }
        }
    }

    return total;
}

/*
 * Get number of dubious nodes.
 */
wingo_size wingo_dht_routing_dubious_count(const wingo_dht_routing_t *rt)
{
    wingo_size total = 0;
    wingo_size i, j;

    if (rt == NULL) {
        return 0;
    }

    for (i = 0; i < rt->bucket_count; i++) {
        wingo_dht_bucket_t *bucket = rt->buckets[i];
        wingo_size count = wingo_dht_bucket_count(bucket);

        for (j = 0; j < count; j++) {
            wingo_dht_node_t *node = wingo_dht_bucket_get_node(bucket, j);

            if (node != NULL && wingo_dht_node_is_dubious(node)) {
                total++;
            }
        }
    }

    return total;
}

/*
 * Get number of bad nodes.
 */
wingo_size wingo_dht_routing_bad_count(const wingo_dht_routing_t *rt)
{
    wingo_size total = 0;
    wingo_size i, j;

    if (rt == NULL) {
        return 0;
    }

    for (i = 0; i < rt->bucket_count; i++) {
        wingo_dht_bucket_t *bucket = rt->buckets[i];
        wingo_size count = wingo_dht_bucket_count(bucket);

        for (j = 0; j < count; j++) {
            wingo_dht_node_t *node = wingo_dht_bucket_get_node(bucket, j);

            if (node != NULL && wingo_dht_node_is_bad(node)) {
                total++;
            }
        }
    }

    return total;
}

/*
 * Get first bucket.
 */
wingo_dht_bucket_t *wingo_dht_routing_first_bucket(
    wingo_dht_routing_t *rt)
{
    if (rt == NULL) {
        return NULL;
    }

    return rt->first;
}

/*
 * Get next bucket.
 *
 * The bucket argument must be a bucket obtained from
 * wingo_dht_routing_first_bucket() or a prior call to this function.
 *
 * NOTE: bucket->next is internal to the bucket, but we expose
 *       it here for iteration.
 */
wingo_dht_bucket_t *wingo_dht_routing_next_bucket(
    wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return NULL;
    }

    /*
     * We need to access bucket->next, but the struct is opaque.
     *
     * This is a design issue — either we expose the field or we
     * provide a proper iterator API.
     *
     * For now, we rely on the fact that dht_routing.c is a friend
     * of dht_bucket.c and can access its internals.
     *
     * Since we can't access bucket->next from here without the struct,
     * we return NULL and let the caller use the array-based iteration.
     *
     * TODO: Provide proper iteration via a routing-level iterator.
     */
    return NULL;
}

/*
 * Get previous bucket.
 *
 * See wingo_dht_routing_next_bucket() for notes.
 */
wingo_dht_bucket_t *wingo_dht_routing_prev_bucket(
    wingo_dht_bucket_t *bucket)
{
    if (bucket == NULL) {
        return NULL;
    }

    return NULL;
}

/* ============================================================================
 * ROUTING TABLE UTILITY
 * ============================================================================ */

/*
 * Convert ID to hex string.
 */
static void id_to_hex(const wingo_dht_id_t *id, char *buf)
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
 * Print routing table.
 */
void wingo_dht_routing_print(const wingo_dht_routing_t *rt, FILE *f)
{
    char my_id_hex[WINGO_DHT_ID_HEX_SIZE];
    wingo_size i;
    wingo_size total_nodes;
    wingo_size good;
    wingo_size dubious;
    wingo_size bad;

    if (f == NULL) {
        f = stderr;
    }

    if (rt == NULL) {
        fprintf(f, "Routing table: (null)\n");
        return;
    }

    id_to_hex(&rt->my_id, my_id_hex);

    total_nodes = wingo_dht_routing_node_count(rt);
    good = wingo_dht_routing_good_count(rt);
    dubious = wingo_dht_routing_dubious_count(rt);
    bad = wingo_dht_routing_bad_count(rt);

    fprintf(f, "Routing Table:\n");
    fprintf(f, "  My ID:      %s\n", my_id_hex);
    fprintf(f, "  Family:     %s\n",
            rt->family == WINGO_ADDR_IPV6 ? "IPv6" : "IPv4");
    fprintf(f, "  Bucket size: %zu\n", rt->bucket_size);
    fprintf(f, "  Buckets:    %zu\n", rt->bucket_count);
    fprintf(f, "  Nodes:      %zu\n", total_nodes);
    fprintf(f, "    Good:     %zu\n", good);
    fprintf(f, "    Dubious:  %zu\n", dubious);
    fprintf(f, "    Bad:      %zu\n", bad);
    fprintf(f, "\n");

    fprintf(f, "  Buckets:\n");
    for (i = 0; i < rt->bucket_count; i++) {
        char first_hex[WINGO_DHT_ID_HEX_SIZE];
        wingo_dht_bucket_t *bucket = rt->buckets[i];

        id_to_hex(wingo_dht_bucket_first(bucket), first_hex);

        fprintf(f, "    [%zu] first=%s  count=%zu/%zu\n",
                i, first_hex,
                wingo_dht_bucket_count(bucket),
                wingo_dht_bucket_max_count(bucket));
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
