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
 * Kademlia search (iterative lookup) implementation for Bowie DHT.
 *
 * This is a PURE ALGORITHM module. It does NOT perform network I/O.
 *
 * The search state machine:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    SEARCH LIFECYCLE                         │
 *   │                                                             │
│   │  INIT ──(start)──> RUNNING                                  │
│   │                       │                                     │
│   │                       ├──(step → QUERY)──> send query       │
│   │                       ├──(step → WAIT)───> wait            │
│   │                       ├──(step → DONE)───> completed       │
│   │                       └──(step → TIMEOUT)> timed out       │
│   │                                                             │
│   │  RUNNING ──(cancel)──> CANCELED                             │
│   │  RUNNING ──(error)───> ERROR                                │
│   │                                                             │
│   └─────────────────────────────────────────────────────────────┘
 *
 * The caller (DHT) drives the search:
 *
 *   1. Call wingo_dht_search_step()
 *   2. If result is QUERY: send query to returned node
 *   3. On response: call wingo_dht_search_insert_node() for each new node
 *   4. Call wingo_dht_search_node_replied() for the queried node
 *   5. Repeat until is_done()
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_search.h"
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
 * Search node entry.
 */
typedef struct {
    wingo_dht_node_info_t   info;           /* ID + address */
    bool                    replied;        /* Has replied? */
    bool                    queried;        /* Has been queried? */
    bool                    acked;          /* For announce: acked? */
    wingo_i64               queried_at;     /* When queried */
    wingo_i64               replied_at;     /* When replied */
    wingo_u8                token[WINGO_DHT_MSG_TOKEN_MAX];
    wingo_size              token_len;
} search_node_t;

/*
 * Search (concrete).
 */
struct wingo_dht_search {
    /* ----- Manager ----- */
    wingo_dht_search_mgr_t *mgr;

    /* ----- Identity ----- */
    wingo_dht_search_type_t type;
    wingo_dht_search_state_t state;
    wingo_dht_id_t          target;
    wingo_u16               port;
    wingo_u16               tid;            /* Transaction ID */

    /* ----- Nodes ----- */
    search_node_t          *nodes;
    wingo_size              count;
    wingo_size              capacity;

    /* ----- Timing ----- */
    wingo_i64               started_at;
    wingo_i64               last_step_at;
    wingo_i64               next_step_at;

    /* ----- State ----- */
    bool                    started;
    bool                    done;
    wingo_error_t           result;

    /* ----- Statistics ----- */
    wingo_u64               queries_sent;
    wingo_u64               queries_replied;
    wingo_u64               nodes_added;

    /* ----- Callbacks ----- */
    wingo_dht_search_peer_cb_t  peer_cb;
    void                       *peer_userdata;
    wingo_dht_search_done_cb_t  done_cb;
    void                       *done_userdata;
};

/*
 * Search manager (concrete).
 */
struct wingo_dht_search_mgr {
    /* ----- Searches ----- */
    wingo_dht_search_t    **searches;
    wingo_size              count;
    wingo_size              capacity;
    wingo_size              max_searches;

    /* ----- Transaction IDs ----- */
    wingo_u16               next_tid;

