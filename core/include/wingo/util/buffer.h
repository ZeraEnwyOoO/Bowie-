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

#ifndef WINGO_UTIL_BUFFER_H
#define WINGO_UTIL_BUFFER_H

/*
 * ============================================================================
 * WINGO BUFFER
 * ============================================================================
 *
 * This header provides:
 *   - Dynamic byte buffer
 *   - Append, prepend, insert, remove operations
 *   - Resize, reserve, clear operations
 *   - Read/write helpers
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * BUFFER STRUCTURE
 * ============================================================================ */

/*
 * Dynamic byte buffer.
 *
 * data:    Pointer to allocated memory
 * len:     Current length of data
 * cap:     Allocated capacity
 * read:    Read position (for read operations)
 */

typedef struct wingo_buf {
    wingo_u8   *data;
    wingo_size  len;
    wingo_size  cap;
    wingo_size  read;
} wingo_buf_t;

/* ============================================================================
 * BUFFER CREATION & DESTRUCTION
 * ============================================================================ */

/*
 * Create a new buffer with the given capacity.
 *
 * @param capacity  Initial capacity (0 for default)
 * @return          New buffer, or NULL on error
 */

wingo_buf_t *wingo_buf_new(wingo_size capacity);

/*
 * Create a new buffer from existing data.
 *
 * @param data      Data to copy
 * @param len       Length of data
 * @return          New buffer, or NULL on error
 */

wingo_buf_t *wingo_buf_new_from(const void *data, wingo_size len);

/*
 * Create a new buffer with a specific size (len = cap = size).
 *
 * @param size      Size of buffer
 * @return          New buffer, or NULL on error
 */

wingo_buf_t *wingo_buf_new_sized(wingo_size size);

/*
 * Free a buffer.
 *
 * @param buf       Buffer to free (NULL is safe)
 */

void wingo_buf_free(wingo_buf_t *buf);

/*
 * Reset a buffer (len = 0, read = 0, data zeroed).
 *
 * @param buf       Buffer to reset
 */

void wingo_buf_reset(wingo_buf_t *buf);

/* ============================================================================
 * BUFFER RESIZE & RESERVE
 * ============================================================================ */

/*
 * Resize buffer to a new capacity.
 *
 * @param buf       Buffer
 * @param capacity  New capacity
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_resize(wingo_buf_t *buf, wingo_size capacity);

/*
 * Ensure buffer has at least the given capacity.
 *
 * @param buf       Buffer
 * @param capacity  Minimum capacity
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_reserve(wingo_buf_t *buf, wingo_size capacity);

/*
 * Ensure buffer has room for at least additional bytes.
 *
 * @param buf       Buffer
 * @param additional Number of additional bytes needed
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_ensure(wingo_buf_t *buf, wingo_size additional);

/* ============================================================================
 * BUFFER APPEND
 * ============================================================================ */

/*
 * Append data to buffer.
 *
 * @param buf       Buffer
 * @param data      Data to append
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_append(wingo_buf_t *buf, const void *data, wingo_size len);

/*
 * Append a single byte.
 *
 * @param buf       Buffer
 * @param byte      Byte to append
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_append_byte(wingo_buf_t *buf, wingo_u8 byte);

/*
 * Append a null-terminated string.
 *
 * @param buf       Buffer
 * @param str       String to append
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_append_str(wingo_buf_t *buf, const char *str);

/*
 * Append a formatted string.
 *
 * @param buf       Buffer
 * @param fmt       Format string
 * @param ...       Format arguments
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_append_printf(wingo_buf_t *buf, const char *fmt, ...)
    WINGO_ATTR_FORMAT(2, 3);

/*
 * Append another buffer.
 *
 * @param buf       Destination buffer
 * @param src       Source buffer
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_append_buf(wingo_buf_t *buf, const wingo_buf_t *src);

/* ============================================================================
 * BUFFER PREPEND
 * ============================================================================ */

/*
 * Prepend data to buffer.
 *
 * @param buf       Buffer
 * @param data      Data to prepend
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_prepend(wingo_buf_t *buf, const void *data, wingo_size len);

/*
 * Prepend a single byte.
 *
 * @param buf       Buffer
 * @param byte      Byte to prepend
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_prepend_byte(wingo_buf_t *buf, wingo_u8 byte);

/* ============================================================================
 * BUFFER INSERT & REMOVE
 * ============================================================================ */

