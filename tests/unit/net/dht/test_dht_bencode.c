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
 * Unit tests for wingo/net/dht/dht_bencode.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 *
 * Run with:
 *   make test_dht_bencode
 *   ./test_dht_bencode
 *
 * Or with verbose output:
 *   CK_VERBOSITY=verbose ./test_dht_bencode
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/net/dht/dht_bencode.h"
#include "wingo/util/buffer.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Decode a C string as bencode.
 */
static wingo_bencode_t *decode_str(const char *str)
{
    return wingo_bencode_decode(str, strlen(str));
}

/*
 * Encode a bencode value to C string.
 *
 * Returns a malloc'd string that caller must free.
 * Returns NULL on error.
 */
static char *encode_to_str(const wingo_bencode_t *value)
{
    wingo_buf_t *buf;
    char *result;

    if (value == NULL) {
        return NULL;
    }

    buf = wingo_bencode_encode_new(value);
    if (buf == NULL) {
        return NULL;
    }

    result = malloc(buf->len + 1);
    if (result == NULL) {
        wingo_buf_free(buf);
        return NULL;
    }

    memcpy(result, buf->data, buf->len);
    result[buf->len] = '\0';

    wingo_buf_free(buf);

    return result;
}

/* ============================================================================
 * TEST: DECODE — INTEGER
 * ============================================================================ */

START_TEST(test_decode_integer_zero)
{
    wingo_bencode_t *v = decode_str("i0e");
    wingo_i64 out = -1;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_integer(v));
    ck_assert_int_eq(wingo_bencode_get_integer(v, &out), WINGO_SUCCESS);
    ck_assert_int_eq(out, 0);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_integer_positive)
{
    wingo_bencode_t *v = decode_str("i42e");
    wingo_i64 out = 0;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_integer(v));
    ck_assert_int_eq(wingo_bencode_get_integer(v, &out), WINGO_SUCCESS);
    ck_assert_int_eq(out, 42);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_integer_negative)
{
    wingo_bencode_t *v = decode_str("i-42e");
    wingo_i64 out = 0;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_integer(v));
    ck_assert_int_eq(wingo_bencode_get_integer(v, &out), WINGO_SUCCESS);
    ck_assert_int_eq(out, -42);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_integer_large)
{
    wingo_bencode_t *v = decode_str("i9223372036854775807e");
    wingo_i64 out = 0;

    ck_assert_ptr_nonnull(v);
    ck_assert_int_eq(wingo_bencode_get_integer(v, &out), WINGO_SUCCESS);
    ck_assert_int_eq(out, INT64_MAX);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_integer_min)
{
    wingo_bencode_t *v = decode_str("i-9223372036854775808e");
    wingo_i64 out = 0;

    /*
     * NOTE: INT64_MIN is problematic due to negation.
     *       Some implementations reject it. We may too.
     */
    if (v != NULL) {
        ck_assert_int_eq(wingo_bencode_get_integer(v, &out), WINGO_SUCCESS);
        ck_assert_int_eq(out, INT64_MIN);
        wingo_bencode_free(v);
    }
}
END_TEST

/* ============================================================================
 * TEST: DECODE — INVALID INTEGER
 * ============================================================================ */

