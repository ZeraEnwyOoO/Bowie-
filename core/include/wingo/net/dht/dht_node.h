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

#ifndef WINGO_NET_DHT_NODE_H
#define WINGO_NET_DHT_NODE_H

/*
 * ============================================================================
 * WINGO DHT NODE
 * ============================================================================
 *
 * This header provides DHT node management for Bowie.
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT NODE                                 │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  ID (160-bit)                                       │   │
 *   │   │  Address (IP + Port)                                │   │
 *   │   │  State (Good, Dubious, Bad)                         │   │
 *   │   │  Timestamps (Seen, Reply, Pinged)                   │   │
 *   │   │  Ping Count                                         │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
│   │   Node Lifecycle:                                           │
│   │   ├── Created                                               │
│   │   ├── Pinged                                                │
│   │   ├── Replied                                               │
│   │   └── Expired                                               │
│   │                                                             │
│   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/dht.h"

/* ============================================================================
 * DHT NODE CONSTANTS
 * ============================================================================ */

/*
 * Node state.
 */
typedef enum {
    WINGO_DHT_NODE_STATE_UNKNOWN  = 0,   /* Unknown state */
    WINGO_DHT_NODE_STATE_GOOD     = 1,   /* Good (replied recently) */
    WINGO_DHT_NODE_STATE_DUBIOUS  = 2,   /* Dubious (pinged, no reply) */
    WINGO_DHT_NODE_STATE_BAD      = 3,   /* Bad (pinged 3+ times) */
} wingo_dht_node_state_t;

/*
 * Maximum ping count before node is considered bad.
 */
#define WINGO_DHT_NODE_MAX_PING     3

/*
 * Node good timeout (30 minutes).
 */
#define WINGO_DHT_NODE_GOOD_TIMEOUT (30 * 60)

/*
 * Node dubious timeout (15 minutes).
 */
#define WINGO_DHT_NODE_DUBIOUS_TIMEOUT (15 * 60)

/* ============================================================================
 * DHT NODE INFO STRUCTURE
 * ============================================================================ */

/*
 * Node info (ID + address).
 *
 * This is used for message parsing and passing node data.
 */
typedef struct {
    wingo_dht_id_t  id;         /* Node ID */
    wingo_addr_t    addr;       /* Node address */
} wingo_dht_node_info_t;

/* ============================================================================
 * DHT NODE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT node handle.
 */
typedef struct wingo_dht_node wingo_dht_node_t;

/* ============================================================================
 * NODE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT node.
 *
 * @param id        Node ID
 * @param addr      Node address
 * @return          Node, or NULL on error
 */
wingo_dht_node_t *wingo_dht_node_new(const wingo_dht_id_t *id,
                                      const wingo_addr_t *addr);

/*
 * Free a DHT node.
 *
 * @param node      Node (NULL is safe)
 */
void wingo_dht_node_free(wingo_dht_node_t *node);

/*
 * Clone a DHT node.
 *
 * @param node      Node
 * @return          New node, or NULL on error
 */
wingo_dht_node_t *wingo_dht_node_clone(const wingo_dht_node_t *node);

/* ============================================================================
 * NODE INFO
 * ============================================================================ */

/*
 * Get node ID.
 *
 * @param node      Node
 * @return          Node ID, or NULL on error
 */
const wingo_dht_id_t *wingo_dht_node_id(const wingo_dht_node_t *node);

/*
 * Get node address.
 *
 * @param node      Node
 * @return          Node address, or NULL on error
 */
const wingo_addr_t *wingo_dht_node_addr(const wingo_dht_node_t *node);

/*
 * Set node address.
 *
 * @param node      Node
 * @param addr      New address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_set_addr(wingo_dht_node_t *node,
                                       const wingo_addr_t *addr);

/*
 * Get node address family.
 *
 * @param node      Node
 * @return          Address family
 */
wingo_addr_family_t wingo_dht_node_family(const wingo_dht_node_t *node);

/*
 * Get node info.
 *
 * @param node      Node
 * @param info      Output info
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_info(const wingo_dht_node_t *node,
                                   wingo_dht_node_info_t *info);

/* ============================================================================
 * NODE STATE
 * ============================================================================ */

/*
 * Get node state.
 *
 * @param node      Node
 * @return          Node state
 */
wingo_dht_node_state_t wingo_dht_node_state(const wingo_dht_node_t *node);

