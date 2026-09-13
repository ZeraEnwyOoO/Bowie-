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

#ifndef WINGO_CORE_THREAD_H
#define WINGO_CORE_THREAD_H

/*
 * ============================================================================
 * WINGO THREADING
 * ============================================================================
 *
 * This module provides:
 *   - Thread abstraction (pthreads wrapper)
 *   - Thread pool for task execution
 *   - Work queue for pending tasks
 *   - Mutex and condition variable wrappers
 *   - Atomic operations
 *
 * Architecture:
 *
 *   ┌─────────────────────────────────────────────────────────────┐
 *   │                    THREAD POOL                              │
 *   │                                                             │
 *   │   ┌─────────────────────────────────────────────────────┐   │
 *   │   │                  WORK QUEUE                         │   │
 *   │   │                                                     │   │
 *   │   │   [Task 1] -> [Task 2] -> [Task 3] -> ...          │   │
 *   │   │                                                     │   │
 *   │   └──────────────────────┬──────────────────────────────┘   │
 *   │                          │                                  │
 *   │          ┌───────────────┼───────────────┐                  │
 *   │          │               │               │                  │
 *   │          ▼               ▼               ▼                  │
 *   │   ┌─────────────┐ ┌─────────────┐ ┌─────────────┐          │
 *   │   │  Worker 1   │ │  Worker 2   │ │  Worker N   │          │
 *   │   │  (thread)   │ │  (thread)   │ │  (thread)   │          │
 *   │   └─────────────┘ └─────────────┘ └─────────────┘          │
 *   │                                                             │
 *   └─────────────────────────────────────────────────────────────┘
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"
#include "wingo/log.h"

#include <pthread.h>

/* ============================================================================
 * THREAD STATE
 * ============================================================================ */

/*
 * Thread states.
 */

typedef enum {
    WINGO_THREAD_STATE_CREATED  = 0,    /* Created, not started */
    WINGO_THREAD_STATE_RUNNING  = 1,    /* Running */
    WINGO_THREAD_STATE_STOPPING = 2,    /* Stop requested */
    WINGO_THREAD_STATE_STOPPED  = 3,    /* Stopped */
    WINGO_THREAD_STATE_ERROR    = 4,    /* Error */
} wingo_thread_state_t;

/* ============================================================================
 * THREAD PRIORITY
 * ============================================================================ */

/*
 * Thread priority levels.
 *
 * NOTE: On Linux, thread priority requires CAP_SYS_NICE or
 *       running as root. These are hints.
 */

typedef enum {
    WINGO_THREAD_PRIO_LOW       = 0,
    WINGO_THREAD_PRIO_NORMAL    = 1,
    WINGO_THREAD_PRIO_HIGH      = 2,
    WINGO_THREAD_PRIO_REALTIME  = 3,
} wingo_thread_prio_t;

/* ============================================================================
 * THREAD STRUCTURE (OPAQUE)
 * ============================================================================ */

/*
 * Thread handle.
 */

typedef struct wingo_thread wingo_thread_t;

/* ============================================================================
 * THREAD FUNCTION
 * ============================================================================ */

/*
 * Thread entry function.
 *
 * @param arg       Argument passed to thread
 * @return          Return value (can be retrieved with join)
 */

typedef void *(*wingo_thread_func_t)(void *arg);

/* ============================================================================
 * THREAD LIFECYCLE
 * ============================================================================ */

/*
 * Create a new thread.
 *
 * @param func      Thread function
 * @param arg       Argument passed to thread function
 * @param name      Thread name (for debugging, may be NULL)
 * @return          New thread, or NULL on error
 */

wingo_thread_t *wingo_thread_new(wingo_thread_func_t func,
                                 void *arg,
                                 const char *name);

/*
 * Start a thread.
 *
 * @param thread    Thread
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_start(wingo_thread_t *thread);

/*
 * Wait for a thread to finish.
 *
 * @param thread    Thread
 * @param retval    Output: return value from thread function
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_join(wingo_thread_t *thread, void **retval);

/*
 * Detach a thread (auto-cleanup on exit).
 *
 * @param thread    Thread
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_detach(wingo_thread_t *thread);

/*
 * Request thread to stop.
 *
 * This sets a flag that the thread can check with
 * wingo_thread_should_stop().
 *
 * @param thread    Thread
 */

void wingo_thread_stop(wingo_thread_t *thread);

/*
 * Check if thread should stop.
 *
 * @param thread    Thread
 * @return          true if stop requested, false otherwise
 */

bool wingo_thread_should_stop(const wingo_thread_t *thread);

/*
 * Free a thread handle.
 *
 * @param thread    Thread (NULL is safe)
 */

void wingo_thread_free(wingo_thread_t *thread);

/* ============================================================================
 * THREAD INFO
 * ============================================================================ */

