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
 * Unit tests for wingo/util/queue.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/util/queue.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

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

static int *make_int(int value)
{
    int *result = malloc(sizeof(int));
    if (result != NULL) {
        *result = value;
    }
    return result;
}

/*
 * Comparator for priority queue tests.
 * Lower value = higher priority (min-heap).
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
 * Comparator for max-heap (higher value = higher priority).
 */
static int cmp_int_max(const void *a, const void *b)
{
    return cmp_int(b, a);
}

/* ============================================================================
 * FIFO QUEUE TESTS
 * ============================================================================ */

START_TEST(test_queue_new)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);

    ck_assert_ptr_nonnull(queue);
    ck_assert_ptr_null(queue->head);
    ck_assert_ptr_null(queue->tail);
    ck_assert_uint_eq(queue->count, 0);

    wingo_queue_free(queue);
}
END_TEST

START_TEST(test_queue_free_null)
{
    /* Should not crash */
    wingo_queue_free(NULL);
}
END_TEST

START_TEST(test_queue_push_pop)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);
    int a = 1, b = 2, c = 3;

    ck_assert_int_eq(wingo_queue_push(queue, &a), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_queue_push(queue, &b), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_queue_push(queue, &c), WINGO_SUCCESS);
    ck_assert_uint_eq(queue->count, 3);

    /* FIFO: first in = first out */
    ck_assert_ptr_eq(wingo_queue_pop(queue), &a);
    ck_assert_uint_eq(queue->count, 2);

    ck_assert_ptr_eq(wingo_queue_pop(queue), &b);
    ck_assert_uint_eq(queue->count, 1);

    ck_assert_ptr_eq(wingo_queue_pop(queue), &c);
    ck_assert_uint_eq(queue->count, 0);

    /* Empty queue */
    ck_assert_ptr_null(wingo_queue_pop(queue));

    wingo_queue_free(queue);
}
END_TEST

START_TEST(test_queue_peek)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);
    int a = 1, b = 2;

    /* Peek empty */
    ck_assert_ptr_null(wingo_queue_peek(queue));

    wingo_queue_push(queue, &a);
    wingo_queue_push(queue, &b);

    /* Peek returns first without removing */
    ck_assert_ptr_eq(wingo_queue_peek(queue), &a);
    ck_assert_uint_eq(queue->count, 2);

    /* Pop then peek */
    wingo_queue_pop(queue);
    ck_assert_ptr_eq(wingo_queue_peek(queue), &b);

    wingo_queue_free(queue);
}
END_TEST

START_TEST(test_queue_clear)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);
    int a = 1, b = 2, c = 3;

    wingo_queue_push(queue, &a);
    wingo_queue_push(queue, &b);
    wingo_queue_push(queue, &c);

    wingo_queue_clear(queue);

    ck_assert_uint_eq(queue->count, 0);
    ck_assert_ptr_null(queue->head);
    ck_assert_ptr_null(queue->tail);
    ck_assert_ptr_null(wingo_queue_pop(queue));

    wingo_queue_free(queue);
}
END_TEST

START_TEST(test_queue_is_empty)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);
    int a = 1;

    ck_assert(wingo_queue_is_empty(queue));
    ck_assert_uint_eq(wingo_queue_count(queue), 0);

    wingo_queue_push(queue, &a);

    ck_assert(!wingo_queue_is_empty(queue));
    ck_assert_uint_eq(wingo_queue_count(queue), 1);

    wingo_queue_pop(queue);

    ck_assert(wingo_queue_is_empty(queue));

    wingo_queue_free(queue);
}
END_TEST

START_TEST(test_queue_free_fn)
{
    reset_counters();

    wingo_queue_t *queue = wingo_queue_new(test_free_fn);

    wingo_queue_push(queue, make_int(1));
    wingo_queue_push(queue, make_int(2));
    wingo_queue_push(queue, make_int(3));

    ck_assert_int_eq(free_count, 0);

    wingo_queue_free(queue);

    ck_assert_int_eq(free_count, 3);
}
END_TEST

