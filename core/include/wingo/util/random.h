
/*
 * Wingo — P2P Internet Sharing Tool
 * Copyright (C) 2024 Wingo Project
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

#ifndef WINGO_UTIL_RANDOM_H
#define WINGO_UTIL_RANDOM_H

/*
 * ============================================================================
 * WINGO RANDOM
 * ============================================================================
 *
 * This header provides:
 *   - Cryptographic random bytes
 *   - Non-cryptographic random numbers
 *   - Random ID generation
 *   - Random string generation
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * RANDOM INITIALIZATION
 * ============================================================================ */

/*
 * Initialize random subsystem.
 *
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_init(void);

/*
 * Shutdown random subsystem.
 */

void wingo_random_shutdown(void);

/* ============================================================================
 * CRYPTOGRAPHIC RANDOM
 * ============================================================================ */

/*
 * Fill buffer with cryptographic random bytes.
 *
 * Uses getrandom() on Linux, /dev/urandom as fallback.
 *
 * @param buf       Output buffer
 * @param len       Number of bytes
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_bytes(void *buf, wingo_size len);

/*
 * Generate a random 8-bit unsigned integer.
 *
 * @return          Random value
 */

wingo_u8 wingo_random_u8(void);

/*
 * Generate a random 16-bit unsigned integer.
 *
 * @return          Random value
 */

wingo_u16 wingo_random_u16(void);

/*
 * Generate a random 32-bit unsigned integer.
 *
 * @return          Random value
 */

wingo_u32 wingo_random_u32(void);

/*
 * Generate a random 64-bit unsigned integer.
 *
 * @return          Random value
 */

wingo_u64 wingo_random_u64(void);

/*
 * Generate a random 32-bit integer in a range.
 *
 * @param min       Minimum value (inclusive)
 * @param max       Maximum value (inclusive)
 * @return          Random value in [min, max]
 */

wingo_u32 wingo_random_range_u32(wingo_u32 min, wingo_u32 max);

/*
 * Generate a random 64-bit integer in a range.
 *
 * @param min       Minimum value (inclusive)
 * @param max       Maximum value (inclusive)
 * @return          Random value in [min, max]
 */

wingo_u64 wingo_random_range_u64(wingo_u64 min, wingo_u64 max);

/*
 * Generate a random double in [0.0, 1.0).
 *
 * @return          Random double
 */

double wingo_random_double(void);

/*
 * Generate a random double in a range.
 *
 * @param min       Minimum value
 * @param max       Maximum value
 * @return          Random double in [min, max)
 */

double wingo_random_range_double(double min, double max);

/* ============================================================================
 * RANDOM ID GENERATION
 * ============================================================================ */

/*
 * Generate a random ID (20 bytes).
 *
 * @param id        Output ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_id(wingo_id *id);

/*
 * Generate a random ID as hex string.
 *
 * @param buf       Output buffer (at least WINGO_ID_HEX_SIZE bytes)
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_id_hex(char *buf, wingo_size size);

/*
 * Generate a random UUID (version 4).
 *
 * @param buf       Output buffer (at least 37 bytes)
 * @param size      Buffer size
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_uuid(char *buf, wingo_size size);

/* ============================================================================
 * RANDOM STRING GENERATION
 * ============================================================================ */

/*
 * Generate a random string from a character set.
 *
 * @param buf       Output buffer
 * @param size      Buffer size (including null terminator)
 * @param charset   Character set (NULL for alphanumeric)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_string(char *buf, wingo_size size, const char *charset);

/*
 * Generate a random alphanumeric string.
 *
 * @param buf       Output buffer
 * @param size      Buffer size (including null terminator)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_alnum(char *buf, wingo_size size);

/*
 * Generate a random hex string.
 *
 * @param buf       Output buffer
 * @param size      Buffer size (including null terminator)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_random_hex(char *buf, wingo_size size);

/* ============================================================================
 * NON-CRYPTOGRAPHIC RANDOM
 * ============================================================================ */

/*
 * Seed the non-cryptographic random number generator.
 *
 * @param seed      Seed value
 */

void wingo_random_seed(wingo_u64 seed);

/*
 * Generate a non-cryptographic random 32-bit integer.
 *
 * @return          Random value
 */

wingo_u32 wingo_rand_u32(void);

/*
 * Generate a non-cryptographic random 64-bit integer.
 *
 * @return          Random value
 */

wingo_u64 wingo_rand_u64(void);

/*
 * Generate a non-cryptographic random integer in a range.
 *
 * @param min       Minimum value (inclusive)
 * @param max       Maximum value (inclusive)
 * @return          Random value in [min, max]
 */

wingo_u32 wingo_rand_range(wingo_u32 min, wingo_u32 max);

/*
 * Generate a non-cryptographic random double in [0.0, 1.0).
 *
 * @return          Random double
 */

double wingo_rand_double(void);

/* ============================================================================
 * SHUFFLE
 * ============================================================================ */

/*
 * Shuffle an array of pointers (Fisher-Yates).
 *
 * @param array     Array of pointers
 * @param count     Number of elements
 */

void wingo_shuffle(void **array, wingo_size count);

/*
 * Shuffle an array of bytes (Fisher-Yates).
 *
 * @param array     Array of bytes
 * @param count     Number of elements
 * @param elem_size Size of each element
 */

void wingo_shuffle_bytes(void *array, wingo_size count, wingo_size elem_size);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_RANDOM_H */
