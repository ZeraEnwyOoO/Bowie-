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

#ifndef WINGO_NET_DHT_SEARCH_H
#define WINGO_NET_DHT_SEARCH_H

/*
 * ============================================================================
 * WINGO DHT SEARCH
 * ============================================================================
 *
 * This header provides the Kademlia search (iterative lookup) for Bowie DHT.
 *
 * Design principle: PURE ALGORITHM
 *
 *   This module does NOT perform network I/O. It is a state machine that:
 *     - Knows which nodes to query next
 *     - Tracks replies and timeouts
 *     - Decides when the search has converged
 *
 *   The caller (DHT) is responsible for:
 *     - Sending query messages to nodes
 *     - Feeding responses back via wingo_dht_search_insert_node()
 *     - Calling wingo_dht_search_step() to get the next node to query
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT SEARCH                               │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Search State                                       │   │
 *   │   │  ├── Target ID (infohash or node ID)                │   │
 *   │   │  ├── Node List (closest seen, sorted)               │   │
 *   │   │  ├── In-flight queries                              │   │
 *   │   │  └── State (INIT, RUNNING, DONE, ...)               │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
 *   │   Iterative Lookup:                                         │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  1. caller calls step()                             │   │
 *   │   │  2. step() returns next node to query               │   │
 *   │   │  3. caller sends query, gets response               │   │
 *   │   │  4. caller calls insert_node() with new nodes       │   │
 *   │   │  5. caller calls node_replied() for the queried node│   │
 *   │   │  6. repeat until is_done() returns true             │   │
 *   │   └─────────────────────────────────────────────────────┘   │
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

/* ============================================================================
 * DHT SEARCH CONSTANTS
 * ============================================================================ */

/*
 * Maximum nodes in a search.
 */
#define WINGO_DHT_SEARCH_MAX_NODES      64

/*
 * Maximum in-flight queries.
 */
#define WINGO_DHT_SEARCH_INFLIGHT       4

/*
 * Search retransmit time (seconds).
 */
#define WINGO_DHT_SEARCH_RETRANSMIT     10

/*
 * Search expire time (62 minutes).
 */
#define WINGO_DHT_SEARCH_EXPIRE         (62 * 60)

/*
 * Maximum number of concurrent searches.
 */
#define WINGO_DHT_SEARCH_MAX_SEARCHES   1024

/* ============================================================================
 * DHT SEARCH TYPES
 * ============================================================================ */

/*
 * Search type.
 */
typedef enum {
    WINGO_DHT_SEARCH_TYPE_FIND_NODE  = 0,   /* Find nodes close to target */
    WINGO_DHT_SEARCH_TYPE_GET_PEERS  = 1,   /* Find peers for info hash */
    WINGO_DHT_SEARCH_TYPE_ANNOUNCE   = 2,   /* Announce ourselves */
} wingo_dht_search_type_t;

/*
 * Search state.
 */
typedef enum {
    WINGO_DHT_SEARCH_STATE_INIT      = 0,   /* Created, not started */
    WINGO_DHT_SEARCH_STATE_RUNNING   = 1,   /* In progress */
    WINGO_DHT_SEARCH_STATE_DONE      = 2,   /* Completed successfully */
    WINGO_DHT_SEARCH_STATE_TIMEOUT   = 3,   /* Timed out */
    WINGO_DHT_SEARCH_STATE_CANCELED  = 4,   /* Canceled by caller */
    WINGO_DHT_SEARCH_STATE_ERROR     = 5,   /* Error occurred */
} wingo_dht_search_state_t;

/*
 * Search step result.
 */
typedef enum {
    WINGO_DHT_STEP_DONE        = 0,   /* Search is done, no more queries */
    WINGO_DHT_STEP_QUERY       = 1,   /* Query the returned node */
    WINGO_DHT_STEP_WAIT        = 2,   /* Wait for in-flight queries */
    WINGO_DHT_STEP_TIMEOUT     = 3,   /* Search timed out */
} wingo_dht_step_result_t;

/* ============================================================================
 * DHT SEARCH STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT search handle.
 */
typedef struct wingo_dht_search wingo_dht_search_t;

/* ============================================================================
 * DHT SEARCH MANAGER STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT search manager handle.
 */
typedef struct wingo_dht_search_mgr wingo_dht_search_mgr_t;

/* ============================================================================
 * SEARCH CALLBACKS
 * ============================================================================ */

/*
 * Search peer callback.
 *
 * Called when a peer address is discovered.
 *
 * @param addr      Peer address
 * @param userdata  User data
 */
