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
/* ============================================================================
 * NODE COMPARISON
 * ============================================================================ */

/*
 * Compare two nodes by ID.
 */
int wingo_dht_node_cmp(const wingo_dht_node_t *a, const wingo_dht_node_t *b)
{
    if (a == NULL && b == NULL) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }

    return memcmp(a->id.bytes, b->id.bytes, WINGO_DHT_ID_SIZE);
}

/*
 * Compare node ID with another ID.
 */
int wingo_dht_node_cmp_id(const wingo_dht_node_t *node,
                           const wingo_dht_id_t *id)
{
    if (node == NULL && id == NULL) {
        return 0;
    }
    if (node == NULL) {
        return -1;
    }
    if (id == NULL) {
        return 1;
    }

    return memcmp(node->id.bytes, id->bytes, WINGO_DHT_ID_SIZE);
}

/*
 * Check if node has ID.
 */
bool wingo_dht_node_has_id(const wingo_dht_node_t *node,
                            const wingo_dht_id_t *id)
{
    if (node == NULL || id == NULL) {
        return false;
    }

    return memcmp(node->id.bytes, id->bytes, WINGO_DHT_ID_SIZE) == 0;
}

/*
 * Check if node has address.
 */
bool wingo_dht_node_has_addr(const wingo_dht_node_t *node,
                              const wingo_addr_t *addr)
{
    if (node == NULL || addr == NULL) {
        return false;
    }

    if (node->addr == NULL) {
        return false;
    }

    return wingo_addr_cmp(node->addr, addr) == 0;
}

/* ============================================================================
 * NODE LIST — INTERNAL HELPERS
 * ============================================================================ */

/*
 * Grow node list capacity.
 */
