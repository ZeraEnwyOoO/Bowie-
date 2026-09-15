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

#ifndef WINGO_NET_DHT_H
#define WINGO_NET_DHT_H

/*
 * ============================================================================
 * WINGO DHT (Distributed Hash Table)
 * ============================================================================
 *
 * This header provides the Mainline DHT (BEP 5) implementation for Bowie.
 *
 * Architecture:
 *
 *  ┌────────────────────────────────────────────────────────────┐
 *  │                    DHT ENGINE                              │
 *  │                                                            │
 *  │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *  │   │   Routing   │  │   Search    │  │  Storage    │        │
 *  │   │   Table     │  │   Manager   │  │  Manager    │        │
 *  │   │             │  │             │  │             │        │
│   │   │  - Buckets  │  │  - Lookup   │  │  - Peers    │        │
│   │   │  - Nodes    │  │  - Announce │  │  - Hashes   │        │
│   │   │             │  │             │  │             │        │
│   │   └─────────────┘  └─────────────┘  └─────────────┘        │
│   │                                                            │
│   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
│   │   │  Message    │  │   Token     │  │  Security   │        │
│   │   │   Codec     │  │   System    │  │  Manager    │        │
│   │   │             │  │             │  │             │        │
│   │   │  - Bencode  │  │  - Make     │  │  - Black    │        │
│   │   │  - Parse    │  │  - Verify   │  │    list     │        │
│   │   │             │  │  - Rotate   │  │  - Check    │        │
│   │   └─────────────┘  └─────────────┘  └─────────────┘        │
│   │                                                            │
│   └────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/peer.h"

/* ============================================================================
 * DHT CONSTANTS
 * ============================================================================ */

/*
 * DHT ID size (160-bit).
 */
#define WINGO_DHT_ID_SIZE       20

/*
 * DHT ID hex size.
 */
#define WINGO_DHT_ID_HEX_SIZE   41

/*
 * DHT token size.
 */
#define WINGO_DHT_TOKEN_SIZE    8

/*
 * Maximum bootstrap nodes.
 */
#define WINGO_DHT_MAX_BOOTSTRAP 8

/*
 * Default DHT port.
 */
#define WINGO_DHT_DEFAULT_PORT  6881

/* ============================================================================
 * DHT TYPES
 * ============================================================================ */

/*
 * DHT ID (160-bit).
 */
typedef struct {
    wingo_u8 bytes[WINGO_DHT_ID_SIZE];
} wingo_dht_id_t;

/*
 * DHT Info Hash (160-bit).
 */
typedef wingo_dht_id_t wingo_info_hash_t;

/*
 * DHT token (8 bytes).
 */
typedef struct {
    wingo_u8 bytes[WINGO_DHT_TOKEN_SIZE];
} wingo_dht_token_t;

/*
 * DHT state.
 */
typedef enum {
    WINGO_DHT_STATE_STOPPED  = 0,
    WINGO_DHT_STATE_STARTING = 1,
    WINGO_DHT_STATE_RUNNING  = 2,
    WINGO_DHT_STATE_STOPPING = 3,
    WINGO_DHT_STATE_ERROR    = 4,
} wingo_dht_state_t;

/* ============================================================================
 * DHT STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT handle.
 */
typedef struct wingo_dht wingo_dht_t;

/* ============================================================================
 * DHT CONFIGURATION
 * ============================================================================ */

/*
 * DHT configuration.
 */
typedef struct {
    /* Identity */
    wingo_dht_id_t      node_id;            /* Node ID (0 = random) */
    wingo_u16           port;               /* Listen port (0 = default) */

    /* Network */
    wingo_addr_family_t family;             /* IPv4, IPv6, or UNSPEC */
    bool                enable_ipv6;        /* Enable IPv6 */

    /* Bootstrap */
    const char         *bootstrap[WINGO_DHT_MAX_BOOTSTRAP];
    wingo_size          num_bootstrap;

    /* Limits */
    wingo_size          max_nodes;          /* Max nodes in routing table */
    wingo_size          max_peers;          /* Max peers in storage */
    wingo_size          max_hashes;         /* Max info hashes */
    wingo_size          max_searches;       /* Max concurrent searches */

    /* Timeouts */
    wingo_i64           search_timeout;     /* Search timeout (seconds) */
    wingo_i64           node_timeout;       /* Node timeout (seconds) */
    wingo_i64           storage_timeout;    /* Storage timeout (seconds) */

    /* Security */
    bool                enable_blacklist;   /* Enable blacklist */
    bool                enable_rate_limit;  /* Enable rate limiting */
} wingo_dht_config_t;

/*
 * Get default DHT configuration.
 *
 * @param config    Output configuration
 */
