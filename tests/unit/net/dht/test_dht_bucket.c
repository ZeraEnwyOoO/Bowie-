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
 * Unit tests for wingo/net/dht/dht_bucket.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/net/dht/dht_bucket.h"
#include "wingo/net/dht/dht_node.h"
#include "wingo/net/dht/dht_types.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

static void make_id(wingo_dht_id_t *id, wingo_u8 first_byte)
{
    memset(id, 0, sizeof(wingo_dht_id_t));
    id->bytes[0] = first_byte;
}

static wingo_addr_t *make_addr(wingo_u8 first_byte, wingo_u16 port)
{
    char ip[32];
    snprintf(ip, sizeof(ip), "10.0.0.%u", first_byte);
    return wingo_addr_new_ipv4(ip, port);
}

/*
 * Create a node with given ID.
 */
static wingo_dht_node_t *make_node(wingo_u8 first_byte)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;

    make_id(&id, first_byte);
    addr = make_addr(first_byte, 6881);

    node = wingo_dht_node_new(&id, addr);
    wingo_addr_free(addr);

    return node;
}

/* ============================================================================
 * TEST: BUCKET CREATION
 * ============================================================================ */

START_TEST(test_bucket_new_valid)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);

    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    ck_assert_ptr_nonnull(bucket);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 0);
    ck_assert_uint_eq(wingo_dht_bucket_max_count(bucket), 8);
    ck_assert(wingo_dht_bucket_is_empty(bucket));
    ck_assert(!wingo_dht_bucket_is_full(bucket));
    ck_assert_int_eq(wingo_dht_bucket_family(bucket), WINGO_ADDR_IPV4);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_new_null_first)
{
    wingo_dht_bucket_t *bucket = wingo_dht_bucket_new(NULL, WINGO_ADDR_IPV4, 8);
    ck_assert_ptr_null(bucket);
}
END_TEST

START_TEST(test_bucket_new_zero_max)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);

    /* 0 → default (8) */
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 0);
    ck_assert_ptr_nonnull(bucket);
    ck_assert_uint_eq(wingo_dht_bucket_max_count(bucket),
                      WINGO_DHT_BUCKET_DEFAULT_SIZE);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_new_min_max)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);

    /* Less than min → clamped to min */
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 1);
    ck_assert_ptr_nonnull(bucket);
    ck_assert_uint_ge(wingo_dht_bucket_max_count(bucket),
                      WINGO_DHT_BUCKET_MIN_SIZE);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_free_null)
{
    /* Should not crash */
    wingo_dht_bucket_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET QUERY
 * ============================================================================ */

START_TEST(test_bucket_first)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x42);

    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    ck_assert_ptr_nonnull(wingo_dht_bucket_first(bucket));
    ck_assert_mem_eq(wingo_dht_bucket_first(bucket)->bytes,
                     first.bytes, WINGO_DHT_ID_SIZE);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_first_null)
{
    ck_assert_ptr_null(wingo_dht_bucket_first(NULL));
}
END_TEST

START_TEST(test_bucket_contains)
{
    wingo_dht_id_t first;
    wingo_dht_id_t in_range;
    wingo_dht_id_t below;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x10);
    make_id(&in_range, 0x20);
    make_id(&below, 0x00);

    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    ck_assert(wingo_dht_bucket_contains(bucket, &in_range));
    ck_assert(!wingo_dht_bucket_contains(bucket, &below));
    ck_assert(!wingo_dht_bucket_contains(bucket, NULL));
    ck_assert(!wingo_dht_bucket_contains(NULL, &in_range));

    wingo_dht_bucket_free(bucket);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET NODE MANAGEMENT — ADD
 * ============================================================================ */

START_TEST(test_bucket_add_node)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, node), WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 1);
    ck_assert_ptr_eq(wingo_dht_bucket_first_node(bucket), node);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_add_multiple)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2, *n3;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    n1 = make_node(0x01);
    n2 = make_node(0x02);
    n3 = make_node(0x03);

    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, n1), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, n2), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, n3), WINGO_SUCCESS);

    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 3);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_add_full)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *nodes[8];
    wingo_dht_node_t *extra;
    int i;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    for (i = 0; i < 4; i++) {
        nodes[i] = make_node((wingo_u8)(i + 1));
        ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, nodes[i]),
                         WINGO_SUCCESS);
    }

    ck_assert(wingo_dht_bucket_is_full(bucket));
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 4);

    /* Adding more should return BUSY (cached) */
    extra = make_node(0xFF);
    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, extra),
                     WINGO_ERR_BUSY);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(extra);
}
END_TEST

