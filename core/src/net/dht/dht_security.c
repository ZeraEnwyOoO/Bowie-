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
 * DHT security implementation for Bowie.
 *
 * This file provides:
 *   - Blacklist (node ID + address)
 *   - Rate limiting (per IP)
 *   - Martian address detection
 *   - Sybil protection
 *   - Token validation (delegates to dht_token)
 *
 * NOTE: Token management is implemented in dht_token.c.
 *       This file delegates token operations to dht_token.
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_security.h"
#include "wingo/net/dht/dht_token.h"
#include "wingo/net/dht/dht_types.h"
#include "wingo/log.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Blacklist entry.
 */
typedef struct blacklist_entry {
    wingo_dht_id_t              id;
    wingo_addr_t               *addr;
    wingo_dht_blacklist_reason_t reason;
    wingo_i64                   expires_at;
    struct blacklist_entry     *next;
} blacklist_entry_t;

/*
 * Rate limit entry.
 */
typedef struct rate_entry {
    wingo_addr_t           *addr;
    wingo_u64               tokens;
    wingo_i64               last_refill;
    wingo_u64               message_count;
    wingo_i64               window_start;
    struct rate_entry      *next;
} rate_entry_t;

/*
 * Sybil subnet entry.
 */
typedef struct sybil_entry {
    wingo_u32               subnet;
    wingo_size              count;
    struct sybil_entry     *next;
} sybil_entry_t;

/*
 * Security (concrete).
 */
struct wingo_dht_security {
    /* ----- Blacklist ----- */
    struct blacklist_entry     *blacklist_head;
    wingo_size                  blacklist_count;
    wingo_size                  max_blacklist;

    /* ----- Rate limiting ----- */
    struct rate_entry          *rate_head;
    wingo_size                  rate_count;
    bool                        enable_rate_limit;

    /* ----- Sybil protection ----- */
    struct sybil_entry         *sybil_head;
    wingo_size                  sybil_count;
    bool                        enable_sybil;

    /* ----- Token ----- */
    wingo_dht_token_t          *token;

    /* ----- Statistics ----- */
    wingo_dht_security_stats_t  stats;
};

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

#define RATE_MAX_ENTRIES        4096
#define SYBIL_SUBNET_SIZE       256
#define SYBIL_MAX_PER_SUBNET    4

/* ============================================================================
 * INTERNAL HELPERS — ADDRESS
 * ============================================================================ */

/*
 * Get IPv4 subnet (/24) from address.
 */
static wingo_u32 addr_to_subnet(const wingo_addr_t *addr)
{
    char ip_str[WINGO_ADDR_STR_MAX];
    unsigned int a, b, c, d;

    if (addr == NULL || !wingo_addr_is_ipv4(addr)) {
        return 0;
    }

    if (wingo_addr_ip_str(addr, ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
        return 0;
    }

    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return 0;
    }

    return (a << 24) | (b << 16) | (c << 8);
}

/* ============================================================================
 * INTERNAL HELPERS — BLACKLIST
 * ============================================================================ */

static blacklist_entry_t *blacklist_find_id(wingo_dht_security_t *security,
                                             const wingo_dht_id_t *id)
{
    blacklist_entry_t *entry;

    if (security == NULL || id == NULL) {
        return NULL;
    }

    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (memcmp(entry->id.bytes, id->bytes, WINGO_DHT_ID_SIZE) == 0 &&
            entry->addr == NULL) {
            return entry;
        }
    }

    return NULL;
}

static blacklist_entry_t *blacklist_find_addr(wingo_dht_security_t *security,
                                               const wingo_addr_t *addr)
{
    blacklist_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return NULL;
    }

    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            return entry;
        }
    }

    return NULL;
}

