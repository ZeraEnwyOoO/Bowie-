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

#ifndef WINGO_CORE_ENGINE_H
#define WINGO_CORE_ENGINE_H

/*
 * ============================================================================
 * WINGO CORE ENGINE
 * ============================================================================
 *
 * The Core Engine is the heart of Bowie. It:
 *   - Manages the overall lifecycle (init, start, stop, shutdown)
 *   - Owns the event loop
 *   - Owns the state machine
 *   - Owns the thread pool
 *   - Coordinates all subsystems (network, crypto, tunnel, etc.)
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                      ENGINE                                 │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │   State     │  │   Event     │  │   Thread    │        │
 *   │   │  Machine    │  │    Loop     │  │    Pool     │        │
 *   │   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
 *   │          │                │                │                │
 *   │          └────────────────┼────────────────┘                │
 *   │                           │                                 │
 *   │                    ┌──────▼──────┐                          │
 *   │                    │   ENGINE    │                          │
 *   │                    │    CORE     │                          │
 *   │                    └──────┬──────┘                          │
 *   │                           │                                 │
 *   │          ┌────────────────┼────────────────┐                │
 *   │          │                │                │                │
 *   │   ┌──────▼──────┐  ┌──────▼──────┐  ┌──────▼──────┐        │
 *   │   │   Network   │  │   Crypto    │  │   Tunnel    │        │
 *   │   │  (Phase 3)  │  │  (Phase 5)  │  │  (Phase 6)  │        │
 *   │   └─────────────┘  └─────────────┘  └─────────────┘        │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * ENGINE STATES
 * ============================================================================ */

/*
 * Engine lifecycle states.
 *
 * State transitions:
 *
 *   CREATED ──> INITIALIZING ──> READY ──> RUNNING ──> STOPPING ──> STOPPED
 *      │              │            │          │            │
 *      │              │            │          │            │
 *      └──────────────┴────────────┴──────────┴────────────┴──> ERROR
 *
 * Description:
 *   CREATED      — Engine struct allocated, nothing initialized
 *   INITIALIZING — Subsystems being initialized
 *   READY        — Initialized, ready to start
 *   RUNNING      — Main loop is running
 *   STOPPING     — Shutdown requested, draining
 *   STOPPED      — Stopped, ready to free or restart
 *   ERROR        — Fatal error occurred
 */

typedef enum {
    WINGO_ENGINE_STATE_CREATED      = 0,
    WINGO_ENGINE_STATE_INITIALIZING = 1,
    WINGO_ENGINE_STATE_READY        = 2,
    WINGO_ENGINE_STATE_RUNNING      = 3,
    WINGO_ENGINE_STATE_STOPPING     = 4,
    WINGO_ENGINE_STATE_STOPPED      = 5,
    WINGO_ENGINE_STATE_ERROR        = 6,
} wingo_engine_state_t;

/* ============================================================================
 * ENGINE CONFIGURATION
 * ============================================================================ */

/*
 * Default configuration values.
 */

#define WINGO_ENGINE_DEFAULT_MAX_PEERS       64
#define WINGO_ENGINE_DEFAULT_MAX_CONNECTIONS 128
#define WINGO_ENGINE_DEFAULT_THREADS         4
#define WINGO_ENGINE_DEFAULT_TICK_MS         10
#define WINGO_ENGINE_DEFAULT_IDLE_MS         1000

/*
 * Engine configuration.
 *
 * This struct is passed to wingo_engine_new() to configure
 * the engine before initialization.
 *
 * All fields have sensible defaults. Set a field to 0 to use
 * the default value (except for pointers, which must be NULL).
 */

