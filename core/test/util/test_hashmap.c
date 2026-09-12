
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
 * Unit tests for wingo/util/hashmap.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wingo/util/hashmap.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Track free calls for memory ownership tests.
 */
static int free_key_count = 0;
static int free_value_count = 0;

static void test_free_key(void *key)
{
    free_key_count++;
    free(key);
}

static void test_free_value(void *value)
{
    free_value_count++;
    free(value);
}

/*
 * Reset counters.
 */
static void reset_counters(void)
{
    free_key_count = 0;
    free_value_count = 0;
}

/*
 * Helper: create a heap-allocated string.
 */
static char *make_str(const char *s)
{
    char *result = malloc(strlen(s) + 1);
    if (result != NULL) {
        strcpy(result, s);
    }
    return result;
}

/*
 * Helper: create heap-allocated integer.
 */
static int *make_int(int value)
{
    int *result = malloc(sizeof(int));
    if (result != NULL) {
        *result = value;
    }
    return result;
}

/* ============================================================================
 * TEST: HASH FUNCTIONS
 * ============================================================================ */

START_TEST(test_hash_str_basic)
{
    wingo_u32 h1 = wingo_hash_str("hello");
    wingo_u32 h2 = wingo_hash_str("hello");
    wingo_u32 h3 = wingo_hash_str("world");

    /* Same input = same output */
    ck_assert_uint_eq(h1, h2);

    /* Different input = different output (usually) */
    ck_assert_uint_ne(h1, h3);
}
END_TEST

START_TEST(test_hash_str_empty)
{
    wingo_u32 h = wingo_hash_str("");
    ck_assert_uint_eq(h, 5381);  /* DJB2 initial value */
}
END_TEST

START_TEST(test_hash_str_null)
{
    wingo_u32 h = wingo_hash_str(NULL);
    ck_assert_uint_eq(h, 0);
}
END_TEST

START_TEST(test_hash_data_basic)
{
    const char *data = "hello";
    wingo_u32 h1 = wingo_hash_data(data, 5);
    wingo_u32 h2 = wingo_hash_data(data, 5);
    wingo_u32 h3 = wingo_hash_data("world", 5);

    ck_assert_uint_eq(h1, h2);
    ck_assert_uint_ne(h1, h3);
}
END_TEST

START_TEST(test_hash_data_empty)
{
    wingo_u32 h = wingo_hash_data("", 0);
    ck_assert_uint_eq(h, 2166136261U);  /* FNV offset basis */
}
END_TEST

START_TEST(test_hash_u32)
{
    wingo_u32 h1 = wingo_hash_u32(42);
    wingo_u32 h2 = wingo_hash_u32(42);
    wingo_u32 h3 = wingo_hash_u32(99);

    ck_assert_uint_eq(h1, h2);
    ck_assert_uint_ne(h1, h3);
}
END_TEST

START_TEST(test_hash_u64)
{
    wingo_u64 h1 = wingo_hash_u64(1234567890123ULL);
    wingo_u64 h2 = wingo_hash_u64(1234567890123ULL);

    ck_assert_uint_eq(h1, h2);
}
END_TEST

/* ============================================================================
 * TEST: STRING HASH MAP CREATION
 * ============================================================================ */

START_TEST(test_hashmap_new_valid)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);

    ck_assert_ptr_nonnull(map);
    ck_assert_uint_eq(map->count, 0);
    ck_assert_uint_ge(map->capacity, 16);
    ck_assert_ptr_nonnull(map->buckets);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_new_zero_capacity)
{
    wingo_hashmap_t *map = wingo_hashmap_new(0, NULL, NULL);

    ck_assert_ptr_nonnull(map);
    ck_assert_uint_gt(map->capacity, 0);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_new_non_power_of_two)
{
    /* Capacity 20 should round up to 32 */
    wingo_hashmap_t *map = wingo_hashmap_new(20, NULL, NULL);

    ck_assert_ptr_nonnull(map);
    ck_assert_uint_eq(map->capacity, 32);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_free_null)
{
    /* Should not crash */
    wingo_hashmap_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: STRING HASH MAP SET/GET
 * ============================================================================ */

START_TEST(test_hashmap_set_get)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    wingo_error_t rc;
    int value = 42;

    rc = wingo_hashmap_set(map, "key", &value);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(map->count, 1);

    void *result = wingo_hashmap_get(map, "key");
    ck_assert_ptr_eq(result, &value);
    ck_assert_int_eq(*(int *)result, 42);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_get_missing)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);

    void *result = wingo_hashmap_get(map, "nonexistent");
    ck_assert_ptr_null(result);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_set_replace)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    int value1 = 42;
    int value2 = 99;

    wingo_hashmap_set(map, "key", &value1);
    ck_assert_uint_eq(map->count, 1);

    /* Set same key with different value */
    wingo_hashmap_set(map, "key", &value2);
    ck_assert_uint_eq(map->count, 1);  /* Still 1 */

    void *result = wingo_hashmap_get(map, "key");
    ck_assert_ptr_eq(result, &value2);
    ck_assert_int_eq(*(int *)result, 99);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_has)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    int value = 42;

    ck_assert(!wingo_hashmap_has(map, "key"));

    wingo_hashmap_set(map, "key", &value);
    ck_assert(wingo_hashmap_has(map, "key"));

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_many_keys)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    char key[32];
    int i;

    /* Insert 100 keys */
    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_set(map, key, make_int(i));
    }

    ck_assert_uint_eq(map->count, 100);

    /* Verify all keys */
    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *value = wingo_hashmap_get(map, key);
        ck_assert_ptr_nonnull(value);
        ck_assert_int_eq(*(int *)value, i);
    }

    /* Free values manually (no free_fn) */
    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *value = wingo_hashmap_get(map, key);
        free(value);
    }

    wingo_hashmap_free(map);
}
END_TEST

