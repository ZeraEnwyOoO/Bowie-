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
 * WINGO DHT TOKEN MANAGER
 * ============================================================================
 *
 * This header provides the DHT token MANAGER for Bowie.
 *
 * IMPORTANT:
 *   wingo_dht_token_t (from dht_types.h) is a token VALUE (8 bytes).
 *   wingo_dht_token_mgr_t (this header) is the token MANAGER.
 *
 * The token manager:
 *   - Generates tokens: SHA1(secret || IP)[0:8]
 *   - Rotates secrets every 15-45 minutes
 *   - Verifies tokens against current + previous secret
 *
 * Token algorithm:
 *
 *   token = SHA1(secret || IP)[0:8]
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
#include "wingo/net/dht/dht_types.h"

/* ============================================================================
 * DHT TOKEN MANAGER CONSTANTS
 * ============================================================================ */

/*
 * Token size (8 bytes) — alias for compatibility.
 */
#define WINGO_DHT_TOKEN_SIZE_LOCAL   WINGO_DHT_TOKEN_SIZE

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
 * DHT TOKEN MANAGER TYPES
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
    wingo_u8    bytes[WINGO_DHT_TOKEN_SECRET_SIZE];
    wingo_i64   created_at;
    bool        active;
} wingo_dht_token_secret_t;

/* ============================================================================
 * DHT TOKEN MANAGER STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Token manager handle.
 *
 * NOTE: This is the ONLY definition of this opaque type in Bowie.
 *       Do NOT confuse with wingo_dht_token_t (token value).
 */
typedef struct wingo_dht_token_mgr wingo_dht_token_mgr_t;

/* ============================================================================
 * DHT TOKEN MANAGER LIFECYCLE
 * ============================================================================ */

/*
 * Create a new token manager.
 *
 * @return          Token manager, or NULL on error
 */
wingo_dht_token_mgr_t *wingo_dht_token_mgr_new(void);

/*
 * Free a token manager.
 *
 * @param mgr       Token manager (NULL is safe)
 */
void wingo_dht_token_mgr_free(wingo_dht_token_mgr_t *mgr);

/*
 * Reset token manager.
 *
 * @param mgr       Token manager
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_reset(wingo_dht_token_mgr_t *mgr);

/* ============================================================================
 * DHT TOKEN MANAGER GENERATION
 * ============================================================================ */

/*
 * Generate a token for an address.
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param out       Output token buffer (at least WINGO_DHT_TOKEN_SIZE)
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_generate(wingo_dht_token_mgr_t *mgr,
                                            const wingo_addr_t *addr,
                                            wingo_u8 *out,
                                            wingo_size len);

/*
 * Generate a token for an IP address (no port).
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param out       Output token buffer
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_generate_ip(wingo_dht_token_mgr_t *mgr,
                                               const wingo_addr_t *addr,
                                               wingo_u8 *out,
                                               wingo_size len);

/*
 * Generate a token for a specific secret.
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param secret    Secret index (0 = current, 1 = previous)
 * @param out       Output token buffer
 * @param len       Output buffer length
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_generate_secret(wingo_dht_token_mgr_t *mgr,
                                                   const wingo_addr_t *addr,
                                                   int secret,
                                                   wingo_u8 *out,
                                                   wingo_size len);

/* ============================================================================
 * DHT TOKEN MANAGER VERIFICATION
 * ============================================================================ */

/*
 * Verify a token for an address.
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param in        Token to verify
 * @param len       Token length
 * @return          WINGO_DHT_TOKEN_OK if valid, error code on failure
 */
wingo_dht_token_result_t wingo_dht_token_mgr_verify(wingo_dht_token_mgr_t *mgr,
                                                     const wingo_addr_t *addr,
                                                     const wingo_u8 *in,
                                                     wingo_size len);

/*
 * Verify a token with detailed result.
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param in        Token to verify
 * @param len       Token length
 * @param out_result Output detailed result
 * @return          true if valid, false otherwise
 */
bool wingo_dht_token_mgr_verify_ex(wingo_dht_token_mgr_t *mgr,
                                    const wingo_addr_t *addr,
                                    const wingo_u8 *in,
                                    wingo_size len,
                                    wingo_dht_token_result_t *out_result);

