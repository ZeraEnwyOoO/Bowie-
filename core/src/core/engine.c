
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

#include "wingo/core/engine.h"
#include "wingo/core/state.h"
#include "wingo/core/event.h"
#include "wingo/core/thread.h"
#include "wingo/util/time.h"
#include "wingo/util/random.h"

#include <string.h>
#include <signal.h>
#include <unistd.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Engine structure.
 *
 * This is the main engine object. It owns all subsystems and
 * coordinates their lifecycle.
 */

struct wingo_engine {
    /* ----- Configuration ----- */
    wingo_engine_config_t   config;

    /* ----- State ----- */
    wingo_engine_state_t    state;
    wingo_state_t          *state_machine;

    /* ----- Identity ----- */
    wingo_id                node_id;
    char                    name[64];

    /* ----- Subsystems ----- */
    wingo_event_loop_t     *event_loop;
    wingo_thread_pool_t    *thread_pool;

    /* ----- Lifecycle ----- */
    wingo_i64               start_time;     /* Monotonic time when started */
    bool                    stop_requested; /* Stop flag */

    /* ----- Statistics ----- */
    wingo_u64               stat_events;
    wingo_u64               stat_ticks;
    wingo_u64               stat_errors;

    /* ----- Synchronization ----- */
    wingo_mutex_t           mutex;
};

/* ============================================================================
 * STATE NAME TABLE
 * ============================================================================ */

static const char *const engine_state_names[] = {
    "CREATED",
    "INITIALIZING",
    "READY",
    "RUNNING",
    "STOPPING",
    "STOPPED",
    "ERROR",
};

/* ============================================================================
 * DEFAULT CONFIGURATION
 * ============================================================================ */

void wingo_engine_config_default(wingo_engine_config_t *config)
{
    if (config == NULL) {
        return;
    }

    memset(config, 0, sizeof(*config));

    config->name             = "bowie";
    config->log_level        = WINGO_LOG_INFO;
    config->log_color        = true;
    config->threads          = WINGO_ENGINE_DEFAULT_THREADS;
    config->max_peers        = WINGO_ENGINE_DEFAULT_MAX_PEERS;
    config->max_connections  = WINGO_ENGINE_DEFAULT_MAX_CONNECTIONS;
    config->tick_ms          = WINGO_ENGINE_DEFAULT_TICK_MS;
    config->idle_ms          = WINGO_ENGINE_DEFAULT_IDLE_MS;
    config->node_id          = NULL;
    config->enable_dht       = true;
    config->enable_nat       = true;
    config->enable_crypto    = true;
    config->enable_tunnel    = false;
    config->enable_gateway   = false;
}

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Set engine state (internal, no lock).
 */
static void engine_set_state(wingo_engine_t *engine, wingo_engine_state_t state)
{
    if (engine == NULL) {
        return;
    }

    if (engine->state != state) {
        WINGO_LOG_DEBUG("Engine state: %s -> %s",
                        engine_state_names[engine->state],
                        engine_state_names[state]);
        engine->state = state;

        /* Also update state machine if present */
        if (engine->state_machine != NULL) {
            wingo_state_force(engine->state_machine,
                              (wingo_state_value_t)state);
        }
    }
}

/*
 * Generate node ID if not set.
 */
static wingo_error_t engine_generate_id(wingo_engine_t *engine)
{
    wingo_error_t rc;

    if (engine->config.node_id != NULL) {
        wingo_id_copy(&engine->node_id, engine->config.node_id);
        return WINGO_SUCCESS;
    }

    rc = wingo_random_id(&engine->node_id);
    if (rc != WINGO_SUCCESS) {
        WINGO_LOG_ERROR("Failed to generate node ID: %s",
                        wingo_error_str(rc));
        return rc;
    }

    return WINGO_SUCCESS;
}

/*
 * Handle SIGINT/SIGTERM.
 *
 * We use a static pointer because signal handlers cannot
 * receive userdata in standard C.
 */
static wingo_engine_t *g_engine_for_signal = NULL;

