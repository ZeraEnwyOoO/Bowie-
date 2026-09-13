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
 * Unit tests for wingo/core/state.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>

#include "wingo/core/state.h"
#include "wingo/util/time.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Callback invocation tracking.
 */
static int on_enter_count = 0;
static int on_exit_count = 0;
static int on_trans_count = 0;
static wingo_state_value_t last_from = WINGO_STATE_NONE;
static wingo_state_value_t last_to = WINGO_STATE_NONE;

static void reset_counters(void)
{
    on_enter_count = 0;
    on_exit_count = 0;
    on_trans_count = 0;
    last_from = WINGO_STATE_NONE;
    last_to = WINGO_STATE_NONE;
}

/*
 * ON_ENTER callback.
 */
static wingo_error_t on_enter_cb(wingo_state_value_t from,
                                 wingo_state_value_t to,
                                 void *userdata)
{
    WINGO_UNUSED(userdata);
    on_enter_count++;
    last_from = from;
    last_to = to;
    return WINGO_SUCCESS;
}

/*
 * ON_EXIT callback.
 */
static wingo_error_t on_exit_cb(wingo_state_value_t from,
                                wingo_state_value_t to,
                                void *userdata)
{
    WINGO_UNUSED(userdata);
    on_exit_count++;
    last_from = from;
    last_to = to;
    return WINGO_SUCCESS;
}

/*
 * ON_TRANS callback.
 */
static wingo_error_t on_trans_cb(wingo_state_value_t from,
                                 wingo_state_value_t to,
                                 void *userdata)
{
    WINGO_UNUSED(userdata);
    on_trans_count++;
    last_from = from;
    last_to = to;
    return WINGO_SUCCESS;
}

/*
 * Rejecting callback (returns error).
 */
static wingo_error_t rejecting_cb(wingo_state_value_t from,
                                  wingo_state_value_t to,
                                  void *userdata)
{
    WINGO_UNUSED(from);
    WINGO_UNUSED(to);
    WINGO_UNUSED(userdata);
    return WINGO_ERR_PERMISSION;
}

/* ============================================================================
 * TEST: STATE MACHINE CREATION
 * ============================================================================ */

START_TEST(test_state_new)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    ck_assert_ptr_nonnull(state);
    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_CREATED);
    ck_assert(!wingo_state_is_strict(state));
    ck_assert_uint_eq(wingo_state_get_transition_count(state), 0);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_new_with_userdata)
{
    int userdata = 42;
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, &userdata);

    ck_assert_ptr_nonnull(state);
    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_CREATED);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_free_null)
{
    /* Should not crash */
    wingo_state_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: STATE NAMES
 * ============================================================================ */

START_TEST(test_state_name)
{
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_NONE), "NONE");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_CREATED), "CREATED");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_INITIALIZING), "INITIALIZING");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_READY), "READY");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_RUNNING), "RUNNING");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_PAUSED), "PAUSED");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_STOPPING), "STOPPING");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_STOPPED), "STOPPED");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_ERROR), "ERROR");
    ck_assert_str_eq(wingo_state_name(WINGO_STATE_DESTROYED), "DESTROYED");
}
END_TEST

START_TEST(test_state_name_unknown)
{
    ck_assert_str_eq(wingo_state_name((wingo_state_value_t)999), "UNKNOWN");
}
END_TEST

/* ============================================================================
 * TEST: STATE QUERY
 * ============================================================================ */

START_TEST(test_state_get)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_READY, NULL);

    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_READY);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_get_null)
{
    ck_assert_int_eq(wingo_state_get(NULL), WINGO_STATE_NONE);
}
END_TEST

START_TEST(test_state_is)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_READY, NULL);

    ck_assert(wingo_state_is(state, WINGO_STATE_READY));
    ck_assert(!wingo_state_is(state, WINGO_STATE_RUNNING));
    ck_assert(!wingo_state_is(NULL, WINGO_STATE_READY));

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_is_any)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_READY, NULL);
    wingo_state_value_t values[] = {WINGO_STATE_READY, WINGO_STATE_RUNNING};

    ck_assert(wingo_state_is_any(state, values, 2));

    values[0] = WINGO_STATE_STOPPED;
    values[1] = WINGO_STATE_ERROR;
    ck_assert(!wingo_state_is_any(state, values, 2));

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: STATE TRANSITIONS
 * ============================================================================ */

START_TEST(test_state_transition)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    rc = wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_READY);
    ck_assert_uint_eq(wingo_state_get_transition_count(state), 1);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_transition_same)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_READY, NULL);
    wingo_error_t rc;

    rc = wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(wingo_state_get_transition_count(state), 0);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_transition_null)
{
    wingo_error_t rc = wingo_state_transition(NULL, WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_state_force)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    /* Force transition that would normally be rejected in strict mode */
    wingo_state_set_strict(state, true);

    rc = wingo_state_force(state, WINGO_STATE_RUNNING);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_RUNNING);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_can_transition)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    /* In non-strict mode, all transitions allowed */
    ck_assert(wingo_state_can_transition(state, WINGO_STATE_READY));

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: TRANSITION RULES
 * ============================================================================ */