static wingo_error_t blacklist_add(wingo_dht_security_t *security,
                                    const wingo_dht_id_t *id,
                                    const wingo_addr_t *addr,
                                    wingo_dht_blacklist_reason_t reason,
                                    wingo_i64 duration_s)
{
    blacklist_entry_t *entry;

    if (security == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (security->blacklist_count >= security->max_blacklist) {
        WINGO_LOG_WARN("DHT security: blacklist full");
        return WINGO_ERR_OUT_OF_RANGE;
    }

    entry = calloc(1, sizeof(blacklist_entry_t));
    if (entry == NULL) {
        return WINGO_ERR_NOMEM;
    }

    if (id != NULL) {
        memcpy(&entry->id, id, sizeof(wingo_dht_id_t));
    } else {
        memset(&entry->id, 0, sizeof(wingo_dht_id_t));
    }

    if (addr != NULL) {
        entry->addr = wingo_addr_copy(addr);
        if (entry->addr == NULL) {
            free(entry);
            return WINGO_ERR_NOMEM;
        }
    } else {
        entry->addr = NULL;
    }

    entry->reason = reason;

    if (duration_s <= 0) {
        duration_s = WINGO_DHT_SECURITY_BLACKLIST_TTL;
    }

    entry->expires_at = wingo_time_now() + duration_s;

    entry->next = security->blacklist_head;
    security->blacklist_head = entry;
    security->blacklist_count++;

    return WINGO_SUCCESS;
}

static void blacklist_remove(wingo_dht_security_t *security,
                              blacklist_entry_t *prev,
                              blacklist_entry_t *entry)
{
    if (security == NULL || entry == NULL) {
        return;
    }

    if (prev == NULL) {
        security->blacklist_head = entry->next;
    } else {
        prev->next = entry->next;
    }

    if (entry->addr != NULL) {
        wingo_addr_free(entry->addr);
    }

    free(entry);
    security->blacklist_count--;
}

/* ============================================================================
 * INTERNAL HELPERS — RATE LIMIT
 * ============================================================================ */

static rate_entry_t *rate_find(wingo_dht_security_t *security,
                                const wingo_addr_t *addr)
{
    rate_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return NULL;
    }

    for (entry = security->rate_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            return entry;
        }
    }

    return NULL;
}

static rate_entry_t *rate_get_or_create(wingo_dht_security_t *security,
                                         const wingo_addr_t *addr)
{
    rate_entry_t *entry;

    entry = rate_find(security, addr);
    if (entry != NULL) {
        return entry;
    }

    if (security->rate_count >= RATE_MAX_ENTRIES) {
        return NULL;
    }

    entry = calloc(1, sizeof(rate_entry_t));
    if (entry == NULL) {
        return NULL;
    }

    entry->addr = wingo_addr_copy(addr);
    if (entry->addr == NULL) {
        free(entry);
        return NULL;
    }

    entry->tokens = WINGO_DHT_SECURITY_RATE_PER_IP;
    entry->last_refill = wingo_time_now();
    entry->message_count = 0;
    entry->window_start = wingo_time_now();

    entry->next = security->rate_head;
    security->rate_head = entry;
    security->rate_count++;

    return entry;
}

static void rate_refill(rate_entry_t *entry, wingo_i64 now)
{
    wingo_i64 elapsed;

    if (entry == NULL) {
        return;
    }

    elapsed = now - entry->last_refill;
    if (elapsed <= 0) {
        return;
    }

    {
        wingo_u64 refill = (wingo_u64)elapsed;

        entry->tokens += refill;

        if (entry->tokens > WINGO_DHT_SECURITY_RATE_PER_IP) {
            entry->tokens = WINGO_DHT_SECURITY_RATE_PER_IP;
        }

        entry->last_refill = now;
    }
}

/* ============================================================================
 * INTERNAL HELPERS — SYBIL
 * ============================================================================ */

static sybil_entry_t *sybil_find(wingo_dht_security_t *security,
                                  wingo_u32 subnet)
{
    sybil_entry_t *entry;

    if (security == NULL) {
        return NULL;
    }

    for (entry = security->sybil_head; entry != NULL; entry = entry->next) {
        if (entry->subnet == subnet) {
            return entry;
        }
    }

    return NULL;
}

static sybil_entry_t *sybil_get_or_create(wingo_dht_security_t *security,
                                           wingo_u32 subnet)
{
    sybil_entry_t *entry;

    entry = sybil_find(security, subnet);
    if (entry != NULL) {
        return entry;
    }

    entry = calloc(1, sizeof(sybil_entry_t));
    if (entry == NULL) {
        return NULL;
    }

    entry->subnet = subnet;
    entry->count = 0;

    entry->next = security->sybil_head;
    security->sybil_head = entry;
    security->sybil_count++;

    return entry;
}