static void engine_signal_handler(int signum)
{
    WINGO_UNUSED(signum);

    if (g_engine_for_signal != NULL) {
        g_engine_for_signal->stop_requested = true;

        if (g_engine_for_signal->event_loop != NULL) {
            wingo_event_loop_stop(g_engine_for_signal->event_loop);
        }
    }
}

/*
 * Install signal handlers.
 */
static wingo_error_t engine_install_signals(wingo_engine_t *engine)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = engine_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    g_engine_for_signal = engine;

    if (sigaction(SIGINT, &sa, NULL) != 0) {
        WINGO_LOG_ERROR("Failed to install SIGINT handler");
        return WINGO_ERR_GENERIC;
    }

    if (sigaction(SIGTERM, &sa, NULL) != 0) {
        WINGO_LOG_ERROR("Failed to install SIGTERM handler");
        return WINGO_ERR_GENERIC;
    }

    /* Ignore SIGPIPE — we handle EPIPE explicitly */
    signal(SIGPIPE, SIG_IGN);

    return WINGO_SUCCESS;
}

/*
 * Timer callback for engine tick.
 */
static void engine_tick_callback(wingo_event_t *event,
                                 int fd,
                                 wingo_u32 flags,
                                 void *userdata)
{
    wingo_engine_t *engine = (wingo_engine_t *)userdata;

    WINGO_UNUSED(event);
    WINGO_UNUSED(fd);
    WINGO_UNUSED(flags);

    if (engine == NULL) {
        return;
    }

    engine->stat_ticks++;

    /*
     * Re-arm the timer for the next tick.
     * This creates a periodic timer.
     */
    if (!engine->stop_requested) {
        wingo_event_timer_reset(event, engine->config.tick_ms);
    }
}

/* ============================================================================
 * LIFECYCLE
 * ============================================================================ */

wingo_engine_t *wingo_engine_new(const wingo_engine_config_t *config)
{
    wingo_engine_t *engine;

    engine = calloc(1, sizeof(wingo_engine_t));
    if (engine == NULL) {
        return NULL;
    }

    /* Apply configuration */
    if (config != NULL) {
        engine->config = *config;
    } else {
        wingo_engine_config_default(&engine->config);
    }

    /* Apply defaults for zero values */
    if (engine->config.name == NULL) {
        engine->config.name = "bowie";
    }
    if (engine->config.threads <= 0) {
        engine->config.threads = WINGO_ENGINE_DEFAULT_THREADS;
    }
    if (engine->config.max_peers <= 0) {
        engine->config.max_peers = WINGO_ENGINE_DEFAULT_MAX_PEERS;
    }
    if (engine->config.max_connections <= 0) {
        engine->config.max_connections = WINGO_ENGINE_DEFAULT_MAX_CONNECTIONS;
    }
    if (engine->config.tick_ms <= 0) {
        engine->config.tick_ms = WINGO_ENGINE_DEFAULT_TICK_MS;
    }
    if (engine->config.idle_ms <= 0) {
        engine->config.idle_ms = WINGO_ENGINE_DEFAULT_IDLE_MS;
    }

    /* Copy name */
    strncpy(engine->name, engine->config.name, sizeof(engine->name) - 1);
    engine->name[sizeof(engine->name) - 1] = '\0';

    /* Initialize mutex */
    if (wingo_mutex_init(&engine->mutex) != WINGO_SUCCESS) {
        free(engine);
        return NULL;
    }

    engine->state = WINGO_ENGINE_STATE_CREATED;
    engine->start_time = 0;
    engine->stop_requested = false;

    return engine;
}

