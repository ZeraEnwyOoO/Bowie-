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
 * Unit tests for wingo/core/thread.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "wingo/core/thread.h"
#include "wingo/util/time.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Test thread function: increments a counter.
 */
static void *test_thread_func(void *arg)
{
    int *counter = (int *)arg;

    if (counter != NULL) {
        (*counter)++;
    }

    return (void *)(intptr_t)42;
}

/*
 * Test thread function: sleeps, then returns.
 */
static void *sleep_thread_func(void *arg)
{
    int *ms = (int *)arg;

    wingo_thread_sleep_ms(*ms);

    return NULL;
}

/*
 * Test thread function: loops until stop requested.
 */
static void *loop_thread_func(void *arg)
{
    wingo_thread_t *thread = (wingo_thread_t *)arg;

    while (!wingo_thread_should_stop(thread)) {
        wingo_thread_sleep_ms(10);
    }

    return NULL;
}

/*
 * Test task function for thread pool.
 */
static void test_task_func(void *arg)
{
    int *counter = (int *)arg;

    if (counter != NULL) {
        (*counter)++;
    }
}

/*
 * Task with delay.
 */
static void delayed_task_func(void *arg)
{
    int *ms = (int *)arg;

    if (ms != NULL) {
        wingo_thread_sleep_ms(*ms);
    }
}

/* ============================================================================
 * TEST: THREAD CREATION
 * ============================================================================ */

START_TEST(test_thread_new)
{
    wingo_thread_t *thread;

    thread = wingo_thread_new(test_thread_func, NULL, "test");
    ck_assert_ptr_nonnull(thread);
    ck_assert_int_eq(wingo_thread_get_state(thread),
                     WINGO_THREAD_STATE_CREATED);
    ck_assert_str_eq(wingo_thread_get_name(thread), "test");
    ck_assert_uint_eq(wingo_thread_get_id(thread), 0);

    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_new_no_name)
{
    wingo_thread_t *thread;

    thread = wingo_thread_new(test_thread_func, NULL, NULL);
    ck_assert_ptr_nonnull(thread);

    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_new_null_func)
{
    wingo_thread_t *thread = wingo_thread_new(NULL, NULL, "test");
    ck_assert_ptr_null(thread);
}
END_TEST

