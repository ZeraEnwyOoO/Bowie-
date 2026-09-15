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

#ifndef WINGO_NET_DHT_BUCKET_H
#define WINGO_NET_DHT_BUCKET_H

/*
 * ============================================================================
 * WINGO DHT BUCKET
 * ============================================================================
 *
 * This header provides the Kademlia bucket for Bowie DHT.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    BUCKET                                   │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  First ID (Range Start)                             │   │
 *   │   │  ───────────────────────────────                    │   │
 *   │   │  Nodes (Max 8)                                      │   │
 *   │   │  ├── Node 1                                         │   │
 *   │   │  ├── Node 2                                         │   │
 *   │   │  ├── ...                                            │   │
 *   │   │  └── Node 8                                         │   │
 *   │   │  ───────────────────────────────                    │   │
 *   │   │  Count, Max Count                                   │   │
 *   │   │  Cached Node (for ping)                             │   │
 *   │   │  Last Changed Time                                  │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
 *   │   Bucket Range: [first, next->first)                        │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/dht/dht_types.h"
#include "wingo/net/dht/dht_node.h"

/* ============================================================================
 * BUCKET CONSTANTS
 * ============================================================================ */

/*
 * Default bucket size (Kademlia K).
 */
#define WINGO_DHT_BUCKET_DEFAULT_SIZE   8

/*
 * Minimum bucket size.
 */
#define WINGO_DHT_BUCKET_MIN_SIZE       4

/* ============================================================================
 * BUCKET STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT bucket handle.
 *
 * NOTE: This is the ONLY definition of this opaque type in Bowie.
 *       Do NOT redefine it in dht_routing.h.
 */
typedef struct wingo_dht_bucket wingo_dht_bucket_t;

/* ============================================================================
 * BUCKET LIFECYCLE
 * ============================================================================ */

/*
 * Create a new bucket.
 *
 * @param first     First ID in bucket
 * @param af        Address family
 * @param max_count Maximum number of nodes (0 = default)
 * @return          Bucket, or NULL on error
 */
wingo_dht_bucket_t *wingo_dht_bucket_new(const wingo_dht_id_t *first,
                                          wingo_addr_family_t af,
                                          wingo_size max_count);

/*
 * Free a bucket.
 *
 * @param bucket    Bucket (NULL is safe)
 */
void wingo_dht_bucket_free(wingo_dht_bucket_t *bucket);

/*
 * Free a bucket and all its nodes.
 *
 * @param bucket    Bucket (NULL is safe)
 */
void wingo_dht_bucket_free_all(wingo_dht_bucket_t *bucket);

/* ============================================================================
 * BUCKET QUERY
 * ============================================================================ */

/*
 * Get bucket first ID.
 *
 * @param bucket    Bucket
 * @return          First ID, or NULL on error
 */
const wingo_dht_id_t *wingo_dht_bucket_first(const wingo_dht_bucket_t *bucket);

/*
 * Get bucket address family.
 *
 * @param bucket    Bucket
 * @return          Address family
 */
wingo_addr_family_t wingo_dht_bucket_family(const wingo_dht_bucket_t *bucket);

/*
 * Get bucket node count.
 *
 * @param bucket    Bucket
 * @return          Node count
 */
wingo_size wingo_dht_bucket_count(const wingo_dht_bucket_t *bucket);

/*
 * Get bucket max count.
 *
 * @param bucket    Bucket
 * @return          Max count
 */
wingo_size wingo_dht_bucket_max_count(const wingo_dht_bucket_t *bucket);

/*
 * Check if bucket is empty.
 *
 * @param bucket    Bucket
 * @return          true if empty, false otherwise
 */
bool wingo_dht_bucket_is_empty(const wingo_dht_bucket_t *bucket);

/*
 * Check if bucket is full.
 *
 * @param bucket    Bucket
 * @return          true if full, false otherwise
 */
bool wingo_dht_bucket_is_full(const wingo_dht_bucket_t *bucket);

/*
 * Check if an ID is in bucket range.
 *
 * @param bucket    Bucket
 * @param id        ID to check
 * @return          true if in range, false otherwise
 */
bool wingo_dht_bucket_contains(const wingo_dht_bucket_t *bucket,
                                const wingo_dht_id_t *id);

/*
 * Get bucket last changed time.
 *
 * @param bucket    Bucket
 * @return          Last changed timestamp
 */
wingo_i64 wingo_dht_bucket_last_changed(const wingo_dht_bucket_t *bucket);

/*
 * Update bucket last changed time.
 *
 * @param bucket    Bucket
 */
void wingo_dht_bucket_update_changed(wingo_dht_bucket_t *bucket);

/* ============================================================================
 * BUCKET NODE MANAGEMENT
 * ============================================================================ */

/*
 * Add a node to bucket.
 *
 * @param bucket    Bucket
 * @param node      Node
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_add_node(wingo_dht_bucket_t *bucket,
                                         wingo_dht_node_t *node);

/*
 * Remove a node from bucket.
 *
 * @param bucket    Bucket
 * @param id        Node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_remove_node(wingo_dht_bucket_t *bucket,
                                            const wingo_dht_id_t *id);

/*
 * Remove a node from bucket (by pointer).
 *
 * @param bucket    Bucket
 * @param node      Node
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_remove_node_ptr(wingo_dht_bucket_t *bucket,
                                                wingo_dht_node_t *node);

/*
 * Find a node in bucket.
 *
 * @param bucket    Bucket
 * @param id        Node ID
 * @return          Node, or NULL if not found
 */
wingo_dht_node_t *wingo_dht_bucket_find_node(const wingo_dht_bucket_t *bucket,
                                              const wingo_dht_id_t *id);

