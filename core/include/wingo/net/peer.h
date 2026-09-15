
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

#ifndef WINGO_NET_PEER_H
#define WINGO_NET_PEER_H
 

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"

/* ============================================================================
 * PEER TYPES
 * ============================================================================ */

/*
 * Peer role.
 */
typedef enum {
    WINGO_PEER_ROLE_NONE       = 0,   /* Unknown role */
    WINGO_PEER_ROLE_DONOR      = 1,   /* Shares Internet */
    WINGO_PEER_ROLE_RECIPIENT  = 2,   /* Receives Internet */
    WINGO_PEER_ROLE_BOTH       = 3,   /* Both */
} wingo_peer_role_t;

/*
 * Peer state.
 */
typedef enum {
    WINGO_PEER_STATE_UNKNOWN   = 0,   /* Unknown state */
    WINGO_PEER_STATE_DISCOVERED= 1,   /* Found via DHT */
    WINGO_PEER_STATE_CONNECTING= 2,   /* Connecting */
    WINGO_PEER_STATE_CONNECTED = 3,   /* Connected */
    WINGO_PEER_STATE_ACTIVE    = 4,   /* Active (data transfer) */
    WINGO_PEER_STATE_IDLE      = 5,   /* Idle */
    WINGO_PEER_STATE_DISCONNECT= 6,   /* Disconnected */
    WINGO_PEER_STATE_ERROR     = 7,   /* Error */
    WINGO_PEER_STATE_BLACKLIST = 8,   /* Blacklisted */
} wingo_peer_state_t;

/*
 * Peer trust level.
 */
typedef enum {
    WINGO_PEER_TRUST_NONE      = 0,   /* No trust */
    WINGO_PEER_TRUST_LOW       = 1,   /* Low trust */
    WINGO_PEER_TRUST_MEDIUM    = 2,   /* Medium trust */
    WINGO_PEER_TRUST_HIGH      = 3,   /* High trust */
    WINGO_PEER_TRUST_FULL      = 4,   /* Full trust (family) */
} wingo_peer_trust_t;

/* ============================================================================
 * PEER STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Peer handle.
 *
 * This is an opaque type. Use wingo_peer_*() functions.
 */
typedef struct wingo_peer wingo_peer_t;

/* ============================================================================
 * PEER MANAGER STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Peer manager handle.
 *
 * This is an opaque type. Use wingo_peer_mgr_*() functions.
 */
typedef struct wingo_peer_mgr wingo_peer_mgr_t;

/* ============================================================================
 * PEER MANAGER LIFECYCLE
 * ============================================================================ */

/*
 * Create a new peer manager.
 *
 * @param max_peers Maximum number of peers
 * @return          Peer manager, or NULL on error
 */
wingo_peer_mgr_t *wingo_peer_mgr_new(wingo_size max_peers);

/*
 * Free a peer manager.
 *
 * @param mgr       Peer manager (NULL is safe)
 */
void wingo_peer_mgr_free(wingo_peer_mgr_t *mgr);

/* ============================================================================
 * PEER MANAGEMENT
 * ============================================================================ */

/*
 * Add a peer to the manager.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @param addr      Peer address
 * @param role      Peer role
 * @return          Peer, or NULL on error
 */
wingo_peer_t *wingo_peer_mgr_add(wingo_peer_mgr_t *mgr,
                                  const wingo_id *id,
                                  const wingo_addr_t *addr,
                                  wingo_peer_role_t role);

/*
 * Remove a peer from the manager.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_mgr_remove(wingo_peer_mgr_t *mgr,
                                     const wingo_id *id);

/*
 * Find a peer by ID.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @return          Peer, or NULL if not found
 */
wingo_peer_t *wingo_peer_mgr_find(wingo_peer_mgr_t *mgr,
                                   const wingo_id *id);

/*
 * Find a peer by address.
 *
 * @param mgr       Peer manager
 * @param addr      Peer address
 * @return          Peer, or NULL if not found
 */
wingo_peer_t *wingo_peer_mgr_find_addr(wingo_peer_mgr_t *mgr,
                                        const wingo_addr_t *addr);

/*
 * Get number of peers.
 *
 * @param mgr       Peer manager
 * @return          Number of peers
 */
wingo_size wingo_peer_mgr_count(const wingo_peer_mgr_t *mgr);

