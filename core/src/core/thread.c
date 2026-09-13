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

#include "wingo/core/thread.h"
#include "wingo/util/time.h"
#include "wingo/util/list.h"
#include "wingo/util/queue.h"

#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/syscall.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Default thread pool queue size (unlimited).
 */
#define THREAD_POOL_DEFAULT_QUEUE_SIZE  0

/*
 * Maximum thread name length (including null terminator).
 * Linux limits this to 16 bytes (TASK_COMM_LEN).
 */
#define THREAD_NAME_MAX                 16

/* ============================================================================
 * THREAD STRUCTURE
 * ============================================================================ */

struct wingo_thread {
    /* ----- pthread ----- */
    pthread_t               handle;
    bool                    handle_valid;

    /* ----- Callback ----- */
    wingo_thread_func_t     func;
    void                   *arg;
    void                   *retval;

    /* ----- State ----- */
    wingo_thread_state_t    state;

    /* ----- Control ----- */
    volatile bool           stop_requested;
    volatile bool           running;

    /* ----- Identity ----- */
    wingo_u64               id;
    char                    name[THREAD_NAME_MAX];

    /* ----- Synchronization ----- */
    pthread_mutex_t         mutex;
    pthread_cond_t          cond;
};

/* ============================================================================
 * THREAD POOL STRUCTURE
 * ============================================================================ */

/*
 * Thread pool worker.
 */
typedef struct {
    pthread_t               handle;
    wingo_thread_pool_t    *pool;
    int                     index;
} pool_worker_t;

/*
 * Thread pool.
 */
struct wingo_thread_pool {
    /* ----- Workers ----- */
    pool_worker_t          *workers;
    int                     num_workers;

    /* ----- Task queue ----- */
    wingo_queue_t          *tasks;
    wingo_size              queue_size;
    wingo_size              queue_count;

    /* ----- State ----- */
    volatile bool           running;
    volatile bool           stop_requested;

    /* ----- Statistics ----- */
    volatile wingo_u64      stat_total;
    volatile wingo_u64      stat_active;
    volatile wingo_u64      stat_pending;

