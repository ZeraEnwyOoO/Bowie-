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

#include "wingo/util/time.h"

#include <errno.h>
#include <string.h>
#include <time.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Nanoseconds per unit.
 */
#define NS_PER_US   1000ULL
#define NS_PER_MS   1000000ULL
#define NS_PER_SEC  1000000000ULL

/*
 * Microseconds per unit.
 */
#define US_PER_MS   1000ULL
#define US_PER_SEC  1000000ULL

/*
 * Milliseconds per unit.
 */
#define MS_PER_SEC  1000ULL

/* ============================================================================
 * TIME FUNCTIONS
 * ============================================================================ */

wingo_error_t wingo_time_monotonic(wingo_timespec_t *ts)
{
    struct timespec real_ts;
    int rc;

    if (ts == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * CLOCK_MONOTONIC is the right choice for measuring intervals:
     *   - It's monotonic (never goes backward)
     *   - It's not affected by NTP adjustments
     *   - It's not affected by user changing system time
     *
     * This is essential for timeouts and rate limiting.
     */
    rc = clock_gettime(CLOCK_MONOTONIC, &real_ts);
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    ts->sec  = (wingo_i64)real_ts.tv_sec;
    ts->nsec = (wingo_i64)real_ts.tv_nsec;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_time_realtime(wingo_timespec_t *ts)
{
    struct timespec real_ts;
    int rc;

    if (ts == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * CLOCK_REALTIME is the wall clock:
     *   - Matches Unix timestamp
     *   - Can go backward (NTP, user change)
     *   - Use for timestamps, not for intervals
     */
    rc = clock_gettime(CLOCK_REALTIME, &real_ts);
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    ts->sec  = (wingo_i64)real_ts.tv_sec;
    ts->nsec = (wingo_i64)real_ts.tv_nsec;

    return WINGO_SUCCESS;
}

wingo_i64 wingo_time_now(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (wingo_i64)ts.tv_sec;
}

wingo_i64 wingo_time_now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (wingo_i64)ts.tv_sec * 1000 + (wingo_i64)ts.tv_nsec / 1000000;
}

wingo_i64 wingo_time_now_us(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (wingo_i64)ts.tv_sec * 1000000 + (wingo_i64)ts.tv_nsec / 1000;
}

wingo_i64 wingo_time_now_ns(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }

    return (wingo_i64)ts.tv_sec * 1000000000 + (wingo_i64)ts.tv_nsec;
}

wingo_i64 wingo_time_unix(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }

    return (wingo_i64)ts.tv_sec;
}

/* ============================================================================
 * TIME ARITHMETIC
 * ============================================================================ */

/*
 * Normalize timespec so nsec is in [0, 999999999].
 *
 * This is needed after addition/subtraction.
 */
static void time_normalize(wingo_timespec_t *ts)
{
    if (ts->nsec >= (wingo_i64)NS_PER_SEC) {
        ts->sec  += ts->nsec / (wingo_i64)NS_PER_SEC;
        ts->nsec %= (wingo_i64)NS_PER_SEC;
    } else if (ts->nsec < 0) {
        wingo_i64 borrow = (-ts->nsec + (wingo_i64)NS_PER_SEC - 1)
                         / (wingo_i64)NS_PER_SEC;
        ts->sec  -= borrow;
        ts->nsec += borrow * (wingo_i64)NS_PER_SEC;
    }
}

wingo_timespec_t wingo_time_add(wingo_timespec_t a, wingo_timespec_t b)
{
    wingo_timespec_t result;

    result.sec  = a.sec + b.sec;
    result.nsec = a.nsec + b.nsec;

    time_normalize(&result);

    return result;
}

wingo_timespec_t wingo_time_sub(wingo_timespec_t a, wingo_timespec_t b)
{
    wingo_timespec_t result;

    result.sec  = a.sec - b.sec;
    result.nsec = a.nsec - b.nsec;

    time_normalize(&result);

    return result;
}

int wingo_time_cmp(wingo_timespec_t a, wingo_timespec_t b)
{
    if (a.sec < b.sec) {
        return -1;
    }
    if (a.sec > b.sec) {
        return 1;
    }
    if (a.nsec < b.nsec) {
        return -1;
    }
    if (a.nsec > b.nsec) {
        return 1;
    }
    return 0;
}

wingo_i64 wingo_time_to_ms(wingo_timespec_t ts)
{
    return ts.sec * 1000 + ts.nsec / 1000000;
}

wingo_i64 wingo_time_to_us(wingo_timespec_t ts)
{
    return ts.sec * 1000000 + ts.nsec / 1000;
}

wingo_i64 wingo_time_to_ns(wingo_timespec_t ts)
{
    return ts.sec * 1000000000 + ts.nsec;
}

