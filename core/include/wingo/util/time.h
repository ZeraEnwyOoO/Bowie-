
/*
 * Wingo — P2P Internet Sharing Tool
 * Copyright (C) 2024 Wingo Project
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

#ifndef WINGO_UTIL_TIME_H
#define WINGO_UTIL_TIME_H

/*
 * ============================================================================
 * WINGO TIME UTILITIES
 * ============================================================================
 *
 * This header provides:
 *   - Monotonic time
 *   - Wall clock time
 *   - Time formatting
 *   - Time arithmetic
 *   - Sleep functions
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * TIME TYPES
 * ============================================================================ */

/*
 * Time value (seconds + nanoseconds).
 */

typedef struct {
    wingo_i64 sec;      /* Seconds */
    wingo_i64 nsec;     /* Nanoseconds (0-999999999) */
} wingo_timespec_t;

/*
 * Time value (seconds + microseconds) — legacy.
 */

typedef struct {
    wingo_i64 sec;      /* Seconds */
    wingo_i64 usec;     /* Microseconds (0-999999) */
} wingo_timeval_t;

/* ============================================================================
 * TIME FUNCTIONS
 * ============================================================================ */

/*
 * Get monotonic time (for measuring intervals).
 *
 * @param ts        Output time
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_time_monotonic(wingo_timespec_t *ts);

/*
 * Get wall clock time (for timestamps).
 *
 * @param ts        Output time
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_time_realtime(wingo_timespec_t *ts);

/*
 * Get current time in seconds (monotonic).
 *
 * @return          Seconds since arbitrary epoch
 */

wingo_i64 wingo_time_now(void);

/*
 * Get current time in milliseconds (monotonic).
 *
 * @return          Milliseconds since arbitrary epoch
 */

wingo_i64 wingo_time_now_ms(void);

/*
 * Get current time in microseconds (monotonic).
 *
 * @return          Microseconds since arbitrary epoch
 */

wingo_i64 wingo_time_now_us(void);

/*
 * Get current time in nanoseconds (monotonic).
 *
 * @return          Nanoseconds since arbitrary epoch
 */

wingo_i64 wingo_time_now_ns(void);

/*
 * Get wall clock time in seconds (Unix timestamp).
 *
 * @return          Unix timestamp
 */

wingo_i64 wingo_time_unix(void);

/* ============================================================================
 * TIME ARITHMETIC
 * ============================================================================ */

/*
 * Add two time values.
 *
 * @param a         First time
 * @param b         Second time
 * @return          Sum
 */

wingo_timespec_t wingo_time_add(wingo_timespec_t a, wingo_timespec_t b);

/*
 * Subtract two time values.
 *
 * @param a         First time
 * @param b         Second time
 * @return          Difference (a - b)
 */

wingo_timespec_t wingo_time_sub(wingo_timespec_t a, wingo_timespec_t b);

/*
 * Compare two time values.
 *
 * @param a         First time
 * @param b         Second time
 * @return          <0 if a<b, 0 if a==b, >0 if a>b
 */

int wingo_time_cmp(wingo_timespec_t a, wingo_timespec_t b);

/*
 * Convert timespec to milliseconds.
 *
 * @param ts        Time
 * @return          Milliseconds
 */

wingo_i64 wingo_time_to_ms(wingo_timespec_t ts);

/*
 * Convert timespec to microseconds.
 *
 * @param ts        Time
 * @return          Microseconds
 */

wingo_i64 wingo_time_to_us(wingo_timespec_t ts);

/*
 * Convert timespec to nanoseconds.
 *
 * @param ts        Time
 * @return          Nanoseconds
 */

wingo_i64 wingo_time_to_ns(wingo_timespec_t ts);

/*
 * Convert milliseconds to timespec.
 *
 * @param ms        Milliseconds
 * @return          Time
 */

wingo_timespec_t wingo_time_from_ms(wingo_i64 ms);

/*
 * Convert microseconds to timespec.
 *
 * @param us        Microseconds
 * @return          Time
 */

wingo_timespec_t wingo_time_from_us(wingo_i64 us);

/*
 * Convert nanoseconds to timespec.
 *
 * @param ns        Nanoseconds
 * @return          Time
 */

wingo_timespec_t wingo_time_from_ns(wingo_i64 ns);

/* ============================================================================
 * TIME FORMATTING
 * ============================================================================ */

/*
 * Format time as ISO 8601 string.
 *
 * @param ts        Time
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          Number of bytes written
 */

int wingo_time_format_iso(wingo_timespec_t ts, char *buf, wingo_size size);

/*
 * Format time as human-readable string.
 *
 * @param ts        Time
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          Number of bytes written
 */

int wingo_time_format_human(wingo_timespec_t ts, char *buf, wingo_size size);

/*
 * Format duration as human-readable string.
 *
 * @param ns        Duration in nanoseconds
 * @param buf       Output buffer
 * @param size      Buffer size
 * @return          Number of bytes written
 */

int wingo_time_format_duration(wingo_i64 ns, char *buf, wingo_size size);

/* ============================================================================
 * SLEEP FUNCTIONS
 * ============================================================================ */

/*
 * Sleep for a specified time.
 *
 * @param ts        Time to sleep
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_time_sleep(wingo_timespec_t ts);

/*
 * Sleep for a specified number of seconds.
 *
 * @param sec       Seconds to sleep
 */

void wingo_time_sleep_sec(wingo_i64 sec);

/*
 * Sleep for a specified number of milliseconds.
 *
 * @param ms        Milliseconds to sleep
 */

void wingo_time_sleep_ms(wingo_i64 ms);

/*
 * Sleep for a specified number of microseconds.
 *
 * @param us        Microseconds to sleep
 */

void wingo_time_sleep_us(wingo_i64 us);

/*
 * Sleep for a specified number of nanoseconds.
 *
 * @param ns        Nanoseconds to sleep
 */

void wingo_time_sleep_ns(wingo_i64 ns);

/* ============================================================================
 * TIMER
 * ============================================================================ */

/*
 * Timer for measuring intervals.
 */

typedef struct {
    wingo_timespec_t start;
    wingo_timespec_t end;
    bool running;
} wingo_timer_t;

/*
 * Initialize a timer.
 *
 * @param timer     Timer
 */

void wingo_timer_init(wingo_timer_t *timer);

/*
 * Start a timer.
 *
 * @param timer     Timer
 */

void wingo_timer_start(wingo_timer_t *timer);

/*
 * Stop a timer.
 *
 * @param timer     Timer
 */

void wingo_timer_stop(wingo_timer_t *timer);

/*
 * Reset a timer.
 *
 * @param timer     Timer
 */

void wingo_timer_reset(wingo_timer_t *timer);

/*
 * Get elapsed time in nanoseconds.
 *
 * @param timer     Timer
 * @return          Elapsed nanoseconds
 */

wingo_i64 wingo_timer_elapsed_ns(const wingo_timer_t *timer);

/*
 * Get elapsed time in microseconds.
 *
 * @param timer     Timer
 * @return          Elapsed microseconds
 */

wingo_i64 wingo_timer_elapsed_us(const wingo_timer_t *timer);

/*
 * Get elapsed time in milliseconds.
 *
 * @param timer     Timer
 * @return          Elapsed milliseconds
 */

wingo_i64 wingo_timer_elapsed_ms(const wingo_timer_t *timer);

/*
 * Get elapsed time in seconds.
 *
 * @param timer     Timer
 * @return          Elapsed seconds
 */

double wingo_timer_elapsed_sec(const wingo_timer_t *timer);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_TIME_H */

