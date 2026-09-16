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
 * Unit tests for wingo/net/dht/dht_token.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/net/dht/dht_token.h"
#include "wingo/net/socket.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static wingo_addr_t *make_addr(const char *ip, wingo_u16 port)
{
    return wingo_addr_new_ipv4(ip, port);
}

/* ============================================================================
 * TEST: TOKEN MANAGER LIFECYCLE
 * ============================================================================ */

START_TEST(test_token_new)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    ck_assert_ptr_nonnull(token);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_free_null)
{
    /* Should not crash */
    wingo_dht_token_free(NULL);
}
END_TEST

START_TEST(test_token_reset)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    ck_assert_int_eq(wingo_dht_token_reset(token), WINGO_SUCCESS);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_reset_null)
{
    ck_assert_int_eq(wingo_dht_token_reset(NULL), WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN GENERATION
 * ============================================================================ */

START_TEST(test_token_generate)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    ck_assert_int_eq(wingo_dht_token_generate(token, addr, out, sizeof(out)),
                     WINGO_SUCCESS);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_generate_deterministic)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out1[WINGO_DHT_TOKEN_SIZE];
    wingo_u8 out2[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr, out1, sizeof(out1));
    wingo_dht_token_generate(token, addr, out2, sizeof(out2));

    /* Same address + same secret → same token */
    ck_assert_mem_eq(out1, out2, WINGO_DHT_TOKEN_SIZE);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_generate_different_addrs)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr1 = make_addr("10.0.0.1", 6881);
    wingo_addr_t *addr2 = make_addr("10.0.0.2", 6881);
    wingo_u8 out1[WINGO_DHT_TOKEN_SIZE];
    wingo_u8 out2[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr1, out1, sizeof(out1));
    wingo_dht_token_generate(token, addr2, out2, sizeof(out2));

    /* Different addresses → different tokens */
    ck_assert_mem_ne(out1, out2, WINGO_DHT_TOKEN_SIZE);

    wingo_dht_token_free(token);
    wingo_addr_free(addr1);
    wingo_addr_free(addr2);
}
END_TEST

START_TEST(test_token_generate_same_ip_different_port)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr1 = make_addr("10.0.0.1", 6881);
    wingo_addr_t *addr2 = make_addr("10.0.0.1", 9999);
    wingo_u8 out1[WINGO_DHT_TOKEN_SIZE];
    wingo_u8 out2[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr1, out1, sizeof(out1));
    wingo_dht_token_generate(token, addr2, out2, sizeof(out2));

    /*
     * Same IP, different port → same token (we hash IP only).
     * This matches BitTorrent DHT behavior.
     */
    ck_assert_mem_eq(out1, out2, WINGO_DHT_TOKEN_SIZE);

    wingo_dht_token_free(token);
    wingo_addr_free(addr1);
    wingo_addr_free(addr2);
}
END_TEST

START_TEST(test_token_generate_null)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    ck_assert_int_eq(wingo_dht_token_generate(NULL, addr, out, sizeof(out)),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_token_generate(token, NULL, out, sizeof(out)),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_token_generate(token, addr, NULL, sizeof(out)),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_generate_buffer_too_small)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[4];  /* too small */

    ck_assert_int_eq(wingo_dht_token_generate(token, addr, out, sizeof(out)),
                     WINGO_ERR_OVERFLOW);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_generate_ip)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    ck_assert_int_eq(wingo_dht_token_generate_ip(token, addr, out, sizeof(out)),
                     WINGO_SUCCESS);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN VERIFICATION
 * ============================================================================ */

