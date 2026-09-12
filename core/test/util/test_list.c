
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
 * Unit tests for wingo/util/list.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "wingo/util/list.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Track free calls for memory ownership tests.
 */
static int free_count = 0;

static void test_free_fn(void *data)
{
    free_count++;
    free(data);
}

static void reset_counters(void)
{
    free_count = 0;
}

/*
 * Helper: create a heap-allocated integer.
 */
static int *make_int(int value)
{
    int *result = malloc(sizeof(int));
    if (result != NULL) {
        *result = value;
    }
    return result;
}

/*
 * Helper: compare integers for find/sort.
 */
static int cmp_int(const void *a, const void *b)
{
    int ia = *(const int *)a;
    int ib = *(const int *)b;

    if (ia < ib) return -1;
    if (ia > ib) return 1;
    return 0;
}

/*
 * Helper: compare integers in reverse order (for sort tests).
 */
static int cmp_int_desc(const void *a, const void *b)
{
    return cmp_int(b, a);
}

/* ============================================================================
 * SINGLY-LINKED LIST TESTS
 * ============================================================================ */

START_TEST(test_slist_new)
{
    wingo_slist_t *list = wingo_slist_new(NULL);

    ck_assert_ptr_nonnull(list);
    ck_assert_ptr_null(list->head);
    ck_assert_ptr_null(list->tail);
    ck_assert_uint_eq(list->count, 0);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_free_null)
{
    /* Should not crash */
    wingo_slist_free(NULL);
}
END_TEST

START_TEST(test_slist_append)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int *a = make_int(1);
    int *b = make_int(2);
    int *c = make_int(3);

    ck_assert_int_eq(wingo_slist_append(list, a), WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 1);
    ck_assert_ptr_eq(list->head->data, a);
    ck_assert_ptr_eq(list->tail->data, a);

    ck_assert_int_eq(wingo_slist_append(list, b), WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 2);
    ck_assert_ptr_eq(list->head->data, a);
    ck_assert_ptr_eq(list->tail->data, b);

    ck_assert_int_eq(wingo_slist_append(list, c), WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(list->tail->data, c);

    wingo_slist_free(list);
    free(a); free(b); free(c);
}
END_TEST

START_TEST(test_slist_prepend)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int *a = make_int(1);
    int *b = make_int(2);
    int *c = make_int(3);

    wingo_slist_prepend(list, a);
    ck_assert_uint_eq(list->count, 1);
    ck_assert_ptr_eq(list->head->data, a);
    ck_assert_ptr_eq(list->tail->data, a);

    wingo_slist_prepend(list, b);
    ck_assert_uint_eq(list->count, 2);
    ck_assert_ptr_eq(list->head->data, b);
    ck_assert_ptr_eq(list->tail->data, a);

    wingo_slist_prepend(list, c);
    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(list->head->data, c);
    ck_assert_ptr_eq(list->tail->data, a);

    wingo_slist_free(list);
    free(a); free(b); free(c);
}
END_TEST

