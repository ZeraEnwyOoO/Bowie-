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
 * Unit tests for wingo/net/dht/dht_routing.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/net/dht/dht_routing.h"
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

/* ============================================================================
 * TEST: ROUTING TABLE CREATION
 * ============================================================================ */

START_TEST(test_routing_new)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x42);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_ptr_nonnull(rt);
    ck_assert_uint_eq(wingo_dht_routing_bucket_count(rt), 1);
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 0);
    ck_assert_uint_eq(wingo_dht_routing_good_count(rt), 0);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_new_null_id)
{
    wingo_dht_routing_t *rt = wingo_dht_routing_new(NULL, WINGO_ADDR_IPV4, 8);
    ck_assert_ptr_null(rt);
}
END_TEST

START_TEST(test_routing_new_zero_bucket_size)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x42);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 0);

    ck_assert_ptr_nonnull(rt);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_free_null)
{
    /* Should not crash */
    wingo_dht_routing_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — FIND BUCKET
 * ============================================================================ */

START_TEST(test_routing_find_bucket)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t target;
    wingo_dht_routing_t *rt;
    wingo_dht_bucket_t *bucket;

    make_id(&my_id, 0x42);
    make_id(&target, 0x80);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    bucket = wingo_dht_routing_find_bucket(rt, &target);
    ck_assert_ptr_nonnull(bucket);

    /* NULL cases */
    ck_assert_ptr_null(wingo_dht_routing_find_bucket(NULL, &target));
    ck_assert_ptr_null(wingo_dht_routing_find_bucket(rt, NULL));

    wingo_dht_routing_free(rt);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — ADD NODE
 * ============================================================================ */

START_TEST(test_routing_add_node)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *node;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    node = wingo_dht_routing_add_node(rt, &node_id, addr, 0);

    ck_assert_ptr_nonnull(node);
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 1);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_add_node_self)
{
    wingo_dht_id_t my_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *node;

    make_id(&my_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* Adding our own ID should be rejected */
    node = wingo_dht_routing_add_node(rt, &my_id, addr, 0);

    ck_assert_ptr_null(node);
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 0);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_add_node_null)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_ptr_null(wingo_dht_routing_add_node(NULL, &node_id, addr, 0));
    ck_assert_ptr_null(wingo_dht_routing_add_node(rt, NULL, addr, 0));
    ck_assert_ptr_null(wingo_dht_routing_add_node(rt, &node_id, NULL, 0));

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_add_multiple)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_ids[4];
    wingo_addr_t *addrs[4];
    wingo_dht_routing_t *rt;
    int i;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    for (i = 0; i < 4; i++) {
        make_id(&node_ids[i], (wingo_u8)(i + 1));
        addrs[i] = make_addr((wingo_u8)(i + 1), 6881);

        ck_assert_ptr_nonnull(
            wingo_dht_routing_add_node(rt, &node_ids[i], addrs[i], 0));
    }

    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 4);

    wingo_dht_routing_free(rt);

    for (i = 0; i < 4; i++) {
        wingo_addr_free(addrs[i]);
    }
}
END_TEST

START_TEST(test_routing_add_duplicate)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    wingo_dht_routing_add_node(rt, &node_id, addr, 0);
    wingo_dht_routing_add_node(rt, &node_id, addr, 0);

    /* Should still be 1 */
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 1);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — FIND NODE
 * ============================================================================ */

START_TEST(test_routing_find_node)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *node;
    wingo_dht_node_t *found;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    node = wingo_dht_routing_add_node(rt, &node_id, addr, 0);
    ck_assert_ptr_nonnull(node);

    found = wingo_dht_routing_find_node(rt, &node_id);
    ck_assert_ptr_eq(found, node);

    /* Missing */
    {
        wingo_dht_id_t missing;
        make_id(&missing, 0x99);
        ck_assert_ptr_null(wingo_dht_routing_find_node(rt, &missing));
    }

    /* NULL */
    ck_assert_ptr_null(wingo_dht_routing_find_node(NULL, &node_id));
    ck_assert_ptr_null(wingo_dht_routing_find_node(rt, NULL));

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — REMOVE NODE
 * ============================================================================ */

