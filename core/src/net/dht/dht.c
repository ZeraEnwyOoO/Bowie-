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
 * Main DHT implementation for Bowie.
 *
 * This file ties together:
 *   - Socket I/O (UDP)
 *   - Routing table
 *   - Storage
 *   - Search manager
 *   - Security
 *   - Message codec
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT ENGINE                               │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │   Socket    │  │   Routing   │  │   Storage   │        │
 *   │   │   (UDP)     │  │   Table     │  │             │        │
 *   │   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
 *   │          │                │                │                │
 *   │          └────────────────┼────────────────┘                │
 *   │                           │                                 │
 *   │                    ┌──────▼──────┐                          │
 *   │                    │   DHT       │                          │
 *   │                    │   CORE      │                          │
 *   │                    └──────┬──────┘                          │
 *   │                           │                                 │
 *   │          ┌────────────────┼────────────────┐                │
 *   │          │                │                │                │
 *   │   ┌──────▼──────┐  ┌──────▼──────┐  ┌──────▼──────┐        │
 *   │   │   Search    │  │  Security   │  │  Messages   │        │
 *   │   │   Manager   │  │             │  │             │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/net/dht.h"
#include "wingo/net/dht/dht_types.h"
#include "wingo/net/dht/dht_config.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/net/dht/dht_bucket.h"
#include "wingo/net/dht/dht_routing.h"
#include "wingo/net/dht/dht_token.h"
#include "wingo/net/dht/dht_security.h"
#include "wingo/net/dht/dht_storage.h"
#include "wingo/net/dht/dht_message.h"
#include "wingo/net/dht/dht_search.h"
#include "wingo/net/dht/dht_bencode.h"
#include "wingo/net/socket.h"
#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

#define DHT_RECV_BUFFER_SIZE        4096
#define DHT_PERIODIC_INTERVAL       60
#define DHT_BOOTSTRAP_INTERVAL      300
#define DHT_MAINTENANCE_INTERVAL    300

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

struct wingo_dht {
    /* ----- Configuration ----- */
    wingo_dht_config_t      config;

    /* ----- State ----- */
    wingo_dht_state_t       state;
    wingo_dht_id_t          my_id;
    wingo_u16               port;

    /* ----- Socket ----- */
    wingo_sock_t           *sock;
    wingo_event_t          *sock_event;
    wingo_u8                recv_buf[DHT_RECV_BUFFER_SIZE];

    /* ----- Subsystems ----- */
    wingo_dht_routing_t    *routing;
    wingo_dht_storage_t    *storage;
    wingo_dht_security_t   *security;
    wingo_dht_search_mgr_t *searches;

    /* ----- Bootstrap ----- */
    wingo_size              bootstrap_count;
    wingo_i64               last_bootstrap;

    /* ----- Timing ----- */
    wingo_i64               started_at;
    wingo_i64               last_periodic;
    wingo_i64               next_periodic;

    /* ----- Statistics ----- */
    wingo_dht_stats_t       stats;

    /* ----- Callbacks ----- */
    wingo_dht_peer_cb_t     peer_cb;
    void                   *peer_userdata;
};

/* ============================================================================
 * INTERNAL HELPERS — ID
 * ============================================================================ */

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

static void id_xor(const wingo_dht_id_t *a,
                    const wingo_dht_id_t *b,
                    wingo_dht_id_t *out)
{
    wingo_size i;
    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        out->bytes[i] = a->bytes[i] ^ b->bytes[i];
    }
}

static int id_cmp(const wingo_dht_id_t *a, const wingo_dht_id_t *b)
{
    return memcmp(a->bytes, b->bytes, WINGO_DHT_ID_SIZE);
}

/* ============================================================================
 * INTERNAL HELPERS — SOCKET CALLBACK
 * ============================================================================ */

static void dht_process_message(wingo_dht_t *dht,
                                 const wingo_u8 *data,
                                 wingo_size len,
                                 const wingo_addr_t *from);

static void dht_socket_callback(wingo_event_t *event,
                                 int fd,
                                 wingo_u32 flags,
                                 void *userdata)
{
    wingo_dht_t *dht = (wingo_dht_t *)userdata;
    wingo_addr_t *from = NULL;
    wingo_ssize n;

    (void)event;
    (void)fd;

    if (dht == NULL) {
        return;
    }

    if (!(flags & WINGO_EVENT_READ)) {
        return;
    }

    n = wingo_sock_recvfrom(dht->sock, dht->recv_buf,
                             sizeof(dht->recv_buf), &from);

    if (n <= 0) {
        if (from != NULL) {
            wingo_addr_free(from);
        }
        return;
    }

    dht_process_message(dht, dht->recv_buf, (wingo_size)n, from);

    if (from != NULL) {
        wingo_addr_free(from);
    }
}

/* ============================================================================
 * DHT LIFECYCLE
 * ============================================================================ */

/*
 * Get default DHT configuration.
 *
 * NOTE: This forwards to dht_config.c.
 */
void wingo_dht_config_default(wingo_dht_config_t *config)
{
    extern void wingo_dht_config_default(wingo_dht_config_t *config);
    wingo_dht_config_default(config);
}

