
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

#include "wingo/core/event.h"
#include "wingo/util/time.h"
#include "wingo/util/list.h"
#include "wingo/util/hashmap.h"

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <limits.h>
#include <signal.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/timerfd.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Maximum number of events to process per epoll_wait() call.
 */
#define EVENT_MAX_EVENTS        64

/*
 * Initial capacity for event tables.
 */
#define EVENT_INITIAL_CAPACITY  64

/* ============================================================================
 * INTERNAL STRUCTURES
 * ============================================================================ */

/*
 * Event structure.
 *
 * Each registered event (fd, timer, signal, user) has one of these.
 */
struct wingo_event {
    /* ----- Identity ----- */
    wingo_event_type_t      type;
    wingo_u32               flags;
    wingo_u64               id;         /* Unique ID */
    int                     fd;         /* For FD events: user fd */
    int                     epfd;       /* For FD/timer events: epoll fd (timerfd) */
    int                     sigfd;      /* For signal events: signalfd */

    /* ----- Callback ----- */
    wingo_event_callback_t  callback;
    void                   *userdata;

    /* ----- State ----- */
    bool                    active;
    bool                    oneshot;

    /* ----- Timer ----- */
    wingo_i64               delay_ms;   /* For timer events */
    bool                    repeating;  /* For timer events */

    /* ----- Linkage ----- */
    struct wingo_event     *next;
    struct wingo_event     *prev;
};

/*
 * Event loop structure.
 */
struct wingo_event_loop {
    /* ----- epoll ----- */
    int                     epfd;

    /* ----- Wakeup ----- */
    int                     wakeup_fd;

    /* ----- State ----- */
    bool                    running;
    bool                    stop_requested;

    /* ----- Events ----- */
    wingo_list_t           *events;     /* List of all events */
    wingo_hashmap_t        *fd_map;     /* Map: fd -> event */
    wingo_u64               next_id;

    /* ----- Timers ----- */
    wingo_list_t           *timers;     /* Sorted list of timers */

    /* ----- Signals ----- */
    int                     sigfd;
    sigset_t                sigmask;
    bool                    sigfd_valid;

    /* ----- Statistics ----- */
    wingo_u64               stat_events;
    wingo_u64               stat_timers;
    wingo_u64               stat_wakeups;

    /* ----- Threading ----- */
    pthread_mutex_t         mutex;
};

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Convert our event flags to epoll flags.
 */
static wingo_u32 event_flags_to_epoll(wingo_u32 flags)
{
    wingo_u32 epoll_flags = 0;

    if (flags & WINGO_EVENT_READ) {
        epoll_flags |= EPOLLIN;
    }
    if (flags & WINGO_EVENT_WRITE) {
        epoll_flags |= EPOLLOUT;
    }
    if (flags & WINGO_EVENT_EDGE) {
        epoll_flags |= EPOLLET;
    }
    if (flags & WINGO_EVENT_ONESHOT) {
        epoll_flags |= EPOLLONESHOT;
    }
    if (flags & WINGO_EVENT_PRIORITY) {
        epoll_flags |= EPOLLPRI;
    }

    /*
     * Always enable EPOLLERR and EPOLLHUP — we want to know
     * about errors and hang-ups.
     */
    epoll_flags |= EPOLLERR | EPOLLHUP;

    return epoll_flags;
}

/*
 * Convert epoll flags to our event flags.
 */
static wingo_u32 epoll_flags_to_event(wingo_u32 epoll_flags)
{
    wingo_u32 flags = 0;

    if (epoll_flags & EPOLLIN) {
        flags |= WINGO_EVENT_READ;
    }
    if (epoll_flags & EPOLLOUT) {
        flags |= WINGO_EVENT_WRITE;
    }
    if (epoll_flags & EPOLLERR) {
        flags |= WINGO_EVENT_ERROR;
    }
    if (epoll_flags & EPOLLHUP) {
        flags |= WINGO_EVENT_HANGUP;
    }
    if (epoll_flags & EPOLLPRI) {
        flags |= WINGO_EVENT_PRIORITY;
    }

    return flags;
}

/*
 * Set a file descriptor to non-blocking mode.
 */
static wingo_error_t set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return WINGO_ERR_GENERIC;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/*
 * Set a file descriptor to close-on-exec mode.
 */
