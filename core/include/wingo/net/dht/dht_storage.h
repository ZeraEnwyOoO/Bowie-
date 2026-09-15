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

#ifndef WINGO_NET_DHT_STORAGE_H
#define WINGO_NET_DHT_STORAGE_H

/*
 * ============================================================================
 * WINGO DHT STORAGE
 * ============================================================================
 *
 * This header provides DHT storage for Bowie.
 *
 * DHT storage stores info hash → peer mappings.
 * When a peer wants to share Internet, it announces itself to the DHT.
 * When a peer wants to find a donor, it looks up the info hash.
 *
 * Storage model:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT STORAGE                              │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Info Hash 1                                        │   │
 *   │   │  ├── Peer A (10.0.0.1:6881)                         │   │
 *   │   │  ├── Peer B (10.0.0.2:6881)                         │   │
 *   │   │  └── Peer C (10.0.0.3:6881)                         │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Info Hash 2                                        │   │
 *   │   │  ├── Peer D (10.0.0.4:6881)                         │   │
 *   │   │  └── Peer E (10.0.0.5:6881)                         │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │  Info Hash N                                        │   │
 *   │   │  └── ...                                            │   │
 *   │   └─────────────────────────────────────────────────────┘   │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * Storage rules:
 *   - Each info hash has a peer list (max WINGO_DHT_STORAGE_MAX_PEERS)
 *   - Peers expire after WINGO_DHT_STORAGE_PEER_TTL seconds
 *   - Info hashes expire after WINGO_DHT_STORAGE_HASH_TTL seconds
 *   - Total storage limited to WINGO_DHT_STORAGE_MAX_HASHES
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/dht.h"

/* ============================================================================
 * DHT STORAGE CONSTANTS
 * ============================================================================ */

/*
 * Maximum number of info hashes in storage.
 */
#define WINGO_DHT_STORAGE_MAX_HASHES            16384

/*
 * Maximum number of peers per info hash.
 */
#define WINGO_DHT_STORAGE_MAX_PEERS             50

/*
 * Peer time-to-live (30 minutes).
 *
 * Peers must re-announce before this expires.
 */
#define WINGO_DHT_STORAGE_PEER_TTL              (30 * 60)

/*
 * Info hash time-to-live (2 hours).
 *
 * Info hash expires if no peers announced.
 */
#define WINGO_DHT_STORAGE_HASH_TTL              (2 * 60 * 60)

/*
 * Initial peer list capacity.
 */
#define WINGO_DHT_STORAGE_PEER_INIT_CAP         8

/*
 * Maximum peer list capacity.
 */
#define WINGO_DHT_STORAGE_PEER_MAX_CAP          64

/* ============================================================================
 * DHT STORAGE TYPES
 * ============================================================================ */

/*
 * Storage result.
 */
typedef enum {
    WINGO_DHT_STORAGE_OK        = 0,   /* Success */
    WINGO_DHT_STORAGE_FULL      = 1,   /* Storage full */
    WINGO_DHT_STORAGE_NOT_FOUND = 2,   /* Info hash not found */
    WINGO_DHT_STORAGE_EXPIRED   = 3,   /* Entry expired */
    WINGO_DHT_STORAGE_DUPLICATE = 4,   /* Peer already exists */
    WINGO_DHT_STORAGE_ERROR     = 5,   /* Internal error */
} wingo_dht_storage_result_t;

/* ============================================================================
 * DHT STORAGE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT storage handle.
 *
 * This is an opaque type. Use wingo_dht_storage_*() functions.
 */
typedef struct wingo_dht_storage wingo_dht_storage_t;

/* ============================================================================
 * DHT STORAGE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT storage.
 *
 * @param max_hashes        Maximum info hashes (0 = default)
 * @param max_peers_per_hash Maximum peers per hash (0 = default)
 * @param peer_ttl_s        Peer TTL in seconds (0 = default)
 * @param hash_ttl_s        Hash TTL in seconds (0 = default)
 * @return                  Storage handle, or NULL on error
 */
