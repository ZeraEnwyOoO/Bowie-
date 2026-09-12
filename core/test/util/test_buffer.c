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
 * Unit tests for wingo/util/buffer.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 *
 * Run with:
 *   make test_buffer
 *   ./test_buffer
 *
 * Or with verbose output:
 *   CK_VERBOSITY=verbose ./test_buffer
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/util/buffer.h"

/* ============================================================================
 * TEST: BUFFER CREATION
 * ============================================================================ */

START_TEST(test_buffer_new_valid)
{
    wingo_buf_t *buf = wingo_buf_new(16);

    ck_assert_ptr_nonnull(buf);
    ck_assert_uint_eq(buf->len, 0);
    ck_assert_uint_eq(buf->cap, 16);
    ck_assert_uint_eq(buf->read, 0);
    ck_assert_ptr_nonnull(buf->data);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_new_zero_capacity)
{
    /* Capacity 0 should use default */
    wingo_buf_t *buf = wingo_buf_new(0);

    ck_assert_ptr_nonnull(buf);
    ck_assert_uint_eq(buf->len, 0);
    ck_assert_uint_gt(buf->cap, 0);  /* Should have default capacity */

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_new_from_data)
{
    const char *data = "hello world";
    wingo_buf_t *buf = wingo_buf_new_from(data, 11);

    ck_assert_ptr_nonnull(buf);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_uint_ge(buf->cap, 11);
    ck_assert_mem_eq(buf->data, data, 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_new_from_empty)
{
    wingo_buf_t *buf = wingo_buf_new_from(NULL, 0);

    ck_assert_ptr_nonnull(buf);
    ck_assert_uint_eq(buf->len, 0);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_new_sized)
{
    wingo_buf_t *buf = wingo_buf_new_sized(32);

    ck_assert_ptr_nonnull(buf);
    ck_assert_uint_eq(buf->len, 32);  /* Sized sets len = cap */
    ck_assert_uint_eq(buf->cap, 32);

    /* Data should be zeroed */
    for (wingo_size i = 0; i < 32; i++) {
        ck_assert_uint_eq(buf->data[i], 0);
    }

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER FREE
 * ============================================================================ */

START_TEST(test_buffer_free_null)
{
    /* Should not crash */
    wingo_buf_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER RESET
 * ============================================================================ */

START_TEST(test_buffer_reset)
{
    wingo_buf_t *buf = wingo_buf_new(16);

    wingo_buf_append(buf, "hello", 5);
    ck_assert_uint_eq(buf->len, 5);

    wingo_buf_reset(buf);
    ck_assert_uint_eq(buf->len, 0);
    ck_assert_uint_eq(buf->read, 0);
    ck_assert_uint_eq(buf->cap, 16);  /* Capacity unchanged */

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER RESIZE
 * ============================================================================ */

START_TEST(test_buffer_resize_grow)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    wingo_buf_append(buf, "hi", 2);

    rc = wingo_buf_resize(buf, 64);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->cap, 64);
    ck_assert_uint_eq(buf->len, 2);
    ck_assert_mem_eq(buf->data, "hi", 2);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_resize_shrink)
{
    wingo_buf_t *buf = wingo_buf_new(64);
    wingo_error_t rc;

    wingo_buf_append(buf, "hello", 5);

    rc = wingo_buf_resize(buf, 8);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->cap, 8);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_resize_same)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    rc = wingo_buf_resize(buf, 16);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->cap, 16);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER RESERVE
 * ============================================================================ */

START_TEST(test_buffer_reserve)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    rc = wingo_buf_reserve(buf, 128);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_ge(buf->cap, 128);

    /* Reserve smaller should not shrink */
    rc = wingo_buf_reserve(buf, 8);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_ge(buf->cap, 128);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER APPEND
 * ============================================================================ */

