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

#ifndef WINGO_CORE_STATE_H
#define WINGO_CORE_STATE_H

/*
 * ============================================================================
 * WINGO STATE MACHINE
 * ============================================================================
 *
 * The state machine manages the lifecycle of the engine and other
 * components. It provides:
 *   - State definitions
 *   - State transitions
 *   - Transition callbacks
 *   - State history
 *   - Thread-safe access
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    STATE MACHINE                            │
 *   │                                                             │
 *   │   ┌─────────────┐                                          │
 *   │   │   State     │                                          │
 *   │   │  Definitions│                                          │
 *   │   └──────┬──────┘                                          │
 *   │          │                                                 │
 *   │          ▼                                                 │
 *   │   ┌─────────────┐                                          │
 *   │   │   Current   │                                          │
 *   │   │    State    │                                          │
 *   │   └──────┬──────┘                                          │
 *   │          │                                                 │
 *   │          ▼                                                 │
 *   │   ┌─────────────┐      ┌─────────────┐                    │
 *   │   │  Transition │─────>│  Callbacks  │                    │
 *   │   │    Logic    │      │  (on_enter) │                    │
 *   │   └──────┬──────┘      │  (on_exit)  │                    │
 *   │          │             └─────────────┘                    │
 *   │          ▼                                                 │
 *   │   ┌─────────────┐                                          │
 *   │   │   History   │                                          │
 *   │   │   (log)     │                                          │
 *   │   └─────────────┘                                          │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * STATE DEFINITIONS
 * ============================================================================ */

/*
 * Generic state values.
 *
 * The state machine is generic — it can be used for the engine
 * lifecycle, peer connections, tunnels, etc.
 *
 * State values 0-15 are reserved for common states.
 * State values 16+ can be used for application-specific states.
 */

typedef enum {
    /* Common states */
    WINGO_STATE_NONE        = 0,    /* No state / uninitialized */
    WINGO_STATE_CREATED     = 1,    /* Object created */
    WINGO_STATE_INITIALIZING = 2,   /* Being initialized */
    WINGO_STATE_READY       = 3,    /* Initialized, ready */
    WINGO_STATE_RUNNING     = 4,    /* Running */
    WINGO_STATE_PAUSED      = 5,    /* Paused */
    WINGO_STATE_STOPPING    = 6,    /* Stopping */
    WINGO_STATE_STOPPED     = 7,    /* Stopped */
    WINGO_STATE_ERROR       = 8,    /* Error occurred */
    WINGO_STATE_DESTROYED   = 9,    /* Destroyed */

    /* Application-specific states start here */
    WINGO_STATE_USER        = 16,
} wingo_state_value_t;

/*
 * Maximum number of states supported.
 */

#define WINGO_STATE_MAX         32

/*
 * Maximum number of transitions supported.
 */

#define WINGO_STATE_MAX_TRANSITIONS 128

/* ============================================================================
 * STATE CALLBACKS
 * ============================================================================ */

/*
 * State transition callback.
 *
 * Called when a state transition occurs.
 *
 * @param from      Previous state
 * @param to        New state
 * @param userdata  User data provided when creating the state machine
 * @return          WINGO_SUCCESS to allow transition,
 *                  error code to reject transition
 */

typedef wingo_error_t (*wingo_state_callback_t)(wingo_state_value_t from,
                                                wingo_state_value_t to,
                                                void *userdata);

/* ============================================================================
 * STATE MACHINE STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * State machine handle.
 *
 * This is an opaque type. All interaction is through the
 * wingo_state_*() functions.
 */

typedef struct wingo_state wingo_state_t;

/* ============================================================================
 * STATE MACHINE LIFECYCLE
 * ============================================================================ */

/*
 * Create a new state machine.
 *
 * @param initial_state Initial state
 * @param userdata      User data passed to callbacks
 * @return              New state machine, or NULL on error
 */

wingo_state_t *wingo_state_new(wingo_state_value_t initial_state,
                               void *userdata);

/*
 * Free a state machine.
 *
 * @param state     State machine (NULL is safe)
 */

void wingo_state_free(wingo_state_t *state);

/* ============================================================================
 * STATE QUERY
 * ============================================================================ */

/*
 * Get current state.
 *
 * @param state     State machine
 * @return          Current state
 */

wingo_state_value_t wingo_state_get(const wingo_state_t *state);

/*
 * Get state name as string.
 *
 * @param value     State value
 * @return          Static string (never NULL)
 */

const char *wingo_state_name(wingo_state_value_t value);

/*
 * Check if state machine is in a specific state.
 *
 * @param state     State machine
 * @param value     State value to check
 * @return          true if in state, false otherwise
 */

bool wingo_state_is(const wingo_state_t *state, wingo_state_value_t value);

/*
 * Check if state machine is in any of the given states.
 *
 * @param state     State machine
 * @param values    Array of state values
 * @param count     Number of values
 * @return          true if in any state, false otherwise
 */

bool wingo_state_is_any(const wingo_state_t *state,
                        const wingo_state_value_t *values,
                        wingo_size count);

/* ============================================================================
 * STATE TRANSITIONS
 * ============================================================================ */