START_TEST(test_decode_integer_no_digits)
{
    wingo_bencode_t *v = decode_str("ie");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_integer_no_end)
{
    wingo_bencode_t *v = decode_str("i42");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_integer_leading_zero)
{
    wingo_bencode_t *v = decode_str("i042e");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_integer_negative_zero)
{
    wingo_bencode_t *v = decode_str("i-0e");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_integer_empty)
{
    wingo_bencode_t *v = decode_str("i");
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — STRING
 * ============================================================================ */

START_TEST(test_decode_string_empty)
{
    wingo_bencode_t *v = decode_str("0:");
    const void *data = NULL;
    wingo_size len = 999;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_string(v));
    ck_assert_int_eq(wingo_bencode_get_string(v, &data, &len), WINGO_SUCCESS);
    ck_assert_uint_eq(len, 0);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_string_hello)
{
    wingo_bencode_t *v = decode_str("5:hello");
    const void *data = NULL;
    wingo_size len = 0;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_string(v));
    ck_assert_int_eq(wingo_bencode_get_string(v, &data, &len), WINGO_SUCCESS);
    ck_assert_uint_eq(len, 5);
    ck_assert_mem_eq(data, "hello", 5);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_string_binary)
{
    const char data[] = {0x00, 0x01, 0x02, 0xFF, 0xFE};
    wingo_bencode_t *v = decode_str("5:");
    const void *out = NULL;
    wingo_size out_len = 0;

    /*
     * "5:" is just length prefix — we need to append data.
     * Let's build the full string.
     */
    {
        char buf[8];
        buf[0] = '5';
        buf[1] = ':';
        memcpy(buf + 2, data, 5);

        v = wingo_bencode_decode(buf, 7);
    }

    ck_assert_ptr_nonnull(v);
    ck_assert_int_eq(wingo_bencode_get_string(v, &out, &out_len), WINGO_SUCCESS);
    ck_assert_uint_eq(out_len, 5);
    ck_assert_mem_eq(out, data, 5);

    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — INVALID STRING
 * ============================================================================ */

START_TEST(test_decode_string_no_colon)
{
    wingo_bencode_t *v = decode_str("5hello");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_string_too_short)
{
    wingo_bencode_t *v = decode_str("5:hi");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_string_no_length)
{
    wingo_bencode_t *v = decode_str(":hello");
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — LIST
 * ============================================================================ */

START_TEST(test_decode_list_empty)
{
    wingo_bencode_t *v = decode_str("le");

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_list(v));
    ck_assert_uint_eq(wingo_bencode_list_len(v), 0);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_list_single)
{
    wingo_bencode_t *v = decode_str("l5:helloe");
    const wingo_bencode_t *item;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_list(v));
    ck_assert_uint_eq(wingo_bencode_list_len(v), 1);

    item = wingo_bencode_list_get(v, 0);
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_string(item));
    ck_assert(wingo_bencode_string_eq(item, "hello", 5));

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_list_multiple)
{
    wingo_bencode_t *v = decode_str("l5:helloi42e3:fooe");
    const wingo_bencode_t *item;
    wingo_i64 num;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_list(v));
    ck_assert_uint_eq(wingo_bencode_list_len(v), 3);

    item = wingo_bencode_list_get(v, 0);
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_string_eq(item, "hello", 5));

    item = wingo_bencode_list_get(v, 1);
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_integer(item));
    ck_assert_int_eq(wingo_bencode_get_integer(item, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 42);

    item = wingo_bencode_list_get(v, 2);
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_string_eq(item, "foo", 3));

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_list_nested)
{
    wingo_bencode_t *v = decode_str("ll1:aei42ee");
    const wingo_bencode_t *inner;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_list(v));
    ck_assert_uint_eq(wingo_bencode_list_len(v), 2);

    /* First item: list ["a"] */
    inner = wingo_bencode_list_get(v, 0);
    ck_assert_ptr_nonnull(inner);
    ck_assert(wingo_bencode_is_list(inner));
    ck_assert_uint_eq(wingo_bencode_list_len(inner), 1);

    /* Second item: integer 42 */
    inner = wingo_bencode_list_get(v, 1);
    ck_assert_ptr_nonnull(inner);
    ck_assert(wingo_bencode_is_integer(inner));

    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — INVALID LIST
 * ============================================================================ */