/*
 * Get node at index.
 *
 * @param bucket    Bucket
 * @param index     Index
 * @return          Node, or NULL if out of range
 */
wingo_dht_node_t *wingo_dht_bucket_get_node(const wingo_dht_bucket_t *bucket,
                                             wingo_size index);

/*
 * Get first node.
 *
 * @param bucket    Bucket
 * @return          Node, or NULL if empty
 */
wingo_dht_node_t *wingo_dht_bucket_first_node(const wingo_dht_bucket_t *bucket);

/*
 * Get last node.
 *
 * @param bucket    Bucket
 * @return          Node, or NULL if empty
 */
wingo_dht_node_t *wingo_dht_bucket_last_node(const wingo_dht_bucket_t *bucket);

/*
 * Get a random node from bucket.
 *
 * @param bucket    Bucket
 * @return          Node, or NULL if empty
 */
wingo_dht_node_t *wingo_dht_bucket_random_node(const wingo_dht_bucket_t *bucket);

/*
 * Get next node in bucket.
 *
 * @param bucket    Bucket
 * @param node      Current node (NULL = first)
 * @return          Next node, or NULL if end
 */
wingo_dht_node_t *wingo_dht_bucket_next_node(const wingo_dht_bucket_t *bucket,
                                              const wingo_dht_node_t *node);

/* ============================================================================
 * BUCKET CACHED NODE
 * ============================================================================ */

/*
 * Set cached node.
 *
 * @param bucket    Bucket
 * @param node      Node
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_set_cached(wingo_dht_bucket_t *bucket,
                                           const wingo_dht_node_t *node);

/*
 * Get cached node.
 *
 * @param bucket    Bucket
 * @return          Cached node, or NULL if none
 */
wingo_dht_node_t *wingo_dht_bucket_get_cached(const wingo_dht_bucket_t *bucket);

/*
 * Clear cached node.
 *
 * @param bucket    Bucket
 */
void wingo_dht_bucket_clear_cached(wingo_dht_bucket_t *bucket);

/*
 * Check if bucket has cached node.
 *
 * @param bucket    Bucket
 * @return          true if has cached, false otherwise
 */
bool wingo_dht_bucket_has_cached(const wingo_dht_bucket_t *bucket);

/* ============================================================================
 * BUCKET SPLIT
 * ============================================================================ */

/*
 * Calculate bucket middle ID.
 *
 * @param bucket    Bucket
 * @param out       Output middle ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_middle(const wingo_dht_bucket_t *bucket,
                                       wingo_dht_id_t *out);

/*
 * Calculate random ID in bucket range.
 *
 * @param bucket    Bucket
 * @param out       Output random ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_random_id(const wingo_dht_bucket_t *bucket,
                                          wingo_dht_id_t *out);

/*
 * Split bucket into two.
 *
 * @param bucket    Bucket to split
 * @return          New bucket (second half), or NULL on error
 */
wingo_dht_bucket_t *wingo_dht_bucket_split(wingo_dht_bucket_t *bucket);

/* ============================================================================
 * BUCKET NODE FILTERING
 * ============================================================================ */

/*
 * Get good nodes from bucket.
 *
 * @param bucket    Bucket
 * @param nodes     Output array
 * @param max       Maximum number of nodes
 * @return          Number of nodes written
 */
wingo_size wingo_dht_bucket_good_nodes(const wingo_dht_bucket_t *bucket,
                                        wingo_dht_node_t **nodes,
                                        wingo_size max);

/*
 * Get nodes closest to target ID.
 *
 * @param bucket    Bucket
 * @param target    Target ID
 * @param nodes     Output array
 * @param max       Maximum number of nodes
 * @return          Number of nodes written
 */
wingo_size wingo_dht_bucket_closest_nodes(const wingo_dht_bucket_t *bucket,
                                           const wingo_dht_id_t *target,
                                           wingo_dht_node_t **nodes,
                                           wingo_size max);

/*
 * Expire old nodes from bucket.
 *
 * @param bucket    Bucket
 * @return          Number of nodes expired
 */
wingo_size wingo_dht_bucket_expire(wingo_dht_bucket_t *bucket);

/*
 * Check if bucket needs refresh.
 *
 * @param bucket    Bucket
 * @param max_age_s Maximum age in seconds
 * @return          true if needs refresh, false otherwise
 */
bool wingo_dht_bucket_needs_refresh(const wingo_dht_bucket_t *bucket,
                                     wingo_i64 max_age_s);

/* ============================================================================
 * BUCKET STATISTICS
 * ============================================================================ */

/*
 * Bucket statistics.
 */
typedef struct {
    wingo_u64   good;           /* Good nodes */
    wingo_u64   dubious;        /* Dubious nodes */
    wingo_u64   bad;            /* Bad nodes */
    wingo_u64   cached;         /* Cached node (0 or 1) */
    wingo_size  count;          /* Total nodes */
    wingo_size  max_count;      /* Max nodes */
    wingo_i64   age;            /* Age in seconds */
} wingo_dht_bucket_stats_t;

/*
 * Get bucket statistics.
 *
 * @param bucket    Bucket
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bucket_get_stats(const wingo_dht_bucket_t *bucket,
                                          wingo_dht_bucket_stats_t *stats);

/* ============================================================================
 * BUCKET UTILITY
 * ============================================================================ */

/*
 * Print bucket.
 *
 * @param bucket    Bucket
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_bucket_print(const wingo_dht_bucket_t *bucket, FILE *f);

/*
 * Print bucket nodes.
 *
 * @param bucket    Bucket
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_bucket_print_nodes(const wingo_dht_bucket_t *bucket, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_BUCKET_H */
