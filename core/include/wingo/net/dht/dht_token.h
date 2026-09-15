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

#ifndef WINGO_NET_DHT_TOKEN_H
#define WINGO_NET_DHT_TOKEN_H

/*
 * ============================================================================
 * WINGO DHT TOKEN
 * ============================================================================
 *
 * This header provides DHT token generation and verification for Bowie.
 *
 * What is a DHT token?
 *
 *   A DHT token is a short opaque value (8 bytes) that proves
 *   a node has previously contacted us. It is used to prevent
 *   unsolicited announce_peer requests (spam, reflection attacks).
 *
 * Token flow:
 *
 *   ┌─────────────┐                              ┌─────────────┐
 *   │  Peer A     │                              │  Peer B     │
 *   │             │                              │             │
 *   └──────┬──────┘                              └──────┬──────┘
 *          │                                            │
 *          │  1. get_peers(infohash)                    │
 *          │───────────────────────────────────────────>│
 *          │                                            │
 *          │  2. response(token)                        │
 *          │<───────────────────────────────────────────│
 *          │                                            │
 *          │  3. announce_peer(infohash, token)         │
 *          │───────────────────────────────────────────>│
 *          │                                            │
 *          │  4. verify token                           │
 *          │     ├── valid   → accept                   │
 *          │     └── invalid → reject                   │
 *          │                                            │
 *
 * Token algorithm:
 *
 *   token = SHA1(secret || IP)
 *
 *   where:
 *     secret = random 16 bytes, rotated every 15-45 minutes
 *     IP     = IPv4 (4 bytes) or IPv6 (16 bytes)
 *
 * Token rotation:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    TOKEN ROTATION                           │
 *   │                                                             │
 *   │   Secret 1 (active)                                         │
 *   │   ├── Used for new tokens                                   │
 *   │   └── Valid for verification                                │
 *   │                                                             │
 *   │   Secret 0 (previous)                                       │
 *   │   └── Valid for verification only (grace period)            │
 *   │                                                             │
 *   │   Rotate every 15-45 minutes                                │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"

/* ============================================================================
 * DHT TOKEN CONSTANTS
 * ============================================================================ */

/*
 * Token size (8 bytes).
 */
#define WINGO_DHT_TOKEN_SIZE            8

/*
 * Secret size (16 bytes).
 */
#define WINGO_DHT_TOKEN_SECRET_SIZE     16

/*
 * Minimum rotation interval (15 minutes).
 */
#define WINGO_DHT_TOKEN_ROTATE_MIN      (15 * 60)

/*
 * Maximum rotation interval (45 minutes).
 */
#define WINGO_DHT_TOKEN_ROTATE_MAX      (45 * 60)

/*
 * Number of active secrets (current + previous).
 */
#define WINGO_DHT_TOKEN_SECRET_COUNT    2

/*
 * Maximum tokens per second (for rate limiting).
 */
#define WINGO_DHT_TOKEN_RATE_MAX        1000

/* ============================================================================
 * DHT TOKEN TYPES
 * ============================================================================ */

/*
 * Token result.
 */
typedef enum {
    WINGO_DHT_TOKEN_OK              = 0,   /* Token valid */
    WINGO_DHT_TOKEN_INVALID         = 1,   /* Token invalid */
    WINGO_DHT_TOKEN_EXPIRED         = 2,   /* Token expired */
    WINGO_DHT_TOKEN_WRONG_ADDR      = 3,   /* Token for different address */
    WINGO_DHT_TOKEN_ERROR           = 4,   /* Internal error */
} wingo_dht_token_result_t;

/*
 * Token secret.
 */
typedef struct {
    wingo_u8    bytes[WINGO_DHT_TOKEN_SECRET_SIZE];  /* Secret bytes */
    wingo_i64   created_at;                          /* Creation time */
    bool        active;                              /* Is this the active secret? */
} wingo_dht_token_secret_t;

/* ============================================================================
 * DHT TOKEN STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT token manager handle.
 *
 * This is an opaque type. Use wingo_dht_token_*() functions.
 */
