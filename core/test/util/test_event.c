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
 * Unit tests for wingo/core/event.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>

#include "wingo/core/event.h"
#include "wingo/util/time.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Counter for callback invocations.
 */
static int callback_count = 0;
static int last_fd = -1;
static wingo_u32 last_flags = 0;
static void *last_userdata = NULL;

/*
 * Test callback.
 */
static void test_callback(wingo_event_t *event,
                          int fd,
                          wingo_u32 flags,
                          void *userdata)
{
    WINGO_UNUSED(event);

    callback_count++;
    last_fd = fd;
    last_flags = flags;
    last_userdata = userdata;
}

/*
 * Reset counters.
 */
static void reset_counters(void)
{
    callback_count = 0;
    last_fd = -1;
    last_flags = 0;
    last_userdata = NULL;
}

/*
 * Create a socket pair for testing.
 */
static int make_socket_pair(int fds[2])
{
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0) {
        return -1;
    }

    /* Set non-blocking */
    fcntl(fds[0], F_SETFL, O_NONBLOCK);
    fcntl(fds[1], F_SETFL, O_NONBLOCK);

    return 0;
}

/*
 * Thread function: write to fd after delay.
 */
typedef struct {
    int fd;
    const char *data;
    int delay_ms;
} writer_args_t;

static void *writer_thread(void *arg)
{
    writer_args_t *args = (writer_args_t *)arg;

    wingo_thread_sleep_ms(args->delay_ms);
    write(args->fd, args->data, strlen(args->data));

    return NULL;
}

/* ============================================================================
 * TEST: EVENT FLAGS
 * ============================================================================ */

START_TEST(test_event_flags_values)
{
    /* Verify flags are distinct */
    ck_assert_uint_ne(WINGO_EVENT_NONE, WINGO_EVENT_READ);
    ck_assert_uint_ne(WINGO_EVENT_READ, WINGO_EVENT_WRITE);
    ck_assert_uint_ne(WINGO_EVENT_WRITE, WINGO_EVENT_ERROR);
    ck_assert_uint_ne(WINGO_EVENT_ERROR, WINGO_EVENT_HANGUP);
    ck_assert_uint_ne(WINGO_EVENT_HANGUP, WINGO_EVENT_EDGE);
    ck_assert_uint_ne(WINGO_EVENT_EDGE, WINGO_EVENT_ONESHOT);
    ck_assert_uint_ne(WINGO_EVENT_ONESHOT, WINGO_EVENT_PRIORITY);

    /* Verify flags are powers of two (single bit) */
    ck_assert_uint_eq(WINGO_EVENT_READ & (WINGO_EVENT_READ - 1), 0);
    ck_assert_uint_eq(WINGO_EVENT_WRITE & (WINGO_EVENT_WRITE - 1), 0);
    ck_assert_uint_eq(WINGO_EVENT_ERROR & (WINGO_EVENT_ERROR - 1), 0);
}
END_TEST

/* ============================================================================
 * TEST: EVENT LOOP CREATION
 * ============================================================================ */

