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

#ifndef WINGO_NET_DHT_CONFIG_H
#define WINGO_NET_DHT_CONFIG_H

/*
 * ============================================================================
 * WINGO DHT CONFIG
 * ============================================================================
 *
 * This header provides configuration for Bowie DHT.
 *
 * Default Values:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT CONFIG DEFAULTS                      │
 *   │                                                             │
 *   │   Port:              6881                                   │
 *   │   Family:            IPv4                                   │
 *   │   IPv6:              Disabled                               │
 *   │   Max Nodes:         2048                                   │
 *   │   Max Peers:         2048                                   │
 *   │   Max Hashes:        16384                                  │
 *   │   Max Searches:      1024                                   │
 *   │   Bucket Size:       8                                      │
 *   │   Search Timeout:    62 * 60 = 3720 seconds                 │
 *   │   Node Timeout:      30 * 60 = 1800 seconds                 │
 *   │   Storage Timeout:   32 * 60 = 1920 seconds                 │
 *   │   Token Size:        8 bytes                                │
 *   │   Token Rotate:      900-2700 seconds                       │
 *   │   Rate Limit:        Enabled                                │
 *   │   Blacklist:         Enabled                                │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/dht.h"

/* ============================================================================
 * DHT CONFIG CONSTANTS
 * ============================================================================ */

/*
 * Default DHT port.
 */
#define WINGO_DHT_CONFIG_DEFAULT_PORT           6881

/*
 * Default DHT family.
 */
#define WINGO_DHT_CONFIG_DEFAULT_FAMILY         WINGO_ADDR_UNSPEC

/*
 * Default IPv6 enabled.
 */
#define WINGO_DHT_CONFIG_DEFAULT_IPV6           false

/*
 * Default max nodes.
 */
#define WINGO_DHT_CONFIG_DEFAULT_MAX_NODES      2048

/*
 * Default max peers.
 */
#define WINGO_DHT_CONFIG_DEFAULT_MAX_PEERS      2048

/*
 * Default max hashes.
 */
#define WINGO_DHT_CONFIG_DEFAULT_MAX_HASHES     16384

/*
 * Default max searches.
 */
#define WINGO_DHT_CONFIG_DEFAULT_MAX_SEARCHES   1024

/*
 * Default bucket size.
 */
#define WINGO_DHT_CONFIG_DEFAULT_BUCKET_SIZE    8

/*
 * Default search timeout (62 minutes).
 */
#define WINGO_DHT_CONFIG_DEFAULT_SEARCH_TIMEOUT (62 * 60)

/*
 * Default node timeout (30 minutes).
 */
#define WINGO_DHT_CONFIG_DEFAULT_NODE_TIMEOUT   (30 * 60)

/*
 * Default storage timeout (32 minutes).
 */
#define WINGO_DHT_CONFIG_DEFAULT_STORAGE_TIMEOUT (32 * 60)

/*
 * Default token size.
 */
#define WINGO_DHT_CONFIG_DEFAULT_TOKEN_SIZE     8

/*
 * Default token rotate min (15 minutes).
 */
#define WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MIN (15 * 60)

/*
 * Default token rotate max (45 minutes).
 */
#define WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MAX (45 * 60)

/*
 * Default rate limit (messages per second).
 */
#define WINGO_DHT_CONFIG_DEFAULT_RATE_LIMIT     100

/*
 * Default max bootstrap nodes.
 */
#define WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP  8

/* ============================================================================
 * DHT CONFIG STRUCTURE
 * ============================================================================ */

/*
 * DHT configuration.
 *
 * This structure holds all configuration for a DHT instance.
 */
