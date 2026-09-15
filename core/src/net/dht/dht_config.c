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
 *   - Bootstrap node management
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
 *
 * Checks that all fields are within acceptable ranges.
 *
 * @return WINGO_SUCCESS if valid, error code on failure
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

    /* Port 0 is technically valid (OS picks), but we require explicit */
    /* Well-known ports < 1024 typically require root */
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
 * Both source and destination must be valid.
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
     * NOTE: The struct contains pointers (bootstrap strings).
     *       We do a shallow copy — the pointers are shared.
     *       Caller must ensure the strings outlive both configs.
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
 * Uses the standard public BitTorrent DHT bootstrap nodes.
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
    for (i = 0; i < config->num_bootstrap; i++) {
        config->bootstrap[i] = NULL;
    }
    config->num_bootstrap = 0;

    /* Add defaults */
    for (i = 0; defaults[i] != NULL &&
                i < WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP; i++) {
        config->bootstrap[i] = defaults[i];
        config->num_bootstrap++;
    }

    WINGO_LOG_DEBUG("DHT config: set %zu default bootstrap nodes",
                    config->num_bootstrap);

    return WINGO_SUCCESS;
}

/*
 * Add a bootstrap node to configuration.
 *
 * The host string is NOT copied — caller must ensure it outlives
 * the config.
 */
