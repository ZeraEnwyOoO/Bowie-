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

#ifndef WINGO_CORE_EVENT_H
#define WINGO_CORE_EVENT_H

/*
 * ============================================================================
 * WINGO EVENT LOOP
 * ============================================================================
 *
 * The event loop is the heart of Bowie's I/O multiplexing. It:
 *   - Waits for events on file descriptors (epoll on Linux)
 *   - Dispatches events to registered callbacks
 *   - Manages timers (timeouts)
 *   - Handles signals
 *   - Provides a wakeup mechanism for cross-thread signaling
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    EVENT LOOP                               │
 *   │                                                             │
 *   │   ┌─────────────┐  ┌─────────────┐  ┌─────────────┐        │
 *   │   │   epoll     │  │   Timers    │  │  Signals    │        │
 *   │   │   (fds)     │  │  (timeouts) │  │  (handlers) │        │
 *   │   └──────┬──────┘  └──────┬──────┘  └──────┬──────┘        │
 *   │          │                │                │                │
 *   │          └────────────────┼────────────────┘                │
 *   │                           │                                 │
 *   │                    ┌──────▼──────┐                          │
 *   │                    │   EVENT     │                          │
 *   │                    │   QUEUE     │                          │
 *   │                    └──────┬──────┘                          │
 *   │                           │                                 │
 *   │                    ┌──────▼──────┐                          │
 *   │                    │   DISPATCH  │                          │
 *   │                    │  (callbacks)│                          │
 *   │                    └─────────────┘                          │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

/* ============================================================================
 * EVENT FLAGS
 * ============================================================================ */

/*
 * Event flags for fd registration.
 *
 * These flags indicate which events we want to be notified about.
 * Multiple flags can be combined with bitwise OR.
 */

#define WINGO_EVENT_NONE        0x0000
#define WINGO_EVENT_READ        0x0001  /* FD is readable */
#define WINGO_EVENT_WRITE       0x0002  /* FD is writable */
#define WINGO_EVENT_ERROR       0x0004  /* FD has error */
#define WINGO_EVENT_HANGUP      0x0008  /* FD hung up (peer closed) */
#define WINGO_EVENT_EDGE        0x0010  /* Edge-triggered mode */
#define WINGO_EVENT_ONESHOT     0x0020  /* One-shot (auto-remove after first event) */
#define WINGO_EVENT_PRIORITY    0x0040  /* Priority event */

/* ============================================================================
 * EVENT TYPES
 * ============================================================================ */

/*
 * Event types for timer and user events.
 */

typedef enum {
    WINGO_EVENT_TYPE_FD     = 0,    /* File descriptor event */
    WINGO_EVENT_TYPE_TIMER  = 1,    /* Timer event */
    WINGO_EVENT_TYPE_SIGNAL = 2,    /* Signal event */
    WINGO_EVENT_TYPE_USER   = 3,    /* User-defined event */
} wingo_event_type_t;

/* ============================================================================
 * EVENT STRUCTURE
 * ============================================================================ */

/*
 * Event handle.
 *
 * Returned by wingo_event_add_fd() and wingo_event_add_timer().
 * Used to modify or remove the event.
 */

typedef struct wingo_event wingo_event_t;

/*
 * Event callback.
 *
 * @param event     Event that fired
 * @param fd        File descriptor (for FD events) or -1
 * @param flags     Event flags that fired
 * @param userdata  User data provided when registering
 */

typedef void (*wingo_event_callback_t)(wingo_event_t *event,
                                       int fd,
                                       wingo_u32 flags,
                                       void *userdata);

/* ============================================================================
 * EVENT LOOP STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Event loop handle.
 *
 * This is an opaque type. All interaction is through the
 * wingo_event_*() functions.
 */

typedef struct wingo_event_loop wingo_event_loop_t;

/* ============================================================================
 * EVENT LOOP LIFECYCLE
 * ============================================================================ */

/*
 * Create a new event loop.
 *
 * @return          New event loop, or NULL on error
 */

wingo_event_loop_t *wingo_event_loop_new(void);

/*
 * Free an event loop.
 *
 * This removes all registered events and frees all memory.
 *
 * @param loop      Event loop (NULL is safe)
 */

void wingo_event_loop_free(wingo_event_loop_t *loop);

/*
 * Run the event loop until stopped.
 *
 * This is the main loop. It blocks waiting for events
 * and dispatches them to callbacks.
 *
 * @param loop      Event loop
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_loop_run(wingo_event_loop_t *loop);

/*
 * Run the event loop for one iteration.
 *
 * This processes all pending events, then returns.
 * Useful for integrating with other event loops.
 *
 * @param loop      Event loop
 * @param timeout_ms Timeout in milliseconds (-1 = block forever, 0 = poll)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_loop_run_once(wingo_event_loop_t *loop,
                                        int timeout_ms);

/*
 * Stop the event loop.
 *
 * The loop will exit at the next iteration.
 * Safe to call from any thread or signal handler.
 *
 * @param loop      Event loop
 */

void wingo_event_loop_stop(wingo_event_loop_t *loop);

/*
 * Check if event loop is running.
 *
 * @param loop      Event loop
 * @return          true if running, false otherwise
 */

bool wingo_event_loop_is_running(const wingo_event_loop_t *loop);