typedef void (*wingo_dht_search_peer_cb_t)(const wingo_addr_t *addr,
                                            void *userdata);

/*
 * Search done callback.
 *
 * Called when search is completed (success, timeout, or error).
 *
 * @param result    Result code (WINGO_SUCCESS, WINGO_ERR_TIMEOUT, ...)
 * @param userdata  User data
 */
typedef void (*wingo_dht_search_done_cb_t)(wingo_error_t result,
                                            void *userdata);

/* ============================================================================
 * SEARCH MANAGER LIFECYCLE
 * ============================================================================ */

/*
 * Create a new search manager.
 *
 * @param max_searches  Maximum number of concurrent searches
 * @return              Search manager, or NULL on error
 */
wingo_dht_search_mgr_t *wingo_dht_search_mgr_new(wingo_size max_searches);

/*
 * Free a search manager.
 *
 * @param mgr       Search manager (NULL is safe)
 */
void wingo_dht_search_mgr_free(wingo_dht_search_mgr_t *mgr);

/* ============================================================================
 * SEARCH LIFECYCLE
 * ============================================================================ */

/*
 * Create a new search.
 *
 * @param mgr       Search manager
 * @param type      Search type
 * @param target    Target ID
 * @param port      Port (for announce, 0 otherwise)
 * @return          Search, or NULL on error
 */
wingo_dht_search_t *wingo_dht_search_new(wingo_dht_search_mgr_t *mgr,
                                          wingo_dht_search_type_t type,
                                          const wingo_dht_id_t *target,
                                          wingo_u16 port);

/*
 * Free a search.
 *
 * @param search    Search (NULL is safe)
 */
void wingo_dht_search_free(wingo_dht_search_t *search);

/*
 * Start a search.
 *
 * @param search    Search
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_search_start(wingo_dht_search_t *search);

/*
 * Cancel a search.
 *
 * @param search    Search
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_search_cancel(wingo_dht_search_t *search);

/* ============================================================================
 * SEARCH QUERY
 * ============================================================================ */

/*
 * Get search type.
 */
wingo_dht_search_type_t wingo_dht_search_type(
    const wingo_dht_search_t *search);

/*
 * Get search state.
 */
wingo_dht_search_state_t wingo_dht_search_state(
    const wingo_dht_search_t *search);

/*
 * Get search target ID.
 */
const wingo_dht_id_t *wingo_dht_search_target(
    const wingo_dht_search_t *search);

/*
 * Get search port.
 */
wingo_u16 wingo_dht_search_port(const wingo_dht_search_t *search);

/*
 * Get search transaction ID.
 */
wingo_u16 wingo_dht_search_tid(const wingo_dht_search_t *search);

/*
 * Check if search is done.
 */
bool wingo_dht_search_is_done(const wingo_dht_search_t *search);

/*
 * Check if search is active.
 */
bool wingo_dht_search_is_active(const wingo_dht_search_t *search);

/*
 * Get search age (seconds).
 */
wingo_i64 wingo_dht_search_age(const wingo_dht_search_t *search);

/* ============================================================================
 * SEARCH NODE MANAGEMENT
 * ============================================================================ */

/*
 * Get search node count.
 */
wingo_size wingo_dht_search_node_count(const wingo_dht_search_t *search);

/*
 * Get search node at index.
 */
const wingo_dht_node_info_t *wingo_dht_search_node(
    const wingo_dht_search_t *search,
    wingo_size index);

/*
 * Insert a node into search.
 *
 * Caller provides node ID + address. Search takes ownership of
 * the address (copies it).
 *
 * @param search    Search
 * @param id        Node ID
 * @param addr      Node address
 * @param replied   Whether node has replied
 * @param token     Token (may be NULL)
 * @param token_len Token length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_search_insert_node(wingo_dht_search_t *search,
                                            const wingo_dht_id_t *id,
                                            const wingo_addr_t *addr,
                                            bool replied,
                                            const wingo_u8 *token,
                                            wingo_size token_len);

/*
 * Remove a node from search.
 */
wingo_error_t wingo_dht_search_remove_node(wingo_dht_search_t *search,
                                            const wingo_dht_id_t *id);

/*
 * Mark a node as replied.
 */
wingo_error_t wingo_dht_search_node_replied(wingo_dht_search_t *search,
                                             const wingo_dht_id_t *id,
                                             const wingo_u8 *token,
                                             wingo_size token_len);

/*
 * Mark a node as acked (announce).
 */
wingo_error_t wingo_dht_search_node_acked(wingo_dht_search_t *search,
                                           const wingo_dht_id_t *id);