START_TEST(test_token_verify_valid)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    ck_assert_int_eq(wingo_dht_token_verify(token, addr, out, sizeof(out)),
                     WINGO_DHT_TOKEN_OK);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_invalid)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];
    wingo_u8 bad[WINGO_DHT_TOKEN_SIZE];

    memset(bad, 0xAA, sizeof(bad));

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    ck_assert_int_eq(wingo_dht_token_verify(token, addr, bad, sizeof(bad)),
                     WINGO_DHT_TOKEN_INVALID);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_wrong_addr)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr1 = make_addr("10.0.0.1", 6881);
    wingo_addr_t *addr2 = make_addr("10.0.0.2", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr1, out, sizeof(out));

    /* Token for addr1 should not verify for addr2 */
    ck_assert_int_eq(wingo_dht_token_verify(token, addr2, out, sizeof(out)),
                     WINGO_DHT_TOKEN_INVALID);

    wingo_dht_token_free(token);
    wingo_addr_free(addr1);
    wingo_addr_free(addr2);
}
END_TEST

START_TEST(test_token_verify_bad_len)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    /* Wrong length */
    ck_assert_int_eq(wingo_dht_token_verify(token, addr, out, 4),
                     WINGO_DHT_TOKEN_INVALID);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_null)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    ck_assert_int_eq(wingo_dht_token_verify(NULL, addr, out, sizeof(out)),
                     WINGO_DHT_TOKEN_INVALID);
    ck_assert_int_eq(wingo_dht_token_verify(token, NULL, out, sizeof(out)),
                     WINGO_DHT_TOKEN_INVALID);
    ck_assert_int_eq(wingo_dht_token_verify(token, addr, NULL, sizeof(out)),
                     WINGO_DHT_TOKEN_INVALID);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_ex)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];
    wingo_dht_token_result_t result;

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    ck_assert(wingo_dht_token_verify_ex(token, addr, out, sizeof(out),
                                         &result));
    ck_assert_int_eq(result, WINGO_DHT_TOKEN_OK);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_ex_invalid)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 bad[WINGO_DHT_TOKEN_SIZE];
    wingo_dht_token_result_t result;

    memset(bad, 0xAA, sizeof(bad));

    ck_assert(!wingo_dht_token_verify_ex(token, addr, bad, sizeof(bad),
                                          &result));
    ck_assert_int_eq(result, WINGO_DHT_TOKEN_INVALID);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_ex_null_result)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    /* Should not crash with NULL result */
    ck_assert(wingo_dht_token_verify_ex(token, addr, out, sizeof(out), NULL));

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_verify_secret)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    /* Generate with active secret */
    wingo_dht_token_generate(token, addr, out, sizeof(out));

    /* Verify with secret 0 (active) */
    ck_assert(wingo_dht_token_verify_secret(token, addr, 0,
                                             out, sizeof(out)));

    /* Verify with secret 1 (should fail) */
    ck_assert(!wingo_dht_token_verify_secret(token, addr, 1,
                                              out, sizeof(out)));

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN EQUAL (CONSTANT-TIME)
 * ============================================================================ */