/*
 * Transition to a new state.
 *
 * This checks if the transition is allowed, calls callbacks,
 * and updates the current state.
 *
 * @param state     State machine
 * @param to        New state
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_transition(wingo_state_t *state,
                                     wingo_state_value_t to);

/*
 * Force a transition without checking rules.
 *
 * This is used for error handling and recovery.
 *
 * @param state     State machine
 * @param to        New state
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_force(wingo_state_t *state,
                                wingo_state_value_t to);

/*
 * Check if a transition is allowed.
 *
 * @param state     State machine
 * @param to        Target state
 * @return          true if allowed, false otherwise
 */

bool wingo_state_can_transition(const wingo_state_t *state,
                                wingo_state_value_t to);

/* ============================================================================
 * TRANSITION RULES
 * ============================================================================ */

/*
 * Add a transition rule.
 *
 * By default, all transitions are allowed. Use this function
 * to restrict transitions.
 *
 * @param state     State machine
 * @param from      Source state
 * @param to        Target state
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_add_transition(wingo_state_t *state,
                                         wingo_state_value_t from,
                                         wingo_state_value_t to);

/*
 * Remove a transition rule.
 *
 * @param state     State machine
 * @param from      Source state
 * @param to        Target state
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_remove_transition(wingo_state_t *state,
                                            wingo_state_value_t from,
                                            wingo_state_value_t to);

/*
 * Clear all transition rules.
 *
 * After this, all transitions are allowed again.
 *
 * @param state     State machine
 */

void wingo_state_clear_transitions(wingo_state_t *state);

/*
 * Enable/disable strict mode.
 *
 * In strict mode, only explicitly allowed transitions are permitted.
 * In non-strict mode (default), all transitions are allowed unless
 * explicitly forbidden.
 *
 * @param state     State machine
 * @param strict    true for strict mode
 */

void wingo_state_set_strict(wingo_state_t *state, bool strict);

/*
 * Check if strict mode is enabled.
 *
 * @param state     State machine
 * @return          true if strict, false otherwise
 */

bool wingo_state_is_strict(const wingo_state_t *state);

/* ============================================================================
 * STATE CALLBACKS
 * ============================================================================ */

/*
 * Callback types.
 */

typedef enum {
    WINGO_STATE_CB_ON_ENTER   = 0,  /* Called when entering a state */
    WINGO_STATE_CB_ON_EXIT    = 1,  /* Called when exiting a state */
    WINGO_STATE_CB_ON_TRANS   = 2,  /* Called on any transition */
} wingo_state_cb_type_t;

/*
 * Add a state callback.
 *
 * @param state     State machine
 * @param type      Callback type
 * @param value     State value (ignored for ON_TRANS)
 * @param callback  Callback function
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_add_callback(wingo_state_t *state,
                                       wingo_state_cb_type_t type,
                                       wingo_state_value_t value,
                                       wingo_state_callback_t callback);

/*
 * Remove a state callback.
 *
 * @param state     State machine
 * @param type      Callback type
 * @param value     State value
 * @param callback  Callback function
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_state_remove_callback(wingo_state_t *state,
                                          wingo_state_cb_type_t type,
                                          wingo_state_value_t value,
                                          wingo_state_callback_t callback);

/*
 * Clear all callbacks.
 *
 * @param state     State machine
 */

void wingo_state_clear_callbacks(wingo_state_t *state);

/* ============================================================================
 * STATE HISTORY
 * ============================================================================ */

/*
 * History entry.
 */

typedef struct {
    wingo_state_value_t from;
    wingo_state_value_t to;
    wingo_i64           timestamp;  /* Unix timestamp */
    wingo_error_t       result;     /* Result of transition */
} wingo_state_history_t;

/*
 * Get state history.
 *
 * @param state     State machine
 * @param out       Output array
 * @param max       Maximum number of entries
 * @return          Number of entries written
 */

wingo_size wingo_state_get_history(const wingo_state_t *state,
                                   wingo_state_history_t *out,
                                   wingo_size max);

/*
 * Clear state history.
 *
 * @param state     State machine
 */

void wingo_state_clear_history(wingo_state_t *state);

/*
 * Get number of transitions.
 *
 * @param state     State machine
 * @return          Number of transitions
 */

wingo_u64 wingo_state_get_transition_count(const wingo_state_t *state);

/*
 * Get time spent in each state.
 *
 * @param state     State machine
 * @param value     State value
 * @return          Time in milliseconds
 */

wingo_i64 wingo_state_get_time_in(const wingo_state_t *state,
                                  wingo_state_value_t value);

/* ============================================================================
 * STATE MACHINE INFO
 * ============================================================================ */

/*
 * Print state machine status to a file.
 *
 * @param state     State machine
 * @param f         Output file (NULL = stderr)
 */

void wingo_state_print(const wingo_state_t *state, FILE *f);

/*
 * Print state history to a file.
 *
 * @param state     State machine
 * @param f         Output file (NULL = stderr)
 */

void wingo_state_print_history(const wingo_state_t *state, FILE *f);

/* ============================================================================
 * THREAD SAFETY
 * ============================================================================ */

/*
 * The state machine is thread-safe by default. All operations
 * acquire an internal mutex.
 *
 * If you need to perform multiple operations atomically,
 * use the lock/unlock functions.
 */

/*
 * Lock the state machine.
 *
 * @param state     State machine
 */

void wingo_state_lock(wingo_state_t *state);

/*
 * Unlock the state machine.
 *
 * @param state     State machine
 */

void wingo_state_unlock(wingo_state_t *state);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CORE_STATE_H */