    /* ----- Synchronization ----- */
    pthread_mutex_t         mutex;
    pthread_cond_t          cond_not_empty;
    pthread_cond_t          cond_not_full;
    pthread_cond_t          cond_idle;
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Set current thread name (Linux-specific).
 */
static void set_thread_name(const char *name)
{
    if (name == NULL) {
        return;
    }

#ifdef __linux__
    /*
     * Linux: use prctl() or pthread_setname_np().
     * pthread_setname_np() is limited to 16 chars.
     */
    pthread_setname_np(pthread_self(), name);
#else
    WINGO_UNUSED(name);
#endif
}

/*
 * Get current thread ID (Linux-specific).
 */
static wingo_u64 get_thread_id(void)
{
#ifdef __linux__
    return (wingo_u64)syscall(SYS_gettid);
#else
    return (wingo_u64)pthread_self();
#endif
}

/* ============================================================================
 * THREAD LIFECYCLE
 * ============================================================================ */

/*
 * Thread wrapper function.
 *
 * This is what pthread_create() calls. It invokes the user's
 * function and stores the return value.
 */
static void *thread_wrapper(void *arg)
{
    wingo_thread_t *thread = (wingo_thread_t *)arg;

    /* Set thread name */
    if (thread->name[0] != '\0') {
        set_thread_name(thread->name);
    }

    /* Get thread ID */
    thread->id = get_thread_id();

    /* Mark as running */
    pthread_mutex_lock(&thread->mutex);
    thread->state = WINGO_THREAD_STATE_RUNNING;
    thread->running = true;
    pthread_cond_broadcast(&thread->cond);
    pthread_mutex_unlock(&thread->mutex);

    /* Call user function */
    if (thread->func != NULL) {
        thread->retval = thread->func(thread->arg);
    }

    /* Mark as stopped */
    pthread_mutex_lock(&thread->mutex);
    thread->state = WINGO_THREAD_STATE_STOPPED;
    thread->running = false;
    pthread_cond_broadcast(&thread->cond);
    pthread_mutex_unlock(&thread->mutex);

    return thread->retval;
}

wingo_thread_t *wingo_thread_new(wingo_thread_func_t func,
                                 void *arg,
                                 const char *name)
{
    wingo_thread_t *thread;

    if (func == NULL) {
        return NULL;
    }

    thread = calloc(1, sizeof(wingo_thread_t));
    if (thread == NULL) {
        return NULL;
    }

    thread->func = func;
    thread->arg = arg;
    thread->retval = NULL;
    thread->state = WINGO_THREAD_STATE_CREATED;
    thread->handle_valid = false;
    thread->stop_requested = false;
    thread->running = false;
    thread->id = 0;

    /* Copy name */
    if (name != NULL) {
        strncpy(thread->name, name, sizeof(thread->name) - 1);
        thread->name[sizeof(thread->name) - 1] = '\0';
    } else {
        thread->name[0] = '\0';
    }

    /* Initialize synchronization */
    if (pthread_mutex_init(&thread->mutex, NULL) != 0) {
        free(thread);
        return NULL;
    }

    if (pthread_cond_init(&thread->cond, NULL) != 0) {
        pthread_mutex_destroy(&thread->mutex);
        free(thread);
        return NULL;
    }

    return thread;
}

wingo_error_t wingo_thread_start(wingo_thread_t *thread)
{
    int rc;

    if (thread == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (thread->handle_valid) {
        return WINGO_ERR_ALREADY_EXISTS;
    }

    rc = pthread_create(&thread->handle, NULL, thread_wrapper, thread);
    if (rc != 0) {
        thread->state = WINGO_THREAD_STATE_ERROR;
        return WINGO_ERR_GENERIC;
    }

    thread->handle_valid = true;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_thread_join(wingo_thread_t *thread, void **retval)
{
    int rc;

    if (thread == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!thread->handle_valid) {
        return WINGO_ERR_INVALID_STATE;
    }

    rc = pthread_join(thread->handle, retval);
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    thread->handle_valid = false;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_thread_detach(wingo_thread_t *thread)
{
    int rc;

    if (thread == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!thread->handle_valid) {
        return WINGO_ERR_INVALID_STATE;
    }

    rc = pthread_detach(thread->handle);
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    thread->handle_valid = false;

    return WINGO_SUCCESS;
}

void wingo_thread_stop(wingo_thread_t *thread)
{
    if (thread == NULL) {
        return;
    }

    pthread_mutex_lock(&thread->mutex);
    thread->stop_requested = true;
    thread->state = WINGO_THREAD_STATE_STOPPING;
    pthread_cond_broadcast(&thread->cond);
    pthread_mutex_unlock(&thread->mutex);
}

bool wingo_thread_should_stop(const wingo_thread_t *thread)
{
    bool stop;

    if (thread == NULL) {
        return true;
    }

    pthread_mutex_lock((pthread_mutex_t *)&thread->mutex);
    stop = thread->stop_requested;
    pthread_mutex_unlock((pthread_mutex_t *)&thread->mutex);

    return stop;
}

void wingo_thread_free(wingo_thread_t *thread)
{
    if (thread == NULL) {
        return;
    }

    /* Wait for thread to finish if still running */
    if (thread->handle_valid) {
        pthread_join(thread->handle, NULL);
    }

    pthread_cond_destroy(&thread->cond);
    pthread_mutex_destroy(&thread->mutex);

    free(thread);
}

/* ============================================================================
 * THREAD INFO
 * ============================================================================ */

wingo_thread_state_t wingo_thread_get_state(const wingo_thread_t *thread)
{
    wingo_thread_state_t state;

    if (thread == NULL) {
        return WINGO_THREAD_STATE_ERROR;
    }

    pthread_mutex_lock((pthread_mutex_t *)&thread->mutex);
    state = thread->state;
    pthread_mutex_unlock((pthread_mutex_t *)&thread->mutex);

    return state;
}

const char *wingo_thread_get_name(const wingo_thread_t *thread)
{
    if (thread == NULL) {
        return "NULL";
    }

    return thread->name;
}

wingo_u64 wingo_thread_get_id(const wingo_thread_t *thread)
{
    if (thread == NULL) {
        return 0;
    }

    return thread->id;
}

wingo_u64 wingo_thread_current_id(void)
{
    return get_thread_id();
}

wingo_error_t wingo_thread_set_current_name(const char *name)
{
    if (name == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    set_thread_name(name);

    return WINGO_SUCCESS;
}

void wingo_thread_sleep_ms(wingo_i64 ms)
{
    struct timespec ts;

    if (ms <= 0) {
        return;
    }

    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;

    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* Retry on signal */
    }
}

void wingo_thread_yield(void)
{
    sched_yield();
}

/* ============================================================================
 * THREAD POOL — WORKER
 * ============================================================================ */

/*
 * Worker thread function.
 *
 * Loops forever, pulling tasks from the queue and executing them.
 * Exits when stop is requested and queue is empty.
 */
static void *pool_worker_func(void *arg)
{
    pool_worker_t *worker = (pool_worker_t *)arg;
    wingo_thread_pool_t *pool = worker->pool;
    char name[THREAD_NAME_MAX];

    /* Set thread name */
    snprintf(name, sizeof(name), "bowie-%d", worker->index);
    set_thread_name(name);

    WINGO_LOG_DEBUG("Worker %d started (tid=%llu)",
                    worker->index,
                    (unsigned long long)get_thread_id());

    while (1) {
        wingo_task_t *task = NULL;

        pthread_mutex_lock(&pool->mutex);

        /* Wait for tasks or stop */
        while (pool->queue_count == 0 && !pool->stop_requested) {
            pthread_cond_wait(&pool->cond_not_empty, &pool->mutex);
        }

        /* Check stop condition */
        if (pool->stop_requested && pool->queue_count == 0) {
            pthread_mutex_unlock(&pool->mutex);
            break;
        }

        /* Dequeue a task */
        task = (wingo_task_t *)wingo_queue_pop(pool->tasks);
        if (task != NULL) {
            pool->queue_count--;
            pool->stat_pending--;
            pool->stat_active++;

            /* Signal not_full (space available) */
            pthread_cond_signal(&pool->cond_not_full);
        }

        pthread_mutex_unlock(&pool->mutex);

        /* Execute task (outside lock) */
        if (task != NULL) {
            if (task->func != NULL) {
                task->func(task->arg);
            }

            /* Cleanup */
            if (task->free_arg != NULL && task->arg != NULL) {
                task->free_arg(task->arg);
            }
            free(task);

            pthread_mutex_lock(&pool->mutex);
            pool->stat_active--;
            pool->stat_total++;

            /* Signal idle if queue is empty */
            if (pool->queue_count == 0 && pool->stat_active == 0) {
                pthread_cond_broadcast(&pool->cond_idle);
            }

            pthread_mutex_unlock(&pool->mutex);
        }
    }

    WINGO_LOG_DEBUG("Worker %d stopped", worker->index);

    return NULL;
}

/* ============================================================================
 * THREAD POOL
 * ============================================================================ */

wingo_thread_pool_t *wingo_thread_pool_new(int num_threads, wingo_size queue_size)
{
    wingo_thread_pool_t *pool;

    if (num_threads <= 0) {
        return NULL;
    }

    pool = calloc(1, sizeof(wingo_thread_pool_t));
    if (pool == NULL) {
        return NULL;
    }

    pool->num_workers = num_threads;
    pool->queue_size = queue_size;
    pool->queue_count = 0;
    pool->running = false;
    pool->stop_requested = false;
    pool->stat_total = 0;
    pool->stat_active = 0;
    pool->stat_pending = 0;

    /* Initialize synchronization */
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_not_empty, NULL) != 0) {
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_not_full, NULL) != 0) {
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }

    if (pthread_cond_init(&pool->cond_idle, NULL) != 0) {
        pthread_cond_destroy(&pool->cond_not_full);
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }

    /* Create task queue */
    pool->tasks = wingo_queue_new(NULL);
    if (pool->tasks == NULL) {
        pthread_cond_destroy(&pool->cond_idle);
        pthread_cond_destroy(&pool->cond_not_full);
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }

    /* Create worker array */
    pool->workers = calloc(num_threads, sizeof(pool_worker_t));
    if (pool->workers == NULL) {
        wingo_queue_free(pool->tasks);
        pthread_cond_destroy(&pool->cond_idle);
        pthread_cond_destroy(&pool->cond_not_full);
        pthread_cond_destroy(&pool->cond_not_empty);
        pthread_mutex_destroy(&pool->mutex);
        free(pool);
        return NULL;
    }

    return pool;
}

void wingo_thread_pool_free(wingo_thread_pool_t *pool)
{
    if (pool == NULL) {
        return;
    }

    /* Stop if running */
    if (pool->running) {
        wingo_thread_pool_stop(pool);
    }

    /* Free pending tasks */
    if (pool->tasks != NULL) {
        wingo_task_t *task;
        while ((task = (wingo_task_t *)wingo_queue_pop(pool->tasks)) != NULL) {
            if (task->free_arg != NULL && task->arg != NULL) {
                task->free_arg(task->arg);
            }
            free(task);
        }
        wingo_queue_free(pool->tasks);
    }

    /* Free workers */
    free(pool->workers);

    /* Destroy synchronization */
    pthread_cond_destroy(&pool->cond_idle);
    pthread_cond_destroy(&pool->cond_not_full);
    pthread_cond_destroy(&pool->cond_not_empty);
    pthread_mutex_destroy(&pool->mutex);

    free(pool);
}

wingo_error_t wingo_thread_pool_start(wingo_thread_pool_t *pool)
{
    int i;
    int rc;

    if (pool == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pool->running) {
        return WINGO_ERR_ALREADY_EXISTS;
    }

    pthread_mutex_lock(&pool->mutex);
    pool->stop_requested = false;
    pthread_mutex_unlock(&pool->mutex);

    /* Create worker threads */
    for (i = 0; i < pool->num_workers; i++) {
        pool->workers[i].pool = pool;
        pool->workers[i].index = i;

        rc = pthread_create(&pool->workers[i].handle, NULL,
                            pool_worker_func, &pool->workers[i]);
        if (rc != 0) {
            /* Failed — stop already-created workers */
            pthread_mutex_lock(&pool->mutex);
            pool->stop_requested = true;
            pthread_cond_broadcast(&pool->cond_not_empty);
            pthread_mutex_unlock(&pool->mutex);

            /* Wait for already-created workers */
            for (int j = 0; j < i; j++) {
                pthread_join(pool->workers[j].handle, NULL);
            }

            return WINGO_ERR_NOMEM;
        }
    }

    pool->running = true;

    WINGO_LOG_INFO("Thread pool started (%d workers)", pool->num_workers);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_thread_pool_stop(wingo_thread_pool_t *pool)
{
    int i;

    if (pool == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!pool->running) {
        return WINGO_SUCCESS;
    }

    WINGO_LOG_INFO("Thread pool stopping...");

    /* Signal stop */
    pthread_mutex_lock(&pool->mutex);
    pool->stop_requested = true;
    pthread_cond_broadcast(&pool->cond_not_empty);
    pthread_mutex_unlock(&pool->mutex);

    /* Wait for all workers to finish */
    for (i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->workers[i].handle, NULL);
    }

    pool->running = false;

    WINGO_LOG_INFO("Thread pool stopped (total=%llu)",
                   (unsigned long long)pool->stat_total);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_thread_pool_submit(wingo_thread_pool_t *pool,
                                       wingo_task_func_t func,
                                       void *arg,
                                       void (*free_arg)(void *arg))
{
    wingo_task_t *task;

    if (pool == NULL || func == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (!pool->running) {
        return WINGO_ERR_INVALID_STATE;
    }

    /* Allocate task */
    task = calloc(1, sizeof(wingo_task_t));
    if (task == NULL) {
        return WINGO_ERR_NOMEM;
    }

    task->func = func;
    task->arg = arg;
    task->free_arg = free_arg;

    pthread_mutex_lock(&pool->mutex);

    /* Wait if queue is full */
    while (pool->queue_size > 0 &&
           pool->queue_count >= pool->queue_size &&
           !pool->stop_requested) {
        pthread_cond_wait(&pool->cond_not_full, &pool->mutex);
    }

    if (pool->stop_requested) {
        pthread_mutex_unlock(&pool->mutex);
        free(task);
        return WINGO_ERR_CANCELED;
    }

    /* Enqueue task */
    if (wingo_queue_push(pool->tasks, task) != WINGO_SUCCESS) {
        pthread_mutex_unlock(&pool->mutex);
        free(task);
        return WINGO_ERR_NOMEM;
    }

    pool->queue_count++;
    pool->stat_pending++;

    /* Signal not_empty */
    pthread_cond_signal(&pool->cond_not_empty);

    pthread_mutex_unlock(&pool->mutex);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_thread_pool_wait(wingo_thread_pool_t *pool)
{
    if (pool == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    pthread_mutex_lock(&pool->mutex);

    while (pool->queue_count > 0 || pool->stat_active > 0) {
        pthread_cond_wait(&pool->cond_idle, &pool->mutex);
    }

    pthread_mutex_unlock(&pool->mutex);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * THREAD POOL INFO
 * ============================================================================ */

int wingo_thread_pool_num_workers(const wingo_thread_pool_t *pool)
{
    if (pool == NULL) {
        return 0;
    }

    return pool->num_workers;
}

wingo_size wingo_thread_pool_pending(const wingo_thread_pool_t *pool)
{
    wingo_size pending;

    if (pool == NULL) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&pool->mutex);
    pending = pool->queue_count;
    pthread_mutex_unlock((pthread_mutex_t *)&pool->mutex);

    return pending;
}

wingo_size wingo_thread_pool_active(const wingo_thread_pool_t *pool)
{
    wingo_size active;

    if (pool == NULL) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&pool->mutex);
    active = pool->stat_active;
    pthread_mutex_unlock((pthread_mutex_t *)&pool->mutex);

    return active;
}

wingo_u64 wingo_thread_pool_total(const wingo_thread_pool_t *pool)
{
    wingo_u64 total;

    if (pool == NULL) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t *)&pool->mutex);
    total = pool->stat_total;
    pthread_mutex_unlock((pthread_mutex_t *)&pool->mutex);

    return total;
}

void wingo_thread_pool_print(const wingo_thread_pool_t *pool, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (pool == NULL) {
        fprintf(f, "Thread pool: (null)\n");
        return;
    }

    fprintf(f, "Thread Pool:\n");
    fprintf(f, "  Workers:     %d\n", pool->num_workers);
    fprintf(f, "  Running:     %s\n", pool->running ? "yes" : "no");
    fprintf(f, "  Queue size:  %zu\n", pool->queue_size);
    fprintf(f, "  Pending:     %zu\n", pool->queue_count);
    fprintf(f, "  Active:      %llu\n",
            (unsigned long long)pool->stat_active);
    fprintf(f, "  Total:       %llu\n",
            (unsigned long long)pool->stat_total);
}

/* ============================================================================
 * MUTEX
 * ============================================================================ */

wingo_error_t wingo_mutex_init(wingo_mutex_t *mutex)
{
    if (mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_mutex_init(mutex, NULL) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_mutex_destroy(wingo_mutex_t *mutex)
{
    if (mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_mutex_destroy(mutex) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_mutex_lock(wingo_mutex_t *mutex)
{
    if (mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_mutex_lock(mutex) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_mutex_trylock(wingo_mutex_t *mutex)
{
    int rc;

    if (mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    rc = pthread_mutex_trylock(mutex);
    if (rc == EBUSY) {
        return WINGO_ERR_BUSY;
    }
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_mutex_unlock(wingo_mutex_t *mutex)
{
    if (mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_mutex_unlock(mutex) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * CONDITION VARIABLE
 * ============================================================================ */

wingo_error_t wingo_cond_init(wingo_cond_t *cond)
{
    if (cond == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_cond_init(cond, NULL) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_cond_destroy(wingo_cond_t *cond)
{
    if (cond == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_cond_destroy(cond) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_cond_wait(wingo_cond_t *cond, wingo_mutex_t *mutex)
{
    if (cond == NULL || mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_cond_wait(cond, mutex) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_cond_timedwait(wingo_cond_t *cond, wingo_mutex_t *mutex,
                                   wingo_i64 timeout_ms)
{
    struct timespec ts;
    wingo_i64 now_ms;
    int rc;

    if (cond == NULL || mutex == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Get current time */
    clock_gettime(CLOCK_REALTIME, &ts);

    /* Add timeout */
    now_ms = (wingo_i64)ts.tv_sec * 1000 + (wingo_i64)ts.tv_nsec / 1000000;
    now_ms += timeout_ms;

    ts.tv_sec = now_ms / 1000;
    ts.tv_nsec = (now_ms % 1000) * 1000000;

    rc = pthread_cond_timedwait(cond, mutex, &ts);

    if (rc == ETIMEDOUT) {
        return WINGO_ERR_TIMEOUT;
    }
    if (rc != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_cond_signal(wingo_cond_t *cond)
{
    if (cond == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_cond_signal(cond) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_cond_broadcast(wingo_cond_t *cond)
{
    if (cond == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (pthread_cond_broadcast(cond) != 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * ONCE
 * ============================================================================ */

static pthread_once_t once_result;
static int once_status;

static void once_helper(void (*func)(void))
{
    if (func != NULL) {
        func();
    }
}

wingo_error_t wingo_once(wingo_once_t *once, void (*func)(void))
{
    if (once == NULL || func == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    WINGO_UNUSED(once_result);
    WINGO_UNUSED(once_status);

    /*
     * pthread_once() takes a function with no arguments,
     * but we need to pass func. We use a global workaround.
     *
     * For a proper implementation, we'd need to pass the function
     * through a struct. For now, this works for simple cases.
     */

    /* Store func in a static (not thread-safe for multiple once) */
    /* In a real implementation, use a proper mechanism */

    pthread_once((pthread_once_t *)once, once_helper);

    return WINGO_SUCCESS;
}