START_TEST(test_queue_many_items)
{
    wingo_queue_t *queue = wingo_queue_new(NULL);
    int values[100];
    int i;

    for (i = 0; i < 100; i++) {
        values[i] = i;
        wingo_queue_push(queue, &values[i]);
    }

    ck_assert_uint_eq(queue->count, 100);

    /* FIFO order */
    for (i = 0; i < 100; i++) {
        int *v = (int *)wingo_queue_pop(queue);
        ck_assert_ptr_nonnull(v);
        ck_assert_int_eq(*v, i);
    }

    ck_assert_uint_eq(queue->count, 0);

    wingo_queue_free(queue);
}
END_TEST

/* ============================================================================
 * RING BUFFER TESTS
 * ============================================================================ */

START_TEST(test_ring_new)
{
    wingo_ring_t *ring = wingo_ring_new(8, NULL);

    ck_assert_ptr_nonnull(ring);
    ck_assert_uint_eq(ring->capacity, 8);
    ck_assert_uint_eq(ring->count, 0);
    ck_assert_uint_eq(ring->head, 0);
    ck_assert_uint_eq(ring->tail, 0);

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_new_non_power_of_two)
{
    /* Capacity 10 should round up to 16 */
    wingo_ring_t *ring = wingo_ring_new(10, NULL);

    ck_assert_ptr_nonnull(ring);
    ck_assert_uint_eq(ring->capacity, 16);

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_new_zero)
{
    /* Zero capacity is invalid */
    wingo_ring_t *ring = wingo_ring_new(0, NULL);
    ck_assert_ptr_null(ring);
}
END_TEST

START_TEST(test_ring_free_null)
{
    wingo_ring_free(NULL);
}
END_TEST