START_TEST(test_token_equal)
{
    wingo_u8 a[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    wingo_u8 b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    wingo_u8 c[8] = {1, 2, 3, 4, 5, 6, 7, 9};

    ck_assert(wingo_dht_token_equal(a, b, 8));
    ck_assert(!wingo_dht_token_equal(a, c, 8));
}
END_TEST

START_TEST(test_token_equal_null)
{
    wingo_u8 a[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    ck_assert(!wingo_dht_token_equal(NULL, a, 8));
    ck_assert(!wingo_dht_token_equal(a, NULL, 8));
    ck_assert(!wingo_dht_token_equal(NULL, NULL, 8));
}
END_TEST

START_TEST(test_token_equal_zero_len)
{
    wingo_u8 a[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    ck_assert(wingo_dht_token_equal(a, a, 0));
}
END_TEST
/* ============================================================================
 * TEST: TOKEN ROTATION
 * ============================================================================ */

START_TEST(test_token_rotate)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out_before[WINGO_DHT_TOKEN_SIZE];
    wingo_u8 out_after[WINGO_DHT_TOKEN_SIZE];

    /* Generate with current secret */
    wingo_dht_token_generate(token, addr, out_before, sizeof(out_before));

    /* Rotate */
    ck_assert_int_eq(wingo_dht_token_rotate(token), WINGO_SUCCESS);

    /* Generate with new secret — should differ */
    wingo_dht_token_generate(token, addr, out_after, sizeof(out_after));

    ck_assert_mem_ne(out_before, out_after, WINGO_DHT_TOKEN_SIZE);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_rotate_old_still_valid)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out_before[WINGO_DHT_TOKEN_SIZE];

    /* Generate with current secret */
    wingo_dht_token_generate(token, addr, out_before, sizeof(out_before));

    /* Rotate */
    wingo_dht_token_rotate(token);

    /*
     * Old token should still verify (grace period).
     */
    ck_assert_int_eq(wingo_dht_token_verify(token, addr, out_before,
                                             sizeof(out_before)),
                     WINGO_DHT_TOKEN_OK);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_rotate_null)
{
    ck_assert_int_eq(wingo_dht_token_rotate(NULL), WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_token_needs_rotate)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    /* Fresh token should not need rotation */
    ck_assert(!wingo_dht_token_needs_rotate(token));

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_needs_rotate_null)
{
    ck_assert(!wingo_dht_token_needs_rotate(NULL));
}
END_TEST

START_TEST(test_token_next_rotate)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_i64 next;

    next = wingo_dht_token_next_rotate(token);

    /* Should be > 0 (within rotation interval) */
    ck_assert(next > 0);
    ck_assert(next <= WINGO_DHT_TOKEN_ROTATE_MAX);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_next_rotate_null)
{
    ck_assert_int_eq(wingo_dht_token_next_rotate(NULL), 0);
}
END_TEST

START_TEST(test_token_set_rotation)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    ck_assert_int_eq(wingo_dht_token_set_rotation(token, 60, 120),
                     WINGO_SUCCESS);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_set_rotation_invalid)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    /* min <= 0 */
    ck_assert_int_eq(wingo_dht_token_set_rotation(token, 0, 100),
                     WINGO_ERR_INVALID_ARG);

    /* max < min */
    ck_assert_int_eq(wingo_dht_token_set_rotation(token, 100, 50),
                     WINGO_ERR_INVALID_ARG);

    /* NULL */
    ck_assert_int_eq(wingo_dht_token_set_rotation(NULL, 60, 120),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_rotation_interval)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_i64 interval;

    interval = wingo_dht_token_rotation_interval(token);

    /* Should be positive */
    ck_assert(interval > 0);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_rotation_interval_null)
{
    ck_assert_int_eq(wingo_dht_token_rotation_interval(NULL), 0);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN SECRET
 * ============================================================================ */

START_TEST(test_token_secret)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    const wingo_dht_token_secret_t *secret;

    secret = wingo_dht_token_secret(token, 0);
    ck_assert_ptr_nonnull(secret);

    secret = wingo_dht_token_secret(token, 1);
    ck_assert_ptr_nonnull(secret);

    /* Invalid indices */
    ck_assert_ptr_null(wingo_dht_token_secret(token, -1));
    ck_assert_ptr_null(wingo_dht_token_secret(token, 2));
    ck_assert_ptr_null(wingo_dht_token_secret(NULL, 0));

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_secret_created)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_i64 created;

    created = wingo_dht_token_secret_created(token, 0);
    ck_assert(created > 0);

    /* Invalid index */
    ck_assert_int_eq(wingo_dht_token_secret_created(token, 99), 0);
    ck_assert_int_eq(wingo_dht_token_secret_created(NULL, 0), 0);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_secret_age)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_i64 age;

    age = wingo_dht_token_secret_age(token, 0);

    /* Should be very small (just created) */
    ck_assert(age >= 0);
    ck_assert(age < 5);

    /* Invalid index */
    ck_assert_int_eq(wingo_dht_token_secret_age(token, 99), 0);
    ck_assert_int_eq(wingo_dht_token_secret_age(NULL, 0), 0);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_secrets_clear)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    /* Should not crash */
    wingo_dht_token_secrets_clear(token);
    wingo_dht_token_secrets_clear(NULL);

    wingo_dht_token_free(token);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN STATISTICS
 * ============================================================================ */