wingo_error_t wingo_engine_init(wingo_engine_t *engine)
{
    wingo_log_config_t log_config;
    wingo_error_t rc;

    if (engine == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (engine->state != WINGO_ENGINE_STATE_CREATED &&
        engine->state != WINGO_ENGINE_STATE_STOPPED) {
        return WINGO_ERR_INVALID_STATE;
    }

    engine_set_state(engine, WINGO_ENGINE_STATE_INITIALIZING);

    /* ----- Initialize logging ----- */
    memset(&log_config, 0, sizeof(log_config));
    log_config.level = engine->config.log_level != WINGO_LOG_NONE
                     ? engine->config.log_level
                     : WINGO_LOG_INFO;
    log_config.targets = WINGO_LOG_TARGET_STDERR;
    log_config.flags = WINGO_LOG_FLAG_DEFAULT;
    if (engine->config.log_color) {
        log_config.flags |= WINGO_LOG_FLAG_COLOR;
    } else {
        log_config.flags &= ~WINGO_LOG_FLAG_COLOR;
    }
    log_config.flush = true;

    rc = wingo_log_init(&log_config);
    if (rc != WINGO_SUCCESS) {
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return rc;
    }

    WINGO_LOG_INFO("Initializing %s v%s",
                   WINGO_NAME, WINGO_VERSION_STRING);

    /* ----- Initialize state machine ----- */
    engine->state_machine = wingo_state_new(
        (wingo_state_value_t)WINGO_ENGINE_STATE_CREATED,
        engine);

    if (engine->state_machine == NULL) {
        WINGO_LOG_ERROR("Failed to create state machine");
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return WINGO_ERR_NOMEM;
    }

    /* ----- Initialize event loop ----- */
    engine->event_loop = wingo_event_loop_new();
    if (engine->event_loop == NULL) {
        WINGO_LOG_ERROR("Failed to create event loop");
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return WINGO_ERR_NOMEM;
    }

    /* ----- Initialize thread pool ----- */
    engine->thread_pool = wingo_thread_pool_new(engine->config.threads, 0);
    if (engine->thread_pool == NULL) {
        WINGO_LOG_ERROR("Failed to create thread pool");
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return WINGO_ERR_NOMEM;
    }

    rc = wingo_thread_pool_start(engine->thread_pool);
    if (rc != WINGO_SUCCESS) {
        WINGO_LOG_ERROR("Failed to start thread pool: %s",
                        wingo_error_str(rc));
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return rc;
    }

    /* ----- Generate node ID ----- */
    rc = engine_generate_id(engine);
    if (rc != WINGO_SUCCESS) {
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return rc;
    }

    /* Log node ID */
    {
        char hex[WINGO_ID_HEX_SIZE];
        wingo_id_to_hex(&engine->node_id, hex);
        WINGO_LOG_INFO("Node ID: %s", hex);
    }

    /* ----- Install signal handlers ----- */
    rc = engine_install_signals(engine);
    if (rc != WINGO_SUCCESS) {
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return rc;
    }

    /* ----- Reset statistics ----- */
    engine->stat_events = 0;
    engine->stat_ticks = 0;
    engine->stat_errors = 0;

    engine_set_state(engine, WINGO_ENGINE_STATE_READY);

    WINGO_LOG_INFO("Engine initialized (threads=%d, max_peers=%d)",
                   engine->config.threads,
                   engine->config.max_peers);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_engine_run(wingo_engine_t *engine)
{
    wingo_error_t rc;
    wingo_event_t *tick_timer;

    if (engine == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (engine->state != WINGO_ENGINE_STATE_READY &&
        engine->state != WINGO_ENGINE_STATE_STOPPED) {
        return WINGO_ERR_INVALID_STATE;
    }

    /* ----- Start ----- */
    engine_set_state(engine, WINGO_ENGINE_STATE_RUNNING);

    engine->start_time = wingo_time_now();
    engine->stop_requested = false;

    WINGO_LOG_INFO("Engine started (uptime begins)");

    /* ----- Create tick timer ----- */
    tick_timer = wingo_event_add_timer(engine->event_loop,
                                       engine->config.tick_ms,
                                       engine_tick_callback,
                                       engine);

    if (tick_timer == NULL) {
        WINGO_LOG_ERROR("Failed to create tick timer");
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return WINGO_ERR_NOMEM;
    }

    /* ----- Main loop ----- */
    rc = wingo_event_loop_run(engine->event_loop);

    if (rc != WINGO_SUCCESS) {
        WINGO_LOG_ERROR("Event loop failed: %s", wingo_error_str(rc));
        engine->stat_errors++;
        engine_set_state(engine, WINGO_ENGINE_STATE_ERROR);
        return rc;
    }

    /* ----- Stop ----- */
    engine_set_state(engine, WINGO_ENGINE_STATE_STOPPING);

    WINGO_LOG_INFO("Engine stopping...");

    /* Remove tick timer */
    wingo_event_remove_timer(tick_timer);

    engine_set_state(engine, WINGO_ENGINE_STATE_STOPPED);

    WINGO_LOG_INFO("Engine stopped (uptime=%llds, ticks=%llu, events=%llu)",
                   (long long)wingo_engine_get_uptime(engine),
                   (unsigned long long)engine->stat_ticks,
                   (unsigned long long)engine->stat_events);

    return WINGO_SUCCESS;
}

void wingo_engine_stop(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }

    engine->stop_requested = true;

    if (engine->event_loop != NULL) {
        wingo_event_loop_stop(engine->event_loop);
    }

    WINGO_LOG_DEBUG("Engine stop requested");
}

wingo_error_t wingo_engine_shutdown(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (engine->state == WINGO_ENGINE_STATE_CREATED ||
        engine->state == WINGO_ENGINE_STATE_STOPPED) {
        /* Nothing to do */
        return WINGO_SUCCESS;
    }

    /* Stop if running */
    if (engine->state == WINGO_ENGINE_STATE_RUNNING) {
        wingo_engine_stop(engine);
    }

    /* Free thread pool */
    if (engine->thread_pool != NULL) {
        wingo_thread_pool_free(engine->thread_pool);
        engine->thread_pool = NULL;
    }

    /* Free event loop */
    if (engine->event_loop != NULL) {
        wingo_event_loop_free(engine->event_loop);
        engine->event_loop = NULL;
    }

    /* Free state machine */
    if (engine->state_machine != NULL) {
        wingo_state_free(engine->state_machine);
        engine->state_machine = NULL;
    }

    /* Shutdown logging */
    wingo_log_shutdown();

    engine_set_state(engine, WINGO_ENGINE_STATE_STOPPED);

    return WINGO_SUCCESS;
}

void wingo_engine_free(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }

    /* Shutdown if needed */
    wingo_engine_shutdown(engine);

    /* Destroy mutex */
    wingo_mutex_destroy(&engine->mutex);

    /* Free engine */
    free(engine);
}

/* ============================================================================
 * STATE QUERY
 * ============================================================================ */

wingo_engine_state_t wingo_engine_get_state(const wingo_engine_t *engine)
{
    if (engine == NULL) {
        return WINGO_ENGINE_STATE_ERROR;
    }

    return engine->state;
}

const char *wingo_engine_state_name(wingo_engine_state_t state)
{
    if (state >= 0 &&
        (wingo_size)state < WINGO_ARRAY_SIZE(engine_state_names)) {
        return engine_state_names[state];
    }

    return "UNKNOWN";
}

bool wingo_engine_is_running(const wingo_engine_t *engine)
{
    if (engine == NULL) {
        return false;
    }

    return engine->state == WINGO_ENGINE_STATE_RUNNING;
}

bool wingo_engine_is_initialized(const wingo_engine_t *engine)
{
    if (engine == NULL) {
        return false;
    }

    return engine->state != WINGO_ENGINE_STATE_CREATED &&
           engine->state != WINGO_ENGINE_STATE_INITIALIZING;
}

/* ============================================================================
 * ENGINE INFO
 * ============================================================================ */

const char *wingo_engine_get_name(const wingo_engine_t *engine)
{
    if (engine == NULL) {
        return "NULL";
    }

    return engine->name;
}

wingo_error_t wingo_engine_get_id(const wingo_engine_t *engine, wingo_id *out)
{
    if (engine == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    wingo_id_copy(out, &engine->node_id);

    return WINGO_SUCCESS;
}

wingo_i64 wingo_engine_get_uptime(const wingo_engine_t *engine)
{
    wingo_i64 now;

    if (engine == NULL || engine->start_time == 0) {
        return 0;
    }

    now = wingo_time_now();

    return now - engine->start_time;
}

wingo_error_t wingo_engine_get_stats(const wingo_engine_t *engine,
                                     wingo_u64 *events,
                                     wingo_u64 *ticks,
                                     wingo_u64 *errors)
{
    if (engine == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (events != NULL) {
        *events = engine->stat_events;
    }
    if (ticks != NULL) {
        *ticks = engine->stat_ticks;
    }
    if (errors != NULL) {
        *errors = engine->stat_errors;
    }

    return WINGO_SUCCESS;
}

void wingo_engine_reset_stats(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }

    engine->stat_events = 0;
    engine->stat_ticks = 0;
    engine->stat_errors = 0;
}

/* ============================================================================
 * ENGINE CONFIGURATION (RUNTIME)
 * ============================================================================ */

wingo_error_t wingo_engine_get_config(const wingo_engine_t *engine,
                                      wingo_engine_config_t *out)
{
    if (engine == NULL || out == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    *out = engine->config;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_engine_set_log_level(wingo_engine_t *engine,
                                         wingo_log_level_t level)
{
    if (engine == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    wingo_log_set_level(level);
    engine->config.log_level = level;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SUBSYSTEM ACCESS
 * ============================================================================ */

wingo_state_t *wingo_engine_get_state_machine(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return NULL;
    }

    return engine->state_machine;
}

wingo_event_loop_t *wingo_engine_get_event_loop(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return NULL;
    }

    return engine->event_loop;
}

wingo_thread_pool_t *wingo_engine_get_thread_pool(wingo_engine_t *engine)
{
    if (engine == NULL) {
        return NULL;
    }

    return engine->thread_pool;
}

/* ============================================================================
 * UTILITY
 * ============================================================================ */

void wingo_engine_print_status(const wingo_engine_t *engine, FILE *f)
{
    char id_hex[WINGO_ID_HEX_SIZE];

    if (f == NULL) {
        f = stderr;
    }

    if (engine == NULL) {
        fprintf(f, "Engine: (null)\n");
        return;
    }

    wingo_id_to_hex(&engine->node_id, id_hex);

    fprintf(f, "Engine Status:\n");
    fprintf(f, "  Name:        %s\n", engine->name);
    fprintf(f, "  State:       %s\n",
            wingo_engine_state_name(engine->state));
    fprintf(f, "  Node ID:     %s\n", id_hex);
    fprintf(f, "  Uptime:      %llds\n",
            (long long)wingo_engine_get_uptime(engine));
    fprintf(f, "  Threads:     %d\n", engine->config.threads);
    fprintf(f, "  Max Peers:   %d\n", engine->config.max_peers);
    fprintf(f, "  Tick:        %d ms\n", engine->config.tick_ms);

    if (engine->thread_pool != NULL) {
        fprintf(f, "  Pending:     %zu\n",
                wingo_thread_pool_pending(engine->thread_pool));
        fprintf(f, "  Active:      %zu\n",
                wingo_thread_pool_active(engine->thread_pool));
    }
}

void wingo_engine_print_stats(const wingo_engine_t *engine, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (engine == NULL) {
        fprintf(f, "Engine: (null)\n");
        return;
    }

    fprintf(f, "Engine Statistics:\n");
    fprintf(f, "  Events:      %llu\n",
            (unsigned long long)engine->stat_events);
    fprintf(f, "  Ticks:       %llu\n",
            (unsigned long long)engine->stat_ticks);
    fprintf(f, "  Errors:      %llu\n",
            (unsigned long long)engine->stat_errors);
    fprintf(f, "  Uptime:      %llds\n",
            (long long)wingo_engine_get_uptime(engine));
}
