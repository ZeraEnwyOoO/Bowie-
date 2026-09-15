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
 * DHT configuration implementation for Bowie.
 *
 * This file provides:
 *   - Default configuration
 *   - Validation
 *   - Copy
 *   - Bootstrap node management (static buffer, no malloc)
 *   - File load/save
 *   - Print
 *
 * Configuration file format (simple key=value):
 *
 *   # Bowie DHT configuration
 *   port = 6881
 *   family = ipv4
 *   enable_ipv6 = false
 *   max_nodes = 2048
 *   max_peers = 2048
 *   max_hashes = 16384
 *   max_searches = 1024
 *   bucket_size = 8
 *   search_timeout = 3720
 *   node_timeout = 1800
 *   storage_timeout = 1920
 *   token_size = 8
 *   token_rotate_min = 900
 *   token_rotate_max = 2700
 *   enable_rate_limit = true
 *   rate_limit = 100
 *   enable_blacklist = true
 *   blacklist_size = 10
 *   debug = false
 *   bootstrap = router.bittorrent.com
 *   bootstrap = router.utorrent.com
 *
 * ============================================================================
 */

#include "wingo/net/dht/dht_config.h"
#include "wingo/net/dht/dht_types.h"
#include "wingo/log.h"
#include "wingo/util/time.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

/* ============================================================================
 * INTERNAL HELPERS — STRING
 * ============================================================================ */

/*
 * Trim leading and trailing whitespace.
 */
