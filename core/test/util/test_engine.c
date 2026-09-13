
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
 * Unit tests for wingo/core/engine.h
 *
 * Uses Check Framework (https://libcheck.github.io/check/)
 */

#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include "wingo/core/engine.h"
#include "wingo/util/time.h"

/* ============================================================================
 * TEST HELPERS
 * ============================================================================ */

/*
 * Test engine name.
 */
#define TEST_ENGINE_NAME "test-engine"

/*
 * Helper: create default engine.
 */
static wingo_engine_t *make_engine(void)
{
    wingo_engine_config_t config;

    wingo_engine_config_default(&config);
    config.name = TEST_ENGINE_NAME;
    config.log_level = WINGO_LOG_NONE;  /* Silent during tests */
    config.threads = 2;

    return wingo_engine_new(&config);
}

/*
 * Thread function for testing thread pool.
 */
static void test_task_func(void *arg)
{
    int *counter = (int *)arg;
    (*counter)++;
}

/* ============================================================================
 * TEST: CONFIGURATION
 * ============================================================================ */

START_TEST(test_engine_config_default)
{
    wingo_engine_config_t config;

    wingo_engine_config_default(&config);

    ck_assert_str_eq(config.name, "bowie");
    ck_assert_int_eq(config.log_level, WINGO_LOG_INFO);
    ck_assert(config.log_color);
    ck_assert_int_eq(config.threads, WINGO_ENGINE_DEFAULT_THREADS);
    ck_assert_int_eq(config.max_peers, WINGO_ENGINE_DEFAULT_MAX_PEERS);
    ck_assert_int_eq(config.max_connections, WINGO_ENGINE_DEFAULT_MAX_CONNECTIONS);
    ck_assert_int_eq(config.tick_ms, WINGO_ENGINE_DEFAULT_TICK_MS);
    ck_assert_int_eq(config.idle_ms, WINGO_ENGINE_DEFAULT_IDLE_MS);
    ck_assert_ptr_null(config.node_id);
    ck_assert(config.enable_dht);
    ck_assert(config.enable_nat);
    ck_assert(config.enable_crypto);
    ck_assert(!config.enable_tunnel);
    ck_assert(!config.enable_gateway);
}
END_TEST

START_TEST(test_engine_config_default_null)
{
    /* Should not crash */
    wingo_engine_config_default(NULL);
}
END_TEST

/* ============================================================================
 * TEST: LIFECYCLE — CREATION
 * ============================================================================ */

START_TEST(test_engine_new_default)
{
    wingo_engine_t *engine = wingo_engine_new(NULL);

    ck_assert_ptr_nonnull(engine);
    ck_assert_int_eq(wingo_engine_get_state(engine),
                     WINGO_ENGINE_STATE_CREATED);
    ck_assert(!wingo_engine_is_running(engine));
    ck_assert(!wingo_engine_is_initialized(engine));

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_new_with_config)
{
    wingo_engine_config_t config;
    wingo_engine_t *engine;

    wingo_engine_config_default(&config);
    config.name = TEST_ENGINE_NAME;
    config.threads = 2;
    config.max_peers = 32;

    engine = wingo_engine_new(&config);

    ck_assert_ptr_nonnull(engine);
    ck_assert_str_eq(wingo_engine_get_name(engine), TEST_ENGINE_NAME);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_free_null)
{
    /* Should not crash */
    wingo_engine_free(NULL);
}
END_TEST

/* ============================================================================
 * TEST: LIFECYCLE — INIT
 * ============================================================================ */

START_TEST(test_engine_init)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    ck_assert_ptr_nonnull(engine);

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(wingo_engine_get_state(engine),
                     WINGO_ENGINE_STATE_READY);
    ck_assert(wingo_engine_is_initialized(engine));
    ck_assert(!wingo_engine_is_running(engine));

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_init_twice)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /*
     * Second init: engine is in READY state, not CREATED or STOPPED,
     * so it should return INVALID_STATE.
     */
    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_STATE);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_init_null)
{
    wingo_error_t rc = wingo_engine_init(NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: LIFECYCLE — SHUTDOWN
 * ============================================================================ */

START_TEST(test_engine_shutdown)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_engine_shutdown(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(wingo_engine_get_state(engine),
                     WINGO_ENGINE_STATE_STOPPED);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_shutdown_null)
{
    wingo_error_t rc = wingo_engine_shutdown(NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

/* ============================================================================
 * TEST: LIFECYCLE — RUN/STOP
 * ============================================================================ */

/*
 * Thread function: stop engine after delay.
 */
static void *stop_engine_thread(void *arg)
{
    wingo_engine_t *engine = (wingo_engine_t *)arg;

    /* Wait a bit for engine to start */
    wingo_thread_sleep_ms(100);

    /* Stop engine */
    wingo_engine_stop(engine);

    return NULL;
}

START_TEST(test_engine_run_stop)
{
    wingo_engine_t *engine = make_engine();
    pthread_t stop_thread;
    wingo_error_t rc;
    int thread_rc;

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Start stop-thread */
    thread_rc = pthread_create(&stop_thread, NULL,
                               stop_engine_thread, engine);
    ck_assert_int_eq(thread_rc, 0);

    /* Run engine (blocks until stopped) */
    rc = wingo_engine_run(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* Wait for stop-thread */
    pthread_join(stop_thread, NULL);

    ck_assert_int_eq(wingo_engine_get_state(engine),
                     WINGO_ENGINE_STATE_STOPPED);
    ck_assert(!wingo_engine_is_running(engine));

    wingo_engine_free(engine);
}
END_TEST

/* ============================================================================
 * TEST: STATE QUERY
 * ============================================================================ */

START_TEST(test_engine_state_name)
{
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_CREATED),
                     "CREATED");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_INITIALIZING),
                     "INITIALIZING");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_READY),
                     "READY");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_RUNNING),
                     "RUNNING");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_STOPPING),
                     "STOPPING");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_STOPPED),
                     "STOPPED");
    ck_assert_str_eq(wingo_engine_state_name(WINGO_ENGINE_STATE_ERROR),
                     "ERROR");
}
END_TEST

