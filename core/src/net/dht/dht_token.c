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
 * DHT token MANAGER implementation for Bowie.
 *
 * A token is a short opaque value (8 bytes) that proves a node
 * has previously contacted us. It prevents unsolicited
 * announce_peer requests.
 *
 * Token algorithm:
 *
 *   token = SHA1(secret || IP)[0:8]
 *
 *   where:
 *     secret = random 16 bytes, rotated every 15-45 minutes
 *     IP     = IPv4 (4 bytes) or IPv6 (16 bytes)
 *
 * NOTE: wingo_dht_token_t (from dht_types.h) is a token VALUE.
 *       wingo_dht_token_mgr_t (this file) is the token MANAGER.
 *
 * ============================================================================
 */

#define _POSIX_C_SOURCE 200809L

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
 * Token manager (concrete).
 */
struct wingo_dht_token_mgr {
    /* ----- Secrets ----- */
    wingo_dht_token_secret_t secrets[WINGO_DHT_TOKEN_SECRET_COUNT];
    int                      active_secret;

    /* ----- Rotation ----- */
    wingo_i64                rotate_min;
    wingo_i64                rotate_max;
    wingo_i64                next_rotate;

    /* ----- Statistics ----- */
    wingo_u64                tokens_generated;
    wingo_u64                tokens_verified;
    wingo_u64                tokens_valid;
    wingo_u64                tokens_invalid;
    wingo_u64                tokens_expired;
    wingo_u64                tokens_wrong_addr;
    wingo_u64                rotations;
    wingo_i64                last_rotation;
};

/* ============================================================================
 * INTERNAL HELPERS — SHA1
 * ============================================================================ */

typedef struct {
    wingo_u32 state[5];
    wingo_u64 count;
    wingo_u8  buffer[64];
    wingo_size buffer_len;
} sha1_ctx_t;

static wingo_u32 sha1_rol(wingo_u32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_transform(sha1_ctx_t *ctx, const wingo_u8 block[64])
{
    wingo_u32 w[80];
    wingo_u32 a, b, c, d, e;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((wingo_u32)block[i * 4] << 24) |
               ((wingo_u32)block[i * 4 + 1] << 16) |
               ((wingo_u32)block[i * 4 + 2] << 8) |
               ((wingo_u32)block[i * 4 + 3]);
    }

    for (i = 16; i < 80; i++) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    for (i = 0; i < 80; i++) {
        wingo_u32 f, k, temp;

        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }

        temp = sha1_rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = sha1_rol(b, 30);
        b = a;
        a = temp;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

static void sha1_init(sha1_ctx_t *ctx)
{
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
    ctx->count = 0;
    ctx->buffer_len = 0;
}

static void sha1_update(sha1_ctx_t *ctx, const void *data, wingo_size len)
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    wingo_size i;

    if (len == 0) {
        return;
    }

    ctx->count += len;

    if (ctx->buffer_len > 0) {
        wingo_size needed = 64 - ctx->buffer_len;
        wingo_size take = (len < needed) ? len : needed;

        memcpy(&ctx->buffer[ctx->buffer_len], bytes, take);
        ctx->buffer_len += take;
        bytes += take;
        len -= take;

        if (ctx->buffer_len == 64) {
            sha1_transform(ctx, ctx->buffer);
            ctx->buffer_len = 0;
        }
    }

    for (i = 0; i + 64 <= len; i += 64) {
        sha1_transform(ctx, &bytes[i]);
    }

    if (i < len) {
        wingo_size remaining = len - i;
        memcpy(ctx->buffer, &bytes[i], remaining);
        ctx->buffer_len = remaining;
    }
}

static void sha1_final(sha1_ctx_t *ctx, wingo_u8 out[20])
{
    wingo_u64 bit_count;
    wingo_u8 padding[64];
    wingo_size pad_len;
    wingo_size i;

    bit_count = ctx->count * 8;

    padding[0] = 0x80;
    memset(&padding[1], 0, 63);

    pad_len = (ctx->buffer_len < 56) ?
              (56 - ctx->buffer_len) :
              (120 - ctx->buffer_len);

    sha1_update(ctx, padding, pad_len);

    {
        wingo_u8 len_buf[8];

        for (i = 0; i < 8; i++) {
            len_buf[i] = (wingo_u8)(bit_count >> (56 - i * 8));
        }

        sha1_update(ctx, len_buf, 8);
    }

    for (i = 0; i < 5; i++) {
        out[i * 4]     = (wingo_u8)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (wingo_u8)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (wingo_u8)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (wingo_u8)(ctx->state[i]);
    }
}