START_TEST(test_buffer_append_basic)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    rc = wingo_buf_append(buf, "hello", 5);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    rc = wingo_buf_append(buf, " world", 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_mem_eq(buf->data, "hello world", 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_grow)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    /* Append more than capacity — should auto-grow */
    rc = wingo_buf_append(buf, "hello world", 11);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_uint_ge(buf->cap, 11);
    ck_assert_mem_eq(buf->data, "hello world", 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_empty)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    rc = wingo_buf_append(buf, "hello", 0);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 0);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_null)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    rc = wingo_buf_append(buf, NULL, 5);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_byte)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    rc = wingo_buf_append_byte(buf, 'A');
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 1);
    ck_assert_uint_eq(buf->data[0], 'A');

    rc = wingo_buf_append_byte(buf, 'B');
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 2);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_str)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    rc = wingo_buf_append_str(buf, "hello");
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_printf)
{
    wingo_buf_t *buf = wingo_buf_new(4);
    wingo_error_t rc;

    rc = wingo_buf_append_printf(buf, "x=%d, y=%d", 42, 99);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    const char *expected = "x=42, y=99";
    ck_assert_uint_eq(buf->len, strlen(expected));
    ck_assert_mem_eq(buf->data, expected, strlen(expected));

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_append_buf)
{
    wingo_buf_t *a = wingo_buf_new(16);
    wingo_buf_t *b = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(a, "hello");
    wingo_buf_append_str(b, " world");

    rc = wingo_buf_append_buf(a, b);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(a->len, 11);
    ck_assert_mem_eq(a->data, "hello world", 11);

    wingo_buf_free(a);
    wingo_buf_free(b);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER PREPEND
 * ============================================================================ */

START_TEST(test_buffer_prepend)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "world");

    rc = wingo_buf_prepend(buf, "hello ", 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_mem_eq(buf->data, "hello world", 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_prepend_byte)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "BC");

    rc = wingo_buf_prepend_byte(buf, 'A');
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 3);
    ck_assert_mem_eq(buf->data, "ABC", 3);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER INSERT
 * ============================================================================ */

START_TEST(test_buffer_insert_middle)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "helo");

    rc = wingo_buf_insert(buf, 2, "l", 1);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_insert_front)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "world");

    rc = wingo_buf_insert(buf, 0, "hello ", 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_mem_eq(buf->data, "hello world", 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_insert_back)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello");

    rc = wingo_buf_insert(buf, 5, " world", 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 11);
    ck_assert_mem_eq(buf->data, "hello world", 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_insert_out_of_range)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello");

    rc = wingo_buf_insert(buf, 100, "x", 1);
    ck_assert_int_eq(rc, WINGO_ERR_OUT_OF_RANGE);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER REMOVE
 * ============================================================================ */

START_TEST(test_buffer_remove_middle)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello world");

    rc = wingo_buf_remove(buf, 5, 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_remove_front)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello world");

    rc = wingo_buf_remove_front(buf, 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "world", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_remove_back)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello world");

    rc = wingo_buf_remove_back(buf, 6);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(buf->len, 5);
    ck_assert_mem_eq(buf->data, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER READ
 * ============================================================================ */

START_TEST(test_buffer_read)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    char out[16] = {0};
    wingo_size n;

    wingo_buf_append_str(buf, "hello world");

    n = wingo_buf_read(buf, out, 5);
    ck_assert_uint_eq(n, 5);
    ck_assert_mem_eq(out, "hello", 5);
    ck_assert_uint_eq(buf->read, 5);
    ck_assert_uint_eq(buf->len, 11);  /* len unchanged */

    n = wingo_buf_read(buf, out, 6);
    ck_assert_uint_eq(n, 6);
    ck_assert_mem_eq(out, " world", 6);
    ck_assert_uint_eq(buf->read, 11);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_read_byte)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_u8 byte;
    wingo_error_t rc;

    wingo_buf_append_str(buf, "ABC");

    rc = wingo_buf_read_byte(buf, &byte);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(byte, 'A');

    rc = wingo_buf_read_byte(buf, &byte);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(byte, 'B');

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_read_empty)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_u8 byte;
    wingo_error_t rc;

    rc = wingo_buf_read_byte(buf, &byte);
    ck_assert_int_eq(rc, WINGO_ERR_NOT_FOUND);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_peek)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    char out[16] = {0};
    wingo_size n;

    wingo_buf_append_str(buf, "hello");

    n = wingo_buf_peek(buf, out, 5);
    ck_assert_uint_eq(n, 5);
    ck_assert_mem_eq(out, "hello", 5);
    ck_assert_uint_eq(buf->read, 0);  /* read position unchanged */

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_skip)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_size n;

    wingo_buf_append_str(buf, "hello world");

    n = wingo_buf_skip(buf, 6);
    ck_assert_uint_eq(n, 6);
    ck_assert_uint_eq(buf->read, 6);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_remaining)
{
    wingo_buf_t *buf = wingo_buf_new(16);

    wingo_buf_append_str(buf, "hello");
    ck_assert_uint_eq(wingo_buf_remaining(buf), 5);

    wingo_buf_skip(buf, 2);
    ck_assert_uint_eq(wingo_buf_remaining(buf), 3);

    ck_assert(wingo_buf_has_more(buf));

    wingo_buf_skip(buf, 3);
    ck_assert_uint_eq(wingo_buf_remaining(buf), 0);
    ck_assert(!wingo_buf_has_more(buf));

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER FIND
 * ============================================================================ */

START_TEST(test_buffer_find_byte)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_size pos;

    wingo_buf_append_str(buf, "hello world");

    pos = wingo_buf_find_byte(buf, 'w', 0);
    ck_assert_uint_eq(pos, 6);

    pos = wingo_buf_find_byte(buf, 'z', 0);
    ck_assert_uint_eq(pos, (wingo_size)-1);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_find_data)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_size pos;

    wingo_buf_append_str(buf, "hello world");

    pos = wingo_buf_find(buf, "world", 5, 0);
    ck_assert_uint_eq(pos, 6);

    pos = wingo_buf_find(buf, "xyz", 3, 0);
    ck_assert_uint_eq(pos, (wingo_size)-1);

    wingo_buf_free(buf);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER COMPARE
 * ============================================================================ */