typedef struct {
    /* ------------------------------------------------------------------------
     * Identity
     * ------------------------------------------------------------------------ */

    /*
     * Node ID.
     * If zero, a random ID will be generated.
     */
    wingo_dht_id_t      node_id;

    /*
     * Listen port.
     * 0 = default (6881)
     */
    wingo_u16           port;

    /* ------------------------------------------------------------------------
     * Network
     * ------------------------------------------------------------------------ */

    /*
     * Address family.
     * WINGO_ADDR_UNSPEC = both IPv4 and IPv6
     */
    wingo_addr_family_t family;

    /*
     * Enable IPv6.
     */
    bool                enable_ipv6;

    /* ------------------------------------------------------------------------
     * Bootstrap
     * ------------------------------------------------------------------------ */

    /*
     * Bootstrap nodes.
     */
    const char         *bootstrap[WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP];

    /*
     * Number of bootstrap nodes.
     */
    wingo_size          num_bootstrap;

    /* ------------------------------------------------------------------------
     * Limits
     * ------------------------------------------------------------------------ */

    /*
     * Maximum number of nodes in routing table.
     * 0 = default (2048)
     */
    wingo_size          max_nodes;

    /*
     * Maximum number of peers in storage.
     * 0 = default (2048)
     */
    wingo_size          max_peers;

    /*
     * Maximum number of info hashes in storage.
     * 0 = default (16384)
     */
    wingo_size          max_hashes;

    /*
     * Maximum number of concurrent searches.
     * 0 = default (1024)
     */
    wingo_size          max_searches;

    /*
     * Bucket size (Kademlia K).
     * 0 = default (8)
     */
    wingo_size          bucket_size;

    /* ------------------------------------------------------------------------
     * Timeouts (in seconds)
     * ------------------------------------------------------------------------ */

    /*
     * Search timeout.
     * 0 = default (3720)
     */
    wingo_i64           search_timeout;

    /*
     * Node timeout.
     * 0 = default (1800)
     */
    wingo_i64           node_timeout;

    /*
     * Storage timeout.
     * 0 = default (1920)
     */
    wingo_i64           storage_timeout;

    /* ------------------------------------------------------------------------
     * Token
     * ------------------------------------------------------------------------ */

    /*
     * Token size.
     * 0 = default (8)
     */
    wingo_size          token_size;

    /*
     * Token rotate minimum interval (seconds).
     * 0 = default (900)
     */
    wingo_i64           token_rotate_min;

    /*
     * Token rotate maximum interval (seconds).
     * 0 = default (2700)
     */
    wingo_i64           token_rotate_max;

    /* ------------------------------------------------------------------------
     * Security
     * ------------------------------------------------------------------------ */

    /*
     * Enable rate limiting.
     */
    bool                enable_rate_limit;

    /*
     * Rate limit (messages per second).
     * 0 = default (100)
     */
    wingo_size          rate_limit;

    /*
     * Enable blacklist.
     */
    bool                enable_blacklist;

    /*
     * Blacklist size.
     * 0 = default (10)
     */
    wingo_size          blacklist_size;

    /* ------------------------------------------------------------------------
     * Debug
     * ------------------------------------------------------------------------ */

    /*
     * Enable debug logging.
     */
    bool                debug;

} wingo_dht_config_t;

/* ============================================================================
 * DHT CONFIG FUNCTIONS
 * ============================================================================ */

/*
 * Get default DHT configuration.
 *
 * @param config    Output configuration
 */
void wingo_dht_config_default(wingo_dht_config_t *config);

/*
 * Validate DHT configuration.
 *
 * @param config    Configuration to validate
 * @return          WINGO_SUCCESS if valid, error code on failure
 */
wingo_error_t wingo_dht_config_validate(const wingo_dht_config_t *config);

/*
 * Copy DHT configuration.
 *
 * @param dst       Destination configuration
 * @param src       Source configuration
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_config_copy(wingo_dht_config_t *dst,
                                     const wingo_dht_config_t *src);

/*
 * Set default bootstrap nodes.
 *
 * @param config    Configuration
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_config_set_default_bootstrap(
    wingo_dht_config_t *config);

/*
 * Add a bootstrap node to configuration.
 *
 * @param config    Configuration
 * @param host      Bootstrap host
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_config_add_bootstrap(wingo_dht_config_t *config,
                                              const char *host);

/*
 * Load DHT configuration from file.
 *
 * @param config    Output configuration
 * @param path      File path
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_config_load(wingo_dht_config_t *config,
                                     const char *path);

/*
 * Save DHT configuration to file.
 *
 * @param config    Configuration
 * @param path      File path
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_config_save(const wingo_dht_config_t *config,
                                     const char *path);

/*
 * Print DHT configuration.
 *
 * @param config    Configuration
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_config_print(const wingo_dht_config_t *config, FILE *f);

/* ============================================================================
 * DHT CONFIG DEFAULT BOOTSTRAP
 * ============================================================================ */

/*
 * Default bootstrap nodes.
 */
#define WINGO_DHT_BOOTSTRAP_NODES \
    "router.bittorrent.com", \
    "router.utorrent.com", \
    "dht.transmissionbt.com", \
    NULL

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_CONFIG_H */