START_TEST(test_routing_remove_node)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    wingo_dht_routing_add_node(rt, &node_id, addr, 0);
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 1);

    ck_assert_int_eq(wingo_dht_routing_remove_node(rt, &node_id),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 0);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_remove_missing)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x99);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_int_eq(wingo_dht_routing_remove_node(rt, &node_id),
                     WINGO_ERR_NOT_FOUND);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_remove_node_null)
{
    wingo_dht_id_t node_id;

    make_id(&node_id, 0x42);

    ck_assert_int_eq(wingo_dht_routing_remove_node(NULL, &node_id),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — CLOSEST NODES
 * ============================================================================ */

START_TEST(test_routing_closest_nodes)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_ids[4];
    wingo_dht_id_t target;
    wingo_addr_t *addrs[4];
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *out[4];
    wingo_size count;
    int i;

    make_id(&my_id, 0x00);
    make_id(&target, 0x00);

    /* Node IDs: 0x01 (closest), 0x10, 0x40, 0x80 (farthest) */
    make_id(&node_ids[0], 0x80);
    make_id(&node_ids[1], 0x10);
    make_id(&node_ids[2], 0x40);
    make_id(&node_ids[3], 0x01);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    for (i = 0; i < 4; i++) {
        addrs[i] = make_addr((wingo_u8)(i + 1), 6881);
        wingo_dht_routing_add_node(rt, &node_ids[i], addrs[i], 0);
    }

    count = wingo_dht_routing_closest_nodes(rt, &target, out, 4);

    ck_assert_uint_eq(count, 4);

    /* First should be closest (0x01) */
    ck_assert(wingo_dht_node_has_id(out[0], &node_ids[3]));

    /* Last should be farthest (0x80) */
    ck_assert(wingo_dht_node_has_id(out[3], &node_ids[0]));

    wingo_dht_routing_free(rt);

    for (i = 0; i < 4; i++) {
        wingo_addr_free(addrs[i]);
    }
}
END_TEST

START_TEST(test_routing_closest_nodes_empty)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t target;
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *out[4];
    wingo_size count;

    make_id(&my_id, 0x00);
    make_id(&target, 0x00);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    count = wingo_dht_routing_closest_nodes(rt, &target, out, 4);
    ck_assert_uint_eq(count, 0);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_closest_nodes_null)
{
    wingo_dht_id_t target;
    wingo_dht_node_t *out[4];

    make_id(&target, 0x00);

    ck_assert_uint_eq(wingo_dht_routing_closest_nodes(NULL, &target, out, 4), 0);
    ck_assert_uint_eq(wingo_dht_routing_closest_nodes(NULL, NULL, out, 4), 0);
    ck_assert_uint_eq(wingo_dht_routing_closest_nodes(NULL, &target, NULL, 4), 0);
    ck_assert_uint_eq(wingo_dht_routing_closest_nodes(NULL, &target, out, 0), 0);
}
END_TEST

START_TEST(test_routing_closest_nodes_limit)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_ids[8];
    wingo_dht_id_t target;
    wingo_addr_t *addrs[8];
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *out[3];
    wingo_size count;
    int i;

    make_id(&my_id, 0x00);
    make_id(&target, 0x00);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 16);

    for (i = 0; i < 8; i++) {
        make_id(&node_ids[i], (wingo_u8)(i + 1));
        addrs[i] = make_addr((wingo_u8)(i + 1), 6881);
        wingo_dht_routing_add_node(rt, &node_ids[i], addrs[i], 0);
    }

    count = wingo_dht_routing_closest_nodes(rt, &target, out, 3);
    ck_assert_uint_eq(count, 3);

    wingo_dht_routing_free(rt);

    for (i = 0; i < 8; i++) {
        wingo_addr_free(addrs[i]);
    }
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — RANDOM NODES
 * ============================================================================ */