/*
 * Get thread state.
 *
 * @param thread    Thread
 * @return          Thread state
 */

wingo_thread_state_t wingo_thread_get_state(const wingo_thread_t *thread);

/*
 * Get thread name.
 *
 * @param thread    Thread
 * @return          Thread name (never NULL)
 */

const char *wingo_thread_get_name(const wingo_thread_t *thread);

/*
 * Get thread ID.
 *
 * @param thread    Thread
 * @return          Thread ID
 */

wingo_u64 wingo_thread_get_id(const wingo_thread_t *thread);

/*
 * Get current thread ID.
 *
 * @return          Current thread ID
 */

wingo_u64 wingo_thread_current_id(void);

/*
 * Set current thread name.
 *
 * @param name      Thread name
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_set_current_name(const char *name);

/*
 * Sleep for a specified number of milliseconds.
 *
 * @param ms        Milliseconds
 */

void wingo_thread_sleep_ms(wingo_i64 ms);

/*
 * Yield the processor.
 */

void wingo_thread_yield(void);

/* ============================================================================
 * THREAD POOL
 * ============================================================================ */

/*
 * Thread pool handle.
 */

typedef struct wingo_thread_pool wingo_thread_pool_t;

/*
 * Task function.
 *
 * @param arg       Argument passed to task
 */

typedef void (*wingo_task_func_t)(void *arg);

/*
 * Task structure.
 */

typedef struct {
    wingo_task_func_t   func;
    void               *arg;
    void              (*free_arg)(void *arg);   /* Optional cleanup */
} wingo_task_t;

/*
 * Create a new thread pool.
 *
 * @param num_threads Number of worker threads
 * @param queue_size  Maximum queue size (0 = unlimited)
 * @return            New thread pool, or NULL on error
 */

wingo_thread_pool_t *wingo_thread_pool_new(int num_threads, wingo_size queue_size);

/*
 * Free a thread pool.
 *
 * This stops all workers and waits for them to finish.
 *
 * @param pool      Thread pool (NULL is safe)
 */

void wingo_thread_pool_free(wingo_thread_pool_t *pool);

/*
 * Start a thread pool.
 *
 * @param pool      Thread pool
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_pool_start(wingo_thread_pool_t *pool);

/*
 * Stop a thread pool.
 *
 * This signals all workers to stop and waits for them.
 *
 * @param pool      Thread pool
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_pool_stop(wingo_thread_pool_t *pool);

/*
 * Submit a task to the pool.
 *
 * @param pool      Thread pool
 * @param func      Task function
 * @param arg       Argument passed to task
 * @param free_arg  Optional cleanup function for arg (may be NULL)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_pool_submit(wingo_thread_pool_t *pool,
                                       wingo_task_func_t func,
                                       void *arg,
                                       void (*free_arg)(void *arg));

/*
 * Wait for all pending tasks to complete.
 *
 * @param pool      Thread pool
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_thread_pool_wait(wingo_thread_pool_t *pool);

/* ============================================================================
 * THREAD POOL INFO
 * ============================================================================ */

/*
 * Get number of worker threads.
 *
 * @param pool      Thread pool
 * @return          Number of workers
 */

int wingo_thread_pool_num_workers(const wingo_thread_pool_t *pool);

/*
 * Get number of pending tasks.
 *
 * @param pool      Thread pool
 * @return          Number of pending tasks
 */

wingo_size wingo_thread_pool_pending(const wingo_thread_pool_t *pool);

/*
 * Get number of active tasks.
 *
 * @param pool      Thread pool
 * @return          Number of active tasks
 */

wingo_size wingo_thread_pool_active(const wingo_thread_pool_t *pool);

/*
 * Get total tasks processed.
 *
 * @param pool      Thread pool
 * @return          Total tasks processed
 */

wingo_u64 wingo_thread_pool_total(const wingo_thread_pool_t *pool);

/*
 * Print thread pool status.
 *
 * @param pool      Thread pool
 * @param f         Output file (NULL = stderr)
 */

void wingo_thread_pool_print(const wingo_thread_pool_t *pool, FILE *f);

/* ============================================================================
 * MUTEX
 * ============================================================================ */

/*
 * Mutex handle.
 */

typedef pthread_mutex_t wingo_mutex_t;

/*
 * Initialize a mutex.
 *
 * @param mutex     Mutex
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_mutex_init(wingo_mutex_t *mutex);

/*
 * Destroy a mutex.
 *
 * @param mutex     Mutex
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_mutex_destroy(wingo_mutex_t *mutex);

/*
 * Lock a mutex.
 *
 * @param mutex     Mutex
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_mutex_lock(wingo_mutex_t *mutex);

/*
 * Try to lock a mutex (non-blocking).
 *
 * @param mutex     Mutex
 * @return          WINGO_SUCCESS if locked, WINGO_ERR_BUSY if not
 */