/* ============================================================================
 * TEST: STRING HASH MAP REMOVE
 * ============================================================================ */

START_TEST(test_hashmap_remove)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    int value = 42;

    wingo_hashmap_set(map, "key", &value);
    ck_assert_uint_eq(map->count, 1);

    void *removed = wingo_hashmap_remove(map, "key");
    ck_assert_ptr_eq(removed, &value);
    ck_assert_uint_eq(map->count, 0);
    ck_assert(!wingo_hashmap_has(map, "key"));

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_remove_missing)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);

    void *removed = wingo_hashmap_remove(map, "nonexistent");
    ck_assert_ptr_null(removed);
    ck_assert_uint_eq(map->count, 0);

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_clear)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);
    char key[32];
    int i;

    for (i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_set(map, key, make_int(i));
    }

    ck_assert_uint_eq(map->count, 50);

    /* Clear (but values leak since no free_fn) */
    wingo_hashmap_clear(map);
    ck_assert_uint_eq(map->count, 0);

    wingo_hashmap_free(map);
}
END_TEST

/* ============================================================================
 * TEST: MEMORY OWNERSHIP
 * ============================================================================ */

START_TEST(test_hashmap_free_fn_called)
{
    reset_counters();

    wingo_hashmap_t *map = wingo_hashmap_new(16,
                                             test_free_key,
                                             test_free_value);

    wingo_hashmap_set(map, "key1", make_int(1));
    wingo_hashmap_set(map, "key2", make_int(2));
    wingo_hashmap_set(map, "key3", make_int(3));

    ck_assert_uint_eq(map->count, 3);
    ck_assert_int_eq(free_key_count, 0);
    ck_assert_int_eq(free_value_count, 0);

    /* Free should call free_key and free_value for each entry */
    wingo_hashmap_free(map);

    ck_assert_int_eq(free_key_count, 3);
    ck_assert_int_eq(free_value_count, 3);
}
END_TEST

START_TEST(test_hashmap_replace_calls_free_value)
{
    reset_counters();

    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, test_free_value);

    wingo_hashmap_set(map, "key", make_int(1));
    ck_assert_int_eq(free_value_count, 0);

    /* Replace should call free_value on old value */
    wingo_hashmap_set(map, "key", make_int(2));
    ck_assert_int_eq(free_value_count, 1);
    ck_assert_uint_eq(map->count, 1);

    wingo_hashmap_free(map);
    ck_assert_int_eq(free_value_count, 2);
}
END_TEST

START_TEST(test_hashmap_remove_calls_free_key)
{
    reset_counters();

    wingo_hashmap_t *map = wingo_hashmap_new(16, test_free_key, NULL);

    wingo_hashmap_set(map, "key", make_int(1));
    ck_assert_int_eq(free_key_count, 0);

    /* Remove should free key but return value */
    void *value = wingo_hashmap_remove(map, "key");
    ck_assert_ptr_nonnull(value);
    ck_assert_int_eq(free_key_count, 1);

    free(value);
    wingo_hashmap_free(map);
}
END_TEST

/* ============================================================================
 * TEST: BINARY HASH MAP
 * ============================================================================ */

