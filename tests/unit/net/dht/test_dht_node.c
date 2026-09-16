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
 * Unit tests for wingo/net/dht/dht_node.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/net/dht/dht_node.h"
#include "wingo/net/dht/dht_types.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Create a test ID with a specific first byte.
 */
static void make_id(wingo_dht_id_t *id, wingo_u8 first_byte)
{
    memset(id, 0, sizeof(wingo_dht_id_t));
    id->bytes[0] = first_byte;
}

/*
 * Create a test address.
 */
static wingo_addr_t *make_addr(const char *ip, wingo_u16 port)
{
    return wingo_addr_new_ipv4(ip, port);
}

/* ============================================================================
 * TEST: NODE CREATION
 * ============================================================================ */

START_TEST(test_node_new_valid)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    addr = make_addr("10.0.0.1", 6881);
    ck_assert_ptr_nonnull(addr);

    node = wingo_dht_node_new(&id, addr);

    ck_assert_ptr_nonnull(node);
    ck_assert_int_eq(wingo_dht_node_state(node), WINGO_DHT_NODE_STATE_UNKNOWN);
    ck_assert(wingo_dht_node_has_id(node, &id));
    ck_assert(wingo_dht_node_has_addr(node, addr));

    wingo_dht_node_free(node);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_new_null_id)
{
    wingo_addr_t *addr = make_addr("10.0.0.1", 6881);
    wingo_dht_node_t *node;

    node = wingo_dht_node_new(NULL, addr);

    ck_assert_ptr_null(node);

    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_new_null_addr)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);

    node = wingo_dht_node_new(&id, NULL);

    /* We allow NULL address (node can be created without addr) */
    ck_assert_ptr_nonnull(node);
    ck_assert(wingo_dht_node_addr(node) == NULL);

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_free_null)
{
    /* Should not crash */
    wingo_dht_node_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: NODE CLONE
 * ============================================================================ */

START_TEST(test_node_clone)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;
    wingo_dht_node_t *clone;

    make_id(&id, 0x42);
    addr = make_addr("10.0.0.1", 6881);

    node = wingo_dht_node_new(&id, addr);
    ck_assert_ptr_nonnull(node);

    wingo_dht_node_replied(node);  /* Set state to GOOD */

    clone = wingo_dht_node_clone(node);

    ck_assert_ptr_nonnull(clone);
    ck_assert(wingo_dht_node_has_id(clone, &id));
    ck_assert(wingo_dht_node_is_good(clone));

    /* Addresses should be different pointers */
    ck_assert_ptr_ne(wingo_dht_node_addr(clone), addr);

    wingo_dht_node_free(node);
    wingo_dht_node_free(clone);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_clone_null)
{
    ck_assert_ptr_null(wingo_dht_node_clone(NULL));
}
END_TEST

/* ============================================================================
 * TEST: NODE INFO
 * ============================================================================ */

START_TEST(test_node_id)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    ck_assert_ptr_nonnull(wingo_dht_node_id(node));
    ck_assert_mem_eq(wingo_dht_node_id(node)->bytes, id.bytes,
                     WINGO_DHT_ID_SIZE);

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_id_null)
{
    ck_assert_ptr_null(wingo_dht_node_id(NULL));
}
END_TEST

START_TEST(test_node_addr)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    addr = make_addr("10.0.0.1", 6881);

    node = wingo_dht_node_new(&id, addr);

    ck_assert_ptr_nonnull(wingo_dht_node_addr(node));
    ck_assert_int_eq(wingo_addr_cmp(wingo_dht_node_addr(node), addr), 0);

    wingo_dht_node_free(node);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_addr_null)
{
    ck_assert_ptr_null(wingo_dht_node_addr(NULL));
}
END_TEST

START_TEST(test_node_set_addr)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr1;
    wingo_addr_t *addr2;
    wingo_dht_node_t *node;
    wingo_error_t rc;

    make_id(&id, 0x42);
    addr1 = make_addr("10.0.0.1", 6881);
    addr2 = make_addr("10.0.0.2", 6882);

    node = wingo_dht_node_new(&id, addr1);

    rc = wingo_dht_node_set_addr(node, addr2);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(wingo_addr_cmp(wingo_dht_node_addr(node), addr2), 0);

    wingo_dht_node_free(node);
    wingo_addr_free(addr1);
    wingo_addr_free(addr2);
}
END_TEST