/* ============================================================================
 * DHT SECURITY LIFECYCLE
 * ============================================================================ */

wingo_dht_security_t *wingo_dht_security_new(wingo_size max_blacklist,
                                              bool enable_rate_limit,
                                              bool enable_sybil)
{
    wingo_dht_security_t *security;

    security = calloc(1, sizeof(wingo_dht_security_t));
    if (security == NULL) {
        return NULL;
    }

    if (max_blacklist == 0) {
        max_blacklist = WINGO_DHT_SECURITY_MAX_BLACKLIST;
    }

    security->max_blacklist = max_blacklist;
    security->enable_rate_limit = enable_rate_limit;
    security->enable_sybil = enable_sybil;

    security->token = wingo_dht_token_new();
    if (security->token == NULL) {
        free(security);
        return NULL;
    }

    WINGO_LOG_DEBUG("DHT security created (max_blacklist=%zu, rate=%s, sybil=%s)",
                    max_blacklist,
                    enable_rate_limit ? "on" : "off",
                    enable_sybil ? "on" : "off");

    return security;
}

void wingo_dht_security_free(wingo_dht_security_t *security)
{
    blacklist_entry_t *entry, *next;
    rate_entry_t *rate, *rate_next;
    sybil_entry_t *sybil, *sybil_next;

    if (security == NULL) {
        return;
    }

    entry = security->blacklist_head;
    while (entry != NULL) {
        next = entry->next;
        if (entry->addr != NULL) {
            wingo_addr_free(entry->addr);
        }
        free(entry);
        entry = next;
    }

    rate = security->rate_head;
    while (rate != NULL) {
        rate_next = rate->next;
        if (rate->addr != NULL) {
            wingo_addr_free(rate->addr);
        }
        free(rate);
        rate = rate_next;
    }

    sybil = security->sybil_head;
    while (sybil != NULL) {
        sybil_next = sybil->next;
        free(sybil);
        sybil = sybil_next;
    }

    if (security->token != NULL) {
        wingo_dht_token_free(security->token);
    }

    free(security);
}

