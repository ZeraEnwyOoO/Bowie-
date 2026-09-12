
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

#ifndef WINGO_UTIL_QUEUE_H
#define WINGO_UTIL_QUEUE_H

/*
 * ============================================================================
 * WINGO QUEUE
 * ============================================================================
 *
 * This header provides:
 *   - FIFO queue (linked list based)
 *   - Ring buffer queue (fixed size)
 *   - Priority queue (binary heap)
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * FIFO QUEUE (LINKED LIST)
 * ============================================================================ */

/*
 * FIFO queue node.
 */

typedef struct wingo_queue_node {
    void *data;
    struct wingo_queue_node *next;
} wingo_queue_node_t;

/*
 * FIFO queue.
 */

typedef struct {
    wingo_queue_node_t *head;
    wingo_queue_node_t *tail;
    wingo_size count;
    void (*free_fn)(void *data);
} wingo_queue_t;

/*
 * Create a new FIFO queue.
 *
 * @param free_fn   Function to free data (NULL if not needed)
 * @return          New queue, or NULL on error
 */

wingo_queue_t *wingo_queue_new(void (*free_fn)(void *data));

/*
 * Free a FIFO queue.
 *
 * @param queue     Queue to free
 */

void wingo_queue_free(wingo_queue_t *queue);

/*
 * Push data to the back of the queue.
 *
 * @param queue     Queue
 * @param data      Data to push
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_queue_push(wingo_queue_t *queue, void *data);

/*
 * Pop data from the front of the queue.
 *
 * @param queue     Queue
 * @return          Data, or NULL if empty
 */

void *wingo_queue_pop(wingo_queue_t *queue);

/*
 * Peek at the front of the queue without removing.
 *
 * @param queue     Queue
 * @return          Data, or NULL if empty
 */

void *wingo_queue_peek(const wingo_queue_t *queue);

/*
 * Clear the queue.
 *
 * @param queue     Queue
 */

void wingo_queue_clear(wingo_queue_t *queue);

/*
 * Check if queue is empty.
 *
 * @param queue     Queue
 * @return          true if empty, false otherwise
 */

static inline bool wingo_queue_is_empty(const wingo_queue_t *queue)
{
    return queue == NULL || queue->count == 0;
}

/*
 * Get queue count.
 *
 * @param queue     Queue
 * @return          Number of items
 */

static inline wingo_size wingo_queue_count(const wingo_queue_t *queue)
{
    return queue == NULL ? 0 : queue->count;
}

/* ============================================================================
 * RING BUFFER QUEUE (FIXED SIZE)
 * ============================================================================ */

/*
 * Ring buffer queue.
 */

typedef struct {
    void **items;
    wingo_size capacity;
    wingo_size head;
    wingo_size tail;
    wingo_size count;
    void (*free_fn)(void *data);
} wingo_ring_t;

/*
 * Create a new ring buffer queue.
 *
 * @param capacity  Maximum number of items
 * @param free_fn   Function to free data (NULL if not needed)
 * @return          New ring buffer, or NULL on error
 */

wingo_ring_t *wingo_ring_new(wingo_size capacity, void (*free_fn)(void *data));

/*
 * Free a ring buffer queue.
 *
 * @param ring      Ring buffer to free
 */

void wingo_ring_free(wingo_ring_t *ring);

/*
 * Push data to the ring buffer.
 *
 * @param ring      Ring buffer
 * @param data      Data to push
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_ring_push(wingo_ring_t *ring, void *data);

/*
 * Pop data from the ring buffer.
 *
 * @param ring      Ring buffer
 * @return          Data, or NULL if empty
 */

void *wingo_ring_pop(wingo_ring_t *ring);

/*
 * Peek at the front of the ring buffer.
 *
 * @param ring      Ring buffer
 * @return          Data, or NULL if empty
 */

void *wingo_ring_peek(const wingo_ring_t *ring);

/*
 * Check if ring buffer is empty.
 *
 * @param ring      Ring buffer
 * @return          true if empty, false otherwise
 */

static inline bool wingo_ring_is_empty(const wingo_ring_t *ring)
{
    return ring == NULL || ring->count == 0;
}

/*
 * Check if ring buffer is full.
 *
 * @param ring      Ring buffer
 * @return          true if full, false otherwise
 */

static inline bool wingo_ring_is_full(const wingo_ring_t *ring)
{
    return ring == NULL || ring->count >= ring->capacity;
}

/*
 * Get ring buffer count.
 *
 * @param ring      Ring buffer
 * @return          Number of items
 */

static inline wingo_size wingo_ring_count(const wingo_ring_t *ring)
{
    return ring == NULL ? 0 : ring->count;
}

/*
 * Get ring buffer capacity.
 *
 * @param ring      Ring buffer
 * @return          Capacity
 */

static inline wingo_size wingo_ring_capacity(const wingo_ring_t *ring)
{
    return ring == NULL ? 0 : ring->capacity;
}

/*
 * Clear the ring buffer.
 *
 * @param ring      Ring buffer
 */

void wingo_ring_clear(wingo_ring_t *ring);

/* ============================================================================
 * PRIORITY QUEUE (BINARY HEAP)
 * ============================================================================ */

/*
 * Priority queue.
 */

typedef struct {
    void **items;
    wingo_size capacity;
    wingo_size count;
    int (*cmp)(const void *a, const void *b);
    void (*free_fn)(void *data);
} wingo_pqueue_t;

/*
 * Create a new priority queue.
 *
 * @param capacity  Initial capacity
 * @param cmp       Comparison function (returns <0 if a has higher priority)
 * @param free_fn   Function to free data (NULL if not needed)
 * @return          New priority queue, or NULL on error
 */

wingo_pqueue_t *wingo_pqueue_new(wingo_size capacity,
                                 int (*cmp)(const void *a, const void *b),
                                 void (*free_fn)(void *data));

/*
 * Free a priority queue.
 *
 * @param pq        Priority queue to free
 */

void wingo_pqueue_free(wingo_pqueue_t *pq);

/*
 * Push data to the priority queue.
 *
 * @param pq        Priority queue
 * @param data      Data to push
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_pqueue_push(wingo_pqueue_t *pq, void *data);

/*
 * Pop data from the priority queue.
 *
 * @param pq        Priority queue
 * @return          Data, or NULL if empty
 */

void *wingo_pqueue_pop(wingo_pqueue_t *pq);

/*
 * Peek at the top of the priority queue.
 *
 * @param pq        Priority queue
 * @return          Data, or NULL if empty
 */

void *wingo_pqueue_peek(const wingo_pqueue_t *pq);

/*
 * Check if priority queue is empty.
 *
 * @param pq        Priority queue
 * @return          true if empty, false otherwise
 */

static inline bool wingo_pqueue_is_empty(const wingo_pqueue_t *pq)
{
    return pq == NULL || pq->count == 0;
}

/*
 * Get priority queue count.
 *
 * @param pq        Priority queue
 * @return          Number of items
 */

static inline wingo_size wingo_pqueue_count(const wingo_pqueue_t *pq)
{
    return pq == NULL ? 0 : pq->count;
}

/*
 * Clear the priority queue.
 *
 * @param pq        Priority queue
 */

void wingo_pqueue_clear(wingo_pqueue_t *pq);

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_QUEUE_H */
