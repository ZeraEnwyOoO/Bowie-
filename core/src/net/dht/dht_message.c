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
 * DHT message implementation for Bowie.
 *
 * DHT messages are encoded using Bencode (BEP 3).
 *
 * Message formats (BEP 5):
 *
 *   Query:
 *     {
 *       "t": <transaction-id>,
 *       "y": "q",
 *       "q": <query-type>,
 *       "a": <query-arguments>
 *     }
 *
 *   Response:
 *     {
 *       "t": <transaction-id>,
 *       "y": "r",
 *       "r": <response-data>
 *     }
 *
 *   Error:
 *     {
 *       "t": <transaction-id>,
 *       "y": "e",
 *       "e": [<error-code>, <error-message>]
 *     }
 *
 * Query types:
 *   - "ping":           {"id": <node-id>}
 *   - "find_node":      {"id": <node-id>, "target": <target-id>}
 *   - "get_peers":      {"id": <node-id>, "info_hash": <info-hash>}
 *   - "announce_peer":  {"id": <node-id>, "info_hash": <info-hash>,
 *                        "port": <port>, "token": <token>}
 *
 * Response data:
 *   - ping:             {"id": <node-id>}
 *   - find_node:        {"id": <node-id>, "nodes": <compact-nodes>}
 *   - get_peers:        {"id": <node-id>, "token": <token>,
 *                        "values": [<addr>...]} OR
 *                       {"id": <node-id>, "token": <token>,
 *                        "nodes": <compact-nodes>}
 *   - announce_peer:    {"id": <node-id>}
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_message.h"
#include "wingo/net/dht/dht_bencode.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/log.h"
#include "wingo/util/random.h"
#include "wingo/util/time.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Compact node info (26 bytes for IPv4, 38 for IPv6).
 *
 * IPv4: 20-byte ID + 4-byte IP + 2-byte port = 26 bytes
 * IPv6: 20-byte ID + 16-byte IP + 2-byte port = 38 bytes
 */
#define COMPACT_NODE_IPV4_SIZE  26
#define COMPACT_NODE_IPV6_SIZE  38

/*
 * DHT message (concrete).
 */
struct wingo_dht_msg {
    /* ----- Header ----- */
    wingo_dht_msg_type_t     type;
    wingo_dht_query_type_t   query_type;

    wingo_u8                 tid[WINGO_DHT_TID_MAX_SIZE];
    wingo_size               tid_len;

    wingo_dht_id_t           id;      /* Sender node ID */
    bool                     has_id;

    /* ----- Query parameters ----- */
    wingo_dht_id_t           target;
    bool                     has_target;

    wingo_info_hash_t        infohash;
    bool                     has_infohash;

    wingo_u8                 token[WINGO_DHT_MSG_TOKEN_MAX];
    wingo_size               token_len;

    wingo_u16                port;
    bool                     implied_port;

    int                      want;

    /* ----- Response data ----- */
    wingo_dht_node_info_t    nodes[WINGO_DHT_MSG_MAX_NODES];
    wingo_size               num_nodes;

    wingo_addr_t            *values[WINGO_DHT_MSG_MAX_VALUES];
    wingo_size               num_values;