wingo_timespec_t wingo_time_from_ms(wingo_i64 ms)
{
    wingo_timespec_t ts;

    ts.sec  = ms / 1000;
    ts.nsec = (ms % 1000) * 1000000;

    if (ts.nsec < 0) {
        ts.sec  -= 1;
        ts.nsec += 1000000000;
    }

    return ts;
}

wingo_timespec_t wingo_time_from_us(wingo_i64 us)
{
    wingo_timespec_t ts;

    ts.sec  = us / 1000000;
    ts.nsec = (us % 1000000) * 1000;

    if (ts.nsec < 0) {
        ts.sec  -= 1;
        ts.nsec += 1000000000;
    }

    return ts;
}

wingo_timespec_t wingo_time_from_ns(wingo_i64 ns)
{
    wingo_timespec_t ts;

    ts.sec  = ns / 1000000000;
    ts.nsec = ns % 1000000000;

    if (ts.nsec < 0) {
        ts.sec  -= 1;
        ts.nsec += 1000000000;
    }

    return ts;
}

/* ============================================================================
 * TIME FORMATTING
 * ============================================================================ */

/*
 * Format time as ISO 8601.
 *
 * Example: 2024-01-15T14:30:45.123Z
 *
 * We use gmtime_r (thread-safe) instead of gmtime.
 */
int wingo_time_format_iso(wingo_timespec_t ts, char *buf, wingo_size size)
{
    struct tm tm;
    time_t sec;
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }

    sec = (time_t)ts.sec;

    /*
     * gmtime_r is thread-safe and doesn't use static storage.
     * Returns NULL on error.
     */
    if (gmtime_r(&sec, &tm) == NULL) {
        return -1;
    }

    n = snprintf(buf, size,
                 "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                 tm.tm_year + 1900,
                 tm.tm_mon + 1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec,
                 (int)(ts.nsec / 1000000));

    return n;
}

/*
 * Format time as human-readable string.
 *
 * Example: 2024-01-15 14:30:45
 */
int wingo_time_format_human(wingo_timespec_t ts, char *buf, wingo_size size)
{
    struct tm tm;
    time_t sec;
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }

    sec = (time_t)ts.sec;

    if (localtime_r(&sec, &tm) == NULL) {
        return -1;
    }

    n = snprintf(buf, size,
                 "%04d-%02d-%02d %02d:%02d:%02d",
                 tm.tm_year + 1900,
                 tm.tm_mon + 1,
                 tm.tm_mday,
                 tm.tm_hour,
                 tm.tm_min,
                 tm.tm_sec);

    return n;
}

/*
 * Format duration as human-readable string.
 *
 * Examples:
 *   500ns
 *   1.5us
 *   2.3ms
 *   1.5s
 *   2m 30s
 *   1h 15m
 *   2d 3h
 */
int wingo_time_format_duration(wingo_i64 ns, char *buf, wingo_size size)
{
    wingo_i64 abs_ns;
    int n;

    if (buf == NULL || size == 0) {
        return -1;
    }

    /* Handle negative */
    if (ns < 0) {
        n = snprintf(buf, size, "-");
        if (n < 0 || (wingo_size)n >= size) {
            return n;
        }
        buf += n;
        size -= n;
        abs_ns = -ns;
    } else {
        abs_ns = ns;
    }

    if (abs_ns < 1000) {
        /* Nanoseconds */
        n = snprintf(buf, size, "%lldns", (long long)abs_ns);
    } else if (abs_ns < 1000000) {
        /* Microseconds */
        n = snprintf(buf, size, "%.2fus", (double)abs_ns / 1000.0);
    } else if (abs_ns < 1000000000) {
        /* Milliseconds */
        n = snprintf(buf, size, "%.2fms", (double)abs_ns / 1000000.0);
    } else if (abs_ns < 60LL * 1000000000LL) {
        /* Seconds */
        n = snprintf(buf, size, "%.2fs", (double)abs_ns / 1000000000.0);
    } else if (abs_ns < 3600LL * 1000000000LL) {
        /* Minutes */
        wingo_i64 total_sec = abs_ns / 1000000000LL;
        wingo_i64 min = total_sec / 60;
        wingo_i64 sec = total_sec % 60;
        n = snprintf(buf, size, "%lldm %llds",
                     (long long)min, (long long)sec);
    } else if (abs_ns < 86400LL * 1000000000LL) {
        /* Hours */
        wingo_i64 total_sec = abs_ns / 1000000000LL;
        wingo_i64 hour = total_sec / 3600;
        wingo_i64 min = (total_sec % 3600) / 60;
        n = snprintf(buf, size, "%lldh %lldm",
                     (long long)hour, (long long)min);
    } else {
        /* Days */
        wingo_i64 total_sec = abs_ns / 1000000000LL;
        wingo_i64 day = total_sec / 86400;
        wingo_i64 hour = (total_sec % 86400) / 3600;
        n = snprintf(buf, size, "%lldd %lldh",
                     (long long)day, (long long)hour);
    }

    return n;
}