START_TEST(test_node_family)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    addr = make_addr("10.0.0.1", 6881);

    node = wingo_dht_node_new(&id, addr);

    ck_assert_int_eq(wingo_dht_node_family(node), WINGO_ADDR_IPV4);

    wingo_dht_node_free(node);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_family_null)
{
    ck_assert_int_eq(wingo_dht_node_family(NULL), WINGO_ADDR_UNSPEC);
}
END_TEST

/* ============================================================================
 * TEST: NODE STATE
 * ============================================================================ */

START_TEST(test_node_state_initial)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    ck_assert_int_eq(wingo_dht_node_state(node), WINGO_DHT_NODE_STATE_UNKNOWN);
    ck_assert(wingo_dht_node_is_unknown(node));
    ck_assert(!wingo_dht_node_is_good(node));
    ck_assert(!wingo_dht_node_is_dubious(node));
    ck_assert(!wingo_dht_node_is_bad(node));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_set_state)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    wingo_dht_node_set_state(node, WINGO_DHT_NODE_STATE_GOOD);
    ck_assert(wingo_dht_node_is_good(node));

    wingo_dht_node_set_state(node, WINGO_DHT_NODE_STATE_DUBIOUS);
    ck_assert(wingo_dht_node_is_dubious(node));

    wingo_dht_node_set_state(node, WINGO_DHT_NODE_STATE_BAD);
    ck_assert(wingo_dht_node_is_bad(node));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_state_name)
{
    ck_assert_str_eq(wingo_dht_node_state_name(WINGO_DHT_NODE_STATE_UNKNOWN),
                     "UNKNOWN");
    ck_assert_str_eq(wingo_dht_node_state_name(WINGO_DHT_NODE_STATE_GOOD),
                     "GOOD");
    ck_assert_str_eq(wingo_dht_node_state_name(WINGO_DHT_NODE_STATE_DUBIOUS),
                     "DUBIOUS");
    ck_assert_str_eq(wingo_dht_node_state_name(WINGO_DHT_NODE_STATE_BAD),
                     "BAD");
}
END_TEST

/* ============================================================================
 * TEST: NODE TIMESTAMPS
 * ============================================================================ */

START_TEST(test_node_last_seen)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;
    wingo_i64 t1, t2;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    t1 = wingo_dht_node_last_seen(node);
    ck_assert(t1 > 0);

    wingo_thread_sleep_ms(50);
    wingo_dht_node_update_seen(node);

    t2 = wingo_dht_node_last_seen(node);
    ck_assert(t2 >= t1);

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_last_reply)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    /* Initially 0 */
    ck_assert_int_eq(wingo_dht_node_last_reply(node), 0);

    wingo_dht_node_update_reply(node);
    ck_assert(wingo_dht_node_last_reply(node) > 0);

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_ping_count)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    ck_assert_int_eq(wingo_dht_node_ping_count(node), 0);

    wingo_dht_node_increment_ping(node);
    ck_assert_int_eq(wingo_dht_node_ping_count(node), 1);

    wingo_dht_node_increment_ping(node);
    ck_assert_int_eq(wingo_dht_node_ping_count(node), 2);

    wingo_dht_node_reset_ping(node);
    ck_assert_int_eq(wingo_dht_node_ping_count(node), 0);

    wingo_dht_node_free(node);
}
END_TEST

/* ============================================================================
 * TEST: NODE PING/REPLY
 * ============================================================================ */

START_TEST(test_node_pinged_transitions_to_dubious)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    /* Ping → DUBIOUS */
    wingo_dht_node_pinged(node);
    ck_assert(wingo_dht_node_is_dubious(node));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_pinged_max_transitions_to_bad)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;
    int i;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    /* Ping MAX_PING times → BAD */
    for (i = 0; i < WINGO_DHT_NODE_MAX_PING; i++) {
        wingo_dht_node_pinged(node);
    }

    ck_assert(wingo_dht_node_is_bad(node));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_replied_transitions_to_good)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    wingo_dht_node_pinged(node);
    ck_assert(wingo_dht_node_is_dubious(node));

    wingo_dht_node_replied(node);
    ck_assert(wingo_dht_node_is_good(node));
    ck_assert_int_eq(wingo_dht_node_ping_count(node), 0);

    wingo_dht_node_free(node);
}
END_TEST

/* ============================================================================
 * TEST: NODE TIMEOUT
 * ============================================================================ */

