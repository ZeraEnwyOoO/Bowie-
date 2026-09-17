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

#ifndef WINGO_NET_DHT_TYPES_H
#define WINGO_NET_DHT_TYPES_H

/*
 * ============================================================================
 * WINGO DHT TYPES
 * ============================================================================
 *
 * This header provides the foundation types for the DHT subsystem.
 *
 * It contains ONLY:
 *   - Constants (ID size, token size, limits)
 *   - Types (ID, info hash, token, state)
 *   - Opaque handle (wingo_dht_t)
 *
 * It does NOT contain:
 *   - API functions (those are in dht.h)
 *   - Configuration (that's in dht_config.h)
 *   - Subsystem headers (node, bucket, routing, ...)
 *
 * This separation allows subsystem headers to include ONLY the types
 * they need, without pulling in the full DHT API or unnecessary
 * dependencies (like peer.h).
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT TYPE HIERARCHY                       │
 *   │                                                             │
 *   │   dht_types.h  ← គ្មាន dependency ក្រៅពី common + error     │
 *   │       │                                                     │
 *   │       ├── dht_id_t          (160-bit node ID)              │
 *   │       ├── info_hash_t       (160-bit info hash)            │
 *   │       ├── dht_token_t       (8-byte token)                 │
 *   │       ├── dht_state_t       (lifecycle state)              │
 *   │       └── dht_t             (opaque handle)                │
 *   │                                                             │
 *   │   dht.h          ← API + config                             │
 *   │   dht_config.h   ← config only                              │
 *   │   dht_node.h     ← node only                                │
 *   │   dht_bucket.h   ← bucket only                              │
 *   │   dht_routing.h  ← routing only                             │
 *   │   dht_search.h   ← search only                              │
 *   │   dht_storage.h  ← storage only                             │
 *   │   dht_message.h  ← message only                             │
 *   │   dht_token.h    ← token only                               │
 *   │   dht_security.h ← security only                            │
 *   │   dht_bencode.h  ← bencode only                             │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * DHT CONSTANTS
 * ============================================================================ */

/*
 * DHT ID size (160-bit).
 */
#define WINGO_DHT_ID_SIZE       20

/*
 * DHT ID hex size (20 bytes * 2 + null terminator).
 */
#define WINGO_DHT_ID_HEX_SIZE   41

/*
 * DHT token size (8 bytes).
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

/*
 * DHT bucket constants.
 *
 * NOTE: These are also defined in dht_bucket.h, but we
 *       define them here so that dht_config.h can use them
 *       without pulling in dht_bucket.h (which has heavy
 *       dependencies on dht_node.h and socket.h).
 */
#define WINGO_DHT_BUCKET_SIZE       8
#define WINGO_DHT_BUCKET_MIN_SIZE   4

/* ============================================================================
 * DHT ID TYPES
 * ============================================================================ */

/*
 * DHT ID (160-bit).
 *
 * This is the fundamental identifier in the DHT.
 * It is used for:
 *   - Node IDs
 *   - Info hashes
 *   - Distance calculation
 */
typedef struct {
    wingo_u8 bytes[WINGO_DHT_ID_SIZE];
} wingo_dht_id_t;

/*
 * Info hash (160-bit).
 *
 * An info hash identifies a resource (e.g., a peer group)
 * in the DHT. It has the same size as a DHT ID.
 */
typedef wingo_dht_id_t wingo_info_hash_t;

/* ============================================================================
 * DHT TOKEN TYPE
 * ============================================================================ */

/*
 * DHT token (8 bytes).
 *
 * A token is a short opaque value that proves a node has
 * previously contacted us. It is used to prevent unsolicited
 * announce_peer requests.
 */
typedef struct {
    wingo_u8 bytes[WINGO_DHT_TOKEN_SIZE];
} wingo_dht_token_t;

/* ============================================================================
 * DHT STATE TYPE
 * ============================================================================ */

/*
 * DHT state.
 */
typedef enum {
    WINGO_DHT_STATE_STOPPED  = 0,   /* Not started */
    WINGO_DHT_STATE_STARTING = 1,   /* Starting */
    WINGO_DHT_STATE_RUNNING  = 2,   /* Running */
    WINGO_DHT_STATE_STOPPING = 3,   /* Stopping */
    WINGO_DHT_STATE_ERROR    = 4,   /* Error */
} wingo_dht_state_t;

/* ============================================================================
 * DHT OPAQUE HANDLE
 * ============================================================================ */

/*
 * DHT handle.
 *
 * This is an opaque type. Use wingo_dht_*() functions.
 */
typedef struct wingo_dht wingo_dht_t;

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_TYPES_H */