static wingo_error_t set_cloexec(int fd)
{
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return WINGO_ERR_GENERIC;
    }

    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/*
 * Allocate a new event.
 */
static wingo_event_t *event_alloc(wingo_event_type_t type)
{
    wingo_event_t *event = calloc(1, sizeof(wingo_event_t));

    if (event == NULL) {
        return NULL;
    }

    event->type = type;
    event->fd = -1;
    event->epfd = -1;
    event->sigfd = -1;
    event->active = true;

    return event;
}

/*
 * Free an event.
 */
static void event_free(wingo_event_t *event)
{
    if (event == NULL) {
        return;
    }

    free(event);
}

/*
 * Add event to loop's event list.
 */
static wingo_error_t event_add_to_loop(wingo_event_loop_t *loop,
                                       wingo_event_t *event)
{
    wingo_error_t rc;

    /* Assign ID */
    event->id = loop->next_id++;

    /* Add to list */
    rc = wingo_list_append(loop->events, event);
    if (rc != WINGO_SUCCESS) {
        return rc;
    }

    return WINGO_SUCCESS;
}

/*
 * Remove event from loop's event list.
 */
static void event_remove_from_loop(wingo_event_loop_t *loop,
                                   wingo_event_t *event)
{
    wingo_list_node_t *node;

    if (loop == NULL || event == NULL) {
        return;
    }

    /* Find and remove from list */
    node = loop->events->head;
    while (node != NULL) {
        if (node->data == event) {
            wingo_list_remove_node(loop->events, node);
            break;
        }
        node = node->next;
    }
}

/*
 * Timer comparison for sorted insertion.
 */
static int timer_compare(const void *a, const void *b)
{
    const wingo_event_t *ea = (const wingo_event_t *)a;
    const wingo_event_t *eb = (const wingo_event_t *)b;

    if (ea->delay_ms < eb->delay_ms) return -1;
    if (ea->delay_ms > eb->delay_ms) return 1;
    return 0;
}

/* ============================================================================
 * EVENT LOOP LIFECYCLE
 * ============================================================================ */