START_TEST(test_buffer_cmp_equal)
{
    wingo_buf_t *a = wingo_buf_new(16);
    wingo_buf_t *b = wingo_buf_new(16);

    wingo_buf_append_str(a, "hello");
    wingo_buf_append_str(b, "hello");

    ck_assert_int_eq(wingo_buf_cmp(a, b), 0);
    ck_assert(wingo_buf_eq(a, b));

    wingo_buf_free(a);
    wingo_buf_free(b);
}
END_TEST

START_TEST(test_buffer_cmp_different)
{
    wingo_buf_t *a = wingo_buf_new(16);
    wingo_buf_t *b = wingo_buf_new(16);

    wingo_buf_append_str(a, "hello");
    wingo_buf_append_str(b, "world");

    ck_assert_int_ne(wingo_buf_cmp(a, b), 0);
    ck_assert(!wingo_buf_eq(a, b));

    wingo_buf_free(a);
    wingo_buf_free(b);
}
END_TEST

START_TEST(test_buffer_cmp_prefix)
{
    wingo_buf_t *a = wingo_buf_new(16);
    wingo_buf_t *b = wingo_buf_new(16);

    wingo_buf_append_str(a, "hello");
    wingo_buf_append_str(b, "hello world");

    /* a is prefix of b, so a < b */
    ck_assert_int_lt(wingo_buf_cmp(a, b), 0);

    wingo_buf_free(a);
    wingo_buf_free(b);
}
END_TEST

/* ============================================================================
 * TEST: BUFFER UTILITY
 * ============================================================================ */

START_TEST(test_buffer_dup)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    wingo_buf_t *dup;

    wingo_buf_append_str(buf, "hello");

    dup = wingo_buf_dup(buf);
    ck_assert_ptr_nonnull(dup);
    ck_assert_uint_eq(dup->len, 5);
    ck_assert_mem_eq(dup->data, "hello", 5);
    ck_assert_ptr_ne(dup->data, buf->data);  /* Different buffer */

    wingo_buf_free(buf);
    wingo_buf_free(dup);
}
END_TEST

START_TEST(test_buffer_copy)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    char out[16] = {0};
    wingo_size out_len = sizeof(out);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello");

    rc = wingo_buf_copy(buf, out, &out_len);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(out_len, 5);
    ck_assert_mem_eq(out, "hello", 5);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_copy_too_small)
{
    wingo_buf_t *buf = wingo_buf_new(16);
    char out[4] = {0};
    wingo_size out_len = sizeof(out);
    wingo_error_t rc;

    wingo_buf_append_str(buf, "hello");

    rc = wingo_buf_copy(buf, out, &out_len);
    ck_assert_int_eq(rc, WINGO_ERR_OVERFLOW);

    wingo_buf_free(buf);
}
END_TEST