    /* ----- Error ----- */
    int                      error_code;
    char                     error_message[128];
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Generate a random transaction ID.
 */
static void random_tid(wingo_u8 *tid, wingo_size len)
{
    wingo_size i;

    for (i = 0; i < len; i++) {
        tid[i] = (wingo_u8)wingo_random_u8();
    }
}

/*
 * Encode compact node info.
 *
 * Format: <20-byte ID><4-byte IPv4><2-byte port>
 */
static wingo_error_t encode_compact_node_ipv4(const wingo_dht_node_info_t *node,
                                               wingo_u8 *out)
{
    wingo_size offset = 0;
    char ip_str[WINGO_ADDR_STR_MAX];
    unsigned int a, b, c, d;
    wingo_u16 port;

    if (node == NULL || out == NULL || node->addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Copy node ID (20 bytes) */
    memcpy(out + offset, node->id.bytes, WINGO_DHT_ID_SIZE);
    offset += WINGO_DHT_ID_SIZE;

    /* Get IP as string */
    if (wingo_addr_ip_str(node->addr, ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Parse IPv4 */
    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Write IP (4 bytes) */
    out[offset++] = (wingo_u8)a;
    out[offset++] = (wingo_u8)b;
    out[offset++] = (wingo_u8)c;
    out[offset++] = (wingo_u8)d;

    /* Write port (2 bytes, big-endian) */
    port = wingo_addr_port(node->addr);
    out[offset++] = (wingo_u8)(port >> 8);
    out[offset++] = (wingo_u8)(port & 0xFF);

    return WINGO_SUCCESS;
}

/*
 * Decode compact node info.
 */
static wingo_error_t decode_compact_node_ipv4(const wingo_u8 *data,
                                               wingo_dht_node_info_t *node)
{
    char ip_str[WINGO_ADDR_STR_MAX];
    wingo_u16 port;

    if (data == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Node ID (20 bytes) */
    memcpy(node->id.bytes, data, WINGO_DHT_ID_SIZE);

    /* IP (4 bytes) */
    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
             data[20], data[21], data[22], data[23]);

    /* Port (2 bytes, big-endian) */
    port = (wingo_u16)((data[24] << 8) | data[25]);

    /* Create address */
    node->addr = wingo_addr_new_ipv4(ip_str, port);
    if (node->addr == NULL) {
        return WINGO_ERR_NOMEM;
    }

    return WINGO_SUCCESS;
}

/*
 * Encode compact nodes string (multiple nodes).
 */
static wingo_bencode_t *encode_compact_nodes(const wingo_dht_node_info_t *nodes,
                                              wingo_size count)
{
    wingo_u8 buf[WINGO_DHT_MSG_MAX_NODES * COMPACT_NODE_IPV4_SIZE];
    wingo_size offset = 0;
    wingo_size i;

    for (i = 0; i < count; i++) {
        if (nodes[i].addr == NULL) {
            continue;
        }

        if (wingo_addr_is_ipv4(nodes[i].addr)) {
            if (encode_compact_node_ipv4(&nodes[i], buf + offset) == WINGO_SUCCESS) {
                offset += COMPACT_NODE_IPV4_SIZE;
            }
        }
    }

    return wingo_bencode_new_string(buf, offset);
}

/*
 * Decode compact nodes string.
 */
static wingo_size decode_compact_nodes(const void *data,
                                        wingo_size len,
                                        wingo_dht_node_info_t *out,
                                        wingo_size max)
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    wingo_size count = 0;
    wingo_size offset = 0;

    if (data == NULL || out == NULL || max == 0) {
        return 0;
    }

    while (offset + COMPACT_NODE_IPV4_SIZE <= len && count < max) {
        if (decode_compact_node_ipv4(bytes + offset, &out[count]) == WINGO_SUCCESS) {
            count++;
        }
        offset += COMPACT_NODE_IPV4_SIZE;
    }

    return count;
}

/* ============================================================================
 * DHT MESSAGE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT message.
 */
wingo_dht_msg_t *wingo_dht_msg_new(wingo_dht_msg_type_t type)
{
    wingo_dht_msg_t *msg;

    msg = calloc(1, sizeof(wingo_dht_msg_t));
    if (msg == NULL) {
        return NULL;
    }

    msg->type = type;
    msg->query_type = WINGO_DHT_QUERY_UNKNOWN;
    msg->tid_len = 0;
    msg->has_id = false;
    msg->has_target = false;
    msg->has_infohash = false;
    msg->token_len = 0;
    msg->port = 0;
    msg->implied_port = false;
    msg->want = 0;
    msg->num_nodes = 0;
    msg->num_values = 0;
    msg->error_code = 0;
    msg->error_message[0] = '\0';

    return msg;
}

/*
 * Free a DHT message.
 */
void wingo_dht_msg_free(wingo_dht_msg_t *msg)
{
    wingo_size i;

    if (msg == NULL) {
        return;
    }

    /* Free node addresses */
    for (i = 0; i < msg->num_nodes; i++) {
        if (msg->nodes[i].addr != NULL) {
            wingo_addr_free(msg->nodes[i].addr);
        }
    }

    /* Free values */
    for (i = 0; i < msg->num_values; i++) {
        if (msg->values[i] != NULL) {
            wingo_addr_free(msg->values[i]);
        }
    }

    free(msg);
}

/*
 * Clone a DHT message.
 */
wingo_dht_msg_t *wingo_dht_msg_clone(const wingo_dht_msg_t *msg)
{
    wingo_dht_msg_t *copy;
    wingo_size i;

    if (msg == NULL) {
        return NULL;
    }

    copy = calloc(1, sizeof(wingo_dht_msg_t));
    if (copy == NULL) {
        return NULL;
    }

    /* Copy scalar fields */
    copy->type = msg->type;
    copy->query_type = msg->query_type;
    memcpy(copy->tid, msg->tid, sizeof(copy->tid));
    copy->tid_len = msg->tid_len;
    memcpy(&copy->id, &msg->id, sizeof(copy->id));
    copy->has_id = msg->has_id;
    memcpy(&copy->target, &msg->target, sizeof(copy->target));
    copy->has_target = msg->has_target;
    memcpy(&copy->infohash, &msg->infohash, sizeof(copy->infohash));
    copy->has_infohash = msg->has_infohash;
    memcpy(copy->token, msg->token, sizeof(copy->token));
    copy->token_len = msg->token_len;
    copy->port = msg->port;
    copy->implied_port = msg->implied_port;
    copy->want = msg->want;
    copy->error_code = msg->error_code;
    memcpy(copy->error_message, msg->error_message,
           sizeof(copy->error_message));

    /* Copy nodes */
    copy->num_nodes = msg->num_nodes;
    for (i = 0; i < msg->num_nodes; i++) {
        memcpy(&copy->nodes[i].id, &msg->nodes[i].id, sizeof(wingo_dht_id_t));

        if (msg->nodes[i].addr != NULL) {
            copy->nodes[i].addr = wingo_addr_copy(msg->nodes[i].addr);
        } else {
            copy->nodes[i].addr = NULL;
        }
    }

    /* Copy values */
    copy->num_values = msg->num_values;
    for (i = 0; i < msg->num_values; i++) {
        if (msg->values[i] != NULL) {
            copy->values[i] = wingo_addr_copy(msg->values[i]);
        } else {
            copy->values[i] = NULL;
        }
    }

    return copy;
}

/* ============================================================================
 * DHT MESSAGE QUERY
 * ============================================================================ */

/*
 * Get message type.
 */
wingo_dht_msg_type_t wingo_dht_msg_type(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return WINGO_DHT_MSG_TYPE_UNKNOWN;
    }

    return msg->type;
}

/*
 * Get query type.
 */
wingo_dht_query_type_t wingo_dht_msg_query_type(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return WINGO_DHT_QUERY_UNKNOWN;
    }

    return msg->query_type;
}

/*
 * Set query type.
 */
void wingo_dht_msg_set_query_type(wingo_dht_msg_t *msg,
                                   wingo_dht_query_type_t query)
{
    if (msg == NULL) {
        return;
    }

    msg->query_type = query;
}

/*
 * Get transaction ID.
 */
wingo_error_t wingo_dht_msg_tid(const wingo_dht_msg_t *msg,
                                 const wingo_u8 **out,
                                 wingo_size *len)
{
    if (msg == NULL || out == NULL || len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    *out = msg->tid;
    *len = msg->tid_len;

    return WINGO_SUCCESS;
}

/*
 * Set transaction ID.
 */
wingo_error_t wingo_dht_msg_set_tid(wingo_dht_msg_t *msg,
                                     const wingo_u8 *tid,
                                     wingo_size len)
{
    if (msg == NULL || tid == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len > WINGO_DHT_TID_MAX_SIZE) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    memcpy(msg->tid, tid, len);
    msg->tid_len = len;

    return WINGO_SUCCESS;
}

/*
 * Get sender node ID.
 */
wingo_error_t wingo_dht_msg_id(const wingo_dht_msg_t *msg,
                                wingo_dht_id_t *out)
{
    if (msg == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!msg->has_id) {
        return WINGO_ERR_NOT_FOUND;
    }

    memcpy(out, &msg->id, sizeof(wingo_dht_id_t));

    return WINGO_SUCCESS;
}

/*
 * Set sender node ID.
 */
wingo_error_t wingo_dht_msg_set_id(wingo_dht_msg_t *msg,
                                    const wingo_dht_id_t *id)
{
    if (msg == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(&msg->id, id, sizeof(wingo_dht_id_t));
    msg->has_id = true;

    return WINGO_SUCCESS;
}
/* ============================================================================
 * DHT MESSAGE QUERY PARAMETERS
 * ============================================================================ */

/*
 * Get target ID (find_node, get_peers).
 */
wingo_error_t wingo_dht_msg_target(const wingo_dht_msg_t *msg,
                                    wingo_dht_id_t *out)
{
    if (msg == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!msg->has_target) {
        return WINGO_ERR_NOT_FOUND;
    }

    memcpy(out, &msg->target, sizeof(wingo_dht_id_t));

    return WINGO_SUCCESS;
}

/*
 * Set target ID (find_node, get_peers).
 */
wingo_error_t wingo_dht_msg_set_target(wingo_dht_msg_t *msg,
                                        const wingo_dht_id_t *target)
{
    if (msg == NULL || target == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(&msg->target, target, sizeof(wingo_dht_id_t));
    msg->has_target = true;

    return WINGO_SUCCESS;
}

/*
 * Get info hash (get_peers, announce_peer).
 */
wingo_error_t wingo_dht_msg_info_hash(const wingo_dht_msg_t *msg,
                                       wingo_info_hash_t *out)
{
    if (msg == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!msg->has_infohash) {
        return WINGO_ERR_NOT_FOUND;
    }

    memcpy(out, &msg->infohash, sizeof(wingo_info_hash_t));

    return WINGO_SUCCESS;
}

/*
 * Set info hash (get_peers, announce_peer).
 */
wingo_error_t wingo_dht_msg_set_info_hash(wingo_dht_msg_t *msg,
                                           const wingo_info_hash_t *infohash)
{
    if (msg == NULL || infohash == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(&msg->infohash, infohash, sizeof(wingo_info_hash_t));
    msg->has_infohash = true;

    return WINGO_SUCCESS;
}

/*
 * Get token.
 */
wingo_error_t wingo_dht_msg_token(const wingo_dht_msg_t *msg,
                                   const wingo_u8 **out,
                                   wingo_size *len)
{
    if (msg == NULL || out == NULL || len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (msg->token_len == 0) {
        return WINGO_ERR_NOT_FOUND;
    }

    *out = msg->token;
    *len = msg->token_len;

    return WINGO_SUCCESS;
}

/*
 * Set token.
 */
wingo_error_t wingo_dht_msg_set_token(wingo_dht_msg_t *msg,
                                       const wingo_u8 *token,
                                       wingo_size len)
{
    if (msg == NULL || token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len > WINGO_DHT_MSG_TOKEN_MAX) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    memcpy(msg->token, token, len);
    msg->token_len = len;

    return WINGO_SUCCESS;
}

/*
 * Get port (announce_peer).
 */
wingo_u16 wingo_dht_msg_port(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return 0;
    }

    return msg->port;
}

/*
 * Set port (announce_peer).
 */
void wingo_dht_msg_set_port(wingo_dht_msg_t *msg, wingo_u16 port)
{
    if (msg == NULL) {
        return;
    }

    msg->port = port;
}

/*
 * Get implied port (announce_peer).
 */
bool wingo_dht_msg_implied_port(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return false;
    }

    return msg->implied_port;
}

/*
 * Set implied port (announce_peer).
 */
void wingo_dht_msg_set_implied_port(wingo_dht_msg_t *msg, bool implied)
{
    if (msg == NULL) {
        return;
    }

    msg->implied_port = implied;
}

/*
 * Get want flags.
 */
int wingo_dht_msg_want(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return 0;
    }

    return msg->want;
}

/*
 * Set want flags.
 */
void wingo_dht_msg_set_want(wingo_dht_msg_t *msg, int want)
{
    if (msg == NULL) {
        return;
    }

    msg->want = want;
}

/* ============================================================================
 * DHT MESSAGE RESPONSE DATA
 * ============================================================================ */

/*
 * Get nodes from response.
 */
wingo_size wingo_dht_msg_nodes(const wingo_dht_msg_t *msg,
                                wingo_dht_node_info_t *nodes,
                                wingo_size max)
{
    wingo_size count;
    wingo_size i;

    if (msg == NULL || nodes == NULL || max == 0) {
        return 0;
    }

    count = (msg->num_nodes < max) ? msg->num_nodes : max;

    for (i = 0; i < count; i++) {
        memcpy(&nodes[i].id, &msg->nodes[i].id, sizeof(wingo_dht_id_t));

        if (msg->nodes[i].addr != NULL) {
            nodes[i].addr = wingo_addr_copy(msg->nodes[i].addr);
        } else {
            nodes[i].addr = NULL;
        }
    }

    return count;
}

/*
 * Add a node to response.
 */
wingo_error_t wingo_dht_msg_add_node(wingo_dht_msg_t *msg,
                                      const wingo_dht_id_t *id,
                                      const wingo_addr_t *addr)
{
    wingo_size idx;

    if (msg == NULL || id == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (msg->num_nodes >= WINGO_DHT_MSG_MAX_NODES) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    idx = msg->num_nodes;

    memcpy(&msg->nodes[idx].id, id, sizeof(wingo_dht_id_t));

    msg->nodes[idx].addr = wingo_addr_copy(addr);
    if (msg->nodes[idx].addr == NULL) {
        return WINGO_ERR_NOMEM;
    }

    msg->num_nodes++;

    return WINGO_SUCCESS;
}

/*
 * Get values (peers) from response.
 */
wingo_size wingo_dht_msg_values(const wingo_dht_msg_t *msg,
                                 wingo_addr_t **addrs,
                                 wingo_size max)
{
    wingo_size count;
    wingo_size i;

    if (msg == NULL || addrs == NULL || max == 0) {
        return 0;
    }

    count = (msg->num_values < max) ? msg->num_values : max;

    for (i = 0; i < count; i++) {
        if (msg->values[i] != NULL) {
            addrs[i] = wingo_addr_copy(msg->values[i]);
        } else {
            addrs[i] = NULL;
        }
    }

    return count;
}

/*
 * Add a value (peer) to response.
 */
wingo_error_t wingo_dht_msg_add_value(wingo_dht_msg_t *msg,
                                       const wingo_addr_t *addr)
{
    if (msg == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (msg->num_values >= WINGO_DHT_MSG_MAX_VALUES) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    msg->values[msg->num_values] = wingo_addr_copy(addr);
    if (msg->values[msg->num_values] == NULL) {
        return WINGO_ERR_NOMEM;
    }

    msg->num_values++;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT MESSAGE ERROR
 * ============================================================================ */

/*
 * Get error code.
 */
int wingo_dht_msg_error_code(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return 0;
    }

    return msg->error_code;
}

/*
 * Set error code.
 */
void wingo_dht_msg_set_error_code(wingo_dht_msg_t *msg, int code)
{
    if (msg == NULL) {
        return;
    }

    msg->error_code = code;
}

/*
 * Get error message.
 */
const char *wingo_dht_msg_error_message(const wingo_dht_msg_t *msg)
{
    if (msg == NULL) {
        return "";
    }

    return msg->error_message;
}

/*
 * Set error message.
 */
wingo_error_t wingo_dht_msg_set_error_message(wingo_dht_msg_t *msg,
                                               const char *message)
{
    wingo_size len;

    if (msg == NULL || message == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    len = strlen(message);
    if (len >= sizeof(msg->error_message)) {
        return WINGO_ERR_OVERFLOW;
    }

    memcpy(msg->error_message, message, len + 1);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT MESSAGE ENCODE
 * ============================================================================ */

/*
 * Encode a query message.
 */
static wingo_error_t encode_query(const wingo_dht_msg_t *msg,
                                   wingo_bencode_t *root)
{
    wingo_bencode_t *args;
    const char *query_str;
    wingo_error_t rc;

    /* Query type string */
    switch (msg->query_type) {
    case WINGO_DHT_QUERY_PING:
        query_str = "ping";
        break;
    case WINGO_DHT_QUERY_FIND_NODE:
        query_str = "find_node";
        break;
    case WINGO_DHT_QUERY_GET_PEERS:
        query_str = "get_peers";
        break;
    case WINGO_DHT_QUERY_ANNOUNCE_PEER:
        query_str = "announce_peer";
        break;
    default:
        return WINGO_ERR_INVALID_ARG;
    }

    /* Add "q" (query type) */
    rc = wingo_bencode_dict_set_string(root, "q", query_str, strlen(query_str));
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Create "a" (arguments) dict */
    args = wingo_bencode_new_dict();
    if (args == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Add sender ID */
    if (msg->has_id) {
        rc = wingo_bencode_dict_set_string(args, "id",
                                            msg->id.bytes,
                                            WINGO_DHT_ID_SIZE);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(args);
            return rc;
        }
    }

    /* Add query-specific arguments */
    switch (msg->query_type) {
    case WINGO_DHT_QUERY_PING:
        /* Nothing extra */
        break;

    case WINGO_DHT_QUERY_FIND_NODE:
        if (msg->has_target) {
            rc = wingo_bencode_dict_set_string(args, "target",
                                                msg->target.bytes,
                                                WINGO_DHT_ID_SIZE);
            if (rc != WINGO_SUCCESS) {
                wingo_bencode_free(args);
                return rc;
            }
        }
        break;

    case WINGO_DHT_QUERY_GET_PEERS:
        if (msg->has_infohash) {
            rc = wingo_bencode_dict_set_string(args, "info_hash",
                                                msg->infohash.bytes,
                                                WINGO_DHT_ID_SIZE);
            if (rc != WINGO_SUCCESS) {
                wingo_bencode_free(args);
                return rc;
            }
        }
        break;

    case WINGO_DHT_QUERY_ANNOUNCE_PEER:
        if (msg->has_infohash) {
            rc = wingo_bencode_dict_set_string(args, "info_hash",
                                                msg->infohash.bytes,
                                                WINGO_DHT_ID_SIZE);
            if (rc != WINGO_SUCCESS) {
                wingo_bencode_free(args);
                return rc;
            }
        }

        if (msg->token_len > 0) {
            rc = wingo_bencode_dict_set_string(args, "token",
                                                msg->token,
                                                msg->token_len);
            if (rc != WINGO_SUCCESS) {
                wingo_bencode_free(args);
                return rc;
            }
        }

        if (msg->implied_port) {
            rc = wingo_bencode_dict_set_integer(args, "implied_port", 1);
        } else {
            rc = wingo_bencode_dict_set_integer(args, "port", msg->port);
        }
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(args);
            return rc;
        }
        break;

    default:
        break;
    }

    /* Add "want" flags (for find_node, get_peers) */
    if (msg->want != 0) {
        wingo_bencode_t *want_list = wingo_bencode_new_list();
        if (want_list == NULL) {
            wingo_bencode_free(args);
            return WINGO_ERR_NOMEM;
        }

        if (msg->want & WINGO_DHT_WANT_IPV4) {
            wingo_bencode_t *v4 = wingo_bencode_new_cstring("n4");
            if (v4 != NULL) {
                wingo_bencode_list_append(want_list, v4);
            }
        }

        if (msg->want & WINGO_DHT_WANT_IPV6) {
            wingo_bencode_t *v6 = wingo_bencode_new_cstring("n6");
            if (v6 != NULL) {
                wingo_bencode_list_append(want_list, v6);
            }
        }

        rc = wingo_bencode_dict_set(args, "want", want_list);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(want_list);
            wingo_bencode_free(args);
            return rc;
        }
    }

    /* Add "a" to root */
    rc = wingo_bencode_dict_set(root, "a", args);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(args);
        return rc;
    }

    return WINGO_SUCCESS;
}

/*
 * Encode a response message.
 */
static wingo_error_t encode_response(const wingo_dht_msg_t *msg,
                                      wingo_bencode_t *root)
{
    wingo_bencode_t *resp;
    wingo_error_t rc;
    wingo_size i;

    /* Create "r" (response) dict */
    resp = wingo_bencode_new_dict();
    if (resp == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Add sender ID */
    if (msg->has_id) {
        rc = wingo_bencode_dict_set_string(resp, "id",
                                            msg->id.bytes,
                                            WINGO_DHT_ID_SIZE);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(resp);
            return rc;
        }
    }

    /* Add token (for get_peers) */
    if (msg->token_len > 0) {
        rc = wingo_bencode_dict_set_string(resp, "token",
                                            msg->token,
                                            msg->token_len);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(resp);
            return rc;
        }
    }

    /* Add nodes (for find_node, get_peers) */
    if (msg->num_nodes > 0) {
        wingo_bencode_t *nodes_val = encode_compact_nodes(msg->nodes,
                                                           msg->num_nodes);
        if (nodes_val == NULL) {
            wingo_bencode_free(resp);
            return WINGO_ERR_NOMEM;
        }

        rc = wingo_bencode_dict_set(resp, "nodes", nodes_val);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(nodes_val);
            wingo_bencode_free(resp);
            return rc;
        }
    }

    /* Add values (peers) for get_peers */
    if (msg->num_values > 0) {
        wingo_bencode_t *values_list = wingo_bencode_new_list();
        if (values_list == NULL) {
            wingo_bencode_free(resp);
            return WINGO_ERR_NOMEM;
        }

        for (i = 0; i < msg->num_values; i++) {
            if (msg->values[i] == NULL) {
                continue;
            }

            /* Encode address as compact bytes (6 bytes for IPv4) */
            char ip_str[WINGO_ADDR_STR_MAX];
            unsigned int a, b, c, d;
            wingo_u16 port;
            wingo_u8 compact[6];

            if (wingo_addr_ip_str(msg->values[i], ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
                continue;
            }

            if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
                continue;
            }

            port = wingo_addr_port(msg->values[i]);

            compact[0] = (wingo_u8)a;
            compact[1] = (wingo_u8)b;
            compact[2] = (wingo_u8)c;
            compact[3] = (wingo_u8)d;
            compact[4] = (wingo_u8)(port >> 8);
            compact[5] = (wingo_u8)(port & 0xFF);

            wingo_bencode_t *v = wingo_bencode_new_string(compact, 6);
            if (v != NULL) {
                wingo_bencode_list_append(values_list, v);
            }
        }

        rc = wingo_bencode_dict_set(resp, "values", values_list);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(values_list);
            wingo_bencode_free(resp);
            return rc;
        }
    }

    /* Add "r" to root */
    rc = wingo_bencode_dict_set(root, "r", resp);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(resp);
        return rc;
    }