typedef struct {
    /* ----- General ----- */

    /*
     * Engine name (for logging and identification).
     * NULL = use "bowie".
     */
    const char *name;

    /*
     * Log level for the engine.
     * WINGO_LOG_NONE = use default (WINGO_LOG_INFO).
     */
    wingo_log_level_t log_level;

    /*
     * Enable colored log output.
     * false = no colors.
     */
    bool log_color;

    /* ----- Threading ----- */

    /*
     * Number of worker threads in the thread pool.
     * 0 = use WINGO_ENGINE_DEFAULT_THREADS.
     */
    int threads;

    /* ----- Networking ----- */

    /*
     * Maximum number of peers to track.
     * 0 = use WINGO_ENGINE_DEFAULT_MAX_PEERS.
     */
    int max_peers;

    /*
     * Maximum number of simultaneous connections.
     * 0 = use WINGO_ENGINE_DEFAULT_MAX_CONNECTIONS.
     */
    int max_connections;

    /* ----- Timing ----- */

    /*
     * Main loop tick interval in milliseconds.
     * This is how often the engine wakes up to process events.
     * 0 = use WINGO_ENGINE_DEFAULT_TICK_MS.
     */
    int tick_ms;

    /*
     * Idle timeout in milliseconds.
     * If no events occur, the engine sleeps this long.
     * 0 = use WINGO_ENGINE_DEFAULT_IDLE_MS.
     */
    int idle_ms;

    /* ----- Identity ----- */

    /*
     * Node ID for this engine instance.
     * If NULL, a random ID will be generated.
     */
    const wingo_id *node_id;

    /* ----- Feature Flags ----- */

    /*
     * Enable DHT discovery.
     * Default: true
     */
    bool enable_dht;

    /*
     * Enable NAT traversal.
     * Default: true
     */
    bool enable_nat;

    /*
     * Enable encryption.
     * Default: true
     */
    bool enable_crypto;

    /*
     * Enable tunnel (TUN/TAP).
     * Default: false (must be enabled explicitly)
     */
    bool enable_tunnel;

    /*
     * Enable gateway (internet sharing).
     * Default: false (must be enabled explicitly)
     */
    bool enable_gateway;

} wingo_engine_config_t;

/*
 * Get default engine configuration.
 *
 * Fills the config struct with default values.
 * Call this before modifying any fields.
 */

void wingo_engine_config_default(wingo_engine_config_t *config);

/* ============================================================================
 * ENGINE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Engine handle.
 *
 * This is an opaque type. All interaction is through the
 * wingo_engine_*() functions.
 */

typedef struct wingo_engine wingo_engine_t;

/* ============================================================================
 * LIFECYCLE
 * ============================================================================ */

/*
 * Create a new engine instance.
 *
 * This allocates the engine struct and stores the configuration.
 * It does NOT initialize subsystems yet — call wingo_engine_init()
 * for that.
 *
 * @param config    Configuration (NULL for defaults)
 * @return          New engine, or NULL on error
 */

wingo_engine_t *wingo_engine_new(const wingo_engine_config_t *config);

/*
 * Initialize the engine.
 *
 * This initializes all subsystems:
 *   - Logging
 *   - State machine
 *   - Event loop (epoll)
 *   - Thread pool
 *   - Node identity
 *   - (Future) Network, crypto, tunnel
 *
 * After this call, the engine is in READY state.
 *
 * @param engine    Engine
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_init(wingo_engine_t *engine);

/*
 * Start the engine.
 *
 * This begins the main event loop. The function blocks until
 * the engine is stopped (via wingo_engine_stop() or signal).
 *
 * @param engine    Engine
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_run(wingo_engine_t *engine);

/*
 * Stop the engine.
 *
 * This signals the engine to stop. It is safe to call from
 * a signal handler or another thread.
 *
 * The main loop will exit at the next iteration.
 *
 * @param engine    Engine
 */

void wingo_engine_stop(wingo_engine_t *engine);

/*
 * Shutdown the engine.
 *
 * This stops the engine (if running) and releases all
 * subsystems. After this call, the engine is in STOPPED state.
 *
 * The engine can be re-initialized with wingo_engine_init().
 *
 * @param engine    Engine
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_shutdown(wingo_engine_t *engine);

/*
 * Free the engine.
 *
 * This shuts down the engine (if needed) and frees all memory.
 * After this call, the engine pointer is invalid.
 *
 * @param engine    Engine (NULL is safe)
 */

void wingo_engine_free(wingo_engine_t *engine);

/* ============================================================================
 * STATE QUERY
 * ============================================================================ */

/*
 * Get current engine state.
 *
 * @param engine    Engine
 * @return          Current state
 */

