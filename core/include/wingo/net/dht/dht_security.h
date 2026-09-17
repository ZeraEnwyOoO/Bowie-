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

#ifndef WINGO_NET_DHT_SECURITY_H
#define WINGO_NET_DHT_SECURITY_H

/*
 * ============================================================================
 * WINGO DHT SECURITY
 * ============================================================================
 *
 * This header provides DHT security mechanisms for Bowie.
 *
 * Security layers:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT SECURITY                             │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │  Blacklist  │  │  Rate       │  │  Token      │        │
 *   │   │             │  │  Limiting   │  │  Validation │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │  Martian    │  │  Sybil      │  │  IP         │        │
 *   │   │  Address    │  │  Protection │  │  Validation │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
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
 * DHT SECURITY CONSTANTS
 * ============================================================================ */

/*
 * Maximum blacklist entries.
 */
#define WINGO_DHT_SECURITY_MAX_BLACKLIST        4096

/*
 * Blacklist entry default TTL (1 hour).
 */
#define WINGO_DHT_SECURITY_BLACKLIST_TTL        (60 * 60)

/*
 * Rate limit: messages per second per IP.
 */
#define WINGO_DHT_SECURITY_RATE_PER_IP          20

/*
 * Rate limit: messages per second per node.
 */
#define WINGO_DHT_SECURITY_RATE_PER_NODE        40

/*
 * Rate limit: global messages per second.
 */
#define WINGO_DHT_SECURITY_RATE_GLOBAL          2000

/*
 * Maximum nodes per subnet (Sybil protection).
 */
#define WINGO_DHT_SECURITY_MAX_PER_SUBNET       4

/*
 * Token size (8 bytes).
 */
#define WINGO_DHT_SECURITY_TOKEN_SIZE           8

/* ============================================================================
 * DHT SECURITY TYPES
 * ============================================================================ */

/*
 * Security result.
 */
typedef enum {
    WINGO_DHT_SECURITY_OK           = 0,   /* Allowed */
    WINGO_DHT_SECURITY_BLOCKED      = 1,   /* Blocked (blacklist) */
    WINGO_DHT_SECURITY_RATE_LIMITED = 2,   /* Rate limited */
    WINGO_DHT_SECURITY_MARTIAN      = 3,   /* Martian address */
    WINGO_DHT_SECURITY_SYBIL        = 4,   /* Sybil detected */
    WINGO_DHT_SECURITY_BAD_TOKEN    = 5,   /* Invalid token */
    WINGO_DHT_SECURITY_SPOOFED      = 6,   /* Spoofed source */
    WINGO_DHT_SECURITY_ERROR        = 7,   /* Internal error */
} wingo_dht_security_result_t;

/*
 * Blacklist reason.
 */
typedef enum {
    WINGO_DHT_BLACKLIST_REASON_NONE     = 0,
    WINGO_DHT_BLACKLIST_REASON_ABUSE    = 1,
    WINGO_DHT_BLACKLIST_REASON_SPAM     = 2,
    WINGO_DHT_BLACKLIST_REASON_INVALID  = 3,
    WINGO_DHT_BLACKLIST_REASON_MANUAL   = 4,
} wingo_dht_blacklist_reason_t;

/* ============================================================================
 * DHT SECURITY STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT security handle.
 *
 * NOTE: This is the ONLY definition of this opaque type in Bowie.
 */
typedef struct wingo_dht_security wingo_dht_security_t;

/* ============================================================================
 * DHT SECURITY LIFECYCLE
 * ============================================================================ */

/*
 * Create a new DHT security instance.
 *
 * @param max_blacklist     Maximum blacklist entries (0 = default)
 * @param enable_rate_limit Enable rate limiting
 * @param enable_sybil      Enable Sybil protection
 * @return                  Security handle, or NULL on error
 */
wingo_dht_security_t *wingo_dht_security_new(wingo_size max_blacklist,
                                              bool enable_rate_limit,
                                              bool enable_sybil);

/*
 * Free a DHT security instance.
 *
 * @param security  Security handle (NULL is safe)
 */
void wingo_dht_security_free(wingo_dht_security_t *security);

/*
 * Reset security state.
 *
 * @param security  Security handle
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_reset(wingo_dht_security_t *security);

/* ============================================================================
 * BLACKLIST
 * ============================================================================ */

/*
 * Add a node to blacklist (by ID).
 */
wingo_error_t wingo_dht_security_blacklist_add(wingo_dht_security_t *security,
                                                const wingo_dht_id_t *id,
                                                wingo_dht_blacklist_reason_t reason,
                                                wingo_i64 duration_s);

/*
 * Add an address to blacklist.
 */
wingo_error_t wingo_dht_security_blacklist_add_addr(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    wingo_dht_blacklist_reason_t reason,
    wingo_i64 duration_s);

/*
 * Remove a node from blacklist.
 */
wingo_error_t wingo_dht_security_blacklist_remove(
    wingo_dht_security_t *security,
    const wingo_dht_id_t *id);

/*
 * Remove an address from blacklist.
 */
