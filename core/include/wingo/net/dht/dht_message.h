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

#ifndef WINGO_NET_DHT_MESSAGE_H
#define WINGO_NET_DHT_MESSAGE_H

/*
 * ============================================================================
 * WINGO DHT MESSAGE
 * ============================================================================
 *
 * This header provides DHT message encoding/decoding for Bowie.
 *
 * DHT Messages (Mainline DHT / BEP 5):
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT MESSAGES                             │
 *   │                                                             │
 *   │   Query Messages:                                           │
 *   │   ├── ping              — Check if node is alive            │
 *   │   ├── find_node         — Find nodes close to target        │
 *   │   ├── get_peers         — Find peers for info hash          │
 *   │   └── announce_peer     — Announce peer for info hash       │
 *   │                                                             │
 *   │   Response Messages:                                        │
 *   │   ├── ping (reply)      — Pong response                     │
 *   │   ├── find_node (reply) — Nodes response                    │
 *   │   ├── get_peers (reply) — Peers or nodes response           │
 *   │   └── announce_peer (reply) — Acknowledgment                │
 *   │                                                             │
 *   │   Error Messages:                                           │
 *   │   └── error             — Error response                    │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/dht.h"
#include "wingo/net/dht/dht_bencode.h"

/* ============================================================================
 * DHT MESSAGE CONSTANTS
 * ============================================================================ */

/*
 * Transaction ID size.
 */
#define WINGO_DHT_TID_SIZE          4

/*
 * Maximum transaction ID size.
 */
#define WINGO_DHT_TID_MAX_SIZE      16

/*
 * Maximum token size.
 */
#define WINGO_DHT_MSG_TOKEN_MAX     128

/*
 * Maximum nodes in message.
 */
#define WINGO_DHT_MSG_MAX_NODES     8

/*
 * Maximum values in message.
 */
#define WINGO_DHT_MSG_MAX_VALUES    50

/*
 * Maximum message size.
 */
#define WINGO_DHT_MSG_MAX_SIZE      4096

/* ============================================================================
 * DHT MESSAGE TYPES
 * ============================================================================ */

/*
 * DHT message type.
 */
typedef enum {
    WINGO_DHT_MSG_TYPE_UNKNOWN      = 0,
    WINGO_DHT_MSG_TYPE_QUERY        = 1,   /* Query (q) */
    WINGO_DHT_MSG_TYPE_RESPONSE     = 2,   /* Response (r) */
    WINGO_DHT_MSG_TYPE_ERROR        = 3,   /* Error (e) */
} wingo_dht_msg_type_t;

/*
 * DHT query type.
 */
typedef enum {
    WINGO_DHT_QUERY_UNKNOWN         = 0,
    WINGO_DHT_QUERY_PING            = 1,   /* ping */
    WINGO_DHT_QUERY_FIND_NODE       = 2,   /* find_node */
    WINGO_DHT_QUERY_GET_PEERS       = 3,   /* get_peers */
    WINGO_DHT_QUERY_ANNOUNCE_PEER   = 4,   /* announce_peer */
} wingo_dht_query_type_t;

/*
 * DHT error code.
 */
typedef enum {
    WINGO_DHT_ERROR_GENERIC         = 201,
    WINGO_DHT_ERROR_SERVER          = 202,
    WINGO_DHT_ERROR_PROTOCOL        = 203,
    WINGO_DHT_ERROR_UNKNOWN         = 204,
} wingo_dht_error_code_t;

/* ============================================================================
 * DHT MESSAGE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT message handle.
 */
typedef struct wingo_dht_msg wingo_dht_msg_t;

/* ============================================================================
 * DHT MESSAGE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT message.
 *
 * @param type      Message type
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_new(wingo_dht_msg_type_t type);

/*
 * Free a DHT message.
 *
 * @param msg       Message (NULL is safe)
 */
void wingo_dht_msg_free(wingo_dht_msg_t *msg);

/*
 * Clone a DHT message.
 *
 * @param msg       Message
 * @return          New message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_clone(const wingo_dht_msg_t *msg);

/* ============================================================================
 * DHT MESSAGE QUERY
 * ============================================================================ */

/*
 * Get message type.
 *
 * @param msg       Message
 * @return          Message type
 */
wingo_dht_msg_type_t wingo_dht_msg_type(const wingo_dht_msg_t *msg);

/*
 * Get query type.
 *
 * @param msg       Message
 * @return          Query type
 */
wingo_dht_query_type_t wingo_dht_msg_query_type(const wingo_dht_msg_t *msg);

/*
 * Set query type.
 *
 * @param msg       Message
 * @param query     Query type
 */
