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

#ifndef WINGO_CRYPTO_CONSTANT_TIME_H
#define WINGO_CRYPTO_CONSTANT_TIME_H

/*
 * ============================================================================
 * WINGO CRYPTO CONSTANT-TIME
 * ============================================================================
 *
 * This header provides constant-time operations for cryptographic use.
 *
 * Why constant-time?
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    TIMING ATTACKS                           │
 *   │                                                             │
 *   │   Regular memcmp():                                         │
 *   │   ├── Compare byte 0 → equal? continue                      │
 *   │   ├── Compare byte 1 → different? return early              │
 *   │   └── Attacker measures time → learns where difference is   │
 *   │                                                             │
 *   │   Constant-time compare:                                    │
 *   │   ├── Compare ALL bytes (no early return)                   │
 *   │   ├── Accumulate difference                                 │
 *   │   └── Attacker learns nothing from timing                   │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * CONSTANT-TIME COMPARE
 * ============================================================================ */

/*
 * Compare two buffers in constant time.
 *
 * This function takes the same amount of time regardless of
 * whether the buffers are equal or where they differ.
 *
 * @param a         First buffer
 * @param b         Second buffer
 * @param len       Length of buffers
 * @return          0 if equal, non-zero otherwise
 */
int wingo_ct_memcmp(const void *a, const void *b, wingo_size len);

/*
 * Check if two buffers are equal in constant time.
 *
 * @param a         First buffer
 * @param b         Second buffer
 * @param len       Length of buffers
 * @return          true if equal, false otherwise
 */
bool wingo_ct_equal(const void *a, const void *b, wingo_size len);

/* ============================================================================
 * CONSTANT-TIME SELECT
 * ============================================================================ */

/*
 * Select between two values in constant time.
 *
 * Returns a if condition is true, b if condition is false.
 * The execution time is the same regardless of condition.
 *
 * @param condition Selection condition (0 or 1)
 * @param a         Value if true
 * @param b         Value if false
 * @return          Selected value
 */
wingo_u8 wingo_ct_select_u8(wingo_u8 condition, wingo_u8 a, wingo_u8 b);

/*
 * Select between two 32-bit values in constant time.
 *
 * @param condition Selection condition (0 or 1)
 * @param a         Value if true
 * @param b         Value if false
 * @return          Selected value
 */
wingo_u32 wingo_ct_select_u32(wingo_u32 condition, wingo_u32 a, wingo_u32 b);

/*
 * Select between two 64-bit values in constant time.
 *
 * @param condition Selection condition (0 or 1)
 * @param a         Value if true
 * @param b         Value if false
 * @return          Selected value
 */
wingo_u64 wingo_ct_select_u64(wingo_u64 condition, wingo_u64 a, wingo_u64 b);

/*
 * Select between two buffers in constant time.
 *
 * @param condition Selection condition (0 or 1)
 * @param a         Value if true
 * @param b         Value if false
 * @param out       Output buffer
 * @param len       Length of buffers
 */
void wingo_ct_select_buf(wingo_u8 condition,
                          const void *a,
                          const void *b,
                          void *out,
                          wingo_size len);

/* ============================================================================
 * CONSTANT-TIME MEMORY
 * ============================================================================ */

/*
 * Copy memory in constant time.
 *
 * Unlike memcpy(), this does not leak information about
 * the source or destination through timing.
 *
 * @param dst       Destination buffer
 * @param src       Source buffer
 * @param len       Length to copy
 */
void wingo_ct_memcpy(void *dst, const void *src, wingo_size len);

/*
 * Set memory to a value in constant time.
 *
 * @param dst       Destination buffer
 * @param value     Value to set
 * @param len       Length to set
 */
void wingo_ct_memset(void *dst, wingo_u8 value, wingo_size len);

/*
 * Zero memory securely in constant time.
 *
 * This function will not be optimized away by the compiler,
 * unlike memset() which can be removed if the memory is
 * not used afterward.
 *
 * @param dst       Destination buffer
 * @param len       Length to zero
 */
void wingo_ct_memzero(void *dst, wingo_size len);

/* ============================================================================
 * CONSTANT-TIME ARITHMETIC
 * ============================================================================ */

/*
 * Check if a value is zero in constant time.
 *
 * @param value     Value to check
 * @return          1 if value is zero, 0 otherwise
 */
wingo_u8 wingo_ct_is_zero_u8(wingo_u8 value);

/*
 * Check if a 32-bit value is zero in constant time.
 *
 * @param value     Value to check
 * @return          1 if value is zero, 0 otherwise
 */
wingo_u8 wingo_ct_is_zero_u32(wingo_u32 value);

/*
 * Check if a 64-bit value is zero in constant time.
 *
 * @param value     Value to check
 * @return          1 if value is zero, 0 otherwise
 */
wingo_u8 wingo_ct_is_zero_u64(wingo_u64 value);

/*
 * Check if two values are equal in constant time.
 *
 * @param a         First value
 * @param b         Second value
 * @return          1 if equal, 0 otherwise
 */
wingo_u8 wingo_ct_equal_u8(wingo_u8 a, wingo_u8 b);

/*
 * Check if two 32-bit values are equal in constant time.
 *
 * @param a         First value
 * @param b         Second value
 * @return          1 if equal, 0 otherwise
 */
wingo_u8 wingo_ct_equal_u32(wingo_u32 a, wingo_u32 b);

/*
 * Check if two 64-bit values are equal in constant time.
 *
 * @param a         First value
 * @param b         Second value
 * @return          1 if equal, 0 otherwise
 */
wingo_u8 wingo_ct_equal_u64(wingo_u64 a, wingo_u64 b);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CRYPTO_CONSTANT_TIME_H */
