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
 *   - Types (ID, info hash, token VALUE, state)
 *   - Opaque handle (wingo_dht_t)
 *
 * It does NOT contain:
 *   - API functions (those are in dht.h)
 *   - Configuration (that's in dht_config.h)
 *   - Token MANAGER (that's in dht_token.h as wingo_dht_token_mgr_t)
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
 * DHT token manager secret size (16 bytes).
 */
#define WINGO_DHT_TOKEN_SECRET_SIZE 16

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
 *       without pulling in dht_bucket.h.
 */
#define WINGO_DHT_BUCKET_SIZE       8
#define WINGO_DHT_BUCKET_MIN_SIZE   4

/* ============================================================================
 * DHT ID TYPES
 * ============================================================================ */

/*
 * DHT ID (160-bit).
 */
typedef struct {
    wingo_u8 bytes[WINGO_DHT_ID_SIZE];
} wingo_dht_id_t;

/*
 * Info hash (160-bit).
 */
typedef wingo_dht_id_t wingo_info_hash_t;

/* ============================================================================
 * DHT TOKEN VALUE TYPE
 * ============================================================================ */

/*
 * DHT token VALUE (8 bytes).
 *
 * NOTE: This is a token VALUE, not a token MANAGER.
 *       The token manager is wingo_dht_token_mgr_t (in dht_token.h).
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
    WINGO_DHT_STATE_STOPPED  = 0,
    WINGO_DHT_STATE_STARTING = 1,
    WINGO_DHT_STATE_RUNNING  = 2,
    WINGO_DHT_STATE_STOPPING = 3,
    WINGO_DHT_STATE_ERROR    = 4,
} wingo_dht_state_t;

/* ============================================================================
 * DHT OPAQUE HANDLE
 * ============================================================================ */

/*
 * DHT handle.
 */
typedef struct wingo_dht wingo_dht_t;

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_TYPES_H */