    return WINGO_SUCCESS;
}

/*
 * Encode an error message.
 */
static wingo_error_t encode_error(const wingo_dht_msg_t *msg,
                                   wingo_bencode_t *root)
{
    wingo_bencode_t *error_list;
    wingo_bencode_t *code_val;
    wingo_bencode_t *msg_val;
    wingo_error_t rc;

    /* Create "e" list */
    error_list = wingo_bencode_new_list();
    if (error_list == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Add error code */
    code_val = wingo_bencode_new_integer(msg->error_code);
    if (code_val == NULL) {
        wingo_bencode_free(error_list);
        return WINGO_ERR_NOMEM;
    }

    rc = wingo_bencode_list_append(error_list, code_val);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(code_val);
        wingo_bencode_free(error_list);
        return rc;
    }

    /* Add error message */
    msg_val = wingo_bencode_new_cstring(msg->error_message);
    if (msg_val == NULL) {
        wingo_bencode_free(error_list);
        return WINGO_ERR_NOMEM;
    }

    rc = wingo_bencode_list_append(error_list, msg_val);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(msg_val);
        wingo_bencode_free(error_list);
        return rc;
    }

    /* Add "e" to root */
    rc = wingo_bencode_dict_set(root, "e", error_list);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(error_list);
        return rc;
    }

    return WINGO_SUCCESS;
}