START_TEST(test_slist_get)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    ck_assert_ptr_eq(wingo_slist_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_slist_get(list, 1), &b);
    ck_assert_ptr_eq(wingo_slist_get(list, 2), &c);
    ck_assert_ptr_null(wingo_slist_get(list, 3));
    ck_assert_ptr_eq(wingo_slist_first(list), &a);
    ck_assert_ptr_eq(wingo_slist_last(list), &c);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_insert)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3, d = 4;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &c);

    /* Insert at position 1 */
    ck_assert_int_eq(wingo_slist_insert(list, 1, &b), WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(wingo_slist_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_slist_get(list, 1), &b);
    ck_assert_ptr_eq(wingo_slist_get(list, 2), &c);

    /* Insert at 0 = prepend */
    ck_assert_int_eq(wingo_slist_insert(list, 0, &d), WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 4);
    ck_assert_ptr_eq(wingo_slist_get(list, 0), &d);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_remove)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    /* Remove middle */
    void *removed = wingo_slist_remove(list, 1);
    ck_assert_ptr_eq(removed, &b);
    ck_assert_uint_eq(list->count, 2);
    ck_assert_ptr_eq(wingo_slist_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_slist_get(list, 1), &c);

    /* Remove head */
    removed = wingo_slist_remove(list, 0);
    ck_assert_ptr_eq(removed, &a);
    ck_assert_uint_eq(list->count, 1);
    ck_assert_ptr_eq(list->head->data, &c);
    ck_assert_ptr_eq(list->tail->data, &c);

    /* Remove tail */
    removed = wingo_slist_remove(list, 0);
    ck_assert_ptr_eq(removed, &c);
    ck_assert_uint_eq(list->count, 0);
    ck_assert_ptr_null(list->head);
    ck_assert_ptr_null(list->tail);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_remove_data)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;
    int target = 2;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    void *removed = wingo_slist_remove_data(list, &target, cmp_int);
    ck_assert_ptr_eq(removed, &b);
    ck_assert_uint_eq(list->count, 2);

    /* Remove nonexistent */
    removed = wingo_slist_remove_data(list, &target, cmp_int);
    ck_assert_ptr_null(removed);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_remove_all)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 2, d = 3, e = 2;
    int target = 2;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);
    wingo_slist_append(list, &d);
    wingo_slist_append(list, &e);

    wingo_size removed = wingo_slist_remove_all(list, &target, cmp_int);
    ck_assert_uint_eq(removed, 3);
    ck_assert_uint_eq(list->count, 2);
    ck_assert_ptr_eq(wingo_slist_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_slist_get(list, 1), &d);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_find)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;
    int target = 2;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    void *found = wingo_slist_find(list, &target, cmp_int);
    ck_assert_ptr_eq(found, &b);

    target = 99;
    found = wingo_slist_find(list, &target, cmp_int);
    ck_assert_ptr_null(found);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_clear)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    wingo_slist_clear(list);
    ck_assert_uint_eq(list->count, 0);
    ck_assert_ptr_null(list->head);
    ck_assert_ptr_null(list->tail);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_sort)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 3, b = 1, c = 4, d = 1, e = 5, f = 9, g = 2, h = 6;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);
    wingo_slist_append(list, &d);
    wingo_slist_append(list, &e);
    wingo_slist_append(list, &f);
    wingo_slist_append(list, &g);
    wingo_slist_append(list, &h);

    wingo_slist_sort(list, cmp_int);

    /* Verify sorted */
    int expected[] = {1, 1, 2, 3, 4, 5, 6, 9};
    wingo_size i;
    for (i = 0; i < 8; i++) {
        int *v = (int *)wingo_slist_get(list, i);
        ck_assert_int_eq(*v, expected[i]);
    }

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_sort_empty)
{
    wingo_slist_t *list = wingo_slist_new(NULL);

    /* Sort empty list should not crash */
    wingo_slist_sort(list, cmp_int);
    ck_assert_uint_eq(list->count, 0);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_sort_single)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 42;

    wingo_slist_append(list, &a);
    wingo_slist_sort(list, cmp_int);

    ck_assert_uint_eq(list->count, 1);
    ck_assert_ptr_eq(wingo_slist_get(list, 0), &a);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_is_empty)
{
    wingo_slist_t *list = wingo_slist_new(NULL);

    ck_assert(wingo_slist_is_empty(list));
    ck_assert_uint_eq(wingo_slist_count(list), 0);

    int a = 1;
    wingo_slist_append(list, &a);

    ck_assert(!wingo_slist_is_empty(list));
    ck_assert_uint_eq(wingo_slist_count(list), 1);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_slist_free_fn)
{
    reset_counters();

    wingo_slist_t *list = wingo_slist_new(test_free_fn);

    wingo_slist_append(list, make_int(1));
    wingo_slist_append(list, make_int(2));
    wingo_slist_append(list, make_int(3));

    ck_assert_int_eq(free_count, 0);

    wingo_slist_free(list);

    ck_assert_int_eq(free_count, 3);
}
END_TEST

/* ============================================================================
 * DOUBLY-LINKED LIST TESTS
 * ============================================================================ */

START_TEST(test_list_new)
{
    wingo_list_t *list = wingo_list_new(NULL);

    ck_assert_ptr_nonnull(list);
    ck_assert_ptr_null(list->head);
    ck_assert_ptr_null(list->tail);
    ck_assert_uint_eq(list->count, 0);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_free_null)
{
    wingo_list_free(NULL);
}
END_TEST