START_TEST(test_decode_list_no_end)
{
    wingo_bencode_t *v = decode_str("l5:hello");
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — DICT
 * ============================================================================ */

START_TEST(test_decode_dict_empty)
{
    wingo_bencode_t *v = decode_str("de");

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_dict(v));
    ck_assert_uint_eq(wingo_bencode_dict_len(v), 0);

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_dict_single)
{
    wingo_bencode_t *v = decode_str("d3:foo3:bare");
    const wingo_bencode_t *item;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_dict(v));
    ck_assert_uint_eq(wingo_bencode_dict_len(v), 1);
    ck_assert(wingo_bencode_dict_has(v, "foo"));

    item = wingo_bencode_dict_get(v, "foo");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_string(item));
    ck_assert(wingo_bencode_string_eq(item, "bar", 3));

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_dict_multiple)
{
    wingo_bencode_t *v = decode_str("d3:foo3:bar3:bazi42e3:quxl1:ae");
    const wingo_bencode_t *item;
    wingo_i64 num;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_dict(v));
    ck_assert_uint_eq(wingo_bencode_dict_len(v), 3);

    /* foo = "bar" */
    item = wingo_bencode_dict_get(v, "foo");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_string_eq(item, "bar", 3));

    /* baz = 42 */
    item = wingo_bencode_dict_get(v, "baz");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_integer(item));
    ck_assert_int_eq(wingo_bencode_get_integer(item, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 42);

    /* qux = ["a"] */
    item = wingo_bencode_dict_get(v, "qux");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_list(item));

    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_decode_dict_nested)
{
    wingo_bencode_t *v = decode_str("d4:infod4:name5:helloe5:valuei42eee");
    const wingo_bencode_t *info;
    const wingo_bencode_t *name;

    ck_assert_ptr_nonnull(v);
    ck_assert(wingo_bencode_is_dict(v));

    info = wingo_bencode_dict_get(v, "info");
    ck_assert_ptr_nonnull(info);
    ck_assert(wingo_bencode_is_dict(info));

    name = wingo_bencode_dict_get(info, "name");
    ck_assert_ptr_nonnull(name);
    ck_assert(wingo_bencode_string_eq(name, "hello", 5));

    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — INVALID DICT
 * ============================================================================ */

START_TEST(test_decode_dict_no_end)
{
    wingo_bencode_t *v = decode_str("d3:foo3:bar");
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_dict_non_string_key)
{
    wingo_bencode_t *v = decode_str("di42e3:bare");
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — TRAILING DATA
 * ============================================================================ */

START_TEST(test_decode_trailing_data)
{
    /* "i42egarbage" should fail because not entire input consumed */
    wingo_bencode_t *v = decode_str("i42egarbage");
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE — NULL/EMPTY
 * ============================================================================ */

START_TEST(test_decode_null)
{
    wingo_bencode_t *v = wingo_bencode_decode(NULL, 0);
    ck_assert_ptr_null(v);
}
END_TEST

START_TEST(test_decode_empty)
{
    wingo_bencode_t *v = wingo_bencode_decode("", 0);
    ck_assert_ptr_null(v);
}
END_TEST

/* ============================================================================
 * TEST: DECODE AT POSITION
 * ============================================================================ */

START_TEST(test_decode_at)
{
    const char *data = "i42ei99e";
    wingo_size pos = 0;
    wingo_bencode_t *v1;
    wingo_bencode_t *v2;
    wingo_i64 num;

    v1 = wingo_bencode_decode_at(data, strlen(data), &pos);
    ck_assert_ptr_nonnull(v1);
    ck_assert_uint_eq(pos, 4);

    v2 = wingo_bencode_decode_at(data, strlen(data), &pos);
    ck_assert_ptr_nonnull(v2);
    ck_assert_uint_eq(pos, 8);

    ck_assert_int_eq(wingo_bencode_get_integer(v1, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 42);

    ck_assert_int_eq(wingo_bencode_get_integer(v2, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 99);

    wingo_bencode_free(v1);
    wingo_bencode_free(v2);
}
END_TEST

/* ============================================================================
 * TEST: ENCODE — INTEGER
 * ============================================================================ */

START_TEST(test_encode_integer_zero)
{
    wingo_bencode_t *v = wingo_bencode_new_integer(0);
    char *str;

    ck_assert_ptr_nonnull(v);

    str = encode_to_str(v);
    ck_assert_ptr_nonnull(str);
    ck_assert_str_eq(str, "i0e");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_integer_positive)
{
    wingo_bencode_t *v = wingo_bencode_new_integer(42);
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "i42e");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_integer_negative)
{
    wingo_bencode_t *v = wingo_bencode_new_integer(-42);
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "i-42e");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: ENCODE — STRING
 * ============================================================================ */

START_TEST(test_encode_string_empty)
{
    wingo_bencode_t *v = wingo_bencode_new_string(NULL, 0);
    char *str;

    ck_assert_ptr_nonnull(v);

    str = encode_to_str(v);
    ck_assert_str_eq(str, "0:");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_string_hello)
{
    wingo_bencode_t *v = wingo_bencode_new_string("hello", 5);
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "5:hello");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_cstring)
{
    wingo_bencode_t *v = wingo_bencode_new_cstring("hello");
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "5:hello");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: ENCODE — LIST
 * ============================================================================ */

START_TEST(test_encode_list_empty)
{
    wingo_bencode_t *v = wingo_bencode_new_list();
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "le");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_list_single)
{
    wingo_bencode_t *list = wingo_bencode_new_list();
    wingo_bencode_t *str_val;
    char *str;

    str_val = wingo_bencode_new_cstring("hello");
    ck_assert_int_eq(wingo_bencode_list_append(list, str_val), WINGO_SUCCESS);

    str = encode_to_str(list);
    ck_assert_str_eq(str, "l5:helloe");

    free(str);
    wingo_bencode_free(list);
}
END_TEST

START_TEST(test_encode_list_multiple)
{
    wingo_bencode_t *list = wingo_bencode_new_list();
    char *str;

    wingo_bencode_list_append(list, wingo_bencode_new_integer(1));
    wingo_bencode_list_append(list, wingo_bencode_new_integer(2));
    wingo_bencode_list_append(list, wingo_bencode_new_integer(3));

    str = encode_to_str(list);
    ck_assert_str_eq(str, "li1ei2ei3ee");

    free(str);
    wingo_bencode_free(list);
}
END_TEST

/* ============================================================================
 * TEST: ENCODE — DICT
 * ============================================================================ */

START_TEST(test_encode_dict_empty)
{
    wingo_bencode_t *v = wingo_bencode_new_dict();
    char *str;

    str = encode_to_str(v);
    ck_assert_str_eq(str, "de");

    free(str);
    wingo_bencode_free(v);
}
END_TEST

START_TEST(test_encode_dict_single)
{
    wingo_bencode_t *dict = wingo_bencode_new_dict();
    char *str;

    wingo_bencode_dict_set(dict, "foo", wingo_bencode_new_cstring("bar"));

    str = encode_to_str(dict);
    ck_assert_str_eq(str, "d3:foo3:bare");

    free(str);
    wingo_bencode_free(dict);
}
END_TEST

START_TEST(test_encode_dict_sorted_keys)
{
    /*
     * Canonical bencode requires sorted keys.
     * We insert in unsorted order, expect sorted output.
     */
    wingo_bencode_t *dict = wingo_bencode_new_dict();
    char *str;

    wingo_bencode_dict_set(dict, "zzz", wingo_bencode_new_integer(1));
    wingo_bencode_dict_set(dict, "aaa", wingo_bencode_new_integer(2));
    wingo_bencode_dict_set(dict, "mmm", wingo_bencode_new_integer(3));

    str = encode_to_str(dict);

    /*
     * Expected: d3:aaai2e3:mmmi3e3:zzzi1ee
     * (keys sorted: aaa, mmm, zzz)
     */
    ck_assert_str_eq(str, "d3:aaai2e3:mmmi3e3:zzzi1ee");

    free(str);
    wingo_bencode_free(dict);
}
END_TEST

/* ============================================================================
 * TEST: ENCODE/DECODE ROUNDTRIP
 * ============================================================================ */

START_TEST(test_roundtrip_integer)
{
    wingo_bencode_t *v1 = wingo_bencode_new_integer(12345);
    wingo_buf_t *buf;
    wingo_bencode_t *v2;
    wingo_i64 num;

    buf = wingo_bencode_encode_new(v1);
    ck_assert_ptr_nonnull(buf);

    v2 = wingo_bencode_decode(buf->data, buf->len);
    ck_assert_ptr_nonnull(v2);
    ck_assert_int_eq(wingo_bencode_get_integer(v2, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 12345);

    wingo_buf_free(buf);
    wingo_bencode_free(v1);
    wingo_bencode_free(v2);
}
END_TEST

START_TEST(test_roundtrip_complex)
{
    wingo_bencode_t *v1 = wingo_bencode_new_dict();
    wingo_buf_t *buf;
    wingo_bencode_t *v2;
    const wingo_bencode_t *item;
    wingo_i64 num;

    /* Build: {"name": "bowie", "version": 1, "tags": ["p2p", "dht"]} */
    wingo_bencode_dict_set(v1, "name",
                            wingo_bencode_new_cstring("bowie"));
    wingo_bencode_dict_set(v1, "version",
                            wingo_bencode_new_integer(1));

    {
        wingo_bencode_t *tags = wingo_bencode_new_list();
        wingo_bencode_list_append(tags, wingo_bencode_new_cstring("p2p"));
        wingo_bencode_list_append(tags, wingo_bencode_new_cstring("dht"));
        wingo_bencode_dict_set(v1, "tags", tags);
    }

    /* Encode */
    buf = wingo_bencode_encode_new(v1);
    ck_assert_ptr_nonnull(buf);

    /* Decode */
    v2 = wingo_bencode_decode(buf->data, buf->len);
    ck_assert_ptr_nonnull(v2);

    /* Verify */
    item = wingo_bencode_dict_get(v2, "name");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_string_eq(item, "bowie", 5));

    item = wingo_bencode_dict_get(v2, "version");
    ck_assert_ptr_nonnull(item);
    ck_assert_int_eq(wingo_bencode_get_integer(item, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 1);

    item = wingo_bencode_dict_get(v2, "tags");
    ck_assert_ptr_nonnull(item);
    ck_assert(wingo_bencode_is_list(item));
    ck_assert_uint_eq(wingo_bencode_list_len(item), 2);

    wingo_buf_free(buf);
    wingo_bencode_free(v1);
    wingo_bencode_free(v2);
}
END_TEST

/* ============================================================================
 * TEST: UTILITY
 * ============================================================================ */

START_TEST(test_free_null)
{
    /* Should not crash */
    wingo_bencode_free(NULL);
}
END_TEST

START_TEST(test_type_null)
{
    ck_assert_int_eq(wingo_bencode_type(NULL), WINGO_BENCODE_INTEGER);
    ck_assert(!wingo_bencode_is_integer(NULL));
    ck_assert(!wingo_bencode_is_string(NULL));
    ck_assert(!wingo_bencode_is_list(NULL));
    ck_assert(!wingo_bencode_is_dict(NULL));
}
END_TEST

START_TEST(test_dup_integer)
{
    wingo_bencode_t *v1 = wingo_bencode_new_integer(42);
    wingo_bencode_t *v2 = wingo_bencode_dup(v1);
    wingo_i64 num;

    ck_assert_ptr_nonnull(v2);
    ck_assert_int_eq(wingo_bencode_get_integer(v2, &num), WINGO_SUCCESS);
    ck_assert_int_eq(num, 42);

    wingo_bencode_free(v1);
    wingo_bencode_free(v2);
}
END_TEST

START_TEST(test_dup_list)
{
    wingo_bencode_t *v1 = wingo_bencode_new_list();
    wingo_bencode_t *v2;
    wingo_size len;

    wingo_bencode_list_append(v1, wingo_bencode_new_integer(1));
    wingo_bencode_list_append(v1, wingo_bencode_new_integer(2));

    v2 = wingo_bencode_dup(v1);

    ck_assert_ptr_nonnull(v2);
    len = wingo_bencode_list_len(v2);
    ck_assert_uint_eq(len, 2);

    wingo_bencode_free(v1);
    wingo_bencode_free(v2);
}
END_TEST

START_TEST(test_eq_same)
{
    wingo_bencode_t *a = wingo_bencode_new_integer(42);
    wingo_bencode_t *b = wingo_bencode_new_integer(42);

    ck_assert(wingo_bencode_eq(a, b));

    wingo_bencode_free(a);
    wingo_bencode_free(b);
}
END_TEST

START_TEST(test_eq_different)
{
    wingo_bencode_t *a = wingo_bencode_new_integer(42);
    wingo_bencode_t *b = wingo_bencode_new_integer(99);

    ck_assert(!wingo_bencode_eq(a, b));

    wingo_bencode_free(a);
    wingo_bencode_free(b);
}
END_TEST

START_TEST(test_encoded_len)
{
    wingo_bencode_t *v = wingo_bencode_new_cstring("hello");
    wingo_size len;

    len = wingo_bencode_encoded_len(v);
    ck_assert_uint_eq(len, 7);  /* "5:hello" */

    wingo_bencode_free(v);
}
END_TEST

/* ============================================================================
 * TEST: CONVENIENCE
 * ============================================================================ */

START_TEST(test_dict_set_get_integer)
{
    wingo_bencode_t *dict = wingo_bencode_new_dict();
    wingo_i64 out;

    ck_assert_int_eq(wingo_bencode_dict_set_integer(dict, "key", 42),
                     WINGO_SUCCESS);
    ck_assert_int_eq(wingo_bencode_dict_get_integer(dict, "key", &out),
                     WINGO_SUCCESS);
    ck_assert_int_eq(out, 42);

    wingo_bencode_free(dict);
}
END_TEST

START_TEST(test_dict_set_get_string)
{
    wingo_bencode_t *dict = wingo_bencode_new_dict();
    const void *out;
    wingo_size len;

    ck_assert_int_eq(wingo_bencode_dict_set_string(dict, "key",
                                                    "hello", 5),
                     WINGO_SUCCESS);
    ck_assert_int_eq(wingo_bencode_dict_get_string(dict, "key", &out, &len),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(len, 5);
    ck_assert_mem_eq(out, "hello", 5);

    wingo_bencode_free(dict);
}
END_TEST

START_TEST(test_dict_get_missing)
{
    wingo_bencode_t *dict = wingo_bencode_new_dict();
    wingo_i64 out;

    ck_assert_int_eq(wingo_bencode_dict_get_integer(dict, "missing", &out),
                     WINGO_ERR_NOT_FOUND);

    wingo_bencode_free(dict);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *bencode_suite(void)
{
    Suite *s;
    TCase *tc_decode_int;
    TCase *tc_decode_str;
    TCase *tc_decode_list;
    TCase *tc_decode_dict;
    TCase *tc_decode_err;
    TCase *tc_encode;
    TCase *tc_roundtrip;
    TCase *tc_util;

    s = suite_create("DHT Bencode");

    /* Integer decode tests */
    tc_decode_int = tcase_create("DecodeInteger");
    tcase_add_test(tc_decode_int, test_decode_integer_zero);
    tcase_add_test(tc_decode_int, test_decode_integer_positive);
    tcase_add_test(tc_decode_int, test_decode_integer_negative);
    tcase_add_test(tc_decode_int, test_decode_integer_large);
    tcase_add_test(tc_decode_int, test_decode_integer_min);
    suite_add_tcase(s, tc_decode_int);

    /* String decode tests */
    tc_decode_str = tcase_create("DecodeString");
    tcase_add_test(tc_decode_str, test_decode_string_empty);
    tcase_add_test(tc_decode_str, test_decode_string_hello);
    tcase_add_test(tc_decode_str, test_decode_string_binary);
    suite_add_tcase(s, tc_decode_str);

    /* List decode tests */
    tc_decode_list = tcase_create("DecodeList");
    tcase_add_test(tc_decode_list, test_decode_list_empty);
    tcase_add_test(tc_decode_list, test_decode_list_single);
    tcase_add_test(tc_decode_list, test_decode_list_multiple);
    tcase_add_test(tc_decode_list, test_decode_list_nested);
    suite_add_tcase(s, tc_decode_list);

    /* Dict decode tests */
    tc_decode_dict = tcase_create("DecodeDict");
    tcase_add_test(tc_decode_dict, test_decode_dict_empty);
    tcase_add_test(tc_decode_dict, test_decode_dict_single);
    tcase_add_test(tc_decode_dict, test_decode_dict_multiple);
    tcase_add_test(tc_decode_dict, test_decode_dict_nested);
    suite_add_tcase(s, tc_decode_dict);

    /* Decode error tests */
    tc_decode_err = tcase_create("DecodeError");
    tcase_add_test(tc_decode_err, test_decode_integer_no_digits);
    tcase_add_test(tc_decode_err, test_decode_integer_no_end);
    tcase_add_test(tc_decode_err, test_decode_integer_leading_zero);
    tcase_add_test(tc_decode_err, test_decode_integer_negative_zero);
    tcase_add_test(tc_decode_err, test_decode_integer_empty);
    tcase_add_test(tc_decode_err, test_decode_string_no_colon);
    tcase_add_test(tc_decode_err, test_decode_string_too_short);
    tcase_add_test(tc_decode_err, test_decode_string_no_length);
    tcase_add_test(tc_decode_err, test_decode_list_no_end);
    tcase_add_test(tc_decode_err, test_decode_dict_no_end);
    tcase_add_test(tc_decode_err, test_decode_dict_non_string_key);
    tcase_add_test(tc_decode_err, test_decode_trailing_data);
    tcase_add_test(tc_decode_err, test_decode_null);
    tcase_add_test(tc_decode_err, test_decode_empty);
    tcase_add_test(tc_decode_err, test_decode_at);
    suite_add_tcase(s, tc_decode_err);

    /* Encode tests */
    tc_encode = tcase_create("Encode");
    tcase_add_test(tc_encode, test_encode_integer_zero);
    tcase_add_test(tc_encode, test_encode_integer_positive);
    tcase_add_test(tc_encode, test_encode_integer_negative);
    tcase_add_test(tc_encode, test_encode_string_empty);
    tcase_add_test(tc_encode, test_encode_string_hello);
    tcase_add_test(tc_encode, test_encode_cstring);
    tcase_add_test(tc_encode, test_encode_list_empty);
    tcase_add_test(tc_encode, test_encode_list_single);
    tcase_add_test(tc_encode, test_encode_list_multiple);
    tcase_add_test(tc_encode, test_encode_dict_empty);
    tcase_add_test(tc_encode, test_encode_dict_single);
    tcase_add_test(tc_encode, test_encode_dict_sorted_keys);
    suite_add_tcase(s, tc_encode);

    /* Roundtrip tests */
    tc_roundtrip = tcase_create("Roundtrip");
    tcase_add_test(tc_roundtrip, test_roundtrip_integer);
    tcase_add_test(tc_roundtrip, test_roundtrip_complex);
    suite_add_tcase(s, tc_roundtrip);

    /* Utility tests */
    tc_util = tcase_create("Utility");
    tcase_add_test(tc_util, test_free_null);
    tcase_add_test(tc_util, test_type_null);
    tcase_add_test(tc_util, test_dup_integer);
    tcase_add_test(tc_util, test_dup_list);
    tcase_add_test(tc_util, test_eq_same);
    tcase_add_test(tc_util, test_eq_different);
    tcase_add_test(tc_util, test_encoded_len);
    tcase_add_test(tc_util, test_dict_set_get_integer);
    tcase_add_test(tc_util, test_dict_set_get_string);
    tcase_add_test(tc_util, test_dict_get_missing);
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

    s = bencode_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