wingo_error_t wingo_dht_security_reset(wingo_dht_security_t *security)
{
    blacklist_entry_t *entry, *next;
    rate_entry_t *rate, *rate_next;
    sybil_entry_t *sybil, *sybil_next;

    if (security == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    entry = security->blacklist_head;
    while (entry != NULL) {
        next = entry->next;
        if (entry->addr != NULL) {
            wingo_addr_free(entry->addr);
        }
        free(entry);
        entry = next;
    }
    security->blacklist_head = NULL;
    security->blacklist_count = 0;

    rate = security->rate_head;
    while (rate != NULL) {
        rate_next = rate->next;
        if (rate->addr != NULL) {
            wingo_addr_free(rate->addr);
        }
        free(rate);
        rate = rate_next;
    }
    security->rate_head = NULL;
    security->rate_count = 0;

    sybil = security->sybil_head;
    while (sybil != NULL) {
        sybil_next = sybil->next;
        free(sybil);
        sybil = sybil_next;
    }
    security->sybil_head = NULL;
    security->sybil_count = 0;

    /* Reset token manager too */
    if (security->token != NULL) {
        wingo_dht_token_reset(security->token);
    }

    memset(&security->stats, 0, sizeof(security->stats));

    return WINGO_SUCCESS;
}

/* ============================================================================
 * BLACKLIST
 * ============================================================================ */

wingo_error_t wingo_dht_security_blacklist_add(wingo_dht_security_t *security,
                                                const wingo_dht_id_t *id,
                                                wingo_dht_blacklist_reason_t reason,
                                                wingo_i64 duration_s)
{
    if (security == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (blacklist_find_id(security, id) != NULL) {
        return WINGO_SUCCESS;
    }

    return blacklist_add(security, id, NULL, reason, duration_s);
}

wingo_error_t wingo_dht_security_blacklist_add_addr(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    wingo_dht_blacklist_reason_t reason,
    wingo_i64 duration_s)
{
    if (security == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (blacklist_find_addr(security, addr) != NULL) {
        return WINGO_SUCCESS;
    }

    return blacklist_add(security, NULL, addr, reason, duration_s);
}

wingo_error_t wingo_dht_security_blacklist_remove(
    wingo_dht_security_t *security,
    const wingo_dht_id_t *id)
{
    blacklist_entry_t *entry, *prev;

    if (security == NULL || id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    prev = NULL;
    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (memcmp(entry->id.bytes, id->bytes, WINGO_DHT_ID_SIZE) == 0 &&
            entry->addr == NULL) {
            blacklist_remove(security, prev, entry);
            return WINGO_SUCCESS;
        }
        prev = entry;
    }

    return WINGO_ERR_NOT_FOUND;
}

wingo_error_t wingo_dht_security_blacklist_remove_addr(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    blacklist_entry_t *entry, *prev;

    if (security == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    prev = NULL;
    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            blacklist_remove(security, prev, entry);
            return WINGO_SUCCESS;
        }
        prev = entry;
    }

    return WINGO_ERR_NOT_FOUND;
}

bool wingo_dht_security_is_blacklisted(const wingo_dht_security_t *security,
                                        const wingo_dht_id_t *id)
{
    blacklist_entry_t *entry;

    if (security == NULL || id == NULL) {
        return false;
    }

    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (memcmp(entry->id.bytes, id->bytes, WINGO_DHT_ID_SIZE) == 0 &&
            entry->addr == NULL) {
            if (entry->expires_at > 0 &&
                wingo_time_now() >= entry->expires_at) {
                return false;
            }
            return true;
        }
    }

    return false;
}

bool wingo_dht_security_is_blacklisted_addr(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    blacklist_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return false;
    }

    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            if (entry->expires_at > 0 &&
                wingo_time_now() >= entry->expires_at) {
                return false;
            }
            return true;
        }
    }

    return false;
}

wingo_size wingo_dht_security_blacklist_count(
    const wingo_dht_security_t *security)
{
    if (security == NULL) {
        return 0;
    }

    return security->blacklist_count;
}

void wingo_dht_security_blacklist_clear(wingo_dht_security_t *security)
{
    blacklist_entry_t *entry, *next;

    if (security == NULL) {
        return;
    }

    entry = security->blacklist_head;
    while (entry != NULL) {
        next = entry->next;
        if (entry->addr != NULL) {
            wingo_addr_free(entry->addr);
        }
        free(entry);
        entry = next;
    }

    security->blacklist_head = NULL;
    security->blacklist_count = 0;
}

wingo_size wingo_dht_security_blacklist_expire(
    wingo_dht_security_t *security)
{
    blacklist_entry_t *entry, *prev, *next;
    wingo_size expired = 0;
    wingo_i64 now;

    if (security == NULL) {
        return 0;
    }

    now = wingo_time_now();

    prev = NULL;
    entry = security->blacklist_head;

    while (entry != NULL) {
        next = entry->next;

        if (entry->expires_at > 0 && now >= entry->expires_at) {
            blacklist_remove(security, prev, entry);
            expired++;
        } else {
            prev = entry;
        }

        entry = next;
    }

    return expired;
}
/* ============================================================================
 * RATE LIMITING
 * ============================================================================ */

/*
 * Check rate limit for an address.
 *
 * Uses token bucket: 1 token per second, max WINGO_DHT_SECURITY_RATE_PER_IP.
 */
wingo_dht_security_result_t wingo_dht_security_rate_limit_check(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    rate_entry_t *entry;
    wingo_i64 now;

    if (security == NULL || addr == NULL) {
        return WINGO_DHT_SECURITY_ERROR;
    }

    if (!security->enable_rate_limit) {
        return WINGO_DHT_SECURITY_OK;
    }

    now = wingo_time_now();
    entry = rate_get_or_create(security, addr);
    if (entry == NULL) {
        /* Can't track → allow (fail open) */
        return WINGO_DHT_SECURITY_OK;
    }

    rate_refill(entry, now);

    if (entry->tokens == 0) {
        security->stats.packets_rate_limited++;
        return WINGO_DHT_SECURITY_RATE_LIMITED;
    }

    entry->tokens--;
    entry->message_count++;

    return WINGO_DHT_SECURITY_OK;
}