START_TEST(test_node_is_timed_out)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    /* Not timed out with 0 timeout */
    ck_assert(!wingo_dht_node_is_timed_out(node, 0));

    /* Not timed out with large timeout */
    ck_assert(!wingo_dht_node_is_timed_out(node, 3600));

    /* Timed out with negative-ish timeout */
    /* (since we just created it, last_seen = now) */
    ck_assert(!wingo_dht_node_is_timed_out(node, 1));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_is_timed_out_null)
{
    ck_assert(wingo_dht_node_is_timed_out(NULL, 100));
}
END_TEST

/* ============================================================================
 * TEST: NODE STATISTICS
 * ============================================================================ */

START_TEST(test_node_get_stats)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *node;
    wingo_dht_node_stats_t stats;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    wingo_dht_node_replied(node);

    ck_assert_int_eq(wingo_dht_node_get_stats(node, &stats), WINGO_SUCCESS);
    ck_assert(stats.is_good);
    ck_assert(!stats.is_dubious);
    ck_assert(!stats.is_bad);
    ck_assert_int_eq(stats.ping_count, 0);

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_get_stats_null)
{
    wingo_dht_node_stats_t stats;
    ck_assert_int_eq(wingo_dht_node_get_stats(NULL, &stats),
                     WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: NODE COMPARISON
 * ============================================================================ */

START_TEST(test_node_cmp_equal)
{
    wingo_dht_id_t id;
    wingo_dht_node_t *a;
    wingo_dht_node_t *b;

    make_id(&id, 0x42);
    a = wingo_dht_node_new(&id, NULL);
    b = wingo_dht_node_new(&id, NULL);

    ck_assert_int_eq(wingo_dht_node_cmp(a, b), 0);

    wingo_dht_node_free(a);
    wingo_dht_node_free(b);
}
END_TEST

START_TEST(test_node_cmp_different)
{
    wingo_dht_id_t id1, id2;
    wingo_dht_node_t *a;
    wingo_dht_node_t *b;

    make_id(&id1, 0x42);
    make_id(&id2, 0x99);
    a = wingo_dht_node_new(&id1, NULL);
    b = wingo_dht_node_new(&id2, NULL);

    ck_assert_int_ne(wingo_dht_node_cmp(a, b), 0);

    wingo_dht_node_free(a);
    wingo_dht_node_free(b);
}
END_TEST

START_TEST(test_node_has_id)
{
    wingo_dht_id_t id1, id2;
    wingo_dht_node_t *node;

    make_id(&id1, 0x42);
    make_id(&id2, 0x99);

    node = wingo_dht_node_new(&id1, NULL);

    ck_assert(wingo_dht_node_has_id(node, &id1));
    ck_assert(!wingo_dht_node_has_id(node, &id2));
    ck_assert(!wingo_dht_node_has_id(node, NULL));
    ck_assert(!wingo_dht_node_has_id(NULL, &id1));

    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_has_addr)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr1;
    wingo_addr_t *addr2;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    addr1 = make_addr("10.0.0.1", 6881);
    addr2 = make_addr("10.0.0.2", 6882);

    node = wingo_dht_node_new(&id, addr1);

    ck_assert(wingo_dht_node_has_addr(node, addr1));
    ck_assert(!wingo_dht_node_has_addr(node, addr2));

    wingo_dht_node_free(node);
    wingo_addr_free(addr1);
    wingo_addr_free(addr2);
}
END_TEST
/* ============================================================================
 * TEST: NODE LIST
 * ============================================================================ */

START_TEST(test_node_list_new)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(8);

    ck_assert_ptr_nonnull(list);
    ck_assert_uint_eq(wingo_dht_node_list_count(list), 0);

    wingo_dht_node_list_free(list);
}
END_TEST

START_TEST(test_node_list_new_zero_capacity)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(0);

    ck_assert_ptr_nonnull(list);

    wingo_dht_node_list_free(list);
}
END_TEST

START_TEST(test_node_list_free_null)
{
    /* Should not crash */
    wingo_dht_node_list_free(NULL);
}
END_TEST

START_TEST(test_node_list_add)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);

    ck_assert_int_eq(wingo_dht_node_list_add(list, node), WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_node_list_count(list), 1);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 0), node);

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(node);
}
END_TEST