START_TEST(test_hashmap_bin_new)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);

    ck_assert_ptr_nonnull(map);
    ck_assert_uint_eq(map->count, 0);
    ck_assert_uint_ge(map->capacity, 16);

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_set_get)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    wingo_error_t rc;
    int value = 42;
    const char *key = "binary_key";

    rc = wingo_hashmap_bin_set(map, key, strlen(key), &value);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(map->count, 1);

    void *result = wingo_hashmap_bin_get(map, key, strlen(key));
    ck_assert_ptr_eq(result, &value);

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_binary_key)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    wingo_u8 key1[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                         10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
    wingo_u8 key2[20] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
                         10, 11, 12, 13, 14, 15, 16, 17, 18, 20};  /* different */
    int value1 = 42;
    int value2 = 99;

    wingo_hashmap_bin_set(map, key1, 20, &value1);
    wingo_hashmap_bin_set(map, key2, 20, &value2);

    ck_assert_uint_eq(map->count, 2);

    void *r1 = wingo_hashmap_bin_get(map, key1, 20);
    void *r2 = wingo_hashmap_bin_get(map, key2, 20);

    ck_assert_ptr_eq(r1, &value1);
    ck_assert_ptr_eq(r2, &value2);

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_has)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    const char *key = "key";
    int value = 42;

    ck_assert(!wingo_hashmap_bin_has(map, key, strlen(key)));

    wingo_hashmap_bin_set(map, key, strlen(key), &value);
    ck_assert(wingo_hashmap_bin_has(map, key, strlen(key)));

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_remove)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    const char *key = "key";
    int value = 42;

    wingo_hashmap_bin_set(map, key, strlen(key), &value);
    ck_assert_uint_eq(map->count, 1);

    void *removed = wingo_hashmap_bin_remove(map, key, strlen(key));
    ck_assert_ptr_eq(removed, &value);
    ck_assert_uint_eq(map->count, 0);

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_many_keys)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    char key[32];
    int i;

    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_bin_set(map, key, strlen(key), make_int(i));
    }

    ck_assert_uint_eq(map->count, 100);

    for (i = 0; i < 100; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *value = wingo_hashmap_bin_get(map, key, strlen(key));
        ck_assert_ptr_nonnull(value);
        ck_assert_int_eq(*(int *)value, i);
        free(value);
    }

    wingo_hashmap_bin_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_clear)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(16, NULL, NULL);
    char key[32];
    int i;

    for (i = 0; i < 50; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_bin_set(map, key, strlen(key), make_int(i));
    }

    ck_assert_uint_eq(map->count, 50);

    wingo_hashmap_bin_clear(map);
    ck_assert_uint_eq(map->count, 0);

    wingo_hashmap_bin_free(map);
}
END_TEST

/* ============================================================================
 * TEST: AUTO-GROW
 * ============================================================================ */

START_TEST(test_hashmap_grow)
{
    wingo_hashmap_t *map = wingo_hashmap_new(4, NULL, NULL);
    char key[32];
    int i;
    wingo_size initial_capacity = map->capacity;

    /* Insert more than load factor allows */
    for (i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_set(map, key, make_int(i));
    }

    /* Capacity should have grown */
    ck_assert_uint_gt(map->capacity, initial_capacity);
    ck_assert_uint_eq(map->count, 20);

    /* All keys should still be accessible */
    for (i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *value = wingo_hashmap_get(map, key);
        ck_assert_ptr_nonnull(value);
        ck_assert_int_eq(*(int *)value, i);
        free(value);
    }

    wingo_hashmap_free(map);
}
END_TEST

START_TEST(test_hashmap_bin_grow)
{
    wingo_hashmap_bin_t *map = wingo_hashmap_bin_new(4, NULL, NULL);
    char key[32];
    int i;
    wingo_size initial_capacity = map->capacity;

    for (i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        wingo_hashmap_bin_set(map, key, strlen(key), make_int(i));
    }

    ck_assert_uint_gt(map->capacity, initial_capacity);
    ck_assert_uint_eq(map->count, 20);

    for (i = 0; i < 20; i++) {
        snprintf(key, sizeof(key), "key_%d", i);
        void *value = wingo_hashmap_bin_get(map, key, strlen(key));
        ck_assert_ptr_nonnull(value);
        ck_assert_int_eq(*(int *)value, i);
        free(value);
    }

    wingo_hashmap_bin_free(map);
}
END_TEST

/* ============================================================================
 * TEST: NULL HANDLING
 * ============================================================================ */