/*
 * Set node state.
 *
 * @param node      Node
 * @param state     New state
 */
void wingo_dht_node_set_state(wingo_dht_node_t *node,
                               wingo_dht_node_state_t state);

/*
 * Check if node is good.
 *
 * @param node      Node
 * @return          true if good, false otherwise
 */
bool wingo_dht_node_is_good(const wingo_dht_node_t *node);

/*
 * Check if node is dubious.
 *
 * @param node      Node
 * @return          true if dubious, false otherwise
 */
bool wingo_dht_node_is_dubious(const wingo_dht_node_t *node);

/*
 * Check if node is bad.
 *
 * @param node      Node
 * @return          true if bad, false otherwise
 */
bool wingo_dht_node_is_bad(const wingo_dht_node_t *node);

/*
 * Check if node is unknown.
 *
 * @param node      Node
 * @return          true if unknown, false otherwise
 */
bool wingo_dht_node_is_unknown(const wingo_dht_node_t *node);

/* ============================================================================
 * NODE TIMESTAMPS
 * ============================================================================ */

/*
 * Get node last seen time.
 *
 * @param node      Node
 * @return          Last seen timestamp (Unix seconds)
 */
wingo_i64 wingo_dht_node_last_seen(const wingo_dht_node_t *node);

/*
 * Get node last reply time.
 *
 * @param node      Node
 * @return          Last reply timestamp (Unix seconds)
 */
wingo_i64 wingo_dht_node_last_reply(const wingo_dht_node_t *node);

/*
 * Get node last pinged time.
 *
 * @param node      Node
 * @return          Last pinged timestamp (Unix seconds)
 */
wingo_i64 wingo_dht_node_last_pinged(const wingo_dht_node_t *node);

/*
 * Get node ping count.
 *
 * @param node      Node
 * @return          Ping count
 */
int wingo_dht_node_ping_count(const wingo_dht_node_t *node);

/*
 * Update node last seen time.
 *
 * @param node      Node
 */
void wingo_dht_node_update_seen(wingo_dht_node_t *node);

/*
 * Update node last reply time.
 *
 * @param node      Node
 */
void wingo_dht_node_update_reply(wingo_dht_node_t *node);

/*
 * Update node last pinged time.
 *
 * @param node      Node
 */
void wingo_dht_node_update_pinged(wingo_dht_node_t *node);

/*
 * Increment node ping count.
 *
 * @param node      Node
 */
void wingo_dht_node_increment_ping(wingo_dht_node_t *node);

/*
 * Reset node ping count.
 *
 * @param node      Node
 */
void wingo_dht_node_reset_ping(wingo_dht_node_t *node);

/*
 * Update node on pinged.
 *
 * @param node      Node
 */
void wingo_dht_node_pinged(wingo_dht_node_t *node);

/*
 * Update node on replied.
 *
 * @param node      Node
 */
void wingo_dht_node_replied(wingo_dht_node_t *node);

/* ============================================================================
 * NODE TIMEOUT
 * ============================================================================ */

/*
 * Check if node is timed out.
 *
 * @param node      Node
 * @param timeout_s Timeout in seconds
 * @return          true if timed out, false otherwise
 */
bool wingo_dht_node_is_timed_out(const wingo_dht_node_t *node,
                                  wingo_i64 timeout_s);

/*
 * Check if node needs ping.
 *
 * @param node      Node
 * @return          true if needs ping, false otherwise
 */
bool wingo_dht_node_needs_ping(const wingo_dht_node_t *node);

/*
 * Check if node is expired.
 *
 * @param node      Node
 * @return          true if expired, false otherwise
 */
bool wingo_dht_node_is_expired(const wingo_dht_node_t *node);

/* ============================================================================
 * NODE STATISTICS
 * ============================================================================ */

/*
 * Node statistics.
 */
typedef struct {
    wingo_i64   age;            /* Age in seconds */
    wingo_i64   reply_age;      /* Time since last reply */
    wingo_i64   ping_age;       /* Time since last ping */
    int         ping_count;     /* Ping count */
    bool        is_good;        /* Is good */
    bool        is_dubious;     /* Is dubious */
    bool        is_bad;         /* Is bad */
} wingo_dht_node_stats_t;

/*
 * Get node statistics.
 *
 * @param node      Node
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_get_stats(const wingo_dht_node_t *node,
                                        wingo_dht_node_stats_t *stats);

/* ============================================================================
 * NODE COMPARISON
 * ============================================================================ */