START_TEST(test_routing_random_nodes)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_ids[4];
    wingo_addr_t *addrs[4];
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *out[4];
    wingo_size count;
    int i;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    for (i = 0; i < 4; i++) {
        make_id(&node_ids[i], (wingo_u8)(i + 1));
        addrs[i] = make_addr((wingo_u8)(i + 1), 6881);
        wingo_dht_routing_add_node(rt, &node_ids[i], addrs[i], 0);
    }

    count = wingo_dht_routing_random_nodes(rt, out, 4);
    ck_assert_uint_eq(count, 4);

    /* All should be non-null */
    for (i = 0; i < 4; i++) {
        ck_assert_ptr_nonnull(out[i]);
    }

    wingo_dht_routing_free(rt);

    for (i = 0; i < 4; i++) {
        wingo_addr_free(addrs[i]);
    }
}
END_TEST

START_TEST(test_routing_random_nodes_empty)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;
    wingo_dht_node_t *out[4];
    wingo_size count;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    count = wingo_dht_routing_random_nodes(rt, out, 4);
    ck_assert_uint_eq(count, 0);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_random_nodes_null)
{
    wingo_dht_node_t *out[4];

    ck_assert_uint_eq(wingo_dht_routing_random_nodes(NULL, out, 4), 0);
}
END_TEST
/* ============================================================================
 * TEST: ROUTING TABLE — SPLIT BUCKET
 * ============================================================================ */

START_TEST(test_routing_split_bucket)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;
    wingo_dht_bucket_t *bucket;
    wingo_dht_bucket_t *bucket_before;
    wingo_size bucket_count_before;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 4);

    bucket_count_before = wingo_dht_routing_bucket_count(rt);
    bucket = wingo_dht_routing_first_bucket(rt);
    ck_assert_ptr_nonnull(bucket);

    bucket_before = bucket;

    ck_assert_int_eq(wingo_dht_routing_split_bucket(rt, bucket),
                     WINGO_SUCCESS);

    ck_assert_uint_eq(wingo_dht_routing_bucket_count(rt),
                      bucket_count_before + 1);

    /* First bucket should still be the same pointer */
    ck_assert_ptr_eq(wingo_dht_routing_first_bucket(rt), bucket_before);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_split_bucket_null)
{
    ck_assert_int_eq(wingo_dht_routing_split_bucket(NULL, NULL),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — EXPIRE
 * ============================================================================ */

START_TEST(test_routing_expire)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;
    wingo_size expired;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);
    wingo_dht_routing_add_node(rt, &node_id, addr, 0);

    /* No bad nodes yet */
    expired = wingo_dht_routing_expire(rt);
    ck_assert_uint_eq(expired, 0);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_expire_null)
{
    ck_assert_uint_eq(wingo_dht_routing_expire(NULL), 0);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — MAINTENANCE
 * ============================================================================ */

START_TEST(test_routing_maintenance)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_int_eq(wingo_dht_routing_maintenance(rt), WINGO_SUCCESS);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_maintenance_null)
{
    ck_assert_int_eq(wingo_dht_routing_maintenance(NULL),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_routing_neighbourhood)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_int_eq(wingo_dht_routing_neighbourhood(rt), WINGO_SUCCESS);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_neighbourhood_null)
{
    ck_assert_int_eq(wingo_dht_routing_neighbourhood(NULL),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — STATISTICS
 * ============================================================================ */

START_TEST(test_routing_bucket_count)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_uint_eq(wingo_dht_routing_bucket_count(rt), 1);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_bucket_count_null)
{
    ck_assert_uint_eq(wingo_dht_routing_bucket_count(NULL), 0);
}
END_TEST

START_TEST(test_routing_node_count)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 0);

    wingo_dht_routing_add_node(rt, &node_id, addr, 0);

    ck_assert_uint_eq(wingo_dht_routing_node_count(rt), 1);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_node_count_null)
{
    ck_assert_uint_eq(wingo_dht_routing_node_count(NULL), 0);
}
END_TEST