START_TEST(test_node_list_add_multiple)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id1, id2, id3;
    wingo_dht_node_t *n1, *n2, *n3;

    make_id(&id1, 0x01);
    make_id(&id2, 0x02);
    make_id(&id3, 0x03);

    n1 = wingo_dht_node_new(&id1, NULL);
    n2 = wingo_dht_node_new(&id2, NULL);
    n3 = wingo_dht_node_new(&id3, NULL);

    wingo_dht_node_list_add(list, n1);
    wingo_dht_node_list_add(list, n2);
    wingo_dht_node_list_add(list, n3);

    ck_assert_uint_eq(wingo_dht_node_list_count(list), 3);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 0), n1);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 1), n2);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 2), n3);

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(n1);
    wingo_dht_node_free(n2);
    wingo_dht_node_free(n3);
}
END_TEST

START_TEST(test_node_list_add_null)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);

    ck_assert_int_eq(wingo_dht_node_list_add(NULL, NULL),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_node_list_add(list, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_node_list_free(list);
}
END_TEST

START_TEST(test_node_list_get_out_of_range)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);

    ck_assert_ptr_null(wingo_dht_node_list_get(list, 0));
    ck_assert_ptr_null(wingo_dht_node_list_get(list, 100));

    wingo_dht_node_list_free(list);
}
END_TEST

START_TEST(test_node_list_find)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id1, id2, missing;
    wingo_dht_node_t *n1, *n2;

    make_id(&id1, 0x42);
    make_id(&id2, 0x99);
    make_id(&missing, 0xAB);

    n1 = wingo_dht_node_new(&id1, NULL);
    n2 = wingo_dht_node_new(&id2, NULL);

    wingo_dht_node_list_add(list, n1);
    wingo_dht_node_list_add(list, n2);

    ck_assert_ptr_eq(wingo_dht_node_list_find(list, &id1), n1);
    ck_assert_ptr_eq(wingo_dht_node_list_find(list, &id2), n2);
    ck_assert_ptr_null(wingo_dht_node_list_find(list, &missing));
    ck_assert_ptr_null(wingo_dht_node_list_find(list, NULL));

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(n1);
    wingo_dht_node_free(n2);
}
END_TEST

START_TEST(test_node_list_remove)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id1, id2, id3;
    wingo_dht_node_t *n1, *n2, *n3;

    make_id(&id1, 0x01);
    make_id(&id2, 0x02);
    make_id(&id3, 0x03);

    n1 = wingo_dht_node_new(&id1, NULL);
    n2 = wingo_dht_node_new(&id2, NULL);
    n3 = wingo_dht_node_new(&id3, NULL);

    wingo_dht_node_list_add(list, n1);
    wingo_dht_node_list_add(list, n2);
    wingo_dht_node_list_add(list, n3);

    /* Remove middle */
    ck_assert_int_eq(wingo_dht_node_list_remove(list, &id2), WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_dht_node_list_count(list), 2);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 0), n1);
    ck_assert_ptr_eq(wingo_dht_node_list_get(list, 1), n3);

    /* Remove missing */
    ck_assert_int_eq(wingo_dht_node_list_remove(list, &id2),
                     WINGO_ERR_NOT_FOUND);

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(n1);
    wingo_dht_node_free(n2);
    wingo_dht_node_free(n3);
}
END_TEST

START_TEST(test_node_list_clear)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id;
    wingo_dht_node_t *n1, *n2;

    make_id(&id, 0x42);

    n1 = wingo_dht_node_new(&id, NULL);
    n2 = wingo_dht_node_new(&id, NULL);

    wingo_dht_node_list_add(list, n1);
    wingo_dht_node_list_add(list, n2);

    wingo_dht_node_list_clear(list);

    ck_assert_uint_eq(wingo_dht_node_list_count(list), 0);

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(n1);
    wingo_dht_node_free(n2);
}
END_TEST

/* ============================================================================
 * TEST: NODE LIST SORT
 * ============================================================================ */