START_TEST(test_list_append)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(list->head->data, &a);
    ck_assert_ptr_eq(list->tail->data, &c);

    /* Check links */
    ck_assert_ptr_null(list->head->prev);
    ck_assert_ptr_eq(list->head->next->data, &b);
    ck_assert_ptr_eq(list->head->next->prev->data, &a);
    ck_assert_ptr_eq(list->tail->next, NULL);
    ck_assert_ptr_eq(list->tail->prev->data, &b);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_prepend)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_list_prepend(list, &a);
    wingo_list_prepend(list, &b);
    wingo_list_prepend(list, &c);

    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(list->head->data, &c);
    ck_assert_ptr_eq(list->tail->data, &a);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_get)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3, d = 4, e = 5;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);
    wingo_list_append(list, &d);
    wingo_list_append(list, &e);

    ck_assert_ptr_eq(wingo_list_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_list_get(list, 1), &b);
    ck_assert_ptr_eq(wingo_list_get(list, 2), &c);
    ck_assert_ptr_eq(wingo_list_get(list, 3), &d);
    ck_assert_ptr_eq(wingo_list_get(list, 4), &e);
    ck_assert_ptr_null(wingo_list_get(list, 5));

    ck_assert_ptr_eq(wingo_list_first(list), &a);
    ck_assert_ptr_eq(wingo_list_last(list), &e);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_insert_before)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3, x = 99;

    wingo_list_append(list, &a);
    wingo_list_append(list, &c);

    wingo_list_node_t *node_c = wingo_list_get_node(list, 1);

    ck_assert_int_eq(wingo_list_insert_before(list, node_c, &x),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(wingo_list_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_list_get(list, 1), &x);
    ck_assert_ptr_eq(wingo_list_get(list, 2), &c);

    /* Insert before head = prepend */
    wingo_list_node_t *node_a = wingo_list_get_node(list, 0);
    ck_assert_int_eq(wingo_list_insert_before(list, node_a, &b),
                     WINGO_SUCCESS);
    ck_assert_ptr_eq(wingo_list_first(list), &b);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_insert_after)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_list_append(list, &a);
    wingo_list_append(list, &c);

    wingo_list_node_t *node_a = wingo_list_get_node(list, 0);

    ck_assert_int_eq(wingo_list_insert_after(list, node_a, &b),
                     WINGO_SUCCESS);
    ck_assert_uint_eq(list->count, 3);
    ck_assert_ptr_eq(wingo_list_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_list_get(list, 1), &b);
    ck_assert_ptr_eq(wingo_list_get(list, 2), &c);

    /* Insert after tail = append */
    wingo_list_node_t *node_c = wingo_list_get_node(list, 2);
    int d = 4;
    wingo_list_insert_after(list, node_c, &d);
    ck_assert_ptr_eq(wingo_list_last(list), &d);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_remove_node)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    wingo_list_node_t *node_b = wingo_list_get_node(list, 1);
    void *removed = wingo_list_remove_node(list, node_b);

    ck_assert_ptr_eq(removed, &b);
    ck_assert_uint_eq(list->count, 2);
    ck_assert_ptr_eq(wingo_list_get(list, 0), &a);
    ck_assert_ptr_eq(wingo_list_get(list, 1), &c);

    /* Verify prev/next links */
    ck_assert_ptr_eq(list->head->next->data, &c);
    ck_assert_ptr_eq(list->tail->prev->data, &a);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_remove)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3, d = 4;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);
    wingo_list_append(list, &d);

    /* Remove head */
    void *removed = wingo_list_remove(list, 0);
    ck_assert_ptr_eq(removed, &a);
    ck_assert_uint_eq(list->count, 3);

    /* Remove tail */
    removed = wingo_list_remove(list, 2);
    ck_assert_ptr_eq(removed, &d);
    ck_assert_uint_eq(list->count, 2);

    /* Remove middle */
    removed = wingo_list_remove(list, 0);
    ck_assert_ptr_eq(removed, &b);
    ck_assert_uint_eq(list->count, 1);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_find)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;
    int target = 2;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    void *found = wingo_list_find(list, &target, cmp_int);
    ck_assert_ptr_eq(found, &b);

    wingo_list_node_t *node = wingo_list_find_node(list, &target, cmp_int);
    ck_assert_ptr_nonnull(node);
    ck_assert_ptr_eq(node->data, &b);

    target = 99;
    ck_assert_ptr_null(wingo_list_find(list, &target, cmp_int));

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_clear)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    wingo_list_clear(list);
    ck_assert_uint_eq(list->count, 0);
    ck_assert_ptr_null(list->head);
    ck_assert_ptr_null(list->tail);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_sort)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 3, b = 1, c = 4, d = 1, e = 5, f = 9, g = 2, h = 6;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);
    wingo_list_append(list, &d);
    wingo_list_append(list, &e);
    wingo_list_append(list, &f);
    wingo_list_append(list, &g);
    wingo_list_append(list, &h);

    wingo_list_sort(list, cmp_int);

    int expected[] = {1, 1, 2, 3, 4, 5, 6, 9};
    wingo_size i;
    for (i = 0; i < 8; i++) {
        int *v = (int *)wingo_list_get(list, i);
        ck_assert_int_eq(*v, expected[i]);
    }

    /* Verify prev/next links after sort */
    ck_assert_ptr_null(list->head->prev);
    ck_assert_ptr_null(list->tail->next);

    wingo_list_node_t *node = list->head;
    while (node->next != NULL) {
        ck_assert_ptr_eq(node->next->prev, node);
        node = node->next;
    }

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_sort_desc)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 3, b = 1, c = 2;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    wingo_list_sort(list, cmp_int_desc);

    int expected[] = {3, 2, 1};
    wingo_size i;
    for (i = 0; i < 3; i++) {
        int *v = (int *)wingo_list_get(list, i);
        ck_assert_int_eq(*v, expected[i]);
    }

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_reverse)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3, d = 4;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);
    wingo_list_append(list, &d);

    wingo_list_reverse(list);

    ck_assert_ptr_eq(wingo_list_get(list, 0), &d);
    ck_assert_ptr_eq(wingo_list_get(list, 1), &c);
    ck_assert_ptr_eq(wingo_list_get(list, 2), &b);
    ck_assert_ptr_eq(wingo_list_get(list, 3), &a);

    /* Verify links */
    ck_assert_ptr_null(list->head->prev);
    ck_assert_ptr_null(list->tail->next);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_reverse_empty)
{
    wingo_list_t *list = wingo_list_new(NULL);

    /* Should not crash */
    wingo_list_reverse(list);
    ck_assert_uint_eq(list->count, 0);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_is_empty)
{
    wingo_list_t *list = wingo_list_new(NULL);

    ck_assert(wingo_list_is_empty(list));
    ck_assert_uint_eq(wingo_list_count(list), 0);

    int a = 1;
    wingo_list_append(list, &a);

    ck_assert(!wingo_list_is_empty(list));
    ck_assert_uint_eq(wingo_list_count(list), 1);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_free_fn)
{
    reset_counters();

    wingo_list_t *list = wingo_list_new(test_free_fn);

    wingo_list_append(list, make_int(1));
    wingo_list_append(list, make_int(2));
    wingo_list_append(list, make_int(3));

    ck_assert_int_eq(free_count, 0);

    wingo_list_free(list);

    ck_assert_int_eq(free_count, 3);
}
END_TEST

