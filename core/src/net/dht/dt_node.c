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
 * DHT node implementation for Bowie.
 *
 * A node represents a remote peer in the DHT network.
 * Each node has:
 *   - A 160-bit ID
 *   - An address (IP + port)
 *   - A state (good, dubious, bad, unknown)
 *   - Timestamps (last seen, last reply, last pinged)
 *   - A ping count
 *
 * Node lifecycle (Kademlia):
 *
 *   UNKNOWN ──(ping)──> DUBIOUS ──(reply)──> GOOD
 *      │                   │                   │
 *      │                   │ (3x no reply)     │
 *      │                   ▼                   │
 *      └───────────────> BAD <─────────────────┘
 *                         │
 *                         ▼
 *                      (expired/removed)
 *
 * Node list is a dynamic array of node pointers.
 * It supports:
 *   - Add/remove
 *   - Find by ID
 *   - Sort by XOR distance to a target
 *   - Iteration
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_node.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Node (concrete).
 */
struct wingo_dht_node {
    /* ----- Identity ----- */
    wingo_dht_id_t      id;             /* Node ID (160-bit) */
    wingo_addr_t       *addr;           /* Node address (owned) */

    /* ----- State ----- */
    wingo_dht_node_state_t state;       /* Node state */

    /* ----- Timestamps ----- */
    wingo_i64           first_seen;     /* When we first saw this node */
    wingo_i64           last_seen;      /* Last time we saw any activity */
    wingo_i64           last_reply;     /* Last time it replied to us */
    wingo_i64           last_pinged;    /* Last time we pinged it */

    /* ----- Counters ----- */
    int                 ping_count;     /* Number of pings without reply */
};

/*
 * Node list (concrete).
 */
struct wingo_dht_node_list {
    wingo_dht_node_t  **items;          /* Array of node pointers */
    wingo_size          count;          /* Number of nodes */
    wingo_size          capacity;       /* Allocated capacity */
    bool                owns_nodes;     /* Whether list owns its nodes */
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial capacity for node list.
 */
#define NODE_LIST_INIT_CAPACITY     16

/*
 * Growth factor for node list.
 */
#define NODE_LIST_GROW_FACTOR       2

/* ============================================================================
 * INTERNAL HELPERS — TIME
 * ============================================================================ */

/*
 * Get current time in seconds.
 */
static wingo_i64 node_now(void)
{
    return wingo_time_now();
}

/* ============================================================================
 * NODE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT node.
 */
wingo_dht_node_t *wingo_dht_node_new(const wingo_dht_id_t *id,
                                      const wingo_addr_t *addr)
{
    wingo_dht_node_t *node;
    wingo_i64 now;

    if (id == NULL) {
        return NULL;
    }

    node = calloc(1, sizeof(wingo_dht_node_t));
    if (node == NULL) {
        return NULL;
    }

    /* Copy ID */
    memcpy(&node->id, id, sizeof(wingo_dht_id_t));

    /* Copy address (if provided) */
    if (addr != NULL) {
        node->addr = wingo_addr_copy(addr);
        if (node->addr == NULL) {
            free(node);
            return NULL;
        }
    } else {
        node->addr = NULL;
    }

    /* Set initial state */
    node->state = WINGO_DHT_NODE_STATE_UNKNOWN;

    /* Set timestamps */
    now = node_now();
    node->first_seen = now;
    node->last_seen = now;
    node->last_reply = 0;
    node->last_pinged = 0;

    /* Set counters */
    node->ping_count = 0;

    return node;
}

/*
 * Free a DHT node.
 */
void wingo_dht_node_free(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    if (node->addr != NULL) {
        wingo_addr_free(node->addr);
    }

    free(node);
}

/*
 * Clone a DHT node.
 */
wingo_dht_node_t *wingo_dht_node_clone(const wingo_dht_node_t *node)
{
    wingo_dht_node_t *copy;

    if (node == NULL) {
        return NULL;
    }

    copy = wingo_dht_node_new(&node->id, node->addr);
    if (copy == NULL) {
        return NULL;
    }

    /* Copy state */
    copy->state = node->state;

    /* Copy timestamps */
    copy->first_seen = node->first_seen;
    copy->last_seen = node->last_seen;
    copy->last_reply = node->last_reply;
    copy->last_pinged = node->last_pinged;

    /* Copy counters */
    copy->ping_count = node->ping_count;

    return copy;
}

/* ============================================================================
 * NODE INFO
 * ============================================================================ */

/*
 * Get node ID.
 */
const wingo_dht_id_t *wingo_dht_node_id(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }

    return &node->id;
}

/*
 * Get node address.
 */
const wingo_addr_t *wingo_dht_node_addr(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return NULL;
    }

    return node->addr;
}

/*
 * Set node address.
 */
wingo_error_t wingo_dht_node_set_addr(wingo_dht_node_t *node,
                                       const wingo_addr_t *addr)
{
    wingo_addr_t *copy;

    if (node == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Copy new address */
    copy = wingo_addr_copy(addr);
    if (copy == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Free old address */
    if (node->addr != NULL) {
        wingo_addr_free(node->addr);
    }

    node->addr = copy;

    return WINGO_SUCCESS;
}

/*
 * Get node address family.
 */
wingo_addr_family_t wingo_dht_node_family(const wingo_dht_node_t *node)
{
    if (node == NULL || node->addr == NULL) {
        return WINGO_ADDR_UNSPEC;
    }

    return wingo_addr_family(node->addr);
}

/*
 * Get node info.
 */
wingo_error_t wingo_dht_node_info(const wingo_dht_node_t *node,
                                   wingo_dht_node_info_t *info)
{
    if (node == NULL || info == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(&info->id, &node->id, sizeof(wingo_dht_id_t));

    /*
     * NOTE: info->addr is a pointer owned by the caller.
     *       We do NOT transfer ownership here.
     */
    info->addr = node->addr;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * NODE STATE
 * ============================================================================ */

/*
 * Get node state.
 */
wingo_dht_node_state_t wingo_dht_node_state(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return WINGO_DHT_NODE_STATE_UNKNOWN;
    }

    return node->state;
}

/*
 * Set node state.
 */
void wingo_dht_node_set_state(wingo_dht_node_t *node,
                               wingo_dht_node_state_t state)
{
    if (node == NULL) {
        return;
    }

    node->state = state;

    /* Reset ping count when transitioning to GOOD */
    if (state == WINGO_DHT_NODE_STATE_GOOD) {
        node->ping_count = 0;
    }
}

/*
 * Check if node is good.
 */
bool wingo_dht_node_is_good(const wingo_dht_node_t *node)
{
    return node != NULL && node->state == WINGO_DHT_NODE_STATE_GOOD;
}

/*
 * Check if node is dubious.
 */
bool wingo_dht_node_is_dubious(const wingo_dht_node_t *node)
{
    return node != NULL && node->state == WINGO_DHT_NODE_STATE_DUBIOUS;
}

/*
 * Check if node is bad.
 */
bool wingo_dht_node_is_bad(const wingo_dht_node_t *node)
{
    return node != NULL && node->state == WINGO_DHT_NODE_STATE_BAD;
}

/*
 * Check if node is unknown.
 */
bool wingo_dht_node_is_unknown(const wingo_dht_node_t *node)
{
    return node != NULL && node->state == WINGO_DHT_NODE_STATE_UNKNOWN;
}

/* ============================================================================
 * NODE TIMESTAMPS
 * ============================================================================ */

/*
 * Get node last seen time.
 */
wingo_i64 wingo_dht_node_last_seen(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return 0;
    }

    return node->last_seen;
}

/*
 * Get node last reply time.
 */
wingo_i64 wingo_dht_node_last_reply(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return 0;
    }

    return node->last_reply;
}

/*
 * Get node last pinged time.
 */
wingo_i64 wingo_dht_node_last_pinged(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return 0;
    }

    return node->last_pinged;
}

/*
 * Get node ping count.
 */
int wingo_dht_node_ping_count(const wingo_dht_node_t *node)
{
    if (node == NULL) {
        return 0;
    }

    return node->ping_count;
}

/*
 * Update node last seen time.
 */
void wingo_dht_node_update_seen(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->last_seen = node_now();
}

/*
 * Update node last reply time.
 */
void wingo_dht_node_update_reply(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->last_reply = node_now();
}

/*
 * Update node last pinged time.
 */
void wingo_dht_node_update_pinged(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->last_pinged = node_now();
}

/*
 * Increment node ping count.
 */
void wingo_dht_node_increment_ping(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->ping_count++;

    /* Transition to BAD after MAX_PING failed pings */
    if (node->ping_count >= WINGO_DHT_NODE_MAX_PING) {
        node->state = WINGO_DHT_NODE_STATE_BAD;
    }
}

/*
 * Reset node ping count.
 */
void wingo_dht_node_reset_ping(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->ping_count = 0;
}