/* ============================================================================
 * SLEEP FUNCTIONS
 * ============================================================================ */

wingo_error_t wingo_time_sleep(wingo_timespec_t ts)
{
    struct timespec req;
    struct timespec rem;
    int rc;

    if (ts.sec < 0 || ts.nsec < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    req.tv_sec  = (time_t)ts.sec;
    req.tv_nsec = (long)ts.nsec;

    /*
     * nanosleep can be interrupted by a signal.
     * When that happens, it returns -1 with errno = EINTR,
     * and stores the remaining time in rem.
     *
     * We loop until fully slept.
     */
    while (1) {
        rc = nanosleep(&req, &rem);

        if (rc == 0) {
            return WINGO_SUCCESS;
        }

        if (errno != EINTR) {
            return WINGO_ERR_GENERIC;
        }

        /* Interrupted — continue with remaining time */
        req = rem;
    }
}

void wingo_time_sleep_sec(wingo_i64 sec)
{
    wingo_timespec_t ts;

    if (sec <= 0) {
        return;
    }

    ts.sec  = sec;
    ts.nsec = 0;

    wingo_time_sleep(ts);
}

void wingo_time_sleep_ms(wingo_i64 ms)
{
    wingo_timespec_t ts;

    if (ms <= 0) {
        return;
    }

    ts.sec  = ms / 1000;
    ts.nsec = (ms % 1000) * 1000000;

    wingo_time_sleep(ts);
}

void wingo_time_sleep_us(wingo_i64 us)
{
    wingo_timespec_t ts;

    if (us <= 0) {
        return;
    }

    ts.sec  = us / 1000000;
    ts.nsec = (us % 1000000) * 1000;

    wingo_time_sleep(ts);
}

void wingo_time_sleep_ns(wingo_i64 ns)
{
    wingo_timespec_t ts;

    if (ns <= 0) {
        return;
    }

    ts.sec  = ns / 1000000000;
    ts.nsec = ns % 1000000000;

    wingo_time_sleep(ts);
}

/* ============================================================================
 * TIMER
 * ============================================================================ */

void wingo_timer_init(wingo_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }

    memset(timer, 0, sizeof(*timer));
    timer->running = false;
}

void wingo_timer_start(wingo_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }

    wingo_time_monotonic(&timer->start);
    timer->running = true;
}

void wingo_timer_stop(wingo_timer_t *timer)
{
    if (timer == NULL || !timer->running) {
        return;
    }

    wingo_time_monotonic(&timer->end);
    timer->running = false;
}

void wingo_timer_reset(wingo_timer_t *timer)
{
    if (timer == NULL) {
        return;
    }

    memset(&timer->start, 0, sizeof(timer->start));
    memset(&timer->end, 0, sizeof(timer->end));
    timer->running = false;
}

/*
 * Get elapsed time.
 *
 * If timer is running, measures from start to now.
 * If timer is stopped, measures from start to end.
 */
static wingo_timespec_t timer_elapsed(const wingo_timer_t *timer)
{
    wingo_timespec_t now;
    wingo_timespec_t elapsed;

    if (timer->running) {
        wingo_time_monotonic(&now);
        elapsed = wingo_time_sub(now, timer->start);
    } else {
        elapsed = wingo_time_sub(timer->end, timer->start);
    }

    return elapsed;
}

wingo_i64 wingo_timer_elapsed_ns(const wingo_timer_t *timer)
{
    wingo_timespec_t elapsed;

    if (timer == NULL) {
        return 0;
    }

    elapsed = timer_elapsed(timer);
    return wingo_time_to_ns(elapsed);
}

wingo_i64 wingo_timer_elapsed_us(const wingo_timer_t *timer)
{
    wingo_timespec_t elapsed;

    if (timer == NULL) {
        return 0;
    }

    elapsed = timer_elapsed(timer);
    return wingo_time_to_us(elapsed);
}

wingo_i64 wingo_timer_elapsed_ms(const wingo_timer_t *timer)
{
    wingo_timespec_t elapsed;

    if (timer == NULL) {
        return 0;
    }

    elapsed = timer_elapsed(timer);
    return wingo_time_to_ms(elapsed);
}

double wingo_timer_elapsed_sec(const wingo_timer_t *timer)
{
    wingo_timespec_t elapsed;

    if (timer == NULL) {
        return 0.0;
    }

    elapsed = timer_elapsed(timer);
    return (double)elapsed.sec + (double)elapsed.nsec / 1000000000.0;
}