void wingo_dht_msg_set_query_type(wingo_dht_msg_t *msg,
                                   wingo_dht_query_type_t query);

/*
 * Get transaction ID.
 *
 * @param msg       Message
 * @param out       Output transaction ID
 * @param len       Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_tid(const wingo_dht_msg_t *msg,
                                 const wingo_u8 **out,
                                 wingo_size *len);

/*
 * Set transaction ID.
 *
 * @param msg       Message
 * @param tid       Transaction ID
 * @param len       Length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_tid(wingo_dht_msg_t *msg,
                                     const wingo_u8 *tid,
                                     wingo_size len);

/*
 * Get sender node ID.
 *
 * @param msg       Message
 * @param out       Output node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_id(const wingo_dht_msg_t *msg,
                                wingo_dht_id_t *out);

/*
 * Set sender node ID.
 *
 * @param msg       Message
 * @param id        Node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_id(wingo_dht_msg_t *msg,
                                    const wingo_dht_id_t *id);

/* ============================================================================
 * DHT MESSAGE QUERY PARAMETERS
 * ============================================================================ */

/*
 * Get target ID (find_node, get_peers).
 *
 * @param msg       Message
 * @param out       Output target ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_target(const wingo_dht_msg_t *msg,
                                    wingo_dht_id_t *out);

/*
 * Set target ID (find_node, get_peers).
 *
 * @param msg       Message
 * @param target    Target ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_target(wingo_dht_msg_t *msg,
                                        const wingo_dht_id_t *target);

/*
 * Get info hash (get_peers, announce_peer).
 *
 * @param msg       Message
 * @param out       Output info hash
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_info_hash(const wingo_dht_msg_t *msg,
                                       wingo_info_hash_t *out);

/*
 * Set info hash (get_peers, announce_peer).
 *
 * @param msg       Message
 * @param infohash  Info hash
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_info_hash(wingo_dht_msg_t *msg,
                                           const wingo_info_hash_t *infohash);

/*
 * Get token.
 *
 * @param msg       Message
 * @param out       Output token
 * @param len       Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_token(const wingo_dht_msg_t *msg,
                                   const wingo_u8 **out,
                                   wingo_size *len);

/*
 * Set token.
 *
 * @param msg       Message
 * @param token     Token
 * @param len       Length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_token(wingo_dht_msg_t *msg,
                                       const wingo_u8 *token,
                                       wingo_size len);

/*
 * Get port (announce_peer).
 *
 * @param msg       Message
 * @return          Port number
 */
wingo_u16 wingo_dht_msg_port(const wingo_dht_msg_t *msg);

/*
 * Set port (announce_peer).
 *
 * @param msg       Message
 * @param port      Port number
 */
void wingo_dht_msg_set_port(wingo_dht_msg_t *msg, wingo_u16 port);

/*
 * Get implied port (announce_peer).
 *
 * @param msg       Message
 * @return          Implied port flag
 */
bool wingo_dht_msg_implied_port(const wingo_dht_msg_t *msg);

/*
 * Set implied port (announce_peer).
 *
 * @param msg       Message
 * @param implied   Implied port flag
 */
void wingo_dht_msg_set_implied_port(wingo_dht_msg_t *msg, bool implied);

/*
 * Get want flags.
 *
 * @param msg       Message
 * @return          Want flags (WINGO_DHT_WANT_*)
 */
int wingo_dht_msg_want(const wingo_dht_msg_t *msg);

/*
 * Set want flags.
 *
 * @param msg       Message
 * @param want      Want flags
 */
void wingo_dht_msg_set_want(wingo_dht_msg_t *msg, int want);

/* ============================================================================
 * DHT MESSAGE RESPONSE DATA
 * ============================================================================ */

/*
 * Get nodes from response.
 *
 * @param msg       Message
 * @param nodes     Output array
 * @param max       Maximum number of nodes
 * @return          Number of nodes
 */
wingo_size wingo_dht_msg_nodes(const wingo_dht_msg_t *msg,
                                wingo_dht_node_info_t *nodes,
                                wingo_size max);

/*
 * Add a node to response.
 *
 * @param msg       Message
 * @param id        Node ID
 * @param addr      Node address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_add_node(wingo_dht_msg_t *msg,
                                      const wingo_dht_id_t *id,
                                      const wingo_addr_t *addr);

/*
 * Get values (peers) from response.
 *
 * @param msg       Message
 * @param addrs     Output array
 * @param max       Maximum number of addresses
 * @return          Number of addresses
 */
wingo_size wingo_dht_msg_values(const wingo_dht_msg_t *msg,
                                 wingo_addr_t **addrs,
                                 wingo_size max);