START_TEST(test_ring_push_pop)
{
    wingo_ring_t *ring = wingo_ring_new(4, NULL);
    int a = 1, b = 2, c = 3;

    ck_assert_int_eq(wingo_ring_push(ring, &a), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_ring_push(ring, &b), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_ring_push(ring, &c), WINGO_SUCCESS);
    ck_assert_uint_eq(ring->count, 3);

    /* FIFO */
    ck_assert_ptr_eq(wingo_ring_pop(ring), &a);
    ck_assert_ptr_eq(wingo_ring_pop(ring), &b);
    ck_assert_ptr_eq(wingo_ring_pop(ring), &c);
    ck_assert_uint_eq(ring->count, 0);

    ck_assert_ptr_null(wingo_ring_pop(ring));

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_wraparound)
{
    wingo_ring_t *ring = wingo_ring_new(4, NULL);
    int values[10];
    int i;

    for (i = 0; i < 10; i++) {
        values[i] = i;
    }

    /*
     * Push 4, pop 4, repeat.
     * This forces head/tail to wrap around.
     */
    for (i = 0; i < 8; i++) {
        ck_assert_int_eq(wingo_ring_push(ring, &values[i]), WINGO_SUCCESS);
        ck_assert_ptr_eq(wingo_ring_pop(ring), &values[i]);
        ck_assert_uint_eq(ring->count, 0);
    }

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_full)
{
    wingo_ring_t *ring = wingo_ring_new(4, NULL);
    int values[5] = {1, 2, 3, 4, 5};

    ck_assert_int_eq(wingo_ring_push(ring, &values[0]), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_ring_push(ring, &values[1]), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_ring_push(ring, &values[2]), WINGO_SUCCESS);
    ck_assert_int_eq(wingo_ring_push(ring, &values[3]), WINGO_SUCCESS);

    ck_assert(wingo_ring_is_full(ring));
    ck_assert_uint_eq(ring->count, 4);

    /* Push when full should fail */
    ck_assert_int_eq(wingo_ring_push(ring, &values[4]), WINGO_ERR_OVERFLOW);
    ck_assert_uint_eq(ring->count, 4);

    /* Pop one, then push should work */
    wingo_ring_pop(ring);
    ck_assert(!wingo_ring_is_full(ring));
    ck_assert_int_eq(wingo_ring_push(ring, &values[4]), WINGO_SUCCESS);

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_peek)
{
    wingo_ring_t *ring = wingo_ring_new(4, NULL);
    int a = 1, b = 2;

    ck_assert_ptr_null(wingo_ring_peek(ring));

    wingo_ring_push(ring, &a);
    wingo_ring_push(ring, &b);

    ck_assert_ptr_eq(wingo_ring_peek(ring), &a);
    ck_assert_uint_eq(ring->count, 2);

    wingo_ring_pop(ring);
    ck_assert_ptr_eq(wingo_ring_peek(ring), &b);

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_clear)
{
    wingo_ring_t *ring = wingo_ring_new(4, NULL);
    int a = 1, b = 2, c = 3;

    wingo_ring_push(ring, &a);
    wingo_ring_push(ring, &b);
    wingo_ring_push(ring, &c);

    wingo_ring_clear(ring);

    ck_assert_uint_eq(ring->count, 0);
    ck_assert_uint_eq(ring->head, 0);
    ck_assert_uint_eq(ring->tail, 0);
    ck_assert(wingo_ring_is_empty(ring));

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_is_empty_full)
{
    wingo_ring_t *ring = wingo_ring_new(2, NULL);
    int a = 1, b = 2;

    ck_assert(wingo_ring_is_empty(ring));
    ck_assert(!wingo_ring_is_full(ring));

    wingo_ring_push(ring, &a);
    ck_assert(!wingo_ring_is_empty(ring));
    ck_assert(!wingo_ring_is_full(ring));

    wingo_ring_push(ring, &b);
    ck_assert(!wingo_ring_is_empty(ring));
    ck_assert(wingo_ring_is_full(ring));

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_count_capacity)
{
    wingo_ring_t *ring = wingo_ring_new(8, NULL);
    int a = 1, b = 2;

    ck_assert_uint_eq(wingo_ring_count(ring), 0);
    ck_assert_uint_eq(wingo_ring_capacity(ring), 8);

    wingo_ring_push(ring, &a);
    wingo_ring_push(ring, &b);

    ck_assert_uint_eq(wingo_ring_count(ring), 2);
    ck_assert_uint_eq(wingo_ring_capacity(ring), 8);

    wingo_ring_free(ring);
}
END_TEST

START_TEST(test_ring_free_fn)
{
    reset_counters();

    wingo_ring_t *ring = wingo_ring_new(4, test_free_fn);

    wingo_ring_push(ring, make_int(1));
    wingo_ring_push(ring, make_int(2));
    wingo_ring_push(ring, make_int(3));

    ck_assert_int_eq(free_count, 0);

    wingo_ring_free(ring);

    ck_assert_int_eq(free_count, 3);
}
END_TEST

/* ============================================================================
 * PRIORITY QUEUE TESTS
 * ============================================================================ */

START_TEST(test_pqueue_new)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);

    ck_assert_ptr_nonnull(pq);
    ck_assert_uint_eq(pq->count, 0);
    ck_assert_uint_ge(pq->capacity, 8);
    ck_assert_ptr_nonnull(pq->cmp);

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_new_null_cmp)
{
    /* Comparator is required */
    wingo_pqueue_t *pq = wingo_pqueue_new(8, NULL, NULL);
    ck_assert_ptr_null(pq);
}
END_TEST

START_TEST(test_pqueue_free_null)
{
    wingo_pqueue_free(NULL);
}
END_TEST