START_TEST(test_event_loop_new)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    ck_assert_ptr_nonnull(loop);
    ck_assert(!wingo_event_loop_is_running(loop));
    ck_assert_uint_eq(wingo_event_loop_count(loop), 0);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_loop_free_null)
{
    /* Should not crash */
    wingo_event_loop_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: FD EVENTS
 * ============================================================================ */

START_TEST(test_event_add_fd)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    int fds[2];
    wingo_event_t *event;

    ck_assert_int_eq(make_socket_pair(fds), 0);

    event = wingo_event_add_fd(loop, fds[0],
                               WINGO_EVENT_READ,
                               test_callback, NULL);

    ck_assert_ptr_nonnull(event);
    ck_assert_int_eq(wingo_event_get_type(event), WINGO_EVENT_TYPE_FD);
    ck_assert_int_eq(wingo_event_get_fd(event), fds[0]);
    ck_assert_uint_eq(wingo_event_get_flags(event), WINGO_EVENT_READ);
    ck_assert_uint_eq(wingo_event_loop_count(loop), 1);

    close(fds[0]);
    close(fds[1]);
    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_add_fd_invalid)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    /* NULL loop */
    ck_assert_ptr_null(wingo_event_add_fd(NULL, 0,
                                          WINGO_EVENT_READ,
                                          test_callback, NULL));

    /* Invalid fd */
    ck_assert_ptr_null(wingo_event_add_fd(loop, -1,
                                          WINGO_EVENT_READ,
                                          test_callback, NULL));

    /* NULL callback */
    ck_assert_ptr_null(wingo_event_add_fd(loop, 0,
                                          WINGO_EVENT_READ,
                                          NULL, NULL));

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_fd_fires)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    int fds[2];
    wingo_event_t *event;

    ck_assert_int_eq(make_socket_pair(fds), 0);

    reset_counters();

    event = wingo_event_add_fd(loop, fds[0],
                               WINGO_EVENT_READ,
                               test_callback, NULL);
    ck_assert_ptr_nonnull(event);

    /* Write to the other end */
    write(fds[1], "hello", 5);

    /* Run one iteration */
    wingo_event_loop_run_once(loop, 100);

    /* Callback should have fired */
    ck_assert_int_eq(callback_count, 1);
    ck_assert_int_eq(last_fd, fds[0]);
    ck_assert(last_flags & WINGO_EVENT_READ);

    close(fds[0]);
    close(fds[1]);
    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_remove_fd)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    int fds[2];
    wingo_event_t *event;
    wingo_error_t rc;

    ck_assert_int_eq(make_socket_pair(fds), 0);

    event = wingo_event_add_fd(loop, fds[0],
                               WINGO_EVENT_READ,
                               test_callback, NULL);

    rc = wingo_event_remove_fd(event);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    close(fds[0]);
    close(fds[1]);
    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST: TIMER EVENTS
 * ============================================================================ */

START_TEST(test_event_add_timer)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_event_t *event;

    event = wingo_event_add_timer(loop, 100,
                                  test_callback, NULL);

    ck_assert_ptr_nonnull(event);
    ck_assert_int_eq(wingo_event_get_type(event), WINGO_EVENT_TYPE_TIMER);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_add_timer_invalid)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    /* NULL loop */
    ck_assert_ptr_null(wingo_event_add_timer(NULL, 100,
                                             test_callback, NULL));

    /* NULL callback */
    ck_assert_ptr_null(wingo_event_add_timer(loop, 100, NULL, NULL));

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_timer_fires)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    reset_counters();

    wingo_event_add_timer(loop, 50, test_callback, NULL);

    /* Run loop until timer fires */
    wingo_event_loop_run_once(loop, 200);

    /* Callback should have fired */
    ck_assert_int_eq(callback_count, 1);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_timer_reset)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_event_t *event;
    wingo_error_t rc;

    event = wingo_event_add_timer(loop, 50, test_callback, NULL);
    ck_assert_ptr_nonnull(event);

    rc = wingo_event_timer_reset(event, 100);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_remove_timer)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_event_t *event;
    wingo_error_t rc;

    event = wingo_event_add_timer(loop, 1000, test_callback, NULL);

    rc = wingo_event_remove_timer(event);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST: USER EVENTS
 * ============================================================================ */

START_TEST(test_event_post)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_error_t rc;

    reset_counters();

    rc = wingo_event_post(loop, test_callback, NULL);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Run one iteration */
    wingo_event_loop_run_once(loop, 100);

    /* Callback should have fired */
    ck_assert_int_eq(callback_count, 1);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_post_invalid)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    /* NULL loop */
    ck_assert_int_eq(wingo_event_post(NULL, test_callback, NULL),
                     WINGO_ERR_INVALID_ARG);

    /* NULL callback */
    ck_assert_int_eq(wingo_event_post(loop, NULL, NULL),
                     WINGO_ERR_INVALID_ARG);

    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST: WAKEUP
 * ============================================================================ */