/*
 * Check rate limit for a node.
 *
 * NOTE: We don't track per-node rate limits separately.
 *       Node rate limit is approximated by address-based limit.
 *
 *       This function requires a mapping from node ID to address,
 *       which is not available at this layer. Callers should use
 *       wingo_dht_security_rate_limit_check() with the address.
 *
 *       For API compatibility, we return WINGO_DHT_SECURITY_ERROR
 *       when we cannot determine the address, so callers know
 *       to fall back to address-based check.
 */
wingo_dht_security_result_t wingo_dht_security_rate_limit_check_node(
    wingo_dht_security_t *security,
    const wingo_dht_id_t *id)
{
    if (security == NULL || id == NULL) {
        return WINGO_DHT_SECURITY_ERROR;
    }

    if (!security->enable_rate_limit) {
        return WINGO_DHT_SECURITY_OK;
    }

    /*
     * We don't have ID → address mapping here.
     * Return ERROR so caller knows to use address-based check.
     */
    return WINGO_DHT_SECURITY_ERROR;
}

/*
 * Get rate limit count for an address.
 */
wingo_size wingo_dht_security_rate_limit_count(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    rate_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return 0;
    }

    for (entry = security->rate_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            return (wingo_size)entry->message_count;
        }
    }

    return 0;
}

/*
 * Reset rate limit for an address.
 */
void wingo_dht_security_rate_limit_reset(wingo_dht_security_t *security,
                                          const wingo_addr_t *addr)
{
    rate_entry_t *entry, *prev;

    if (security == NULL || addr == NULL) {
        return;
    }

    prev = NULL;
    for (entry = security->rate_head; entry != NULL; entry = entry->next) {
        if (entry->addr != NULL && wingo_addr_cmp(entry->addr, addr) == 0) {
            if (prev == NULL) {
                security->rate_head = entry->next;
            } else {
                prev->next = entry->next;
            }

            wingo_addr_free(entry->addr);
            free(entry);
            security->rate_count--;
            return;
        }
        prev = entry;
    }
}

/*
 * Clear all rate limits.
 */
void wingo_dht_security_rate_limit_clear(wingo_dht_security_t *security)
{
    rate_entry_t *entry, *next;

    if (security == NULL) {
        return;
    }

    entry = security->rate_head;
    while (entry != NULL) {
        next = entry->next;
        if (entry->addr != NULL) {
            wingo_addr_free(entry->addr);
        }
        free(entry);
        entry = next;
    }

    security->rate_head = NULL;
    security->rate_count = 0;
}

/* ============================================================================
 * MARTIAN ADDRESS CHECK
 * ============================================================================ */

bool wingo_dht_security_is_martian(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return true;
    }

    return wingo_addr_is_martian(addr);
}

bool wingo_dht_security_is_private(const wingo_addr_t *addr)
{
    char ip_str[WINGO_ADDR_STR_MAX];
    unsigned int a, b, c, d;

    if (addr == NULL || !wingo_addr_is_ipv4(addr)) {
        return false;
    }

    if (wingo_addr_ip_str(addr, ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
        return false;
    }

    if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return false;
    }

    /* 10.0.0.0/8 */
    if (a == 10) return true;

    /* 172.16.0.0/12 */
    if (a == 172 && b >= 16 && b <= 31) return true;

    /* 192.168.0.0/16 */
    if (a == 192 && b == 168) return true;

    return false;
}

bool wingo_dht_security_is_loopback(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    return wingo_addr_is_loopback(addr);
}

bool wingo_dht_security_is_multicast(const wingo_addr_t *addr)
{
    if (addr == NULL) {
        return false;
    }

    return wingo_addr_is_multicast(addr);
}

/* ============================================================================
 * SYBIL PROTECTION
 * ============================================================================ */