START_TEST(test_engine_get_state_null)
{
    ck_assert_int_eq(wingo_engine_get_state(NULL),
                     WINGO_ENGINE_STATE_ERROR);
}
END_TEST

START_TEST(test_engine_is_running_null)
{
    ck_assert(!wingo_engine_is_running(NULL));
}
END_TEST

START_TEST(test_engine_is_initialized_null)
{
    ck_assert(!wingo_engine_is_initialized(NULL));
}
END_TEST

/* ============================================================================
 * TEST: ENGINE INFO
 * ============================================================================ */

START_TEST(test_engine_get_name)
{
    wingo_engine_t *engine = make_engine();

    ck_assert_str_eq(wingo_engine_get_name(engine), TEST_ENGINE_NAME);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_name_null)
{
    ck_assert_str_eq(wingo_engine_get_name(NULL), "NULL");
}
END_TEST

START_TEST(test_engine_get_id)
{
    wingo_engine_t *engine = make_engine();
    wingo_id id;
    wingo_error_t rc;

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    rc = wingo_engine_get_id(engine, &id);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    /* ID should not be all zeros */
    ck_assert(!wingo_id_is_zero(&id));

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_id_null_engine)
{
    wingo_id id;
    wingo_error_t rc = wingo_engine_get_id(NULL, &id);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_engine_get_id_null_out)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc = wingo_engine_get_id(engine, NULL);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_uptime)
{
    wingo_engine_t *engine = make_engine();

    /* Before init, uptime = 0 */
    ck_assert_int_eq(wingo_engine_get_uptime(engine), 0);

    wingo_engine_free(engine);
}
END_TEST

/* ============================================================================
 * TEST: STATISTICS
 * ============================================================================ */

START_TEST(test_engine_get_stats)
{
    wingo_engine_t *engine = make_engine();
    wingo_u64 events, ticks, errors;
    wingo_error_t rc;

    rc = wingo_engine_get_stats(engine, &events, &ticks, &errors);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_uint_eq(events, 0);
    ck_assert_uint_eq(ticks, 0);
    ck_assert_uint_eq(errors, 0);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_stats_null)
{
    wingo_u64 events, ticks, errors;
    wingo_error_t rc = wingo_engine_get_stats(NULL, &events, &ticks, &errors);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_engine_get_stats_null_outputs)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    /* Should not crash with NULL outputs */
    rc = wingo_engine_get_stats(engine, NULL, NULL, NULL);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_reset_stats)
{
    wingo_engine_t *engine = make_engine();

    /* Should not crash */
    wingo_engine_reset_stats(engine);
    wingo_engine_reset_stats(NULL);

    wingo_engine_free(engine);
}
END_TEST

/* ============================================================================
 * TEST: CONFIGURATION (RUNTIME)
 * ============================================================================ */

START_TEST(test_engine_get_config)
{
    wingo_engine_t *engine = make_engine();
    wingo_engine_config_t config;
    wingo_error_t rc;

    rc = wingo_engine_get_config(engine, &config);
    ck_assert_int_eq(rc, WINGO_SUCCESS);
    ck_assert_int_eq(config.threads, 2);

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_config_null)
{
    wingo_engine_config_t config;
    wingo_error_t rc = wingo_engine_get_config(NULL, &config);
    ck_assert_int_eq(rc, WINGO_ERR_INVALID_ARG);
}
END_TEST