START_TEST(test_state_add_transition)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    rc = wingo_state_add_transition(state, WINGO_STATE_CREATED,
                                    WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Now only this transition is allowed */
    ck_assert(wingo_state_can_transition(state, WINGO_STATE_READY));
    ck_assert(!wingo_state_can_transition(state, WINGO_STATE_RUNNING));

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_remove_transition)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    wingo_state_add_transition(state, WINGO_STATE_CREATED,
                               WINGO_STATE_READY);

    rc = wingo_state_remove_transition(state, WINGO_STATE_CREATED,
                                       WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_clear_transitions)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    wingo_state_add_transition(state, WINGO_STATE_CREATED,
                               WINGO_STATE_READY);
    wingo_state_clear_transitions(state);

    /* After clear, all transitions allowed again */
    ck_assert(wingo_state_can_transition(state, WINGO_STATE_RUNNING));

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_strict_mode)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    /* Non-strict by default */
    ck_assert(!wingo_state_is_strict(state));

    /* Enable strict */
    wingo_state_set_strict(state, true);
    ck_assert(wingo_state_is_strict(state));

    /* In strict mode, no transitions allowed without rules */
    ck_assert(!wingo_state_can_transition(state, WINGO_STATE_READY));

    /* Add rule */
    wingo_state_add_transition(state, WINGO_STATE_CREATED,
                               WINGO_STATE_READY);
    ck_assert(wingo_state_can_transition(state, WINGO_STATE_READY));

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_strict_reject)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    wingo_state_set_strict(state, true);
    wingo_state_add_transition(state, WINGO_STATE_CREATED,
                               WINGO_STATE_READY);

    /* Allowed */
    rc = wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Not allowed */
    rc = wingo_state_transition(state, WINGO_STATE_RUNNING);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_STATE);

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: CALLBACKS
 * ============================================================================ */

START_TEST(test_state_on_enter)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    reset_counters();

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_ENTER,
                             WINGO_STATE_READY, on_enter_cb);

    wingo_state_transition(state, WINGO_STATE_READY);

    ck_assert_int_eq(on_enter_count, 1);
    ck_assert_int_eq(last_to, WINGO_STATE_READY);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_on_exit)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_READY, NULL);

    reset_counters();

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_EXIT,
                             WINGO_STATE_READY, on_exit_cb);

    wingo_state_transition(state, WINGO_STATE_RUNNING);

    ck_assert_int_eq(on_exit_count, 1);
    ck_assert_int_eq(last_from, WINGO_STATE_READY);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_on_trans)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    reset_counters();

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_TRANS,
                             WINGO_STATE_NONE, on_trans_cb);

    wingo_state_transition(state, WINGO_STATE_READY);

    ck_assert_int_eq(on_trans_count, 1);
    ck_assert_int_eq(last_from, WINGO_STATE_CREATED);
    ck_assert_int_eq(last_to, WINGO_STATE_READY);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_callback_reject)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_TRANS,
                             WINGO_STATE_NONE, rejecting_cb);

    rc = wingo_state_transition(state, WINGO_STATE_READY);

    /* Callback rejected the transition */
    ck_assert_int_eq(rc, WINGO_ERR_PERMISSION);
    ck_assert_int_eq(wingo_state_get(state), WINGO_STATE_CREATED);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_remove_callback)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_error_t rc;

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_TRANS,
                             WINGO_STATE_NONE, on_trans_cb);

    rc = wingo_state_remove_callback(state, WINGO_STATE_CB_ON_TRANS,
                                     WINGO_STATE_NONE, on_trans_cb);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    reset_counters();
    wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_int_eq(on_trans_count, 0);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_clear_callbacks)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    wingo_state_add_callback(state, WINGO_STATE_CB_ON_TRANS,
                             WINGO_STATE_NONE, on_trans_cb);
    wingo_state_clear_callbacks(state);

    reset_counters();
    wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_int_eq(on_trans_count, 0);

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: HISTORY
 * ============================================================================ */

START_TEST(test_state_get_history)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_state_history_t history[16];
    wingo_size count;

    wingo_state_transition(state, WINGO_STATE_READY);
    wingo_state_transition(state, WINGO_STATE_RUNNING);

    count = wingo_state_get_history(state, history, 16);

    ck_assert_uint_eq(count, 2);
    ck_assert_int_eq(history[0].from, WINGO_STATE_CREATED);
    ck_assert_int_eq(history[0].to, WINGO_STATE_READY);
    ck_assert_int_eq(history[1].from, WINGO_STATE_READY);
    ck_assert_int_eq(history[1].to, WINGO_STATE_RUNNING);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_clear_history)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_state_history_t history[16];
    wingo_size count;

    wingo_state_transition(state, WINGO_STATE_READY);
    wingo_state_clear_history(state);

    count = wingo_state_get_history(state, history, 16);
    ck_assert_uint_eq(count, 0);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_get_transition_count)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    ck_assert_uint_eq(wingo_state_get_transition_count(state), 0);

    wingo_state_transition(state, WINGO_STATE_READY);
    ck_assert_uint_eq(wingo_state_get_transition_count(state), 1);

    wingo_state_transition(state, WINGO_STATE_RUNNING);
    ck_assert_uint_eq(wingo_state_get_transition_count(state), 2);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_get_time_in)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);
    wingo_i64 time_ms;

    /* Transition to READY */
    wingo_state_transition(state, WINGO_STATE_READY);

    /* Sleep a bit */
    wingo_thread_sleep_ms(50);

    /* Time in READY should be at least 50ms */
    time_ms = wingo_state_get_time_in(state, WINGO_STATE_READY);
    ck_assert(time_ms >= 40);  /* Allow some slack */

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: PRINT
 * ============================================================================ */