wingo_error_t wingo_mutex_trylock(wingo_mutex_t *mutex);

/*
 * Unlock a mutex.
 *
 * @param mutex     Mutex
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_mutex_unlock(wingo_mutex_t *mutex);

/* ============================================================================
 * CONDITION VARIABLE
 * ============================================================================ */

/*
 * Condition variable handle.
 */

typedef pthread_cond_t wingo_cond_t;

/*
 * Initialize a condition variable.
 *
 * @param cond      Condition variable
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_cond_init(wingo_cond_t *cond);

/*
 * Destroy a condition variable.
 *
 * @param cond      Condition variable
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_cond_destroy(wingo_cond_t *cond);

/*
 * Wait on a condition variable.
 *
 * @param cond      Condition variable
 * @param mutex     Mutex (must be locked)
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_cond_wait(wingo_cond_t *cond, wingo_mutex_t *mutex);

/*
 * Wait on a condition variable with timeout.
 *
 * @param cond      Condition variable
 * @param mutex     Mutex (must be locked)
 * @param timeout_ms Timeout in milliseconds
 * @return          WINGO_SUCCESS if signaled, WINGO_ERR_TIMEOUT on timeout
 */

wingo_error_t wingo_cond_timedwait(wingo_cond_t *cond, wingo_mutex_t *mutex,
                                   wingo_i64 timeout_ms);

/*
 * Signal one waiter.
 *
 * @param cond      Condition variable
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_cond_signal(wingo_cond_t *cond);

/*
 * Signal all waiters.
 *
 * @param cond      Condition variable
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_cond_broadcast(wingo_cond_t *cond);

/* ============================================================================
 * ATOMIC OPERATIONS
 * ============================================================================ */

/*
 * Atomic integer.
 *
 * Uses GCC/Clang __atomic builtins.
 */

typedef struct {
    volatile int value;
} wingo_atomic_int_t;

/*
 * Initialize atomic integer.
 *
 * @param a         Atomic integer
 * @param value     Initial value
 */

static inline void wingo_atomic_init(wingo_atomic_int_t *a, int value)
{
    __atomic_store_n(&a->value, value, __ATOMIC_SEQ_CST);
}

/*
 * Load atomic integer.
 *
 * @param a         Atomic integer
 * @return          Current value
 */

static inline int wingo_atomic_load(const wingo_atomic_int_t *a)
{
    return __atomic_load_n(&a->value, __ATOMIC_SEQ_CST);
}

/*
 * Store atomic integer.
 *
 * @param a         Atomic integer
 * @param value     New value
 */

static inline void wingo_atomic_store(wingo_atomic_int_t *a, int value)
{
    __atomic_store_n(&a->value, value, __ATOMIC_SEQ_CST);
}

/*
 * Add to atomic integer.
 *
 * @param a         Atomic integer
 * @param delta     Amount to add
 * @return          Previous value
 */

static inline int wingo_atomic_add(wingo_atomic_int_t *a, int delta)
{
    return __atomic_fetch_add(&a->value, delta, __ATOMIC_SEQ_CST);
}

/*
 * Subtract from atomic integer.
 *
 * @param a         Atomic integer
 * @param delta     Amount to subtract
 * @return          Previous value
 */

static inline int wingo_atomic_sub(wingo_atomic_int_t *a, int delta)
{
    return __atomic_fetch_sub(&a->value, delta, __ATOMIC_SEQ_CST);
}

/*
 * Increment atomic integer.
 *
 * @param a         Atomic integer
 * @return          Previous value
 */

static inline int wingo_atomic_inc(wingo_atomic_int_t *a)
{
    return wingo_atomic_add(a, 1);
}

/*
 * Decrement atomic integer.
 *
 * @param a         Atomic integer
 * @return          Previous value
 */

static inline int wingo_atomic_dec(wingo_atomic_int_t *a)
{
    return wingo_atomic_sub(a, 1);
}

/*
 * Compare and swap.
 *
 * @param a         Atomic integer
 * @param expected  Expected value
 * @param desired   Desired value
 * @return          true if swapped, false otherwise
 */

static inline bool wingo_atomic_cas(wingo_atomic_int_t *a,
                                    int expected,
                                    int desired)
{
    return __atomic_compare_exchange_n(&a->value, &expected, desired,
                                       false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
}

/* ============================================================================
 * ONCE
 * ============================================================================ */

/*
 * Once control.
 *
 * Ensures a function is called exactly once.
 */

typedef pthread_once_t wingo_once_t;

#define WINGO_ONCE_INIT PTHREAD_ONCE_INIT

/*
 * Call a function exactly once.
 *
 * @param once      Once control
 * @param func      Function to call
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_once(wingo_once_t *once, void (*func)(void));

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_CORE_THREAD_H */
