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
 * This header provides security for Bowie DHT:
 *   - Blacklist (malicious nodes)
 *   - Martian check (invalid addresses)
 *   - Rate limiting (per-IP)
 *   - Node verification
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    DHT SECURITY                             │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │  Blacklist  │  │  Martian    │  │   Rate      │        │
 *   │   │             │  │   Check     │  │   Limit     │        │
 *   │   │  - Add      │  │             │  │             │        │
 *   │   │  - Remove   │  │  - IPv4     │  │  - Per-IP   │        │
 *   │   │  - Check    │  │  - IPv6     │  │  - Global   │        │
│   │   │             │  │             │  │             │        │
│   │   └─────────────┘  └─────────────┘  └─────────────┘        │
│   │                                                             │
│   │   ┌─────────────────────────────────────────────────────┐   │
│   │   │              NODE VERIFICATION                      │   │
│   │   │                                                     │   │
│   │   │   - ID Valid                                        │   │
│   │   │   - Not Self                                        │   │
│   │   │   - Not Blacklisted                                 │   │
│   │   │   - Not Martian                                     │   │
│   │   │   - Rate OK                                         │   │
│   │   └─────────────────────────────────────────────────────┘   │
│   │                                                             │
│   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/net/socket.h"
#include "wingo/net/dht.h"

/* ============================================================================
 * DHT SECURITY CONSTANTS
 * ============================================================================ */

/*
 * Default blacklist size.
 */
#define WINGO_DHT_BLACKLIST_DEFAULT_SIZE    10

/*
 * Maximum blacklist size.
 */
#define WINGO_DHT_BLACKLIST_MAX_SIZE        1024

/*
 * Default rate limit (messages per second).
 */
#define WINGO_DHT_RATE_LIMIT_DEFAULT        100

/*
 * Default rate window (seconds).
 */
#define WINGO_DHT_RATE_WINDOW_DEFAULT       1

/*
 * Default per-IP rate limit (messages per window).
 */
#define WINGO_DHT_RATE_PER_IP_DEFAULT       10

/*
 * Rate entry expire time (seconds).
 */
#define WINGO_DHT_RATE_ENTRY_EXPIRE         (60 * 5)

/* ============================================================================
 * DHT SECURITY TYPES
 * ============================================================================ */

/*
 * Blacklist entry.
 */
typedef struct {
    wingo_dht_id_t      id;         /* Node ID (may be zero) */
    wingo_addr_t        addr;       /* Node address */
    wingo_i64           added;      /* When added */
    wingo_i64           expires;    /* When expires (0 = never) */
    char                reason[128];/* Reason (may be empty) */
} wingo_dht_blacklist_entry_t;

/*
 * Rate entry.
 */
typedef struct {
    wingo_addr_t        addr;       /* IP address */
    wingo_u32           count;      /* Message count */
    wingo_i64           window_start;/* Window start time */
    wingo_i64           last_seen;  /* Last seen time */
} wingo_dht_rate_entry_t;

/* ============================================================================
 * DHT SECURITY STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * DHT security handle.
 */
typedef struct wingo_dht_security wingo_dht_security_t;

/* ============================================================================
 * SECURITY LIFECYCLE
 * ============================================================================ */

/*
 * Create a new security instance.
 *
 * @param blacklist_size Maximum blacklist size
 * @param rate_limit     Rate limit (messages per second)
 * @return               Security instance, or NULL on error
 */
wingo_dht_security_t *wingo_dht_security_new(wingo_size blacklist_size,
                                              wingo_size rate_limit);

/*
 * Free a security instance.
 *
 * @param sec       Security instance (NULL is safe)
 */
void wingo_dht_security_free(wingo_dht_security_t *sec);

/* ============================================================================
 * BLACKLIST
 * ============================================================================ */

/*
 * Add an address to blacklist.
 *
 * @param sec       Security instance
 * @param addr      Address to blacklist
 * @param reason    Reason (may be NULL)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_blacklist_addr(wingo_dht_security_t *sec,
                                                 const wingo_addr_t *addr,
                                                 const char *reason);

/*
 * Add a node ID to blacklist.
 *
 * @param sec       Security instance
 * @param id        Node ID to blacklist
 * @param reason    Reason (may be NULL)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_blacklist_id(wingo_dht_security_t *sec,
                                               const wingo_dht_id_t *id,
                                               const char *reason);

/*
 * Add a node to blacklist (ID + address).
 *
 * @param sec       Security instance
 * @param id        Node ID (may be NULL)
 * @param addr      Address (may be NULL)
 * @param reason    Reason (may be NULL)
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_blacklist(wingo_dht_security_t *sec,
                                            const wingo_dht_id_t *id,
                                            const wingo_addr_t *addr,
                                            const char *reason);

/*
 * Remove an address from blacklist.
 *
 * @param sec       Security instance
 * @param addr      Address
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_unblacklist_addr(wingo_dht_security_t *sec,
                                                   const wingo_addr_t *addr);

/*
 * Remove a node ID from blacklist.
 *
 * @param sec       Security instance
 * @param id        Node ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_unblacklist_id(wingo_dht_security_t *sec,
                                                 const wingo_dht_id_t *id);

/*
 * Check if address is blacklisted.
 *
 * @param sec       Security instance
 * @param addr      Address
 * @return          true if blacklisted, false otherwise
 */