wingo_error_t wingo_dht_security_blacklist_remove_addr(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Check if a node is blacklisted.
 */
bool wingo_dht_security_is_blacklisted(const wingo_dht_security_t *security,
                                        const wingo_dht_id_t *id);

/*
 * Check if an address is blacklisted.
 */
bool wingo_dht_security_is_blacklisted_addr(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Get blacklist entry count.
 */
wingo_size wingo_dht_security_blacklist_count(
    const wingo_dht_security_t *security);

/*
 * Clear blacklist.
 */
void wingo_dht_security_blacklist_clear(wingo_dht_security_t *security);

/*
 * Expire blacklist entries.
 */
wingo_size wingo_dht_security_blacklist_expire(
    wingo_dht_security_t *security);

/* ============================================================================
 * RATE LIMITING
 * ============================================================================ */

/*
 * Check rate limit for an address.
 */
wingo_dht_security_result_t wingo_dht_security_rate_limit_check(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Check rate limit for a node.
 */
wingo_dht_security_result_t wingo_dht_security_rate_limit_check_node(
    wingo_dht_security_t *security,
    const wingo_dht_id_t *id);

/*
 * Get rate limit count for an address.
 */
wingo_size wingo_dht_security_rate_limit_count(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Reset rate limit for an address.
 */
void wingo_dht_security_rate_limit_reset(wingo_dht_security_t *security,
                                          const wingo_addr_t *addr);

/*
 * Clear all rate limits.
 */
void wingo_dht_security_rate_limit_clear(wingo_dht_security_t *security);

/* ============================================================================
 * MARTIAN ADDRESS CHECK
 * ============================================================================ */

/*
 * Check if an address is martian (invalid).
 */
bool wingo_dht_security_is_martian(const wingo_addr_t *addr);

/*
 * Check if an IP is private.
 */
bool wingo_dht_security_is_private(const wingo_addr_t *addr);

/*
 * Check if an IP is loopback.
 */
bool wingo_dht_security_is_loopback(const wingo_addr_t *addr);

/*
 * Check if an IP is multicast.
 */
bool wingo_dht_security_is_multicast(const wingo_addr_t *addr);

/* ============================================================================
 * SYBIL PROTECTION
 * ============================================================================ */

/*
 * Check if a node can be added to routing table.
 */
wingo_dht_security_result_t wingo_dht_security_sybil_check(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Register a node in Sybil tracking.
 */
wingo_error_t wingo_dht_security_sybil_register(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Unregister a node from Sybil tracking.
 */
wingo_error_t wingo_dht_security_sybil_unregister(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Get Sybil count for a subnet.
 */
wingo_size wingo_dht_security_sybil_count(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Clear Sybil tracking.
 */
void wingo_dht_security_sybil_clear(wingo_dht_security_t *security);

/* ============================================================================
 * TOKEN VALIDATION (delegates to dht_token)
 * ============================================================================ */

/*
 * Generate a token for an address.
 */
wingo_error_t wingo_dht_security_token_generate(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    wingo_u8 *out,
    wingo_size len);

/*
 * Verify a token for an address.
 */
bool wingo_dht_security_token_verify(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    const wingo_u8 *token,
    wingo_size len);

/*
 * Rotate token secret.
 */
wingo_error_t wingo_dht_security_token_rotate(
    wingo_dht_security_t *security);

/*
 * Get time until next token rotation.
 */
wingo_i64 wingo_dht_security_token_next_rotate(
    const wingo_dht_security_t *security);

/*
 * Check if token rotation is needed.
 */
bool wingo_dht_security_token_needs_rotate(
    const wingo_dht_security_t *security);

/* ============================================================================
 * IP VALIDATION
 * ============================================================================ */

/*
 * Validate source address.
 */
wingo_dht_security_result_t wingo_dht_security_validate_source(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/*
 * Check if a source address is spoofed.
 */
bool wingo_dht_security_is_spoofed(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr);

/* ============================================================================
 * SECURITY STATISTICS
 * ============================================================================ */

/*
 * Security statistics.
 */
typedef struct {
    wingo_u64   packets_allowed;
    wingo_u64   packets_blocked;
    wingo_u64   packets_rate_limited;
    wingo_u64   packets_martian;
    wingo_u64   packets_sybil;
    wingo_u64   packets_bad_token;
    wingo_u64   packets_spoofed;
    wingo_size  blacklist_size;
    wingo_size  rate_limit_entries;
    wingo_size  sybil_subnets;
    wingo_u64   token_rotations;
} wingo_dht_security_stats_t;

/*
 * Get security statistics.
 */
wingo_error_t wingo_dht_security_get_stats(
    const wingo_dht_security_t *security,
    wingo_dht_security_stats_t *stats);

/*
 * Reset security statistics.
 */
void wingo_dht_security_reset_stats(wingo_dht_security_t *security);

/* ============================================================================
 * SECURITY UTILITY
 * ============================================================================ */

/*
 * Get security result name.
 */
const char *wingo_dht_security_result_name(
    wingo_dht_security_result_t result);

/*
 * Get blacklist reason name.
 */
const char *wingo_dht_blacklist_reason_name(
    wingo_dht_blacklist_reason_t reason);

/*
 * Print security status.
 */
void wingo_dht_security_print(const wingo_dht_security_t *security, FILE *f);

/*
 * Print blacklist.
 */
void wingo_dht_security_print_blacklist(
    const wingo_dht_security_t *security,
    FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_SECURITY_H */
