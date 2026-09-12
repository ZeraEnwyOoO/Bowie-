
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

#include "wingo/util/buffer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Default initial capacity for new buffers.
 * 64 bytes is a good balance between memory and avoiding early reallocs.
 */
#define BUFFER_DEFAULT_CAPACITY     64

/*
 * Growth factor for buffer expansion.
 * 2x is standard for dynamic arrays. It gives amortized O(1) append.
 */
#define BUFFER_GROWTH_FACTOR        2

/*
 * Maximum buffer capacity (1 GB).
 * Prevents overflow and accidental huge allocations.
 */
#define BUFFER_MAX_CAPACITY         (1024ULL * 1024ULL * 1024ULL)

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Calculate next capacity when growing.
 *
 * Strategy:
 *   - Start with BUFFER_DEFAULT_CAPACITY if current is 0
 *   - Otherwise multiply by BUFFER_GROWTH_FACTOR
 *   - Clamp to BUFFER_MAX_CAPACITY
 *   - Ensure at least min_required
 */
static wingo_size buffer_next_capacity(wingo_size current, wingo_size min_required)
{
    wingo_size next;

    if (current == 0) {
        next = BUFFER_DEFAULT_CAPACITY;
    } else {
        next = current * BUFFER_GROWTH_FACTOR;
        /* Handle overflow */
        if (next < current) {
            next = BUFFER_MAX_CAPACITY;
        }
    }

    /* Ensure we can hold the minimum required */
    if (next < min_required) {
        next = min_required;
    }

    /* Clamp to max */
    if (next > BUFFER_MAX_CAPACITY) {
        next = BUFFER_MAX_CAPACITY;
    }

    return next;
}

/*
 * Validate buffer pointer.
 * Returns true if buffer is usable.
 */
static bool buffer_valid(const wingo_buf_t *buf)
{
    return buf != NULL;
}

/* ============================================================================
 * BUFFER CREATION & DESTRUCTION
 * ============================================================================ */

wingo_buf_t *wingo_buf_new(wingo_size capacity)
{
    wingo_buf_t *buf;

    /* Use default if 0 */
    if (capacity == 0) {
        capacity = BUFFER_DEFAULT_CAPACITY;
    }

    /* Clamp to max */
    if (capacity > BUFFER_MAX_CAPACITY) {
        return NULL;
    }

    /* Allocate buffer struct */
    buf = calloc(1, sizeof(wingo_buf_t));
    if (buf == NULL) {
        return NULL;
    }

    /* Allocate data */
    buf->data = malloc(capacity);
    if (buf->data == NULL) {
        free(buf);
        return NULL;
    }

    buf->len  = 0;
    buf->cap  = capacity;
    buf->read = 0;

    return buf;
}

wingo_buf_t *wingo_buf_new_from(const void *data, wingo_size len)
{
    wingo_buf_t *buf;

    if (data == NULL && len > 0) {
        return NULL;
    }

    buf = wingo_buf_new(len > 0 ? len : BUFFER_DEFAULT_CAPACITY);
    if (buf == NULL) {
        return NULL;
    }

    if (len > 0) {
        memcpy(buf->data, data, len);
        buf->len = len;
    }

    return buf;
}

wingo_buf_t *wingo_buf_new_sized(wingo_size size)
{
    wingo_buf_t *buf;

    if (size == 0) {
        size = BUFFER_DEFAULT_CAPACITY;
    }

    if (size > BUFFER_MAX_CAPACITY) {
        return NULL;
    }

    buf = wingo_buf_new(size);
    if (buf == NULL) {
        return NULL;
    }

    /*
     * For sized buffer, we set len = cap so caller can use it
     * as a fixed-size scratch area.
     */
    buf->len = size;
    memset(buf->data, 0, size);

    return buf;
}

void wingo_buf_free(wingo_buf_t *buf)
{
    if (buf == NULL) {
        return;
    }

    if (buf->data != NULL) {
        /*
         * Zero memory before free.
         * This is important for security: buffers may contain keys,
         * plaintext, or other sensitive data.
         */
        memset(buf->data, 0, buf->cap);
        free(buf->data);
    }

    /* Zero struct itself */
    memset(buf, 0, sizeof(wingo_buf_t));
    free(buf);
}