/* ============================================================================
 * FILE DESCRIPTOR EVENTS
 * ============================================================================ */

/*
 * Add a file descriptor to the event loop.
 *
 * @param loop      Event loop
 * @param fd        File descriptor
 * @param flags     Event flags (WINGO_EVENT_READ, etc.)
 * @param callback  Callback function
 * @param userdata  User data passed to callback
 * @return          Event handle, or NULL on error
 */

wingo_event_t *wingo_event_add_fd(wingo_event_loop_t *loop,
                                  int fd,
                                  wingo_u32 flags,
                                  wingo_event_callback_t callback,
                                  void *userdata);

/*
 * Modify an existing fd event.
 *
 * @param event     Event handle
 * @param flags     New event flags
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_modify_fd(wingo_event_t *event, wingo_u32 flags);

/*
 * Remove an fd event.
 *
 * @param event     Event handle
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_remove_fd(wingo_event_t *event);

/* ============================================================================
 * TIMER EVENTS
 * ============================================================================ */

/*
 * Add a timer to the event loop.
 *
 * The timer fires once after the specified delay.
 * For a repeating timer, use wingo_event_timer_reset() in the callback.
 *
 * @param loop      Event loop
 * @param delay_ms  Delay in milliseconds
 * @param callback  Callback function
 * @param userdata  User data passed to callback
 * @return          Event handle, or NULL on error
 */

wingo_event_t *wingo_event_add_timer(wingo_event_loop_t *loop,
                                     wingo_i64 delay_ms,
                                     wingo_event_callback_t callback,
                                     void *userdata);

/*
 * Reset a timer to fire again after a new delay.
 *
 * @param event     Timer event
 * @param delay_ms  New delay in milliseconds
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_timer_reset(wingo_event_t *event,
                                      wingo_i64 delay_ms);

/*
 * Cancel a timer.
 *
 * @param event     Timer event
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_remove_timer(wingo_event_t *event);

/* ============================================================================
 * SIGNAL EVENTS
 * ============================================================================ */

/*
 * Add a signal handler to the event loop.
 *
 * The signal is handled asynchronously — when it arrives,
 * the callback is invoked from the event loop thread.
 *
 * @param loop      Event loop
 * @param signum    Signal number (SIGINT, SIGTERM, etc.)
 * @param callback  Callback function
 * @param userdata  User data passed to callback
 * @return          Event handle, or NULL on error
 */

wingo_event_t *wingo_event_add_signal(wingo_event_loop_t *loop,
                                      int signum,
                                      wingo_event_callback_t callback,
                                      void *userdata);

/*
 * Remove a signal handler.
 *
 * @param event     Signal event
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_remove_signal(wingo_event_t *event);

/* ============================================================================
 * USER EVENTS
 * ============================================================================ */

/*
 * Post a user event to the event loop.
 *
 * This is thread-safe. It can be called from any thread to
 * schedule a callback in the event loop thread.
 *
 * @param loop      Event loop
 * @param callback  Callback function
 * @param userdata  User data passed to callback
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_post(wingo_event_loop_t *loop,
                               wingo_event_callback_t callback,
                               void *userdata);

/* ============================================================================
 * WAKEUP
 * ============================================================================ */

/*
 * Wake up the event loop.
 *
 * If the loop is blocked in epoll_wait(), this will cause it
 * to return immediately. Useful for cross-thread signaling.
 *
 * @param loop      Event loop
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_wakeup(wingo_event_loop_t *loop);

/* ============================================================================
 * EVENT QUERY
 * ============================================================================ */

/*
 * Get event type.
 *
 * @param event     Event
 * @return          Event type
 */

wingo_event_type_t wingo_event_get_type(const wingo_event_t *event);

/*
 * Get event fd.
 *
 * @param event     Event
 * @return          File descriptor, or -1 if not an fd event
 */

int wingo_event_get_fd(const wingo_event_t *event);

/*
 * Get event flags.
 *
 * @param event     Event
 * @return          Event flags
 */

wingo_u32 wingo_event_get_flags(const wingo_event_t *event);

/*
 * Get event userdata.
 *
 * @param event     Event
 * @return          User data
 */

void *wingo_event_get_userdata(const wingo_event_t *event);

/* ============================================================================
 * EVENT LOOP STATISTICS
 * ============================================================================ */

/*
 * Get event loop statistics.
 *
 * @param loop      Event loop
 * @param events    Output: number of events processed
 * @param timers    Output: number of timers fired
 * @param wakeups   Output: number of wakeups
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_event_loop_get_stats(const wingo_event_loop_t *loop,
                                         wingo_u64 *events,
                                         wingo_u64 *timers,
                                         wingo_u64 *wakeups);

/*
 * Reset event loop statistics.
 *
 * @param loop      Event loop
 */

void wingo_event_loop_reset_stats(wingo_event_loop_t *loop);

/* ============================================================================
 * EVENT LOOP INFO
 * ============================================================================ */

/*
 * Get number of registered events.
 *
 * @param loop      Event loop
 * @return          Number of events
 */

wingo_size wingo_event_loop_count(const wingo_event_loop_t *loop);

/*
 * Print event loop status to a file.
 *
 * @param loop      Event loop
 * @param f         Output file (NULL = stderr)
 */

void wingo_event_loop_print_status(const wingo_event_loop_t *loop, FILE *f);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CORE_EVENT_H */