/*
 * Update node on pinged.
 *
 * - Updates last_pinged timestamp
 * - Increments ping count
 * - Transitions UNKNOWN/GOOD to DUBIOUS
 * - Transitions DUBIOUS to BAD after MAX_PING
 */
void wingo_dht_node_pinged(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->last_pinged = node_now();
    node->ping_count++;

    /* Transition state */
    if (node->state == WINGO_DHT_NODE_STATE_UNKNOWN) {
        node->state = WINGO_DHT_NODE_STATE_DUBIOUS;
    } else if (node->state == WINGO_DHT_NODE_STATE_GOOD) {
        node->state = WINGO_DHT_NODE_STATE_DUBIOUS;
    } else if (node->state == WINGO_DHT_NODE_STATE_DUBIOUS) {
        if (node->ping_count >= WINGO_DHT_NODE_MAX_PING) {
            node->state = WINGO_DHT_NODE_STATE_BAD;
        }
    }
}

/*
 * Update node on replied.
 *
 * - Updates last_reply and last_seen timestamps
 * - Resets ping count
 * - Transitions to GOOD
 */
void wingo_dht_node_replied(wingo_dht_node_t *node)
{
    if (node == NULL) {
        return;
    }

    node->last_reply = node_now();
    node->last_seen = node->last_reply;
    node->ping_count = 0;
    node->state = WINGO_DHT_NODE_STATE_GOOD;
}

/* ============================================================================
 * NODE TIMEOUT
 * ============================================================================ */

/*
 * Check if node is timed out.
 */
bool wingo_dht_node_is_timed_out(const wingo_dht_node_t *node,
                                  wingo_i64 timeout_s)
{
    wingo_i64 now;
    wingo_i64 last;

    if (node == NULL) {
        return true;
    }

    if (timeout_s <= 0) {
        return false;
    }

    now = node_now();

    /* Use last_seen if available, else first_seen */
    last = (node->last_seen > 0) ? node->last_seen : node->first_seen;

    if (last <= 0) {
        return true;
    }

    return (now - last) >= timeout_s;
}

/*
 * Check if node needs ping.
 */
bool wingo_dht_node_needs_ping(const wingo_dht_node_t *node)
{
    wingo_i64 now;
    wingo_i64 last_ping;

    if (node == NULL) {
        return false;
    }

    /* Bad nodes don't need ping */
    if (node->state == WINGO_DHT_NODE_STATE_BAD) {
        return false;
    }

    last_ping = node->last_pinged;
    if (last_ping == 0) {
        return true;
    }

    now = node_now();

    /*
     * Ping if we haven't pinged in DUBIOUS_TIMEOUT seconds.
     */
    return (now - last_ping) >= WINGO_DHT_NODE_DUBIOUS_TIMEOUT;
}

/*
 * Check if node is expired.
 *
 * A node is expired if:
 *   - It is marked BAD, OR
 *   - Its last seen is older than GOOD_TIMEOUT * 2
 */
bool wingo_dht_node_is_expired(const wingo_dht_node_t *node)
{
    wingo_i64 now;
    wingo_i64 last;
    wingo_i64 max_age;

    if (node == NULL) {
        return true;
    }

    /* Bad nodes are always expired */
    if (node->state == WINGO_DHT_NODE_STATE_BAD) {
        return true;
    }

    now = node_now();
    last = (node->last_seen > 0) ? node->last_seen : node->first_seen;

    if (last <= 0) {
        return true;
    }

    /* Node expires after 2 * GOOD_TIMEOUT seconds of inactivity */
    max_age = WINGO_DHT_NODE_GOOD_TIMEOUT * 2;

    return (now - last) >= max_age;
}

/* ============================================================================
 * NODE STATISTICS
 * ============================================================================ */

/*
 * Get node statistics.
 */
wingo_error_t wingo_dht_node_get_stats(const wingo_dht_node_t *node,
                                        wingo_dht_node_stats_t *stats)
{
    wingo_i64 now;

    if (node == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    now = node_now();

    stats->age = (node->first_seen > 0) ? (now - node->first_seen) : 0;
    stats->reply_age = (node->last_reply > 0) ? (now - node->last_reply) : -1;
    stats->ping_age = (node->last_pinged > 0) ? (now - node->last_pinged) : -1;
    stats->ping_count = node->ping_count;
    stats->is_good = wingo_dht_node_is_good(node);
    stats->is_dubious = wingo_dht_node_is_dubious(node);
    stats->is_bad = wingo_dht_node_is_bad(node);

    return WINGO_SUCCESS;
}