wingo_dht_storage_t *wingo_dht_storage_new(wingo_size max_hashes,
                                            wingo_size max_peers_per_hash,
                                            wingo_i64 peer_ttl_s,
                                            wingo_i64 hash_ttl_s);

/*
 * Free a DHT storage.
 *
 * @param storage   Storage handle (NULL is safe)
 */
void wingo_dht_storage_free(wingo_dht_storage_t *storage);

/*
 * Clear all storage entries.
 *
 * @param storage   Storage handle
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_clear(wingo_dht_storage_t *storage);

/* ============================================================================
 * DHT STORAGE ANNOUNCE
 * ============================================================================ */

/*
 * Announce a peer for an info hash.
 *
 * This adds a peer to the peer list for the given info hash.
 * If the info hash doesn't exist, it's created.
 * If the peer already exists, its TTL is refreshed.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param addr      Peer address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_announce(wingo_dht_storage_t *storage,
                                          const wingo_info_hash_t *infohash,
                                          const wingo_addr_t *addr);

/*
 * Announce a peer with authentication token.
 *
 * The token is verified by the security layer.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param addr      Peer address
 * @param token     Authentication token
 * @param token_len Token length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_announce_token(wingo_dht_storage_t *storage,
                                                const wingo_info_hash_t *infohash,
                                                const wingo_addr_t *addr,
                                                const wingo_u8 *token,
                                                wingo_size token_len);

/*
 * Remove a peer from an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param addr      Peer address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_remove_peer(wingo_dht_storage_t *storage,
                                             const wingo_info_hash_t *infohash,
                                             const wingo_addr_t *addr);

/*
 * Remove an entire info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_remove_hash(wingo_dht_storage_t *storage,
                                             const wingo_info_hash_t *infohash);

/* ============================================================================
 * DHT STORAGE GET PEERS
 * ============================================================================ */

/*
 * Get peers for an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param out       Output array of addresses
 * @param max       Maximum number of addresses
 * @return          Number of addresses written, or 0 if not found
 */
wingo_size wingo_dht_storage_get_peers(wingo_dht_storage_t *storage,
                                        const wingo_info_hash_t *infohash,
                                        wingo_addr_t **out,
                                        wingo_size max);

/*
 * Get peer count for an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @return          Number of peers, or 0 if not found
 */
wingo_size wingo_dht_storage_peer_count(wingo_dht_storage_t *storage,
                                         const wingo_info_hash_t *infohash);

/*
 * Check if an info hash exists.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @return          true if exists, false otherwise
 */
bool wingo_dht_storage_has_hash(const wingo_dht_storage_t *storage,
                                 const wingo_info_hash_t *infohash);

/*
 * Check if a peer exists for an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param addr      Peer address
 * @return          true if exists, false otherwise
 */
bool wingo_dht_storage_has_peer(const wingo_dht_storage_t *storage,
                                 const wingo_info_hash_t *infohash,
                                 const wingo_addr_t *addr);

/* ============================================================================
 * DHT STORAGE ITERATION
 * ============================================================================ */

/*
 * Info hash callback.
 *
 * @param infohash  Info hash
 * @param peer_count Number of peers for this hash
 * @param userdata  User data
 * @return          true to continue, false to stop
 */
typedef bool (*wingo_dht_storage_hash_cb_t)(const wingo_info_hash_t *infohash,
                                             wingo_size peer_count,
                                             void *userdata);

/*
 * Iterate over all info hashes.
 *
 * @param storage   Storage handle
 * @param callback  Callback function
 * @param userdata  User data
 */
void wingo_dht_storage_foreach_hash(wingo_dht_storage_t *storage,
                                     wingo_dht_storage_hash_cb_t callback,
                                     void *userdata);

/*
 * Peer callback.
 *
 * @param addr      Peer address
 * @param userdata  User data
 * @return          true to continue, false to stop
 */