START_TEST(test_routing_good_count)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* Add with confirm=2 → good */
    wingo_dht_routing_add_node(rt, &node_id, addr, 2);

    ck_assert_uint_eq(wingo_dht_routing_good_count(rt), 1);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_good_count_null)
{
    ck_assert_uint_eq(wingo_dht_routing_good_count(NULL), 0);
}
END_TEST

START_TEST(test_routing_dubious_count)
{
    wingo_dht_id_t my_id;
    wingo_dht_id_t node_id;
    wingo_addr_t *addr;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    make_id(&node_id, 0x42);
    addr = make_addr(0x42, 6881);

    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* Add with confirm=1 → dubious */
    wingo_dht_routing_add_node(rt, &node_id, addr, 1);

    ck_assert_uint_eq(wingo_dht_routing_dubious_count(rt), 1);

    wingo_dht_routing_free(rt);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_routing_dubious_count_null)
{
    ck_assert_uint_eq(wingo_dht_routing_dubious_count(NULL), 0);
}
END_TEST

START_TEST(test_routing_bad_count)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* Empty table → 0 bad */
    ck_assert_uint_eq(wingo_dht_routing_bad_count(rt), 0);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_bad_count_null)
{
    ck_assert_uint_eq(wingo_dht_routing_bad_count(NULL), 0);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — ITERATION
 * ============================================================================ */

START_TEST(test_routing_first_bucket)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;
    wingo_dht_bucket_t *bucket;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    bucket = wingo_dht_routing_first_bucket(rt);
    ck_assert_ptr_nonnull(bucket);

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_first_bucket_null)
{
    ck_assert_ptr_null(wingo_dht_routing_first_bucket(NULL));
}
END_TEST

START_TEST(test_routing_next_bucket)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* NOTE: next_bucket() is not fully implemented (returns NULL).
     *       This test documents the current behavior. */
    ck_assert_ptr_null(wingo_dht_routing_next_bucket(NULL));

    wingo_dht_routing_free(rt);
}
END_TEST

START_TEST(test_routing_prev_bucket)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* NOTE: prev_bucket() is not fully implemented (returns NULL). */
    ck_assert_ptr_null(wingo_dht_routing_prev_bucket(NULL));

    wingo_dht_routing_free(rt);
}
END_TEST

/* ============================================================================
 * TEST: ROUTING TABLE — PRINT
 * ============================================================================ */