typedef struct wingo_dht_token wingo_dht_token_t;

/* ============================================================================
 * DHT TOKEN LIFECYCLE
 * ============================================================================ */

/*
 * Create a new token manager.
 *
 * @return          Token manager, or NULL on error
 */
wingo_dht_token_t *wingo_dht_token_new(void);

/*
 * Free a token manager.
 *
 * @param token     Token manager (NULL is safe)
 */
void wingo_dht_token_free(wingo_dht_token_t *token);

/*
 * Reset token manager.
 *
 * Generates new secrets and clears statistics.
 *
 * @param token     Token manager
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_reset(wingo_dht_token_t *token);

/* ============================================================================
 * DHT TOKEN GENERATION
 * ============================================================================ */

/*
 * Generate a token for an address.
 *
 * The token is deterministic: same address + same secret = same token.
 * This means we don't need to store tokens.
 *
 * @param token     Token manager
 * @param addr      Address
 * @param out       Output token buffer (at least WINGO_DHT_TOKEN_SIZE)
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_generate(wingo_dht_token_t *token,
                                        const wingo_addr_t *addr,
                                        wingo_u8 *out,
                                        wingo_size len);

/*
 * Generate a token for an IP address (no port).
 *
 * Some DHT implementations ignore port and only use IP.
 * This is useful for compatibility.
 *
 * @param token     Token manager
 * @param addr      Address
 * @param out       Output token buffer
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_generate_ip(wingo_dht_token_t *token,
                                           const wingo_addr_t *addr,
                                           wingo_u8 *out,
                                           wingo_size len);

/*
 * Generate a token for a specific secret.
 *
 * Used internally for verification (checks old secret).
 *
 * @param token     Token manager
 * @param addr      Address
 * @param secret    Secret index (0 = current, 1 = previous)
 * @param out       Output token buffer
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_generate_secret(wingo_dht_token_t *token,
                                               const wingo_addr_t *addr,
                                               int secret,
                                               wingo_u8 *out,
                                               wingo_size len);

/* ============================================================================
 * DHT TOKEN VERIFICATION
 * ============================================================================ */

/*
 * Verify a token for an address.
 *
 * Checks the token against both current and previous secrets.
 *
 * @param token     Token manager
 * @param addr      Address
 * @param in        Token to verify
 * @param len       Token length
 * @return          WINGO_DHT_TOKEN_OK if valid,
 *                  error code on failure
 */
wingo_dht_token_result_t wingo_dht_token_verify(wingo_dht_token_t *token,
                                                 const wingo_addr_t *addr,
                                                 const wingo_u8 *in,
                                                 wingo_size len);

/*
 * Verify a token with detailed result.
 *
 * @param token     Token manager
 * @param addr      Address
 * @param in        Token to verify
 * @param len       Token length
 * @param out_result Output detailed result
 * @return          true if valid, false otherwise
 */
bool wingo_dht_token_verify_ex(wingo_dht_token_t *token,
                                const wingo_addr_t *addr,
                                const wingo_u8 *in,
                                wingo_size len,
                                wingo_dht_token_result_t *out_result);

/*
 * Check if a token matches a specific secret.
 *
 * @param token     Token manager
 * @param addr      Address
 * @param secret    Secret index
 * @param in        Token to verify
 * @param len       Token length
 * @return          true if valid, false otherwise
 */
bool wingo_dht_token_verify_secret(wingo_dht_token_t *token,
                                    const wingo_addr_t *addr,
                                    int secret,
                                    const wingo_u8 *in,
                                    wingo_size len);

/* ============================================================================
 * DHT TOKEN ROTATION
 * ============================================================================ */

/*
 * Rotate the token secret.
 *
 * This moves the current secret to previous, and generates a new current.
 *
 * @param token     Token manager
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_rotate(wingo_dht_token_t *token);

/*
 * Check if rotation is needed.
 *
 * @param token     Token manager
 * @return          true if needed, false otherwise
 */