void wingo_buf_reset(wingo_buf_t *buf)
{
    if (buf == NULL) {
        return;
    }

    if (buf->data != NULL && buf->cap > 0) {
        memset(buf->data, 0, buf->cap);
    }

    buf->len  = 0;
    buf->read = 0;
}

/* ============================================================================
 * BUFFER RESIZE & RESERVE
 * ============================================================================ */

wingo_error_t wingo_buf_resize(wingo_buf_t *buf, wingo_size capacity)
{
    wingo_u8 *new_data;

    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (capacity > BUFFER_MAX_CAPACITY) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Nothing to do */
    if (capacity == buf->cap) {
        return WINGO_SUCCESS;
    }

    /* Shrinking: just update cap, keep data */
    if (capacity < buf->cap) {
        buf->cap = capacity;
        if (buf->len > capacity) {
            buf->len = capacity;
        }
        if (buf->read > capacity) {
            buf->read = capacity;
        }
        return WINGO_SUCCESS;
    }

    /* Growing: allocate new data */
    new_data = malloc(capacity);
    if (new_data == NULL) {
        return WINGO_ERR_NOMEM;
    }

    /* Copy existing data */
    if (buf->data != NULL && buf->len > 0) {
        memcpy(new_data, buf->data, buf->len);
    }

    /* Zero rest (for security) */
    if (capacity > buf->len) {
        memset(new_data + buf->len, 0, capacity - buf->len);
    }

    /* Free old data */
    if (buf->data != NULL) {
        memset(buf->data, 0, buf->cap);
        free(buf->data);
    }

    buf->data = new_data;
    buf->cap  = capacity;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_reserve(wingo_buf_t *buf, wingo_size capacity)
{
    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (capacity <= buf->cap) {
        return WINGO_SUCCESS;
    }

    return wingo_buf_resize(buf, capacity);
}

wingo_error_t wingo_buf_ensure(wingo_buf_t *buf, wingo_size additional)
{
    wingo_size needed;

    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Check overflow */
    if (additional > BUFFER_MAX_CAPACITY - buf->len) {
        return WINGO_ERR_OVERFLOW;
    }

    needed = buf->len + additional;

    if (needed <= buf->cap) {
        return WINGO_SUCCESS;
    }

    return wingo_buf_resize(buf, buffer_next_capacity(buf->cap, needed));
}

/* ============================================================================
 * BUFFER APPEND
 * ============================================================================ */

wingo_error_t wingo_buf_append(wingo_buf_t *buf, const void *data, wingo_size len)
{
    wingo_error_t rc;

    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    if (data == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Ensure capacity */
    rc = wingo_buf_ensure(buf, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /* Copy data */
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_append_byte(wingo_buf_t *buf, wingo_u8 byte)
{
    return wingo_buf_append(buf, &byte, 1);
}

wingo_error_t wingo_buf_append_str(wingo_buf_t *buf, const char *str)
{
    if (str == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    return wingo_buf_append(buf, str, strlen(str));
}

wingo_error_t wingo_buf_append_printf(wingo_buf_t *buf, const char *fmt, ...)
{
    va_list args;
    va_list args_copy;
    int needed;
    wingo_error_t rc;
    wingo_size old_len;

    if (buf == NULL || fmt == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * First pass: determine how many bytes we need.
     * We use a copy of va_list because we'll need it twice.
     */
    va_start(args, fmt);
    va_copy(args_copy, args);

    needed = vsnprintf(NULL, 0, fmt, args_copy);
    va_end(args_copy);

    if (needed < 0) {
        va_end(args);
        return WINGO_ERR_GENERIC;
    }

    /* Ensure we have room for the string + null terminator */
    old_len = buf->len;
    rc = wingo_buf_ensure(buf, (wingo_size)needed + 1);
    if (rc != WINGO_SUCCESS) {
        va_end(args);
        return rc;
    }

    /*
     * Second pass: actually write.
     * We write the null terminator too, but then we adjust len
     * to not include it (since buffer is binary-safe).
     */
    vsnprintf((char *)buf->data + old_len, (size_t)needed + 1, fmt, args);
    va_end(args);

    buf->len = old_len + (wingo_size)needed;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_append_buf(wingo_buf_t *buf, const wingo_buf_t *src)
{
    if (buf == NULL || src == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (src->len == 0) {
        return WINGO_SUCCESS;
    }

    return wingo_buf_append(buf, src->data, src->len);
}

/* ============================================================================
 * BUFFER PREPEND
 * ============================================================================ */

wingo_error_t wingo_buf_prepend(wingo_buf_t *buf, const void *data, wingo_size len)
{
    wingo_error_t rc;

    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    if (data == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Ensure capacity */
    rc = wingo_buf_ensure(buf, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /*
     * Shift existing data to the right.
     * We use memmove because source and destination overlap.
     */
    if (buf->len > 0) {
        memmove(buf->data + len, buf->data, buf->len);
    }

    /* Copy new data to front */
    memcpy(buf->data, data, len);
    buf->len += len;

    /*
     * Adjust read position.
     * If we prepend, existing read data shifts right.
     */
    if (buf->read > 0) {
        buf->read += len;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_prepend_byte(wingo_buf_t *buf, wingo_u8 byte)
{
    return wingo_buf_prepend(buf, &byte, 1);
}

/* ============================================================================
 * BUFFER INSERT & REMOVE
 * ============================================================================ */

wingo_error_t wingo_buf_insert(wingo_buf_t *buf, wingo_size pos,
                               const void *data, wingo_size len)
{
    wingo_error_t rc;

    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pos > buf->len) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    if (data == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Ensure capacity */
    rc = wingo_buf_ensure(buf, len);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    /*
     * Shift data after position to the right.
     * memmove handles overlap correctly.
     */
    if (pos < buf->len) {
        memmove(buf->data + pos + len, buf->data + pos, buf->len - pos);
    }

    /* Copy new data */
    memcpy(buf->data + pos, data, len);
    buf->len += len;

    /* Adjust read position if insert is before it */
    if (buf->read > pos) {
        buf->read += len;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_remove(wingo_buf_t *buf, wingo_size pos, wingo_size len)
{
    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pos > buf->len) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Clamp len to what's available */
    if (len > buf->len - pos) {
        len = buf->len - pos;
    }

    if (len == 0) {
        return WINGO_SUCCESS;
    }

    /*
     * Shift data after position to the left.
     * memmove handles overlap correctly.
     */
    if (pos + len < buf->len) {
        memmove(buf->data + pos, buf->data + pos + len, buf->len - pos - len);
    }

    /* Zero out the removed area (for security) */
    memset(buf->data + buf->len - len, 0, len);

    buf->len -= len;

    /* Adjust read position */
    if (buf->read > pos + len) {
        buf->read -= len;
    } else if (buf->read > pos) {
        buf->read = pos;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_buf_remove_front(wingo_buf_t *buf, wingo_size len)
{
    return wingo_buf_remove(buf, 0, len);
}

wingo_error_t wingo_buf_remove_back(wingo_buf_t *buf, wingo_size len)
{
    if (buf == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (len > buf->len) {
        len = buf->len;
    }

    return wingo_buf_remove(buf, buf->len - len, len);
}

/* ============================================================================
 * BUFFER READ
 * ============================================================================ */

wingo_size wingo_buf_read(wingo_buf_t *buf, void *out, wingo_size len)
{
    wingo_size available;
    wingo_size to_read;

    if (buf == NULL || out == NULL) {
        return 0;
    }

    available = buf->len - buf->read;
    to_read = (len < available) ? len : available;

    if (to_read == 0) {
        return 0;
    }

    memcpy(out, buf->data + buf->read, to_read);
    buf->read += to_read;

    return to_read;
}

wingo_error_t wingo_buf_read_byte(wingo_buf_t *buf, wingo_u8 *out)
{
    if (buf == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (buf->read >= buf->len) {
        return WINGO_ERR_NOT_FOUND;
    }

    *out = buf->data[buf->read++];

    return WINGO_SUCCESS;
}

wingo_size wingo_buf_peek(const wingo_buf_t *buf, void *out, wingo_size len)
{
    wingo_size available;
    wingo_size to_read;

    if (buf == NULL || out == NULL) {
        return 0;
    }

    available = buf->len - buf->read;
    to_read = (len < available) ? len : available;

    if (to_read == 0) {
        return 0;
    }

    memcpy(out, buf->data + buf->read, to_read);

    return to_read;
}

wingo_size wingo_buf_skip(wingo_buf_t *buf, wingo_size len)
{
    wingo_size available;
    wingo_size to_skip;

    if (buf == NULL) {
        return 0;
    }

    available = buf->len - buf->read;
    to_skip = (len < available) ? len : available;

    buf->read += to_skip;

    return to_skip;
}

/* ============================================================================
 * BUFFER SEARCH
 * ============================================================================ */

wingo_size wingo_buf_find_byte(const wingo_buf_t *buf, wingo_u8 byte, wingo_size start)
{
    wingo_size i;

    if (buf == NULL) {
        return (wingo_size)-1;
    }

    if (start >= buf->len) {
        return (wingo_size)-1;
    }

    for (i = start; i < buf->len; i++) {
        if (buf->data[i] == byte) {
            return i;
        }
    }

    return (wingo_size)-1;
}

wingo_size wingo_buf_find(const wingo_buf_t *buf, const void *data,
                          wingo_size len, wingo_size start)
{
    wingo_size i;

    if (buf == NULL || data == NULL) {
        return (wingo_size)-1;
    }

    if (len == 0) {
        return (wingo_size)-1;
    }

    if (start >= buf->len) {
        return (wingo_size)-1;
    }

    if (len > buf->len - start) {
        return (wingo_size)-1;
    }

    /* Naive search. Good enough for small buffers. */
    for (i = start; i <= buf->len - len; i++) {
        if (memcmp(buf->data + i, data, len) == 0) {
            return i;
        }
    }

    return (wingo_size)-1;
}

/* ============================================================================
 * BUFFER COMPARE
 * ============================================================================ */

int wingo_buf_cmp(const wingo_buf_t *a, const wingo_buf_t *b)
{
    wingo_size min_len;
    int rc;

    /* Handle NULL cases */
    if (a == NULL && b == NULL) {
        return 0;
    }
    if (a == NULL) {
        return -1;
    }
    if (b == NULL) {
        return 1;
    }

    /* Compare common prefix */
    min_len = (a->len < b->len) ? a->len : b->len;

    if (min_len > 0) {
        rc = memcmp(a->data, b->data, min_len);
        if (rc != 0) {
            return rc;
        }
    }

    /* Shorter buffer is less */
    if (a->len < b->len) {
        return -1;
    }
    if (a->len > b->len) {
        return 1;
    }

    return 0;
}

/* ============================================================================
 * BUFFER UTILITY
 * ============================================================================ */

wingo_buf_t *wingo_buf_dup(const wingo_buf_t *buf)
{
    if (buf == NULL) {
        return NULL;
    }

    return wingo_buf_new_from(buf->data, buf->len);
}

wingo_error_t wingo_buf_copy(const wingo_buf_t *buf, void *out, wingo_size *out_len)
{
    if (buf == NULL || out == NULL || out_len == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (*out_len < buf->len) {
        return WINGO_ERR_OVERFLOW;
    }

    if (buf->len > 0) {
        memcpy(out, buf->data, buf->len);
    }

    *out_len = buf->len;

    return WINGO_SUCCESS;
}

void wingo_buf_swap(wingo_buf_t *a, wingo_buf_t *b)
{
    wingo_buf_t tmp;

    if (a == NULL || b == NULL) {
        return;
    }

    /* Struct copy (safe for this struct) */
    tmp = *a;
    *a  = *b;
    *b  = tmp;
}

void wingo_buf_print(const wingo_buf_t *buf)
{
    wingo_buf_fprint(buf, stdout);
}

void wingo_buf_fprint(const wingo_buf_t *buf, FILE *f)
{
    wingo_size i;

    if (buf == NULL || f == NULL) {
        return;
    }

    fprintf(f, "Buffer[cap=%zu, len=%zu, read=%zu]: ",
            buf->cap, buf->len, buf->read);

    if (buf->data == NULL) {
        fprintf(f, "(null)\n");
        return;
    }

    for (i = 0; i < buf->len; i++) {
        fprintf(f, "%02x", buf->data[i]);
    }

    fprintf(f, "\n");
}