static char *str_trim(char *str)
{
    char *end;

    if (str == NULL) {
        return NULL;
    }

    /* Trim leading */
    while (*str != '\0' && isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    /* Trim trailing */
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    end[1] = '\0';

    return str;
}

/*
 * Parse a boolean value.
 *
 * Accepts: true, false, yes, no, 1, 0, on, off
 */
static bool parse_bool(const char *str, bool *out)
{
    if (str == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(str, "true") == 0 ||
        strcasecmp(str, "yes") == 0 ||
        strcasecmp(str, "on") == 0 ||
        strcmp(str, "1") == 0) {
        *out = true;
        return true;
    }

    if (strcasecmp(str, "false") == 0 ||
        strcasecmp(str, "no") == 0 ||
        strcasecmp(str, "off") == 0 ||
        strcmp(str, "0") == 0) {
        *out = false;
        return true;
    }

    return false;
}

/*
 * Parse an address family.
 */
static bool parse_family(const char *str, wingo_addr_family_t *out)
{
    if (str == NULL || out == NULL) {
        return false;
    }

    if (strcasecmp(str, "ipv4") == 0 ||
        strcasecmp(str, "inet") == 0) {
        *out = WINGO_ADDR_IPV4;
        return true;
    }

    if (strcasecmp(str, "ipv6") == 0 ||
        strcasecmp(str, "inet6") == 0) {
        *out = WINGO_ADDR_IPV6;
        return true;
    }

    if (strcasecmp(str, "unspec") == 0 ||
        strcasecmp(str, "any") == 0) {
        *out = WINGO_ADDR_UNSPEC;
        return true;
    }

    return false;
}

/*
 * Parse a 64-bit signed integer.
 */
static bool parse_i64(const char *str, wingo_i64 *out)
{
    char *end;
    long long value;

    if (str == NULL || out == NULL) {
        return false;
    }

    errno = 0;
    value = strtoll(str, &end, 10);

    if (errno != 0 || *end != '\0') {
        return false;
    }

    *out = (wingo_i64)value;
    return true;
}

/*
 * Parse a size_t value.
 */
static bool parse_size(const char *str, wingo_size *out)
{
    char *end;
    unsigned long long value;

    if (str == NULL || out == NULL) {
        return false;
    }

    errno = 0;
    value = strtoull(str, &end, 10);

    if (errno != 0 || *end != '\0') {
        return false;
    }

    *out = (wingo_size)value;
    return true;
}

/*
 * Safe string copy into fixed-size buffer.
 */
static void safe_strcpy(char *dst, wingo_size dst_size, const char *src)
{
    wingo_size len;

    if (dst == NULL || dst_size == 0) {
        return;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return;
    }

    len = strlen(src);
    if (len >= dst_size) {
        len = dst_size - 1;
    }

    if (len > 0) {
        memcpy(dst, src, len);
    }
    dst[len] = '\0';
}

/* ============================================================================
 * DEFAULT CONFIGURATION
 * ============================================================================ */

/*
 * Get default DHT configuration.
 *
 * Fills the config with sensible defaults for a typical Bowie node.
 */
void wingo_dht_config_default(wingo_dht_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    /* ----- Identity ----- */
    /* node_id: leave zero (random will be generated) */
    memset(&config->node_id, 0, sizeof(config->node_id));
    config->port = WINGO_DHT_CONFIG_DEFAULT_PORT;

    /* ----- Network ----- */
    config->family = WINGO_DHT_CONFIG_DEFAULT_FAMILY;
    config->enable_ipv6 = WINGO_DHT_CONFIG_DEFAULT_IPV6;

    /* ----- Bootstrap ----- */
    /* Leave empty — use wingo_dht_config_set_default_bootstrap() */
    memset(config->bootstrap, 0, sizeof(config->bootstrap));
    config->num_bootstrap = 0;

    /* ----- Limits ----- */
    config->max_nodes    = WINGO_DHT_CONFIG_DEFAULT_MAX_NODES;
    config->max_peers    = WINGO_DHT_CONFIG_DEFAULT_MAX_PEERS;
    config->max_hashes   = WINGO_DHT_CONFIG_DEFAULT_MAX_HASHES;
    config->max_searches = WINGO_DHT_CONFIG_DEFAULT_MAX_SEARCHES;
    config->bucket_size  = WINGO_DHT_CONFIG_DEFAULT_BUCKET_SIZE;

    /* ----- Timeouts ----- */
    config->search_timeout  = WINGO_DHT_CONFIG_DEFAULT_SEARCH_TIMEOUT;
    config->node_timeout    = WINGO_DHT_CONFIG_DEFAULT_NODE_TIMEOUT;
    config->storage_timeout = WINGO_DHT_CONFIG_DEFAULT_STORAGE_TIMEOUT;

    /* ----- Token ----- */
    config->token_size       = WINGO_DHT_CONFIG_DEFAULT_TOKEN_SIZE;
    config->token_rotate_min = WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MIN;
    config->token_rotate_max = WINGO_DHT_CONFIG_DEFAULT_TOKEN_ROTATE_MAX;

    /* ----- Security ----- */
    config->enable_rate_limit = true;
    config->rate_limit        = WINGO_DHT_CONFIG_DEFAULT_RATE_LIMIT;
    config->enable_blacklist  = true;
    config->blacklist_size    = WINGO_DHT_CONFIG_DEFAULT_BLACKLIST_SIZE;

    /* ----- Debug ----- */
    config->debug = WINGO_DHT_CONFIG_DEFAULT_DEBUG;
}

/* ============================================================================
 * VALIDATION
 * ============================================================================ */

/*
 * Validate DHT configuration.
 */
wingo_error_t wingo_dht_config_validate(const wingo_dht_config_t *config)
{
    if (config == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* ----- Port ----- */
    if (config->port == 0) {
        WINGO_LOG_ERROR("DHT config: port must be non-zero");
        return WINGO_ERR_CONFIG_INVALID;
    }

    if (config->port < 1024) {
        WINGO_LOG_WARN("DHT config: port %u requires root privileges",
                       config->port);
    }

    /* ----- Family ----- */
    if (config->family != WINGO_ADDR_IPV4 &&
        config->family != WINGO_ADDR_IPV6 &&
        config->family != WINGO_ADDR_UNSPEC) {
        WINGO_LOG_ERROR("DHT config: invalid address family");
        return WINGO_ERR_CONFIG_INVALID;
    }

    /* ----- Limits ----- */
    if (config->max_nodes == 0) {
        WINGO_LOG_ERROR("DHT config: max_nodes must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->max_peers == 0) {
        WINGO_LOG_ERROR("DHT config: max_peers must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->max_hashes == 0) {
        WINGO_LOG_ERROR("DHT config: max_hashes must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->max_searches == 0) {
        WINGO_LOG_ERROR("DHT config: max_searches must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->bucket_size < WINGO_DHT_BUCKET_MIN_SIZE ||
        config->bucket_size > 64) {
        WINGO_LOG_ERROR("DHT config: bucket_size must be between %d and 64",
                        WINGO_DHT_BUCKET_MIN_SIZE);
        return WINGO_ERR_CONFIG_RANGE;
    }

    /* ----- Timeouts ----- */
    if (config->search_timeout <= 0) {
        WINGO_LOG_ERROR("DHT config: search_timeout must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->node_timeout <= 0) {
        WINGO_LOG_ERROR("DHT config: node_timeout must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->storage_timeout <= 0) {
        WINGO_LOG_ERROR("DHT config: storage_timeout must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    /* ----- Token ----- */
    if (config->token_size == 0 || config->token_size > 64) {
        WINGO_LOG_ERROR("DHT config: token_size must be between 1 and 64");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->token_rotate_min <= 0) {
        WINGO_LOG_ERROR("DHT config: token_rotate_min must be > 0");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->token_rotate_max < config->token_rotate_min) {
        WINGO_LOG_ERROR("DHT config: token_rotate_max must be >= token_rotate_min");
        return WINGO_ERR_CONFIG_RANGE;
    }

    /* ----- Security ----- */
    if (config->enable_rate_limit && config->rate_limit == 0) {
        WINGO_LOG_ERROR("DHT config: rate_limit must be > 0 when enabled");
        return WINGO_ERR_CONFIG_RANGE;
    }

    if (config->enable_blacklist && config->blacklist_size == 0) {
        WINGO_LOG_ERROR("DHT config: blacklist_size must be > 0 when enabled");
        return WINGO_ERR_CONFIG_RANGE;
    }

    /* ----- Bootstrap ----- */
    if (config->num_bootstrap > WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP) {
        WINGO_LOG_ERROR("DHT config: too many bootstrap nodes (max %d)",
                        WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP);
        return WINGO_ERR_CONFIG_RANGE;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * COPY
 * ============================================================================ */

/*
 * Copy DHT configuration.
 *
 * Performs a deep copy. Because bootstrap uses a fixed-size
 * array (not pointers), memcpy() works correctly.
 */
wingo_error_t wingo_dht_config_copy(wingo_dht_config_t *dst,
                                     const wingo_dht_config_t *src)
{
    if (dst == NULL || src == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (dst == src) {
        return WINGO_SUCCESS;  /* Self-copy is no-op */
    }

    /*
     * Deep copy — works because bootstrap is an inline array.
     * No pointers to share, no dangling references.
     */
    memcpy(dst, src, sizeof(wingo_dht_config_t));

    return WINGO_SUCCESS;
}

/* ============================================================================
 * BOOTSTRAP NODES
 * ============================================================================ */

/*
 * Set default bootstrap nodes.
 *
 * Copies the standard public BitTorrent DHT bootstrap nodes
 * into the config's fixed-size buffer.
 *
 * WARNING: These are public nodes. For a truly serverless Bowie,
 *          use manual peer entry instead. This function is provided
 *          for compatibility and initial bootstrap only.
 */
wingo_error_t wingo_dht_config_set_default_bootstrap(
    wingo_dht_config_t *config)
{
    static const char *defaults[] = {
        "router.bittorrent.com",
        "router.utorrent.com",
        "dht.transmissionbt.com",
        NULL
    };

    wingo_size i;

    if (config == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Clear existing */
    memset(config->bootstrap, 0, sizeof(config->bootstrap));
    config->num_bootstrap = 0;

    /* Copy defaults into static buffer */
    for (i = 0; defaults[i] != NULL &&
                i < WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP; i++) {
        safe_strcpy(config->bootstrap[i].host,
                    sizeof(config->bootstrap[i].host),
                    defaults[i]);
        config->num_bootstrap++;
    }

    WINGO_LOG_DEBUG("DHT config: set %zu default bootstrap nodes",
                    config->num_bootstrap);

    return WINGO_SUCCESS;
}

/*
 * Add a bootstrap node to configuration.
 *
 * Copies the host string into the config's fixed-size buffer.
 * No malloc, no free, no leak.
 */
wingo_error_t wingo_dht_config_add_bootstrap(wingo_dht_config_t *config,
                                              const char *host)
{
    wingo_size i;
    wingo_size host_len;

    if (config == NULL || host == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (config->num_bootstrap >= WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP) {
        WINGO_LOG_ERROR("DHT config: bootstrap list full (max %d)",
                        WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP);
        return WINGO_ERR_OUT_OF_RANGE;
    }

    host_len = strlen(host);
    if (host_len == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (host_len >= WINGO_DHT_CONFIG_BOOTSTRAP_MAX_HOST) {
        WINGO_LOG_ERROR("DHT config: bootstrap host too long (%zu >= %d)",
                        host_len, WINGO_DHT_CONFIG_BOOTSTRAP_MAX_HOST);
        return WINGO_ERR_OVERFLOW;
    }

    /* Check for duplicate */
    for (i = 0; i < config->num_bootstrap; i++) {
        if (strcmp(config->bootstrap[i].host, host) == 0) {
            return WINGO_SUCCESS;  /* Already present */
        }
    }

    /* Copy into static buffer */
    safe_strcpy(config->bootstrap[config->num_bootstrap].host,
                sizeof(config->bootstrap[0].host),
                host);
    config->num_bootstrap++;

    WINGO_LOG_DEBUG("DHT config: added bootstrap node '%s'", host);

    return WINGO_SUCCESS;
}