/* ============================================================================
 * ITERATION MACRO TESTS
 * ============================================================================ */

START_TEST(test_slist_foreach)
{
    wingo_slist_t *list = wingo_slist_new(NULL);
    int a = 1, b = 2, c = 3;
    int sum = 0;
    wingo_slist_node_t *node;

    wingo_slist_append(list, &a);
    wingo_slist_append(list, &b);
    wingo_slist_append(list, &c);

    WINGO_SLIST_FOREACH(list, node) {
        sum += *(int *)node->data;
    }

    ck_assert_int_eq(sum, 6);

    wingo_slist_free(list);
}
END_TEST

START_TEST(test_list_foreach)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;
    int sum = 0;
    wingo_list_node_t *node;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    WINGO_LIST_FOREACH(list, node) {
        sum += *(int *)node->data;
    }

    ck_assert_int_eq(sum, 6);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_foreach_reverse)
{
    wingo_list_t *list = wingo_list_new(NULL);
    int a = 1, b = 2, c = 3;
    int expected[] = {3, 2, 1};
    int idx = 0;
    wingo_list_node_t *node;

    wingo_list_append(list, &a);
    wingo_list_append(list, &b);
    wingo_list_append(list, &c);

    WINGO_LIST_FOREACH_REVERSE(list, node) {
        ck_assert_int_eq(*(int *)node->data, expected[idx++]);
    }

    ck_assert_int_eq(idx, 3);

    wingo_list_free(list);
}
END_TEST