/*
 * Encode a DHT message.
 */
wingo_error_t wingo_dht_msg_encode(const wingo_dht_msg_t *msg,
                                    wingo_buf_t *buf)
{
    wingo_bencode_t *root;
    wingo_error_t rc;
    const char *type_str;

    if (msg == NULL || buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Create root dict */
    root = wingo_bencode_new_dict();
    if (root == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Add transaction ID */
    if (msg->tid_len > 0) {
        rc = wingo_bencode_dict_set_string(root, "t",
                                            msg->tid, msg->tid_len);
        if (rc != WINGO_SUCCESS) {
            wingo_bencode_free(root);
            return rc;
        }
    }

    /* Add message type */
    switch (msg->type) {
    case WINGO_DHT_MSG_TYPE_QUERY:
        type_str = "q";
        break;
    case WINGO_DHT_MSG_TYPE_RESPONSE:
        type_str = "r";
        break;
    case WINGO_DHT_MSG_TYPE_ERROR:
        type_str = "e";
        break;
    default:
        wingo_bencode_free(root);
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_bencode_dict_set_string(root, "y", type_str, 1);
    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(root);
        return rc;
    }

    /* Encode based on type */
    switch (msg->type) {
    case WINGO_DHT_MSG_TYPE_QUERY:
        rc = encode_query(msg, root);
        break;
    case WINGO_DHT_MSG_TYPE_RESPONSE:
        rc = encode_response(msg, root);
        break;
    case WINGO_DHT_MSG_TYPE_ERROR:
        rc = encode_error(msg, root);
        break;
    default:
        rc = WINGO_ERR_INVALID_ARG;
        break;
    }

    if (rc != WINGO_SUCCESS) {
        wingo_bencode_free(root);
        return rc;
    }

    /* Encode to buffer */
    rc = wingo_bencode_encode(root, buf);
    wingo_bencode_free(root);

    return rc;
}

/*
 * Encode a DHT message to newly allocated buffer.
 */
wingo_buf_t *wingo_dht_msg_encode_new(const wingo_dht_msg_t *msg)
{
    wingo_buf_t *buf;
    wingo_error_t rc;

    if (msg == NULL) {
        return NULL;
    }

    buf = wingo_buf_new(256);
    if (buf == NULL) {
        return NULL;
    }

    rc = wingo_dht_msg_encode(msg, buf);
    if (rc != WINGO_SUCCESS) {
        wingo_buf_free(buf);
        return NULL;
    }

    return buf;
}

/* ============================================================================
 * DHT MESSAGE DECODE
 * ============================================================================ */

/*
 * Decode a query message.
 */
static wingo_error_t decode_query(wingo_dht_msg_t *msg,
                                   const wingo_bencode_t *root)
{
    const wingo_bencode_t *query;
    const wingo_bencode_t *args;
    const char *query_str;

    /* Get "q" (query type) */
    query = wingo_bencode_dict_get(root, "q");
    if (query == NULL || !wingo_bencode_is_string(query)) {
        return WINGO_ERR_DHT_PARSE;
    }

    if (wingo_bencode_get_cstring(query, (char *)&query_str, 0) != WINGO_SUCCESS) {
        /* Fallback: get string directly */
        const void *data;
        wingo_size len;

        if (wingo_bencode_get_string(query, &data, &len) != WINGO_SUCCESS) {
            return WINGO_ERR_DHT_PARSE;
        }

        /* Compare against known strings */
        if (wingo_bencode_string_eq(query, "ping", 4)) {
            msg->query_type = WINGO_DHT_QUERY_PING;
        } else if (wingo_bencode_string_eq(query, "find_node", 9)) {
            msg->query_type = WINGO_DHT_QUERY_FIND_NODE;
        } else if (wingo_bencode_string_eq(query, "get_peers", 9)) {
            msg->query_type = WINGO_DHT_QUERY_GET_PEERS;
        } else if (wingo_bencode_string_eq(query, "announce_peer", 13)) {
            msg->query_type = WINGO_DHT_QUERY_ANNOUNCE_PEER;
        } else {
            msg->query_type = WINGO_DHT_QUERY_UNKNOWN;
        }

        (void)len;
        (void)data;
    }

    /* Get "a" (arguments) */
    args = wingo_bencode_dict_get(root, "a");
    if (args == NULL || !wingo_bencode_is_dict(args)) {
        return WINGO_ERR_DHT_PARSE;
    }

    /* Get sender ID */
    {
        const void *id_data;
        wingo_size id_len;

        if (wingo_bencode_dict_get_string(args, "id", &id_data, &id_len) == WINGO_SUCCESS) {
            if (id_len == WINGO_DHT_ID_SIZE) {
                memcpy(msg->id.bytes, id_data, WINGO_DHT_ID_SIZE);
                msg->has_id = true;
            }
        }
    }

    /* Get query-specific arguments */
    switch (msg->query_type) {
    case WINGO_DHT_QUERY_FIND_NODE: {
        const void *target_data;
        wingo_size target_len;

        if (wingo_bencode_dict_get_string(args, "target", &target_data, &target_len) == WINGO_SUCCESS) {
            if (target_len == WINGO_DHT_ID_SIZE) {
                memcpy(msg->target.bytes, target_data, WINGO_DHT_ID_SIZE);
                msg->has_target = true;
            }
        }
        break;
    }

    case WINGO_DHT_QUERY_GET_PEERS:
    case WINGO_DHT_QUERY_ANNOUNCE_PEER: {
        const void *ih_data;
        wingo_size ih_len;

        if (wingo_bencode_dict_get_string(args, "info_hash", &ih_data, &ih_len) == WINGO_SUCCESS) {
            if (ih_len == WINGO_DHT_ID_SIZE) {
                memcpy(msg->infohash.bytes, ih_data, WINGO_DHT_ID_SIZE);
                msg->has_infohash = true;
            }
        }

        /* Token */
        if (msg->query_type == WINGO_DHT_QUERY_ANNOUNCE_PEER) {
            const void *token_data;
            wingo_size token_len;

            if (wingo_bencode_dict_get_string(args, "token", &token_data, &token_len) == WINGO_SUCCESS) {
                if (token_len <= WINGO_DHT_MSG_TOKEN_MAX) {
                    memcpy(msg->token, token_data, token_len);
                    msg->token_len = token_len;
                }
            }

            /* Port */
            wingo_i64 port_val;
            if (wingo_bencode_dict_get_integer(args, "port", &port_val) == WINGO_SUCCESS) {
                msg->port = (wingo_u16)port_val;
            }

            /* Implied port */
            wingo_i64 implied_val;
            if (wingo_bencode_dict_get_integer(args, "implied_port", &implied_val) == WINGO_SUCCESS) {
                msg->implied_port = (implied_val != 0);
            }
        }
        break;
    }

    default:
        break;
    }

    return WINGO_SUCCESS;
}

/*
 * Decode a response message.
 */
static wingo_error_t decode_response(wingo_dht_msg_t *msg,
                                      const wingo_bencode_t *root)
{
    const wingo_bencode_t *resp;

    /* Get "r" (response) */
    resp = wingo_bencode_dict_get(root, "r");
    if (resp == NULL || !wingo_bencode_is_dict(resp)) {
        return WINGO_ERR_DHT_PARSE;
    }

    /* Get sender ID */
    {
        const void *id_data;
        wingo_size id_len;

        if (wingo_bencode_dict_get_string(resp, "id", &id_data, &id_len) == WINGO_SUCCESS) {
            if (id_len == WINGO_DHT_ID_SIZE) {
                memcpy(msg->id.bytes, id_data, WINGO_DHT_ID_SIZE);
                msg->has_id = true;
            }
        }
    }

    /* Get token */
    {
        const void *token_data;
        wingo_size token_len;

        if (wingo_bencode_dict_get_string(resp, "token", &token_data, &token_len) == WINGO_SUCCESS) {
            if (token_len <= WINGO_DHT_MSG_TOKEN_MAX) {
                memcpy(msg->token, token_data, token_len);
                msg->token_len = token_len;
            }
        }
    }

    /* Get nodes */
    {
        const void *nodes_data;
        wingo_size nodes_len;

        if (wingo_bencode_dict_get_string(resp, "nodes", &nodes_data, &nodes_len) == WINGO_SUCCESS) {
            msg->num_nodes = decode_compact_nodes(nodes_data, nodes_len,
                                                   msg->nodes,
                                                   WINGO_DHT_MSG_MAX_NODES);
        }
    }

    /* Get values (peers) */
    {
        const wingo_bencode_t *values = wingo_bencode_dict_get(resp, "values");

        if (values != NULL && wingo_bencode_is_list(values)) {
            wingo_size count = wingo_bencode_list_len(values);
            wingo_size i;

            for (i = 0; i < count && msg->num_values < WINGO_DHT_MSG_MAX_VALUES; i++) {
                const wingo_bencode_t *v = wingo_bencode_list_get(values, i);
                const void *vdata;
                wingo_size vlen;

                if (v == NULL || !wingo_bencode_is_string(v)) {
                    continue;
                }

                if (wingo_bencode_get_string(v, &vdata, &vlen) != WINGO_SUCCESS) {
                    continue;
                }

                if (vlen == 6) {
                    /* IPv4: 4 bytes IP + 2 bytes port */
                    const wingo_u8 *bytes = (const wingo_u8 *)vdata;
                    char ip_str[WINGO_ADDR_STR_MAX];
                    wingo_u16 port;

                    snprintf(ip_str, sizeof(ip_str), "%u.%u.%u.%u",
                             bytes[0], bytes[1], bytes[2], bytes[3]);

                    port = (wingo_u16)((bytes[4] << 8) | bytes[5]);

                    msg->values[msg->num_values] = wingo_addr_new_ipv4(ip_str, port);
                    if (msg->values[msg->num_values] != NULL) {
                        msg->num_values++;
                    }
                }
            }
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Decode an error message.
 */
static wingo_error_t decode_error(wingo_dht_msg_t *msg,
                                   const wingo_bencode_t *root)
{
    const wingo_bencode_t *error;
    const wingo_bencode_t *code_val;
    const wingo_bencode_t *msg_val;
    wingo_i64 code;

    /* Get "e" (error) */
    error = wingo_bencode_dict_get(root, "e");
    if (error == NULL || !wingo_bencode_is_list(error)) {
        return WINGO_ERR_DHT_PARSE;
    }

    /* Get error code */
    code_val = wingo_bencode_list_get(error, 0);
    if (code_val != NULL && wingo_bencode_is_integer(code_val)) {
        if (wingo_bencode_get_integer(code_val, &code) == WINGO_SUCCESS) {
            msg->error_code = (int)code;
        }
    }

    /* Get error message */
    msg_val = wingo_bencode_list_get(error, 1);
    if (msg_val != NULL && wingo_bencode_is_string(msg_val)) {
        const void *data;
        wingo_size len;

        if (wingo_bencode_get_string(msg_val, &data, &len) == WINGO_SUCCESS) {
            if (len < sizeof(msg->error_message)) {
                memcpy(msg->error_message, data, len);
                msg->error_message[len] = '\0';
            }
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Decode a DHT message.
 */
wingo_dht_msg_t *wingo_dht_msg_decode(const void *data, wingo_size len)
{
    wingo_bencode_t *root;
    wingo_dht_msg_t *msg;
    const wingo_bencode_t *type_val;
    wingo_error_t rc;

    if (data == NULL || len == 0) {
        return NULL;
    }

    /* Decode bencode */
    root = wingo_bencode_decode(data, len);
    if (root == NULL) {
        return NULL;
    }

    if (!wingo_bencode_is_dict(root)) {
        wingo_bencode_free(root);
        return NULL;
    }

    /* Create message */
    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_UNKNOWN);
    if (msg == NULL) {
        wingo_bencode_free(root);
        return NULL;
    }

    /* Get transaction ID */
    {
        const void *tid_data;
        wingo_size tid_len;

        if (wingo_bencode_dict_get_string(root, "t", &tid_data, &tid_len) == WINGO_SUCCESS) {
            if (tid_len <= WINGO_DHT_TID_MAX_SIZE) {
                memcpy(msg->tid, tid_data, tid_len);
                msg->tid_len = tid_len;
            }
        }
    }

    /* Get message type */
    type_val = wingo_bencode_dict_get(root, "y");
    if (type_val == NULL || !wingo_bencode_is_string(type_val)) {
        wingo_bencode_free(root);
        wingo_dht_msg_free(msg);
        return NULL;
    }

    if (wingo_bencode_string_eq(type_val, "q", 1)) {
        msg->type = WINGO_DHT_MSG_TYPE_QUERY;
        rc = decode_query(msg, root);
    } else if (wingo_bencode_string_eq(type_val, "r", 1)) {
        msg->type = WINGO_DHT_MSG_TYPE_RESPONSE;
        rc = decode_response(msg, root);
    } else if (wingo_bencode_string_eq(type_val, "e", 1)) {
        msg->type = WINGO_DHT_MSG_TYPE_ERROR;
        rc = decode_error(msg, root);
    } else {
        rc = WINGO_ERR_DHT_PARSE;
    }

    wingo_bencode_free(root);

    if (rc != WINGO_SUCCESS) {
        wingo_dht_msg_free(msg);
        return NULL;
    }

    return msg;
}

/* ============================================================================
 * DHT MESSAGE BUILDERS
 * ============================================================================ */

/*
 * Build a ping query.
 */
wingo_dht_msg_t *wingo_dht_msg_build_ping(const wingo_dht_id_t *my_id,
                                           const wingo_u8 *tid,
                                           wingo_size tid_len)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_QUERY);
    if (msg == NULL) {
        return NULL;
    }

    msg->query_type = WINGO_DHT_QUERY_PING;

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    } else {
        wingo_u8 new_tid[4];
        random_tid(new_tid, sizeof(new_tid));
        wingo_dht_msg_set_tid(msg, new_tid, sizeof(new_tid));
    }

    return msg;
}

/*
 * Build a pong response.
 */
wingo_dht_msg_t *wingo_dht_msg_build_pong(const wingo_dht_id_t *my_id,
                                           const wingo_u8 *tid,
                                           wingo_size tid_len)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_RESPONSE);
    if (msg == NULL) {
        return NULL;
    }

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    }

    return msg;
}

/*
 * Build a find_node query.
 */
wingo_dht_msg_t *wingo_dht_msg_build_find_node(const wingo_dht_id_t *my_id,
                                                const wingo_u8 *tid,
                                                wingo_size tid_len,
                                                const wingo_dht_id_t *target,
                                                int want)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_QUERY);
    if (msg == NULL) {
        return NULL;
    }

    msg->query_type = WINGO_DHT_QUERY_FIND_NODE;

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (target != NULL) {
        wingo_dht_msg_set_target(msg, target);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    } else {
        wingo_u8 new_tid[4];
        random_tid(new_tid, sizeof(new_tid));
        wingo_dht_msg_set_tid(msg, new_tid, sizeof(new_tid));
    }

    msg->want = want;

    return msg;
}