START_TEST(test_token_get_stats)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_dht_token_stats_t stats;
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    /* Initial stats */
    ck_assert_int_eq(wingo_dht_token_get_stats(token, &stats), WINGO_SUCCESS);
    ck_assert_uint_eq(stats.tokens_generated, 0);
    ck_assert_uint_eq(stats.tokens_verified, 0);

    /* Generate some tokens */
    wingo_dht_token_generate(token, addr, out, sizeof(out));
    wingo_dht_token_generate(token, addr, out, sizeof(out));

    ck_assert_int_eq(wingo_dht_token_get_stats(token, &stats), WINGO_SUCCESS);
    ck_assert_uint_eq(stats.tokens_generated, 2);

    /* Verify */
    wingo_dht_token_verify(token, addr, out, sizeof(out));
    ck_assert_int_eq(wingo_dht_token_get_stats(token, &stats), WINGO_SUCCESS);
    ck_assert_uint_eq(stats.tokens_verified, 1);
    ck_assert_uint_eq(stats.tokens_valid, 1);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_token_get_stats_invalid)
{
    wingo_dht_token_stats_t stats;

    ck_assert_int_eq(wingo_dht_token_get_stats(NULL, &stats),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_token_reset_stats)
{
    wingo_dht_token_t *token = wingo_dht_token_new();
    wingo_dht_token_stats_t stats;
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_u8 out[WINGO_DHT_TOKEN_SIZE];

    wingo_dht_token_generate(token, addr, out, sizeof(out));

    wingo_dht_token_reset_stats(token);

    ck_assert_int_eq(wingo_dht_token_get_stats(token, &stats), WINGO_SUCCESS);
    ck_assert_uint_eq(stats.tokens_generated, 0);

    /* NULL should not crash */
    wingo_dht_token_reset_stats(NULL);

    wingo_dht_token_free(token);
    wingo_addr_free(addr);
}
END_TEST

/* ============================================================================
 * TEST: TOKEN UTILITY
 * ============================================================================ */

START_TEST(test_token_result_name)
{
    ck_assert_str_eq(wingo_dht_token_result_name(WINGO_DHT_TOKEN_OK), "OK");
    ck_assert_str_eq(wingo_dht_token_result_name(WINGO_DHT_TOKEN_INVALID),
                     "INVALID");
    ck_assert_str_eq(wingo_dht_token_result_name(WINGO_DHT_TOKEN_EXPIRED),
                     "EXPIRED");
    ck_assert_str_eq(wingo_dht_token_result_name(WINGO_DHT_TOKEN_WRONG_ADDR),
                     "WRONG_ADDR");
    ck_assert_str_eq(wingo_dht_token_result_name(WINGO_DHT_TOKEN_ERROR),
                     "ERROR");
}
END_TEST

START_TEST(test_token_print)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    /* Should not crash */
    wingo_dht_token_print(token, stdout);
    wingo_dht_token_print(token, NULL);
    wingo_dht_token_print(NULL, stdout);

    wingo_dht_token_free(token);
}
END_TEST