START_TEST(test_routing_print)
{
    wingo_dht_id_t my_id;
    wingo_dht_routing_t *rt;

    make_id(&my_id, 0x00);
    rt = wingo_dht_routing_new(&my_id, WINGO_ADDR_IPV4, 8);

    /* Should not crash */
    wingo_dht_routing_print(rt, stdout);
    wingo_dht_routing_print(rt, NULL);
    wingo_dht_routing_print(NULL, stdout);

    wingo_dht_routing_free(rt);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *routing_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_add;
    TCase *tc_find;
    TCase *tc_remove;
    TCase *tc_closest;
    TCase *tc_random;
    TCase *tc_split;
    TCase *tc_maintenance;
    TCase *tc_stats;
    TCase *tc_iteration;
    TCase *tc_print;

    s = suite_create("DHT Routing");

    /* Creation */
    tc_create = tcase_create("Create");
    tcase_add_test(tc_create, test_routing_new);
    tcase_add_test(tc_create, test_routing_new_null_id);
    tcase_add_test(tc_create, test_routing_new_zero_bucket_size);
    tcase_add_test(tc_create, test_routing_free_null);
    suite_add_tcase(s, tc_create);

    /* Find bucket */
    tc_find = tcase_create("FindBucket");
    tcase_add_test(tc_find, test_routing_find_bucket);
    suite_add_tcase(s, tc_find);

    /* Add node */
    tc_add = tcase_create("AddNode");
    tcase_add_test(tc_add, test_routing_add_node);
    tcase_add_test(tc_add, test_routing_add_node_self);
    tcase_add_test(tc_add, test_routing_add_node_null);
    tcase_add_test(tc_add, test_routing_add_multiple);
    tcase_add_test(tc_add, test_routing_add_duplicate);
    suite_add_tcase(s, tc_add);

    /* Find node */
    tc_find = tcase_create("FindNode");
    tcase_add_test(tc_find, test_routing_find_node);
    suite_add_tcase(s, tc_find);

    /* Remove node */
    tc_remove = tcase_create("RemoveNode");
    tcase_add_test(tc_remove, test_routing_remove_node);
    tcase_add_test(tc_remove, test_routing_remove_missing);
    tcase_add_test(tc_remove, test_routing_remove_node_null);
    suite_add_tcase(s, tc_remove);

    /* Closest nodes */
    tc_closest = tcase_create("Closest");
    tcase_add_test(tc_closest, test_routing_closest_nodes);
    tcase_add_test(tc_closest, test_routing_closest_nodes_empty);
    tcase_add_test(tc_closest, test_routing_closest_nodes_null);
    tcase_add_test(tc_closest, test_routing_closest_nodes_limit);
    suite_add_tcase(s, tc_closest);

    /* Random nodes */
    tc_random = tcase_create("Random");
    tcase_add_test(tc_random, test_routing_random_nodes);
    tcase_add_test(tc_random, test_routing_random_nodes_empty);
    tcase_add_test(tc_random, test_routing_random_nodes_null);
    suite_add_tcase(s, tc_random);

    /* Split */
    tc_split = tcase_create("Split");
    tcase_add_test(tc_split, test_routing_split_bucket);
    tcase_add_test(tc_split, test_routing_split_bucket_null);
    suite_add_tcase(s, tc_split);

    /* Maintenance */
    tc_maintenance = tcase_create("Maintenance");
    tcase_add_test(tc_maintenance, test_routing_expire);
    tcase_add_test(tc_maintenance, test_routing_expire_null);
    tcase_add_test(tc_maintenance, test_routing_maintenance);
    tcase_add_test(tc_maintenance, test_routing_maintenance_null);
    tcase_add_test(tc_maintenance, test_routing_neighbourhood);
    tcase_add_test(tc_maintenance, test_routing_neighbourhood_null);
    suite_add_tcase(s, tc_maintenance);

    /* Stats */
    tc_stats = tcase_create("Stats");
    tcase_add_test(tc_stats, test_routing_bucket_count);
    tcase_add_test(tc_stats, test_routing_bucket_count_null);
    tcase_add_test(tc_stats, test_routing_node_count);
    tcase_add_test(tc_stats, test_routing_node_count_null);
    tcase_add_test(tc_stats, test_routing_good_count);
    tcase_add_test(tc_stats, test_routing_good_count_null);
    tcase_add_test(tc_stats, test_routing_dubious_count);
    tcase_add_test(tc_stats, test_routing_dubious_count_null);
    tcase_add_test(tc_stats, test_routing_bad_count);
    tcase_add_test(tc_stats, test_routing_bad_count_null);
    suite_add_tcase(s, tc_stats);

    /* Iteration */
    tc_iteration = tcase_create("Iteration");
    tcase_add_test(tc_iteration, test_routing_first_bucket);
    tcase_add_test(tc_iteration, test_routing_first_bucket_null);
    tcase_add_test(tc_iteration, test_routing_next_bucket);
    tcase_add_test(tc_iteration, test_routing_prev_bucket);
    suite_add_tcase(s, tc_iteration);

    /* Print */
    tc_print = tcase_create("Print");
    tcase_add_test(tc_print, test_routing_print);
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

    s = routing_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