START_TEST(test_thread_free_null)
{
    /* Should not crash */
    wingo_thread_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: THREAD START/JOIN
 * ============================================================================ */

START_TEST(test_thread_start_join)
{
    wingo_thread_t *thread;
    int counter = 0;
    void *retval = NULL;
    wingo_error_t rc;

    thread = wingo_thread_new(test_thread_func, &counter, "test");
    ck_assert_ptr_nonnull(thread);

    rc = wingo_thread_start(thread);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_thread_join(thread, &retval);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(counter, 1);
    ck_assert_int_eq((intptr_t)retval, 42);

    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_start_twice)
{
    wingo_thread_t *thread;
    wingo_error_t rc;

    thread = wingo_thread_new(test_thread_func, NULL, "test");

    rc = wingo_thread_start(thread);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Second start should fail */
    rc = wingo_thread_start(thread);
    ck_assert_int_eq(rc, WINGO_ERR_ALREADY_EXISTS);

    wingo_thread_join(thread, NULL);
    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_join_null)
{
    wingo_error_t rc = wingo_thread_join(NULL, NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_thread_join_not_started)
{
    wingo_thread_t *thread = wingo_thread_new(test_thread_func, NULL, "test");
    wingo_error_t rc;

    rc = wingo_thread_join(thread, NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_STATE);

    wingo_thread_free(thread);
}
END_TEST

/* ============================================================================
 * TEST: THREAD DETACH
 * ============================================================================ */

START_TEST(test_thread_detach)
{
    wingo_thread_t *thread;
    wingo_error_t rc;

    thread = wingo_thread_new(test_thread_func, NULL, "detached");

    rc = wingo_thread_start(thread);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_thread_detach(thread);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Give it time to finish */
    wingo_thread_sleep_ms(50);

    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_detach_not_started)
{
    wingo_thread_t *thread = wingo_thread_new(test_thread_func, NULL, "test");
    wingo_error_t rc;

    rc = wingo_thread_detach(thread);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_STATE);

    wingo_thread_free(thread);
}
END_TEST

/* ============================================================================
 * TEST: THREAD STOP
 * ============================================================================ */

START_TEST(test_thread_stop)
{
    wingo_thread_t *thread;
    wingo_error_t rc;

    thread = wingo_thread_new(loop_thread_func, NULL, "loop");
    ck_assert_ptr_nonnull(thread);

    rc = wingo_thread_start(thread);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Let it run */
    wingo_thread_sleep_ms(50);

    /* Check should_stop is false */
    ck_assert(!wingo_thread_should_stop(thread));

    /* Request stop */
    wingo_thread_stop(thread);

    /* Now should_stop should be true */
    ck_assert(wingo_thread_should_stop(thread));

    /* Wait for thread to finish */
    wingo_thread_join(thread, NULL);

    wingo_thread_free(thread);
}
END_TEST

START_TEST(test_thread_should_stop_null)
{
    ck_assert(wingo_thread_should_stop(NULL));
}
END_TEST

START_TEST(test_thread_stop_null)
{
    /* Should not crash */
    wingo_thread_stop(NULL);
}
END_TEST

/* ============================================================================
 * TEST: THREAD INFO
 * ============================================================================ */

START_TEST(test_thread_current_id)
{
    wingo_u64 id = wingo_thread_current_id();
    ck_assert_uint_gt(id, 0);
}
END_TEST

START_TEST(test_thread_set_current_name)
{
    wingo_error_t rc;

    rc = wingo_thread_set_current_name("test-thread");
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_thread_set_current_name(NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_thread_sleep_ms)
{
    wingo_i64 start = wingo_time_now_ms();

    wingo_thread_sleep_ms(100);

    wingo_i64 elapsed = wingo_time_now_ms() - start;

    /* Should sleep at least 90ms (allow slack) */
    ck_assert(elapsed >= 90);
}
END_TEST

START_TEST(test_thread_sleep_zero)
{
    /* Should return immediately */
    wingo_thread_sleep_ms(0);
    wingo_thread_sleep_ms(-1);
}
END_TEST

START_TEST(test_thread_yield)
{
    /* Should not crash */
    wingo_thread_yield();
}
END_TEST

/* ============================================================================
 * TEST: THREAD POOL
 * ============================================================================ */

START_TEST(test_thread_pool_new)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(4, 0);

    ck_assert_ptr_nonnull(pool);
    ck_assert_int_eq(wingo_thread_pool_num_workers(pool), 4);
    ck_assert_uint_eq(wingo_thread_pool_pending(pool), 0);
    ck_assert_uint_eq(wingo_thread_pool_active(pool), 0);
    ck_assert_uint_eq(wingo_thread_pool_total(pool), 0);

    wingo_thread_pool_free(pool);
}
END_TEST

START_TEST(test_thread_pool_new_invalid)
{
    /* Zero workers */
    ck_assert_ptr_null(wingo_thread_pool_new(0, 0));

    /* Negative workers */
    ck_assert_ptr_null(wingo_thread_pool_new(-1, 0));
}
END_TEST

START_TEST(test_thread_pool_free_null)
{
    /* Should not crash */
    wingo_thread_pool_free(NULL);
}
END_TEST

START_TEST(test_thread_pool_start_stop)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(2, 0);
    wingo_error_t rc;

    rc = wingo_thread_pool_start(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_thread_pool_stop(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_thread_pool_free(pool);
}
END_TEST

START_TEST(test_thread_pool_submit)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(4, 0);
    int counter = 0;
    wingo_error_t rc;
    int i;

    rc = wingo_thread_pool_start(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Submit 100 tasks */
    for (i = 0; i < 100; i++) {
        rc = wingo_thread_pool_submit(pool, test_task_func, &counter, NULL);
        ck_assert_int_eq(rc, WINGO_SUCCESS);
    }

    /* Wait for all tasks */
    rc = wingo_thread_pool_wait(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* All tasks should have run */
    ck_assert_int_eq(counter, 100);
    ck_assert_uint_eq(wingo_thread_pool_total(pool), 100);

    wingo_thread_pool_stop(pool);
    wingo_thread_pool_free(pool);
}
END_TEST

START_TEST(test_thread_pool_submit_not_running)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(2, 0);
    wingo_error_t rc;

    /* Pool not started */
    rc = wingo_thread_pool_submit(pool, test_task_func, NULL, NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_STATE);

    wingo_thread_pool_free(pool);
}
END_TEST

START_TEST(test_thread_pool_submit_null)
{
    wingo_error_t rc = wingo_thread_pool_submit(NULL, test_task_func, NULL, NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_thread_pool_many_tasks)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(8, 0);
    int counters[8] = {0};
    wingo_error_t rc;
    int i;

    rc = wingo_thread_pool_start(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Submit 1000 tasks distributed across counters */
    for (i = 0; i < 1000; i++) {
        wingo_thread_pool_submit(pool, test_task_func,
                                 &counters[i % 8], NULL);
    }

    rc = wingo_thread_pool_wait(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Total should be 1000 */
    int total = 0;
    for (i = 0; i < 8; i++) {
        total += counters[i];
    }
    ck_assert_int_eq(total, 1000);

    wingo_thread_pool_stop(pool);
    wingo_thread_pool_free(pool);
}
END_TEST

START_TEST(test_thread_pool_wait_empty)
{
    wingo_thread_pool_t *pool = wingo_thread_pool_new(2, 0);
    wingo_error_t rc;

    rc = wingo_thread_pool_start(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Wait with no tasks should return immediately */
    rc = wingo_thread_pool_wait(pool);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_thread_pool_stop(pool);
    wingo_thread_pool_free(pool);
}
END_TEST

/* ============================================================================
 * TEST: MUTEX
 * ============================================================================ */

START_TEST(test_mutex_init_destroy)
{
    wingo_mutex_t mutex;
    wingo_error_t rc;

    rc = wingo_mutex_init(&mutex);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_mutex_destroy(&mutex);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
}
END_TEST

START_TEST(test_mutex_lock_unlock)
{
    wingo_mutex_t mutex;
    wingo_error_t rc;

    wingo_mutex_init(&mutex);

    rc = wingo_mutex_lock(&mutex);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_mutex_unlock(&mutex);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_mutex_destroy(&mutex);
}
END_TEST

START_TEST(test_mutex_trylock)
{
    wingo_mutex_t mutex;
    wingo_error_t rc;

    wingo_mutex_init(&mutex);

    /* First trylock should succeed */
    rc = wingo_mutex_trylock(&mutex);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Second trylock should fail (already locked) */
    rc = wingo_mutex_trylock(&mutex);
    ck_assert_int_eq(rc, WINGO_ERR_BUSY);

    wingo_mutex_unlock(&mutex);
    wingo_mutex_destroy(&mutex);
}
END_TEST

START_TEST(test_mutex_null)
{
    ck_assert_int_eq(wingo_mutex_init(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_mutex_destroy(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_mutex_lock(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_mutex_trylock(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_mutex_unlock(NULL), WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: CONDITION VARIABLE
 * ============================================================================ */

START_TEST(test_cond_init_destroy)
{
    wingo_cond_t cond;
    wingo_error_t rc;

    rc = wingo_cond_init(&cond);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_cond_destroy(&cond);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
}
END_TEST

START_TEST(test_cond_signal)
{
    wingo_cond_t cond;
    wingo_error_t rc;

    wingo_cond_init(&cond);

    rc = wingo_cond_signal(&cond);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_cond_broadcast(&cond);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_cond_destroy(&cond);
}
END_TEST

START_TEST(test_cond_null)
{
    ck_assert_int_eq(wingo_cond_init(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_cond_destroy(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_cond_signal(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_cond_broadcast(NULL), WINGO_ERR_INVALID_ARG);
    ck_assert_int_eq(wingo_cond_wait(NULL, NULL), WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: ATOMIC OPERATIONS
 * ============================================================================ */

START_TEST(test_atomic_init_load_store)
{
    wingo_atomic_int_t a;

    wingo_atomic_init(&a, 42);
    ck_assert_int_eq(wingo_atomic_load(&a), 42);

    wingo_atomic_store(&a, 100);
    ck_assert_int_eq(wingo_atomic_load(&a), 100);
}
END_TEST

START_TEST(test_atomic_add_sub)
{
    wingo_atomic_int_t a;

    wingo_atomic_init(&a, 10);

    ck_assert_int_eq(wingo_atomic_add(&a, 5), 10);
    ck_assert_int_eq(wingo_atomic_load(&a), 15);

    ck_assert_int_eq(wingo_atomic_sub(&a, 3), 15);
    ck_assert_int_eq(wingo_atomic_load(&a), 12);
}
END_TEST

START_TEST(test_atomic_inc_dec)
{
    wingo_atomic_int_t a;

    wingo_atomic_init(&a, 0);

    wingo_atomic_inc(&a);
    ck_assert_int_eq(wingo_atomic_load(&a), 1);

    wingo_atomic_inc(&a);
    ck_assert_int_eq(wingo_atomic_load(&a), 2);

    wingo_atomic_dec(&a);
    ck_assert_int_eq(wingo_atomic_load(&a), 1);

    wingo_atomic_dec(&a);
    ck_assert_int_eq(wingo_atomic_load(&a), 0);
}
END_TEST

START_TEST(test_atomic_cas)
{
    wingo_atomic_int_t a;

    wingo_atomic_init(&a, 42);

    /* CAS with wrong expected value should fail */
    ck_assert(!wingo_atomic_cas(&a, 10, 100));
    ck_assert_int_eq(wingo_atomic_load(&a), 42);

    /* CAS with correct expected value should succeed */
    ck_assert(wingo_atomic_cas(&a, 42, 100));
    ck_assert_int_eq(wingo_atomic_load(&a), 100);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *thread_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_start;
    TCase *tc_stop;
    TCase *tc_info;
    TCase *tc_pool;
    TCase *tc_mutex;
    TCase *tc_cond;
    TCase *tc_atomic;

    s = suite_create("Thread");

    /* Creation tests */
    tc_create = tcase_create("Create");
    tcase_add_test(tc_create, test_thread_new);
    tcase_add_test(tc_create, test_thread_new_no_name);
    tcase_add_test(tc_create, test_thread_new_null_func);
    tcase_add_test(tc_create, test_thread_free_null);
    suite_add_tcase(s, tc_create);

    /* Start/Join tests */
    tc_start = tcase_create("Start");
    tcase_add_test(tc_start, test_thread_start_join);
    tcase_add_test(tc_start, test_thread_start_twice);
    tcase_add_test(tc_start, test_thread_join_null);
    tcase_add_test(tc_start, test_thread_join_not_started);
    suite_add_tcase(s, tc_start);

    /* Stop tests */
    tc_stop = tcase_create("Stop");
    tcase_add_test(tc_stop, test_thread_detach);
    tcase_add_test(tc_stop, test_thread_detach_not_started);
    tcase_add_test(tc_stop, test_thread_stop);
    tcase_add_test(tc_stop, test_thread_should_stop_null);
    tcase_add_test(tc_stop, test_thread_stop_null);
    suite_add_tcase(s, tc_stop);

    /* Info tests */
    tc_info = tcase_create("Info");
    tcase_add_test(tc_info, test_thread_current_id);
    tcase_add_test(tc_info, test_thread_set_current_name);
    tcase_add_test(tc_info, test_thread_sleep_ms);
    tcase_add_test(tc_info, test_thread_sleep_zero);
    tcase_add_test(tc_info, test_thread_yield);
    suite_add_tcase(s, tc_info);

    /* Pool tests */
    tc_pool = tcase_create("Pool");
    tcase_add_test(tc_pool, test_thread_pool_new);
    tcase_add_test(tc_pool, test_thread_pool_new_invalid);
    tcase_add_test(tc_pool, test_thread_pool_free_null);
    tcase_add_test(tc_pool, test_thread_pool_start_stop);
    tcase_add_test(tc_pool, test_thread_pool_submit);
    tcase_add_test(tc_pool, test_thread_pool_submit_not_running);
    tcase_add_test(tc_pool, test_thread_pool_submit_null);
    tcase_add_test(tc_pool, test_thread_pool_many_tasks);
    tcase_add_test(tc_pool, test_thread_pool_wait_empty);
    suite_add_tcase(s, tc_pool);

    /* Mutex tests */
    tc_mutex = tcase_create("Mutex");
    tcase_add_test(tc_mutex, test_mutex_init_destroy);
    tcase_add_test(tc_mutex, test_mutex_lock_unlock);
    tcase_add_test(tc_mutex, test_mutex_trylock);
    tcase_add_test(tc_mutex, test_mutex_null);
    suite_add_tcase(s, tc_mutex);

    /* Condition variable tests */
    tc_cond = tcase_create("Cond");
    tcase_add_test(tc_cond, test_cond_init_destroy);
    tcase_add_test(tc_cond, test_cond_signal);
    tcase_add_test(tc_cond, test_cond_null);
    suite_add_tcase(s, tc_cond);

    /* Atomic tests */
    tc_atomic = tcase_create("Atomic");
    tcase_add_test(tc_atomic, test_atomic_init_load_store);
    tcase_add_test(tc_atomic, test_atomic_add_sub);
    tcase_add_test(tc_atomic, test_atomic_inc_dec);
    tcase_add_test(tc_atomic, test_atomic_cas);
    suite_add_tcase(s, tc_atomic);

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

    s = thread_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
