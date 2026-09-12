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

#include "wingo/util/random.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <sys/random.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Maximum number of bytes to request from getrandom() in one call.
 * Linux caps this at 256 bytes for the non-blocking variant.
 */
#define RANDOM_MAX_GETRANDOM        256

/*
 * Path to /dev/urandom fallback.
 */
#define RANDOM_DEV_URANDOM          "/dev/urandom"

/*
 * Hex character set.
 */
#define RANDOM_HEX_CHARS            "0123456789abcdef"

/*
 * Alphanumeric character set.
 */
#define RANDOM_ALNUM_CHARS          \
    "0123456789"                    \
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"    \
    "abcdefghijklmnopqrstuvwxyz"

/* ============================================================================
 * INTERNAL STATE
 * ============================================================================ */

/*
 * Fallback file descriptor for /dev/urandom.
 * Opened on first use, kept open for performance.
 */
static int urandom_fd = -1;

/*
 * State for non-cryptographic PRNG (xorshift64*).
 *
 * This is NOT cryptographically secure. Only use for:
 *   - Shuffling (non-security)
 *   - Random delays
 *   - Hash table seeds
 *
 * For crypto (keys, nonces, IDs), use wingo_random_bytes().
 */
static wingo_u64 prng_state = 0;
static bool prng_seeded = false;

/* ============================================================================
 * RANDOM INITIALIZATION
 * ============================================================================ */

wingo_error_t wingo_random_init(void)
{
    /*
     * Seed non-crypto PRNG from crypto source.
     * This ensures PRNG starts with good entropy.
     */
    wingo_u64 seed;

    if (wingo_random_bytes(&seed, sizeof(seed)) != WINGO_SUCCESS) {
        /*
         * Fallback: use time + pid.
         * Not great, but better than nothing.
         */
        seed = (wingo_u64)time(NULL);
        seed ^= (wingo_u64)getpid() << 32;
        seed ^= (wingo_u64)(uintptr_t)&seed;
    }

    wingo_random_seed(seed);

    return WINGO_SUCCESS;
}

void wingo_random_shutdown(void)
{
    /*
     * Close /dev/urandom fd if open.
     */
    if (urandom_fd >= 0) {
        close(urandom_fd);
        urandom_fd = -1;
    }

    /*
     * Zero PRNG state (for security).
     */
    prng_state  = 0;
    prng_seeded = false;
}

/* ============================================================================
 * CRYPTOGRAPHIC RANDOM
 * ============================================================================ */

/*
 * Read from /dev/urandom.
 *
 * This is the fallback when getrandom() is unavailable
 * or returns EAGAIN (e.g., early boot before entropy pool
 * is initialized).
 */