START_TEST(test_engine_set_log_level)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    rc = wingo_engine_set_log_level(engine, WINGO_LOG_ERROR);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    wingo_engine_free(engine);
}
END_TEST

/* ============================================================================
 * TEST: SUBSYSTEM ACCESS
 * ============================================================================ */

START_TEST(test_engine_get_subsystems)
{
    wingo_engine_t *engine = make_engine();
    wingo_error_t rc;

    rc = wingo_engine_init(engine);
    ck_assert_int_eq(rc, WINGO_SUCCESS);

    ck_assert_ptr_nonnull(wingo_engine_get_state_machine(engine));
    ck_assert_ptr_nonnull(wingo_engine_get_event_loop(engine));
    ck_assert_ptr_nonnull(wingo_engine_get_thread_pool(engine));

    wingo_engine_free(engine);
}
END_TEST

START_TEST(test_engine_get_subsystems_null)
{
    ck_assert_ptr_null(wingo_engine_get_state_machine(NULL));
    ck_assert_ptr_null(wingo_engine_get_event_loop(NULL));
    ck_assert_ptr_null(wingo_engine_get_thread_pool(NULL));
}
END_TEST

/* ============================================================================
 * TEST SUITE
 * ============================================================================ */

static Suite *engine_suite(void)
{
    Suite *s;
    TCase *tc_config;
    TCase *tc_lifecycle;
    TCase *tc_state;
    TCase *tc_info;
    TCase *tc_stats;
    TCase *tc_subsystems;

    s = suite_create("Engine");

    /* Configuration tests */
    tc_config = tcase_create("Config");
    tcase_add_test(tc_config, test_engine_config_default);
    tcase_add_test(tc_config, test_engine_config_default_null);
    tcase_add_test(tc_config, test_engine_get_config);
    tcase_add_test(tc_config, test_engine_get_config_null);
    tcase_add_test(tc_config, test_engine_set_log_level);
    suite_add_tcase(s, tc_config);

    /* Lifecycle tests */
    tc_lifecycle = tcase_create("Lifecycle");
    tcase_add_test(tc_lifecycle, test_engine_new_default);
    tcase_add_test(tc_lifecycle, test_engine_new_with_config);
    tcase_add_test(tc_lifecycle, test_engine_free_null);
    tcase_add_test(tc_lifecycle, test_engine_init);
    tcase_add_test(tc_lifecycle, test_engine_init_twice);
    tcase_add_test(tc_lifecycle, test_engine_init_null);
    tcase_add_test(tc_lifecycle, test_engine_shutdown);
    tcase_add_test(tc_lifecycle, test_engine_shutdown_null);
    tcase_add_test(tc_lifecycle, test_engine_run_stop);
    suite_add_tcase(s, tc_lifecycle);

    /* State tests */
    tc_state = tcase_create("State");
    tcase_add_test(tc_state, test_engine_state_name);
    tcase_add_test(tc_state, test_engine_get_state_null);
    tcase_add_test(tc_state, test_engine_is_running_null);
    tcase_add_test(tc_state, test_engine_is_initialized_null);
    suite_add_tcase(s, tc_state);

    /* Info tests */
    tc_info = tcase_create("Info");
    tcase_add_test(tc_info, test_engine_get_name);
    tcase_add_test(tc_info, test_engine_get_name_null);
    tcase_add_test(tc_info, test_engine_get_id);
    tcase_add_test(tc_info, test_engine_get_id_null_engine);
    tcase_add_test(tc_info, test_engine_get_id_null_out);
    tcase_add_test(tc_info, test_engine_get_uptime);
    suite_add_tcase(s, tc_info);

    /* Statistics tests */
    tc_stats = tcase_create("Stats");
    tcase_add_test(tc_stats, test_engine_get_stats);
    tcase_add_test(tc_stats, test_engine_get_stats_null);
    tcase_add_test(tc_stats, test_engine_get_stats_null_outputs);
    tcase_add_test(tc_stats, test_engine_reset_stats);
    suite_add_tcase(s, tc_stats);

    /* Subsystem tests */
    tc_subsystems = tcase_create("Subsystems");
    tcase_add_test(tc_subsystems, test_engine_get_subsystems);
    tcase_add_test(tc_subsystems, test_engine_get_subsystems_null);
    suite_add_tcase(s, tc_subsystems);

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

    s = engine_suite();
    sr = srunner_create(s);

    srunner_run_all(sr, CK_NORMAL);

    number_failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