wingo_dht_security_result_t wingo_dht_security_sybil_check(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    wingo_u32 subnet;
    sybil_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return WINGO_DHT_SECURITY_ERROR;
    }

    if (!security->enable_sybil) {
        return WINGO_DHT_SECURITY_OK;
    }

    if (!wingo_addr_is_ipv4(addr)) {
        return WINGO_DHT_SECURITY_OK;
    }

    subnet = addr_to_subnet(addr);
    if (subnet == 0) {
        return WINGO_DHT_SECURITY_OK;
    }

    entry = sybil_find(security, subnet);
    if (entry == NULL) {
        return WINGO_DHT_SECURITY_OK;
    }

    if (entry->count >= SYBIL_MAX_PER_SUBNET) {
        security->stats.packets_sybil++;
        return WINGO_DHT_SECURITY_SYBIL;
    }

    return WINGO_DHT_SECURITY_OK;
}

wingo_error_t wingo_dht_security_sybil_register(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    wingo_u32 subnet;
    sybil_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!security->enable_sybil) {
        return WINGO_SUCCESS;
    }

    if (!wingo_addr_is_ipv4(addr)) {
        return WINGO_SUCCESS;
    }

    subnet = addr_to_subnet(addr);
    if (subnet == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    entry = sybil_get_or_create(security, subnet);
    if (entry == NULL) {
        return WINGO_ERR_NOMEM;
    }

    entry->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_security_sybil_unregister(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    wingo_u32 subnet;
    sybil_entry_t *entry, *prev;

    if (security == NULL || addr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!security->enable_sybil) {
        return WINGO_SUCCESS;
    }

    if (!wingo_addr_is_ipv4(addr)) {
        return WINGO_SUCCESS;
    }

    subnet = addr_to_subnet(addr);
    if (subnet == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    prev = NULL;
    for (entry = security->sybil_head; entry != NULL; entry = entry->next) {
        if (entry->subnet == subnet) {
            if (entry->count > 0) {
                entry->count--;
            }

            if (entry->count == 0) {
                if (prev == NULL) {
                    security->sybil_head = entry->next;
                } else {
                    prev->next = entry->next;
                }
                free(entry);
                security->sybil_count--;
            }

            return WINGO_SUCCESS;
        }
        prev = entry;
    }

    return WINGO_ERR_NOT_FOUND;
}

wingo_size wingo_dht_security_sybil_count(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    wingo_u32 subnet;
    sybil_entry_t *entry;

    if (security == NULL || addr == NULL) {
        return 0;
    }

    if (!wingo_addr_is_ipv4(addr)) {
        return 0;
    }

    subnet = addr_to_subnet(addr);
    if (subnet == 0) {
        return 0;
    }

    for (entry = security->sybil_head; entry != NULL; entry = entry->next) {
        if (entry->subnet == subnet) {
            return entry->count;
        }
    }

    return 0;
}

void wingo_dht_security_sybil_clear(wingo_dht_security_t *security)
{
    sybil_entry_t *entry, *next;

    if (security == NULL) {
        return;
    }

    entry = security->sybil_head;
    while (entry != NULL) {
        next = entry->next;
        free(entry);
        entry = next;
    }

    security->sybil_head = NULL;
    security->sybil_count = 0;
}

/* ============================================================================
 * TOKEN VALIDATION (delegates to dht_token)
 * ============================================================================ */

wingo_error_t wingo_dht_security_token_generate(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    wingo_u8 *out,
    wingo_size len)
{
    if (security == NULL || security->token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return wingo_dht_token_generate(security->token, addr, out, len);
}

bool wingo_dht_security_token_verify(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr,
    const wingo_u8 *token,
    wingo_size len)
{
    wingo_dht_token_result_t result;

    if (security == NULL || security->token == NULL) {
        return false;
    }

    result = wingo_dht_token_verify(security->token, addr, token, len);

    if (result != WINGO_DHT_TOKEN_OK) {
        security->stats.packets_bad_token++;
        return false;
    }

    return true;
}

wingo_error_t wingo_dht_security_token_rotate(
    wingo_dht_security_t *security)
{
    if (security == NULL || security->token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return wingo_dht_token_rotate(security->token);
}

wingo_i64 wingo_dht_security_token_next_rotate(
    const wingo_dht_security_t *security)
{
    if (security == NULL || security->token == NULL) {
        return 0;
    }

    return wingo_dht_token_next_rotate(security->token);
}

bool wingo_dht_security_token_needs_rotate(
    const wingo_dht_security_t *security)
{
    if (security == NULL || security->token == NULL) {
        return false;
    }

    return wingo_dht_token_needs_rotate(security->token);
}

/* ============================================================================
 * IP VALIDATION
 * ============================================================================ */

/*
 * Validate source address.
 *
 * Checks:
 *   1. Not martian
 *   2. Not blacklisted
 *   3. Rate limited
 *   4. Sybil protected
 */
wingo_dht_security_result_t wingo_dht_security_validate_source(
    wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    wingo_dht_security_result_t result;

    if (security == NULL || addr == NULL) {
        return WINGO_DHT_SECURITY_ERROR;
    }

    /* Check martian */
    if (wingo_dht_security_is_martian(addr)) {
        security->stats.packets_martian++;
        return WINGO_DHT_SECURITY_MARTIAN;
    }

    /* Check blacklist */
    if (wingo_dht_security_is_blacklisted_addr(security, addr)) {
        security->stats.packets_blocked++;
        return WINGO_DHT_SECURITY_BLOCKED;
    }

    /* Check rate limit */
    result = wingo_dht_security_rate_limit_check(security, addr);
    if (result != WINGO_DHT_SECURITY_OK) {
        return result;
    }

    /* Check Sybil */
    result = wingo_dht_security_sybil_check(security, addr);
    if (result != WINGO_DHT_SECURITY_OK) {
        return result;
    }

    security->stats.packets_allowed++;

    return WINGO_DHT_SECURITY_OK;
}

/*
 * Check if a source address is spoofed.
 *
 * NOTE: Real spoof detection requires:
 *   - TCP handshake verification
 *   - BGP validation
 *   - RPF (Reverse Path Forwarding)
 *
 * We can only do basic checks at this layer:
 *   - Martian address
 *   - Blacklisted address
 *   - Multicast address
 *
 * If any of these are true, the address is suspicious.
 */
bool wingo_dht_security_is_spoofed(
    const wingo_dht_security_t *security,
    const wingo_addr_t *addr)
{
    if (security == NULL || addr == NULL) {
        return true;
    }

    /* Martian addresses are definitely spoofed */
    if (wingo_dht_security_is_martian(addr)) {
        return true;
    }

    /* Multicast addresses should never be sources */
    if (wingo_dht_security_is_multicast(addr)) {
        return true;
    }

    /* Blacklisted addresses are suspicious */
    if (wingo_dht_security_is_blacklisted_addr(security, addr)) {
        return true;
    }

    return false;
}

/* ============================================================================
 * SECURITY STATISTICS
 * ============================================================================ */

wingo_error_t wingo_dht_security_get_stats(
    const wingo_dht_security_t *security,
    wingo_dht_security_stats_t *stats)
{
    if (security == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memcpy(stats, &security->stats, sizeof(wingo_dht_security_stats_t));

    stats->blacklist_size = security->blacklist_count;
    stats->rate_limit_entries = security->rate_count;
    stats->sybil_subnets = security->sybil_count;

    return WINGO_SUCCESS;
}

void wingo_dht_security_reset_stats(wingo_dht_security_t *security)
{
    if (security == NULL) {
        return;
    }

    memset(&security->stats, 0, sizeof(security->stats));
}

/* ============================================================================
 * SECURITY UTILITY
 * ============================================================================ */

const char *wingo_dht_security_result_name(
    wingo_dht_security_result_t result)
{
    switch (result) {
    case WINGO_DHT_SECURITY_OK:           return "OK";
    case WINGO_DHT_SECURITY_BLOCKED:      return "BLOCKED";
    case WINGO_DHT_SECURITY_RATE_LIMITED: return "RATE_LIMITED";
    case WINGO_DHT_SECURITY_MARTIAN:      return "MARTIAN";
    case WINGO_DHT_SECURITY_SYBIL:        return "SYBIL";
    case WINGO_DHT_SECURITY_BAD_TOKEN:    return "BAD_TOKEN";
    case WINGO_DHT_SECURITY_SPOOFED:      return "SPOOFED";
    case WINGO_DHT_SECURITY_ERROR:        return "ERROR";
    default:                              return "UNKNOWN";
    }
}

const char *wingo_dht_blacklist_reason_name(
    wingo_dht_blacklist_reason_t reason)
{
    switch (reason) {
    case WINGO_DHT_BLACKLIST_REASON_NONE:    return "NONE";
    case WINGO_DHT_BLACKLIST_REASON_ABUSE:   return "ABUSE";
    case WINGO_DHT_BLACKLIST_REASON_SPAM:    return "SPAM";
    case WINGO_DHT_BLACKLIST_REASON_INVALID: return "INVALID";
    case WINGO_DHT_BLACKLIST_REASON_MANUAL:  return "MANUAL";
    default:                                 return "UNKNOWN";
    }
}

void wingo_dht_security_print(const wingo_dht_security_t *security, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (security == NULL) {
        fprintf(f, "DHT security: (null)\n");
        return;
    }

    fprintf(f, "DHT Security:\n");
    fprintf(f, "  Blacklist:     %zu entries (max %zu)\n",
            security->blacklist_count, security->max_blacklist);
    fprintf(f, "  Rate limit:    %s (%zu entries)\n",
            security->enable_rate_limit ? "enabled" : "disabled",
            security->rate_count);
    fprintf(f, "  Sybil:         %s (%zu subnets)\n",
            security->enable_sybil ? "enabled" : "disabled",
            security->sybil_count);
    fprintf(f, "  Token:         %s\n",
            security->token != NULL ? "present" : "absent");
    fprintf(f, "\n");

    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Allowed:       %llu\n",
            (unsigned long long)security->stats.packets_allowed);
    fprintf(f, "    Blocked:       %llu\n",
            (unsigned long long)security->stats.packets_blocked);
    fprintf(f, "    Rate limited:  %llu\n",
            (unsigned long long)security->stats.packets_rate_limited);
    fprintf(f, "    Martian:       %llu\n",
            (unsigned long long)security->stats.packets_martian);
    fprintf(f, "    Sybil:         %llu\n",
            (unsigned long long)security->stats.packets_sybil);
    fprintf(f, "    Bad token:     %llu\n",
            (unsigned long long)security->stats.packets_bad_token);
    fprintf(f, "    Spoofed:       %llu\n",
            (unsigned long long)security->stats.packets_spoofed);
    fprintf(f, "    Rotations:     %llu\n",
            (unsigned long long)security->stats.token_rotations);
}

void wingo_dht_security_print_blacklist(
    const wingo_dht_security_t *security,
    FILE *f)
{
    blacklist_entry_t *entry;
    wingo_size index = 0;

    if (f == NULL) {
        f = stderr;
    }

    if (security == NULL) {
        fprintf(f, "DHT security: (null)\n");
        return;
    }

    fprintf(f, "DHT Blacklist (%zu entries):\n", security->blacklist_count);

    for (entry = security->blacklist_head; entry != NULL; entry = entry->next) {
        char id_hex[WINGO_DHT_ID_HEX_SIZE];
        char addr_str[WINGO_ADDR_STR_MAX];
        wingo_size i;
        static const char hex[] = "0123456789abcdef";

        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            id_hex[i * 2]     = hex[(entry->id.bytes[i] >> 4) & 0x0F];
            id_hex[i * 2 + 1] = hex[entry->id.bytes[i] & 0x0F];
        }
        id_hex[WINGO_DHT_ID_SIZE * 2] = '\0';

        if (entry->addr != NULL) {
            wingo_addr_str(entry->addr, addr_str, sizeof(addr_str));
        } else {
            snprintf(addr_str, sizeof(addr_str), "(none)");
        }

        fprintf(f, "  [%zu] id=%s addr=%s reason=%s expires_in=%llds\n",
                index++,
                id_hex,
                addr_str,
                wingo_dht_blacklist_reason_name(entry->reason),
                (long long)(entry->expires_at - wingo_time_now()));
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