START_TEST(test_bucket_add_null)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    ck_assert_int_eq(wingo_dht_bucket_add_node(NULL, node),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_bucket_add_node(bucket, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_bucket_add_duplicate)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;
    wingo_dht_node_t *dup;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    node = make_node(0x42);
    dup = make_node(0x42);  /* Same ID */

    wingo_dht_bucket_add_node(bucket, node);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 1);

    /* Duplicate should not increase count */
    wingo_dht_bucket_add_node(bucket, dup);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 1);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(dup);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET NODE MANAGEMENT — REMOVE
 * ============================================================================ */

START_TEST(test_bucket_remove_node)
{
    wingo_dht_id_t first;
    wingo_dht_id_t node_id;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);
    make_id(&node_id, 0x42);

    wingo_dht_bucket_add_node(bucket, node);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 1);

    ck_assert_int_eq(wingo_dht_bucket_remove_node(bucket, &node_id),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 0);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_remove_missing)
{
    wingo_dht_id_t first;
    wingo_dht_id_t node_id;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    make_id(&node_id, 0x99);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert_int_eq(wingo_dht_bucket_remove_node(bucket, &node_id),
                     WINGO_ERR_NOT_FOUND);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_remove_node_ptr)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    wingo_dht_bucket_add_node(bucket, node);

    ck_assert_int_eq(wingo_dht_bucket_remove_node_ptr(bucket, node),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 0);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_remove_node_ptr_missing)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    ck_assert_int_eq(wingo_dht_bucket_remove_node_ptr(bucket, node),
                     WINGO_ERR_NOT_FOUND);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(node);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET NODE MANAGEMENT — FIND
 * ============================================================================ */

START_TEST(test_bucket_find_node)
{
    wingo_dht_id_t first;
    wingo_dht_id_t node_id;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);
    make_id(&node_id, 0x42);

    wingo_dht_bucket_add_node(bucket, node);

    ck_assert_ptr_eq(wingo_dht_bucket_find_node(bucket, &node_id), node);
    ck_assert_ptr_null(wingo_dht_bucket_find_node(bucket, NULL));

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_get_node)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    n1 = make_node(0x01);
    n2 = make_node(0x02);

    wingo_dht_bucket_add_node(bucket, n1);
    wingo_dht_bucket_add_node(bucket, n2);

    ck_assert_ptr_eq(wingo_dht_bucket_get_node(bucket, 0), n1);
    ck_assert_ptr_eq(wingo_dht_bucket_get_node(bucket, 1), n2);
    ck_assert_ptr_null(wingo_dht_bucket_get_node(bucket, 2));
    ck_assert_ptr_null(wingo_dht_bucket_get_node(bucket, 100));

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_first_last_node)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2, *n3;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    /* Empty bucket */
    ck_assert_ptr_null(wingo_dht_bucket_first_node(bucket));
    ck_assert_ptr_null(wingo_dht_bucket_last_node(bucket));

    n1 = make_node(0x01);
    n2 = make_node(0x02);
    n3 = make_node(0x03);

    wingo_dht_bucket_add_node(bucket, n1);
    wingo_dht_bucket_add_node(bucket, n2);
    wingo_dht_bucket_add_node(bucket, n3);

    ck_assert_ptr_eq(wingo_dht_bucket_first_node(bucket), n1);
    ck_assert_ptr_eq(wingo_dht_bucket_last_node(bucket), n3);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_random_node)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2, *random;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    /* Empty bucket */
    ck_assert_ptr_null(wingo_dht_bucket_random_node(bucket));

    n1 = make_node(0x01);
    n2 = make_node(0x02);

    wingo_dht_bucket_add_node(bucket, n1);
    wingo_dht_bucket_add_node(bucket, n2);

    random = wingo_dht_bucket_random_node(bucket);
    ck_assert_ptr_nonnull(random);
    ck_assert(random == n1 || random == n2);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_next_node)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2, *n3;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    n1 = make_node(0x01);
    n2 = make_node(0x02);
    n3 = make_node(0x03);

    wingo_dht_bucket_add_node(bucket, n1);
    wingo_dht_bucket_add_node(bucket, n2);
    wingo_dht_bucket_add_node(bucket, n3);

    ck_assert_ptr_eq(wingo_dht_bucket_next_node(bucket, NULL), n1);
    ck_assert_ptr_eq(wingo_dht_bucket_next_node(bucket, n1), n2);
    ck_assert_ptr_eq(wingo_dht_bucket_next_node(bucket, n2), n3);
    ck_assert_ptr_null(wingo_dht_bucket_next_node(bucket, n3));

    wingo_dht_bucket_free_all(bucket);
}
END_TEST
/* ============================================================================
 * TEST: BUCKET CACHED NODE
 * ============================================================================ */

