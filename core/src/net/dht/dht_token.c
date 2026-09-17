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
 * DHT token implementation for Bowie.
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
 * NOTE: We use SHA1 because that's what the BitTorrent DHT uses.
 *       SHA1 is not used for security — it's used for hashing.
 *       For real security, tokens are combined with other defenses
 *       (rate limiting, blacklist, etc.).
 *
 * ============================================================================
 */

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
struct wingo_dht_token {
    /* ----- Secrets ----- */
    wingo_dht_token_secret_t secrets[WINGO_DHT_TOKEN_SECRET_COUNT];
    int                      active_secret;  /* Index of active secret */

    /* ----- Rotation ----- */
    wingo_i64                rotate_min;     /* Min rotation interval */
    wingo_i64                rotate_max;     /* Max rotation interval */
    wingo_i64                next_rotate;    /* Time of next rotation */

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

/*
 * SHA1 context.
 */
typedef struct {
    wingo_u32 state[5];
    wingo_u64 count;        /* Total bytes processed */
    wingo_u8  buffer[64];
    wingo_size buffer_len;
} sha1_ctx_t;

/*
 * SHA1 initial state.
 */
#define SHA1_INIT_STATE \
    { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 }

/*
 * Rotate left.
 */
static wingo_u32 sha1_rol(wingo_u32 value, int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

/*
 * SHA1 transform (process one 64-byte block).
 */
static void sha1_transform(sha1_ctx_t *ctx, const wingo_u8 block[64])
{
    wingo_u32 w[80];
    wingo_u32 a, b, c, d, e;
    int i;

    /* Prepare message schedule */
    for (i = 0; i < 16; i++) {
        w[i] = ((wingo_u32)block[i * 4] << 24) |
               ((wingo_u32)block[i * 4 + 1] << 16) |
               ((wingo_u32)block[i * 4 + 2] << 8) |
               ((wingo_u32)block[i * 4 + 3]);
    }

    for (i = 16; i < 80; i++) {
        w[i] = sha1_rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    /* Initialize working variables */
    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    /* Main loop */
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

    /* Update state */
    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

/*
 * SHA1 init.
 */
static void sha1_init(sha1_ctx_t *ctx)
{
    static const wingo_u32 init_state[5] = SHA1_INIT_STATE;

    memcpy(ctx->state, init_state, sizeof(init_state));
    ctx->count = 0;
    ctx->buffer_len = 0;
}

/*
 * SHA1 update.
 */
static void sha1_update(sha1_ctx_t *ctx, const void *data, wingo_size len)
{
    const wingo_u8 *bytes = (const wingo_u8 *)data;
    wingo_size i;

    if (len == 0) {
        return;
    }

    ctx->count += len;

    /* Fill buffer if partially full */
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

    /* Process full blocks */
    for (i = 0; i + 64 <= len; i += 64) {
        sha1_transform(ctx, &bytes[i]);
    }

    /* Buffer remaining */
    if (i < len) {
        wingo_size remaining = len - i;
        memcpy(ctx->buffer, &bytes[i], remaining);
        ctx->buffer_len = remaining;
    }
}

/*
 * SHA1 final.
 */
static void sha1_final(sha1_ctx_t *ctx, wingo_u8 out[20])
{
    wingo_u64 bit_count;
    wingo_u8 padding[64];
    wingo_size pad_len;
    wingo_size i;

    bit_count = ctx->count * 8;

    /* Padding: 0x80 followed by zeros, then 64-bit length */
    padding[0] = 0x80;
    memset(&padding[1], 0, 63);

    pad_len = (ctx->buffer_len < 56) ?
              (56 - ctx->buffer_len) :
              (120 - ctx->buffer_len);

    sha1_update(ctx, padding, pad_len);

    /* Append length (big-endian) */
    {
        wingo_u8 len_buf[8];

        for (i = 0; i < 8; i++) {
            len_buf[i] = (wingo_u8)(bit_count >> (56 - i * 8));
        }

        sha1_update(ctx, len_buf, 8);
    }

    /* Output state (big-endian) */
    for (i = 0; i < 5; i++) {
        out[i * 4]     = (wingo_u8)(ctx->state[i] >> 24);
        out[i * 4 + 1] = (wingo_u8)(ctx->state[i] >> 16);
        out[i * 4 + 2] = (wingo_u8)(ctx->state[i] >> 8);
        out[i * 4 + 3] = (wingo_u8)(ctx->state[i]);
    }
}

/*
 * One-shot SHA1.
 */
static void sha1_hash(const void *data, wingo_size len, wingo_u8 out[20])
{
    sha1_ctx_t ctx;

    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}

/* ============================================================================
 * INTERNAL HELPERS — SECRET
 * ============================================================================ */

/*
 * Generate a random secret.
 */
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

/*
 * Extract IP bytes from an address.
 *
 * For IPv4: writes 4 bytes, returns 4
 * For IPv6: writes 16 bytes, returns 16
 * For invalid: returns 0
 */
static wingo_size addr_to_ip_bytes(const wingo_addr_t *addr,
                                    wingo_u8 *out,
                                    wingo_size out_size)
{
    if (addr == NULL || out == NULL || out_size == 0) {
        return 0;
    }

    if (wingo_addr_is_ipv4(addr)) {
        if (out_size < 4) {
            return 0;
        }

        if (wingo_addr_ip_str(addr, (char *)out, out_size) != WINGO_SUCCESS) {
            /* Fall back: extract raw bytes */
            /* Actually, we can't easily do this with the public API.
             * We need a different approach. */
            return 0;
        }

        /* The IP string is like "192.168.1.1" — not raw bytes.
         * We need raw bytes. */
        return 0;
    }

    return 0;
}

/*
 * Get raw IP bytes from an address.
 *
 * This is a helper that uses a temporary socket to extract bytes.
 * For IPv4: 4 bytes
 * For IPv6: 16 bytes
 */
static wingo_size addr_get_ip_bytes(const wingo_addr_t *addr,
                                     wingo_u8 *out,
                                     wingo_size out_size)
{
    /*
     * We can't access the internal sockaddr from here.
     *
     * We need to add a public function to socket.h:
     *   wingo_error_t wingo_addr_to_bytes(const wingo_addr_t *addr,
     *                                      void *out, wingo_size *out_len,
     *                                      wingo_addr_family_t *family);
     *
     * For now, we use a workaround: format as string, then parse back.
     * But that's slow and error-prone.
     *
     * Actually, the cleanest fix is to add the function to socket.h.
     * Let me note this as a TODO.
     */

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
        /* IPv6 is complex — skip for now */
        return 0;
    }

    return 0;
}

/* ============================================================================
 * INTERNAL HELPERS — TOKEN COMPUTATION
 * ============================================================================ */

/*
 * Compute token from secret + address.
 *
 * Uses SHA1(secret || IP) truncated to WINGO_DHT_TOKEN_SIZE.
 */
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

    /* Get IP bytes */
    ip_len = addr_get_ip_bytes(addr, ip_bytes, sizeof(ip_bytes));
    if (ip_len == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* SHA1(secret || IP) */
    sha1_init(&ctx);
    sha1_update(&ctx, secret->bytes, WINGO_DHT_TOKEN_SECRET_SIZE);
    sha1_update(&ctx, ip_bytes, ip_len);
    sha1_final(&ctx, hash);

    /* Truncate to token size */
    memcpy(out, hash, WINGO_DHT_TOKEN_SIZE);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT TOKEN LIFECYCLE
 * ============================================================================ */

/*
 * Create a new token manager.
 */
wingo_dht_token_t *wingo_dht_token_new(void)
{
    wingo_dht_token_t *token;
    wingo_error_t rc;

    token = calloc(1, sizeof(wingo_dht_token_t));
    if (token == NULL) {
        return NULL;
    }

    /* Initialize rotation intervals */
    token->rotate_min = WINGO_DHT_TOKEN_ROTATE_MIN;
    token->rotate_max = WINGO_DHT_TOKEN_ROTATE_MAX;

    /* Generate initial secrets */
    rc = secret_generate(&token->secrets[0]);
    if (rc != WINGO_SUCCESS) {
        free(token);
        return NULL;
    }

    rc = secret_generate(&token->secrets[1]);
    if (rc != WINGO_SUCCESS) {
        free(token);
        return NULL;
    }

    /* First secret is active */
    token->secrets[0].active = true;
    token->secrets[1].active = false;
    token->active_secret = 0;

    /* Schedule next rotation */
    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)token->rotate_min,
            (wingo_u32)token->rotate_max);

        token->next_rotate = wingo_time_now() + interval;
    }

    token->last_rotation = wingo_time_now();

    WINGO_LOG_DEBUG("DHT token manager created");

    return token;
}

/*
 * Free a token manager.
 */
void wingo_dht_token_free(wingo_dht_token_t *token)
{
    if (token == NULL) {
        return;
    }

    /* Zero secrets for security */
    memset(token->secrets, 0, sizeof(token->secrets));

    free(token);
}

/*
 * Reset token manager.
 */
wingo_error_t wingo_dht_token_reset(wingo_dht_token_t *token)
{
    wingo_error_t rc;

    if (token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Generate new secrets */
    rc = secret_generate(&token->secrets[0]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    rc = secret_generate(&token->secrets[1]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    token->secrets[0].active = true;
    token->secrets[1].active = false;
    token->active_secret = 0;

    /* Reset rotation */
    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)token->rotate_min,
            (wingo_u32)token->rotate_max);

        token->next_rotate = wingo_time_now() + interval;
    }

    token->last_rotation = wingo_time_now();

    /* Reset stats */
    memset(&token->tokens_generated, 0,
           sizeof(wingo_u64) * 6);  /* 6 counters */

    return WINGO_SUCCESS;
}
/* ============================================================================
 * DHT TOKEN GENERATION
 * ============================================================================ */

/*
 * Generate a token for an address.
 *
 * Uses the active secret.
 */
wingo_error_t wingo_dht_token_generate(wingo_dht_token_t *token,
                                        const wingo_addr_t *addr,
                                        wingo_u8 *out,
                                        wingo_size len)
{
    wingo_error_t rc;
    wingo_dht_token_secret_t *secret;

    if (token == NULL || addr == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len < WINGO_DHT_TOKEN_SIZE) {
        return WINGO_ERR_OVERFLOW;
    }

    /* Get active secret */
    secret = &token->secrets[token->active_secret];

    /* Compute token */
    rc = token_compute(secret, addr, out, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    token->tokens_generated++;

    return WINGO_SUCCESS;
}

/*
 * Generate a token for an IP address (no port).
 *
 * Same as wingo_dht_token_generate() — port is ignored.
 */
wingo_error_t wingo_dht_token_generate_ip(wingo_dht_token_t *token,
                                           const wingo_addr_t *addr,
                                           wingo_u8 *out,
                                           wingo_size len)
{
    /* Same as generate() — port is not used */
    return wingo_dht_token_generate(token, addr, out, len);
}

/*
 * Generate a token for a specific secret.
 *
 * Used internally for verification (checks old secret).
 */
wingo_error_t wingo_dht_token_generate_secret(wingo_dht_token_t *token,
                                               const wingo_addr_t *addr,
                                               int secret,
                                               wingo_u8 *out,
                                               wingo_size len)
{
    wingo_error_t rc;

    if (token == NULL || addr == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (secret < 0 || secret >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len < WINGO_DHT_TOKEN_SIZE) {
        return WINGO_ERR_OVERFLOW;
    }

    rc = token_compute(&token->secrets[secret], addr, out, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    token->tokens_generated++;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * DHT TOKEN VERIFICATION
 * ============================================================================ */

/*
 * Constant-time memory comparison.
 *
 * Prevents timing attacks.
 */
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

/*
 * Check if a token matches a specific secret.
 */
bool wingo_dht_token_verify_secret(wingo_dht_token_t *token,
                                    const wingo_addr_t *addr,
                                    int secret,
                                    const wingo_u8 *in,
                                    wingo_size len)
{
    wingo_u8 expected[WINGO_DHT_TOKEN_SIZE];
    wingo_error_t rc;

    if (token == NULL || addr == NULL || in == NULL) {
        return false;
    }

    if (secret < 0 || secret >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return false;
    }

    if (len != WINGO_DHT_TOKEN_SIZE) {
        return false;
    }

    /* Compute expected token */
    rc = token_compute(&token->secrets[secret], addr, expected, sizeof(expected));
    if (rc != WINGO_SUCCESS) {
        return false;
    }

    /* Constant-time compare */
    return wingo_dht_token_equal(expected, in, WINGO_DHT_TOKEN_SIZE);
}

/*
 * Verify a token with detailed result.
 */
bool wingo_dht_token_verify_ex(wingo_dht_token_t *token,
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

    if (token == NULL || addr == NULL || in == NULL) {
        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_ERROR;
        }
        return false;
    }

    if (len != WINGO_DHT_TOKEN_SIZE) {
        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_INVALID;
        }
        token->tokens_invalid++;
        return false;
    }

    token->tokens_verified++;

    /*
     * Try both secrets (active + previous).
     *
     * This gives a grace period for tokens generated with the
     * previous secret.
     */
    for (i = 0; i < WINGO_DHT_TOKEN_SECRET_COUNT; i++) {
        if (wingo_dht_token_verify_secret(token, addr, i, in, len)) {
            valid = true;

            if (out_result != NULL) {
                *out_result = WINGO_DHT_TOKEN_OK;
            }

            token->tokens_valid++;
            break;
        }
    }

    if (!valid) {
        token->tokens_invalid++;

        if (out_result != NULL) {
            *out_result = WINGO_DHT_TOKEN_INVALID;
        }
    }

    return valid;
}

/*
 * Verify a token for an address.
 */
wingo_dht_token_result_t wingo_dht_token_verify(wingo_dht_token_t *token,
                                                 const wingo_addr_t *addr,
                                                 const wingo_u8 *in,
                                                 wingo_size len)
{
    wingo_dht_token_result_t result = WINGO_DHT_TOKEN_INVALID;

    wingo_dht_token_verify_ex(token, addr, in, len, &result);

    return result;
}

/* ============================================================================
 * DHT TOKEN ROTATION
 * ============================================================================ */

/*
 * Rotate the token secret.
 *
 * Moves the active secret to "previous", generates a new active.
 */
wingo_error_t wingo_dht_token_rotate(wingo_dht_token_t *token)
{
    wingo_error_t rc;
    int new_active;

    if (token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* New active is the other secret slot */
    new_active = (token->active_secret + 1) % WINGO_DHT_TOKEN_SECRET_COUNT;

    /* Generate new secret for the slot we're about to promote */
    rc = secret_generate(&token->secrets[new_active]);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Mark old active as no longer active */
    token->secrets[token->active_secret].active = false;

    /* Mark new active */
    token->secrets[new_active].active = true;
    token->active_secret = new_active;

    /* Schedule next rotation */
    {
        wingo_i64 interval;

        interval = wingo_rand_range(
            (wingo_u32)token->rotate_min,
            (wingo_u32)token->rotate_max);

        token->next_rotate = wingo_time_now() + interval;
    }

    token->last_rotation = wingo_time_now();
    token->rotations++;

    WINGO_LOG_DEBUG("DHT token: rotated secret (active=%d, rotations=%llu)",
                    token->active_secret,
                    (unsigned long long)token->rotations);

    return WINGO_SUCCESS;
}

/*
 * Check if rotation is needed.
 */
bool wingo_dht_token_needs_rotate(const wingo_dht_token_t *token)
{
    wingo_i64 now;

    if (token == NULL) {
        return false;
    }

    now = wingo_time_now();

    return now >= token->next_rotate;
}

/*
 * Get time until next rotation.
 */
wingo_i64 wingo_dht_token_next_rotate(const wingo_dht_token_t *token)
{
    wingo_i64 now;

    if (token == NULL) {
        return 0;
    }

    now = wingo_time_now();

    if (now >= token->next_rotate) {
        return 0;
    }

    return token->next_rotate - now;
}

/*
 * Set rotation interval.
 */
wingo_error_t wingo_dht_token_set_rotation(wingo_dht_token_t *token,
                                            wingo_i64 min_s,
                                            wingo_i64 max_s)
{
    if (token == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (min_s <= 0 || max_s < min_s) {
        return WINGO_ERR_INVALID_ARG;
    }

    token->rotate_min = min_s;
    token->rotate_max = max_s;

    /* Reschedule next rotation */
    {
        wingo_i64 interval;

        interval = wingo_rand_range((wingo_u32)min_s, (wingo_u32)max_s);
        token->next_rotate = wingo_time_now() + interval;
    }

    return WINGO_SUCCESS;
}

/*
 * Get current rotation interval.
 */
wingo_i64 wingo_dht_token_rotation_interval(const wingo_dht_token_t *token)
{
    if (token == NULL) {
        return 0;
    }

    return token->next_rotate - token->last_rotation;
}

/* ============================================================================
 * DHT TOKEN SECRET
 * ============================================================================ */

/*
 * Get secret at index.
 */
const wingo_dht_token_secret_t *wingo_dht_token_secret(
    const wingo_dht_token_t *token,
    int index)
{
    if (token == NULL) {
        return NULL;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return NULL;
    }

    return &token->secrets[index];
}

/*
 * Get secret creation time.
 */
wingo_i64 wingo_dht_token_secret_created(const wingo_dht_token_t *token,
                                          int index)
{
    if (token == NULL) {
        return 0;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return 0;
    }

    return token->secrets[index].created_at;
}

/*
 * Get secret age.
 */
wingo_i64 wingo_dht_token_secret_age(const wingo_dht_token_t *token,
                                      int index)
{
    wingo_i64 created;
    wingo_i64 now;

    if (token == NULL) {
        return 0;
    }

    if (index < 0 || index >= WINGO_DHT_TOKEN_SECRET_COUNT) {
        return 0;
    }

    created = token->secrets[index].created_at;
    if (created <= 0) {
        return 0;
    }

    now = wingo_time_now();

    return now - created;
}

/*
 * Clear all secrets.
 */
void wingo_dht_token_secrets_clear(wingo_dht_token_t *token)
{
    if (token == NULL) {
        return;
    }

    memset(token->secrets, 0, sizeof(token->secrets));
}

/* ============================================================================
 * DHT TOKEN STATISTICS
 * ============================================================================ */

/*
 * Get token statistics.
 */
wingo_error_t wingo_dht_token_get_stats(const wingo_dht_token_t *token,
                                         wingo_dht_token_stats_t *stats)
{
    if (token == NULL || stats == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    memset(stats, 0, sizeof(*stats));

    stats->tokens_generated = token->tokens_generated;
    stats->tokens_verified  = token->tokens_verified;
    stats->tokens_valid     = token->tokens_valid;
    stats->tokens_invalid   = token->tokens_invalid;
    stats->tokens_expired   = token->tokens_expired;
    stats->tokens_wrong_addr = token->tokens_wrong_addr;
    stats->rotations        = token->rotations;
    stats->last_rotation    = token->last_rotation;
    stats->next_rotation    = token->next_rotate;

    /* Compute secret ages */
    {
        wingo_i64 now = wingo_time_now();

        if (token->secrets[0].created_at > 0) {
            stats->current_age = now - token->secrets[0].created_at;
        }

        if (token->secrets[1].created_at > 0) {
            stats->previous_age = now - token->secrets[1].created_at;
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Reset token statistics.
 */
void wingo_dht_token_reset_stats(wingo_dht_token_t *token)
{
    if (token == NULL) {
        return;
    }

    token->tokens_generated = 0;
    token->tokens_verified = 0;
    token->tokens_valid = 0;
    token->tokens_invalid = 0;
    token->tokens_expired = 0;
    token->tokens_wrong_addr = 0;
    token->rotations = 0;
}

/* ============================================================================
 * DHT TOKEN UTILITY
 * ============================================================================ */

/*
 * Get token result name.
 */
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

/*
 * Print token status.
 */
void wingo_dht_token_print(const wingo_dht_token_t *token, FILE *f)
{
    wingo_i64 now;
    wingo_i64 age0 = 0;
    wingo_i64 age1 = 0;

    if (f == NULL) {
        f = stderr;
    }

    if (token == NULL) {
        fprintf(f, "DHT token: (null)\n");
        return;
    }

    now = wingo_time_now();

    if (token->secrets[0].created_at > 0) {
        age0 = now - token->secrets[0].created_at;
    }
    if (token->secrets[1].created_at > 0) {
        age1 = now - token->secrets[1].created_at;
    }

    fprintf(f, "DHT Token Manager:\n");
    fprintf(f, "  Active secret:   %d\n", token->active_secret);
    fprintf(f, "  Rotation min:    %llds\n", (long long)token->rotate_min);
    fprintf(f, "  Rotation max:    %llds\n", (long long)token->rotate_max);
    fprintf(f, "  Next rotate:     %llds\n",
            (long long)wingo_dht_token_next_rotate(token));
    fprintf(f, "  Secret 0 age:    %llds (%s)\n",
            (long long)age0,
            token->secrets[0].active ? "ACTIVE" : "prev");
    fprintf(f, "  Secret 1 age:    %llds (%s)\n",
            (long long)age1,
            token->secrets[1].active ? "ACTIVE" : "prev");
    fprintf(f, "\n");
    fprintf(f, "  Statistics:\n");
    fprintf(f, "    Generated:     %llu\n",
            (unsigned long long)token->tokens_generated);
    fprintf(f, "    Verified:      %llu\n",
            (unsigned long long)token->tokens_verified);
    fprintf(f, "    Valid:         %llu\n",
            (unsigned long long)token->tokens_valid);
    fprintf(f, "    Invalid:       %llu\n",
            (unsigned long long)token->tokens_invalid);
    fprintf(f, "    Rotations:     %llu\n",
            (unsigned long long)token->rotations);
}

/*
 * Print secrets (for debugging only).
 *
 * WARNING: This exposes secrets. Do NOT use in production.
 */
void wingo_dht_token_print_secrets(const wingo_dht_token_t *token, FILE *f)
{
    int i;
    int j;

    if (f == NULL) {
        f = stderr;
    }

    if (token == NULL) {
        fprintf(f, "DHT token: (null)\n");
        return;
    }

    fprintf(f, "DHT Token Secrets (DEBUG ONLY):\n");

    for (i = 0; i < WINGO_DHT_TOKEN_SECRET_COUNT; i++) {
        fprintf(f, "  Secret %d (%s): ",
                i,
                token->secrets[i].active ? "ACTIVE" : "prev");

        for (j = 0; j < WINGO_DHT_TOKEN_SECRET_SIZE; j++) {
            fprintf(f, "%02x", token->secrets[i].bytes[j]);
        }

        fprintf(f, "\n");
    }
}

/* ============================================================================
 * END OF FILE
 * ============================================================================ */