typedef bool (*wingo_dht_storage_peer_cb_t)(const wingo_addr_t *addr,
                                             void *userdata);

/*
 * Iterate over all peers for an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param callback  Callback function
 * @param userdata  User data
 * @return          Number of peers iterated
 */
wingo_size wingo_dht_storage_foreach_peer(wingo_dht_storage_t *storage,
                                           const wingo_info_hash_t *infohash,
                                           wingo_dht_storage_peer_cb_t callback,
                                           void *userdata);

/* ============================================================================
 * DHT STORAGE EXPIRY
 * ============================================================================ */

/*
 * Expire old peers and hashes.
 *
 * Call this periodically (e.g., every 5 minutes).
 *
 * @param storage   Storage handle
 * @return          Number of entries expired
 */
wingo_size wingo_dht_storage_expire(wingo_dht_storage_t *storage);

/*
 * Get time until next expiry check.
 *
 * @param storage   Storage handle
 * @return          Seconds until next check
 */
wingo_i64 wingo_dht_storage_next_expire(const wingo_dht_storage_t *storage);

/*
 * Check if expiry is needed.
 *
 * @param storage   Storage handle
 * @return          true if needed, false otherwise
 */
bool wingo_dht_storage_needs_expire(const wingo_dht_storage_t *storage);

/*
 * Refresh a peer's TTL.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param addr      Peer address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_refresh_peer(wingo_dht_storage_t *storage,
                                              const wingo_info_hash_t *infohash,
                                              const wingo_addr_t *addr);

/* ============================================================================
 * DHT STORAGE STATISTICS
 * ============================================================================ */

/*
 * Storage statistics.
 */
typedef struct {
    wingo_size  hash_count;         /* Total info hashes */
    wingo_size  peer_count;         /* Total peers */
    wingo_size  max_hashes;         /* Max info hashes */
    wingo_size  max_peers_per_hash; /* Max peers per hash */
    wingo_u64   announces;          /* Total announces */
    wingo_u64   lookups;            /* Total lookups */
    wingo_u64   removes;            /* Total removes */
    wingo_u64   expires;            /* Total expires */
    wingo_i64   oldest_hash_age;    /* Age of oldest hash (seconds) */
    wingo_i64   newest_hash_age;    /* Age of newest hash (seconds) */
} wingo_dht_storage_stats_t;

/*
 * Get storage statistics.
 *
 * @param storage   Storage handle
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_get_stats(const wingo_dht_storage_t *storage,
                                           wingo_dht_storage_stats_t *stats);

/*
 * Reset storage statistics.
 *
 * @param storage   Storage handle
 */
void wingo_dht_storage_reset_stats(wingo_dht_storage_t *storage);

/* ============================================================================
 * DHT STORAGE PERSISTENCE
 * ============================================================================ */

/*
 * Save storage to file.
 *
 * @param storage   Storage handle
 * @param path      File path
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_save(const wingo_dht_storage_t *storage,
                                      const char *path);

/*
 * Load storage from file.
 *
 * @param storage   Storage handle
 * @param path      File path
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_storage_load(wingo_dht_storage_t *storage,
                                      const char *path);

/* ============================================================================
 * DHT STORAGE UTILITY
 * ============================================================================ */

/*
 * Get storage result name.
 *
 * @param result    Storage result
 * @return          Static string
 */
const char *wingo_dht_storage_result_name(wingo_dht_storage_result_t result);

/*
 * Print storage status.
 *
 * @param storage   Storage handle
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_storage_print(const wingo_dht_storage_t *storage, FILE *f);

/*
 * Print all info hashes.
 *
 * @param storage   Storage handle
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_storage_print_hashes(const wingo_dht_storage_t *storage,
                                     FILE *f);

/*
 * Print peers for an info hash.
 *
 * @param storage   Storage handle
 * @param infohash  Info hash
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_storage_print_peers(const wingo_dht_storage_t *storage,
                                    const wingo_info_hash_t *infohash,
                                    FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_STORAGE_H */