START_TEST(test_node_list_sort)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(8);
    wingo_dht_id_t target;
    wingo_dht_id_t ids[4];
    wingo_dht_node_t *nodes[4];
    wingo_dht_node_t *closest;
    int i;

    /*
     * Target: 0x00...
     *
     * Distances:
     *   0x01 → distance 0x01 (closest)
     *   0x10 → distance 0x10
     *   0x40 → distance 0x40
     *   0x80 → distance 0x80 (farthest)
     */
    make_id(&target, 0x00);
    make_id(&ids[0], 0x80);
    make_id(&ids[1], 0x10);
    make_id(&ids[2], 0x40);
    make_id(&ids[3], 0x01);

    for (i = 0; i < 4; i++) {
        nodes[i] = wingo_dht_node_new(&ids[i], NULL);
        wingo_dht_node_list_add(list, nodes[i]);
    }

    ck_assert_int_eq(wingo_dht_node_list_sort(list, &target), WINGO_SUCCESS);

    /* After sort, closest should be first */
    closest = wingo_dht_node_list_get(list, 0);
    ck_assert_ptr_nonnull(closest);
    ck_assert(wingo_dht_node_has_id(closest, &ids[3]));  /* 0x01 */

    /* Farthest should be last */
    closest = wingo_dht_node_list_get(list, 3);
    ck_assert(wingo_dht_node_has_id(closest, &ids[0]));  /* 0x80 */

    wingo_dht_node_list_free(list);
    for (i = 0; i < 4; i++) {
        wingo_dht_node_free(nodes[i]);
    }
}
END_TEST

START_TEST(test_node_list_sort_empty)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t target;

    make_id(&target, 0x00);

    ck_assert_int_eq(wingo_dht_node_list_sort(list, &target), WINGO_SUCCESS);

    wingo_dht_node_list_free(list);
}
END_TEST

START_TEST(test_node_list_sort_null)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t target;

    make_id(&target, 0x00);

    ck_assert_int_eq(wingo_dht_node_list_sort(NULL, &target),
                     WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_dht_node_list_sort(list, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_dht_node_list_free(list);
}
END_TEST

/* ============================================================================
 * TEST: NODE LIST ITERATION
 * ============================================================================ */

static int g_foreach_count;
static bool g_foreach_stop_early;

static bool test_foreach_cb(wingo_dht_node_t *node, void *userdata)
{
    (void)node;
    (void)userdata;

    g_foreach_count++;

    if (g_foreach_stop_early && g_foreach_count >= 2) {
        return false;
    }

    return true;
}

START_TEST(test_node_list_foreach)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(8);
    wingo_dht_id_t ids[5];
    wingo_dht_node_t *nodes[5];
    int i;

    for (i = 0; i < 5; i++) {
        make_id(&ids[i], (wingo_u8)(i + 1));
        nodes[i] = wingo_dht_node_new(&ids[i], NULL);
        wingo_dht_node_list_add(list, nodes[i]);
    }

    g_foreach_count = 0;
    g_foreach_stop_early = false;

    wingo_dht_node_list_foreach(list, test_foreach_cb, NULL);

    ck_assert_int_eq(g_foreach_count, 5);

    wingo_dht_node_list_free(list);
    for (i = 0; i < 5; i++) {
        wingo_dht_node_free(nodes[i]);
    }
}
END_TEST

START_TEST(test_node_list_foreach_stop)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(8);
    wingo_dht_id_t ids[5];
    wingo_dht_node_t *nodes[5];
    int i;

    for (i = 0; i < 5; i++) {
        make_id(&ids[i], (wingo_u8)(i + 1));
        nodes[i] = wingo_dht_node_new(&ids[i], NULL);
        wingo_dht_node_list_add(list, nodes[i]);
    }

    g_foreach_count = 0;
    g_foreach_stop_early = true;

    wingo_dht_node_list_foreach(list, test_foreach_cb, NULL);

    ck_assert_int_eq(g_foreach_count, 2);

    wingo_dht_node_list_free(list);
    for (i = 0; i < 5; i++) {
        wingo_dht_node_free(nodes[i]);
    }
}
END_TEST

/* ============================================================================
 * TEST: NODE UTILITY
 * ============================================================================ */

START_TEST(test_node_print)
{
    wingo_dht_id_t id;
    wingo_addr_t *addr;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    addr = make_addr("10.0.0.1", 6881);
    node = wingo_dht_node_new(&id, addr);

    /* Should not crash */
    wingo_dht_node_print(node, stdout);
    wingo_dht_node_print(node, NULL);
    wingo_dht_node_print(NULL, stdout);

    wingo_dht_node_free(node);
    wingo_addr_free(addr);
}
END_TEST