void wingo_dht_config_default(wingo_dht_config_t *config);

/* ============================================================================
 * DHT LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT instance.
 *
 * @param config    Configuration (NULL = default)
 * @return          DHT instance, or NULL on error
 */
wingo_dht_t *wingo_dht_new(const wingo_dht_config_t *config);

/*
 * Start the DHT.
 *
 * @param dht       DHT instance
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_start(wingo_dht_t *dht);

/*
 * Stop the DHT.
 *
 * @param dht       DHT instance
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_stop(wingo_dht_t *dht);

/*
 * Free a DHT instance.
 *
 * @param dht       DHT instance (NULL is safe)
 */
void wingo_dht_free(wingo_dht_t *dht);

/*
 * Get DHT state.
 *
 * @param dht       DHT instance
 * @return          DHT state
 */
wingo_dht_state_t wingo_dht_state(const wingo_dht_t *dht);

/*
 * Check if DHT is running.
 *
 * @param dht       DHT instance
 * @return          true if running, false otherwise
 */
bool wingo_dht_is_running(const wingo_dht_t *dht);

/* ============================================================================
 * DHT IDENTITY
 * ============================================================================ */

/*
 * Get DHT node ID.
 *
 * @param dht       DHT instance
 * @param id        Output ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_get_id(const wingo_dht_t *dht, wingo_dht_id_t *id);

/*
 * Generate a random DHT ID.
 *
 * @param id        Output ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_random_id(wingo_dht_id_t *id);

/*
 * Convert DHT ID to hex string.
 *
 * @param id        DHT ID
 * @param buf       Output buffer (at least WINGO_DHT_ID_HEX_SIZE)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_id_to_hex(const wingo_dht_id_t *id,
                                   char *buf);

/*
 * Compare two DHT IDs.
 *
 * @param a         First ID
 * @param b         Second ID
 * @return          0 if equal, <0 if a<b, >0 if a>b
 */
int wingo_dht_id_cmp(const wingo_dht_id_t *a, const wingo_dht_id_t *b);

/*
 * Check if DHT ID is zero.
 *
 * @param id        DHT ID
 * @return          true if zero, false otherwise
 */
bool wingo_dht_id_is_zero(const wingo_dht_id_t *id);

/*
 * Calculate XOR distance between two DHT IDs.
 *
 * @param a         First ID
 * @param b         Second ID
 * @param out       Output distance
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_id_distance(const wingo_dht_id_t *a,
                                     const wingo_dht_id_t *b,
                                     wingo_dht_id_t *out);

/*
 * Calculate common prefix length (bits).
 *
 * @param a         First ID
 * @param b         Second ID
 * @return          Common prefix length (0-160)
 */
int wingo_dht_id_common_bits(const wingo_dht_id_t *a,
                              const wingo_dht_id_t *b);

/* ============================================================================
 * DHT BOOTSTRAP
 * ============================================================================ */

/*
 * Bootstrap the DHT.
 *
 * @param dht       DHT instance
 * @param host      Bootstrap host
 * @param port      Bootstrap port
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bootstrap(wingo_dht_t *dht,
                                   const char *host,
                                   wingo_u16 port);

/*
 * Bootstrap the DHT with default nodes.
 *
 * @param dht       DHT instance
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_bootstrap_default(wingo_dht_t *dht);

/*
 * Add a bootstrap node.
 *
 * @param dht       DHT instance
 * @param host      Bootstrap host
 * @param port      Bootstrap port
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_add_bootstrap(wingo_dht_t *dht,
                                       const char *host,
                                       wingo_u16 port);

/* ============================================================================
 * DHT ANNOUNCE
 * ============================================================================ */

/*
 * Announce a peer to the DHT.
 *
 * @param dht       DHT instance
 * @param infohash  Info hash
 * @param port      Peer port
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_announce(wingo_dht_t *dht,
                                  const wingo_info_hash_t *infohash,
                                  wingo_u16 port);

/*
 * Announce a peer with callback.
 *
 * @param dht       DHT instance
 * @param infohash  Info hash
 * @param port      Peer port
 * @param callback  Callback when done
 * @param userdata  User data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_announce_async(wingo_dht_t *dht,
                                        const wingo_info_hash_t *infohash,
                                        wingo_u16 port,
                                        void (*callback)(int result, void *userdata),
                                        void *userdata);

/* ============================================================================
 * DHT GET PEERS
 * ============================================================================ */

/*
 * Peer callback.
 *
 * @param peer      Peer address
 * @param userdata  User data
 */
typedef void (*wingo_dht_peer_cb_t)(const wingo_addr_t *addr, void *userdata);