START_TEST(test_pqueue_min_heap)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);
    int a = 5, b = 2, c = 8, d = 1, e = 9, f = 3;

    wingo_pqueue_push(pq, &a);
    wingo_pqueue_push(pq, &b);
    wingo_pqueue_push(pq, &c);
    wingo_pqueue_push(pq, &d);
    wingo_pqueue_push(pq, &e);
    wingo_pqueue_push(pq, &f);

    ck_assert_uint_eq(pq->count, 6);

    /* Min-heap: pop in ascending order */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &d);  /* 1 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &b);  /* 2 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &f);  /* 3 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &a);  /* 5 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &c);  /* 8 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &e);  /* 9 */

    ck_assert_ptr_null(wingo_pqueue_pop(pq));

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_max_heap)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int_max, NULL);
    int a = 5, b = 2, c = 8, d = 1, e = 9, f = 3;

    wingo_pqueue_push(pq, &a);
    wingo_pqueue_push(pq, &b);
    wingo_pqueue_push(pq, &c);
    wingo_pqueue_push(pq, &d);
    wingo_pqueue_push(pq, &e);
    wingo_pqueue_push(pq, &f);

    /* Max-heap: pop in descending order */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &e);  /* 9 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &c);  /* 8 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &a);  /* 5 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &f);  /* 3 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &b);  /* 2 */
    ck_assert_ptr_eq(wingo_pqueue_pop(pq), &d);  /* 1 */

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_peek)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);
    int a = 5, b = 2, c = 8;

    ck_assert_ptr_null(wingo_pqueue_peek(pq));

    wingo_pqueue_push(pq, &a);
    wingo_pqueue_push(pq, &b);
    wingo_pqueue_push(pq, &c);

    /* Peek returns minimum without removing */
    ck_assert_ptr_eq(wingo_pqueue_peek(pq), &b);
    ck_assert_uint_eq(pq->count, 3);

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_clear)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);
    int a = 5, b = 2, c = 8;

    wingo_pqueue_push(pq, &a);
    wingo_pqueue_push(pq, &b);
    wingo_pqueue_push(pq, &c);

    wingo_pqueue_clear(pq);

    ck_assert_uint_eq(pq->count, 0);
    ck_assert(wingo_pqueue_is_empty(pq));
    ck_assert_ptr_null(wingo_pqueue_pop(pq));

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_is_empty)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);
    int a = 42;

    ck_assert(wingo_pqueue_is_empty(pq));
    ck_assert_uint_eq(wingo_pqueue_count(pq), 0);

    wingo_pqueue_push(pq, &a);

    ck_assert(!wingo_pqueue_is_empty(pq));
    ck_assert_uint_eq(wingo_pqueue_count(pq), 1);

    wingo_pqueue_pop(pq);

    ck_assert(wingo_pqueue_is_empty(pq));

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_grow)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(4, cmp_int, NULL);
    int values[100];
    int i;
    wingo_size initial_cap = pq->capacity;

    for (i = 0; i < 100; i++) {
        values[i] = 100 - i;  /* Descending */
        wingo_pqueue_push(pq, &values[i]);
    }

    ck_assert_uint_gt(pq->capacity, initial_cap);
    ck_assert_uint_eq(pq->count, 100);

    /* Verify min-heap order */
    for (i = 0; i < 100; i++) {
        int *v = (int *)wingo_pqueue_pop(pq);
        ck_assert_ptr_nonnull(v);
        ck_assert_int_eq(*v, i + 1);  /* 1, 2, 3, ..., 100 */
    }

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_many_random)
{
    wingo_pqueue_t *pq = wingo_pqueue_new(8, cmp_int, NULL);
    int values[1000];
    int i;
    int prev = -1;

    /* Insert in pseudo-random order */
    for (i = 0; i < 1000; i++) {
        values[i] = (i * 7919) % 1000;  /* Prime multiplier */
        wingo_pqueue_push(pq, &values[i]);
    }

    ck_assert_uint_eq(pq->count, 1000);

    /* Pop should be in ascending order */
    for (i = 0; i < 1000; i++) {
        int *v = (int *)wingo_pqueue_pop(pq);
        ck_assert_ptr_nonnull(v);
        ck_assert_int_ge(*v, prev);
        prev = *v;
    }

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_free_fn)
{
    reset_counters();

    wingo_pqueue_t *pq = wingo_pqueue_new(4, cmp_int, test_free_fn);
    int a = 1, b = 2, c = 3;

    wingo_pqueue_push(pq, &a);
    wingo_pqueue_push(pq, &b);
    wingo_pqueue_push(pq, &c);

    /*
     * Note: we pushed pointers to stack ints.
     * free_fn will be called on them during free.
     * This will crash because they're not heap-allocated.
     *
     * For this test, we use NULL free_fn instead.
     */
    wingo_pqueue_clear(pq);

    ck_assert_uint_eq(pq->count, 0);

    wingo_pqueue_free(pq);
}
END_TEST