START_TEST(test_node_list_print)
{
    wingo_dht_node_list_t *list = wingo_dht_node_list_new(4);
    wingo_dht_id_t id;
    wingo_dht_node_t *node;

    make_id(&id, 0x42);
    node = wingo_dht_node_new(&id, NULL);
    wingo_dht_node_list_add(list, node);

    /* Should not crash */
    wingo_dht_node_list_print(list, stdout);
    wingo_dht_node_list_print(list, NULL);
    wingo_dht_node_list_print(NULL, stdout);

    wingo_dht_node_list_free(list);
    wingo_dht_node_free(node);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *node_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_info;
    TCase *tc_state;
    TCase *tc_time;
    TCase *tc_cmp;
    TCase *tc_list;
    TCase *tc_sort;
    TCase *tc_iter;
    TCase *tc_print;

    s = suite_create("DHT Node");

    /* Creation tests */
    tc_create = tcase_create("Create");
    tcase_add_test(tc_create, test_node_new_valid);
    tcase_add_test(tc_create, test_node_new_null_id);
    tcase_add_test(tc_create, test_node_new_null_addr);
    tcase_add_test(tc_create, test_node_free_null);
    tcase_add_test(tc_create, test_node_clone);
    tcase_add_test(tc_create, test_node_clone_null);
    suite_add_tcase(s, tc_create);

    /* Info tests */
    tc_info = tcase_create("Info");
    tcase_add_test(tc_info, test_node_id);
    tcase_add_test(tc_info, test_node_id_null);
    tcase_add_test(tc_info, test_node_addr);
    tcase_add_test(tc_info, test_node_addr_null);
    tcase_add_test(tc_info, test_node_set_addr);
    tcase_add_test(tc_info, test_node_family);
    tcase_add_test(tc_info, test_node_family_null);
    suite_add_tcase(s, tc_info);

    /* State tests */
    tc_state = tcase_create("State");
    tcase_add_test(tc_state, test_node_state_initial);
    tcase_add_test(tc_state, test_node_set_state);
    tcase_add_test(tc_state, test_node_state_name);
    tcase_add_test(tc_state, test_node_pinged_transitions_to_dubious);
    tcase_add_test(tc_state, test_node_pinged_max_transitions_to_bad);
    tcase_add_test(tc_state, test_node_replied_transitions_to_good);
    suite_add_tcase(s, tc_state);

    /* Time tests */
    tc_time = tcase_create("Time");
    tcase_add_test(tc_time, test_node_last_seen);
    tcase_add_test(tc_time, test_node_last_reply);
    tcase_add_test(tc_time, test_node_ping_count);
    tcase_add_test(tc_time, test_node_is_timed_out);
    tcase_add_test(tc_time, test_node_is_timed_out_null);
    tcase_add_test(tc_time, test_node_get_stats);
    tcase_add_test(tc_time, test_node_get_stats_null);
    suite_add_tcase(s, tc_time);

    /* Comparison tests */
    tc_cmp = tcase_create("Compare");
    tcase_add_test(tc_cmp, test_node_cmp_equal);
    tcase_add_test(tc_cmp, test_node_cmp_different);
    tcase_add_test(tc_cmp, test_node_has_id);
    tcase_add_test(tc_cmp, test_node_has_addr);
    suite_add_tcase(s, tc_cmp);

    /* List tests */
    tc_list = tcase_create("List");
    tcase_add_test(tc_list, test_node_list_new);
    tcase_add_test(tc_list, test_node_list_new_zero_capacity);
    tcase_add_test(tc_list, test_node_list_free_null);
    tcase_add_test(tc_list, test_node_list_add);
    tcase_add_test(tc_list, test_node_list_add_multiple);
    tcase_add_test(tc_list, test_node_list_add_null);
    tcase_add_test(tc_list, test_node_list_get_out_of_range);
    tcase_add_test(tc_list, test_node_list_find);
    tcase_add_test(tc_list, test_node_list_remove);
    tcase_add_test(tc_list, test_node_list_clear);
    suite_add_tcase(s, tc_list);

    /* Sort tests */
    tc_sort = tcase_create("Sort");
    tcase_add_test(tc_sort, test_node_list_sort);
    tcase_add_test(tc_sort, test_node_list_sort_empty);
    tcase_add_test(tc_sort, test_node_list_sort_null);
    suite_add_tcase(s, tc_sort);

    /* Iteration tests */
    tc_iter = tcase_create("Iterate");
    tcase_add_test(tc_iter, test_node_list_foreach);
    tcase_add_test(tc_iter, test_node_list_foreach_stop);
    suite_add_tcase(s, tc_iter);

    /* Print tests */
    tc_print = tcase_create("Print");
    tcase_add_test(tc_print, test_node_print);
    tcase_add_test(tc_print, test_node_list_print);
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

    s = node_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
