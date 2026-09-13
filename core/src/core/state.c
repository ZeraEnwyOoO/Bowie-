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

#include "wingo/core/state.h"
#include "wingo/util/time.h"
#include "wingo/util/list.h"

#include <string.h>
#include <pthread.h>

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Transition rule.
 */
typedef struct {
    wingo_state_value_t from;
    wingo_state_value_t to;
} state_transition_rule_t;

/*
 * Callback entry.
 */
typedef struct {
    wingo_state_cb_type_t  type;
    wingo_state_value_t    value;
    wingo_state_callback_t callback;
} state_callback_entry_t;

/*
 * State machine structure.
 */
struct wingo_state {
    /* ----- Current state ----- */
    wingo_state_value_t     current;
    wingo_state_value_t     initial;
    wingo_i64               state_enter_time;   /* When we entered current state */

    /* ----- Configuration ----- */
    bool                    strict;
    void                   *userdata;

    /* ----- Transition rules ----- */
    state_transition_rule_t *transitions;
    wingo_size              num_transitions;
    wingo_size              max_transitions;

    /* ----- Callbacks ----- */
    state_callback_entry_t *callbacks;
    wingo_size              num_callbacks;
    wingo_size              max_callbacks;

    /* ----- History ----- */
    wingo_state_history_t  *history;
    wingo_size              num_history;
    wingo_size              max_history;
    wingo_size              history_head;      /* Circular buffer */

    /* ----- Time tracking ----- */
    wingo_i64               time_in_state[WINGO_STATE_MAX];
    wingo_i64               last_state_change;

    /* ----- Statistics ----- */
    wingo_u64               transition_count;

    /* ----- Synchronization ----- */
    pthread_mutex_t         mutex;
};

/* ============================================================================
 * STATE NAME TABLE
 * ============================================================================ */

static const char *const state_names[] = {
    "NONE",
    "CREATED",
    "INITIALIZING",
    "READY",
    "RUNNING",
    "PAUSED",
    "STOPPING",
    "STOPPED",
    "ERROR",
    "DESTROYED",
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Find a transition rule.
 */
static state_transition_rule_t *find_transition(wingo_state_t *state,
                                                wingo_state_value_t from,
                                                wingo_state_value_t to)
{
    wingo_size i;

    for (i = 0; i < state->num_transitions; i++) {
        if (state->transitions[i].from == from &&
            state->transitions[i].to == to) {
            return &state->transitions[i];
        }
    }

    return NULL;
}

/*
 * Check if a transition is allowed by rules.
 */
static bool transition_allowed(wingo_state_t *state,
                               wingo_state_value_t from,
                               wingo_state_value_t to)
{
    /* If not strict, all transitions are allowed unless explicitly forbidden */
    if (!state->strict) {
        /* In non-strict mode, we still check if the transition exists
         * to allow explicit control. If no rules exist, allow everything. */
        if (state->num_transitions == 0) {
            return true;
        }

        /* If rules exist, check if this transition is allowed */
        return find_transition(state, from, to) != NULL;
    }

    /* In strict mode, only explicitly allowed transitions are permitted */
    return find_transition(state, from, to) != NULL;
}

/*
 * Call callbacks of a specific type.
 */
static wingo_error_t call_callbacks(wingo_state_t *state,
                                    wingo_state_cb_type_t type,
                                    wingo_state_value_t from,
                                    wingo_state_value_t to)
{
    wingo_size i;
    wingo_error_t rc;

    for (i = 0; i < state->num_callbacks; i++) {
        state_callback_entry_t *entry = &state->callbacks[i];

        if (entry->type != type) {
            continue;
        }

        /* For ON_ENTER/ON_EXIT, check value match */
        if (type != WINGO_STATE_CB_ON_TRANS) {
            if (entry->value != to && entry->value != from) {
                continue;
            }
        }

        if (entry->callback != NULL) {
            rc = entry->callback(from, to, state->userdata);
            if (rc != WINGO_SUCCESS) {
                return rc;
            }
        }
    }

    return WINGO_SUCCESS;
}

/*
 * Record a transition in history.
 */
static void record_history(wingo_state_t *state,
                           wingo_state_value_t from,
                           wingo_state_value_t to,
                           wingo_error_t result)
{
    wingo_state_history_t *entry;

    if (state->history == NULL || state->max_history == 0) {
        return;
    }

    /* Circular buffer */
    entry = &state->history[state->history_head];

    entry->from      = from;
    entry->to        = to;
    entry->timestamp = wingo_time_unix();
    entry->result    = result;

    state->history_head = (state->history_head + 1) % state->max_history;

    if (state->num_history < state->max_history) {
        state->num_history++;
    }
}

/* ============================================================================
 * STATE MACHINE LIFECYCLE
 * ============================================================================ */

wingo_state_t *wingo_state_new(wingo_state_value_t initial_state,
                               void *userdata)
{
    wingo_state_t *state;

    state = calloc(1, sizeof(wingo_state_t));
    if (state == NULL) {
        return NULL;
    }

    /* Initialize fields */
    state->current = initial_state;
    state->initial = initial_state;
    state->strict = false;
    state->userdata = userdata;
    state->transition_count = 0;

    /* Initialize time tracking */
    state->state_enter_time = wingo_time_now_ms();
    state->last_state_change = state->state_enter_time;

    /* Allocate transitions array */
    state->max_transitions = WINGO_STATE_MAX_TRANSITIONS;
    state->transitions = calloc(state->max_transitions,
                                sizeof(state_transition_rule_t));
    if (state->transitions == NULL) {
        free(state);
        return NULL;
    }

    /* Allocate callbacks array */
    state->max_callbacks = 64;
    state->callbacks = calloc(state->max_callbacks,
                              sizeof(state_callback_entry_t));
    if (state->callbacks == NULL) {
        free(state->transitions);
        free(state);
        return NULL;
    }

    /* Allocate history array */
    state->max_history = 64;
    state->history = calloc(state->max_history,
                            sizeof(wingo_state_history_t));
    if (state->history == NULL) {
        free(state->callbacks);
        free(state->transitions);
        free(state);
        return NULL;
    }

    /* Initialize mutex */
    if (pthread_mutex_init(&state->mutex, NULL) != 0) {
        free(state->history);
        free(state->callbacks);
        free(state->transitions);
        free(state);
        return NULL;
    }

    return state;
}

void wingo_state_free(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_destroy(&state->mutex);

    free(state->history);
    free(state->callbacks);
    free(state->transitions);
    free(state);
}

/* ============================================================================
 * STATE QUERY
 * ============================================================================ */

wingo_state_value_t wingo_state_get(const wingo_state_t *state)
{
    wingo_state_value_t value;

    if (state == NULL) {
        return WINGO_STATE_NONE;
    }

    /* Cast away const for mutex */
    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);
    value = state->current;
    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return value;
}

