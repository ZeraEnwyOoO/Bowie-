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
 * Peer manager implementation for Bowie.
 *
 * The peer manager tracks all known peers (nodes that we've
 * communicated with or plan to communicate with).
 *
 * Data structures:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    PEER MANAGER                             │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │   Array     │  │  Hash Map   │  │  Blacklist  │        │
 *   │   │  (by index) │  │  (by ID)    │  │  (by ID)    │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   │   Each peer has:                                            │
 *   │   - ID (20 bytes)                                           │
 *   │   - Address (IP + port)                                     │
 *   │   - Role (donor/recipient/both)                             │
 *   │   - State (unknown/connected/...)                           │
 *   │   - Trust level                                             │
 *   │   - Name (optional)                                         │
 *   │   - Socket (if connected)                                   │
 *   │   - Statistics                                              │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/net/peer.h"
#include "wingo/net/socket.h"
#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"
#include "wingo/util/hashmap.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Peer (concrete).
 */
struct wingo_peer {
    /* ----- Identity ----- */
    wingo_id_t          id;
    wingo_addr_t       *addr;

    /* ----- Role & State ----- */
    wingo_peer_role_t   role;
    wingo_peer_state_t  state;
    wingo_peer_trust_t  trust;

    /* ----- Name ----- */
    char                name[64];

    /* ----- Connection ----- */
    wingo_sock_t       *sock;
    bool                owns_sock;

    /* ----- Timestamps ----- */
    wingo_i64           connected_at;
    wingo_i64           last_activity;
    wingo_i64           first_seen;

    /* ----- Statistics ----- */
    wingo_peer_stats_t  stats;

    /* ----- Flags ----- */
    bool                blacklisted;
};

/*
 * Peer manager (concrete).
 */
struct wingo_peer_mgr {
    /* ----- Peers (array) ----- */
    wingo_peer_t      **peers;
    wingo_size          count;
    wingo_size          capacity;
    wingo_size          max_peers;

    /* ----- ID → peer map ----- */
    wingo_hashmap_bin_t *id_map;