bool wingo_dht_security_is_blacklisted_addr(const wingo_dht_security_t *sec,
                                             const wingo_addr_t *addr);

/*
 * Check if node ID is blacklisted.
 *
 * @param sec       Security instance
 * @param id        Node ID
 * @return          true if blacklisted, false otherwise
 */
bool wingo_dht_security_is_blacklisted_id(const wingo_dht_security_t *sec,
                                           const wingo_dht_id_t *id);

/*
 * Check if node is blacklisted.
 *
 * @param sec       Security instance
 * @param id        Node ID (may be NULL)
 * @param addr      Address (may be NULL)
 * @return          true if blacklisted, false otherwise
 */
bool wingo_dht_security_is_blacklisted(const wingo_dht_security_t *sec,
                                        const wingo_dht_id_t *id,
                                        const wingo_addr_t *addr);

/*
 * Get blacklist entry count.
 *
 * @param sec       Security instance
 * @return          Number of entries
 */
wingo_size wingo_dht_security_blacklist_count(
    const wingo_dht_security_t *sec);

/*
 * Get blacklist entry at index.
 *
 * @param sec       Security instance
 * @param index     Index
 * @return          Entry, or NULL if out of range
 */
const wingo_dht_blacklist_entry_t *wingo_dht_security_blacklist_entry(
    const wingo_dht_security_t *sec,
    wingo_size index);

/*
 * Clear blacklist.
 *
 * @param sec       Security instance
 */
void wingo_dht_security_blacklist_clear(wingo_dht_security_t *sec);

/*
 * Expire blacklist entries.
 *
 * @param sec       Security instance
 * @return          Number of entries expired
 */
wingo_size wingo_dht_security_blacklist_expire(wingo_dht_security_t *sec);

/* ============================================================================
 * MARTIAN CHECK
 * ============================================================================ */

/*
 * Check if address is martian (invalid).
 *
 * Martian addresses:
 *   - 0.0.0.0/8, 127.0.0.0/8, 224.0.0.0/4, 240.0.0.0/4
 *   - ::, ::1, ff00::/8, fe80::/10
 *   - IPv4-mapped IPv6
 *
 * @param addr      Address to check
 * @return          true if martian, false otherwise
 */
bool wingo_dht_security_is_martian(const wingo_addr_t *addr);

/*
 * Check if address is a valid public address.
 *
 * @param addr      Address to check
 * @return          true if public, false otherwise
 */
bool wingo_dht_security_is_public(const wingo_addr_t *addr);

/*
 * Check if address is private (RFC 1918 / RFC 4193).
 *
 * @param addr      Address to check
 * @return          true if private, false otherwise
 */
bool wingo_dht_security_is_private(const wingo_addr_t *addr);

/* ============================================================================
 * RATE LIMIT
 * ============================================================================ */

/*
 * Check rate limit for an address.
 *
 * @param sec       Security instance
 * @param addr      Address
 * @return          true if rate OK, false otherwise
 */
bool wingo_dht_security_rate_ok(wingo_dht_security_t *sec,
                                 const wingo_addr_t *addr);

/*
 * Check global rate limit.
 *
 * @param sec       Security instance
 * @return          true if rate OK, false otherwise
 */
bool wingo_dht_security_rate_ok_global(wingo_dht_security_t *sec);

/*
 * Check if address is rate limited.
 *
 * @param sec       Security instance
 * @param addr      Address
 * @return          true if rate limited, false otherwise
 */
bool wingo_dht_security_is_rate_limited(const wingo_dht_security_t *sec,
                                         const wingo_addr_t *addr);

/*
 * Get rate entry count.
 *
 * @param sec       Security instance
 * @return          Number of entries
 */
wingo_size wingo_dht_security_rate_count(const wingo_dht_security_t *sec);

/*
 * Expire rate entries.
 *
 * @param sec       Security instance
 * @return          Number of entries expired
 */
wingo_size wingo_dht_security_rate_expire(wingo_dht_security_t *sec);

/*
 * Clear rate entries.
 *
 * @param sec       Security instance
 */
void wingo_dht_security_rate_clear(wingo_dht_security_t *sec);