const char *wingo_state_name(wingo_state_value_t value)
{
    if (value < WINGO_ARRAY_SIZE(state_names)) {
        return state_names[value];
    }

    return "UNKNOWN";
}

bool wingo_state_is(const wingo_state_t *state, wingo_state_value_t value)
{
    if (state == NULL) {
        return false;
    }

    return state->current == value;
}

bool wingo_state_is_any(const wingo_state_t *state,
                        const wingo_state_value_t *values,
                        wingo_size count)
{
    wingo_size i;

    if (state == NULL || values == NULL) {
        return false;
    }

    for (i = 0; i < count; i++) {
        if (state->current == values[i]) {
            return true;
        }
    }

    return false;
}

/* ============================================================================
 * STATE TRANSITIONS
 * ============================================================================ */

wingo_error_t wingo_state_transition(wingo_state_t *state,
                                     wingo_state_value_t to)
{
    wingo_state_value_t from;
    wingo_i64 now;
    wingo_error_t rc;

    if (state == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    from = state->current;

    /* No-op if already in target state */
    if (from == to) {
        pthread_mutex_unlock(&state->mutex);
        return WINGO_SUCCESS;
    }

    /* Check if transition is allowed */
    if (!transition_allowed(state, from, to)) {
        WINGO_LOG_WARN("State transition not allowed: %s -> %s",
                       wingo_state_name(from), wingo_state_name(to));
        record_history(state, from, to, WINGO_ERR_INVALID_STATE);
        pthread_mutex_unlock(&state->mutex);
        return WINGO_ERR_INVALID_STATE;
    }

    /* Call ON_EXIT callbacks */
    rc = call_callbacks(state, WINGO_STATE_CB_ON_EXIT, from, to);
    if (rc != WINGO_SUCCESS) {
        record_history(state, from, to, rc);
        pthread_mutex_unlock(&state->mutex);
        return rc;
    }

    /* Call ON_TRANS callbacks */
    rc = call_callbacks(state, WINGO_STATE_CB_ON_TRANS, from, to);
    if (rc != WINGO_SUCCESS) {
        record_history(state, from, to, rc);
        pthread_mutex_unlock(&state->mutex);
        return rc;
    }

    /* Update time tracking */
    now = wingo_time_now_ms();
    if (from < WINGO_STATE_MAX) {
        state->time_in_state[from] += (now - state->last_state_change);
    }

    /* Update state */
    state->current = to;
    state->state_enter_time = now;
    state->last_state_change = now;
    state->transition_count++;

    /* Call ON_ENTER callbacks */
    rc = call_callbacks(state, WINGO_STATE_CB_ON_ENTER, from, to);
    if (rc != WINGO_SUCCESS) {
        record_history(state, from, to, rc);
        pthread_mutex_unlock(&state->mutex);
        return rc;
    }

    /* Record history */
    record_history(state, from, to, WINGO_SUCCESS);

    WINGO_LOG_DEBUG("State: %s -> %s",
                    wingo_state_name(from), wingo_state_name(to));

    pthread_mutex_unlock(&state->mutex);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_state_force(wingo_state_t *state,
                                wingo_state_value_t to)
{
    wingo_state_value_t from;
    wingo_i64 now;

    if (state == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    from = state->current;

    if (from == to) {
        pthread_mutex_unlock(&state->mutex);
        return WINGO_SUCCESS;
    }

    /* Call ON_EXIT callbacks (ignore errors) */
    call_callbacks(state, WINGO_STATE_CB_ON_EXIT, from, to);

    /* Call ON_TRANS callbacks (ignore errors) */
    call_callbacks(state, WINGO_STATE_CB_ON_TRANS, from, to);

    /* Update time tracking */
    now = wingo_time_now_ms();
    if (from < WINGO_STATE_MAX) {
        state->time_in_state[from] += (now - state->last_state_change);
    }

    /* Update state */
    state->current = to;
    state->state_enter_time = now;
    state->last_state_change = now;
    state->transition_count++;

    /* Call ON_ENTER callbacks (ignore errors) */
    call_callbacks(state, WINGO_STATE_CB_ON_ENTER, from, to);

    /* Record history */
    record_history(state, from, to, WINGO_SUCCESS);

    pthread_mutex_unlock(&state->mutex);

    return WINGO_SUCCESS;
}

bool wingo_state_can_transition(const wingo_state_t *state,
                                wingo_state_value_t to)
{
    bool allowed;

    if (state == NULL) {
        return false;
    }

    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);
    allowed = transition_allowed((wingo_state_t *)state, state->current, to);
    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return allowed;
}

/* ============================================================================
 * TRANSITION RULES
 * ============================================================================ */

wingo_error_t wingo_state_add_transition(wingo_state_t *state,
                                         wingo_state_value_t from,
                                         wingo_state_value_t to)
{
    if (state == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    /* Check if already exists */
    if (find_transition(state, from, to) != NULL) {
        pthread_mutex_unlock(&state->mutex);
        return WINGO_SUCCESS;
    }

    /* Check capacity */
    if (state->num_transitions >= state->max_transitions) {
        pthread_mutex_unlock(&state->mutex);
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Add rule */
    state->transitions[state->num_transitions].from = from;
    state->transitions[state->num_transitions].to = to;
    state->num_transitions++;

    pthread_mutex_unlock(&state->mutex);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_state_remove_transition(wingo_state_t *state,
                                            wingo_state_value_t from,
                                            wingo_state_value_t to)
{
    wingo_size i;

    if (state == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    for (i = 0; i < state->num_transitions; i++) {
        if (state->transitions[i].from == from &&
            state->transitions[i].to == to) {
            /* Shift remaining entries */
            if (i < state->num_transitions - 1) {
                memmove(&state->transitions[i],
                        &state->transitions[i + 1],
                        (state->num_transitions - i - 1) *
                        sizeof(state_transition_rule_t));
            }
            state->num_transitions--;
            pthread_mutex_unlock(&state->mutex);
            return WINGO_SUCCESS;
        }
    }

    pthread_mutex_unlock(&state->mutex);
    return WINGO_ERR_NOT_FOUND;
}

void wingo_state_clear_transitions(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    state->num_transitions = 0;
    pthread_mutex_unlock(&state->mutex);
}

void wingo_state_set_strict(wingo_state_t *state, bool strict)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    state->strict = strict;
    pthread_mutex_unlock(&state->mutex);
}

bool wingo_state_is_strict(const wingo_state_t *state)
{
    bool strict;

    if (state == NULL) {
        return false;
    }

    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);
    strict = state->strict;
    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return strict;
}

/* ============================================================================
 * STATE CALLBACKS
 * ============================================================================ */

wingo_error_t wingo_state_add_callback(wingo_state_t *state,
                                       wingo_state_cb_type_t type,
                                       wingo_state_value_t value,
                                       wingo_state_callback_t callback)
{
    if (state == NULL || callback == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    /* Check capacity */
    if (state->num_callbacks >= state->max_callbacks) {
        pthread_mutex_unlock(&state->mutex);
        return WINGO_ERR_OUT_OF_RANGE;
    }

    /* Add callback */
    state->callbacks[state->num_callbacks].type = type;
    state->callbacks[state->num_callbacks].value = value;
    state->callbacks[state->num_callbacks].callback = callback;
    state->num_callbacks++;

    pthread_mutex_unlock(&state->mutex);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_state_remove_callback(wingo_state_t *state,
                                          wingo_state_cb_type_t type,
                                          wingo_state_value_t value,
                                          wingo_state_callback_t callback)
{
    wingo_size i;

    if (state == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&state->mutex);

    for (i = 0; i < state->num_callbacks; i++) {
        if (state->callbacks[i].type == type &&
            state->callbacks[i].value == value &&
            state->callbacks[i].callback == callback) {
            /* Shift remaining entries */
            if (i < state->num_callbacks - 1) {
                memmove(&state->callbacks[i],
                        &state->callbacks[i + 1],
                        (state->num_callbacks - i - 1) *
                        sizeof(state_callback_entry_t));
            }
            state->num_callbacks--;
            pthread_mutex_unlock(&state->mutex);
            return WINGO_SUCCESS;
        }
    }

    pthread_mutex_unlock(&state->mutex);
    return WINGO_ERR_NOT_FOUND;
}

void wingo_state_clear_callbacks(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    state->num_callbacks = 0;
    pthread_mutex_unlock(&state->mutex);
}

/* ============================================================================
 * STATE HISTORY
 * ============================================================================ */

wingo_size wingo_state_get_history(const wingo_state_t *state,
                                   wingo_state_history_t *out,
                                   wingo_size max)
{
    wingo_size i;
    wingo_size count;
    wingo_size start;

    if (state == NULL || out == NULL || max == 0) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);

    count = (state->num_history < max) ? state->num_history : max;

    /* Copy from circular buffer (oldest first) */
    if (state->num_history < state->max_history) {
        /* Not yet wrapped — linear order */
        start = 0;
    } else {
        /* Wrapped — start from head */
        start = state->history_head;
    }

    for (i = 0; i < count; i++) {
        wingo_size idx = (start + i) % state->max_history;
        out[i] = state->history[idx];
    }

    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return count;
}

void wingo_state_clear_history(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
    state->num_history = 0;
    state->history_head = 0;
    pthread_mutex_unlock(&state->mutex);
}

wingo_u64 wingo_state_get_transition_count(const wingo_state_t *state)
{
    wingo_u64 count;

    if (state == NULL) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);
    count = state->transition_count;
    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return count;
}

wingo_i64 wingo_state_get_time_in(const wingo_state_t *state,
                                  wingo_state_value_t value)
{
    wingo_i64 time_ms;

    if (state == NULL || value >= WINGO_STATE_MAX) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&state->mutex);

    time_ms = state->time_in_state[value];

    /* If currently in this state, add current duration */
    if (state->current == value) {
        wingo_i64 now = wingo_time_now_ms();
        time_ms += (now - state->state_enter_time);
    }

    pthread_mutex_unlock((pthread_mutex_t *)&state->mutex);

    return time_ms;
}

/* ============================================================================
 * INFO
 * ============================================================================ */

void wingo_state_print(const wingo_state_t *state, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (state == NULL) {
        fprintf(f, "State: (null)\n");
        return;
    }

    fprintf(f, "State Machine:\n");
    fprintf(f, "  Current:       %s\n",
            wingo_state_name(state->current));
    fprintf(f, "  Initial:       %s\n",
            wingo_state_name(state->initial));
    fprintf(f, "  Strict:        %s\n", state->strict ? "yes" : "no");
    fprintf(f, "  Transitions:   %llu\n",
            (unsigned long long)state->transition_count);
    fprintf(f, "  Rules:         %zu\n", state->num_transitions);
    fprintf(f, "  Callbacks:     %zu\n", state->num_callbacks);
    fprintf(f, "  History:       %zu\n", state->num_history);
}

void wingo_state_print_history(const wingo_state_t *state, FILE *f)
{
    wingo_state_history_t *history;
    wingo_size count;
    wingo_size i;

    if (f == NULL) {
        f = stderr;
    }

    if (state == NULL) {
        fprintf(f, "State: (null)\n");
        return;
    }

    history = calloc(64, sizeof(wingo_state_history_t));
    if (history == NULL) {
        return;
    }

    count = wingo_state_get_history(state, history, 64);

    fprintf(f, "State History (%zu entries):\n", count);

    for (i = 0; i < count; i++) {
        fprintf(f, "  [%lld] %s -> %s (%s)\n",
                (long long)history[i].timestamp,
                wingo_state_name(history[i].from),
                wingo_state_name(history[i].to),
                wingo_error_name(history[i].result));
    }

    free(history);
}

/* ============================================================================
 * THREAD SAFETY
 * ============================================================================ */

void wingo_state_lock(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_lock(&state->mutex);
}

void wingo_state_unlock(wingo_state_t *state)
{
    if (state == NULL) {
        return;
    }

    pthread_mutex_unlock(&state->mutex);
}