    /* ----- Statistics ----- */
    wingo_dht_search_stats_t stats;
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial node capacity for a search.
 */
#define SEARCH_INIT_NODE_CAP        8

/*
 * Growth factor for node array.
 */
#define SEARCH_GROW_FACTOR          2

/* ============================================================================
 * INTERNAL HELPERS — ID COMPARISON
 * ============================================================================ */

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
 * Compare two IDs.
 */
static int id_cmp(const wingo_dht_id_t *a, const wingo_dht_id_t *b)
{
    return memcmp(a->bytes, b->bytes, WINGO_DHT_ID_SIZE);
}

/* ============================================================================
 * INTERNAL HELPERS — NODE ARRAY
 * ============================================================================ */

/*
 * Grow search node array.
 */
static wingo_error_t search_grow_nodes(wingo_dht_search_t *search)
{
    wingo_size new_cap;
    search_node_t *new_nodes;

    if (search->capacity == 0) {
        new_cap = SEARCH_INIT_NODE_CAP;
    } else {
        new_cap = search->capacity * SEARCH_GROW_FACTOR;
    }

    if (new_cap < search->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    if (new_cap > WINGO_DHT_SEARCH_MAX_NODES) {
        new_cap = WINGO_DHT_SEARCH_MAX_NODES;
    }

    new_nodes = realloc(search->nodes, new_cap * sizeof(search_node_t));
    if (new_nodes == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    if (new_cap > search->capacity) {
        memset(&new_nodes[search->capacity], 0,
               (new_cap - search->capacity) * sizeof(search_node_t));
    }

    search->nodes = new_nodes;
    search->capacity = new_cap;

    return WINGO_SUCCESS;
}

/*
 * Find node index by ID.
 */
static int search_find_node_index(const wingo_dht_search_t *search,
                                   const wingo_dht_id_t *id)
{
    wingo_size i;

    for (i = 0; i < search->count; i++) {
        if (id_cmp(&search->nodes[i].info.id, id) == 0) {
            return (int)i;
        }
    }

    return -1;
}

/*
 * Remove node at index.
 */
static void search_remove_node_at(wingo_dht_search_t *search, wingo_size index)
{
    if (index >= search->count) {
        return;
    }

    /* Free address */
    if (search->nodes[index].info.addr != NULL) {
        wingo_addr_free(search->nodes[index].info.addr);
    }

    /* Shift remaining */
    if (index < search->count - 1) {
        memmove(&search->nodes[index],
                &search->nodes[index + 1],
                (search->count - index - 1) * sizeof(search_node_t));
    }

    search->count--;
    memset(&search->nodes[search->count], 0, sizeof(search_node_t));
}

/* ============================================================================
 * INTERNAL HELPERS — SORTING
 * ============================================================================ */

/*
 * Sort context for qsort.
 */
typedef struct {
    const wingo_dht_id_t *target;
} search_sort_ctx_t;

/*
 * Thread-local sort context.
 */
static search_sort_ctx_t g_sort_ctx;

/*
 * Compare two search nodes by XOR distance to target.
 */
static int search_node_cmp(const void *a, const void *b)
{
    const search_node_t *na = (const search_node_t *)a;
    const search_node_t *nb = (const search_node_t *)b;
    const wingo_dht_id_t *target = g_sort_ctx.target;
    wingo_dht_id_t da, db;

    id_xor(&na->info.id, target, &da);
    id_xor(&nb->info.id, target, &db);

    return memcmp(da.bytes, db.bytes, WINGO_DHT_ID_SIZE);
}

/*
 * Sort search nodes by distance to target.
 */
static void search_sort_nodes(wingo_dht_search_t *search)
{
    if (search->count < 2) {
        return;
    }

    g_sort_ctx.target = &search->target;
    qsort(search->nodes, search->count, sizeof(search_node_t),
          search_node_cmp);
}

/* ============================================================================
 * INTERNAL HELPERS — CONVERGENCE
 * ============================================================================ */

/*
 * Check if search has converged.
 *
 * Convergence: The K closest nodes have all been queried and
 * have either replied or timed out.
 */
static bool search_has_converged(const wingo_dht_search_t *search)
{
    wingo_size i;
    wingo_size closest_count;
    wingo_i64 now;
    wingo_i64 timeout;

    now = wingo_time_now();
    timeout = WINGO_DHT_SEARCH_RETRANSMIT;

    /* Check the K closest nodes */
    closest_count = (search->count < WINGO_DHT_SEARCH_INFLIGHT)
                  ? search->count
                  : WINGO_DHT_SEARCH_INFLIGHT;

    for (i = 0; i < closest_count; i++) {
        const search_node_t *node = &search->nodes[i];

        if (!node->queried) {
            return false;  /* Not yet queried */
        }

        if (!node->replied) {
            /* Not replied — check if timeout has passed */
            if (now - node->queried_at < timeout) {
                return false;  /* Still waiting */
            }
            /* Timeout passed — consider converged for this node */
        }
    }

    return true;
}

/* ============================================================================
 * INTERNAL HELPERS — NEXT QUERY
 * ============================================================================ */

/*
 * Find the next node to query.
 *
 * Returns:
 *   true  — found a node, out_index is set
 *   false — no node available
 */
static bool search_find_next_query(wingo_dht_search_t *search,
                                    wingo_size *out_index)
{
    wingo_size i;
    wingo_size inflight = 0;
    wingo_i64 now;

    now = wingo_time_now();

    /* Count in-flight queries */
    for (i = 0; i < search->count; i++) {
        if (search->nodes[i].queried && !search->nodes[i].replied) {
            if (now - search->nodes[i].queried_at <
                WINGO_DHT_SEARCH_RETRANSMIT) {
                inflight++;
            }
        }
    }

    /* Too many in-flight — wait */
    if (inflight >= WINGO_DHT_SEARCH_INFLIGHT) {
        return false;
    }

    /* Find closest unqueried node */
    for (i = 0; i < search->count; i++) {
        search_node_t *node = &search->nodes[i];

        /* Skip if already queried or replied */
        if (node->queried || node->replied) {
            continue;
        }

        *out_index = i;
        return true;
    }

    return false;
}

/* ============================================================================
 * SEARCH MANAGER LIFECYCLE
 * ============================================================================ */

/*
 * Create a new search manager.
 */
wingo_dht_search_mgr_t *wingo_dht_search_mgr_new(wingo_size max_searches)
{
    wingo_dht_search_mgr_t *mgr;

    if (max_searches == 0) {
        max_searches = WINGO_DHT_SEARCH_MAX_SEARCHES;
    }

    mgr = calloc(1, sizeof(wingo_dht_search_mgr_t));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->max_searches = max_searches;
    mgr->capacity = 16;
    mgr->searches = calloc(mgr->capacity, sizeof(wingo_dht_search_t *));
    if (mgr->searches == NULL) {
        free(mgr);
        return NULL;
    }

    mgr->count = 0;
    mgr->next_tid = 1;

    WINGO_LOG_DEBUG("DHT search manager created (max=%zu)", max_searches);

    return mgr;
}

/*
 * Free a search manager.
 */
void wingo_dht_search_mgr_free(wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;

    if (mgr == NULL) {
        return;
    }

    /* Free all searches */
    if (mgr->searches != NULL) {
        for (i = 0; i < mgr->count; i++) {
            wingo_dht_search_free(mgr->searches[i]);
        }
        free(mgr->searches);
    }

    free(mgr);
}

/*
 * Grow search manager's search array.
 */
static wingo_error_t mgr_grow(wingo_dht_search_mgr_t *mgr)
{
    wingo_size new_cap;
    wingo_dht_search_t **new_searches;

    new_cap = mgr->capacity * SEARCH_GROW_FACTOR;
    if (new_cap < mgr->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    new_searches = realloc(mgr->searches,
                           new_cap * sizeof(wingo_dht_search_t *));
    if (new_searches == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    memset(&new_searches[mgr->capacity], 0,
           (new_cap - mgr->capacity) * sizeof(wingo_dht_search_t *));

    mgr->searches = new_searches;
    mgr->capacity = new_cap;

    return WINGO_SUCCESS;
}

/*
 * Add search to manager.
 */
static wingo_error_t mgr_add_search(wingo_dht_search_mgr_t *mgr,
                                     wingo_dht_search_t *search)
{
    wingo_error_t rc;

    if (mgr->count >= mgr->capacity) {
        rc = mgr_grow(mgr);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    mgr->searches[mgr->count++] = search;

    return WINGO_SUCCESS;
}

/*
 * Remove search from manager.
 */
static void mgr_remove_search(wingo_dht_search_mgr_t *mgr,
                               wingo_dht_search_t *search)
{
    wingo_size i;

    for (i = 0; i < mgr->count; i++) {
        if (mgr->searches[i] == search) {
            if (i < mgr->count - 1) {
                memmove(&mgr->searches[i],
                        &mgr->searches[i + 1],
                        (mgr->count - i - 1) * sizeof(wingo_dht_search_t *));
            }
            mgr->count--;
            mgr->searches[mgr->count] = NULL;
            return;
        }
    }
}

/*
 * Allocate a new transaction ID.
 */
static wingo_u16 mgr_next_tid(wingo_dht_search_mgr_t *mgr)
{
    wingo_u16 tid = mgr->next_tid++;

    if (mgr->next_tid == 0) {
        mgr->next_tid = 1;
    }

    return tid;
}

/* ============================================================================
 * SEARCH LIFECYCLE
 * ============================================================================ */

/*
 * Create a new search.
 */
wingo_dht_search_t *wingo_dht_search_new(wingo_dht_search_mgr_t *mgr,
                                          wingo_dht_search_type_t type,
                                          const wingo_dht_id_t *target,
                                          wingo_u16 port)
{
    wingo_dht_search_t *search;
    wingo_error_t rc;

    if (mgr == NULL || target == NULL) {
        return NULL;
    }

    search = calloc(1, sizeof(wingo_dht_search_t));
    if (search == NULL) {
        return NULL;
    }

    /* Initialize */
    search->mgr = mgr;
    search->type = type;
    search->state = WINGO_DHT_SEARCH_STATE_INIT;
    memcpy(&search->target, target, sizeof(wingo_dht_id_t));
    search->port = port;
    search->tid = mgr_next_tid(mgr);

    /* Allocate node array */
    search->capacity = SEARCH_INIT_NODE_CAP;
    search->nodes = calloc(search->capacity, sizeof(search_node_t));
    if (search->nodes == NULL) {
        free(search);
        return NULL;
    }

    /* Timestamps */
    search->started_at = wingo_time_now();
    search->last_step_at = search->started_at;
    search->next_step_at = search->started_at;

    /* State */
    search->started = false;
    search->done = false;
    search->result = WINGO_SUCCESS;

    /* Add to manager */
    rc = mgr_add_search(mgr, search);
    if (rc != WINGO_SUCCESS) {
        free(search->nodes);
        free(search);
        return NULL;
    }

    WINGO_LOG_DEBUG("DHT search created (type=%d, tid=%u)",
                    type, search->tid);

    return search;
}

/*
 * Free a search.
 */
void wingo_dht_search_free(wingo_dht_search_t *search)
{
    wingo_size i;

    if (search == NULL) {
        return;
    }

    /* Remove from manager */
    if (search->mgr != NULL) {
        mgr_remove_search(search->mgr, search);
    }

    /* Free node addresses */
    if (search->nodes != NULL) {
        for (i = 0; i < search->count; i++) {
            if (search->nodes[i].info.addr != NULL) {
                wingo_addr_free(search->nodes[i].info.addr);
            }
        }
        free(search->nodes);
    }

    free(search);
}

/*
 * Start a search.
 */
wingo_error_t wingo_dht_search_start(wingo_dht_search_t *search)
{
    if (search == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (search->state != WINGO_DHT_SEARCH_STATE_INIT) {
        return WINGO_ERR_INVALID_STATE;
    }

    search->state = WINGO_DHT_SEARCH_STATE_RUNNING;
    search->started = true;
    search->started_at = wingo_time_now();
    search->next_step_at = search->started_at;

    if (search->mgr != NULL) {
        search->mgr->stats.searches_started++;
    }

    return WINGO_SUCCESS;
}

/*
 * Cancel a search.
 */
wingo_error_t wingo_dht_search_cancel(wingo_dht_search_t *search)
{
    if (search == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (search->done) {
        return WINGO_SUCCESS;
    }

    search->state = WINGO_DHT_SEARCH_STATE_CANCELED;
    search->done = true;
    search->result = WINGO_ERR_CANCELED;

    /* Call done callback */
    if (search->done_cb != NULL) {
        search->done_cb(WINGO_ERR_CANCELED, search->done_userdata);
    }

    return WINGO_SUCCESS;
}
/* ============================================================================
 * SEARCH QUERY
 * ============================================================================ */

/*
 * Get search type.
 */
wingo_dht_search_type_t wingo_dht_search_type(
    const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return WINGO_DHT_SEARCH_TYPE_FIND_NODE;
    }

    return search->type;
}

/*
 * Get search state.
 */
wingo_dht_search_state_t wingo_dht_search_state(
    const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return WINGO_DHT_SEARCH_STATE_ERROR;
    }

    return search->state;
}

/*
 * Get search target ID.
 */
const wingo_dht_id_t *wingo_dht_search_target(
    const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return NULL;
    }

    return &search->target;
}

/*
 * Get search port.
 */
wingo_u16 wingo_dht_search_port(const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return 0;
    }

    return search->port;
}

/*
 * Get search transaction ID.
 */
wingo_u16 wingo_dht_search_tid(const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return 0;
    }

    return search->tid;
}

/*
 * Check if search is done.
 */
bool wingo_dht_search_is_done(const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return true;
    }

    return search->done;
}

/*
 * Check if search is active.
 */
bool wingo_dht_search_is_active(const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return false;
    }

    if (search->done) {
        return false;
    }

    return search->state == WINGO_DHT_SEARCH_STATE_RUNNING;
}

/*
 * Get search age (seconds).
 */
wingo_i64 wingo_dht_search_age(const wingo_dht_search_t *search)
{
    wingo_i64 now;

    if (search == NULL || search->started_at == 0) {
        return 0;
    }

    now = wingo_time_now();

    return now - search->started_at;
}

/* ============================================================================
 * SEARCH NODE MANAGEMENT
 * ============================================================================ */

/*
 * Get search node count.
 */
wingo_size wingo_dht_search_node_count(const wingo_dht_search_t *search)
{
    if (search == NULL) {
        return 0;
    }

    return search->count;
}

/*
 * Get search node at index.
 */
const wingo_dht_node_info_t *wingo_dht_search_node(
    const wingo_dht_search_t *search,
    wingo_size index)
{
    if (search == NULL || index >= search->count) {
        return NULL;
    }

    return &search->nodes[index].info;
}

/*
 * Insert a node into search.
 */
wingo_error_t wingo_dht_search_insert_node(wingo_dht_search_t *search,
                                            const wingo_dht_id_t *id,
                                            const wingo_addr_t *addr,
                                            bool replied,
                                            const wingo_u8 *token,
                                            wingo_size token_len)
{
    int existing;
    wingo_size idx;
    wingo_error_t rc;

    if (search == NULL || id == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (search->done) {
        return WINGO_ERR_INVALID_STATE;
    }

    /* Check if node already exists */
    existing = search_find_node_index(search, id);
    if (existing >= 0) {
        search_node_t *node = &search->nodes[existing];

        /* Update existing node */
        if (replied) {
            node->replied = true;
            node->replied_at = wingo_time_now();
        }

        if (token != NULL && token_len > 0 && token_len <= WINGO_DHT_MSG_TOKEN_MAX) {
            memcpy(node->token, token, token_len);
            node->token_len = token_len;
        }

        return WINGO_SUCCESS;
    }

    /* Check capacity */
    if (search->count >= WINGO_DHT_SEARCH_MAX_NODES) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Grow if needed */
    if (search->count >= search->capacity) {
        rc = search_grow_nodes(search);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    /* Add new node */
    idx = search->count;

    memcpy(&search->nodes[idx].info.id, id, sizeof(wingo_dht_id_t));

    search->nodes[idx].info.addr = wingo_addr_copy(addr);
    if (search->nodes[idx].info.addr == NULL) {
        return WINGO_ERR_NOMEM;
    }

    search->nodes[idx].replied = replied;
    search->nodes[idx].queried = false;
    search->nodes[idx].acked = false;
    search->nodes[idx].queried_at = 0;
    search->nodes[idx].replied_at = replied ? wingo_time_now() : 0;
    search->nodes[idx].token_len = 0;

    if (token != NULL && token_len > 0 && token_len <= WINGO_DHT_MSG_TOKEN_MAX) {
        memcpy(search->nodes[idx].token, token, token_len);
        search->nodes[idx].token_len = token_len;
    }

    search->count++;
    search->nodes_added++;

    if (search->mgr != NULL) {
        search->mgr->stats.nodes_found++;
    }

    /* Re-sort by distance */
    search_sort_nodes(search);

    /* Notify peer callback if this is a peer (get_peers) */
    if (search->peer_cb != NULL &&
        search->type == WINGO_DHT_SEARCH_TYPE_GET_PEERS) {
        search->peer_cb(addr, search->peer_userdata);
        if (search->mgr != NULL) {
            search->mgr->stats.peers_found++;
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Remove a node from search.
 */
wingo_error_t wingo_dht_search_remove_node(wingo_dht_search_t *search,
                                            const wingo_dht_id_t *id)
{
    int index;

    if (search == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    index = search_find_node_index(search, id);
    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    search_remove_node_at(search, (wingo_size)index);

    return WINGO_SUCCESS;
}

/*
 * Mark a node as replied.
 */
wingo_error_t wingo_dht_search_node_replied(wingo_dht_search_t *search,
                                             const wingo_dht_id_t *id,
                                             const wingo_u8 *token,
                                             wingo_size token_len)
{
    int index;

    if (search == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    index = search_find_node_index(search, id);
    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    search->nodes[index].replied = true;
    search->nodes[index].replied_at = wingo_time_now();

    if (token != NULL && token_len > 0 && token_len <= WINGO_DHT_MSG_TOKEN_MAX) {
        memcpy(search->nodes[index].token, token, token_len);
        search->nodes[index].token_len = token_len;
    }

    search->queries_replied++;

    if (search->mgr != NULL) {
        search->mgr->stats.queries_replied++;
    }

    return WINGO_SUCCESS;
}

/*
 * Mark a node as acked (announce).
 */
wingo_error_t wingo_dht_search_node_acked(wingo_dht_search_t *search,
                                           const wingo_dht_id_t *id)
{
    int index;

    if (search == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    index = search_find_node_index(search, id);
    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    search->nodes[index].acked = true;

    return WINGO_SUCCESS;
}

/*
 * Get closest nodes from search.
 */
wingo_size wingo_dht_search_closest_nodes(wingo_dht_search_t *search,
                                           wingo_dht_node_info_t *nodes,
                                           wingo_size max)
{
    wingo_size count;
    wingo_size i;

    if (search == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    /* Re-sort by distance */
    search_sort_nodes(search);

    count = (search->count < max) ? search->count : max;

    for (i = 0; i < count; i++) {
        memcpy(&nodes[i].id, &search->nodes[i].info.id, sizeof(wingo_dht_id_t));

        if (search->nodes[i].info.addr != NULL) {
            nodes[i].addr = wingo_addr_copy(search->nodes[i].info.addr);
        } else {
            nodes[i].addr = NULL;
        }
    }

    return count;
}

/* ============================================================================
 * SEARCH STEP
 * ============================================================================ */

/*
 * Step the search.
 */
wingo_dht_step_result_t wingo_dht_search_step(
    wingo_dht_search_t *search,
    wingo_dht_node_info_t *out_node)
{
    wingo_size next_index;
    wingo_i64 now;

    if (search == NULL) {
        return WINGO_DHT_STEP_DONE;
    }

    /* Already done? */
    if (search->done) {
        return WINGO_DHT_STEP_DONE;
    }

    /* Not started? */
    if (!search->started) {
        return WINGO_DHT_STEP_WAIT;
    }

    now = wingo_time_now();
    search->last_step_at = now;

    /* Check timeout */
    if (now - search->started_at >= WINGO_DHT_SEARCH_EXPIRE) {
        search->state = WINGO_DHT_SEARCH_STATE_TIMEOUT;
        search->done = true;
        search->result = WINGO_ERR_TIMEOUT;

        if (search->mgr != NULL) {
            search->mgr->stats.searches_timeout++;
        }

        if (search->done_cb != NULL) {
            search->done_cb(WINGO_ERR_TIMEOUT, search->done_userdata);
        }

        return WINGO_DHT_STEP_TIMEOUT;
    }

    /* Check convergence */
    if (search_has_converged(search)) {
        search->state = WINGO_DHT_SEARCH_STATE_DONE;
        search->done = true;
        search->result = WINGO_SUCCESS;

        if (search->mgr != NULL) {
            search->mgr->stats.searches_done++;
        }

        if (search->done_cb != NULL) {
            search->done_cb(WINGO_SUCCESS, search->done_userdata);
        }

        return WINGO_DHT_STEP_DONE;
    }

    /* Find next node to query */
    if (!search_find_next_query(search, &next_index)) {
        /* No node to query right now — wait */
        search->next_step_at = now + 1;  /* Check again in 1 second */
        return WINGO_DHT_STEP_WAIT;
    }

    /* Mark node as queried */
    search->nodes[next_index].queried = true;
    search->nodes[next_index].queried_at = now;

    /* Output node */
    if (out_node != NULL) {
        memcpy(&out_node->id, &search->nodes[next_index].info.id,
               sizeof(wingo_dht_id_t));

        if (search->nodes[next_index].info.addr != NULL) {
            out_node->addr = wingo_addr_copy(search->nodes[next_index].info.addr);
        } else {
            out_node->addr = NULL;
        }
    }

    search->queries_sent++;

    if (search->mgr != NULL) {
        search->mgr->stats.queries_sent++;
    }

    search->next_step_at = now + WINGO_DHT_SEARCH_RETRANSMIT;

    return WINGO_DHT_STEP_QUERY;
}

/*
 * Get next step time.
 */
wingo_i64 wingo_dht_search_next_step(const wingo_dht_search_t *search)
{
    if (search == NULL || search->done) {
        return 0;
    }

    return search->next_step_at;
}

/*
 * Check if search needs step.
 */
bool wingo_dht_search_needs_step(const wingo_dht_search_t *search)
{
    wingo_i64 now;

    if (search == NULL || search->done) {
        return false;
    }

    now = wingo_time_now();

    return now >= search->next_step_at;
}

/* ============================================================================
 * SEARCH CALLBACKS
 * ============================================================================ */

/*
 * Set search peer callback.
 */
wingo_error_t wingo_dht_search_set_peer_callback(
    wingo_dht_search_t *search,
    wingo_dht_search_peer_cb_t callback,
    void *userdata)
{
    if (search == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    search->peer_cb = callback;
    search->peer_userdata = userdata;

    return WINGO_SUCCESS;
}

/*
 * Set search done callback.
 */
wingo_error_t wingo_dht_search_set_done_callback(
    wingo_dht_search_t *search,
    wingo_dht_search_done_cb_t callback,
    void *userdata)
{
    if (search == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    search->done_cb = callback;
    search->done_userdata = userdata;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SEARCH MANAGER OPERATIONS
 * ============================================================================ */

/*
 * Find a search by transaction ID.
 */
wingo_dht_search_t *wingo_dht_search_mgr_find(
    wingo_dht_search_mgr_t *mgr,
    wingo_u16 tid)
{
    wingo_size i;

    if (mgr == NULL || tid == 0) {
        return NULL;
    }

    for (i = 0; i < mgr->count; i++) {
        if (mgr->searches[i] != NULL && mgr->searches[i]->tid == tid) {
            return mgr->searches[i];
        }
    }

    return NULL;
}

/*
 * Get search count.
 */
wingo_size wingo_dht_search_mgr_count(const wingo_dht_search_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }

    return mgr->count;
}

/*
 * Get active search count.
 */
wingo_size wingo_dht_search_mgr_active_count(
    const wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;
    wingo_size count = 0;

    if (mgr == NULL) {
        return 0;
    }

    for (i = 0; i < mgr->count; i++) {
        if (mgr->searches[i] != NULL &&
            wingo_dht_search_is_active(mgr->searches[i])) {
            count++;
        }
    }

    return count;
}

/*
 * Step all searches.
 */
wingo_error_t wingo_dht_search_mgr_step(wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;

    if (mgr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * NOTE: We iterate over the searches array. If a search is freed
     * during iteration (e.g., from a done callback), the array
     * changes under us. To be safe, we snapshot the count.
     */
    for (i = 0; i < mgr->count; i++) {
        wingo_dht_search_t *search = mgr->searches[i];

        if (search == NULL) {
            continue;
        }

        if (search->done) {
            continue;
        }

        if (wingo_dht_search_needs_step(search)) {
            wingo_dht_node_info_t node;

            memset(&node, 0, sizeof(node));

            wingo_dht_search_step(search, &node);

            if (node.addr != NULL) {
                wingo_addr_free(node.addr);
            }
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Expire old searches.
 */
wingo_size wingo_dht_search_mgr_expire(wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;
    wingo_size expired = 0;
    wingo_i64 now;

    if (mgr == NULL) {
        return 0;
    }

    now = wingo_time_now();

    i = 0;
    while (i < mgr->count) {
        wingo_dht_search_t *search = mgr->searches[i];

        if (search == NULL) {
            i++;
            continue;
        }

        /* Check if search has expired */
        if (!search->done &&
            now - search->started_at >= WINGO_DHT_SEARCH_EXPIRE) {
            /* Mark as timeout */
            search->state = WINGO_DHT_SEARCH_STATE_TIMEOUT;
            search->done = true;

            if (search->done_cb != NULL) {
                search->done_cb(WINGO_ERR_TIMEOUT, search->done_userdata);
            }

            mgr->stats.searches_timeout++;
        }

        i++;
    }

    return expired;
}

/*
 * Get next step time for all searches.
 */
wingo_i64 wingo_dht_search_mgr_next_step(
    const wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;
    wingo_i64 next = 0;

    if (mgr == NULL) {
        return 0;
    }

    for (i = 0; i < mgr->count; i++) {
        wingo_i64 t;

        if (mgr->searches[i] == NULL || mgr->searches[i]->done) {
            continue;
        }

        t = wingo_dht_search_next_step(mgr->searches[i]);

        if (t == 0) {
            continue;
        }

        if (next == 0 || t < next) {
            next = t;
        }
    }

    return next;
}

/*
 * Clear all searches.
 */
void wingo_dht_search_mgr_clear(wingo_dht_search_mgr_t *mgr)
{
    wingo_size i;

    if (mgr == NULL) {
        return;
    }

    for (i = 0; i < mgr->count; i++) {
        if (mgr->searches[i] != NULL) {
            wingo_dht_search_free(mgr->searches[i]);
            mgr->searches[i] = NULL;
        }
    }

    mgr->count = 0;
}

/* ============================================================================
 * SEARCH STATISTICS
 * ============================================================================ */

/*
 * Get search manager statistics.
 */
wingo_error_t wingo_dht_search_mgr_get_stats(
    const wingo_dht_search_mgr_t *mgr,
    wingo_dht_search_stats_t *stats)
{
    if (mgr == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(stats, &mgr->stats, sizeof(wingo_dht_search_stats_t));

    return WINGO_SUCCESS;
}

/*
 * Reset search manager statistics.
 */
void wingo_dht_search_mgr_reset_stats(wingo_dht_search_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    memset(&mgr->stats, 0, sizeof(wingo_dht_search_stats_t));
}

/* ============================================================================
 * SEARCH UTILITY
 * ============================================================================ */

/*
 * Get search type name.
 */
const char *wingo_dht_search_type_name(wingo_dht_search_type_t type)
{
    switch (type) {
    case WINGO_DHT_SEARCH_TYPE_FIND_NODE:  return "FIND_NODE";
    case WINGO_DHT_SEARCH_TYPE_GET_PEERS:  return "GET_PEERS";
    case WINGO_DHT_SEARCH_TYPE_ANNOUNCE:   return "ANNOUNCE";
    default:                               return "UNKNOWN";
    }
}

/*
 * Get search state name.
 */
const char *wingo_dht_search_state_name(wingo_dht_search_state_t state)
{
    switch (state) {
    case WINGO_DHT_SEARCH_STATE_INIT:     return "INIT";
    case WINGO_DHT_SEARCH_STATE_RUNNING:  return "RUNNING";
    case WINGO_DHT_SEARCH_STATE_DONE:     return "DONE";
    case WINGO_DHT_SEARCH_STATE_TIMEOUT:  return "TIMEOUT";
    case WINGO_DHT_SEARCH_STATE_CANCELED: return "CANCELED";
    case WINGO_DHT_SEARCH_STATE_ERROR:    return "ERROR";
    default:                              return "UNKNOWN";
    }
}

/*
 * Get step result name.
 */
const char *wingo_dht_step_result_name(wingo_dht_step_result_t result)
{
    switch (result) {
    case WINGO_DHT_STEP_DONE:    return "DONE";
    case WINGO_DHT_STEP_QUERY:   return "QUERY";
    case WINGO_DHT_STEP_WAIT:    return "WAIT";
    case WINGO_DHT_STEP_TIMEOUT: return "TIMEOUT";
    default:                     return "UNKNOWN";
    }
}

/*
 * Convert ID to hex string (internal helper).
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
 * Print search.
 */
void wingo_dht_search_print(const wingo_dht_search_t *search, FILE *f)
{
    char target_hex[WINGO_DHT_ID_HEX_SIZE];
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (search == NULL) {
        fprintf(f, "DHT search: (null)\n");
        return;
    }

    id_to_hex_local(&search->target, target_hex);

    fprintf(f, "DHT Search:\n");
    fprintf(f, "  Type:       %s\n", wingo_dht_search_type_name(search->type));
    fprintf(f, "  State:      %s\n", wingo_dht_search_state_name(search->state));
    fprintf(f, "  TID:        %u\n", search->tid);
    fprintf(f, "  Target:     %s\n", target_hex);
    fprintf(f, "  Port:       %u\n", search->port);
    fprintf(f, "  Age:        %llds\n", (long long)wingo_dht_search_age(search));
    fprintf(f, "  Nodes:      %zu\n", search->count);
    fprintf(f, "  Queries:    %llu sent, %llu replied\n",
            (unsigned long long)search->queries_sent,
            (unsigned long long)search->queries_replied);
    fprintf(f, "\n");

    if (search->count > 0) {
        fprintf(f, "  Closest nodes:\n");

        for (i = 0; i < search->count && i < 8; i++) {
            char id_hex[WINGO_DHT_ID_HEX_SIZE];
            char addr_str[WINGO_ADDR_STR_MAX];

            id_to_hex_local(&search->nodes[i].info.id, id_hex);

            if (search->nodes[i].info.addr != NULL) {
                wingo_addr_str(search->nodes[i].info.addr,
                               addr_str, sizeof(addr_str));
            } else {
                snprintf(addr_str, sizeof(addr_str), "(null)");
            }

            fprintf(f, "    [%zu] %s  %s  %s%s\n",
                    i, id_hex, addr_str,
                    search->nodes[i].queried ? "Q" : "-",
                    search->nodes[i].replied ? "R" : "-");
        }
    }
}

/*
 * Print search manager.
 */
void wingo_dht_search_mgr_print(const wingo_dht_search_mgr_t *mgr, FILE *f)
{
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (mgr == NULL) {
        fprintf(f, "DHT search manager: (null)\n");
        return;
    }

    fprintf(f, "DHT Search Manager:\n");
    fprintf(f, "  Searches:   %zu / %zu\n", mgr->count, mgr->max_searches);
    fprintf(f, "  Active:     %zu\n",
            wingo_dht_search_mgr_active_count(mgr));
    fprintf(f, "  Next TID:   %u\n", mgr->next_tid);
    fprintf(f, "\n");

    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Started:  %llu\n",
            (unsigned long long)mgr->stats.searches_started);
    fprintf(f, "    Done:     %llu\n",
            (unsigned long long)mgr->stats.searches_done);
    fprintf(f, "    Timeout:  %llu\n",
            (unsigned long long)mgr->stats.searches_timeout);
    fprintf(f, "    Error:    %llu\n",
            (unsigned long long)mgr->stats.searches_error);
    fprintf(f, "    Queries:  %llu sent, %llu replied\n",
            (unsigned long long)mgr->stats.queries_sent,
            (unsigned long long)mgr->stats.queries_replied);
    fprintf(f, "    Nodes:    %llu\n",
            (unsigned long long)mgr->stats.nodes_found);
    fprintf(f, "    Peers:    %llu\n",
            (unsigned long long)mgr->stats.peers_found);
    fprintf(f, "\n");

    if (mgr->count > 0) {
        fprintf(f, "  Active searches:\n");

        for (i = 0; i < mgr->count; i++) {
            wingo_dht_search_t *search = mgr->searches[i];

            if (search == NULL || search->done) {
                continue;
            }

            fprintf(f, "    [%zu] tid=%u type=%s state=%s nodes=%zu\n",
                    i, search->tid,
                    wingo_dht_search_type_name(search->type),
                    wingo_dht_search_state_name(search->state),
                    search->count);
        }
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