START_TEST(test_event_wakeup)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_error_t rc;

    rc = wingo_event_wakeup(loop);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_wakeup_null)
{
    ck_assert_int_eq(wingo_event_wakeup(NULL), WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: EVENT QUERY
 * ============================================================================ */

START_TEST(test_event_get_userdata)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    int fds[2];
    wingo_event_t *event;
    int userdata = 42;

    ck_assert_int_eq(make_socket_pair(fds), 0);

    event = wingo_event_add_fd(loop, fds[0],
                               WINGO_EVENT_READ,
                               test_callback, &userdata);

    ck_assert_ptr_eq(wingo_event_get_userdata(event), &userdata);

    close(fds[0]);
    close(fds[1]);
    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST: STATISTICS
 * ============================================================================ */

START_TEST(test_event_loop_get_stats)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    wingo_u64 events, timers, wakeups;
    wingo_error_t rc;

    rc = wingo_event_loop_get_stats(loop, &events, &timers, &wakeups);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(events, 0);
    ck_assert_uint_eq(timers, 0);
    ck_assert_uint_eq(wakeups, 0);

    wingo_event_loop_free(loop);
}
END_TEST

START_TEST(test_event_loop_get_stats_null)
{
    wingo_u64 events, timers, wakeups;
    wingo_error_t rc = wingo_event_loop_get_stats(NULL, &events, &timers, &wakeups);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_event_loop_reset_stats)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();

    /* Should not crash */
    wingo_event_loop_reset_stats(loop);
    wingo_event_loop_reset_stats(NULL);

    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST: RUN LOOP
 * ============================================================================ */

/*
 * Thread: stop loop after delay.
 */
static void *stop_loop_thread(void *arg)
{
    wingo_event_loop_t *loop = (wingo_event_loop_t *)arg;

    wingo_thread_sleep_ms(100);
    wingo_event_loop_stop(loop);

    return NULL;
}

START_TEST(test_event_loop_run_stop)
{
    wingo_event_loop_t *loop = wingo_event_loop_new();
    pthread_t stop_thread;
    wingo_error_t rc;
    int thread_rc;

    /* Start stop-thread */
    thread_rc = pthread_create(&stop_thread, NULL,
                               stop_loop_thread, loop);
    ck_assert_int_eq(thread_rc, 0);

    /* Run loop (blocks until stopped) */
    rc = wingo_event_loop_run(loop);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Wait for stop-thread */
    pthread_join(stop_thread, NULL);

    ck_assert(!wingo_event_loop_is_running(loop));

    wingo_event_loop_free(loop);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *event_suite(void)
{
    Suite *s;
    TCase *tc_flags;
    TCase *tc_loop;
    TCase *tc_fd;
    TCase *tc_timer;
    TCase *tc_user;
    TCase *tc_query;
    TCase *tc_stats;
    TCase *tc_run;

    s = suite_create("Event");

    /* Flags tests */
    tc_flags = tcase_create("Flags");
    tcase_add_test(tc_flags, test_event_flags_values);
    suite_add_tcase(s, tc_flags);

    /* Loop tests */
    tc_loop = tcase_create("Loop");
    tcase_add_test(tc_loop, test_event_loop_new);
    tcase_add_test(tc_loop, test_event_loop_free_null);
    suite_add_tcase(s, tc_loop);

    /* FD tests */
    tc_fd = tcase_create("FD");
    tcase_add_test(tc_fd, test_event_add_fd);
    tcase_add_test(tc_fd, test_event_add_fd_invalid);
    tcase_add_test(tc_fd, test_event_fd_fires);
    tcase_add_test(tc_fd, test_event_remove_fd);
    suite_add_tcase(s, tc_fd);

    /* Timer tests */
    tc_timer = tcase_create("Timer");
    tcase_add_test(tc_timer, test_event_add_timer);
    tcase_add_test(tc_timer, test_event_add_timer_invalid);
    tcase_add_test(tc_timer, test_event_timer_fires);
    tcase_add_test(tc_timer, test_event_timer_reset);
    tcase_add_test(tc_timer, test_event_remove_timer);
    suite_add_tcase(s, tc_timer);

    /* User event tests */
    tc_user = tcase_create("User");
    tcase_add_test(tc_user, test_event_post);
    tcase_add_test(tc_user, test_event_post_invalid);
    tcase_add_test(tc_user, test_event_wakeup);
    tcase_add_test(tc_user, test_event_wakeup_null);
    suite_add_tcase(s, tc_user);

    /* Query tests */
    tc_query = tcase_create("Query");
    tcase_add_test(tc_query, test_event_get_userdata);
    suite_add_tcase(s, tc_query);

    /* Statistics tests */
    tc_stats = tcase_create("Stats");
    tcase_add_test(tc_stats, test_event_loop_get_stats);
    tcase_add_test(tc_stats, test_event_loop_get_stats_null);
    tcase_add_test(tc_stats, test_event_loop_reset_stats);
    suite_add_tcase(s, tc_stats);

    /* Run loop tests */
    tc_run = tcase_create("Run");
    tcase_add_test(tc_run, test_event_loop_run_stop);
    suite_add_tcase(s, tc_run);

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

    s = event_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