START_TEST(test_state_print)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    /* Should not crash */
    wingo_state_print(state, stdout);
    wingo_state_print(state, NULL);
    wingo_state_print(NULL, stdout);

    wingo_state_free(state);
}
END_TEST

START_TEST(test_state_print_history)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    wingo_state_transition(state, WINGO_STATE_READY);

    /* Should not crash */
    wingo_state_print_history(state, stdout);
    wingo_state_print_history(state, NULL);
    wingo_state_print_history(NULL, stdout);

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST: THREAD SAFETY
 * ============================================================================ */

START_TEST(test_state_lock)
{
    wingo_state_t *state = wingo_state_new(WINGO_STATE_CREATED, NULL);

    wingo_state_lock(state);
    wingo_state_unlock(state);

    /* NULL is safe */
    wingo_state_lock(NULL);
    wingo_state_unlock(NULL);

    wingo_state_free(state);
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *state_suite(void)
{
    Suite *s;
    TCase *tc_create;
    TCase *tc_names;
    TCase *tc_query;
    TCase *tc_trans;
    TCase *tc_rules;
    TCase *tc_callbacks;
    TCase *tc_history;
    TCase *tc_print;
    TCase *tc_lock;

    s = suite_create("State");

    /* Creation tests */
    tc_create = tcase_create("Create");
    tcase_add_test(tc_create, test_state_new);
    tcase_add_test(tc_create, test_state_new_with_userdata);
    tcase_add_test(tc_create, test_state_free_null);
    suite_add_tcase(s, tc_create);

    /* Name tests */
    tc_names = tcase_create("Names");
    tcase_add_test(tc_names, test_state_name);
    tcase_add_test(tc_names, test_state_name_unknown);
    suite_add_tcase(s, tc_names);

    /* Query tests */
    tc_query = tcase_create("Query");
    tcase_add_test(tc_query, test_state_get);
    tcase_add_test(tc_query, test_state_get_null);
    tcase_add_test(tc_query, test_state_is);
    tcase_add_test(tc_query, test_state_is_any);
    suite_add_tcase(s, tc_query);

    /* Transition tests */
    tc_trans = tcase_create("Transition");
    tcase_add_test(tc_trans, test_state_transition);
    tcase_add_test(tc_trans, test_state_transition_same);
    tcase_add_test(tc_trans, test_state_transition_null);
    tcase_add_test(tc_trans, test_state_force);
    tcase_add_test(tc_trans, test_state_can_transition);
    suite_add_tcase(s, tc_trans);

    /* Rule tests */
    tc_rules = tcase_create("Rules");
    tcase_add_test(tc_rules, test_state_add_transition);
    tcase_add_test(tc_rules, test_state_remove_transition);
    tcase_add_test(tc_rules, test_state_clear_transitions);
    tcase_add_test(tc_rules, test_state_strict_mode);
    tcase_add_test(tc_rules, test_state_strict_reject);
    suite_add_tcase(s, tc_rules);

    /* Callback tests */
    tc_callbacks = tcase_create("Callbacks");
    tcase_add_test(tc_callbacks, test_state_on_enter);
    tcase_add_test(tc_callbacks, test_state_on_exit);
    tcase_add_test(tc_callbacks, test_state_on_trans);
    tcase_add_test(tc_callbacks, test_state_callback_reject);
    tcase_add_test(tc_callbacks, test_state_remove_callback);
    tcase_add_test(tc_callbacks, test_state_clear_callbacks);
    suite_add_tcase(s, tc_callbacks);

    /* History tests */
    tc_history = tcase_create("History");
    tcase_add_test(tc_history, test_state_get_history);
    tcase_add_test(tc_history, test_state_clear_history);
    tcase_add_test(tc_history, test_state_get_transition_count);
    tcase_add_test(tc_history, test_state_get_time_in);
    suite_add_tcase(s, tc_history);

    /* Print tests */
    tc_print = tcase_create("Print");
    tcase_add_test(tc_print, test_state_print);
    tcase_add_test(tc_print, test_state_print_history);
    suite_add_tcase(s, tc_print);

    /* Lock tests */
    tc_lock = tcase_create("Lock");
    tcase_add_test(tc_lock, test_state_lock);
    suite_add_tcase(s, tc_lock);

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

    s = state_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