START_TEST(test_buffer_swap)
{
    wingo_buf_t *a = wingo_buf_new(16);
    wingo_buf_t *b = wingo_buf_new(16);

    wingo_buf_append_str(a, "hello");
    wingo_buf_append_str(b, "world");

    wingo_buf_swap(a, b);

    ck_assert_uint_eq(a->len, 5);
    ck_assert_mem_eq(a->data, "world", 5);
    ck_assert_uint_eq(b->len, 5);
    ck_assert_mem_eq(b->data, "hello", 5);

    wingo_buf_free(a);
    wingo_buf_free(b);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *buffer_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_modify;
    TCase *tc_read;
    TCase *tc_util;

    s = suite_create("Buffer");

    /* Creation tests */
    tc_create = tcase_create("Creation");
    tcase_add_test(tc_create, test_buffer_new_valid);
    tcase_add_test(tc_create, test_buffer_new_zero_capacity);
    tcase_add_test(tc_create, test_buffer_new_from_data);
    tcase_add_test(tc_create, test_buffer_new_from_empty);
    tcase_add_test(tc_create, test_buffer_new_sized);
    tcase_add_test(tc_create, test_buffer_free_null);
    tcase_add_test(tc_create, test_buffer_reset);
    suite_add_tcase(s, tc_create);

    /* Modification tests */
    tc_modify = tcase_create("Modification");
    tcase_add_test(tc_modify, test_buffer_resize_grow);
    tcase_add_test(tc_modify, test_buffer_resize_shrink);
    tcase_add_test(tc_modify, test_buffer_resize_same);
    tcase_add_test(tc_modify, test_buffer_reserve);
    tcase_add_test(tc_modify, test_buffer_append_basic);
    tcase_add_test(tc_modify, test_buffer_append_grow);
    tcase_add_test(tc_modify, test_buffer_append_empty);
    tcase_add_test(tc_modify, test_buffer_append_null);
    tcase_add_test(tc_modify, test_buffer_append_byte);
    tcase_add_test(tc_modify, test_buffer_append_str);
    tcase_add_test(tc_modify, test_buffer_append_printf);
    tcase_add_test(tc_modify, test_buffer_append_buf);
    tcase_add_test(tc_modify, test_buffer_prepend);
    tcase_add_test(tc_modify, test_buffer_prepend_byte);
    tcase_add_test(tc_modify, test_buffer_insert_middle);
    tcase_add_test(tc_modify, test_buffer_insert_front);
    tcase_add_test(tc_modify, test_buffer_insert_back);
    tcase_add_test(tc_modify, test_buffer_insert_out_of_range);
    tcase_add_test(tc_modify, test_buffer_remove_middle);
    tcase_add_test(tc_modify, test_buffer_remove_front);
    tcase_add_test(tc_modify, test_buffer_remove_back);
    suite_add_tcase(s, tc_modify);

    /* Read tests */
    tc_read = tcase_create("Read");
    tcase_add_test(tc_read, test_buffer_read);
    tcase_add_test(tc_read, test_buffer_read_byte);
    tcase_add_test(tc_read, test_buffer_read_empty);
    tcase_add_test(tc_read, test_buffer_peek);
    tcase_add_test(tc_read, test_buffer_skip);
    tcase_add_test(tc_read, test_buffer_remaining);
    tcase_add_test(tc_read, test_buffer_find_byte);
    tcase_add_test(tc_read, test_buffer_find_data);
    suite_add_tcase(s, tc_read);

    /* Utility tests */
    tc_util = tcase_create("Utility");
    tcase_add_test(tc_util, test_buffer_cmp_equal);
    tcase_add_test(tc_util, test_buffer_cmp_different);
    tcase_add_test(tc_util, test_buffer_cmp_prefix);
    tcase_add_test(tc_util, test_buffer_dup);
    tcase_add_test(tc_util, test_buffer_copy);
    tcase_add_test(tc_util, test_buffer_copy_too_small);
    tcase_add_test(tc_util, test_buffer_swap);
    suite_add_tcase(s, tc_util);

    return s;
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void)
{
    int number_failed;
    Suite *s;
    SRunner *sr;

    s = buffer_suite();
    sr = srunner_create(s);

    /*
     * Run tests.
     *
     * CK_NORMAL     — show only failures
     * CK_VERBOSE    — show all tests
     * CK_MINIMAL    — show only summary
     * CK_ENV        — read verbosity from CK_VERBOSITY env
     */
    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