START_TEST(test_bucket_cached_initial)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert(!wingo_dht_bucket_has_cached(bucket));
    ck_assert_ptr_null(wingo_dht_bucket_get_cached(bucket));

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_set_cached)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    ck_assert_int_eq(wingo_dht_bucket_set_cached(bucket, node), WINGO_SUCCESS);
    ck_assert(wingo_dht_bucket_has_cached(bucket));
    ck_assert_ptr_nonnull(wingo_dht_bucket_get_cached(bucket));

    /* Cached node is a clone, so different pointer */
    ck_assert_ptr_ne(wingo_dht_bucket_get_cached(bucket), node);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_bucket_set_cached_replace)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *n1, *n2;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    n1 = make_node(0x01);
    n2 = make_node(0x02);

    wingo_dht_bucket_set_cached(bucket, n1);
    ck_assert(wingo_dht_bucket_has_cached(bucket));

    /* Replace with n2 */
    wingo_dht_bucket_set_cached(bucket, n2);
    ck_assert(wingo_dht_bucket_has_cached(bucket));

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(n1);
    wingo_dht_node_free(n2);
}
END_TEST

START_TEST(test_bucket_clear_cached)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);
    node = make_node(0x42);

    wingo_dht_bucket_set_cached(bucket, node);
    ck_assert(wingo_dht_bucket_has_cached(bucket));

    wingo_dht_bucket_clear_cached(bucket);
    ck_assert(!wingo_dht_bucket_has_cached(bucket));

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_bucket_set_cached_null)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert_int_eq(wingo_dht_bucket_set_cached(NULL, NULL),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_bucket_set_cached(bucket, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_bucket_free(bucket);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET SPLIT
 * ============================================================================ */

START_TEST(test_bucket_middle)
{
    wingo_dht_id_t first;
    wingo_dht_id_t mid;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert_int_eq(wingo_dht_bucket_middle(bucket, &mid), WINGO_SUCCESS);

    /* Middle should be > first (since first = 0x00) */
    ck_assert(memcmp(mid.bytes, first.bytes, WINGO_DHT_ID_SIZE) > 0);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_middle_null)
{
    wingo_dht_id_t mid;
    wingo_dht_bucket_t *bucket;
    wingo_dht_id_t first;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert_int_eq(wingo_dht_bucket_middle(NULL, &mid),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_bucket_middle(bucket, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_random_id)
{
    wingo_dht_id_t first;
    wingo_dht_id_t random_id;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    ck_assert_int_eq(wingo_dht_bucket_random_id(bucket, &random_id),
                     WINGO_SUCCESS);

    /* Random ID should be >= first */
    ck_assert(memcmp(random_id.bytes, first.bytes, WINGO_DHT_ID_SIZE) >= 0);

    wingo_dht_bucket_free(bucket);
}
END_TEST

START_TEST(test_bucket_split)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_bucket_t *new_bucket;
    wingo_dht_node_t *nodes[4];
    wingo_dht_id_t mid;
    int i;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 4);

    /* Add nodes */
    for (i = 0; i < 4; i++) {
        nodes[i] = make_node((wingo_u8)(i * 0x40));  /* 0x00, 0x40, 0x80, 0xC0 */
        wingo_dht_bucket_add_node(bucket, nodes[i]);
    }

    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 4);

    /* Get middle before split */
    wingo_dht_bucket_middle(bucket, &mid);

    /* Split */
    new_bucket = wingo_dht_bucket_split(bucket);

    ck_assert_ptr_nonnull(new_bucket);
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 2);
    ck_assert_uint_eq(wingo_dht_bucket_count(new_bucket), 2);

    /* New bucket's first should be the middle */
    ck_assert_mem_eq(wingo_dht_bucket_first(new_bucket)->bytes,
                     mid.bytes, WINGO_DHT_ID_SIZE);

    wingo_dht_bucket_free_all(bucket);
    wingo_dht_bucket_free_all(new_bucket);
}
END_TEST