START_TEST(test_pqueue_free_fn_heap)
{
    reset_counters();

    wingo_pqueue_t *pq = wingo_pqueue_new(4, cmp_int, test_free_fn);

    wingo_pqueue_push(pq, make_int(1));
    wingo_pqueue_push(pq, make_int(2));
    wingo_pqueue_push(pq, make_int(3));

    ck_assert_int_eq(free_count, 0);

    wingo_pqueue_free(pq);

    ck_assert_int_eq(free_count, 3);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *queue_suite(void)
{
    Suite *s;
    TCase *tc_fifo;
    TCase *tc_ring;
    TCase *tc_pqueue;

    s = suite_create("Queue");

    /* FIFO queue tests */
    tc_fifo = tcase_create("FIFO");
    tcase_add_test(tc_fifo, test_queue_new);
    tcase_add_test(tc_fifo, test_queue_free_null);
    tcase_add_test(tc_fifo, test_queue_push_pop);
    tcase_add_test(tc_fifo, test_queue_peek);
    tcase_add_test(tc_fifo, test_queue_clear);
    tcase_add_test(tc_fifo, test_queue_is_empty);
    tcase_add_test(tc_fifo, test_queue_free_fn);
    tcase_add_test(tc_fifo, test_queue_many_items);
    suite_add_tcase(s, tc_fifo);

    /* Ring buffer tests */
    tc_ring = tcase_create("Ring");
    tcase_add_test(tc_ring, test_ring_new);
    tcase_add_test(tc_ring, test_ring_new_non_power_of_two);
    tcase_add_test(tc_ring, test_ring_new_zero);
    tcase_add_test(tc_ring, test_ring_free_null);
    tcase_add_test(tc_ring, test_ring_push_pop);
    tcase_add_test(tc_ring, test_ring_wraparound);
    tcase_add_test(tc_ring, test_ring_full);
    tcase_add_test(tc_ring, test_ring_peek);
    tcase_add_test(tc_ring, test_ring_clear);
    tcase_add_test(tc_ring, test_ring_is_empty_full);
    tcase_add_test(tc_ring, test_ring_count_capacity);
    tcase_add_test(tc_ring, test_ring_free_fn);
    suite_add_tcase(s, tc_ring);

    /* Priority queue tests */
    tc_pqueue = tcase_create("PQueue");
    tcase_add_test(tc_pqueue, test_pqueue_new);
    tcase_add_test(tc_pqueue, test_pqueue_new_null_cmp);
    tcase_add_test(tc_pqueue, test_pqueue_free_null);
    tcase_add_test(tc_pqueue, test_pqueue_min_heap);
    tcase_add_test(tc_pqueue, test_pqueue_max_heap);
    tcase_add_test(tc_pqueue, test_pqueue_peek);
    tcase_add_test(tc_pqueue, test_pqueue_clear);
    tcase_add_test(tc_pqueue, test_pqueue_is_empty);
    tcase_add_test(tc_pqueue, test_pqueue_grow);
    tcase_add_test(tc_pqueue, test_pqueue_many_random);
    tcase_add_test(tc_pqueue, test_pqueue_free_fn);
    tcase_add_test(tc_pqueue, test_pqueue_free_fn_heap);
    suite_add_tcase(s, tc_pqueue);

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

    s = queue_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