/*
 * Set rate limit.
 *
 * @param sec       Security instance
 * @param rate      Rate limit (messages per second)
 */
void wingo_dht_security_set_rate_limit(wingo_dht_security_t *sec,
                                        wingo_size rate);

/*
 * Get rate limit.
 *
 * @param sec       Security instance
 * @return          Rate limit
 */
wingo_size wingo_dht_security_get_rate_limit(
    const wingo_dht_security_t *sec);

/*
 * Set per-IP rate limit.
 *
 * @param sec       Security instance
 * @param rate      Per-IP rate limit (messages per window)
 */
void wingo_dht_security_set_rate_per_ip(wingo_dht_security_t *sec,
                                         wingo_size rate);

/*
 * Get per-IP rate limit.
 *
 * @param sec       Security instance
 * @return          Per-IP rate limit
 */
wingo_size wingo_dht_security_get_rate_per_ip(
    const wingo_dht_security_t *sec);

/* ============================================================================
 * NODE VERIFICATION
 * ============================================================================ */

/*
 * Verification result.
 */
typedef enum {
    WINGO_DHT_VERIFY_OK              = 0,
    WINGO_DHT_VERIFY_INVALID_ID      = 1,
    WINGO_DHT_VERIFY_SELF            = 2,
    WINGO_DHT_VERIFY_BLACKLISTED     = 3,
    WINGO_DHT_VERIFY_MARTIAN         = 4,
    WINGO_DHT_VERIFY_RATE_LIMITED    = 5,
    WINGO_DHT_VERIFY_INVALID_ADDR    = 6,
} wingo_dht_verify_result_t;

/*
 * Verify a node.
 *
 * @param sec       Security instance
 * @param my_id     My node ID
 * @param id        Node ID (may be NULL)
 * @param addr      Node address (may be NULL)
 * @return          Verification result
 */
wingo_dht_verify_result_t wingo_dht_security_verify_node(
    wingo_dht_security_t *sec,
    const wingo_dht_id_t *my_id,
    const wingo_dht_id_t *id,
    const wingo_addr_t *addr);

/*
 * Verify a node ID.
 *
 * @param sec       Security instance
 * @param my_id     My node ID
 * @param id        Node ID
 * @return          Verification result
 */
wingo_dht_verify_result_t wingo_dht_security_verify_id(
    const wingo_dht_security_t *sec,
    const wingo_dht_id_t *my_id,
    const wingo_dht_id_t *id);

/*
 * Verify an address.
 *
 * @param sec       Security instance
 * @param addr      Address
 * @return          Verification result
 */
wingo_dht_verify_result_t wingo_dht_security_verify_addr(
    const wingo_dht_security_t *sec,
    const wingo_addr_t *addr);

/*
 * Get verification result name.
 *
 * @param result    Verification result
 * @return          Static string
 */
const char *wingo_dht_verify_result_name(wingo_dht_verify_result_t result);

/* ============================================================================
 * SECURITY STATISTICS
 * ============================================================================ */

/*
 * Security statistics.
 */
typedef struct {
    wingo_u64   blacklist_adds;         /* Blacklist additions */
    wingo_u64   blacklist_removes;      /* Blacklist removals */
    wingo_u64   blacklist_hits;         /* Blacklist hits */
    wingo_u64   martian_hits;           /* Martian hits */
    wingo_u64   rate_limited;           /* Rate limited */
    wingo_u64   verify_ok;              /* Verification OK */
    wingo_u64   verify_failed;          /* Verification failed */
    wingo_size  blacklist_size;         /* Current blacklist size */
    wingo_size  rate_size;              /* Current rate size */
} wingo_dht_security_stats_t;

/*
 * Get security statistics.
 *
 * @param sec       Security instance
 * @param stats     Output statistics
 * @return          WINGO_SUCCESS on success, error code on failure
 */
wingo_error_t wingo_dht_security_get_stats(
    const wingo_dht_security_t *sec,
    wingo_dht_security_stats_t *stats);

/*
 * Reset security statistics.
 *
 * @param sec       Security instance
 */
void wingo_dht_security_reset_stats(wingo_dht_security_t *sec);

/* ============================================================================
 * SECURITY UTILITY
 * ============================================================================ */

/*
 * Print security status.
 *
 * @param sec       Security instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_security_print(const wingo_dht_security_t *sec, FILE *f);

/*
 * Print blacklist.
 *
 * @param sec       Security instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_security_print_blacklist(const wingo_dht_security_t *sec,
                                         FILE *f);

/*
 * Print rate entries.
 *
 * @param sec       Security instance
 * @param f         Output file (NULL = stderr)
 */
void wingo_dht_security_print_rate(const wingo_dht_security_t *sec,
                                    FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_NET_DHT_SECURITY_H */