/*
 * Insert data at a specific position.
 *
 * @param buf       Buffer
 * @param pos       Position (0 to len)
 * @param data      Data to insert
 * @param len       Length of data
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_insert(wingo_buf_t *buf, wingo_size pos,
                               const void *data, wingo_size len);

/*
 * Remove data from a specific position.
 *
 * @param buf       Buffer
 * @param pos       Position (0 to len)
 * @param len       Number of bytes to remove
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_remove(wingo_buf_t *buf, wingo_size pos, wingo_size len);

/*
 * Remove from the beginning.
 *
 * @param buf       Buffer
 * @param len       Number of bytes to remove
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_remove_front(wingo_buf_t *buf, wingo_size len);

/*
 * Remove from the end.
 *
 * @param buf       Buffer
 * @param len       Number of bytes to remove
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_remove_back(wingo_buf_t *buf, wingo_size len);

/* ============================================================================
 * BUFFER READ
 * ============================================================================ */

/*
 * Read data from buffer at read position.
 *
 * @param buf       Buffer
 * @param out       Output buffer
 * @param len       Number of bytes to read
 * @return          Number of bytes read
 */

wingo_size wingo_buf_read(wingo_buf_t *buf, void *out, wingo_size len);

/*
 * Read a single byte.
 *
 * @param buf       Buffer
 * @param out       Output byte
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_read_byte(wingo_buf_t *buf, wingo_u8 *out);

/*
 * Peek data without advancing read position.
 *
 * @param buf       Buffer
 * @param out       Output buffer
 * @param len       Number of bytes to peek
 * @return          Number of bytes peeked
 */

wingo_size wingo_buf_peek(const wingo_buf_t *buf, void *out, wingo_size len);

/*
 * Skip bytes (advance read position).
 *
 * @param buf       Buffer
 * @param len       Number of bytes to skip
 * @return          Number of bytes skipped
 */

wingo_size wingo_buf_skip(wingo_buf_t *buf, wingo_size len);

/*
 * Get remaining bytes to read.
 *
 * @param buf       Buffer
 * @return          Remaining bytes
 */

static inline wingo_size wingo_buf_remaining(const wingo_buf_t *buf)
{
    if (buf == NULL)
        return 0;
    return buf->len - buf->read;
}

/*
 * Check if buffer has more data to read.
 *
 * @param buf       Buffer
 * @return          true if more data, false otherwise
 */

static inline bool wingo_buf_has_more(const wingo_buf_t *buf)
{
    return wingo_buf_remaining(buf) > 0;
}

/* ============================================================================
 * BUFFER SEARCH
 * ============================================================================ */

/*
 * Find a byte in buffer.
 *
 * @param buf       Buffer
 * @param byte      Byte to find
 * @param start     Start position
 * @return          Position, or (wingo_size)-1 if not found
 */

wingo_size wingo_buf_find_byte(const wingo_buf_t *buf, wingo_u8 byte, wingo_size start);

/*
 * Find data in buffer.
 *
 * @param buf       Buffer
 * @param data      Data to find
 * @param len       Length of data
 * @param start     Start position
 * @return          Position, or (wingo_size)-1 if not found
 */

wingo_size wingo_buf_find(const wingo_buf_t *buf, const void *data,
                          wingo_size len, wingo_size start);

/* ============================================================================
 * BUFFER COMPARE
 * ============================================================================ */

/*
 * Compare two buffers.
 *
 * @param a         First buffer
 * @param b         Second buffer
 * @return          <0 if a<b, 0 if a==b, >0 if a>b
 */

int wingo_buf_cmp(const wingo_buf_t *a, const wingo_buf_t *b);

/*
 * Check if two buffers are equal.
 *
 * @param a         First buffer
 * @param b         Second buffer
 * @return          true if equal, false otherwise
 */

static inline bool wingo_buf_eq(const wingo_buf_t *a, const wingo_buf_t *b)
{
    if (a == NULL || b == NULL)
        return a == b;
    if (a->len != b->len)
        return false;
    return memcmp(a->data, b->data, a->len) == 0;
}

/* ============================================================================
 * BUFFER UTILITY
 * ============================================================================ */

/*
 * Duplicate a buffer.
 *
 * @param buf       Buffer to duplicate
 * @return          New buffer, or NULL on error
 */

wingo_buf_t *wingo_buf_dup(const wingo_buf_t *buf);

/*
 * Copy buffer data to a new buffer.
 *
 * @param buf       Buffer
 * @param out       Output buffer
 * @param out_len   Output length
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_buf_copy(const wingo_buf_t *buf, void *out, wingo_size *out_len);

/*
 * Swap two buffers.
 *
 * @param a         First buffer
 * @param b         Second buffer
 */

void wingo_buf_swap(wingo_buf_t *a, wingo_buf_t *b);

/*
 * Print buffer as hex to stdout (for debugging).
 *
 * @param buf       Buffer
 */

void wingo_buf_print(const wingo_buf_t *buf);

/*
 * Print buffer as hex to a file.
 *
 * @param buf       Buffer
 * @param f         File
 */

void wingo_buf_fprint(const wingo_buf_t *buf, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_BUFFER_H */