/*
 * Check if a token matches a specific secret.
 *
 * @param mgr       Token manager
 * @param addr      Address
 * @param secret    Secret index
 * @param in        Token to verify
 * @param len       Token length
 * @return          true if valid, false otherwise
 */
bool wingo_dht_token_mgr_verify_secret(wingo_dht_token_mgr_t *mgr,
                                        const wingo_addr_t *addr,
                                        int secret,
                                        const wingo_u8 *in,
                                        wingo_size len);

/* ============================================================================
 * DHT TOKEN MANAGER ROTATION
 * ============================================================================ */

/*
 * Rotate the token secret.
 *
 * @param mgr       Token manager
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_rotate(wingo_dht_token_mgr_t *mgr);

/*
 * Check if rotation is needed.
 *
 * @param mgr       Token manager
 * @return          true if needed, false otherwise
 */
bool wingo_dht_token_mgr_needs_rotate(const wingo_dht_token_mgr_t *mgr);

/*
 * Get time until next rotation.
 *
 * @param mgr       Token manager
 * @return          Seconds until next rotation
 */
wingo_i64 wingo_dht_token_mgr_next_rotate(const wingo_dht_token_mgr_t *mgr);

/*
 * Set rotation interval.
 *
 * @param mgr       Token manager
 * @param min_s     Minimum interval (seconds)
 * @param max_s     Maximum interval (seconds)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_set_rotation(wingo_dht_token_mgr_t *mgr,
                                                wingo_i64 min_s,
                                                wingo_i64 max_s);

/*
 * Get current rotation interval.
 *
 * @param mgr       Token manager
 * @return          Rotation interval in seconds
 */
wingo_i64 wingo_dht_token_mgr_rotation_interval(const wingo_dht_token_mgr_t *mgr);

/* ============================================================================
 * DHT TOKEN MANAGER SECRET
 * ============================================================================ */

/*
 * Get secret at index.
 *
 * @param mgr       Token manager
 * @param index     Secret index (0 = current, 1 = previous)
 * @return          Secret, or NULL if invalid index
 */
const wingo_dht_token_secret_t *wingo_dht_token_mgr_secret(
    const wingo_dht_token_mgr_t *mgr,
    int index);

/*
 * Get secret creation time.
 *
 * @param mgr       Token manager
 * @param index     Secret index
 * @return          Creation timestamp
 */
wingo_i64 wingo_dht_token_mgr_secret_created(const wingo_dht_token_mgr_t *mgr,
                                              int index);

/*
 * Get secret age.
 *
 * @param mgr       Token manager
 * @param index     Secret index
 * @return          Age in seconds
 */
wingo_i64 wingo_dht_token_mgr_secret_age(const wingo_dht_token_mgr_t *mgr,
                                          int index);

/*
 * Clear all secrets.
 *
 * @param mgr       Token manager
 */
void wingo_dht_token_mgr_secrets_clear(wingo_dht_token_mgr_t *mgr);

/* ============================================================================
 * DHT TOKEN MANAGER STATISTICS
 * ============================================================================ */

/*
 * Token statistics.
 */
typedef struct {
    wingo_u64   tokens_generated;
    wingo_u64   tokens_verified;
    wingo_u64   tokens_valid;
    wingo_u64   tokens_invalid;
    wingo_u64   tokens_expired;
    wingo_u64   tokens_wrong_addr;
    wingo_u64   rotations;
    wingo_i64   last_rotation;
    wingo_i64   next_rotation;
    wingo_i64   current_age;
    wingo_i64   previous_age;
} wingo_dht_token_stats_t;

/*
 * Get token statistics.
 *
 * @param mgr       Token manager
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_token_mgr_get_stats(const wingo_dht_token_mgr_t *mgr,
                                             wingo_dht_token_stats_t *stats);

/*
 * Reset token statistics.
 *
 * @param mgr       Token manager
 */
void wingo_dht_token_mgr_reset_stats(wingo_dht_token_mgr_t *mgr);

/* ============================================================================
 * DHT TOKEN MANAGER UTILITY
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
 * @param mgr       Token manager
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_token_mgr_print(const wingo_dht_token_mgr_t *mgr, FILE *f);

/*
 * Print secrets (for debugging only).
 *
 * @param mgr       Token manager
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_token_mgr_print_secrets(const wingo_dht_token_mgr_t *mgr, FILE *f);

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