bool wingo_dht_token_needs_rotate(const wingo_dht_token_t *token);

/*
 * Get time until next rotation.
 *
 * @param token     Token manager
 * @return          Seconds until next rotation
 */
wingo_i64 wingo_dht_token_next_rotate(const wingo_dht_token_t *token);

/*
 * Set rotation interval.
 *
 * @param token     Token manager
 * @param min_s     Minimum interval (seconds)
 * @param max_s     Maximum interval (seconds)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_set_rotation(wingo_dht_token_t *token,
                                            wingo_i64 min_s,
                                            wingo_i64 max_s);

/*
 * Get current rotation interval.
 *
 * @param token     Token manager
 * @return          Rotation interval in seconds
 */
wingo_i64 wingo_dht_token_rotation_interval(const wingo_dht_token_t *token);

/* ============================================================================
 * DHT TOKEN SECRET
 * ============================================================================ */

/*
 * Get secret at index.
 *
 * @param token     Token manager
 * @param index     Secret index (0 = current, 1 = previous)
 * @return          Secret, or NULL if invalid index
 */
const wingo_dht_token_secret_t *wingo_dht_token_secret(
    const wingo_dht_token_t *token,
    int index);

/*
 * Get secret creation time.
 *
 * @param token     Token manager
 * @param index     Secret index
 * @return          Creation timestamp
 */
wingo_i64 wingo_dht_token_secret_created(const wingo_dht_token_t *token,
                                          int index);

/*
 * Get secret age.
 *
 * @param token     Token manager
 * @param index     Secret index
 * @return          Age in seconds
 */
wingo_i64 wingo_dht_token_secret_age(const wingo_dht_token_t *token,
                                      int index);

/*
 * Clear all secrets.
 *
 * @param token     Token manager
 */
void wingo_dht_token_secrets_clear(wingo_dht_token_t *token);

/* ============================================================================
 * DHT TOKEN STATISTICS
 * ============================================================================ */

/*
 * Token statistics.
 */
typedef struct {
    wingo_u64   tokens_generated;       /* Tokens generated */
    wingo_u64   tokens_verified;        /* Tokens verified */
    wingo_u64   tokens_valid;           /* Valid tokens */
    wingo_u64   tokens_invalid;         /* Invalid tokens */
    wingo_u64   tokens_expired;         /* Expired tokens */
    wingo_u64   tokens_wrong_addr;      /* Wrong address */
    wingo_u64   rotations;              /* Secret rotations */
    wingo_i64   last_rotation;          /* Last rotation timestamp */
    wingo_i64   next_rotation;          /* Next rotation timestamp */
    wingo_i64   current_age;            /* Current secret age */
    wingo_i64   previous_age;           /* Previous secret age */
} wingo_dht_token_stats_t;

/*
 * Get token statistics.
 *
 * @param token     Token manager
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_get_stats(const wingo_dht_token_t *token,
                                         wingo_dht_token_stats_t *stats);

/*
 * Reset token statistics.
 *
 * @param token     Token manager
 */
void wingo_dht_token_reset_stats(wingo_dht_token_t *token);

/* ============================================================================
 * DHT TOKEN UTILITY
 * ============================================================================ */

/*
 * Get token result name.
 *
 * @param result    Token result
 * @return          Static string
 */
const char *wingo_dht_token_result_name(wingo_dht_token_result_t result);

/*
 * Print token status.
 *
 * @param token     Token manager
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_token_print(const wingo_dht_token_t *token, FILE *f);

/*
 * Print secrets (for debugging only).
 *
 * @param token     Token manager
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_token_print_secrets(const wingo_dht_token_t *token, FILE *f);

/*
 * Compare two tokens (constant-time).
 *
 * @param a         First token
 * @param b         Second token
 * @param len       Token length
 * @return          true if equal, false otherwise
 */
bool wingo_dht_token_equal(const wingo_u8 *a, const wingo_u8 *b, wingo_size len);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_TOKEN_H */