/*
 * Add a value (peer) to response.
 *
 * @param msg       Message
 * @param addr      Peer address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_add_value(wingo_dht_msg_t *msg,
                                       const wingo_addr_t *addr);

/* ============================================================================
 * DHT MESSAGE ERROR
 * ============================================================================ */

/*
 * Get error code.
 *
 * @param msg       Message
 * @return          Error code
 */
int wingo_dht_msg_error_code(const wingo_dht_msg_t *msg);

/*
 * Set error code.
 *
 * @param msg       Message
 * @param code      Error code
 */
void wingo_dht_msg_set_error_code(wingo_dht_msg_t *msg, int code);

/*
 * Get error message.
 *
 * @param msg       Message
 * @return          Error message (never NULL)
 */
const char *wingo_dht_msg_error_message(const wingo_dht_msg_t *msg);

/*
 * Set error message.
 *
 * @param msg       Message
 * @param message   Error message
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_set_error_message(wingo_dht_msg_t *msg,
                                               const char *message);

/* ============================================================================
 * DHT MESSAGE ENCODE/DECODE
 * ============================================================================ */

/*
 * Encode a DHT message.
 *
 * @param msg       Message
 * @param buf       Output buffer
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_msg_encode(const wingo_dht_msg_t *msg,
                                    wingo_buf_t *buf);

/*
 * Encode a DHT message to newly allocated buffer.
 *
 * @param msg       Message
 * @return          New buffer, or NULL on error
 */
wingo_buf_t *wingo_dht_msg_encode_new(const wingo_dht_msg_t *msg);

/*
 * Decode a DHT message.
 *
 * @param data      Data to decode
 * @param len       Length of data
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_decode(const void *data, wingo_size len);

/* ============================================================================
 * DHT MESSAGE BUILDERS
 * ============================================================================ */

/*
 * Build a ping query.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_ping(const wingo_dht_id_t *my_id,
                                           const wingo_u8 *tid,
                                           wingo_size tid_len);

/*
 * Build a pong response.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_pong(const wingo_dht_id_t *my_id,
                                           const wingo_u8 *tid,
                                           wingo_size tid_len);

/*
 * Build a find_node query.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @param target    Target ID
 * @param want      Want flags
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_find_node(const wingo_dht_id_t *my_id,
                                                const wingo_u8 *tid,
                                                wingo_size tid_len,
                                                const wingo_dht_id_t *target,
                                                int want);

/*
 * Build a nodes response.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_nodes(const wingo_dht_id_t *my_id,
                                            const wingo_u8 *tid,
                                            wingo_size tid_len);

/*
 * Build a get_peers query.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @param infohash  Info hash
 * @param want      Want flags
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_get_peers(const wingo_dht_id_t *my_id,
                                                const wingo_u8 *tid,
                                                wingo_size tid_len,
                                                const wingo_info_hash_t *infohash,
                                                int want);

/*
 * Build a peers/nodes response.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @param token     Token
 * @param token_len Token length
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_peers(const wingo_dht_id_t *my_id,
                                            const wingo_u8 *tid,
                                            wingo_size tid_len,
                                            const wingo_u8 *token,
                                            wingo_size token_len);

/*
 * Build an announce_peer query.
 *
 * @param my_id     My node ID
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @param infohash  Info hash
 * @param port      Port number
 * @param token     Token
 * @param token_len Token length
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_announce_peer(const wingo_dht_id_t *my_id,
                                                    const wingo_u8 *tid,
                                                    wingo_size tid_len,
                                                    const wingo_info_hash_t *infohash,
                                                    wingo_u16 port,
                                                    const wingo_u8 *token,
                                                    wingo_size token_len);

/*
 * Build an error response.
 *
 * @param tid       Transaction ID
 * @param tid_len   Transaction ID length
 * @param code      Error code
 * @param message   Error message
 * @return          Message, or NULL on error
 */
wingo_dht_msg_t *wingo_dht_msg_build_error(const wingo_u8 *tid,
                                            wingo_size tid_len,
                                            int code,
                                            const char *message);

/* ============================================================================
 * DHT MESSAGE UTILITY
 * ============================================================================ */

/*
 * Get message type name.
 *
 * @param type      Message type
 * @return          Static string
 */
const char *wingo_dht_msg_type_name(wingo_dht_msg_type_t type);

/*
 * Get query type name.
 *
 * @param query     Query type
 * @return          Static string
 */
const char *wingo_dht_msg_query_name(wingo_dht_query_type_t query);

/*
 * Print DHT message.
 *
 * @param msg       Message
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_msg_print(const wingo_dht_msg_t *msg, FILE *f);

/*
 * Dump DHT message (hex).
 *
 * @param msg       Message
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_msg_dump(const wingo_dht_msg_t *msg, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_MESSAGE_H */