/*
 * Search for peers.
 *
 * @param dht       DHT instance
 * @param infohash  Info hash
 * @param callback  Peer callback
 * @param userdata  User data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_get_peers(wingo_dht_t *dht,
                                   const wingo_info_hash_t *infohash,
                                   wingo_dht_peer_cb_t callback,
                                   void *userdata);

/*
 * Search for peers (async).
 *
 * @param dht       DHT instance
 * @param infohash  Info hash
 * @param callback  Peer callback
 * @param done      Done callback
 * @param userdata  User data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_get_peers_async(wingo_dht_t *dht,
                                         const wingo_info_hash_t *infohash,
                                         wingo_dht_peer_cb_t callback,
                                         void (*done)(int result, void *userdata),
                                         void *userdata);

/* ============================================================================
 * DHT FIND NODE
 * ============================================================================ */

/*
 * Find nodes close to a target ID.
 *
 * @param dht       DHT instance
 * @param target    Target ID
 * @param callback  Peer callback
 * @param userdata  User data
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_find_node(wingo_dht_t *dht,
                                   const wingo_dht_id_t *target,
                                   wingo_dht_peer_cb_t callback,
                                   void *userdata);

/* ============================================================================
 * DHT PING
 * ============================================================================ */

/*
 * Ping a node.
 *
 * @param dht       DHT instance
 * @param addr      Node address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_ping(wingo_dht_t *dht, const wingo_addr_t *addr);

/* ============================================================================
 * DHT INFO HASH
 * ============================================================================ */

/*
 * Create a Bowie info hash from a string.
 *
 * @param name      Name (e.g., "bowie:abc123")
 * @param infohash  Output info hash
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_make_infohash(const char *name,
                                       wingo_info_hash_t *infohash);

/*
 * Create a Bowie info hash from a peer ID.
 *
 * @param id        Peer ID
 * @param infohash  Output info hash
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_make_infohash_from_id(const wingo_id *id,
                                               wingo_info_hash_t *infohash);

/*
 * Check if an info hash is a Bowie info hash.
 *
 * @param infohash  Info hash
 * @return          true if Bowie, false otherwise
 */
bool wingo_dht_is_bowie_infohash(const wingo_info_hash_t *infohash);

/* ============================================================================
 * DHT STATISTICS
 * ============================================================================ */

/*
 * DHT statistics.
 */
typedef struct {
    wingo_u64   nodes_good;         /* Good nodes */
    wingo_u64   nodes_dubious;      /* Dubious nodes */
    wingo_u64   nodes_cached;       /* Cached nodes */
    wingo_u64   nodes_incoming;     /* Incoming nodes */
    wingo_u64   searches_active;    /* Active searches */
    wingo_u64   searches_done;      /* Completed searches */
    wingo_u64   storage_hashes;     /* Stored info hashes */
    wingo_u64   storage_peers;      /* Stored peers */
    wingo_u64   messages_sent;      /* Messages sent */
    wingo_u64   messages_received;  /* Messages received */
    wingo_u64   messages_dropped;   /* Dropped messages */
    wingo_u64   bytes_sent;         /* Bytes sent */
    wingo_u64   bytes_received;     /* Bytes received */
    wingo_i64   uptime;             /* Uptime (seconds) */
} wingo_dht_stats_t;

/*
 * Get DHT statistics.
 *
 * @param dht       DHT instance
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_get_stats(const wingo_dht_t *dht,
                                   wingo_dht_stats_t *stats);

/*
 * Reset DHT statistics.
 *
 * @param dht       DHT instance
 */
void wingo_dht_reset_stats(wingo_dht_t *dht);

/* ============================================================================
 * DHT PERIODIC
 * ============================================================================ */

/*
 * Process DHT events (call periodically).
 *
 * @param dht       DHT instance
 * @param timeout_ms Timeout in milliseconds
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_periodic(wingo_dht_t *dht, wingo_i64 timeout_ms);

/*
 * Get next timeout.
 *
 * @param dht       DHT instance
 * @return          Timeout in milliseconds
 */
wingo_i64 wingo_dht_next_timeout(const wingo_dht_t *dht);

/* ============================================================================
 * DHT UTILITY
 * ============================================================================ */

/*
 * Get DHT state name.
 *
 * @param state     DHT state
 * @return          Static string
 */
const char *wingo_dht_state_name(wingo_dht_state_t state);

/*
 * Print DHT status.
 *
 * @param dht       DHT instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_print(const wingo_dht_t *dht, FILE *f);

/*
 * Dump DHT routing table.
 *
 * @param dht       DHT instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_dump_routing(const wingo_dht_t *dht, FILE *f);

/*
 * Dump DHT storage.
 *
 * @param dht       DHT instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_dump_storage(const wingo_dht_t *dht, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_H */