/*
 * Build a nodes response.
 */
wingo_dht_msg_t *wingo_dht_msg_build_nodes(const wingo_dht_id_t *my_id,
                                            const wingo_u8 *tid,
                                            wingo_size tid_len)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_RESPONSE);
    if (msg == NULL) {
        return NULL;
    }

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    }

    return msg;
}

/*
 * Build a get_peers query.
 */
wingo_dht_msg_t *wingo_dht_msg_build_get_peers(const wingo_dht_id_t *my_id,
                                                const wingo_u8 *tid,
                                                wingo_size tid_len,
                                                const wingo_info_hash_t *infohash,
                                                int want)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_QUERY);
    if (msg == NULL) {
        return NULL;
    }

    msg->query_type = WINGO_DHT_QUERY_GET_PEERS;

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (infohash != NULL) {
        wingo_dht_msg_set_info_hash(msg, infohash);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    } else {
        wingo_u8 new_tid[4];
        random_tid(new_tid, sizeof(new_tid));
        wingo_dht_msg_set_tid(msg, new_tid, sizeof(new_tid));
    }

    msg->want = want;

    return msg;
}

/*
 * Build a peers/nodes response.
 */
wingo_dht_msg_t *wingo_dht_msg_build_peers(const wingo_dht_id_t *my_id,
                                            const wingo_u8 *tid,
                                            wingo_size tid_len,
                                            const wingo_u8 *token,
                                            wingo_size token_len)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_RESPONSE);
    if (msg == NULL) {
        return NULL;
    }

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (token != NULL && token_len > 0) {
        wingo_dht_msg_set_token(msg, token, token_len);
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    }

    return msg;
}