/*
 * Get peer at index.
 *
 * @param mgr       Peer manager
 * @param index     Index
 * @return          Peer, or NULL if out of range
 */
wingo_peer_t *wingo_peer_mgr_get(wingo_peer_mgr_t *mgr, wingo_size index);

/*
 * Clear all peers.
 *
 * @param mgr       Peer manager
 */
void wingo_peer_mgr_clear(wingo_peer_mgr_t *mgr);

/* ============================================================================
 * PEER INFO
 * ============================================================================ */

/*
 * Get peer ID.
 *
 * @param peer      Peer
 * @return          Peer ID, or NULL on error
 */
const wingo_id *wingo_peer_id(const wingo_peer_t *peer);

/*
 * Get peer address.
 *
 * @param peer      Peer
 * @return          Peer address, or NULL on error
 */
const wingo_addr_t *wingo_peer_addr(const wingo_peer_t *peer);

/*
 * Get peer role.
 *
 * @param peer      Peer
 * @return          Peer role
 */
wingo_peer_role_t wingo_peer_role(const wingo_peer_t *peer);

/*
 * Set peer role.
 *
 * @param peer      Peer
 * @param role      New role
 */
void wingo_peer_set_role(wingo_peer_t *peer, wingo_peer_role_t role);

/*
 * Get peer state.
 *
 * @param peer      Peer
 * @return          Peer state
 */
wingo_peer_state_t wingo_peer_state(const wingo_peer_t *peer);

/*
 * Set peer state.
 *
 * @param peer      Peer
 * @param state     New state
 */
void wingo_peer_set_state(wingo_peer_t *peer, wingo_peer_state_t state);

/*
 * Get peer trust level.
 *
 * @param peer      Peer
 * @return          Trust level
 */
wingo_peer_trust_t wingo_peer_trust(const wingo_peer_t *peer);

/*
 * Set peer trust level.
 *
 * @param peer      Peer
 * @param trust     New trust level
 */
void wingo_peer_set_trust(wingo_peer_t *peer, wingo_peer_trust_t trust);

/*
 * Get peer name.
 *
 * @param peer      Peer
 * @return          Peer name (never NULL)
 */
const char *wingo_peer_name(const wingo_peer_t *peer);

/*
 * Set peer name.
 *
 * @param peer      Peer
 * @param name      New name
 */
void wingo_peer_set_name(wingo_peer_t *peer, const char *name);

/* ============================================================================
 * PEER STATE QUERY
 * ============================================================================ */

/*
 * Check if peer is connected.
 *
 * @param peer      Peer
 * @return          true if connected, false otherwise
 */
bool wingo_peer_is_connected(const wingo_peer_t *peer);

/*
 * Check if peer is active.
 *
 * @param peer      Peer
 * @return          true if active, false otherwise
 */
bool wingo_peer_is_active(const wingo_peer_t *peer);

/*
 * Check if peer is donor.
 *
 * @param peer      Peer
 * @return          true if donor, false otherwise
 */
bool wingo_peer_is_donor(const wingo_peer_t *peer);

/*
 * Check if peer is recipient.
 *
 * @param peer      Peer
 * @return          true if recipient, false otherwise
 */
bool wingo_peer_is_recipient(const wingo_peer_t *peer);

/*
 * Check if peer is blacklisted.
 *
 * @param peer      Peer
 * @return          true if blacklisted, false otherwise
 */
bool wingo_peer_is_blacklisted(const wingo_peer_t *peer);

/* ============================================================================
 * PEER CONNECTION
 * ============================================================================ */

/*
 * Connect to a peer.
 *
 * @param peer      Peer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_connect(wingo_peer_t *peer);

/*
 * Disconnect from a peer.
 *
 * @param peer      Peer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_disconnect(wingo_peer_t *peer);

/*
 * Get peer socket.
 *
 * @param peer      Peer
 * @return          Socket, or NULL on error
 */
wingo_sock_t *wingo_peer_socket(const wingo_peer_t *peer);

/* ============================================================================
 * PEER DATA TRANSFER
 * ============================================================================ */

/*
 * Send data to peer.
 *
 * @param peer      Peer
 * @param data      Data to send
 * @param len       Length of data
 * @return          Number of bytes sent, or -1 on error
 */