wingo_engine_state_t wingo_engine_get_state(const wingo_engine_t *engine);

/*
 * Get state name as string.
 *
 * @param state     State
 * @return          Static string (never NULL)
 */

const char *wingo_engine_state_name(wingo_engine_state_t state);

/*
 * Check if engine is running.
 *
 * @param engine    Engine
 * @return          true if running, false otherwise
 */

bool wingo_engine_is_running(const wingo_engine_t *engine);

/*
 * Check if engine is initialized.
 *
 * @param engine    Engine
 * @return          true if initialized, false otherwise
 */

bool wingo_engine_is_initialized(const wingo_engine_t *engine);

/* ============================================================================
 * ENGINE INFO
 * ============================================================================ */

/*
 * Get engine name.
 *
 * @param engine    Engine
 * @return          Engine name (never NULL)
 */

const char *wingo_engine_get_name(const wingo_engine_t *engine);

/*
 * Get engine node ID.
 *
 * @param engine    Engine
 * @param out       Output ID
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_get_id(const wingo_engine_t *engine, wingo_id *out);

/*
 * Get engine uptime in seconds.
 *
 * @param engine    Engine
 * @return          Uptime in seconds, or 0 if not running
 */

wingo_i64 wingo_engine_get_uptime(const wingo_engine_t *engine);

/*
 * Get engine statistics.
 *
 * @param engine    Engine
 * @param events    Output: number of events processed
 * @param ticks     Output: number of loop ticks
 * @param errors    Output: number of errors encountered
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_get_stats(const wingo_engine_t *engine,
                                     wingo_u64 *events,
                                     wingo_u64 *ticks,
                                     wingo_u64 *errors);

/*
 * Reset engine statistics.
 *
 * @param engine    Engine
 */

void wingo_engine_reset_stats(wingo_engine_t *engine);

/* ============================================================================
 * ENGINE CONFIGURATION (RUNTIME)
 * ============================================================================ */

/*
 * Get engine configuration.
 *
 * @param engine    Engine
 * @param out       Output configuration
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_get_config(const wingo_engine_t *engine,
                                      wingo_engine_config_t *out);

/*
 * Set engine log level at runtime.
 *
 * @param engine    Engine
 * @param level     New log level
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_engine_set_log_level(wingo_engine_t *engine,
                                         wingo_log_level_t level);

/* ============================================================================
 * SUBSYSTEM ACCESS
 * ============================================================================ */

/*
 * These functions give access to engine subsystems.
 * They are used by higher-level modules (network, tunnel, etc.)
 * and by tests.
 *
 * In Phase 2, only the state machine, event loop, and thread pool
 * are available. Other subsystems will be added in later phases.
 */

/*
 * Forward declarations for subsystem types.
 */

typedef struct wingo_state wingo_state_t;
typedef struct wingo_event_loop wingo_event_loop_t;
typedef struct wingo_thread_pool wingo_thread_pool_t;

/*
 * Get the engine's state machine.
 *
 * @param engine    Engine
 * @return          State machine, or NULL on error
 */

wingo_state_t *wingo_engine_get_state_machine(wingo_engine_t *engine);

/*
 * Get the engine's event loop.
 *
 * @param engine    Engine
 * @return          Event loop, or NULL on error
 */

wingo_event_loop_t *wingo_engine_get_event_loop(wingo_engine_t *engine);

/*
 * Get the engine's thread pool.
 *
 * @param engine    Engine
 * @return          Thread pool, or NULL on error
 */

wingo_thread_pool_t *wingo_engine_get_thread_pool(wingo_engine_t *engine);

/* ============================================================================
 * UTILITY
 * ============================================================================ */

/*
 * Print engine status to a file.
 *
 * @param engine    Engine
 * @param f         Output file (NULL = stderr)
 */

void wingo_engine_print_status(const wingo_engine_t *engine, FILE *f);

/*
 * Print engine statistics to a file.
 *
 * @param engine    Engine
 * @param f         Output file (NULL = stderr)
 */

void wingo_engine_print_stats(const wingo_engine_t *engine, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CORE_ENGINE_H */