/*
 * Build an announce_peer query.
 */
wingo_dht_msg_t *wingo_dht_msg_build_announce_peer(const wingo_dht_id_t *my_id,
                                                    const wingo_u8 *tid,
                                                    wingo_size tid_len,
                                                    const wingo_info_hash_t *infohash,
                                                    wingo_u16 port,
                                                    const wingo_u8 *token,
                                                    wingo_size token_len)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_QUERY);
    if (msg == NULL) {
        return NULL;
    }

    msg->query_type = WINGO_DHT_QUERY_ANNOUNCE_PEER;

    if (my_id != NULL) {
        wingo_dht_msg_set_id(msg, my_id);
    }

    if (infohash != NULL) {
        wingo_dht_msg_set_info_hash(msg, infohash);
    }

    if (token != NULL && token_len > 0) {
        wingo_dht_msg_set_token(msg, token, token_len);
    }

    msg->port = port;

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    } else {
        wingo_u8 new_tid[4];
        random_tid(new_tid, sizeof(new_tid));
        wingo_dht_msg_set_tid(msg, new_tid, sizeof(new_tid));
    }

    return msg;
}

/*
 * Build an error response.
 */
wingo_dht_msg_t *wingo_dht_msg_build_error(const wingo_u8 *tid,
                                            wingo_size tid_len,
                                            int code,
                                            const char *message)
{
    wingo_dht_msg_t *msg;

    msg = wingo_dht_msg_new(WINGO_DHT_MSG_TYPE_ERROR);
    if (msg == NULL) {
        return NULL;
    }

    if (tid != NULL && tid_len > 0) {
        wingo_dht_msg_set_tid(msg, tid, tid_len);
    }

    msg->error_code = code;

    if (message != NULL) {
        wingo_dht_msg_set_error_message(msg, message);
    }

    return msg;
}