wingo_ssize wingo_peer_send(wingo_peer_t *peer,
                             const void *data,
                             wingo_size len);

/*
 * Receive data from peer.
 *
 * @param peer      Peer
 * @param buf       Output buffer
 * @param len       Buffer size
 * @return          Number of bytes received, or -1 on error
 */
wingo_ssize wingo_peer_recv(wingo_peer_t *peer, void *buf, wingo_size len);

/* ============================================================================
 * PEER STATISTICS
 * ============================================================================ */

/*
 * Peer statistics.
 */
typedef struct {
    wingo_u64   bytes_sent;
    wingo_u64   bytes_received;
    wingo_u64   packets_sent;
    wingo_u64   packets_received;
    wingo_u64   errors;
    wingo_i64   connected_at;
    wingo_i64   last_activity;
    wingo_i64   uptime;
} wingo_peer_stats_t;

/*
 * Get peer statistics.
 *
 * @param peer      Peer
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_get_stats(const wingo_peer_t *peer,
                                    wingo_peer_stats_t *stats);

/*
 * Reset peer statistics.
 *
 * @param peer      Peer
 */
void wingo_peer_reset_stats(wingo_peer_t *peer);

/* ============================================================================
 * PEER TIMEOUT
 * ============================================================================ */

/*
 * Get peer last activity time.
 *
 * @param peer      Peer
 * @return          Last activity timestamp
 */
wingo_i64 wingo_peer_last_activity(const wingo_peer_t *peer);

/*
 * Update peer activity.
 *
 * @param peer      Peer
 */
void wingo_peer_update_activity(wingo_peer_t *peer);

/*
 * Check if peer is timed out.
 *
 * @param peer      Peer
 * @param timeout_s Timeout in seconds
 * @return          true if timed out, false otherwise
 */
bool wingo_peer_is_timed_out(const wingo_peer_t *peer, wingo_i64 timeout_s);

/*
 * Expire timed out peers.
 *
 * @param mgr       Peer manager
 * @param timeout_s Timeout in seconds
 * @return          Number of peers expired
 */
wingo_size wingo_peer_mgr_expire(wingo_peer_mgr_t *mgr, wingo_i64 timeout_s);

/* ============================================================================
 * PEER BLACKLIST
 * ============================================================================ */

/*
 * Blacklist a peer.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_mgr_blacklist(wingo_peer_mgr_t *mgr,
                                        const wingo_id *id);

/*
 * Unblacklist a peer.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_peer_mgr_unblacklist(wingo_peer_mgr_t *mgr,
                                          const wingo_id *id);

/*
 * Check if peer is blacklisted.
 *
 * @param mgr       Peer manager
 * @param id        Peer ID
 * @return          true if blacklisted, false otherwise
 */
bool wingo_peer_mgr_is_blacklisted(const wingo_peer_mgr_t *mgr,
                                    const wingo_id *id);

/* ============================================================================
 * PEER ITERATION
 * ============================================================================ */

/*
 * Peer callback.
 *
 * @param peer      Peer
 * @param userdata  User data
 * @return          true to continue, false to stop
 */
typedef bool (*wingo_peer_cb_t)(wingo_peer_t *peer, void *userdata);

/*
 * Iterate over all peers.
 *
 * @param mgr       Peer manager
 * @param callback  Callback function
 * @param userdata  User data
 */
void wingo_peer_mgr_foreach(wingo_peer_mgr_t *mgr,
                             wingo_peer_cb_t callback,
                             void *userdata);

/* ============================================================================
 * PEER UTILITY
 * ============================================================================ */

/*
 * Get peer state name.
 *
 * @param state     Peer state
 * @return          Static string
 */
const char *wingo_peer_state_name(wingo_peer_state_t state);

/*
 * Get peer role name.
 *
 * @param role      Peer role
 * @return          Static string
 */
const char *wingo_peer_role_name(wingo_peer_role_t role);

/*
 * Print peer info.
 *
 * @param peer      Peer
 * @param f         Output file (NULL = stderr)
 */
void wingo_peer_print(const wingo_peer_t *peer, FILE *f);

/*
 * Print peer manager status.
 *
 * @param mgr       Peer manager
 * @param f         Output file (NULL = stderr)
 */
void wingo_peer_mgr_print(const wingo_peer_mgr_t *mgr, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_PEER_H */