static wingo_error_t node_list_grow(wingo_dht_node_list_t *list)
{
    wingo_size new_cap;
    wingo_dht_node_t **new_items;

    if (list->capacity == 0) {
        new_cap = NODE_LIST_INIT_CAPACITY;
    } else {
        new_cap = list->capacity * NODE_LIST_GROW_FACTOR;
    }

    /* Overflow check */
    if (new_cap < list->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_items = realloc(list->items,
                        new_cap * sizeof(wingo_dht_node_t *));
    if (new_items == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    if (new_cap > list->capacity) {
        memset(&new_items[list->capacity], 0,
               (new_cap - list->capacity) * sizeof(wingo_dht_node_t *));
    }

    list->items = new_items;
    list->capacity = new_cap;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * NODE LIST — LIFECYCLE
 * ============================================================================ */

/*
 * Create a node list.
 *
 * The list does NOT own its nodes by default — caller is responsible
 * for freeing them. Set owns_nodes to true if the list should free
 * nodes when cleared/freed.
 */
wingo_dht_node_list_t *wingo_dht_node_list_new(wingo_size capacity)
{
    wingo_dht_node_list_t *list;

    if (capacity == 0) {
        capacity = NODE_LIST_INIT_CAPACITY;
    }

    list = calloc(1, sizeof(wingo_dht_node_list_t));
    if (list == NULL) {
        return NULL;
    }

    list->items = calloc(capacity, sizeof(wingo_dht_node_t *));
    if (list->items == NULL) {
        free(list);
        return NULL;
    }

    list->capacity = capacity;
    list->count = 0;
    list->owns_nodes = false;  /* Default: don't own */

    return list;
}

/*
 * Free a node list.
 *
 * If owns_nodes is true, frees all nodes too.
 */
void wingo_dht_node_list_free(wingo_dht_node_list_t *list)
{
    wingo_size i;

    if (list == NULL) {
        return;
    }

    /* Free nodes if we own them */
    if (list->owns_nodes) {
        for (i = 0; i < list->count; i++) {
            wingo_dht_node_free(list->items[i]);
        }
    }

    if (list->items != NULL) {
        free(list->items);
    }

    free(list);
}

/* ============================================================================
 * NODE LIST — OPERATIONS
 * ============================================================================ */

/*
 * Add a node to list.
 *
 * If list owns nodes, ownership transfers to the list.
 */
wingo_error_t wingo_dht_node_list_add(wingo_dht_node_list_t *list,
                                       wingo_dht_node_t *node)
{
    wingo_error_t rc;

    if (list == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Grow if needed */
    if (list->count >= list->capacity) {
        rc = node_list_grow(list);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    list->items[list->count++] = node;

    return WINGO_SUCCESS;
}

/*
 * Remove a node from list by ID.
 *
 * If list owns nodes, the removed node is freed.
 * Otherwise, the caller is responsible for the node.
 */
wingo_error_t wingo_dht_node_list_remove(wingo_dht_node_list_t *list,
                                          const wingo_dht_id_t *id)
{
    wingo_size i;

    if (list == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    for (i = 0; i < list->count; i++) {
        if (wingo_dht_node_has_id(list->items[i], id)) {
            /* Free node if we own it */
            if (list->owns_nodes) {
                wingo_dht_node_free(list->items[i]);
            }

            /* Shift remaining */
            if (i < list->count - 1) {
                memmove(&list->items[i],
                        &list->items[i + 1],
                        (list->count - i - 1) * sizeof(wingo_dht_node_t *));
            }

            list->count--;
            list->items[list->count] = NULL;

            return WINGO_SUCCESS;
        }
    }

    return WINGO_ERR_NOT_FOUND;
}

/*
 * Get node from list.
 */
wingo_dht_node_t *wingo_dht_node_list_get(const wingo_dht_node_list_t *list,
                                           wingo_size index)
{
    if (list == NULL || index >= list->count) {
        return NULL;
    }

    return list->items[index];
}

/*
 * Get node count.
 */
wingo_size wingo_dht_node_list_count(const wingo_dht_node_list_t *list)
{
    if (list == NULL) {
        return 0;
    }

    return list->count;
}

/*
 * Find node in list.
 */
wingo_dht_node_t *wingo_dht_node_list_find(const wingo_dht_node_list_t *list,
                                            const wingo_dht_id_t *id)
{
    wingo_size i;

    if (list == NULL || id == NULL) {
        return NULL;
    }

    for (i = 0; i < list->count; i++) {
        if (wingo_dht_node_has_id(list->items[i], id)) {
            return list->items[i];
        }
    }

    return NULL;
}

/*
 * Clear node list.
 *
 * If list owns nodes, frees all nodes too.
 */
void wingo_dht_node_list_clear(wingo_dht_node_list_t *list)
{
    wingo_size i;

    if (list == NULL) {
        return;
    }

    /* Free nodes if we own them */
    if (list->owns_nodes) {
        for (i = 0; i < list->count; i++) {
            wingo_dht_node_free(list->items[i]);
        }
    }

    /* Zero array */
    if (list->items != NULL && list->count > 0) {
        memset(list->items, 0, list->count * sizeof(wingo_dht_node_t *));
    }

    list->count = 0;
}

/* ============================================================================
 * NODE LIST — SORTING
 * ============================================================================ */

/*
 * Sort context for qsort.
 */
typedef struct {
    const wingo_dht_id_t   *target;
} sort_context_t;

/*
 * Thread-local sort context (qsort doesn't allow passing context).
 */
static sort_context_t g_sort_ctx;

/*
 * Compare two nodes by XOR distance to target.
 */
static int node_list_sort_cmp(const void *a, const void *b)
{
    const wingo_dht_node_t *na = *(const wingo_dht_node_t **)a;
    const wingo_dht_node_t *nb = *(const wingo_dht_node_t **)b;
    const wingo_dht_id_t *target = g_sort_ctx.target;
    wingo_u8 da[WINGO_DHT_ID_SIZE];
    wingo_u8 db[WINGO_DHT_ID_SIZE];
    wingo_size i;

    /* Compute XOR distances */
    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        da[i] = na->id.bytes[i] ^ target->bytes[i];
        db[i] = nb->id.bytes[i] ^ target->bytes[i];
    }

    /* Compare */
    return memcmp(da, db, WINGO_DHT_ID_SIZE);
}

/*
 * Sort node list by XOR distance to target.
 *
 * Nodes are sorted from closest to farthest.
 */
wingo_error_t wingo_dht_node_list_sort(wingo_dht_node_list_t *list,
                                        const wingo_dht_id_t *target)
{
    if (list == NULL || target == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (list->count < 2) {
        return WINGO_SUCCESS;
    }

    /*
     * NOTE: g_sort_ctx is a global. This is not thread-safe.
     *       For Bowie, we assume single-threaded DHT operations.
     */
    g_sort_ctx.target = target;

    qsort(list->items, list->count,
          sizeof(wingo_dht_node_t *), node_list_sort_cmp);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * NODE LIST — ITERATION
 * ============================================================================ */

/*
 * Iterate over node list.
 *
 * If callback returns false, iteration stops.
 */
void wingo_dht_node_list_foreach(wingo_dht_node_list_t *list,
                                  wingo_dht_node_cb_t callback,
                                  void *userdata)
{
    wingo_size i;

    if (list == NULL || callback == NULL) {
        return;
    }

    for (i = 0; i < list->count; i++) {
        if (!callback(list->items[i], userdata)) {
            break;
        }
    }
}

/* ============================================================================
 * NODE UTILITY
 * ============================================================================ */

/*
 * Get node state name.
 */
const char *wingo_dht_node_state_name(wingo_dht_node_state_t state)
{
    switch (state) {
    case WINGO_DHT_NODE_STATE_UNKNOWN:  return "UNKNOWN";
    case WINGO_DHT_NODE_STATE_GOOD:     return "GOOD";
    case WINGO_DHT_NODE_STATE_DUBIOUS:  return "DUBIOUS";
    case WINGO_DHT_NODE_STATE_BAD:      return "BAD";
    default:                            return "INVALID";
    }
}

/*
 * Convert ID to hex string (internal helper).
 */
static void node_id_to_hex(const wingo_dht_id_t *id, char *buf)
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
 * Print node.
 */
void wingo_dht_node_print(const wingo_dht_node_t *node, FILE *f)
{
    char id_hex[WINGO_DHT_ID_HEX_SIZE];
    char addr_str[WINGO_ADDR_STR_MAX];
    wingo_i64 now;
    wingo_i64 seen_age = 0;
    wingo_i64 reply_age = 0;
    wingo_i64 ping_age = 0;

    if (f == NULL) {
        f = stderr;
    }

    if (node == NULL) {
        fprintf(f, "Node: (null)\n");
        return;
    }

    node_id_to_hex(&node->id, id_hex);

    if (node->addr != NULL) {
        wingo_addr_str(node->addr, addr_str, sizeof(addr_str));
    } else {
        snprintf(addr_str, sizeof(addr_str), "(null)");
    }

    now = node_now();
    if (node->last_seen > 0) {
        seen_age = now - node->last_seen;
    }
    if (node->last_reply > 0) {
        reply_age = now - node->last_reply;
    }
    if (node->last_pinged > 0) {
        ping_age = now - node->last_pinged;
    }

    fprintf(f, "Node:\n");
    fprintf(f, "  ID:         %s\n", id_hex);
    fprintf(f, "  Address:    %s\n", addr_str);
    fprintf(f, "  State:      %s\n",
            wingo_dht_node_state_name(node->state));
    fprintf(f, "  Ping count: %d\n", node->ping_count);
    fprintf(f, "  Seen:       %llds ago\n", (long long)seen_age);
    fprintf(f, "  Replied:    %llds ago\n", (long long)reply_age);
    fprintf(f, "  Pinged:     %llds ago\n", (long long)ping_age);
}

/*
 * Print node list.
 */
void wingo_dht_node_list_print(const wingo_dht_node_list_t *list, FILE *f)
{
    wingo_size i;
    char id_hex[WINGO_DHT_ID_HEX_SIZE];
    char addr_str[WINGO_ADDR_STR_MAX];

    if (f == NULL) {
        f = stderr;
    }

    if (list == NULL) {
        fprintf(f, "Node list: (null)\n");
        return;
    }

    fprintf(f, "Node list (%zu nodes):\n", list->count);

    for (i = 0; i < list->count; i++) {
        wingo_dht_node_t *node = list->items[i];
        const char *state_name;

        if (node == NULL) {
            fprintf(f, "  [%zu] (null)\n", i);
            continue;
        }

        node_id_to_hex(&node->id, id_hex);

        if (node->addr != NULL) {
            wingo_addr_str(node->addr, addr_str, sizeof(addr_str));
        } else {
            snprintf(addr_str, sizeof(addr_str), "(null)");
        }

        state_name = wingo_dht_node_state_name(node->state);

        fprintf(f, "  [%zu] %s  %s  %s\n",
                i, id_hex, addr_str, state_name);
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