START_TEST(test_bucket_split_null)
{
    ck_assert_ptr_null(wingo_dht_bucket_split(NULL));
}
END_TEST

/* ============================================================================
 * TEST: BUCKET FILTERING
 * ============================================================================ */

START_TEST(test_bucket_good_nodes)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *good1, *good2, *bad;
    wingo_dht_node_t *out[4];
    wingo_size count;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    good1 = make_node(0x01);
    good2 = make_node(0x02);
    bad = make_node(0x03);

    wingo_dht_node_replied(good1);
    wingo_dht_node_replied(good2);
    wingo_dht_node_set_state(bad, WINGO_DHT_NODE_STATE_BAD);

    wingo_dht_bucket_add_node(bucket, good1);
    wingo_dht_bucket_add_node(bucket, good2);
    wingo_dht_bucket_add_node(bucket, bad);

    count = wingo_dht_bucket_good_nodes(bucket, out, 4);

    ck_assert_uint_eq(count, 2);
    ck_assert(out[0] == good1 || out[0] == good2);
    ck_assert(out[1] == good1 || out[1] == good2);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_closest_nodes)
{
    wingo_dht_id_t first;
    wingo_dht_id_t target;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *nodes[4];
    wingo_dht_node_t *out[4];
    wingo_size count;
    int i;

    make_id(&first, 0x00);
    make_id(&target, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    /* Distances: 0x01 (closest), 0x10, 0x40, 0x80 (farthest) */
    nodes[0] = make_node(0x80);
    nodes[1] = make_node(0x10);
    nodes[2] = make_node(0x40);
    nodes[3] = make_node(0x01);

    for (i = 0; i < 4; i++) {
        wingo_dht_bucket_add_node(bucket, nodes[i]);
    }

    count = wingo_dht_bucket_closest_nodes(bucket, &target, out, 4);
    ck_assert_uint_eq(count, 4);

    /* First should be closest (0x01) */
    {
        wingo_dht_id_t id;
        make_id(&id, 0x01);
        ck_assert(wingo_dht_node_has_id(out[0], &id));
    }

    /* Last should be farthest (0x80) */
    {
        wingo_dht_id_t id;
        make_id(&id, 0x80);
        ck_assert(wingo_dht_node_has_id(out[3], &id));
    }

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_expire)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *good, *bad;
    wingo_size expired;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    good = make_node(0x01);
    bad = make_node(0x02);

    wingo_dht_node_replied(good);
    wingo_dht_node_set_state(bad, WINGO_DHT_NODE_STATE_BAD);

    wingo_dht_bucket_add_node(bucket, good);
    wingo_dht_bucket_add_node(bucket, bad);

    expired = wingo_dht_bucket_expire(bucket);

    ck_assert_uint_eq(expired, 1);  /* bad node */
    ck_assert_uint_eq(wingo_dht_bucket_count(bucket), 1);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_needs_refresh)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    /* Fresh bucket, large max_age → no refresh needed */
    ck_assert(!wingo_dht_bucket_needs_refresh(bucket, 3600));

    /* Small max_age → refresh needed */
    ck_assert(wingo_dht_bucket_needs_refresh(bucket, -1));

    wingo_dht_bucket_free(bucket);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET STATISTICS
 * ============================================================================ */

START_TEST(test_bucket_get_stats)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *good, *dubious, *bad;
    wingo_dht_bucket_stats_t stats;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);

    good = make_node(0x01);
    dubious = make_node(0x02);
    bad = make_node(0x03);

    wingo_dht_node_replied(good);
    wingo_dht_node_set_state(dubious, WINGO_DHT_NODE_STATE_DUBIOUS);
    wingo_dht_node_set_state(bad, WINGO_DHT_NODE_STATE_BAD);

    wingo_dht_bucket_add_node(bucket, good);
    wingo_dht_bucket_add_node(bucket, dubious);
    wingo_dht_bucket_add_node(bucket, bad);

    ck_assert_int_eq(wingo_dht_bucket_get_stats(bucket, &stats),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(stats.count, 3);
    ck_assert_uint_eq(stats.good, 1);
    ck_assert_uint_eq(stats.dubious, 1);
    ck_assert_uint_eq(stats.bad, 1);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

/* ============================================================================
 * TEST: BUCKET UTILITY
 * ============================================================================ */

START_TEST(test_bucket_print)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);
    node = make_node(0x42);
    wingo_dht_bucket_add_node(bucket, node);

    /* Should not crash */
    wingo_dht_bucket_print(bucket, stdout);
    wingo_dht_bucket_print(bucket, NULL);
    wingo_dht_bucket_print(NULL, stdout);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