wingo_error_t wingo_dht_config_add_bootstrap(wingo_dht_config_t *config,
                                              const char *host)
{
    if (config == NULL || host == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (config->num_bootstrap >= WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP) {
        WINGO_LOG_ERROR("DHT config: bootstrap list full (max %d)",
                        WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP);
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Check for duplicate */
    {
        wingo_size i;
        for (i = 0; i < config->num_bootstrap; i++) {
            if (config->bootstrap[i] != NULL &&
                strcmp(config->bootstrap[i], host) == 0) {
                return WINGO_SUCCESS;  /* Already present */
            }
        }
    }

    config->bootstrap[config->num_bootstrap++] = host;

    WINGO_LOG_DEBUG("DHT config: added bootstrap node '%s'", host);

    return WINGO_SUCCESS;
}
/* ============================================================================
 * FILE LOAD/SAVE — HELPERS
 * ============================================================================ */

/*
 * Get family name from family value.
 */
static const char *family_to_string(wingo_addr_family_t family)
{
    switch (family) {
    case WINGO_ADDR_IPV4:   return "ipv4";
    case WINGO_ADDR_IPV6:   return "ipv6";
    case WINGO_ADDR_UNSPEC: return "unspec";
    default:                return "unknown";
    }
}

/*
 * Write a string field to file.
 */
static void config_write_string(FILE *f, const char *key, const char *value)
{
    fprintf(f, "%s = %s\n", key, value != NULL ? value : "");
}

/*
 * Write an integer field to file.
 */
static void config_write_i64(FILE *f, const char *key, wingo_i64 value)
{
    fprintf(f, "%s = %lld\n", key, (long long)value);
}

/*
 * Write a size field to file.
 */
static void config_write_size(FILE *f, const char *key, wingo_size value)
{
    fprintf(f, "%s = %zu\n", key, value);
}

/*
 * Write a boolean field to file.
 */
static void config_write_bool(FILE *f, const char *key, bool value)
{
    fprintf(f, "%s = %s\n", key, value ? "true" : "false");
}

/* ============================================================================
 * PARSE LINE
 * ============================================================================ */

/*
 * Parse a single config line.
 *
 * Format: key = value
 * Comments start with '#' or ';'.
 *
 * Returns:
 *   WINGO_SUCCESS    — parsed successfully (or comment/blank)
 *   WINGO_ERR_*       — parse error
 */
static wingo_error_t config_parse_line(wingo_dht_config_t *config,
                                        char *line)
{
    char *key;
    char *value;
    char *eq;

    if (config == NULL || line == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Trim */
    key = str_trim(line);

    /* Skip empty lines */
    if (*key == '\0') {
        return WINGO_SUCCESS;
    }

    /* Skip comments */
    if (*key == '#' || *key == ';') {
        return WINGO_SUCCESS;
    }

    /* Find '=' */
    eq = strchr(key, '=');
    if (eq == NULL) {
        WINGO_LOG_WARN("DHT config: invalid line (no '='): %s", key);
        return WINGO_ERR_CONFIG_SYNTAX;
    }

    *eq = '\0';
    value = str_trim(eq + 1);
    key = str_trim(key);

    /* Parse based on key */
    if (strcmp(key, "port") == 0) {
        wingo_i64 v;
        if (!parse_i64(value, &v) || v < 0 || v > 65535) {
            WINGO_LOG_WARN("DHT config: invalid port: %s", value);
            return WINGO_ERR_CONFIG_INVALID;
        }
        config->port = (wingo_u16)v;
    }
    else if (strcmp(key, "family") == 0) {
        if (!parse_family(value, &config->family)) {
            WINGO_LOG_WARN("DHT config: invalid family: %s", value);
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "enable_ipv6") == 0) {
        if (!parse_bool(value, &config->enable_ipv6)) {
            WINGO_LOG_WARN("DHT config: invalid enable_ipv6: %s", value);
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "max_nodes") == 0) {
        if (!parse_size(value, &config->max_nodes)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "max_peers") == 0) {
        if (!parse_size(value, &config->max_peers)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "max_hashes") == 0) {
        if (!parse_size(value, &config->max_hashes)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "max_searches") == 0) {
        if (!parse_size(value, &config->max_searches)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "bucket_size") == 0) {
        if (!parse_size(value, &config->bucket_size)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "search_timeout") == 0) {
        if (!parse_i64(value, &config->search_timeout)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "node_timeout") == 0) {
        if (!parse_i64(value, &config->node_timeout)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "storage_timeout") == 0) {
        if (!parse_i64(value, &config->storage_timeout)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "token_size") == 0) {
        if (!parse_size(value, &config->token_size)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "token_rotate_min") == 0) {
        if (!parse_i64(value, &config->token_rotate_min)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "token_rotate_max") == 0) {
        if (!parse_i64(value, &config->token_rotate_max)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "enable_rate_limit") == 0) {
        if (!parse_bool(value, &config->enable_rate_limit)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "rate_limit") == 0) {
        if (!parse_size(value, &config->rate_limit)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "enable_blacklist") == 0) {
        if (!parse_bool(value, &config->enable_blacklist)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "blacklist_size") == 0) {
        if (!parse_size(value, &config->blacklist_size)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "debug") == 0) {
        if (!parse_bool(value, &config->debug)) {
            return WINGO_ERR_CONFIG_INVALID;
        }
    }
    else if (strcmp(key, "bootstrap") == 0) {
        /*
         * Bootstrap node. We do NOT copy the string — caller must
         * keep it alive. Since we're reading from a file, we need
         * to strdup it.
         *
         * NOTE: This is a memory leak risk — the strings are not
         *       freed by wingo_dht_config_free (there is none).
         *       For now, we store the raw pointer from a static
         *       buffer.
         *
         * TODO: Handle bootstrap string ownership properly.
         */
        if (config->num_bootstrap < WINGO_DHT_CONFIG_DEFAULT_MAX_BOOTSTRAP) {
            /*
             * We can't strdup safely here without a free function.
             * For now, log a warning.
             */
            WINGO_LOG_WARN("DHT config: bootstrap from file not yet supported");
        }
    }
    else {
        WINGO_LOG_WARN("DHT config: unknown key '%s'", key);
        return WINGO_ERR_CONFIG_UNKNOWN;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * LOAD FROM FILE
 * ============================================================================ */

/*
 * Load DHT configuration from file.
 *
 * The file format is simple key=value pairs, one per line.
 * Comments start with '#' or ';'.
 *
 * If the file does not exist, WINGO_ERR_FILE_NOT_FOUND is returned
 * and the config is left unchanged.
 */
wingo_error_t wingo_dht_config_load(wingo_dht_config_t *config,
                                     const char *path)
{
    FILE *f;
    char line[512];
    int line_num = 0;
    wingo_error_t rc;

    if (config == NULL || path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen(path, "r");
    if (f == NULL) {
        if (errno == ENOENT) {
            WINGO_LOG_DEBUG("DHT config: file '%s' not found", path);
            return WINGO_ERR_FILE_NOT_FOUND;
        }
        WINGO_LOG_ERROR("DHT config: cannot open '%s': %s",
                        path, strerror(errno));
        return WINGO_ERR_FILE_OPEN;
    }

    WINGO_LOG_DEBUG("DHT config: loading from '%s'", path);

    while (fgets(line, sizeof(line), f) != NULL) {
        line_num++;

        rc = config_parse_line(config, line);
        if (rc != WINGO_SUCCESS) {
            WINGO_LOG_WARN("DHT config: error on line %d: %s",
                           line_num, wingo_error_str(rc));
            /* Continue on parse errors — don't fail the whole load */
        }
    }

    if (ferror(f)) {
        WINGO_LOG_ERROR("DHT config: read error on '%s'", path);
        fclose(f);
        return WINGO_ERR_FILE_READ;
    }

    fclose(f);

    WINGO_LOG_DEBUG("DHT config: loaded %d lines", line_num);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SAVE TO FILE
 * ============================================================================ */

/*
 * Save DHT configuration to file.
 */
wingo_error_t wingo_dht_config_save(const wingo_dht_config_t *config,
                                     const char *path)
{
    FILE *f;
    wingo_size i;

    if (config == NULL || path == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    f = fopen(path, "w");
    if (f == NULL) {
        WINGO_LOG_ERROR("DHT config: cannot create '%s': %s",
                        path, strerror(errno));
        return WINGO_ERR_FILE_OPEN;
    }

    fprintf(f, "# Bowie DHT configuration\n");
    fprintf(f, "# Generated by wingo_dht_config_save()\n");
    fprintf(f, "\n");

    /* ----- Identity ----- */
    fprintf(f, "# Identity\n");
    config_write_i64(f, "port", config->port);
    fprintf(f, "\n");

    /* ----- Network ----- */
    fprintf(f, "# Network\n");
    config_write_string(f, "family", family_to_string(config->family));
    config_write_bool(f, "enable_ipv6", config->enable_ipv6);
    fprintf(f, "\n");

    /* ----- Limits ----- */
    fprintf(f, "# Limits\n");
    config_write_size(f, "max_nodes", config->max_nodes);
    config_write_size(f, "max_peers", config->max_peers);
    config_write_size(f, "max_hashes", config->max_hashes);
    config_write_size(f, "max_searches", config->max_searches);
    config_write_size(f, "bucket_size", config->bucket_size);
    fprintf(f, "\n");

    /* ----- Timeouts ----- */
    fprintf(f, "# Timeouts (seconds)\n");
    config_write_i64(f, "search_timeout", config->search_timeout);
    config_write_i64(f, "node_timeout", config->node_timeout);
    config_write_i64(f, "storage_timeout", config->storage_timeout);
    fprintf(f, "\n");

    /* ----- Token ----- */
    fprintf(f, "# Token\n");
    config_write_size(f, "token_size", config->token_size);
    config_write_i64(f, "token_rotate_min", config->token_rotate_min);
    config_write_i64(f, "token_rotate_max", config->token_rotate_max);
    fprintf(f, "\n");

    /* ----- Security ----- */
    fprintf(f, "# Security\n");
    config_write_bool(f, "enable_rate_limit", config->enable_rate_limit);
    config_write_size(f, "rate_limit", config->rate_limit);
    config_write_bool(f, "enable_blacklist", config->enable_blacklist);
    config_write_size(f, "blacklist_size", config->blacklist_size);
    fprintf(f, "\n");

    /* ----- Debug ----- */
    fprintf(f, "# Debug\n");
    config_write_bool(f, "debug", config->debug);
    fprintf(f, "\n");

    /* ----- Bootstrap ----- */
    fprintf(f, "# Bootstrap nodes\n");
    for (i = 0; i < config->num_bootstrap; i++) {
        if (config->bootstrap[i] != NULL) {
            fprintf(f, "bootstrap = %s\n", config->bootstrap[i]);
        }
    }
    fprintf(f, "\n");

    /* Check for write errors */
    if (ferror(f)) {
        WINGO_LOG_ERROR("DHT config: write error on '%s'", path);
        fclose(f);
        return WINGO_ERR_FILE_WRITE;
    }

    fclose(f);

    WINGO_LOG_DEBUG("DHT config: saved to '%s'", path);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * PRINT
 * ============================================================================ */

/*
 * Print DHT configuration.
 */
void wingo_dht_config_print(const wingo_dht_config_t *config, FILE *f)
{
    char id_hex[WINGO_DHT_ID_HEX_SIZE];
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (config == NULL) {
        fprintf(f, "DHT config: (null)\n");
        return;
    }

    /*
     * Convert node ID to hex for display.
     */
    {
        static const char hex[] = "0123456789abcdef";
        for (i = 0; i < WINGO_DHT_ID_SIZE; i++) {
            id_hex[i * 2]     = hex[(config->node_id.bytes[i] >> 4) & 0x0F];
            id_hex[i * 2 + 1] = hex[config->node_id.bytes[i] & 0x0F];
        }
        id_hex[WINGO_DHT_ID_SIZE * 2] = '\0';
    }

    fprintf(f, "DHT Configuration:\n");
    fprintf(f, "\n");

    fprintf(f, "  Identity:\n");
    fprintf(f, "    Node ID:            %s\n", id_hex);
    fprintf(f, "    Port:               %u\n", config->port);
    fprintf(f, "\n");

    fprintf(f, "  Network:\n");
    fprintf(f, "    Family:             %s\n",
            family_to_string(config->family));
    fprintf(f, "    IPv6 enabled:       %s\n",
            config->enable_ipv6 ? "yes" : "no");
    fprintf(f, "\n");

    fprintf(f, "  Limits:\n");
    fprintf(f, "    Max nodes:          %zu\n", config->max_nodes);
    fprintf(f, "    Max peers:          %zu\n", config->max_peers);
    fprintf(f, "    Max hashes:         %zu\n", config->max_hashes);
    fprintf(f, "    Max searches:       %zu\n", config->max_searches);
    fprintf(f, "    Bucket size:        %zu\n", config->bucket_size);
    fprintf(f, "\n");

    fprintf(f, "  Timeouts:\n");
    fprintf(f, "    Search:             %llds\n",
            (long long)config->search_timeout);
    fprintf(f, "    Node:               %llds\n",
            (long long)config->node_timeout);
    fprintf(f, "    Storage:            %llds\n",
            (long long)config->storage_timeout);
    fprintf(f, "\n");

    fprintf(f, "  Token:\n");
    fprintf(f, "    Token size:         %zu bytes\n", config->token_size);
    fprintf(f, "    Rotate min:         %llds\n",
            (long long)config->token_rotate_min);
    fprintf(f, "    Rotate max:         %llds\n",
            (long long)config->token_rotate_max);
    fprintf(f, "\n");

    fprintf(f, "  Security:\n");
    fprintf(f, "    Rate limit:         %s\n",
            config->enable_rate_limit ? "yes" : "no");
    if (config->enable_rate_limit) {
        fprintf(f, "      Rate:             %zu msg/s\n", config->rate_limit);
    }
    fprintf(f, "    Blacklist:          %s\n",
            config->enable_blacklist ? "yes" : "no");
    if (config->enable_blacklist) {
        fprintf(f, "      Size:             %zu entries\n",
                config->blacklist_size);
    }
    fprintf(f, "\n");

    fprintf(f, "  Debug:\n");
    fprintf(f, "    Debug:              %s\n",
            config->debug ? "yes" : "no");
    fprintf(f, "\n");

    fprintf(f, "  Bootstrap:\n");
    if (config->num_bootstrap == 0) {
        fprintf(f, "    (none)\n");
    } else {
        for (i = 0; i < config->num_bootstrap; i++) {
            fprintf(f, "    [%zu] %s\n", i,
                    config->bootstrap[i] != NULL
                        ? config->bootstrap[i]
                        : "(null)");
        }
    }
    fprintf(f, "\n");
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