/* ============================================================================
 * INTERNAL HELPERS — SECRET
 * ============================================================================ */

static wingo_error_t secret_generate(wingo_dht_token_secret_t *secret)
{
    wingo_error_t rc;

    if (secret == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_random_bytes(secret->bytes, WINGO_DHT_TOKEN_SECRET_SIZE);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    secret->created_at = wingo_time_now();
    secret->active = false;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * INTERNAL HELPERS — ADDRESS TO BYTES
 * ============================================================================ */

static wingo_size addr_get_ip_bytes(const wingo_addr_t *addr,
                                     wingo_u8 *out,
                                     wingo_size out_size)
{
    char ip_str[WINGO_ADDR_STR_MAX];

    if (addr == NULL || out == NULL) {
        return 0;
    }

    if (wingo_addr_ip_str(addr, ip_str, sizeof(ip_str)) != WINGO_SUCCESS) {
        return 0;
    }

    if (wingo_addr_is_ipv4(addr)) {
        unsigned int a, b, c, d;

        if (out_size < 4) {
            return 0;
        }

        if (sscanf(ip_str, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
            return 0;
        }

        out[0] = (wingo_u8)a;
        out[1] = (wingo_u8)b;
        out[2] = (wingo_u8)c;
        out[3] = (wingo_u8)d;

        return 4;
    }

    if (wingo_addr_is_ipv6(addr)) {
        /* IPv6 — skip for now */
        return 0;
    }

    return 0;
}

/* ============================================================================
 * INTERNAL HELPERS — TOKEN COMPUTATION
 * ============================================================================ */

static wingo_error_t token_compute(const wingo_dht_token_secret_t *secret,
                                    const wingo_addr_t *addr,
                                    wingo_u8 *out,
                                    wingo_size out_len)
{
    sha1_ctx_t ctx;
    wingo_u8 hash[20];
    wingo_u8 ip_bytes[16];
    wingo_size ip_len;

    if (secret == NULL || addr == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (out_len < WINGO_DHT_TOKEN_SIZE) {
        return WINGO_ERR_OVERFLOW;
    }

    ip_len = addr_get_ip_bytes(addr, ip_bytes, sizeof(ip_bytes));
    if (ip_len == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    sha1_init(&ctx);
    sha1_update(&ctx, secret->bytes, WINGO_DHT_TOKEN_SECRET_SIZE);
    sha1_update(&ctx, ip_bytes, ip_len);
    sha1_final(&ctx, hash);

    memcpy(out, hash, WINGO_DHT_TOKEN_SIZE);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT TOKEN MANAGER LIFECYCLE
 * ============================================================================ */

wingo_dht_token_mgr_t *wingo_dht_token_mgr_new(void)
{
    wingo_dht_token_mgr_t *mgr;
    wingo_error_t rc;

    mgr = calloc(1, sizeof(wingo_dht_token_mgr_t));
    if (mgr == NULL) {
        return NULL;
    }

    mgr->rotate_min = WINGO_DHT_TOKEN_ROTATE_MIN;
    mgr->rotate_max = WINGO_DHT_TOKEN_ROTATE_MAX;

    rc = secret_generate(&mgr->secrets[0]);
    if (rc != WINGO_SUCCESS) {
        free(mgr);
        return NULL;
    }

    rc = secret_generate(&mgr->secrets[1]);
    if (rc != WINGO_SUCCESS) {
        free(mgr);
        return NULL;
    }

    mgr->secrets[0].active = true;
    mgr->secrets[1].active = false;
    mgr->active_secret = 0;

    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)mgr->rotate_min,
            (wingo_u32)mgr->rotate_max);

        mgr->next_rotate = wingo_time_now() + interval;
    }

    mgr->last_rotation = wingo_time_now();

    WINGO_LOG_DEBUG("DHT token manager created");

    return mgr;
}

void wingo_dht_token_mgr_free(wingo_dht_token_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    memset(mgr->secrets, 0, sizeof(mgr->secrets));

    free(mgr);
}

wingo_error_t wingo_dht_token_mgr_reset(wingo_dht_token_mgr_t *mgr)
{
    wingo_error_t rc;

    if (mgr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = secret_generate(&mgr->secrets[0]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    rc = secret_generate(&mgr->secrets[1]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    mgr->secrets[0].active = true;
    mgr->secrets[1].active = false;
    mgr->active_secret = 0;

    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)mgr->rotate_min,
            (wingo_u32)mgr->rotate_max);

        mgr->next_rotate = wingo_time_now() + interval;
    }

    mgr->last_rotation = wingo_time_now();

    mgr->tokens_generated = 0;
    mgr->tokens_verified = 0;
    mgr->tokens_valid = 0;
    mgr->tokens_invalid = 0;
    mgr->tokens_expired = 0;
    mgr->tokens_wrong_addr = 0;
    mgr->rotations = 0;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT TOKEN MANAGER GENERATION
 * ============================================================================ */

wingo_error_t wingo_dht_token_mgr_generate(wingo_dht_token_mgr_t *mgr,
                                            const wingo_addr_t *addr,
                                            wingo_u8 *out,
                                            wingo_size len)
{
    wingo_error_t rc;
    wingo_dht_token_secret_t *secret;

    if (mgr == NULL || addr == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len < WINGO_DHT_TOKEN_SIZE) {
        return WINGO_ERR_OVERFLOW;
    }

    secret = &mgr->secrets[mgr->active_secret];

    rc = token_compute(secret, addr, out, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    mgr->tokens_generated++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_dht_token_mgr_generate_ip(wingo_dht_token_mgr_t *mgr,
                                               const wingo_addr_t *addr,
                                               wingo_u8 *out,
                                               wingo_size len)
{
    return wingo_dht_token_mgr_generate(mgr, addr, out, len);
}

wingo_error_t wingo_dht_token_mgr_generate_secret(wingo_dht_token_mgr_t *mgr,
                                                   const wingo_addr_t *addr,
                                                   int secret,
                                                   wingo_u8 *out,
                                                   wingo_size len)
{
    wingo_error_t rc;

    if (mgr == NULL || addr == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (secret < 0 || secret >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len < WINGO_DHT_TOKEN_SIZE) {
        return WINGO_ERR_OVERFLOW;
    }

    rc = token_compute(&mgr->secrets[secret], addr, out, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    mgr->tokens_generated++;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT TOKEN MANAGER VERIFICATION
 * ============================================================================ */

bool wingo_dht_token_equal(const wingo_u8 *a, const wingo_u8 *b, wingo_size len)
{
    wingo_u8 diff = 0;
    wingo_size i;

    if (a == NULL || b == NULL) {
        return false;
    }

    for (i = 0; i < len; i++) {
        diff |= a[i] ^ b[i];
    }

    return diff == 0;
}

bool wingo_dht_token_mgr_verify_secret(wingo_dht_token_mgr_t *mgr,
                                        const wingo_addr_t *addr,
                                        int secret,
                                        const wingo_u8 *in,
                                        wingo_size len)
{
    wingo_u8 expected[WINGO_DHT_TOKEN_SIZE];
    wingo_error_t rc;

    if (mgr == NULL || addr == NULL || in == NULL) {
        return false;
    }

    if (secret < 0 || secret >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return false;
    }

    if (len != WINGO_DHT_TOKEN_SIZE) {
        return false;
    }

    rc = token_compute(&mgr->secrets[secret], addr, expected, sizeof(expected));
    if (rc != WINGO_SUCCESS) {
        return false;
    }

    return wingo_dht_token_equal(expected, in, WINGO_DHT_TOKEN_SIZE);
}

bool wingo_dht_token_mgr_verify_ex(wingo_dht_token_mgr_t *mgr,
                                    const wingo_addr_t *addr,
                                    const wingo_u8 *in,
                                    wingo_size len,
                                    wingo_dht_token_result_t *out_result)
{
    int i;
    bool valid = false;

    if (out_result != NULL) {
        *out_result = WINGO_DHT_TOKEN_INVALID;
    }

    if (mgr == NULL || addr == NULL || in == NULL) {
        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_ERROR;
        }
        return false;
    }

    if (len != WINGO_DHT_TOKEN_SIZE) {
        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_INVALID;
        }
        mgr->tokens_invalid++;
        return false;
    }

    mgr->tokens_verified++;

    for (i = 0; i < WINGO_DHT_TOKEN_SECRET_COUNT; i++) {
        if (wingo_dht_token_mgr_verify_secret(mgr, addr, i, in, len)) {
            valid = true;

            if (out_result != NULL) {
                *out_result = WINGO_DHT_TOKEN_OK;
            }

            mgr->tokens_valid++;
            break;
        }
    }

    if (!valid) {
        mgr->tokens_invalid++;

        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_INVALID;
        }
    }

    return valid;
}

wingo_dht_token_result_t wingo_dht_token_mgr_verify(wingo_dht_token_mgr_t *mgr,
                                                     const wingo_addr_t *addr,
                                                     const wingo_u8 *in,
                                                     wingo_size len)
{
    wingo_dht_token_result_t result = WINGO_DHT_TOKEN_INVALID;

    wingo_dht_token_mgr_verify_ex(mgr, addr, in, len, &result);

    return result;
}
/* ============================================================================
 * DHT TOKEN MANAGER ROTATION
 * ============================================================================ */

wingo_error_t wingo_dht_token_mgr_rotate(wingo_dht_token_mgr_t *mgr)
{
    wingo_error_t rc;
    int new_active;

    if (mgr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    new_active = (mgr->active_secret + 1) % WINGO_DHT_TOKEN_SECRET_COUNT;

    rc = secret_generate(&mgr->secrets[new_active]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    mgr->secrets[mgr->active_secret].active = false;

    mgr->secrets[new_active].active = true;
    mgr->active_secret = new_active;

    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)mgr->rotate_min,
            (wingo_u32)mgr->rotate_max);

        mgr->next_rotate = wingo_time_now() + interval;
    }

    mgr->last_rotation = wingo_time_now();
    mgr->rotations++;

    WINGO_LOG_DEBUG("DHT token: rotated secret (active=%d, rotations=%llu)",
                    mgr->active_secret,
                    (unsigned long long)mgr->rotations);

    return WINGO_SUCCESS;
}

bool wingo_dht_token_mgr_needs_rotate(const wingo_dht_token_mgr_t *mgr)
{
    wingo_i64 now;

    if (mgr == NULL) {
        return false;
    }

    now = wingo_time_now();

    return now >= mgr->next_rotate;
}

wingo_i64 wingo_dht_token_mgr_next_rotate(const wingo_dht_token_mgr_t *mgr)
{
    wingo_i64 now;

    if (mgr == NULL) {
        return 0;
    }

    now = wingo_time_now();

    if (now >= mgr->next_rotate) {
        return 0;
    }

    return mgr->next_rotate - now;
}

wingo_error_t wingo_dht_token_mgr_set_rotation(wingo_dht_token_mgr_t *mgr,
                                                wingo_i64 min_s,
                                                wingo_i64 max_s)
{
    if (mgr == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (min_s <= 0 || max_s < min_s) {
        return WINGO_ERR_INVALID_ARG;
    }

    mgr->rotate_min = min_s;
    mgr->rotate_max = max_s;

    {
        wingo_i64 interval;

        interval = wingo_rand_range((wingo_u32)min_s, (wingo_u32)max_s);
        mgr->next_rotate = wingo_time_now() + interval;
    }

    return WINGO_SUCCESS;
}

wingo_i64 wingo_dht_token_mgr_rotation_interval(const wingo_dht_token_mgr_t *mgr)
{
    if (mgr == NULL) {
        return 0;
    }

    return mgr->next_rotate - mgr->last_rotation;
}

/* ============================================================================
 * DHT TOKEN MANAGER SECRET
 * ============================================================================ */

const wingo_dht_token_secret_t *wingo_dht_token_mgr_secret(
    const wingo_dht_token_mgr_t *mgr,
    int index)
{
    if (mgr == NULL) {
        return NULL;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return NULL;
    }

    return &mgr->secrets[index];
}

wingo_i64 wingo_dht_token_mgr_secret_created(const wingo_dht_token_mgr_t *mgr,
                                              int index)
{
    if (mgr == NULL) {
        return 0;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return 0;
    }

    return mgr->secrets[index].created_at;
}

wingo_i64 wingo_dht_token_mgr_secret_age(const wingo_dht_token_mgr_t *mgr,
                                          int index)
{
    wingo_i64 created;
    wingo_i64 now;

    if (mgr == NULL) {
        return 0;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return 0;
    }

    created = mgr->secrets[index].created_at;
    if (created <= 0) {
        return 0;
    }

    now = wingo_time_now();

    return now - created;
}

void wingo_dht_token_mgr_secrets_clear(wingo_dht_token_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    memset(mgr->secrets, 0, sizeof(mgr->secrets));
}

/* ============================================================================
 * DHT TOKEN MANAGER STATISTICS
 * ============================================================================ */

wingo_error_t wingo_dht_token_mgr_get_stats(const wingo_dht_token_mgr_t *mgr,
                                             wingo_dht_token_stats_t *stats)
{
    if (mgr == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    stats->tokens_generated = mgr->tokens_generated;
    stats->tokens_verified  = mgr->tokens_verified;
    stats->tokens_valid     = mgr->tokens_valid;
    stats->tokens_invalid   = mgr->tokens_invalid;
    stats->tokens_expired   = mgr->tokens_expired;
    stats->tokens_wrong_addr = mgr->tokens_wrong_addr;
    stats->rotations        = mgr->rotations;
    stats->last_rotation    = mgr->last_rotation;
    stats->next_rotation    = mgr->next_rotate;

    {
        wingo_i64 now = wingo_time_now();

        if (mgr->secrets[0].created_at > 0) {
            stats->current_age = now - mgr->secrets[0].created_at;
        }

        if (mgr->secrets[1].created_at > 0) {
            stats->previous_age = now - mgr->secrets[1].created_at;
        }
    }

    return WINGO_SUCCESS;
}

void wingo_dht_token_mgr_reset_stats(wingo_dht_token_mgr_t *mgr)
{
    if (mgr == NULL) {
        return;
    }

    mgr->tokens_generated = 0;
    mgr->tokens_verified = 0;
    mgr->tokens_valid = 0;
    mgr->tokens_invalid = 0;
    mgr->tokens_expired = 0;
    mgr->tokens_wrong_addr = 0;
    mgr->rotations = 0;
}

/* ============================================================================
 * DHT TOKEN MANAGER UTILITY
 * ============================================================================ */

const char *wingo_dht_token_result_name(wingo_dht_token_result_t result)
{
    switch (result) {
    case WINGO_DHT_TOKEN_OK:         return "OK";
    case WINGO_DHT_TOKEN_INVALID:    return "INVALID";
    case WINGO_DHT_TOKEN_EXPIRED:    return "EXPIRED";
    case WINGO_DHT_TOKEN_WRONG_ADDR: return "WRONG_ADDR";
    case WINGO_DHT_TOKEN_ERROR:      return "ERROR";
    default:                         return "UNKNOWN";
    }
}

void wingo_dht_token_mgr_print(const wingo_dht_token_mgr_t *mgr, FILE *f)
{
    wingo_i64 now;
    wingo_i64 age0 = 0;
    wingo_i64 age1 = 0;

    if (f == NULL) {
        f = stderr;
    }

    if (mgr == NULL) {
        fprintf(f, "DHT token mgr: (null)\n");
        return;
    }

    now = wingo_time_now();

    if (mgr->secrets[0].created_at > 0) {
        age0 = now - mgr->secrets[0].created_at;
    }
    if (mgr->secrets[1].created_at > 0) {
        age1 = now - mgr->secrets[1].created_at;
    }

    fprintf(f, "DHT Token Manager:\n");
    fprintf(f, "  Active secret:   %d\n", mgr->active_secret);
    fprintf(f, "  Rotation min:    %llds\n", (long long)mgr->rotate_min);
    fprintf(f, "  Rotation max:    %llds\n", (long long)mgr->rotate_max);
    fprintf(f, "  Next rotate:     %llds\n",
            (long long)wingo_dht_token_mgr_next_rotate(mgr));
    fprintf(f, "  Secret 0 age:    %llds (%s)\n",
            (long long)age0,
            mgr->secrets[0].active ? "ACTIVE" : "prev");
    fprintf(f, "  Secret 1 age:    %llds (%s)\n",
            (long long)age1,
            mgr->secrets[1].active ? "ACTIVE" : "prev");
    fprintf(f, "\n");
    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Generated:     %llu\n",
            (unsigned long long)mgr->tokens_generated);
    fprintf(f, "    Verified:      %llu\n",
            (unsigned long long)mgr->tokens_verified);
    fprintf(f, "    Valid:         %llu\n",
            (unsigned long long)mgr->tokens_valid);
    fprintf(f, "    Invalid:       %llu\n",
            (unsigned long long)mgr->tokens_invalid);
    fprintf(f, "    Rotations:     %llu\n",
            (unsigned long long)mgr->rotations);
}

void wingo_dht_token_mgr_print_secrets(const wingo_dht_token_mgr_t *mgr, FILE *f)
{
    int i;
    int j;

    if (f == NULL) {
        f = stderr;
    }

    if (mgr == NULL) {
        fprintf(f, "DHT token mgr: (null)\n");
        return;
    }

    fprintf(f, "DHT Token Secrets (DEBUG ONLY):\n");

    for (i = 0; i < WINGO_DHT_TOKEN_SECRET_COUNT; i++) {
        fprintf(f, "  Secret %d (%s): ",
                i,
                mgr->secrets[i].active ? "ACTIVE" : "prev");

        for (j = 0; j < WINGO_DHT_TOKEN_SECRET_SIZE; j++) {
            fprintf(f, "%02x", mgr->secrets[i].bytes[j]);
        }

        fprintf(f, "\n");
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