START_TEST(test_list_foreach_safe)
{
    wingo_list_t *list = wingo_list_new(NULL);
    wingo_list_node_t *node, *tmp;
    int i;

    for (i = 0; i < 5; i++) {
        wingo_list_append(list, make_int(i));
    }

    ck_assert_uint_eq(list->count, 5);

    /*
     * Remove all nodes while iterating.
     * Safe iteration should allow this.
     */
    WINGO_LIST_FOREACH_SAFE(list, node, tmp) {
        void *data = wingo_list_remove_node(list, node);
        free(data);
    }

    ck_assert_uint_eq(list->count, 0);

    wingo_list_free(list);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *list_suite(void)
{
    Suite *s;
    TCase *tc_slist;
    TCase *tc_list;
    TCase *tc_iter;

    s = suite_create("List");

    /* Singly-linked list tests */
    tc_slist = tcase_create("SList");
    tcase_add_test(tc_slist, test_slist_new);
    tcase_add_test(tc_slist, test_slist_free_null);
    tcase_add_test(tc_slist, test_slist_append);
    tcase_add_test(tc_slist, test_slist_prepend);
    tcase_add_test(tc_slist, test_slist_get);
    tcase_add_test(tc_slist, test_slist_insert);
    tcase_add_test(tc_slist, test_slist_remove);
    tcase_add_test(tc_slist, test_slist_remove_data);
    tcase_add_test(tc_slist, test_slist_remove_all);
    tcase_add_test(tc_slist, test_slist_find);
    tcase_add_test(tc_slist, test_slist_clear);
    tcase_add_test(tc_slist, test_slist_sort);
    tcase_add_test(tc_slist, test_slist_sort_empty);
    tcase_add_test(tc_slist, test_slist_sort_single);
    tcase_add_test(tc_slist, test_slist_is_empty);
    tcase_add_test(tc_slist, test_slist_free_fn);
    suite_add_tcase(s, tc_slist);

    /* Doubly-linked list tests */
    tc_list = tcase_create("List");
    tcase_add_test(tc_list, test_list_new);
    tcase_add_test(tc_list, test_list_free_null);
    tcase_add_test(tc_list, test_list_append);
    tcase_add_test(tc_list, test_list_prepend);
    tcase_add_test(tc_list, test_list_get);
    tcase_add_test(tc_list, test_list_insert_before);
    tcase_add_test(tc_list, test_list_insert_after);
    tcase_add_test(tc_list, test_list_remove_node);
    tcase_add_test(tc_list, test_list_remove);
    tcase_add_test(tc_list, test_list_find);
    tcase_add_test(tc_list, test_list_clear);
    tcase_add_test(tc_list, test_list_sort);
    tcase_add_test(tc_list, test_list_sort_desc);
    tcase_add_test(tc_list, test_list_reverse);
    tcase_add_test(tc_list, test_list_reverse_empty);
    tcase_add_test(tc_list, test_list_is_empty);
    tcase_add_test(tc_list, test_list_free_fn);
    suite_add_tcase(s, tc_list);

    /* Iteration macro tests */
    tc_iter = tcase_create("Iteration");
    tcase_add_test(tc_iter, test_slist_foreach);
    tcase_add_test(tc_iter, test_list_foreach);
    tcase_add_test(tc_iter, test_list_foreach_reverse);
    tcase_add_test(tc_iter, test_list_foreach_safe);
    suite_add_tcase(s, tc_iter);

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

    s = list_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