START_TEST(test_token_print_secrets)
{
    wingo_dht_token_t *token = wingo_dht_token_new();

    /* Should not crash */
    wingo_dht_token_print_secrets(token, stdout);
    wingo_dht_token_print_secrets(token, NULL);
    wingo_dht_token_print_secrets(NULL, stdout);

    wingo_dht_token_free(token);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *token_suite(void)
{
    Suite *s;
    TCase *tc_lifecycle;
    TCase *tc_generate;
    TCase *tc_verify;
    TCase *tc_equal;
    TCase *tc_rotate;
    TCase *tc_secret;
    TCase *tc_stats;
    TCase *tc_print;

    s = suite_create("DHT Token");

    /* Lifecycle */
    tc_lifecycle = tcase_create("Lifecycle");
    tcase_add_test(tc_lifecycle, test_token_new);
    tcase_add_test(tc_lifecycle, test_token_free_null);
    tcase_add_test(tc_lifecycle, test_token_reset);
    tcase_add_test(tc_lifecycle, test_token_reset_null);
    suite_add_tcase(s, tc_lifecycle);

    /* Generation */
    tc_generate = tcase_create("Generate");
    tcase_add_test(tc_generate, test_token_generate);
    tcase_add_test(tc_generate, test_token_generate_deterministic);
    tcase_add_test(tc_generate, test_token_generate_different_addrs);
    tcase_add_test(tc_generate, test_token_generate_same_ip_different_port);
    tcase_add_test(tc_generate, test_token_generate_null);
    tcase_add_test(tc_generate, test_token_generate_buffer_too_small);
    tcase_add_test(tc_generate, test_token_generate_ip);
    suite_add_tcase(s, tc_generate);

    /* Verification */
    tc_verify = tcase_create("Verify");
    tcase_add_test(tc_verify, test_token_verify_valid);
    tcase_add_test(tc_verify, test_token_verify_invalid);
    tcase_add_test(tc_verify, test_token_verify_wrong_addr);
    tcase_add_test(tc_verify, test_token_verify_bad_len);
    tcase_add_test(tc_verify, test_token_verify_null);
    tcase_add_test(tc_verify, test_token_verify_ex);
    tcase_add_test(tc_verify, test_token_verify_ex_invalid);
    tcase_add_test(tc_verify, test_token_verify_ex_null_result);
    tcase_add_test(tc_verify, test_token_verify_secret);
    suite_add_tcase(s, tc_verify);

    /* Equal */
    tc_equal = tcase_create("Equal");
    tcase_add_test(tc_equal, test_token_equal);
    tcase_add_test(tc_equal, test_token_equal_null);
    tcase_add_test(tc_equal, test_token_equal_zero_len);
    suite_add_tcase(s, tc_equal);

    /* Rotation */
    tc_rotate = tcase_create("Rotate");
    tcase_add_test(tc_rotate, test_token_rotate);
    tcase_add_test(tc_rotate, test_token_rotate_old_still_valid);
    tcase_add_test(tc_rotate, test_token_rotate_null);
    tcase_add_test(tc_rotate, test_token_needs_rotate);
    tcase_add_test(tc_rotate, test_token_needs_rotate_null);
    tcase_add_test(tc_rotate, test_token_next_rotate);
    tcase_add_test(tc_rotate, test_token_next_rotate_null);
    tcase_add_test(tc_rotate, test_token_set_rotation);
    tcase_add_test(tc_rotate, test_token_set_rotation_invalid);
    tcase_add_test(tc_rotate, test_token_rotation_interval);
    tcase_add_test(tc_rotate, test_token_rotation_interval_null);
    suite_add_tcase(s, tc_rotate);

    /* Secret */
    tc_secret = tcase_create("Secret");
    tcase_add_test(tc_secret, test_token_secret);
    tcase_add_test(tc_secret, test_token_secret_created);
    tcase_add_test(tc_secret, test_token_secret_age);
    tcase_add_test(tc_secret, test_token_secrets_clear);
    suite_add_tcase(s, tc_secret);

    /* Stats */
    tc_stats = tcase_create("Stats");
    tcase_add_test(tc_stats, test_token_get_stats);
    tcase_add_test(tc_stats, test_token_get_stats_invalid);
    tcase_add_test(tc_stats, test_token_reset_stats);
    suite_add_tcase(s, tc_stats);

    /* Print */
    tc_print = tcase_create("Print");
    tcase_add_test(tc_print, test_token_result_name);
    tcase_add_test(tc_print, test_token_print);
    tcase_add_test(tc_print, test_token_print_secrets);
    suite_add_tcase(s, tc_print);

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

    s = token_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