    /* ----- Blacklist ----- */
    wingo_hashmap_bin_t *blacklist;
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Initial peer array capacity.
 */
#define PEER_INIT_CAPACITY      16

/*
 * Growth factor.
 */
#define PEER_GROW_FACTOR        2

/*
 * Default name.
 */
#define PEER_DEFAULT_NAME       "peer"

/* ============================================================================
 * INTERNAL HELPERS — PEER ARRAY
 * ============================================================================ */

/*
 * Grow peer array.
 */
static wingo_error_t mgr_grow_peers(wingo_peer_mgr_t *mgr)
{
    wingo_size new_cap;
    wingo_peer_t **new_peers;

    if (mgr->capacity == 0) {
        new_cap = PEER_INIT_CAPACITY;
    } else {
        new_cap = mgr->capacity * PEER_GROW_FACTOR;
    }

    if (new_cap < mgr->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    if (mgr->max_peers > 0 && new_cap > mgr->max_peers) {
        new_cap = mgr->max_peers;
    }

    new_peers = realloc(mgr->peers, new_cap * sizeof(wingo_peer_t *));
    if (new_peers == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Zero new memory */
    if (new_cap > mgr->capacity) {
        memset(&new_peers[mgr->capacity], 0,
               (new_cap - mgr->capacity) * sizeof(wingo_peer_t *));
    }

    mgr->peers = new_peers;
    mgr->capacity = new_cap;

    return WINGO_SUCCESS;
}

/*
 * Find peer index in array.
 */
static int mgr_find_index(const wingo_peer_mgr_t *mgr,
                           const wingo_peer_t *peer)
{
    wingo_size i;

    for (i = 0; i < mgr->count; i++) {
        if (mgr->peers[i] == peer) {
            return (int)i;
        }
    }

    return -1;
}

/*
 * Remove peer from array at index.
 */
static void mgr_remove_at(wingo_peer_mgr_t *mgr, wingo_size index)
{
    if (index >= mgr->count) {
        return;
    }

    if (index < mgr->count - 1) {
        memmove(&mgr->peers[index],
                &mgr->peers[index + 1],
                (mgr->count - index - 1) * sizeof(wingo_peer_t *));
    }

    mgr->count--;
    mgr->peers[mgr->count] = NULL;
}

/* ============================================================================
 * INTERNAL HELPERS — BLACKLIST
 * ============================================================================ */

/*
 * Check if ID is blacklisted.
 */
static bool mgr_is_blacklisted(const wingo_peer_mgr_t *mgr,
                                const wingo_id_t *id)
{
    if (mgr == NULL || mgr->blacklist == NULL || id == NULL) {
        return false;
    }

    return wingo_hashmap_bin_has(mgr->blacklist, id->bytes, WINGO_ID_SIZE);
}

/*
 * Add ID to blacklist.
 */
static wingo_error_t mgr_blacklist_add(wingo_peer_mgr_t *mgr,
                                        const wingo_id_t *id)
{
    if (mgr == NULL || mgr->blacklist == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Check if already blacklisted */
    if (wingo_hashmap_bin_has(mgr->blacklist, id->bytes, WINGO_ID_SIZE)) {
        return WINGO_SUCCESS;
    }

    /* Store with value 1 */
    return wingo_hashmap_bin_set(mgr->blacklist, id->bytes, WINGO_ID_SIZE,
                                  (void *)(uintptr_t)1);
}

/*
 * Remove ID from blacklist.
 */
static wingo_error_t mgr_blacklist_remove(wingo_peer_mgr_t *mgr,
                                           const wingo_id_t *id)
{
    if (mgr == NULL || mgr->blacklist == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    void *removed = wingo_hashmap_bin_remove(mgr->blacklist,
                                               id->bytes, WINGO_ID_SIZE);
    if (removed == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * PEER LIFECYCLE
 * ============================================================================ */

/*
 * Allocate a new peer.
 */
static wingo_peer_t *peer_alloc(const wingo_id_t *id,
                                 const wingo_addr_t *addr,
                                 wingo_peer_role_t role)
{
    wingo_peer_t *peer;

    peer = calloc(1, sizeof(wingo_peer_t));
    if (peer == NULL) {
        return NULL;
    }

    /* Copy ID */
    if (id != NULL) {
        memcpy(&peer->id, id, sizeof(wingo_id_t));
    } else {
        memset(&peer->id, 0, sizeof(wingo_id_t));
    }

    /* Copy address */
    if (addr != NULL) {
        peer->addr = wingo_addr_copy(addr);
        if (peer->addr == NULL) {
            free(peer);
            return NULL;
        }
    } else {
        peer->addr = NULL;
    }

    /* Initialize fields */
    peer->role = role;
    peer->state = WINGO_PEER_STATE_DISCOVERED;
    peer->trust = WINGO_PEER_TRUST_NONE;
    snprintf(peer->name, sizeof(peer->name), "%s", PEER_DEFAULT_NAME);

    peer->sock = NULL;
    peer->owns_sock = false;

    peer->first_seen = wingo_time_now();
    peer->last_activity = peer->first_seen;
    peer->connected_at = 0;

    memset(&peer->stats, 0, sizeof(peer->stats));

    peer->blacklisted = false;

    return peer;
}

/*
 * Free a peer.
 */
static void peer_free(wingo_peer_t *peer)
{
    if (peer == NULL) {
        return;
    }

    /* Free socket if we own it */
    if (peer->sock != NULL && peer->owns_sock) {
        wingo_sock_free(peer->sock);
    }
    peer->sock = NULL;

    /* Free address */
    if (peer->addr != NULL) {
        wingo_addr_free(peer->addr);
        peer->addr = NULL;
    }

    free(peer);
}

/* ============================================================================
 * PEER MANAGER LIFECYCLE
 * ============================================================================ */

/*
 * Create a new peer manager.
 */
wingo_peer_mgr_t *wingo_peer_mgr_new(wingo_size max_peers)
{
    wingo_peer_mgr_t *mgr;

    mgr = calloc(1, sizeof(wingo_peer_mgr_t));
    if (mgr == NULL) {
        return NULL;
    }

    /* Set max peers */
    if (max_peers == 0) {
        max_peers = WINGO_MAX_PEERS;
    }
    mgr->max_peers = max_peers;

    /* Allocate peer array */
    mgr->capacity = PEER_INIT_CAPACITY;
    if (mgr->capacity > max_peers) {
        mgr->capacity = max_peers;
    }

    mgr->peers = calloc(mgr->capacity, sizeof(wingo_peer_t *));
    if (mgr->peers == NULL) {
        free(mgr);
        return NULL;
    }

    mgr->count = 0;

    /* Create ID map */
    mgr->id_map = wingo_hashmap_bin_new(64, NULL, NULL);
    if (mgr->id_map == NULL) {
        free(mgr->peers);
        free(mgr);
        return NULL;
    }

    /* Create blacklist */
    mgr->blacklist = wingo_hashmap_bin_new(16, NULL, NULL);
    if (mgr->blacklist == NULL) {
        wingo_hashmap_bin_free(mgr->id_map);
        free(mgr->peers);
        free(mgr);
        return NULL;
    }

    WINGO_LOG_DEBUG("Peer manager created (max=%zu)", max_peers);

    return mgr;
}

/*
 * Free a peer manager.
 */
void wingo_peer_mgr_free(wingo_peer_mgr_t *mgr)
{
    wingo_size i;

    if (mgr == NULL) {
        return;
    }

    /* Free all peers */
    if (mgr->peers != NULL) {
        for (i = 0; i < mgr->count; i++) {
            peer_free(mgr->peers[i]);
        }
        free(mgr->peers);
    }

    /* Free maps */
    if (mgr->id_map != NULL) {
        wingo_hashmap_bin_free(mgr->id_map);
    }

    if (mgr->blacklist != NULL) {
        wingo_hashmap_bin_free(mgr->blacklist);
    }

    free(mgr);
}

/* ============================================================================
 * PEER MANAGEMENT
 * ============================================================================ */

/*
 * Add a peer to the manager.
 */
wingo_peer_t *wingo_peer_mgr_add(wingo_peer_mgr_t *mgr,
                                  const wingo_id_t *id,
                                  const wingo_addr_t *addr,
                                  wingo_peer_role_t role)
{
    wingo_peer_t *peer;
    wingo_error_t rc;

    if (mgr == NULL) {
        return NULL;
    }

    /* Check if peer already exists (by ID) */
    if (id != NULL) {
        peer = wingo_peer_mgr_find(mgr, id);
        if (peer != NULL) {
            /* Update address if changed */
            if (addr != NULL) {
                wingo_peer_set_state(peer, WINGO_PEER_STATE_DISCOVERED);
                wingo_peer_update_activity(peer);
            }
            return peer;
        }
    }

    /* Check capacity */
    if (mgr->count >= mgr->max_peers) {
        WINGO_LOG_WARN("Peer manager: max peers reached (%zu)", mgr->max_peers);
        return NULL;
    }

    /* Grow if needed */
    if (mgr->count >= mgr->capacity) {
        rc = mgr_grow_peers(mgr);
        if (rc != WINGO_SUCCESS) {
            return NULL;
        }
    }

    /* Allocate new peer */
    peer = peer_alloc(id, addr, role);
    if (peer == NULL) {
        return NULL;
    }

    /* Add to array */
    mgr->peers[mgr->count++] = peer;

    /* Add to ID map */
    if (id != NULL) {
        rc = wingo_hashmap_bin_set(mgr->id_map, id->bytes, WINGO_ID_SIZE,
                                    peer);
        if (rc != WINGO_SUCCESS) {
            /* Remove from array */
            mgr->count--;
            peer_free(peer);
            return NULL;
        }
    }

    WINGO_LOG_DEBUG("Peer added (count=%zu)", mgr->count);

    return peer;
}

/*
 * Remove a peer from the manager.
 */
wingo_error_t wingo_peer_mgr_remove(wingo_peer_mgr_t *mgr,
                                     const wingo_id_t *id)
{
    wingo_peer_t *peer;
    int index;

    if (mgr == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Find peer */
    peer = wingo_peer_mgr_find(mgr, id);
    if (peer == NULL) {
        return WINGO_ERR_NOT_FOUND;
    }

    /* Find index */
    index = mgr_find_index(mgr, peer);
    if (index < 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    /* Remove from ID map */
    wingo_hashmap_bin_remove(mgr->id_map, id->bytes, WINGO_ID_SIZE);

    /* Remove from array */
    mgr_remove_at(mgr, (wingo_size)index);

    /* Free peer */
    peer_free(peer);

    return WINGO_SUCCESS;
}

/*
 * Find a peer by ID.
 */
wingo_peer_t *wingo_peer_mgr_find(wingo_peer_mgr_t *mgr,
                                   const wingo_id_t *id)
{
    void *value;

    if (mgr == NULL || id == NULL || mgr->id_map == NULL) {
        return NULL;
    }

    value = wingo_hashmap_bin_get(mgr->id_map, id->bytes, WINGO_ID_SIZE);

    return (wingo_peer_t *)value;
}

/*
 * Find a peer by address.
 */
wingo_peer_t *wingo_peer_mgr_find_addr(wingo_peer_mgr_t *mgr,
                                        const wingo_addr_t *addr)
{
    wingo_size i;

    if (mgr == NULL || addr == NULL) {
        return NULL;
    }

    for (i = 0; i < mgr->count; i++) {
        wingo_peer_t *peer = mgr->peers[i];

        if (peer != NULL && peer->addr != NULL &&
            wingo_addr_cmp(peer->addr, addr) == 0) {
            return peer;
        }
    }

    return NULL;
}

/*
 * Get number of peers.
 */
wingo_size wingo_peer_mgr_count(const wingo_peer_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }

    return mgr->count;
}

/*
 * Get peer at index.
 */
wingo_peer_t *wingo_peer_mgr_get(wingo_peer_mgr_t *mgr, wingo_size index)
{
    if (mgr == NULL || index >= mgr->count) {
        return NULL;
    }

    return mgr->peers[index];
}

/*
 * Clear all peers.
 */
void wingo_peer_mgr_clear(wingo_peer_mgr_t *mgr)
{
    wingo_size i;

    if (mgr == NULL) {
        return;
    }

    for (i = 0; i < mgr->count; i++) {
        peer_free(mgr->peers[i]);
        mgr->peers[i] = NULL;
    }

    mgr->count = 0;

    if (mgr->id_map != NULL) {
        wingo_hashmap_bin_clear(mgr->id_map);
    }
}

/* ============================================================================
 * PEER INFO
 * ============================================================================ */

/*
 * Get peer ID.
 */
const wingo_id_t *wingo_peer_id(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return NULL;
    }

    return &peer->id;
}

/*
 * Get peer address.
 */
const wingo_addr_t *wingo_peer_addr(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return NULL;
    }

    return peer->addr;
}

/*
 * Get peer role.
 */
wingo_peer_role_t wingo_peer_role(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return WINGO_PEER_ROLE_NONE;
    }

    return peer->role;
}

/*
 * Set peer role.
 */
void wingo_peer_set_role(wingo_peer_t *peer, wingo_peer_role_t role)
{
    if (peer == NULL) {
        return;
    }

    peer->role = role;
}

/*
 * Get peer state.
 */
wingo_peer_state_t wingo_peer_state(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return WINGO_PEER_STATE_UNKNOWN;
    }

    return peer->state;
}

/*
 * Set peer state.
 */
void wingo_peer_set_state(wingo_peer_t *peer, wingo_peer_state_t state)
{
    if (peer == NULL) {
        return;
    }

    peer->state = state;

    /* Update timestamp if connecting */
    if (state == WINGO_PEER_STATE_CONNECTING) {
        peer->connected_at = wingo_time_now();
    }
}

/*
 * Get peer trust level.
 */
wingo_peer_trust_t wingo_peer_trust(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return WINGO_PEER_TRUST_NONE;
    }

    return peer->trust;
}

/*
 * Set peer trust level.
 */
void wingo_peer_set_trust(wingo_peer_t *peer, wingo_peer_trust_t trust)
{
    if (peer == NULL) {
        return;
    }

    peer->trust = trust;
}

/*
 * Get peer name.
 */
const char *wingo_peer_name(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return "";
    }

    return peer->name;
}

/*
 * Set peer name.
 */
void wingo_peer_set_name(wingo_peer_t *peer, const char *name)
{
    wingo_size len;

    if (peer == NULL || name == NULL) {
        return;
    }

    len = strlen(name);
    if (len >= sizeof(peer->name)) {
        len = sizeof(peer->name) - 1;
    }

    memcpy(peer->name, name, len);
    peer->name[len] = '\0';
}

/* ============================================================================
 * PEER STATE QUERY
 * ============================================================================ */

/*
 * Check if peer is connected.
 */
bool wingo_peer_is_connected(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }

    return peer->state == WINGO_PEER_STATE_CONNECTED ||
           peer->state == WINGO_PEER_STATE_ACTIVE;
}

/*
 * Check if peer is active.
 */
bool wingo_peer_is_active(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }

    return peer->state == WINGO_PEER_STATE_ACTIVE;
}

/*
 * Check if peer is donor.
 */
bool wingo_peer_is_donor(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }

    return peer->role == WINGO_PEER_ROLE_DONOR ||
           peer->role == WINGO_PEER_ROLE_BOTH;
}

/*
 * Check if peer is recipient.
 */
bool wingo_peer_is_recipient(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }

    return peer->role == WINGO_PEER_ROLE_RECIPIENT ||
           peer->role == WINGO_PEER_ROLE_BOTH;
}

/*
 * Check if peer is blacklisted.
 */
bool wingo_peer_is_blacklisted(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return false;
    }

    return peer->blacklisted;
}
/* ============================================================================
 * PEER CONNECTION
 * ============================================================================ */

/*
 * Connect to a peer.
 */
wingo_error_t wingo_peer_connect(wingo_peer_t *peer)
{
    wingo_error_t rc;
    wingo_sock_t *sock;

    if (peer == NULL || peer->addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Already connected? */
    if (peer->state == WINGO_PEER_STATE_CONNECTED ||
        peer->state == WINGO_PEER_STATE_ACTIVE) {
        return WINGO_SUCCESS;
    }

    /* Set state */
    peer->state = WINGO_PEER_STATE_CONNECTING;
    peer->connected_at = wingo_time_now();

    /* Create TCP socket */
    sock = wingo_sock_new_tcp(wingo_addr_family(peer->addr));
    if (sock == NULL) {
        peer->state = WINGO_PEER_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* Set non-blocking */
    wingo_sock_set_blocking(sock, false);

    /* Set timeout */
    wingo_sock_set_timeout(sock, WINGO_PEER_CONNECT_TIMEOUT_MS);

    /* Connect */
    rc = wingo_sock_connect_timeout(sock, peer->addr,
                                      WINGO_PEER_CONNECT_TIMEOUT_MS);
    if (rc != WINGO_SUCCESS) {
        wingo_sock_free(sock);
        peer->state = WINGO_PEER_STATE_ERROR;
        peer->stats.errors++;
        return rc;
    }

    /* Store socket */
    if (peer->sock != NULL && peer->owns_sock) {
        wingo_sock_free(peer->sock);
    }

    peer->sock = sock;
    peer->owns_sock = true;
    peer->state = WINGO_PEER_STATE_CONNECTED;
    peer->last_activity = wingo_time_now();

    WINGO_LOG_DEBUG("Peer connected: %s", peer->name);

    return WINGO_SUCCESS;
}

/*
 * Disconnect from a peer.
 */
wingo_error_t wingo_peer_disconnect(wingo_peer_t *peer)
{
    if (peer == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (peer->sock != NULL) {
        if (peer->owns_sock) {
            wingo_sock_free(peer->sock);
        }
        peer->sock = NULL;
        peer->owns_sock = false;
    }

    peer->state = WINGO_PEER_STATE_DISCONNECT;
    peer->last_activity = wingo_time_now();

    WINGO_LOG_DEBUG("Peer disconnected: %s", peer->name);

    return WINGO_SUCCESS;
}

/*
 * Get peer socket.
 */
wingo_sock_t *wingo_peer_socket(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return NULL;
    }

    return peer->sock;
}

/* ============================================================================
 * PEER DATA TRANSFER
 * ============================================================================ */

/*
 * Send data to peer.
 */
wingo_ssize wingo_peer_send(wingo_peer_t *peer,
                             const void *data,
                             wingo_size len)
{
    wingo_ssize n;

    if (peer == NULL || data == NULL) {
        return -1;
    }

    if (peer->sock == NULL) {
        return -1;
    }

    n = wingo_sock_send(peer->sock, data, len);

    if (n > 0) {
        peer->stats.bytes_sent += (wingo_u64)n;
        peer->stats.packets_sent++;
        peer->last_activity = wingo_time_now();
    } else {
        peer->stats.errors++;
    }

    return n;
}

/*
 * Receive data from peer.
 */
wingo_ssize wingo_peer_recv(wingo_peer_t *peer, void *buf, wingo_size len)
{
    wingo_ssize n;

    if (peer == NULL || buf == NULL) {
        return -1;
    }

    if (peer->sock == NULL) {
        return -1;
    }

    n = wingo_sock_recv(peer->sock, buf, len);

    if (n > 0) {
        peer->stats.bytes_received += (wingo_u64)n;
        peer->stats.packets_received++;
        peer->last_activity = wingo_time_now();
    } else if (n < 0) {
        peer->stats.errors++;
    }

    return n;
}

/* ============================================================================
 * PEER STATISTICS
 * ============================================================================ */

/*
 * Get peer statistics.
 */
wingo_error_t wingo_peer_get_stats(const wingo_peer_t *peer,
                                    wingo_peer_stats_t *stats)
{
    wingo_i64 now;

    if (peer == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(stats, &peer->stats, sizeof(wingo_peer_stats_t));

    /* Fill in computed fields */
    stats->connected_at = peer->connected_at;
    stats->last_activity = peer->last_activity;

    now = wingo_time_now();
    if (peer->connected_at > 0) {
        stats->uptime = now - peer->connected_at;
    } else {
        stats->uptime = 0;
    }

    return WINGO_SUCCESS;
}

/*
 * Reset peer statistics.
 */
void wingo_peer_reset_stats(wingo_peer_t *peer)
{
    if (peer == NULL) {
        return;
    }

    memset(&peer->stats, 0, sizeof(peer->stats));
}

/* ============================================================================
 * PEER TIMEOUT
 * ============================================================================ */

/*
 * Get peer last activity time.
 */
wingo_i64 wingo_peer_last_activity(const wingo_peer_t *peer)
{
    if (peer == NULL) {
        return 0;
    }

    return peer->last_activity;
}

/*
 * Update peer activity.
 */
void wingo_peer_update_activity(wingo_peer_t *peer)
{
    if (peer == NULL) {
        return;
    }

    peer->last_activity = wingo_time_now();
}

/*
 * Check if peer is timed out.
 */
bool wingo_peer_is_timed_out(const wingo_peer_t *peer, wingo_i64 timeout_s)
{
    wingo_i64 now;
    wingo_i64 last;

    if (peer == NULL) {
        return true;
    }

    if (timeout_s <= 0) {
        return false;
    }

    now = wingo_time_now();
    last = (peer->last_activity > 0) ? peer->last_activity : peer->first_seen;

    if (last <= 0) {
        return true;
    }

    return (now - last) >= timeout_s;
}

/*
 * Expire timed out peers.
 */
wingo_size wingo_peer_mgr_expire(wingo_peer_mgr_t *mgr, wingo_i64 timeout_s)
{
    wingo_size i = 0;
    wingo_size expired = 0;

    if (mgr == NULL) {
        return 0;
    }

    while (i < mgr->count) {
        wingo_peer_t *peer = mgr->peers[i];

        if (peer == NULL) {
            i++;
            continue;
        }

        if (wingo_peer_is_timed_out(peer, timeout_s)) {
            /* Remove from ID map */
            if (mgr->id_map != NULL) {
                wingo_hashmap_bin_remove(mgr->id_map,
                                          peer->id.bytes, WINGO_ID_SIZE);
            }

            /* Remove from array */
            mgr_remove_at(mgr, i);

            /* Free peer */
            peer_free(peer);

            expired++;
            /* Don't increment i — next peer shifted into position i */
        } else {
            i++;
        }
    }

    if (expired > 0) {
        WINGO_LOG_DEBUG("Peer manager: expired %zu peers", expired);
    }

    return expired;
}

/* ============================================================================
 * PEER BLACKLIST
 * ============================================================================ */

/*
 * Blacklist a peer.
 */
wingo_error_t wingo_peer_mgr_blacklist(wingo_peer_mgr_t *mgr,
                                        const wingo_id_t *id)
{
    wingo_peer_t *peer;
    wingo_error_t rc;

    if (mgr == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Add to blacklist map */
    rc = mgr_blacklist_add(mgr, id);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Mark peer as blacklisted */
    peer = wingo_peer_mgr_find(mgr, id);
    if (peer != NULL) {
        peer->blacklisted = true;
        peer->state = WINGO_PEER_STATE_BLACKLIST;

        /* Disconnect if connected */
        if (peer->sock != NULL) {
            wingo_peer_disconnect(peer);
        }
    }

    WINGO_LOG_DEBUG("Peer blacklisted");

    return WINGO_SUCCESS;
}

/*
 * Unblacklist a peer.
 */
wingo_error_t wingo_peer_mgr_unblacklist(wingo_peer_mgr_t *mgr,
                                          const wingo_id_t *id)
{
    wingo_peer_t *peer;
    wingo_error_t rc;

    if (mgr == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Remove from blacklist map */
    rc = mgr_blacklist_remove(mgr, id);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Mark peer as not blacklisted */
    peer = wingo_peer_mgr_find(mgr, id);
    if (peer != NULL) {
        peer->blacklisted = false;

        if (peer->state == WINGO_PEER_STATE_BLACKLIST) {
            peer->state = WINGO_PEER_STATE_DISCOVERED;
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Check if peer is blacklisted.
 */
bool wingo_peer_mgr_is_blacklisted(const wingo_peer_mgr_t *mgr,
                                    const wingo_id_t *id)
{
    if (mgr == NULL || id == NULL) {
        return false;
    }

    return mgr_is_blacklisted(mgr, id);
}

/* ============================================================================
 * PEER ITERATION
 * ============================================================================ */

/*
 * Iterate over all peers.
 */
void wingo_peer_mgr_foreach(wingo_peer_mgr_t *mgr,
                             wingo_peer_cb_t callback,
                             void *userdata)
{
    wingo_size i;

    if (mgr == NULL || callback == NULL) {
        return;
    }

    for (i = 0; i < mgr->count; i++) {
        wingo_peer_t *peer = mgr->peers[i];

        if (peer == NULL) {
            continue;
        }

        if (!callback(peer, userdata)) {
            break;
        }
    }
}

/* ============================================================================
 * PEER UTILITY
 * ============================================================================ */

/*
 * Get peer state name.
 */
const char *wingo_peer_state_name(wingo_peer_state_t state)
{
    switch (state) {
    case WINGO_PEER_STATE_UNKNOWN:    return "UNKNOWN";
    case WINGO_PEER_STATE_DISCOVERED: return "DISCOVERED";
    case WINGO_PEER_STATE_CONNECTING: return "CONNECTING";
    case WINGO_PEER_STATE_CONNECTED:  return "CONNECTED";
    case WINGO_PEER_STATE_ACTIVE:     return "ACTIVE";
    case WINGO_PEER_STATE_IDLE:       return "IDLE";
    case WINGO_PEER_STATE_DISCONNECT: return "DISCONNECT";
    case WINGO_PEER_STATE_ERROR:      return "ERROR";
    case WINGO_PEER_STATE_BLACKLIST:  return "BLACKLIST";
    default:                          return "INVALID";
    }
}

/*
 * Get peer role name.
 */
const char *wingo_peer_role_name(wingo_peer_role_t role)
{
    switch (role) {
    case WINGO_PEER_ROLE_NONE:      return "NONE";
    case WINGO_PEER_ROLE_DONOR:     return "DONOR";
    case WINGO_PEER_ROLE_RECIPIENT: return "RECIPIENT";
    case WINGO_PEER_ROLE_BOTH:      return "BOTH";
    default:                        return "INVALID";
    }
}

/*
 * Convert ID to hex (local helper).
 */
static void id_to_hex_local(const wingo_id_t *id, char *buf)
{
    static const char hex[] = "0123456789abcdef";
    wingo_size i;

    for (i = 0; i < WINGO_ID_SIZE; i++) {
        buf[i * 2]     = hex[(id->bytes[i] >> 4) & 0x0F];
        buf[i * 2 + 1] = hex[id->bytes[i] & 0x0F];
    }
    buf[WINGO_ID_SIZE * 2] = '\0';
}

/*
 * Print peer info.
 */
void wingo_peer_print(const wingo_peer_t *peer, FILE *f)
{
    char id_hex[WINGO_ID_HEX_SIZE];
    char addr_str[WINGO_ADDR_STR_MAX];

    if (f == NULL) {
        f = stderr;
    }

    if (peer == NULL) {
        fprintf(f, "Peer: (null)\n");
        return;
    }

    id_to_hex_local(&peer->id, id_hex);

    if (peer->addr != NULL) {
        wingo_addr_str(peer->addr, addr_str, sizeof(addr_str));
    } else {
        snprintf(addr_str, sizeof(addr_str), "(none)");
    }

    fprintf(f, "Peer:\n");
    fprintf(f, "  ID:        %s\n", id_hex);
    fprintf(f, "  Address:   %s\n", addr_str);
    fprintf(f, "  Name:      %s\n", peer->name);
    fprintf(f, "  Role:      %s\n", wingo_peer_role_name(peer->role));
    fprintf(f, "  State:     %s\n", wingo_peer_state_name(peer->state));
    fprintf(f, "  Trust:     %d\n", peer->trust);
    fprintf(f, "  Blacklist: %s\n", peer->blacklisted ? "yes" : "no");
    fprintf(f, "  Socket:    %s\n", peer->sock != NULL ? "yes" : "no");
    fprintf(f, "\n");
    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Bytes sent:      %llu\n",
            (unsigned long long)peer->stats.bytes_sent);
    fprintf(f, "    Bytes received:  %llu\n",
            (unsigned long long)peer->stats.bytes_received);
    fprintf(f, "    Packets sent:    %llu\n",
            (unsigned long long)peer->stats.packets_sent);
    fprintf(f, "    Packets recv:    %llu\n",
            (unsigned long long)peer->stats.packets_received);
    fprintf(f, "    Errors:          %llu\n",
            (unsigned long long)peer->stats.errors);
    fprintf(f, "    Uptime:          %llds\n",
            (long long)peer->stats.uptime);
}

/*
 * Print peer manager status.
 */
void wingo_peer_mgr_print(const wingo_peer_mgr_t *mgr, FILE *f)
{
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (mgr == NULL) {
        fprintf(f, "Peer manager: (null)\n");
        return;
    }

    fprintf(f, "Peer Manager:\n");
    fprintf(f, "  Peers:     %zu / %zu\n", mgr->count, mgr->max_peers);
    fprintf(f, "  Capacity:  %zu\n", mgr->capacity);
    fprintf(f, "  Blacklist: %zu\n",
            mgr->blacklist != NULL
                ? wingo_hashmap_bin_count(mgr->blacklist)
                : 0);
    fprintf(f, "\n");

    if (mgr->count > 0) {
        fprintf(f, "  Peers:\n");

        for (i = 0; i < mgr->count; i++) {
            wingo_peer_t *peer = mgr->peers[i];

            if (peer == NULL) {
                continue;
            }

            fprintf(f, "    [%zu] %s  %s  %s  %s\n",
                    i,
                    peer->name,
                    wingo_peer_role_name(peer->role),
                    wingo_peer_state_name(peer->state),
                    peer->blacklisted ? "BLACKLISTED" : "");
        }
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