/* ============================================================================
 * DHT MESSAGE UTILITY
 * ============================================================================ */

/*
 * Get message type name.
 */
const char *wingo_dht_msg_type_name(wingo_dht_msg_type_t type)
{
    switch (type) {
    case WINGO_DHT_MSG_TYPE_QUERY:    return "QUERY";
    case WINGO_DHT_MSG_TYPE_RESPONSE: return "RESPONSE";
    case WINGO_DHT_MSG_TYPE_ERROR:    return "ERROR";
    default:                          return "UNKNOWN";
    }
}

/*
 * Get query type name.
 */
const char *wingo_dht_msg_query_name(wingo_dht_query_type_t query)
{
    switch (query) {
    case WINGO_DHT_QUERY_PING:          return "ping";
    case WINGO_DHT_QUERY_FIND_NODE:     return "find_node";
    case WINGO_DHT_QUERY_GET_PEERS:     return "get_peers";
    case WINGO_DHT_QUERY_ANNOUNCE_PEER: return "announce_peer";
    default:                            return "unknown";
    }
}

/*
 * Print DHT message.
 */
void wingo_dht_msg_print(const wingo_dht_msg_t *msg, FILE *f)
{
    char id_hex[WINGO_DHT_ID_HEX_SIZE];
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (msg == NULL) {
        fprintf(f, "DHT message: (null)\n");
        return;
    }

    fprintf(f, "DHT Message:\n");
    fprintf(f, "  Type:       %s\n", wingo_dht_msg_type_name(msg->type));

    if (msg->type == WINGO_DHT_MSG_TYPE_QUERY) {
        fprintf(f, "  Query:      %s\n", wingo_dht_msg_query_name(msg->query_type));
    }

    /* TID */
    fprintf(f, "  TID:        ");
    for (i = 0; i < msg->tid_len; i++) {
        fprintf(f, "%02x", msg->tid[i]);
    }
    fprintf(f, "\n");

    /* Sender ID */
    if (msg->has_id) {
        static const char hex[] = "0123456789abcdef";
        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            id_hex[i * 2] = hex[(msg->id.bytes[i] >> 4) & 0x0F];
            id_hex[i * 2 + 1] = hex[msg->id.bytes[i] & 0x0F];
        }
        id_hex[WINGO_DHT_ID_SIZE * 2] = '\0';
        fprintf(f, "  Sender ID:  %s\n", id_hex);
    }

    /* Query params */
    if (msg->has_target) {
        static const char hex[] = "0123456789abcdef";
        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            id_hex[i * 2] = hex[(msg->target.bytes[i] >> 4) & 0x0F];
            id_hex[i * 2 + 1] = hex[msg->target.bytes[i] & 0x0F];
        }
        id_hex[WINGO_DHT_ID_SIZE * 2] = '\0';
        fprintf(f, "  Target:     %s\n", id_hex);
    }

    if (msg->has_infohash) {
        static const char hex[] = "0123456789abcdef";
        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            id_hex[i * 2] = hex[(msg->infohash.bytes[i] >> 4) & 0x0F];
            id_hex[i * 2 + 1] = hex[msg->infohash.bytes[i] & 0x0F];
        }
        id_hex[WINGO_DHT_ID_SIZE * 2] = '\0';
        fprintf(f, "  Info hash:  %s\n", id_hex);
    }

    if (msg->token_len > 0) {
        fprintf(f, "  Token:      ");
        for (i = 0; i < msg->token_len; i++) {
            fprintf(f, "%02x", msg->token[i]);
        }
        fprintf(f, "\n");
    }

    if (msg->port > 0) {
        fprintf(f, "  Port:       %u\n", msg->port);
    }

    /* Response data */
    if (msg->num_nodes > 0) {
        fprintf(f, "  Nodes:      %zu\n", msg->num_nodes);
    }

    if (msg->num_values > 0) {
        fprintf(f, "  Values:     %zu\n", msg->num_values);

        for (i = 0; i < msg->num_values; i++) {
            char addr_str[WINGO_ADDR_STR_MAX];

            if (msg->values[i] != NULL) {
                wingo_addr_str(msg->values[i], addr_str, sizeof(addr_str));
                fprintf(f, "    [%zu] %s\n", i, addr_str);
            }
        }
    }

    /* Error */
    if (msg->type == WINGO_DHT_MSG_TYPE_ERROR) {
        fprintf(f, "  Error code: %d\n", msg->error_code);
        fprintf(f, "  Error msg:  %s\n", msg->error_message);
    }
}

/*
 * Dump DHT message (hex).
 */
void wingo_dht_msg_dump(const wingo_dht_msg_t *msg, FILE *f)
{
    wingo_buf_t *buf;

    if (f == NULL) {
        f = stderr;
    }

    if (msg == NULL) {
        fprintf(f, "DHT message: (null)\n");
        return;
    }

    buf = wingo_dht_msg_encode_new(msg);
    if (buf == NULL) {
        fprintf(f, "DHT message: encode failed\n");
        return;
    }

    fprintf(f, "DHT message (%zu bytes):\n", buf->len);

    wingo_buf_fprint(buf, f);

    wingo_buf_free(buf);
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