wingo_dht_t *wingo_dht_new(const wingo_dht_config_t *config)
{
    wingo_dht_t *dht;
    wingo_dht_config_t default_config;
    wingo_dht_config_t *cfg;

    dht = calloc(1, sizeof(wingo_dht_t));
    if (dht == NULL) {
        return NULL;
    }

    if (config != NULL) {
        cfg = (wingo_dht_config_t *)config;
    } else {
        wingo_dht_config_default(&default_config);
        cfg = &default_config;
    }

    memcpy(&dht->config, cfg, sizeof(wingo_dht_config_t));

    /* Apply defaults for zero values */
    if (dht->config.port == 0) {
        dht->config.port = WINGO_DHT_DEFAULT_PORT;
    }
    if (dht->config.max_nodes == 0) {
        dht->config.max_nodes = WINGO_DHT_CONFIG_DEFAULT_MAX_NODES;
    }
    if (dht->config.max_peers == 0) {
        dht->config.max_peers = WINGO_DHT_CONFIG_DEFAULT_MAX_PEERS;
    }
    if (dht->config.max_hashes == 0) {
        dht->config.max_hashes = WINGO_DHT_CONFIG_DEFAULT_MAX_HASHES;
    }
    if (dht->config.max_searches == 0) {
        dht->config.max_searches = WINGO_DHT_CONFIG_DEFAULT_MAX_SEARCHES;
    }
    if (dht->config.bucket_size == 0) {
        dht->config.bucket_size = WINGO_DHT_CONFIG_DEFAULT_BUCKET_SIZE;
    }
    if (dht->config.search_timeout == 0) {
        dht->config.search_timeout = WINGO_DHT_CONFIG_DEFAULT_SEARCH_TIMEOUT;
    }
    if (dht->config.node_timeout == 0) {
        dht->config.node_timeout = WINGO_DHT_CONFIG_DEFAULT_NODE_TIMEOUT;
    }
    if (dht->config.storage_timeout == 0) {
        dht->config.storage_timeout = WINGO_DHT_CONFIG_DEFAULT_STORAGE_TIMEOUT;
    }
    if (dht->config.token_size == 0) {
        dht->config.token_size = WINGO_DHT_CONFIG_DEFAULT_TOKEN_SIZE;
    }
    if (dht->config.token_rotate_min == 0) {
        dht->config.token_rotate_min = WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MIN;
    }
    if (dht->config.token_rotate_max == 0) {
        dht->config.token_rotate_max = WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MAX;
    }
    if (dht->config.rate_limit == 0) {
        dht->config.rate_limit = WINGO_DHT_CONFIG_DEFAULT_RATE_LIMIT;
    }
    if (dht->config.blacklist_size == 0) {
        dht->config.blacklist_size = WINGO_DHT_CONFIG_DEFAULT_BLACKLIST_SIZE;
    }

    /* Generate node ID if not set */
    if (wingo_dht_id_is_zero(&dht->config.node_id)) {
        wingo_dht_random_id(&dht->my_id);
    } else {
        memcpy(&dht->my_id, &dht->config.node_id, sizeof(wingo_dht_id_t));
    }

    dht->port = dht->config.port;
    dht->state = WINGO_DHT_STATE_STOPPED;

    dht->sock = NULL;
    dht->sock_event = NULL;

    dht->routing = NULL;
    dht->storage = NULL;
    dht->security = NULL;
    dht->searches = NULL;

    dht->bootstrap_count = 0;
    dht->last_bootstrap = 0;

    dht->started_at = 0;
    dht->last_periodic = 0;
    dht->next_periodic = 0;

    memset(&dht->stats, 0, sizeof(dht->stats));

    dht->peer_cb = NULL;
    dht->peer_userdata = NULL;

    {
        char id_hex[WINGO_DHT_ID_HEX_SIZE];
        id_to_hex_local(&dht->my_id, id_hex);
        WINGO_LOG_DEBUG("DHT created (id=%s, port=%u)", id_hex, dht->port);
    }

    return dht;
}