START_TEST(test_hashmap_null_map)
{
    /* All these should not crash */
    ck_assert_ptr_null(wingo_hashmap_get(NULL, "key"));
    ck_assert(!wingo_hashmap_has(NULL, "key"));
    ck_assert_ptr_null(wingo_hashmap_remove(NULL, "key"));
    ck_assert_int_eq(wingo_hashmap_set(NULL, "key", NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_uint_eq(wingo_hashmap_count(NULL), 0);
}
END_TEST

START_TEST(test_hashmap_null_key)
{
    wingo_hashmap_t *map = wingo_hashmap_new(16, NULL, NULL);

    ck_assert_ptr_null(wingo_hashmap_get(map, NULL));
    ck_assert(!wingo_hashmap_has(map, NULL));
    ck_assert_ptr_null(wingo_hashmap_remove(map, NULL));
    ck_assert_int_eq(wingo_hashmap_set(map, NULL, NULL), WINGO_ERR_INVALID_ARG);

    wingo_hashmap_free(map);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *hashmap_suite(void)
{
    Suite *s;
    TCase *tc_hash;
    TCase *tc_create;
    TCase *tc_set_get;
    TCase *tc_remove;
    TCase *tc_memory;
    TCase *tc_binary;
    TCase *tc_grow;
    TCase *tc_null;

    s = suite_create("HashMap");

    /* Hash function tests */
    tc_hash = tcase_create("Hash");
    tcase_add_test(tc_hash, test_hash_str_basic);
    tcase_add_test(tc_hash, test_hash_str_empty);
    tcase_add_test(tc_hash, test_hash_str_null);
    tcase_add_test(tc_hash, test_hash_data_basic);
    tcase_add_test(tc_hash, test_hash_data_empty);
    tcase_add_test(tc_hash, test_hash_u32);
    tcase_add_test(tc_hash, test_hash_u64);
    suite_add_tcase(s, tc_hash);

    /* Creation tests */
    tc_create = tcase_create("Creation");
    tcase_add_test(tc_create, test_hashmap_new_valid);
    tcase_add_test(tc_create, test_hashmap_new_zero_capacity);
    tcase_add_test(tc_create, test_hashmap_new_non_power_of_two);
    tcase_add_test(tc_create, test_hashmap_free_null);
    tcase_add_test(tc_create, test_hashmap_bin_new);
    suite_add_tcase(s, tc_create);

    /* Set/Get tests */
    tc_set_get = tcase_create("SetGet");
    tcase_add_test(tc_set_get, test_hashmap_set_get);
    tcase_add_test(tc_set_get, test_hashmap_get_missing);
    tcase_add_test(tc_set_get, test_hashmap_set_replace);
    tcase_add_test(tc_set_get, test_hashmap_has);
    tcase_add_test(tc_set_get, test_hashmap_many_keys);
    suite_add_tcase(s, tc_set_get);

    /* Remove tests */
    tc_remove = tcase_create("Remove");
    tcase_add_test(tc_remove, test_hashmap_remove);
    tcase_add_test(tc_remove, test_hashmap_remove_missing);
    tcase_add_test(tc_remove, test_hashmap_clear);
    suite_add_tcase(s, tc_remove);

    /* Memory ownership tests */
    tc_memory = tcase_create("Memory");
    tcase_add_test(tc_memory, test_hashmap_free_fn_called);
    tcase_add_test(tc_memory, test_hashmap_replace_calls_free_value);
    tcase_add_test(tc_memory, test_hashmap_remove_calls_free_key);
    suite_add_tcase(s, tc_memory);

    /* Binary key tests */
    tc_binary = tcase_create("Binary");
    tcase_add_test(tc_binary, test_hashmap_bin_set_get);
    tcase_add_test(tc_binary, test_hashmap_bin_binary_key);
    tcase_add_test(tc_binary, test_hashmap_bin_has);
    tcase_add_test(tc_binary, test_hashmap_bin_remove);
    tcase_add_test(tc_binary, test_hashmap_bin_many_keys);
    tcase_add_test(tc_binary, test_hashmap_bin_clear);
    suite_add_tcase(s, tc_binary);

    /* Auto-grow tests */
    tc_grow = tcase_create("Grow");
    tcase_add_test(tc_grow, test_hashmap_grow);
    tcase_add_test(tc_grow, test_hashmap_bin_grow);
    suite_add_tcase(s, tc_grow);

    /* NULL handling tests */
    tc_null = tcase_create("Null");
    tcase_add_test(tc_null, test_hashmap_null_map);
    tcase_add_test(tc_null, test_hashmap_null_key);
    suite_add_tcase(s, tc_null);

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

    s = hashmap_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