/*
 * Get closest nodes from search.
 */
wingo_size wingo_dht_search_closest_nodes(wingo_dht_search_t *search,
                                           wingo_dht_node_info_t *nodes,
                                           wingo_size max);

/* ============================================================================
 * SEARCH STEP
 * ============================================================================ */

/*
 * Step the search.
 *
 * This is the main algorithm entry point. Caller calls this
 * repeatedly to drive the search forward.
 *
 * @param search    Search
 * @param out_node  Output: node to query (if result is QUERY)
 * @param out_addr  Output: address to query (if result is QUERY)
 * @return          Step result
 */
wingo_dht_step_result_t wingo_dht_search_step(
    wingo_dht_search_t *search,
    wingo_dht_node_info_t *out_node);

/*
 * Get next step time.
 *
 * @param search    Search
 * @return          Unix timestamp of next step, or 0 if no step needed
 */
wingo_i64 wingo_dht_search_next_step(const wingo_dht_search_t *search);

/*
 * Check if search needs step.
 *
 * @param search    Search
 * @return          true if needs step, false otherwise
 */
bool wingo_dht_search_needs_step(const wingo_dht_search_t *search);

/* ============================================================================
 * SEARCH CALLBACKS
 * ============================================================================ */

/*
 * Set search peer callback.
 */
wingo_error_t wingo_dht_search_set_peer_callback(
    wingo_dht_search_t *search,
    wingo_dht_search_peer_cb_t callback,
    void *userdata);

/*
 * Set search done callback.
 */
wingo_error_t wingo_dht_search_set_done_callback(
    wingo_dht_search_t *search,
    wingo_dht_search_done_cb_t callback,
    void *userdata);

/* ============================================================================
 * SEARCH MANAGER OPERATIONS
 * ============================================================================ */

/*
 * Find a search by transaction ID.
 */
wingo_dht_search_t *wingo_dht_search_mgr_find(
    wingo_dht_search_mgr_t *mgr,
    wingo_u16 tid);

/*
 * Get search count.
 */
wingo_size wingo_dht_search_mgr_count(const wingo_dht_search_mgr_t *mgr);

/*
 * Get active search count.
 */
wingo_size wingo_dht_search_mgr_active_count(
    const wingo_dht_search_mgr_t *mgr);

/*
 * Step all searches.
 *
 * @param mgr       Search manager
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_search_mgr_step(wingo_dht_search_mgr_t *mgr);

/*
 * Expire old searches.
 *
 * @param mgr       Search manager
 * @return          Number of searches expired
 */
wingo_size wingo_dht_search_mgr_expire(wingo_dht_search_mgr_t *mgr);

/*
 * Get next step time for all searches.
 */
wingo_i64 wingo_dht_search_mgr_next_step(
    const wingo_dht_search_mgr_t *mgr);

/*
 * Clear all searches.
 */
void wingo_dht_search_mgr_clear(wingo_dht_search_mgr_t *mgr);

/* ============================================================================
 * SEARCH STATISTICS
 * ============================================================================ */

/*
 * Search statistics.
 */
typedef struct {
    wingo_u64   searches_started;
    wingo_u64   searches_done;
    wingo_u64   searches_timeout;
    wingo_u64   searches_error;
    wingo_u64   queries_sent;
    wingo_u64   queries_replied;
    wingo_u64   nodes_found;
    wingo_u64   peers_found;
} wingo_dht_search_stats_t;

/*
 * Get search manager statistics.
 */
wingo_error_t wingo_dht_search_mgr_get_stats(
    const wingo_dht_search_mgr_t *mgr,
    wingo_dht_search_stats_t *stats);

/*
 * Reset search manager statistics.
 */
void wingo_dht_search_mgr_reset_stats(wingo_dht_search_mgr_t *mgr);

/* ============================================================================
 * SEARCH UTILITY
 * ============================================================================ */

/*
 * Get search type name.
 */
const char *wingo_dht_search_type_name(wingo_dht_search_type_t type);

/*
 * Get search state name.
 */
const char *wingo_dht_search_state_name(wingo_dht_search_state_t state);

/*
 * Get step result name.
 */
const char *wingo_dht_step_result_name(wingo_dht_step_result_t result);

/*
 * Print search.
 */
void wingo_dht_search_print(const wingo_dht_search_t *search, FILE *f);

/*
 * Print search manager.
 */
void wingo_dht_search_mgr_print(const wingo_dht_search_mgr_t *mgr, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_SEARCH_H */