/*
 * Compare two nodes by ID.
 *
 * @param a         First node
 * @param b         Second node
 * @return          0 if equal, <0 if a<b, >0 if a>b
 */
int wingo_dht_node_cmp(const wingo_dht_node_t *a, const wingo_dht_node_t *b);

/*
 * Compare node ID with another ID.
 *
 * @param node      Node
 * @param id        ID
 * @return          0 if equal, <0 if node<id, >0 if node>id
 */
int wingo_dht_node_cmp_id(const wingo_dht_node_t *node,
                           const wingo_dht_id_t *id);

/*
 * Check if node has ID.
 *
 * @param node      Node
 * @param id        ID
 * @return          true if equal, false otherwise
 */
bool wingo_dht_node_has_id(const wingo_dht_node_t *node,
                            const wingo_dht_id_t *id);

/*
 * Check if node has address.
 *
 * @param node      Node
 * @param addr      Address
 * @return          true if equal, false otherwise
 */
bool wingo_dht_node_has_addr(const wingo_dht_node_t *node,
                              const wingo_addr_t *addr);

/* ============================================================================
 * NODE LIST
 * ============================================================================ */

/*
 * Create a node list.
 *
 * @param capacity  Initial capacity
 * @return          Node list, or NULL on error
 */
wingo_dht_node_list_t *wingo_dht_node_list_new(wingo_size capacity);

/*
 * Free a node list.
 *
 * @param list      Node list (NULL is safe)
 */
void wingo_dht_node_list_free(wingo_dht_node_list_t *list);

/*
 * Add a node to list.
 *
 * @param list      Node list
 * @param node      Node
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_list_add(wingo_dht_node_list_t *list,
                                       wingo_dht_node_t *node);

/*
 * Remove a node from list.
 *
 * @param list      Node list
 * @param id        Node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_list_remove(wingo_dht_node_list_t *list,
                                          const wingo_dht_id_t *id);

/*
 * Get node from list.
 *
 * @param list      Node list
 * @param index     Index
 * @return          Node, or NULL if out of range
 */
wingo_dht_node_t *wingo_dht_node_list_get(const wingo_dht_node_list_t *list,
                                           wingo_size index);

/*
 * Get node count.
 *
 * @param list      Node list
 * @return          Node count
 */
wingo_size wingo_dht_node_list_count(const wingo_dht_node_list_t *list);

/*
 * Find node in list.
 *
 * @param list      Node list
 * @param id        Node ID
 * @return          Node, or NULL if not found
 */
wingo_dht_node_t *wingo_dht_node_list_find(const wingo_dht_node_list_t *list,
                                            const wingo_dht_id_t *id);

/*
 * Clear node list.
 *
 * @param list      Node list
 */
void wingo_dht_node_list_clear(wingo_dht_node_list_t *list);

/*
 * Sort node list by XOR distance to target.
 *
 * @param list      Node list
 * @param target    Target ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_node_list_sort(wingo_dht_node_list_t *list,
                                        const wingo_dht_id_t *target);

/* ============================================================================
 * NODE LIST ITERATION
 * ============================================================================ */

/*
 * Node callback.
 *
 * @param node      Node
 * @param userdata  User data
 * @return          true to continue, false to stop
 */
typedef bool (*wingo_dht_node_cb_t)(wingo_dht_node_t *node, void *userdata);

/*
 * Iterate over node list.
 *
 * @param list      Node list
 * @param callback  Callback
 * @param userdata  User data
 */
void wingo_dht_node_list_foreach(wingo_dht_node_list_t *list,
                                  wingo_dht_node_cb_t callback,
                                  void *userdata);

/* ============================================================================
 * NODE UTILITY
 * ============================================================================ */

/*
 * Get node state name.
 *
 * @param state     Node state
 * @return          Static string
 */
const char *wingo_dht_node_state_name(wingo_dht_node_state_t state);

/*
 * Print node.
 *
 * @param node      Node
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_node_print(const wingo_dht_node_t *node, FILE *f);

/*
 * Print node list.
 *
 * @param list      Node list
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_node_list_print(const wingo_dht_node_list_t *list, FILE *f);

/* ============================================================================
 * NODE LIST STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT node list handle.
 */
typedef struct wingo_dht_node_list wingo_dht_node_list_t;

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_NODE_H */