static wingo_error_t random_from_urandom(void *buf, wingo_size len)
{
    wingo_u8 *ptr = (wingo_u8 *)buf;
    wingo_size remaining = len;
    ssize_t n;

    /* Open /dev/urandom if not already open */
    if (urandom_fd < 0) {
        urandom_fd = open(RANDOM_DEV_URANDOM, O_RDONLY | O_CLOEXEC);
        if (urandom_fd < 0) {
            return WINGO_ERR_FILE_OPEN;
        }
    }

    /* Read in a loop (read can return short) */
    while (remaining > 0) {
        n = read(urandom_fd, ptr, remaining);

        if (n < 0) {
            if (errno == EINTR) {
                /* Interrupted — retry */
                continue;
            }
            return WINGO_ERR_FILE_READ;
        }

        if (n == 0) {
            /* EOF — should never happen for /dev/urandom */
            return WINGO_ERR_FILE_EOF;
        }

        ptr += n;
        remaining -= (wingo_size)n;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_random_bytes(void *buf, wingo_size len)
{
    wingo_u8 *ptr;
    wingo_size remaining;
    ssize_t n;

    if (buf == NULL && len > 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    ptr = (wingo_u8 *)buf;
    remaining = len;

    /*
     * Try getrandom() first — it's the modern, preferred way.
     * It doesn't require opening a file and doesn't consume fd.
     */
    while (remaining > 0) {
        wingo_size chunk = (remaining > RANDOM_MAX_GETRANDOM)
                         ? RANDOM_MAX_GETRANDOM
                         : remaining;

        n = getrandom(ptr, chunk, 0);

        if (n < 0) {
            if (errno == EINTR) {
                /* Interrupted — retry */
                continue;
            }

            if (errno == EAGAIN || errno == ENOSYS) {
                /*
                 * getrandom() not available or would block.
                 * Fall back to /dev/urandom.
                 */
                return random_from_urandom(buf, len);
            }

            /* Other error — try urandom as fallback */
            return random_from_urandom(buf, len);
        }

        if (n == 0) {
            /* Shouldn't happen, but be safe */
            return random_from_urandom(buf, len);
        }

        ptr += n;
        remaining -= (wingo_size)n;
    }

    return WINGO_SUCCESS;
}

wingo_u8 wingo_random_u8(void)
{
    wingo_u8 value = 0;
    wingo_random_bytes(&value, sizeof(value));
    return value;
}

wingo_u16 wingo_random_u16(void)
{
    wingo_u16 value = 0;
    wingo_random_bytes(&value, sizeof(value));
    return value;
}

wingo_u32 wingo_random_u32(void)
{
    wingo_u32 value = 0;
    wingo_random_bytes(&value, sizeof(value));
    return value;
}

wingo_u64 wingo_random_u64(void)
{
    wingo_u64 value = 0;
    wingo_random_bytes(&value, sizeof(value));
    return value;
}

/*
 * Uniform random in range [min, max].
 *
 * Uses rejection sampling to avoid modulo bias.
 *
 * Modulo bias: if range doesn't divide evenly into
 * the space of random values, some outputs are more
 * likely than others. Rejection sampling fixes this.
 */
wingo_u32 wingo_random_range_u32(wingo_u32 min, wingo_u32 max)
{
    wingo_u32 range;
    wingo_u32 threshold;
    wingo_u32 r;

    if (min > max) {
        wingo_u32 tmp = min;
        min = max;
        max = tmp;
    }

    if (min == max) {
        return min;
    }

    /*
     * Range is (max - min + 1). Careful with overflow.
     */
    range = max - min + 1;

    if (range == 0) {
        /* Full u32 range — any value is fine */
        return wingo_random_u32();
    }

    /*
     * Rejection sampling:
     * Compute largest multiple of range that fits in u32.
     * Reject any value >= threshold.
     */
    threshold = (wingo_u32)(-1) - ((wingo_u32)(-1) % range);

    do {
        r = wingo_random_u32();
    } while (r >= threshold);

    return min + (r % range);
}

wingo_u64 wingo_random_range_u64(wingo_u64 min, wingo_u64 max)
{
    wingo_u64 range;
    wingo_u64 threshold;
    wingo_u64 r;

    if (min > max) {
        wingo_u64 tmp = min;
        min = max;
        max = tmp;
    }

    if (min == max) {
        return min;
    }

    range = max - min + 1;

    if (range == 0) {
        return wingo_random_u64();
    }

    threshold = (wingo_u64)(-1) - ((wingo_u64)(-1) % range);

    do {
        r = wingo_random_u64();
    } while (r >= threshold);

    return min + (r % range);
}

/*
 * Random double in [0.0, 1.0).
 *
 * We use 53 bits of randomness (the precision of IEEE 754 double).
 * Divide by 2^53 to get value in [0, 1).
 */
double wingo_random_double(void)
{
    wingo_u64 r;

    wingo_random_bytes(&r, sizeof(r));

    /* Use top 53 bits */
    r >>= 11;

    /* 2^53 = 9007199254740992.0 */
    return (double)r / 9007199254740992.0;
}

double wingo_random_range_double(double min, double max)
{
    double r = wingo_random_double();

    if (min > max) {
        double tmp = min;
        min = max;
        max = tmp;
    }

    return min + r * (max - min);
}

/* ============================================================================
 * RANDOM ID GENERATION
 * ============================================================================ */

wingo_error_t wingo_random_id(wingo_id *id)
{
    if (id == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return wingo_random_bytes(id->bytes, WINGO_ID_SIZE);
}

wingo_error_t wingo_random_id_hex(char *buf, wingo_size size)
{
    wingo_id id;
    wingo_error_t rc;

    if (buf == NULL || size < WINGO_ID_HEX_SIZE) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_random_id(&id);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    wingo_id_to_hex(&id, buf);

    return WINGO_SUCCESS;
}

/*
 * Generate UUID v4.
 *
 * Format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
 *   where x is random hex digit
 *   and y is one of 8, 9, a, b.
 *
 * Total: 36 characters + null terminator = 37 bytes.
 */
wingo_error_t wingo_random_uuid(char *buf, wingo_size size)
{
    wingo_u8 bytes[16];
    wingo_error_t rc;
    int i;
    int j;

    if (buf == NULL || size < 37) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = wingo_random_bytes(bytes, sizeof(bytes));
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /*
     * Set version (4) and variant (RFC 4122).
     *
     * byte[6] = (byte[6] & 0x0F) | 0x40  — version 4
     * byte[8] = (byte[8] & 0x3F) | 0x80  — variant 10xx
     */
    bytes[6] = (bytes[6] & 0x0F) | 0x40;
    bytes[8] = (bytes[8] & 0x3F) | 0x80;

    /*
     * Format as string.
     *
     * Positions of hyphens: 8, 13, 18, 23
     */
    j = 0;
    for (i = 0; i < 16; i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            buf[j++] = '-';
        }

        buf[j++] = RANDOM_HEX_CHARS[(bytes[i] >> 4) & 0x0F];
        buf[j++] = RANDOM_HEX_CHARS[bytes[i] & 0x0F];
    }

    buf[j] = '\0';

    return WINGO_SUCCESS;
}

/* ============================================================================
 * RANDOM STRING GENERATION
 * ============================================================================ */

/*
 * Generate random string from given charset.
 *
 * Uses rejection sampling to avoid modulo bias.
 */
wingo_error_t wingo_random_string(char *buf, wingo_size size, const char *charset)
{
    wingo_size charset_len;
    wingo_size i;
    wingo_size target_len;

    if (buf == NULL || size == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (charset == NULL) {
        charset = RANDOM_ALNUM_CHARS;
    }

    charset_len = strlen(charset);
    if (charset_len == 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Generate size-1 characters (leave room for null) */
    target_len = size - 1;

    for (i = 0; i < target_len; i++) {
        wingo_u32 idx;

        if (charset_len == 1) {
            idx = 0;
        } else {
            idx = wingo_random_range_u32(0, (wingo_u32)(charset_len - 1));
        }

        buf[i] = charset[idx];
    }

    buf[target_len] = '\0';

    return WINGO_SUCCESS;
}

wingo_error_t wingo_random_alnum(char *buf, wingo_size size)
{
    return wingo_random_string(buf, size, RANDOM_ALNUM_CHARS);
}

wingo_error_t wingo_random_hex(char *buf, wingo_size size)
{
    return wingo_random_string(buf, size, RANDOM_HEX_CHARS);
}

/* ============================================================================
 * NON-CRYPTOGRAPHIC RANDOM
 * ============================================================================ */

/*
 * xorshift64* — fast, decent-quality non-crypto PRNG.
 *
 * Reference: Vigna, "Further scramblings of Marsaglia's xorshift generators"
 *
 * This is NOT cryptographically secure. Do not use for:
 *   - Keys
 *   - Nonces
 *   - Session tokens
 *   - Anything security-related
 *
 * Good for:
 *   - Shuffling
 *   - Hash table seeds
 *   - Random delays
 *   - Simulation
 */
static wingo_u64 xorshift64star(void)
{
    wingo_u64 x = prng_state;

    /*
     * xorshift64* algorithm:
     *   x ^= x >> 12
     *   x ^= x << 25
     *   x ^= x >> 27
     *   return x * 2685821657736338717
     */
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;

    prng_state = x;

    return x * 2685821657736338717ULL;
}

void wingo_random_seed(wingo_u64 seed)
{
    /*
     * xorshift64 requires non-zero state.
     * If seed is zero, use a fixed non-zero constant.
     */
    if (seed == 0) {
        seed = 0x9E3779B97F4A7C15ULL;  /* Golden ratio */
    }

    prng_state  = seed;
    prng_seeded = true;
}

wingo_u32 wingo_rand_u32(void)
{
    if (!prng_seeded) {
        /*
         * Auto-seed if user forgot.
         * This is not ideal, but better than crash.
         */
        wingo_random_init();
    }

    return (wingo_u32)(xorshift64star() >> 32);
}

wingo_u64 wingo_rand_u64(void)
{
    if (!prng_seeded) {
        wingo_random_init();
    }

    return xorshift64star();
}

wingo_u32 wingo_rand_range(wingo_u32 min, wingo_u32 max)
{
    wingo_u32 range;
    wingo_u32 threshold;
    wingo_u32 r;

    if (min > max) {
        wingo_u32 tmp = min;
        min = max;
        max = tmp;
    }

    if (min == max) {
        return min;
    }

    range = max - min + 1;

    if (range == 0) {
        return wingo_rand_u32();
    }

    threshold = (wingo_u32)(-1) - ((wingo_u32)(-1) % range);

    do {
        r = wingo_rand_u32();
    } while (r >= threshold);

    return min + (r % range);
}

double wingo_rand_double(void)
{
    wingo_u64 r;

    if (!prng_seeded) {
        wingo_random_init();
    }

    r = xorshift64star();

    /* Use top 53 bits */
    r >>= 11;

    return (double)r / 9007199254740992.0;
}

/* ============================================================================
 * SHUFFLE
 * ============================================================================ */

/*
 * Fisher-Yates shuffle for array of pointers.
 *
 * For each i from n-1 down to 1:
 *   j = random in [0, i]
 *   swap(a[i], a[j])
 *
 * This gives uniform random permutation.
 */
void wingo_shuffle(void **array, wingo_size count)
{
    wingo_size i;
    wingo_size j;
    void *tmp;

    if (array == NULL || count < 2) {
        return;
    }

    for (i = count - 1; i > 0; i--) {
        j = wingo_rand_range(0, (wingo_u32)i);

        tmp = array[i];
        array[i] = array[j];
        array[j] = tmp;
    }
}

/*
 * Fisher-Yates shuffle for generic array.
 *
 * Works on any element type by using elem_size.
 * Uses a temporary buffer for swapping (up to 256 bytes).
 *
 * For larger element sizes, use pointer shuffle instead.
 */
void wingo_shuffle_bytes(void *array, wingo_size count, wingo_size elem_size)
{
    wingo_u8 *base = (wingo_u8 *)array;
    wingo_u8 tmp[256];
    wingo_size i;
    wingo_size j;

    if (array == NULL || count < 2 || elem_size == 0) {
        return;
    }

    if (elem_size > sizeof(tmp)) {
        /*
         * Element too large for stack buffer.
         * This is a caller error — use pointer shuffle.
         */
        return;
    }

    for (i = count - 1; i > 0; i--) {
        j = wingo_rand_range(0, (wingo_u32)i);

        if (i == j) {
            continue;
        }

        /* Swap base[i] and base[j] */
        memcpy(tmp, base + i * elem_size, elem_size);
        memcpy(base + i * elem_size, base + j * elem_size, elem_size);
        memcpy(base + j * elem_size, tmp, elem_size);
    }
}