START_TEST(test_bucket_print_nodes)
{
    wingo_dht_id_t first;
    wingo_dht_bucket_t *bucket;
    wingo_dht_node_t *node;

    make_id(&first, 0x00);
    bucket = wingo_dht_bucket_new(&first, WINGO_ADDR_IPV4, 8);
    node = make_node(0x42);
    wingo_dht_bucket_add_node(bucket, node);

    /* Should not crash */
    wingo_dht_bucket_print_nodes(bucket, stdout);
    wingo_dht_bucket_print_nodes(bucket, NULL);
    wingo_dht_bucket_print_nodes(NULL, stdout);

    wingo_dht_bucket_free_all(bucket);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *bucket_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_query;
    TCase *tc_add;
    TCase *tc_remove;
    TCase *tc_find;
    TCase *tc_cached;
    TCase *tc_split;
    TCase *tc_filter;
    TCase *tc_print;

    s = suite_create("DHT Bucket");

    tc_create = tcase_create("Create");
    tcase_add_test(tc_create, test_bucket_new_valid);
    tcase_add_test(tc_create, test_bucket_new_null_first);
    tcase_add_test(tc_create, test_bucket_new_zero_max);
    tcase_add_test(tc_create, test_bucket_new_min_max);
    tcase_add_test(tc_create, test_bucket_free_null);
    suite_add_tcase(s, tc_create);

    tc_query = tcase_create("Query");
    tcase_add_test(tc_query, test_bucket_first);
    tcase_add_test(tc_query, test_bucket_first_null);
    tcase_add_test(tc_query, test_bucket_contains);
    suite_add_tcase(s, tc_query);

    tc_add = tcase_create("Add");
    tcase_add_test(tc_add, test_bucket_add_node);
    tcase_add_test(tc_add, test_bucket_add_multiple);
    tcase_add_test(tc_add, test_bucket_add_full);
    tcase_add_test(tc_add, test_bucket_add_null);
    tcase_add_test(tc_add, test_bucket_add_duplicate);
    suite_add_tcase(s, tc_add);

    tc_remove = tcase_create("Remove");
    tcase_add_test(tc_remove, test_bucket_remove_node);
    tcase_add_test(tc_remove, test_bucket_remove_missing);
    tcase_add_test(tc_remove, test_bucket_remove_node_ptr);
    tcase_add_test(tc_remove, test_bucket_remove_node_ptr_missing);
    suite_add_tcase(s, tc_remove);

    tc_find = tcase_create("Find");
    tcase_add_test(tc_find, test_bucket_find_node);
    tcase_add_test(tc_find, test_bucket_get_node);
    tcase_add_test(tc_find, test_bucket_first_last_node);
    tcase_add_test(tc_find, test_bucket_random_node);
    tcase_add_test(tc_find, test_bucket_next_node);
    suite_add_tcase(s, tc_find);

    tc_cached = tcase_create("Cached");
    tcase_add_test(tc_cached, test_bucket_cached_initial);
    tcase_add_test(tc_cached, test_bucket_set_cached);
    tcase_add_test(tc_cached, test_bucket_set_cached_replace);
    tcase_add_test(tc_cached, test_bucket_clear_cached);
    tcase_add_test(tc_cached, test_bucket_set_cached_null);
    suite_add_tcase(s, tc_cached);

    tc_split = tcase_create("Split");
    tcase_add_test(tc_split, test_bucket_middle);
    tcase_add_test(tc_split, test_bucket_middle_null);
    tcase_add_test(tc_split, test_bucket_random_id);
    tcase_add_test(tc_split, test_bucket_split);
    tcase_add_test(tc_split, test_bucket_split_null);
    suite_add_tcase(s, tc_split);

    tc_filter = tcase_create("Filter");
    tcase_add_test(tc_filter, test_bucket_good_nodes);
    tcase_add_test(tc_filter, test_bucket_closest_nodes);
    tcase_add_test(tc_filter, test_bucket_expire);
    tcase_add_test(tc_filter, test_bucket_needs_refresh);
    tcase_add_test(tc_filter, test_bucket_get_stats);
    suite_add_tcase(s, tc_filter);

    tc_print = tcase_create("Print");
    tcase_add_test(tc_print, test_bucket_print);
    tcase_add_test(tc_print, test_bucket_print_nodes);
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

    s = bucket_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