wingo_error_t wingo_dht_start(wingo_dht_t *dht)
{
    wingo_error_t rc;

    if (dht == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (dht->state == WINGO_DHT_STATE_RUNNING) {
        return WINGO_SUCCESS;
    }

    dht->state = WINGO_DHT_STATE_STARTING;

    WINGO_LOG_INFO("DHT starting...");

    /* ----- Create routing table ----- */
    dht->routing = wingo_dht_routing_new(&dht->my_id,
                                          dht->config.family,
                                          dht->config.bucket_size);
    if (dht->routing == NULL) {
        WINGO_LOG_ERROR("DHT: failed to create routing table");
        dht->state = WINGO_DHT_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* ----- Create storage ----- */
    dht->storage = wingo_dht_storage_new(dht->config.max_hashes,
                                          dht->config.max_peers,
                                          dht->config.storage_timeout,
                                          dht->config.storage_timeout * 2);
    if (dht->storage == NULL) {
        WINGO_LOG_ERROR("DHT: failed to create storage");
        wingo_dht_routing_free(dht->routing);
        dht->routing = NULL;
        dht->state = WINGO_DHT_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* ----- Create security ----- */
    dht->security = wingo_dht_security_new(dht->config.blacklist_size,
                                            dht->config.enable_rate_limit,
                                            true);
    if (dht->security == NULL) {
        WINGO_LOG_ERROR("DHT: failed to create security");
        wingo_dht_storage_free(dht->storage);
        wingo_dht_routing_free(dht->routing);
        dht->storage = NULL;
        dht->routing = NULL;
        dht->state = WINGO_DHT_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* ----- Create search manager ----- */
    dht->searches = wingo_dht_search_mgr_new(dht->config.max_searches);
    if (dht->searches == NULL) {
        WINGO_LOG_ERROR("DHT: failed to create search manager");
        wingo_dht_security_free(dht->security);
        wingo_dht_storage_free(dht->storage);
        wingo_dht_routing_free(dht->routing);
        dht->searches = NULL;
        dht->security = NULL;
        dht->storage = NULL;
        dht->routing = NULL;
        dht->state = WINGO_DHT_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* ----- Create socket ----- */
    dht->sock = wingo_sock_new_udp(dht->config.family);
    if (dht->sock == NULL) {
        WINGO_LOG_ERROR("DHT: failed to create UDP socket");
        wingo_dht_search_mgr_free(dht->searches);
        wingo_dht_security_free(dht->security);
        wingo_dht_storage_free(dht->storage);
        wingo_dht_routing_free(dht->routing);
        dht->sock = NULL;
        dht->searches = NULL;
        dht->security = NULL;
        dht->storage = NULL;
        dht->routing = NULL;
        dht->state = WINGO_DHT_STATE_ERROR;
        return WINGO_ERR_NOMEM;
    }

    /* ----- Configure socket ----- */
    wingo_sock_set_reuseaddr(dht->sock, true);
    wingo_sock_set_blocking(dht->sock, false);
    wingo_sock_set_rcvbuf(dht->sock, 1024 * 1024);

    /* ----- Bind socket ----- */
    rc = wingo_sock_bind_any(dht->sock, dht->port);
    if (rc != WINGO_SUCCESS) {
        WINGO_LOG_ERROR("DHT: failed to bind socket: %s",
                        wingo_error_str(rc));
        wingo_sock_free(dht->sock);
        wingo_dht_search_mgr_free(dht->searches);
        wingo_dht_security_free(dht->security);
        wingo_dht_storage_free(dht->storage);
        wingo_dht_routing_free(dht->routing);
        dht->sock = NULL;
        dht->searches = NULL;
        dht->security = NULL;
        dht->storage = NULL;
        dht->routing = NULL;
        dht->state = WINGO_DHT_STATE_ERROR;
        return rc;
    }

    /* ----- Get actual bound port (if 0) ----- */
    if (dht->port == 0) {
        wingo_addr_t *local = wingo_sock_local_addr(dht->sock);
        if (local != NULL) {
            dht->port = wingo_addr_port(local);
            wingo_addr_free(local);
        }
    }

    /* ----- Set state ----- */
    dht->started_at = wingo_time_now();
    dht->last_periodic = dht->started_at;
    dht->next_periodic = dht->started_at + DHT_PERIODIC_INTERVAL;
    dht->state = WINGO_DHT_STATE_RUNNING;

    WINGO_LOG_INFO("DHT started (port=%u)", dht->port);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_stop(wingo_dht_t *dht)
{
    if (dht == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (dht->state != WINGO_DHT_STATE_RUNNING) {
        return WINGO_SUCCESS;
    }

    dht->state = WINGO_DHT_STATE_STOPPING;

    WINGO_LOG_INFO("DHT stopping...");

    if (dht->sock != NULL) {
        wingo_sock_free(dht->sock);
        dht->sock = NULL;
    }

    if (dht->searches != NULL) {
        wingo_dht_search_mgr_free(dht->searches);
        dht->searches = NULL;
    }

    if (dht->security != NULL) {
        wingo_dht_security_free(dht->security);
        dht->security = NULL;
    }

    if (dht->storage != NULL) {
        wingo_dht_storage_free(dht->storage);
        dht->storage = NULL;
    }

    if (dht->routing != NULL) {
        wingo_dht_routing_free(dht->routing);
        dht->routing = NULL;
    }

    dht->state = WINGO_DHT_STATE_STOPPED;

    WINGO_LOG_INFO("DHT stopped");

    return WINGO_SUCCESS;
}

void wingo_dht_free(wingo_dht_t *dht)
{
    if (dht == NULL) {
        return;
    }

    if (dht->state == WINGO_DHT_STATE_RUNNING) {
        wingo_dht_stop(dht);
    }

    free(dht);
}

wingo_dht_state_t wingo_dht_state(const wingo_dht_t *dht)
{
    if (dht == NULL) {
        return WINGO_DHT_STATE_ERROR;
    }

    return dht->state;
}

bool wingo_dht_is_running(const wingo_dht_t *dht)
{
    if (dht == NULL) {
        return false;
    }

    return dht->state == WINGO_DHT_STATE_RUNNING;
}
/* ============================================================================
 * DHT IDENTITY
 * ============================================================================ */

wingo_error_t wingo_dht_get_id(const wingo_dht_t *dht, wingo_dht_id_t *id)
{
    if (dht == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(id, &dht->my_id, sizeof(wingo_dht_id_t));

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_random_id(wingo_dht_id_t *id)
{
    if (id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return wingo_random_bytes(id->bytes, WINGO_DHT_ID_SIZE);
}

wingo_error_t wingo_dht_id_to_hex(const wingo_dht_id_t *id, char *buf)
{
    if (id == NULL || buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    id_to_hex_local(id, buf);

    return WINGO_SUCCESS;
}

int wingo_dht_id_cmp(const wingo_dht_id_t *a, const wingo_dht_id_t *b)
{
    if (a == NULL || b == NULL) {
        return 0;
    }

    return id_cmp(a, b);
}

bool wingo_dht_id_is_zero(const wingo_dht_id_t *id)
{
    static const wingo_u8 zero[WINGO_DHT_ID_SIZE] = {0};

    if (id == NULL) {
        return true;
    }

    return memcmp(id->bytes, zero, WINGO_DHT_ID_SIZE) == 0;
}

wingo_error_t wingo_dht_id_distance(const wingo_dht_id_t *a,
                                     const wingo_dht_id_t *b,
                                     wingo_dht_id_t *out)
{
    if (a == NULL || b == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    id_xor(a, b, out);

    return WINGO_SUCCESS;
}

int wingo_dht_id_common_bits(const wingo_dht_id_t *a,
                              const wingo_dht_id_t *b)
{
    wingo_size i;
    int common_bits = 0;

    if (a == NULL || b == NULL) {
        return 0;
    }

    for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
        wingo_u8 x = a->bytes[i] ^ b->bytes[i];

        if (x == 0) {
            common_bits += 8;
            continue;
        }

        while ((x & 0x80) == 0) {
            common_bits++;
            x <<= 1;
        }

        break;
    }

    return common_bits;
}

/* ============================================================================
 * DHT BOOTSTRAP
 * ============================================================================ */

wingo_error_t wingo_dht_bootstrap(wingo_dht_t *dht,
                                   const char *host,
                                   wingo_u16 port)
{
    wingo_addr_t *addr;

    if (dht == NULL || host == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (port == 0) {
        port = WINGO_DHT_DEFAULT_PORT;
    }

    /* Resolve host */
    addr = wingo_addr_new(host, port);
    if (addr == NULL) {
        WINGO_LOG_WARN("DHT bootstrap: cannot resolve '%s'", host);
        return WINGO_ERR_NET_RESOLVE;
    }

    /* Send ping (the response will add the node properly) */
    wingo_dht_ping(dht, addr);

    wingo_addr_free(addr);

    dht->bootstrap_count++;

    WINGO_LOG_DEBUG("DHT bootstrap: pinged %s:%u", host, port);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_bootstrap_default(wingo_dht_t *dht)
{
    static const char *defaults[] = {
        "router.bittorrent.com",
        "router.utorrent.com",
        "dht.transmissionbt.com",
        NULL
    };

    wingo_size i;
    wingo_error_t last_rc = WINGO_ERR_NOT_FOUND;

    if (dht == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    for (i = 0; defaults[i] != NULL; i++) {
        wingo_error_t rc;

        rc = wingo_dht_bootstrap(dht, defaults[i],
                                  WINGO_DHT_DEFAULT_PORT);
        if (rc == WINGO_SUCCESS) {
            last_rc = WINGO_SUCCESS;
        }
    }

    dht->last_bootstrap = wingo_time_now();

    return last_rc;
}

wingo_error_t wingo_dht_add_bootstrap(wingo_dht_t *dht,
                                       const char *host,
                                       wingo_u16 port)
{
    if (dht == NULL || host == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    (void)port;

    if (dht->config.num_bootstrap >= WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Copy into config's static buffer */
    strncpy(dht->config.bootstrap[dht->config.num_bootstrap].host,
            host,
            sizeof(dht->config.bootstrap[0].host) - 1);
    dht->config.bootstrap[dht->config.num_bootstrap].host[
        sizeof(dht->config.bootstrap[0].host) - 1] = '\0';
    dht->config.num_bootstrap++;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT ANNOUNCE
 * ============================================================================ */

wingo_error_t wingo_dht_announce(wingo_dht_t *dht,
                                  const wingo_info_hash_t *infohash,
                                  wingo_u16 port)
{
    wingo_dht_node_t *nodes[WINGO_DHT_SEARCH_INFLIGHT];
    wingo_size node_count;
    wingo_size i;
    wingo_addr_t *local_addr = NULL;

    if (dht == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    if (port == 0) {
        port = dht->port;
    }

    /* Create local address placeholder */
    local_addr = wingo_addr_new_ipv4("0.0.0.0", port);
    if (local_addr == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Store locally */
    {
        wingo_error_t rc = wingo_dht_storage_announce(dht->storage,
                                                       infohash,
                                                       local_addr);
        if (rc != WINGO_SUCCESS) {
            wingo_addr_free(local_addr);
            return rc;
        }
    }

    /* Find closest nodes */
    node_count = wingo_dht_routing_closest_nodes(dht->routing,
                                                  (const wingo_dht_id_t *)infohash,
                                                  nodes,
                                                  WINGO_DHT_SEARCH_INFLIGHT);

    /* Send announce_peer to each node */
    for (i = 0; i < node_count; i++) {
        const wingo_addr_t *addr = wingo_dht_node_addr(nodes[i]);
        wingo_dht_msg_t *msg;
        wingo_u8 tid[4];
        wingo_u8 token[WINGO_DHT_TOKEN_SIZE];

        if (addr == NULL) {
            continue;
        }

        wingo_random_bytes(tid, sizeof(tid));

        wingo_dht_security_token_generate(dht->security, addr,
                                           token, sizeof(token));

        msg = wingo_dht_msg_build_announce_peer(&dht->my_id,
                                                 tid, sizeof(tid),
                                                 infohash,
                                                 port,
                                                 token, sizeof(token));
        if (msg != NULL) {
            wingo_buf_t *buf = wingo_dht_msg_encode_new(msg);

            if (buf != NULL) {
                wingo_sock_sendto(dht->sock, buf->data, buf->len, addr);
                wingo_buf_free(buf);
                dht->stats.messages_sent++;
            }

            wingo_dht_msg_free(msg);
        }
    }

    wingo_addr_free(local_addr);

    WINGO_LOG_DEBUG("DHT announce: sent to %zu nodes", node_count);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_announce_async(wingo_dht_t *dht,
                                        const wingo_info_hash_t *infohash,
                                        wingo_u16 port,
                                        void (*callback)(int result, void *userdata),
                                        void *userdata)
{
    wingo_error_t rc;

    if (dht == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_dht_announce(dht, infohash, port);

    if (callback != NULL) {
        callback(rc == WINGO_SUCCESS ? 0 : -1, userdata);
    }

    return rc;
}

/* ============================================================================
 * DHT GET PEERS
 * ============================================================================ */

wingo_error_t wingo_dht_get_peers(wingo_dht_t *dht,
                                   const wingo_info_hash_t *infohash,
                                   wingo_dht_peer_cb_t callback,
                                   void *userdata)
{
    wingo_dht_search_t *search;

    if (dht == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    /* First, check local storage */
    {
        wingo_addr_t *local_peers[WINGO_DHT_STORAGE_MAX_PEERS];
        wingo_size count;
        wingo_size i;

        count = wingo_dht_storage_get_peers(dht->storage, infohash,
                                             local_peers,
                                             WINGO_DHT_STORAGE_MAX_PEERS);

        for (i = 0; i < count; i++) {
            if (callback != NULL) {
                callback(local_peers[i], userdata);
            }
            wingo_addr_free(local_peers[i]);
        }
    }

    /* Create search */
    search = wingo_dht_search_new(dht->searches,
                                   WINGO_DHT_SEARCH_TYPE_GET_PEERS,
                                   (const wingo_dht_id_t *)infohash,
                                   0);
    if (search == NULL) {
        return WINGO_ERR_NOMEM;
    }

    if (callback != NULL) {
        wingo_dht_search_set_peer_callback(search, callback, userdata);
    }

    /* Seed with closest nodes */
    {
        wingo_dht_node_t *nodes[WINGO_DHT_SEARCH_INFLIGHT];
        wingo_size count;
        wingo_size i;

        count = wingo_dht_routing_closest_nodes(dht->routing,
                                                  (const wingo_dht_id_t *)infohash,
                                                  nodes,
                                                  WINGO_DHT_SEARCH_INFLIGHT);

        for (i = 0; i < count; i++) {
            const wingo_dht_id_t *id = wingo_dht_node_id(nodes[i]);
            const wingo_addr_t *addr = wingo_dht_node_addr(nodes[i]);

            if (id != NULL && addr != NULL) {
                wingo_dht_search_insert_node(search, id, addr,
                                              false, NULL, 0);
            }
        }
    }

    wingo_dht_search_start(search);

    WINGO_LOG_DEBUG("DHT get_peers: search started");

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_get_peers_async(wingo_dht_t *dht,
                                         const wingo_info_hash_t *infohash,
                                         wingo_dht_peer_cb_t callback,
                                         void (*done)(int result, void *userdata),
                                         void *userdata)
{
    wingo_error_t rc;

    if (dht == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_dht_get_peers(dht, infohash, callback, userdata);

    if (rc != WINGO_SUCCESS) {
        if (done != NULL) {
            done(-1, userdata);
        }
        return rc;
    }

    if (done != NULL) {
        done(0, userdata);
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT FIND NODE
 * ============================================================================ */

wingo_error_t wingo_dht_find_node(wingo_dht_t *dht,
                                   const wingo_dht_id_t *target,
                                   wingo_dht_peer_cb_t callback,
                                   void *userdata)
{
    wingo_dht_search_t *search;

    if (dht == NULL || target == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    search = wingo_dht_search_new(dht->searches,
                                   WINGO_DHT_SEARCH_TYPE_FIND_NODE,
                                   target,
                                   0);
    if (search == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Seed search */
    {
        wingo_dht_node_t *nodes[WINGO_DHT_SEARCH_INFLIGHT];
        wingo_size count;
        wingo_size i;

        count = wingo_dht_routing_closest_nodes(dht->routing,
                                                  target,
                                                  nodes,
                                                  WINGO_DHT_SEARCH_INFLIGHT);

        for (i = 0; i < count; i++) {
            const wingo_dht_id_t *id = wingo_dht_node_id(nodes[i]);
            const wingo_addr_t *addr = wingo_dht_node_addr(nodes[i]);

            if (id != NULL && addr != NULL) {
                wingo_dht_search_insert_node(search, id, addr,
                                              false, NULL, 0);
            }
        }
    }

    wingo_dht_search_start(search);

    (void)callback;
    (void)userdata;

    WINGO_LOG_DEBUG("DHT find_node: search started");

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT PING
 * ============================================================================ */

wingo_error_t wingo_dht_ping(wingo_dht_t *dht, const wingo_addr_t *addr)
{
    wingo_dht_msg_t *msg;
    wingo_buf_t *buf;
    wingo_u8 tid[4];
    wingo_ssize n;

    if (dht == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    wingo_random_bytes(tid, sizeof(tid));

    msg = wingo_dht_msg_build_ping(&dht->my_id, tid, sizeof(tid));
    if (msg == NULL) {
        return WINGO_ERR_NOMEM;
    }

    buf = wingo_dht_msg_encode_new(msg);
    wingo_dht_msg_free(msg);

    if (buf == NULL) {
        return WINGO_ERR_NOMEM;
    }

    n = wingo_sock_sendto(dht->sock, buf->data, buf->len, addr);

    dht->stats.messages_sent++;
    dht->stats.bytes_sent += buf->len;

    wingo_buf_free(buf);

    if (n < 0) {
        return WINGO_ERR_NET_SEND;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT INFO HASH
 * ============================================================================ */

/*
 * Simple hash function for info hash creation.
 *
 * NOTE: Real DHT uses SHA1. We expose a simple hash here
 *       because SHA1 is internal to dht_token.c.
 *
 *       TODO: Expose SHA1 publicly and use it here.
 */
static void simple_hash(const void *data, wingo_size len, wingo_u8 out[20])
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    wingo_size i;
    wingo_u8 h[20] = {0};

    /* FNV-1a 160-bit-ish */
    for (i = 0; i < len; i++) {
        wingo_size j;
        for (j = 0; j < 20; j++) {
            h[j] ^= bytes[i];
            h[j] *= (wingo_u8)(31 + j);
        }
    }

    memcpy(out, h, 20);
}

wingo_error_t wingo_dht_make_infohash(const char *name,
                                       wingo_info_hash_t *infohash)
{
    wingo_u8 buf[256];
    wingo_size len;

    if (name == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    len = strlen(name);
    if (len > sizeof(buf) - 7) {
        return WINGO_ERR_OVERFLOW;
    }

    memcpy(buf, "bowie:", 6);
    memcpy(buf + 6, name, len);
    len += 6;

    simple_hash(buf, len, infohash->bytes);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_make_infohash_from_id(const wingo_id *id,
                                               wingo_info_hash_t *infohash)
{
    wingo_u8 buf[64];

    if (id == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(buf, "bowie:", 6);
    memcpy(buf + 6, id->bytes, WINGO_ID_SIZE);

    simple_hash(buf, 6 + WINGO_ID_SIZE, infohash->bytes);

    return WINGO_SUCCESS;
}

bool wingo_dht_is_bowie_infohash(const wingo_info_hash_t *infohash)
{
    if (infohash == NULL) {
        return false;
    }

    return true;
}
/* ============================================================================
 * DHT MESSAGE PROCESSING — HELPERS
 * ============================================================================ */

static wingo_error_t dht_send_msg(wingo_dht_t *dht,
                                   const wingo_dht_msg_t *msg,
                                   const wingo_addr_t *addr)
{
    wingo_buf_t *buf;
    wingo_ssize n;

    if (dht == NULL || msg == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    buf = wingo_dht_msg_encode_new(msg);
    if (buf == NULL) {
        return WINGO_ERR_NOMEM;
    }

    n = wingo_sock_sendto(dht->sock, buf->data, buf->len, addr);

    dht->stats.messages_sent++;
    dht->stats.bytes_sent += buf->len;

    wingo_buf_free(buf);

    if (n < 0) {
        return WINGO_ERR_NET_SEND;
    }

    return WINGO_SUCCESS;
}

static void dht_add_node_from_msg(wingo_dht_t *dht,
                                   const wingo_dht_id_t *id,
                                   const wingo_addr_t *addr,
                                   int confirm)
{
    if (dht == NULL || id == NULL || addr == NULL) {
        return;
    }

    if (wingo_dht_security_is_martian(addr)) {
        dht->stats.messages_dropped++;
        return;
    }

    if (wingo_dht_security_is_blacklisted_addr(dht->security, addr)) {
        dht->stats.messages_dropped++;
        return;
    }

    if (wingo_dht_security_sybil_check(dht->security, addr) !=
        WINGO_DHT_SECURITY_OK) {
        return;
    }

    wingo_dht_routing_add_node(dht->routing, id, addr, confirm);
    wingo_dht_security_sybil_register(dht->security, addr);
}

/* ============================================================================
 * DHT MESSAGE PROCESSING — HANDLERS
 * ============================================================================ */

static void dht_handle_ping(wingo_dht_t *dht,
                             const wingo_dht_msg_t *msg,
                             const wingo_addr_t *from)
{
    wingo_dht_msg_t *response;
    const wingo_u8 *tid;
    wingo_size tid_len;
    wingo_dht_id_t sender_id;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 1);

    if (wingo_dht_msg_tid(msg, &tid, &tid_len) != WINGO_SUCCESS) {
        return;
    }

    response = wingo_dht_msg_build_pong(&dht->my_id, tid, tid_len);
    if (response == NULL) {
        return;
    }

    dht_send_msg(dht, response, from);
    wingo_dht_msg_free(response);

    WINGO_LOG_TRACE("DHT: ping from peer, pong sent");
}

static void dht_handle_find_node(wingo_dht_t *dht,
                                  const wingo_dht_msg_t *msg,
                                  const wingo_addr_t *from)
{
    wingo_dht_msg_t *response;
    wingo_dht_id_t target;
    wingo_dht_node_t *nodes[WINGO_DHT_MSG_MAX_NODES];
    wingo_size count;
    wingo_size i;
    const wingo_u8 *tid;
    wingo_size tid_len;
    wingo_dht_id_t sender_id;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);

    if (wingo_dht_msg_target(msg, &target) != WINGO_SUCCESS) {
        return;
    }

    if (wingo_dht_msg_tid(msg, &tid, &tid_len) != WINGO_SUCCESS) {
        return;
    }

    count = wingo_dht_routing_closest_nodes(dht->routing, &target,
                                              nodes, WINGO_DHT_MSG_MAX_NODES);

    response = wingo_dht_msg_build_nodes(&dht->my_id, tid, tid_len);
    if (response == NULL) {
        return;
    }

    for (i = 0; i < count; i++) {
        const wingo_dht_id_t *node_id = wingo_dht_node_id(nodes[i]);
        const wingo_addr_t *node_addr = wingo_dht_node_addr(nodes[i]);

        if (node_id != NULL && node_addr != NULL) {
            wingo_dht_msg_add_node(response, node_id, node_addr);
        }
    }

    dht_send_msg(dht, response, from);
    wingo_dht_msg_free(response);

    WINGO_LOG_TRACE("DHT: find_node from peer, %zu nodes sent", count);
}

static void dht_handle_get_peers(wingo_dht_t *dht,
                                  const wingo_dht_msg_t *msg,
                                  const wingo_addr_t *from)
{
    wingo_dht_msg_t *response;
    wingo_info_hash_t infohash;
    wingo_addr_t *peers[WINGO_DHT_MSG_MAX_VALUES];
    wingo_size peer_count;
    wingo_dht_node_t *nodes[WINGO_DHT_MSG_MAX_NODES];
    wingo_size node_count;
    wingo_size i;
    const wingo_u8 *tid;
    wingo_size tid_len;
    wingo_dht_id_t sender_id;
    wingo_u8 token[WINGO_DHT_TOKEN_SIZE];

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);

    if (wingo_dht_msg_info_hash(msg, &infohash) != WINGO_SUCCESS) {
        return;
    }

    if (wingo_dht_msg_tid(msg, &tid, &tid_len) != WINGO_SUCCESS) {
        return;
    }

    wingo_dht_security_token_generate(dht->security, from,
                                       token, sizeof(token));

    response = wingo_dht_msg_build_peers(&dht->my_id, tid, tid_len,
                                          token, sizeof(token));
    if (response == NULL) {
        return;
    }

    peer_count = wingo_dht_storage_get_peers(dht->storage, &infohash,
                                               peers,
                                               WINGO_DHT_MSG_MAX_VALUES);

    if (peer_count > 0) {
        for (i = 0; i < peer_count; i++) {
            wingo_dht_msg_add_value(response, peers[i]);
            wingo_addr_free(peers[i]);
        }
    } else {
        node_count = wingo_dht_routing_closest_nodes(dht->routing,
                                                       (const wingo_dht_id_t *)&infohash,
                                                       nodes,
                                                       WINGO_DHT_MSG_MAX_NODES);

        for (i = 0; i < node_count; i++) {
            const wingo_dht_id_t *node_id = wingo_dht_node_id(nodes[i]);
            const wingo_addr_t *node_addr = wingo_dht_node_addr(nodes[i]);

            if (node_id != NULL && node_addr != NULL) {
                wingo_dht_msg_add_node(response, node_id, node_addr);
            }
        }
    }

    dht_send_msg(dht, response, from);
    wingo_dht_msg_free(response);

    WINGO_LOG_TRACE("DHT: get_peers from peer, %zu peers sent", peer_count);
}

static void dht_handle_announce_peer(wingo_dht_t *dht,
                                      const wingo_dht_msg_t *msg,
                                      const wingo_addr_t *from)
{
    wingo_info_hash_t infohash;
    const wingo_u8 *token;
    wingo_size token_len;
    wingo_u16 port;
    wingo_dht_id_t sender_id;
    wingo_addr_t *peer_addr;
    wingo_dht_msg_t *response;
    const wingo_u8 *tid;
    wingo_size tid_len;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);

    if (wingo_dht_msg_info_hash(msg, &infohash) != WINGO_SUCCESS) {
        return;
    }

    if (wingo_dht_msg_token(msg, &token, &token_len) != WINGO_SUCCESS) {
        return;
    }

    if (!wingo_dht_security_token_verify(dht->security, from,
                                          token, token_len)) {
        WINGO_LOG_DEBUG("DHT: announce_peer with bad token");

        if (wingo_dht_msg_tid(msg, &tid, &tid_len) == WINGO_SUCCESS) {
            response = wingo_dht_msg_build_error(tid, tid_len,
                                                   WINGO_DHT_ERROR_PROTOCOL,
                                                   "Bad token");
            if (response != NULL) {
                dht_send_msg(dht, response, from);
                wingo_dht_msg_free(response);
            }
        }
        return;
    }

    port = wingo_dht_msg_port(msg);

    if (port == 0) {
        port = wingo_addr_port(from);
    }

    {
        char ip_str[WINGO_ADDR_STR_MAX];

        if (wingo_addr_ip_str(from, ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
            return;
        }

        if (wingo_addr_is_ipv6(from)) {
            peer_addr = wingo_addr_new_ipv6(ip_str, port);
        } else {
            peer_addr = wingo_addr_new_ipv4(ip_str, port);
        }
    }

    if (peer_addr == NULL) {
        return;
    }

    wingo_dht_storage_announce(dht->storage, &infohash, peer_addr);
    wingo_addr_free(peer_addr);

    if (wingo_dht_msg_tid(msg, &tid, &tid_len) == WINGO_SUCCESS) {
        response = wingo_dht_msg_build_pong(&dht->my_id, tid, tid_len);
        if (response != NULL) {
            dht_send_msg(dht, response, from);
            wingo_dht_msg_free(response);
        }
    }

    WINGO_LOG_TRACE("DHT: announce_peer from peer, stored");
}

static void dht_handle_pong(wingo_dht_t *dht,
                             const wingo_dht_msg_t *msg,
                             const wingo_addr_t *from)
{
    wingo_dht_id_t sender_id;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);
}

static void dht_handle_nodes(wingo_dht_t *dht,
                              const wingo_dht_msg_t *msg,
                              const wingo_addr_t *from)
{
    wingo_dht_node_info_t nodes[WINGO_DHT_MSG_MAX_NODES];
    wingo_size count;
    wingo_size i;
    wingo_dht_id_t sender_id;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);

    count = wingo_dht_msg_nodes(msg, nodes, WINGO_DHT_MSG_MAX_NODES);

    for (i = 0; i < count; i++) {
        if (nodes[i].addr != NULL) {
            dht_add_node_from_msg(dht, &nodes[i].id, nodes[i].addr, 0);
            wingo_addr_free(nodes[i].addr);
        }
    }
}

static void dht_handle_peers_response(wingo_dht_t *dht,
                                       const wingo_dht_msg_t *msg,
                                       const wingo_addr_t *from)
{
    wingo_addr_t *peers[WINGO_DHT_MSG_MAX_VALUES];
    wingo_dht_node_info_t nodes[WINGO_DHT_MSG_MAX_NODES];
    wingo_size peer_count;
    wingo_size node_count;
    wingo_size i;
    wingo_dht_id_t sender_id;

    if (wingo_dht_msg_id(msg, &sender_id) != WINGO_SUCCESS) {
        return;
    }

    dht_add_node_from_msg(dht, &sender_id, from, 2);

    peer_count = wingo_dht_msg_values(msg, peers, WINGO_DHT_MSG_MAX_VALUES);

    for (i = 0; i < peer_count; i++) {
        if (peers[i] != NULL) {
            if (dht->peer_cb != NULL) {
                dht->peer_cb(peers[i], dht->peer_userdata);
            }
            wingo_addr_free(peers[i]);
        }
    }

    node_count = wingo_dht_msg_nodes(msg, nodes, WINGO_DHT_MSG_MAX_NODES);

    for (i = 0; i < node_count; i++) {
        if (nodes[i].addr != NULL) {
            dht_add_node_from_msg(dht, &nodes[i].id, nodes[i].addr, 0);
            wingo_addr_free(nodes[i].addr);
        }
    }
}

static void dht_handle_error(wingo_dht_t *dht,
                              const wingo_dht_msg_t *msg,
                              const wingo_addr_t *from)
{
    (void)dht;
    (void)from;

    WINGO_LOG_DEBUG("DHT: error from peer: %d %s",
                    wingo_dht_msg_error_code(msg),
                    wingo_dht_msg_error_message(msg));
}

/* ============================================================================
 * DHT MESSAGE PROCESSING — MAIN
 * ============================================================================ */

static void dht_process_message(wingo_dht_t *dht,
                                 const wingo_u8 *data,
                                 wingo_size len,
                                 const wingo_addr_t *from)
{
    wingo_dht_msg_t *msg;
    wingo_dht_msg_type_t type;
    wingo_dht_query_type_t query;

    if (dht == NULL || data == NULL || from == NULL) {
        return;
    }

    dht->stats.messages_received++;
    dht->stats.bytes_received += len;

    if (wingo_dht_security_validate_source(dht->security, from) !=
        WINGO_DHT_SECURITY_OK) {
        dht->stats.messages_dropped++;
        return;
    }

    msg = wingo_dht_msg_decode(data, len);
    if (msg == NULL) {
        dht->stats.messages_dropped++;
        return;
    }

    type = wingo_dht_msg_type(msg);

    switch (type) {
    case WINGO_DHT_MSG_TYPE_QUERY:
        query = wingo_dht_msg_query_type(msg);

        switch (query) {
        case WINGO_DHT_QUERY_PING:
            dht_handle_ping(dht, msg, from);
            break;
        case WINGO_DHT_QUERY_FIND_NODE:
            dht_handle_find_node(dht, msg, from);
            break;
        case WINGO_DHT_QUERY_GET_PEERS:
            dht_handle_get_peers(dht, msg, from);
            break;
        case WINGO_DHT_QUERY_ANNOUNCE_PEER:
            dht_handle_announce_peer(dht, msg, from);
            break;
        default:
            dht->stats.messages_dropped++;
            break;
        }
        break;

    case WINGO_DHT_MSG_TYPE_RESPONSE:
        {
            wingo_dht_id_t sender_id;

            if (wingo_dht_msg_id(msg, &sender_id) == WINGO_SUCCESS) {
                wingo_addr_t *peers[1];
                wingo_dht_node_info_t nodes[1];

                if (wingo_dht_msg_values(msg, peers, 1) > 0) {
                    dht_handle_peers_response(dht, msg, from);
                } else if (wingo_dht_msg_nodes(msg, nodes, 1) > 0) {
                    dht_handle_nodes(dht, msg, from);
                } else {
                    dht_handle_pong(dht, msg, from);
                }
            }
        }
        break;

    case WINGO_DHT_MSG_TYPE_ERROR:
        dht_handle_error(dht, msg, from);
        break;

    default:
        dht->stats.messages_dropped++;
        break;
    }

    wingo_dht_msg_free(msg);
}

/* ============================================================================
 * DHT PERIODIC
 * ============================================================================ */

wingo_error_t wingo_dht_periodic(wingo_dht_t *dht, wingo_i64 timeout_ms)
{
    wingo_i64 now;

    if (dht == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!wingo_dht_is_running(dht)) {
        return WINGO_ERR_INVALID_STATE;
    }

    now = wingo_time_now();

    wingo_dht_search_mgr_step(dht->searches);
    wingo_dht_search_mgr_expire(dht->searches);

    if (now >= dht->next_periodic) {
        wingo_dht_routing_expire(dht->routing);
        wingo_dht_storage_expire(dht->storage);
        wingo_dht_security_blacklist_expire(dht->security);

        if (wingo_dht_security_token_needs_rotate(dht->security)) {
            wingo_dht_security_token_rotate(dht->security);
        }

        wingo_dht_routing_maintenance(dht->routing);

        dht->last_periodic = now;
        dht->next_periodic = now + DHT_PERIODIC_INTERVAL;
    }

    if (dht->bootstrap_count > 0 &&
        now - dht->last_bootstrap >= DHT_BOOTSTRAP_INTERVAL) {
        wingo_dht_bootstrap_default(dht);
    }

    (void)timeout_ms;

    return WINGO_SUCCESS;
}

wingo_i64 wingo_dht_next_timeout(const wingo_dht_t *dht)
{
    wingo_i64 now;
    wingo_i64 next;
    wingo_i64 search_next;

    if (dht == NULL || !wingo_dht_is_running(dht)) {
        return 0;
    }

    now = wingo_time_now();

    next = dht->next_periodic - now;
    if (next < 0) {
        next = 0;
    }

    search_next = wingo_dht_search_mgr_next_step(dht->searches);
    if (search_next > 0) {
        wingo_i64 delta = search_next - now;
        if (delta < 0) {
            delta = 0;
        }
        if (next == 0 || delta < next) {
            next = delta;
        }
    }

    return next * 1000;
}

/* ============================================================================
 * DHT STATISTICS
 * ============================================================================ */

wingo_error_t wingo_dht_get_stats(const wingo_dht_t *dht,
                                   wingo_dht_stats_t *stats)
{
    wingo_dht_search_stats_t search_stats;

    if (dht == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(stats, &dht->stats, sizeof(wingo_dht_stats_t));

    if (dht->routing != NULL) {
        stats->nodes_good = wingo_dht_routing_good_count(dht->routing);
        stats->nodes_dubious = wingo_dht_routing_dubious_count(dht->routing);
        stats->nodes_cached = 0;
    }

    if (dht->storage != NULL) {
        wingo_dht_storage_stats_t storage_stats;
        wingo_dht_storage_get_stats(dht->storage, &storage_stats);
        stats->storage_hashes = storage_stats.hash_count;
        stats->storage_peers = storage_stats.peer_count;
    }

    if (dht->searches != NULL) {
        if (wingo_dht_search_mgr_get_stats(dht->searches,
                                            &search_stats) == WINGO_SUCCESS) {
            stats->searches_active = search_stats.searches_started -
                                     search_stats.searches_done -
                                     search_stats.searches_timeout;
            stats->searches_done = search_stats.searches_done;
        }
    }

    if (dht->started_at > 0) {
        stats->uptime = wingo_time_now() - dht->started_at;
    }

    return WINGO_SUCCESS;
}

void wingo_dht_reset_stats(wingo_dht_t *dht)
{
    if (dht == NULL) {
        return;
    }

    memset(&dht->stats, 0, sizeof(dht->stats));

    if (dht->searches != NULL) {
        wingo_dht_search_mgr_reset_stats(dht->searches);
    }

    if (dht->storage != NULL) {
        wingo_dht_storage_reset_stats(dht->storage);
    }

    if (dht->security != NULL) {
        wingo_dht_security_reset_stats(dht->security);
    }
}

/* ============================================================================
 * DHT UTILITY
 * ============================================================================ */

const char *wingo_dht_state_name(wingo_dht_state_t state)
{
    switch (state) {
    case WINGO_DHT_STATE_STOPPED:  return "STOPPED";
    case WINGO_DHT_STATE_STARTING: return "STARTING";
    case WINGO_DHT_STATE_RUNNING:  return "RUNNING";
    case WINGO_DHT_STATE_STOPPING: return "STOPPING";
    case WINGO_DHT_STATE_ERROR:    return "ERROR";
    default:                       return "UNKNOWN";
    }
}

/*
 * Print DHT status.
 *
 * FIXED (CodeQL Alert #1):
 *   - Format specifier mismatch (%zu with int) is resolved by
 *     using wingo_dht_storage_get_stats() which returns proper
 *     wingo_size (size_t) values.
 *   - Storage section now shows real hash_count / peer_count
 *     instead of incorrectly checking if my_id is a stored hash.
 */
void wingo_dht_print(const wingo_dht_t *dht, FILE *f)
{
    char id_hex[WINGO_DHT_ID_HEX_SIZE];

    if (f == NULL) {
        f = stderr;
    }

    if (dht == NULL) {
        fprintf(f, "DHT: (null)\n");
        return;
    }

    id_to_hex_local(&dht->my_id, id_hex);

    fprintf(f, "DHT Status:\n");
    fprintf(f, "  State:      %s\n", wingo_dht_state_name(dht->state));
    fprintf(f, "  Node ID:    %s\n", id_hex);
    fprintf(f, "  Port:       %u\n", dht->port);
    fprintf(f, "  Started:    %llds ago\n",
            (long long)(dht->started_at > 0
                       ? wingo_time_now() - dht->started_at
                       : 0));
    fprintf(f, "  Bootstrap:  %zu nodes\n", dht->bootstrap_count);
    fprintf(f, "\n");

    /* ----- Routing ----- */
    if (dht->routing != NULL) {
        fprintf(f, "  Routing:\n");
        fprintf(f, "    Buckets:  %zu\n",
                wingo_dht_routing_bucket_count(dht->routing));
        fprintf(f, "    Nodes:    %zu\n",
                wingo_dht_routing_node_count(dht->routing));
        fprintf(f, "    Good:     %zu\n",
                wingo_dht_routing_good_count(dht->routing));
        fprintf(f, "    Dubious:  %zu\n",
                wingo_dht_routing_dubious_count(dht->routing));
        fprintf(f, "    Bad:      %zu\n",
                wingo_dht_routing_bad_count(dht->routing));
    }

    /* ----- Storage (CodeQL FIX) ----- */
    if (dht->storage != NULL) {
        wingo_dht_storage_stats_t storage_stats;

        if (wingo_dht_storage_get_stats(dht->storage, &storage_stats) ==
            WINGO_SUCCESS) {
            fprintf(f, "  Storage:\n");
            fprintf(f, "    Hashes:   %zu\n", storage_stats.hash_count);
            fprintf(f, "    Peers:    %zu\n", storage_stats.peer_count);
            fprintf(f, "    Max hashes: %zu\n", storage_stats.max_hashes);
            fprintf(f, "    Announces: %llu\n",
                    (unsigned long long)storage_stats.announces);
            fprintf(f, "    Lookups:  %llu\n",
                    (unsigned long long)storage_stats.lookups);
        }
    }

    /* ----- Searches ----- */
    if (dht->searches != NULL) {
        fprintf(f, "  Searches:\n");
        fprintf(f, "    Total:    %zu\n",
                wingo_dht_search_mgr_count(dht->searches));
        fprintf(f, "    Active:   %zu\n",
                wingo_dht_search_mgr_active_count(dht->searches));
    }

    /* ----- Statistics ----- */
    fprintf(f, "\n");
    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Messages: %llu sent, %llu received, %llu dropped\n",
            (unsigned long long)dht->stats.messages_sent,
            (unsigned long long)dht->stats.messages_received,
            (unsigned long long)dht->stats.messages_dropped);
    fprintf(f, "    Bytes:    %llu sent, %llu received\n",
            (unsigned long long)dht->stats.bytes_sent,
            (unsigned long long)dht->stats.bytes_received);

    /* ----- Uptime ----- */
    if (dht->started_at > 0) {
        wingo_i64 uptime = wingo_time_now() - dht->started_at;
        fprintf(f, "    Uptime:   %llds\n", (long long)uptime);
    }
}

void wingo_dht_dump_routing(const wingo_dht_t *dht, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (dht == NULL || dht->routing == NULL) {
        fprintf(f, "DHT routing: (null)\n");
        return;
    }

    wingo_dht_routing_print(dht->routing, f);
}

void wingo_dht_dump_storage(const wingo_dht_t *dht, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (dht == NULL || dht->storage == NULL) {
        fprintf(f, "DHT storage: (null)\n");
        return;
    }

    wingo_dht_storage_print(dht->storage, f);
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
