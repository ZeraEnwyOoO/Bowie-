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

#ifndef WINGO_NET_DHT_ROUTING_H
#define WINGO_NET_DHT_ROUTING_H

/*
 * ============================================================================
 * WINGO DHT ROUTING TABLE
 * ============================================================================
 *
 * This header provides the Kademlia routing table for Bowie DHT.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    ROUTING TABLE                            │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │  Bucket 0   │  │  Bucket 1   │  │  Bucket N   │        │
 *   │   │             │  │             │  │             │        │
 *   │   │  - Nodes    │  │  - Nodes    │  │  - Nodes    │        │
 *   │   │  - Max 8    │  │  - Max 8    │  │  - Max 8    │        │
 *   │   │             │  │             │  │             │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   │   Bucket Selection:                                         │
 *   │   ├── Based on XOR distance                                 │
 *   │   ├── Common prefix length                                  │
 *   │   └── Bucket splitting when full                            │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/dht/dht_types.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/net/dht/dht_bucket.h"

/* ============================================================================
 * ROUTING TABLE CONSTANTS
 * ============================================================================ */

/*
 * Maximum number of buckets (160 bits = 160 buckets).
 */
#define WINGO_DHT_MAX_BUCKETS       160

/*
 * Default bucket size (Kademlia K).
 */
#define WINGO_DHT_BUCKET_SIZE       8

/*
 * Minimum bucket size.
 */
#define WINGO_DHT_MIN_BUCKET_SIZE   4

/* ============================================================================
 * ROUTING TABLE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Routing table handle.
 *
 * NOTE: This is the ONLY definition of this opaque type in Bowie.
 */
typedef struct wingo_dht_routing wingo_dht_routing_t;

/* ============================================================================
 * ROUTING TABLE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new routing table.
 *
 * @param my_id     My node ID
 * @param af        Address family
 * @param bucket_size Bucket size (0 = default)
 * @return          Routing table, or NULL on error
 */
wingo_dht_routing_t *wingo_dht_routing_new(const wingo_dht_id_t *my_id,
                                            wingo_addr_family_t af,
                                            wingo_size bucket_size);

/*
 * Free a routing table.
 *
 * @param rt        Routing table (NULL is safe)
 */
void wingo_dht_routing_free(wingo_dht_routing_t *rt);

/* ============================================================================
 * ROUTING TABLE OPERATIONS
 * ============================================================================ */

/*
 * Find bucket for an ID.
 *
 * @param rt        Routing table
 * @param id        Node ID
 * @return          Bucket, or NULL on error
 */
wingo_dht_bucket_t *wingo_dht_routing_find_bucket(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id);

/*
 * Find a node in routing table.
 *
 * @param rt        Routing table
 * @param id        Node ID
 * @return          Node, or NULL if not found
 */
wingo_dht_node_t *wingo_dht_routing_find_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id);

/*
 * Add a node to routing table.
 *
 * @param rt        Routing table
 * @param id        Node ID
 * @param addr      Node address
 * @param confirm   Confirmation level (0 = no, 1 = pinged, 2 = replied)
 * @return          Node, or NULL on error
 */
wingo_dht_node_t *wingo_dht_routing_add_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id,
    const wingo_addr_t *addr,
    int confirm);

/*
 * Remove a node from routing table.
 *
 * @param rt        Routing table
 * @param id        Node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_routing_remove_node(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *id);

/*
 * Get closest nodes to a target ID.
 *
 * @param rt        Routing table
 * @param target    Target ID
 * @param nodes     Output array
 * @param max       Maximum number of nodes
 * @return          Number of nodes written
 */
wingo_size wingo_dht_routing_closest_nodes(
    wingo_dht_routing_t *rt,
    const wingo_dht_id_t *target,
    wingo_dht_node_t **nodes,
    wingo_size max);

/*
 * Get random nodes.
 *
 * @param rt        Routing table
 * @param nodes     Output array
 * @param max       Maximum number of nodes
 * @return          Number of nodes written
 */
wingo_size wingo_dht_routing_random_nodes(
    wingo_dht_routing_t *rt,
    wingo_dht_node_t **nodes,
    wingo_size max);

/* ============================================================================
 * ROUTING TABLE MAINTENANCE
 * ============================================================================ */

/*
 * Split a bucket.
 *
 * @param rt        Routing table
 * @param bucket    Bucket to split
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_routing_split_bucket(
    wingo_dht_routing_t *rt,
    wingo_dht_bucket_t *bucket);

/*
 * Expire old nodes.
 *
 * @param rt        Routing table
 * @return          Number of nodes expired
 */
wingo_size wingo_dht_routing_expire(wingo_dht_routing_t *rt);

/*
 * Bucket maintenance.
 *
 * @param rt        Routing table
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_routing_maintenance(wingo_dht_routing_t *rt);

/*
 * Neighbourhood maintenance.
 *
 * @param rt        Routing table
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_routing_neighbourhood(
    wingo_dht_routing_t *rt);

/* ============================================================================
 * ROUTING TABLE QUERY
 * ============================================================================ */

/*
 * Get number of buckets.
 *
 * @param rt        Routing table
 * @return          Number of buckets
 */
wingo_size wingo_dht_routing_bucket_count(const wingo_dht_routing_t *rt);

/*
 * Get total number of nodes.
 *
 * @param rt        Routing table
 * @return          Number of nodes
 */
wingo_size wingo_dht_routing_node_count(const wingo_dht_routing_t *rt);

/*
 * Get number of good nodes.
 *
 * @param rt        Routing table
 * @return          Number of good nodes
 */
wingo_size wingo_dht_routing_good_count(const wingo_dht_routing_t *rt);

/*
 * Get number of dubious nodes.
 *
 * @param rt        Routing table
 * @return          Number of dubious nodes
 */
wingo_size wingo_dht_routing_dubious_count(const wingo_dht_routing_t *rt);

/*
 * Get number of bad nodes.
 *
 * @param rt        Routing table
 * @return          Number of bad nodes
 */
wingo_size wingo_dht_routing_bad_count(const wingo_dht_routing_t *rt);

/*
 * Get first bucket.
 *
 * @param rt        Routing table
 * @return          First bucket, or NULL if empty
 */
wingo_dht_bucket_t *wingo_dht_routing_first_bucket(
    wingo_dht_routing_t *rt);

/*
 * Get next bucket.
 *
 * @param bucket    Current bucket
 * @return          Next bucket, or NULL if last
 */
wingo_dht_bucket_t *wingo_dht_routing_next_bucket(
    wingo_dht_bucket_t *bucket);

/*
 * Get previous bucket.
 *
 * @param bucket    Current bucket
 * @return          Previous bucket, or NULL if first
 */
wingo_dht_bucket_t *wingo_dht_routing_prev_bucket(
    wingo_dht_bucket_t *bucket);

/* ============================================================================
 * ROUTING TABLE UTILITY
 * ============================================================================ */

/*
 * Print routing table.
 *
 * @param rt        Routing table
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_routing_print(const wingo_dht_routing_t *rt, FILE *f);

/*
 * Print bucket.
 *
 * @param bucket    Bucket
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_bucket_print(const wingo_dht_bucket_t *bucket, FILE *f);

/*
 * Print node.
 *
 * @param node      Node
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_node_print(const wingo_dht_node_t *node, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_ROUTING_H */