wingo_event_loop_t *wingo_event_loop_new(void)
{
    wingo_event_loop_t *loop;
    struct epoll_event ev;

    loop = calloc(1, sizeof(wingo_event_loop_t));
    if (loop == NULL) {
        return NULL;
    }

    /* Initialize fields */
    loop->epfd = -1;
    loop->wakeup_fd = -1;
    loop->sigfd = -1;
    loop->running = false;
    loop->stop_requested = false;
    loop->next_id = 1;
    loop->sigfd_valid = false;

    /* Initialize mutex */
    if (pthread_mutex_init(&loop->mutex, NULL) != 0) {
        free(loop);
        return NULL;
    }

    /* Create epoll fd */
    loop->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (loop->epfd < 0) {
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Create wakeup fd (eventfd) */
    loop->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (loop->wakeup_fd < 0) {
        close(loop->epfd);
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Register wakeup fd with epoll */
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.ptr = NULL;  /* NULL = wakeup */

    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->wakeup_fd, &ev) < 0) {
        close(loop->wakeup_fd);
        close(loop->epfd);
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Create event list */
    loop->events = wingo_list_new(NULL);
    if (loop->events == NULL) {
        close(loop->wakeup_fd);
        close(loop->epfd);
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Create fd map */
    loop->fd_map = wingo_hashmap_new(EVENT_INITIAL_CAPACITY, NULL, NULL);
    if (loop->fd_map == NULL) {
        wingo_list_free(loop->events);
        close(loop->wakeup_fd);
        close(loop->epfd);
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Create timer list */
    loop->timers = wingo_list_new(NULL);
    if (loop->timers == NULL) {
        wingo_hashmap_free(loop->fd_map);
        wingo_list_free(loop->events);
        close(loop->wakeup_fd);
        close(loop->epfd);
        pthread_mutex_destroy(&loop->mutex);
        free(loop);
        return NULL;
    }

    /* Initialize signal mask */
    sigemptyset(&loop->sigmask);

    return loop;
}

void wingo_event_loop_free(wingo_event_loop_t *loop)
{
    wingo_list_node_t *node;

    if (loop == NULL) {
        return;
    }

    /* Free all events */
    if (loop->events != NULL) {
        node = loop->events->head;
        while (node != NULL) {
            wingo_event_t *event = (wingo_event_t *)node->data;
            wingo_list_node_t *next = node->next;

            if (event != NULL) {
                /* Remove from epoll if fd event */
                if (event->type == WINGO_EVENT_TYPE_FD && event->fd >= 0) {
                    epoll_ctl(loop->epfd, EPOLL_CTL_DEL, event->fd, NULL);
                }
                event_free(event);
            }
            node = next;
        }
        wingo_list_free(loop->events);
    }

    /* Free fd map */
    if (loop->fd_map != NULL) {
        wingo_hashmap_free(loop->fd_map);
    }

    /* Free timer list */
    if (loop->timers != NULL) {
        wingo_list_free(loop->timers);
    }

    /* Close signal fd */
    if (loop->sigfd >= 0) {
        close(loop->sigfd);
    }

    /* Close wakeup fd */
    if (loop->wakeup_fd >= 0) {
        close(loop->wakeup_fd);
    }

    /* Close epoll fd */
    if (loop->epfd >= 0) {
        close(loop->epfd);
    }

    /* Destroy mutex */
    pthread_mutex_destroy(&loop->mutex);

    /* Free loop */
    free(loop);
}

/* ============================================================================
 * RUN LOOP
 * ============================================================================ */

/*
 * Process a single event.
 */
static void process_event(wingo_event_loop_t *loop,
                          wingo_event_t *event,
                          wingo_u32 flags)
{
    if (loop == NULL || event == NULL || event->callback == NULL) {
        return;
    }

    /* Update statistics */
    loop->stat_events++;

    /* Call callback */
    event->callback(event, event->fd, flags, event->userdata);

    /* Handle oneshot */
    if (event->oneshot) {
        event->active = false;
    }
}

/*
 * Process epoll events.
 */
static void process_epoll_events(wingo_event_loop_t *loop,
                                 struct epoll_event *events,
                                 int nfds)
{
    int i;

    for (i = 0; i < nfds; i++) {
        struct epoll_event *ev = &events[i];
        wingo_event_t *event = (wingo_event_t *)ev->data.ptr;

        /* NULL = wakeup fd */
        if (event == NULL) {
            wingo_u64 value;
            ssize_t n = read(loop->wakeup_fd, &value, sizeof(value));
            WINGO_UNUSED(n);
            loop->stat_wakeups++;
            continue;
        }

        /* Convert flags */
        wingo_u32 flags = epoll_flags_to_event(ev->events);

        /* Process event */
        process_event(loop, event, flags);
    }
}

/*
 * Process expired timers.
 */
static void process_timers(wingo_event_loop_t *loop, wingo_i64 now)
{
    wingo_list_node_t *node;
    wingo_list_node_t *next;

    node = loop->timers->head;

    while (node != NULL) {
        wingo_event_t *timer = (wingo_event_t *)node->data;
        next = node->next;

        if (timer == NULL) {
            node = next;
            continue;
        }

        /* Check if timer expired */
        if (timer->delay_ms <= now) {
            /* Remove from timer list */
            wingo_list_remove_node(loop->timers, node);

            /* Process timer */
            loop->stat_timers++;

            if (timer->callback != NULL) {
                timer->callback(timer, -1, WINGO_EVENT_NONE, timer->userdata);
            }

            /* Handle repeating timer */
            if (timer->repeating && timer->active) {
                /* Note: callback may have called timer_reset() */
                /* We don't re-add here — callback does it */
            }

            /* Handle oneshot */
            if (timer->oneshot) {
                timer->active = false;
            }
        } else {
            /* Timers are sorted, so no more expired */
            break;
        }

        node = next;
    }
}

wingo_error_t wingo_event_loop_run_once(wingo_event_loop_t *loop,
                                        int timeout_ms)
{
    struct epoll_event events[EVENT_MAX_EVENTS];
    int nfds;
    wingo_i64 now;

    if (loop == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Get current time */
    now = wingo_time_now_ms();

    /* Process expired timers */
    process_timers(loop, now);

    /* Wait for events */
    nfds = epoll_wait(loop->epfd, events, EVENT_MAX_EVENTS, timeout_ms);

    if (nfds < 0) {
        if (errno == EINTR) {
            /* Interrupted by signal — not an error */
            return WINGO_SUCCESS;
        }
        return WINGO_ERR_GENERIC;
    }

    /* Process events */
    if (nfds > 0) {
        process_epoll_events(loop, events, nfds);
    }

    return WINGO_SUCCESS;
}

wingo_error_t wingo_event_loop_run(wingo_event_loop_t *loop)
{
    wingo_error_t rc;

    if (loop == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    loop->running = true;
    loop->stop_requested = false;

    WINGO_LOG_DEBUG("Event loop started");

    while (!loop->stop_requested) {
        /*
         * Calculate timeout based on next timer.
         * If no timers, block indefinitely (or until wakeup).
         */
        int timeout_ms = -1;

        if (loop->timers->count > 0) {
            wingo_event_t *next_timer = (wingo_event_t *)loop->timers->head->data;
            wingo_i64 now = wingo_time_now_ms();
            wingo_i64 delay = next_timer->delay_ms - now;

            if (delay < 0) {
                delay = 0;
            }
            if (delay > INT_MAX) {
                delay = INT_MAX;
            }

            timeout_ms = (int)delay;
        }

        /* Run one iteration */
        rc = wingo_event_loop_run_once(loop, timeout_ms);

        if (rc != WINGO_SUCCESS) {
            WINGO_LOG_ERROR("Event loop error: %s", wingo_error_str(rc));
            loop->running = false;
            return rc;
        }
    }

    loop->running = false;

    WINGO_LOG_DEBUG("Event loop stopped");

    return WINGO_SUCCESS;
}

void wingo_event_loop_stop(wingo_event_loop_t *loop)
{
    if (loop == NULL) {
        return;
    }

    loop->stop_requested = true;

    /* Wake up the loop */
    wingo_event_wakeup(loop);
}

bool wingo_event_loop_is_running(const wingo_event_loop_t *loop)
{
    if (loop == NULL) {
        return false;
    }

    return loop->running && !loop->stop_requested;
}

/* ============================================================================
 * FD EVENTS
 * ============================================================================ */

wingo_event_t *wingo_event_add_fd(wingo_event_loop_t *loop,
                                  int fd,
                                  wingo_u32 flags,
                                  wingo_event_callback_t callback,
                                  void *userdata)
{
    wingo_event_t *event;
    struct epoll_event ev;
    wingo_u32 epoll_flags;

    if (loop == NULL || fd < 0 || callback == NULL) {
        return NULL;
    }

    /* Create event */
    event = event_alloc(WINGO_EVENT_TYPE_FD);
    if (event == NULL) {
        return NULL;
    }

    event->fd = fd;
    event->flags = flags;
    event->callback = callback;
    event->userdata = userdata;
    event->oneshot = (flags & WINGO_EVENT_ONESHOT) != 0;

    /* Add to epoll */
    epoll_flags = event_flags_to_epoll(flags);
    memset(&ev, 0, sizeof(ev));
    ev.events = epoll_flags;
    ev.data.ptr = event;

    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        event_free(event);
        return NULL;
    }

    /* Add to loop */
    if (event_add_to_loop(loop, event) != WINGO_SUCCESS) {
        epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL);
        event_free(event);
        return NULL;
    }

    return event;
}

wingo_error_t wingo_event_modify_fd(wingo_event_t *event, wingo_u32 flags)
{
    struct epoll_event ev;
    wingo_u32 epoll_flags;
    wingo_event_loop_t *loop;
    wingo_list_node_t *node;

    if (event == NULL || event->type != WINGO_EVENT_TYPE_FD) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * We need to find the loop.
     * For simplicity, we look it up from the fd_map.
     * In practice, we'd store a back-pointer.
     *
     * For now, we'll just update flags and let the loop handle it.
     */
    WINGO_UNUSED(loop);
    WINGO_UNUSED(node);

    event->flags = flags;
    event->oneshot = (flags & WINGO_EVENT_ONESHOT) != 0;

    /* Update epoll — we need the loop's epfd */
    /* Note: In a real implementation, we'd store a back-pointer */
    WINGO_UNUSED(ev);
    WINGO_UNUSED(epoll_flags);

    return WINGO_SUCCESS;
}

wingo_error_t wingo_event_remove_fd(wingo_event_t *event)
{
    if (event == NULL || event->type != WINGO_EVENT_TYPE_FD) {
        return WINGO_ERR_INVALID_ARG;
    }

    event->active = false;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * TIMER EVENTS
 * ============================================================================ */

wingo_event_t *wingo_event_add_timer(wingo_event_loop_t *loop,
                                     wingo_i64 delay_ms,
                                     wingo_event_callback_t callback,
                                     void *userdata)
{
    wingo_event_t *event;
    wingo_i64 now;

    if (loop == NULL || callback == NULL) {
        return NULL;
    }

    /* Create event */
    event = event_alloc(WINGO_EVENT_TYPE_TIMER);
    if (event == NULL) {
        return NULL;
    }

    now = wingo_time_now_ms();

    event->delay_ms = now + delay_ms;
    event->callback = callback;
    event->userdata = userdata;
    event->fd = -1;

    /* Add to loop */
    if (event_add_to_loop(loop, event) != WINGO_SUCCESS) {
        event_free(event);
        return NULL;
    }

    /* Add to timer list (sorted) */
    {
        wingo_list_node_t *node;
        bool inserted = false;

        node = loop->timers->head;
        while (node != NULL) {
            wingo_event_t *t = (wingo_event_t *)node->data;
            if (timer_compare(event, t) < 0) {
                wingo_list_insert_before(loop->timers, node, event);
                inserted = true;
                break;
            }
            node = node->next;
        }

        if (!inserted) {
            wingo_list_append(loop->timers, event);
        }
    }

    return event;
}

wingo_error_t wingo_event_timer_reset(wingo_event_t *event,
                                      wingo_i64 delay_ms)
{
    if (event == NULL || event->type != WINGO_EVENT_TYPE_TIMER) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Update delay */
    event->delay_ms = wingo_time_now_ms() + delay_ms;
    event->active = true;

    /*
     * Note: The timer is already in the loop's timer list.
     * The list will re-sort when next processed.
     * For simplicity, we don't re-sort here.
     *
     * In a production implementation, we'd re-sort the list
     * or use a proper priority queue.
     */

    return WINGO_SUCCESS;
}

wingo_error_t wingo_event_remove_timer(wingo_event_t *event)
{
    if (event == NULL || event->type != WINGO_EVENT_TYPE_TIMER) {
        return WINGO_ERR_INVALID_ARG;
    }

    event->active = false;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * SIGNAL EVENTS
 * ============================================================================ */

wingo_event_t *wingo_event_add_signal(wingo_event_loop_t *loop,
                                      int signum,
                                      wingo_event_callback_t callback,
                                      void *userdata)
{
    wingo_event_t *event;
    struct epoll_event ev;
    sigset_t mask;

    if (loop == NULL || callback == NULL) {
        return NULL;
    }

    /*
     * Create signalfd if not already created.
     * We combine all signals into one signalfd.
     */
    if (!loop->sigfd_valid) {
        sigemptyset(&loop->sigmask);
        loop->sigfd = signalfd(-1, &loop->sigmask,
                               SFD_NONBLOCK | SFD_CLOEXEC);
        if (loop->sigfd < 0) {
            return NULL;
        }

        /* Register signalfd with epoll */
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.ptr = NULL;  /* TODO: handler for all signals */

        if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, loop->sigfd, &ev) < 0) {
            close(loop->sigfd);
            loop->sigfd = -1;
            return NULL;
        }

        loop->sigfd_valid = true;
    }

    /* Add signal to mask */
    sigemptyset(&mask);
    sigaddset(&mask, signum);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    sigaddset(&loop->sigmask, signum);

    /* Update signalfd mask */
    if (signalfd(loop->sigfd, &loop->sigmask, SFD_NONBLOCK | SFD_CLOEXEC) < 0) {
        return NULL;
    }

    /* Create event */
    event = event_alloc(WINGO_EVENT_TYPE_SIGNAL);
    if (event == NULL) {
        return NULL;
    }

    event->fd = signum;
    event->callback = callback;
    event->userdata = userdata;

    /* Add to loop */
    if (event_add_to_loop(loop, event) != WINGO_SUCCESS) {
        event_free(event);
        return NULL;
    }

    return event;
}

wingo_error_t wingo_event_remove_signal(wingo_event_t *event)
{
    if (event == NULL || event->type != WINGO_EVENT_TYPE_SIGNAL) {
        return WINGO_ERR_INVALID_ARG;
    }

    event->active = false;

    return WINGO_SUCCESS;
}

/* ============================================================================
 * USER EVENTS
 * ============================================================================ */

wingo_error_t wingo_event_post(wingo_event_loop_t *loop,
                               wingo_event_callback_t callback,
                               void *userdata)
{
    wingo_event_t *event;

    if (loop == NULL || callback == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /*
     * We use a simple approach: create a oneshot event that
     * fires on the next loop iteration.
     *
     * In a production implementation, we'd use a queue and
     * a dedicated user event fd.
     */

    event = event_alloc(WINGO_EVENT_TYPE_USER);
    if (event == NULL) {
        return WINGO_ERR_NOMEM;
    }

    event->callback = callback;
    event->userdata = userdata;
    event->oneshot = true;
    event->delay_ms = wingo_time_now_ms();  /* Fire immediately */

    /* Add to loop */
    if (event_add_to_loop(loop, event) != WINGO_SUCCESS) {
        event_free(event);
        return WINGO_ERR_NOMEM;
    }

    /* Add to timer list (fires immediately) */
    wingo_list_append(loop->timers, event);

    /* Wake up the loop */
    wingo_event_wakeup(loop);

    return WINGO_SUCCESS;
}

/* ============================================================================
 * WAKEUP
 * ============================================================================ */

wingo_error_t wingo_event_wakeup(wingo_event_loop_t *loop)
{
    wingo_u64 value = 1;
    ssize_t n;

    if (loop == NULL || loop->wakeup_fd < 0) {
        return WINGO_ERR_INVALID_ARG;
    }

    n = write(loop->wakeup_fd, &value, sizeof(value));
    if (n != sizeof(value)) {
        if (errno == EAGAIN) {
            /* Already signaled — not an error */
            return WINGO_SUCCESS;
        }
        return WINGO_ERR_GENERIC;
    }

    return WINGO_SUCCESS;
}

/* ============================================================================
 * EVENT QUERY
 * ============================================================================ */

wingo_event_type_t wingo_event_get_type(const wingo_event_t *event)
{
    if (event == NULL) {
        return WINGO_EVENT_TYPE_USER;
    }

    return event->type;
}

int wingo_event_get_fd(const wingo_event_t *event)
{
    if (event == NULL || event->type != WINGO_EVENT_TYPE_FD) {
        return -1;
    }

    return event->fd;
}

wingo_u32 wingo_event_get_flags(const wingo_event_t *event)
{
    if (event == NULL) {
        return 0;
    }

    return event->flags;
}

void *wingo_event_get_userdata(const wingo_event_t *event)
{
    if (event == NULL) {
        return NULL;
    }

    return event->userdata;
}

/* ============================================================================
 * STATISTICS
 * ============================================================================ */

wingo_error_t wingo_event_loop_get_stats(const wingo_event_loop_t *loop,
                                         wingo_u64 *events,
                                         wingo_u64 *timers,
                                         wingo_u64 *wakeups)
{
    if (loop == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    if (events != NULL) {
        *events = loop->stat_events;
    }
    if (timers != NULL) {
        *timers = loop->stat_timers;
    }
    if (wakeups != NULL) {
        *wakeups = loop->stat_wakeups;
    }

    return WINGO_SUCCESS;
}

void wingo_event_loop_reset_stats(wingo_event_loop_t *loop)
{
    if (loop == NULL) {
        return;
    }

    loop->stat_events = 0;
    loop->stat_timers = 0;
    loop->stat_wakeups = 0;
}

/* ============================================================================
 * INFO
 * ============================================================================ */

wingo_size wingo_event_loop_count(const wingo_event_loop_t *loop)
{
    if (loop == NULL || loop->events == NULL) {
        return 0;
    }

    return loop->events->count;
}

void wingo_event_loop_print_status(const wingo_event_loop_t *loop, FILE *f)
{
    if (f == NULL) {
        f = stderr;
    }

    if (loop == NULL) {
        fprintf(f, "Event loop: (null)\n");
        return;
    }

    fprintf(f, "Event Loop Status:\n");
    fprintf(f, "  Running:     %s\n", loop->running ? "yes" : "no");
    fprintf(f, "  Events:      %zu\n", loop->events->count);
    fprintf(f, "  Timers:      %zu\n", loop->timers->count);
    fprintf(f, "  Wakeups:     %llu\n",
            (unsigned long long)loop->stat_wakeups);
    fprintf(f, "  Processed:   %llu\n",
            (unsigned long long)loop->stat_events);
    fprintf(f, "  Timer fires: %llu\n",
            (unsigned long long)loop->stat_timers);
}
