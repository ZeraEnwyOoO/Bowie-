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

#include "wingo/util/queue.h"

#include <string.h>

/* ============================================================================
 * INTERNAL CONSTANTS
 * ============================================================================ */

/*
 * Default capacity for priority queue.
 */
#define PQUEUE_DEFAULT_CAPACITY     16

/*
 * Growth factor for priority queue.
 */
#define PQUEUE_GROWTH_FACTOR        2

/*
 * Maximum priority queue capacity.
 */
#define PQUEUE_MAX_CAPACITY         (1U << 30)

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Free a queue node.
 * Calls free_fn on data if set, then frees node.
 */
static void queue_node_free(wingo_queue_t *queue, wingo_queue_node_t *node)
{
    if (node == NULL) {
        return;
    }

    if (node->data != NULL && queue != NULL && queue->free_fn != NULL) {
        queue->free_fn(node->data);
    }

    free(node);
}

/* ============================================================================
 * FIFO QUEUE (LINKED LIST)
 * ============================================================================ */

wingo_queue_t *wingo_queue_new(void (*free_fn)(void *data))
{
    wingo_queue_t *queue;

    queue = calloc(1, sizeof(wingo_queue_t));
    if (queue == NULL) {
        return NULL;
    }

    queue->head    = NULL;
    queue->tail    = NULL;
    queue->count   = 0;
    queue->free_fn = free_fn;

    return queue;
}

void wingo_queue_free(wingo_queue_t *queue)
{
    wingo_queue_node_t *node, *next;

    if (queue == NULL) {
        return;
    }

    node = queue->head;
    while (node != NULL) {
        next = node->next;
        queue_node_free(queue, node);
        node = next;
    }

    free(queue);
}

wingo_error_t wingo_queue_push(wingo_queue_t *queue, void *data)
{
    wingo_queue_node_t *node;

    if (queue == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Allocate new node */
    node = calloc(1, sizeof(wingo_queue_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->next = NULL;

    /* Link at tail */
    if (queue->tail == NULL) {
        /* Empty queue */
        queue->head = node;
        queue->tail = node;
    } else {
        queue->tail->next = node;
        queue->tail = node;
    }

    queue->count++;

    return WINGO_SUCCESS;
}

void *wingo_queue_pop(wingo_queue_t *queue)
{
    wingo_queue_node_t *node;
    void *data;

    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }

    /* Remove from head */
    node = queue->head;
    queue->head = node->next;

    /* If queue is now empty, reset tail */
    if (queue->head == NULL) {
        queue->tail = NULL;
    }

    data = node->data;
    free(node);  /* Don't call free_fn — we return data */
    queue->count--;

    return data;
}

void *wingo_queue_peek(const wingo_queue_t *queue)
{
    if (queue == NULL || queue->head == NULL) {
        return NULL;
    }

    return queue->head->data;
}

void wingo_queue_clear(wingo_queue_t *queue)
{
    wingo_queue_node_t *node, *next;

    if (queue == NULL) {
        return;
    }

    node = queue->head;
    while (node != NULL) {
        next = node->next;
        queue_node_free(queue, node);
        node = next;
    }

    queue->head  = NULL;
    queue->tail  = NULL;
    queue->count = 0;
}

/* ============================================================================
 * RING BUFFER QUEUE (FIXED SIZE)
 * ============================================================================ */

wingo_ring_t *wingo_ring_new(wingo_size capacity, void (*free_fn)(void *data))
{
    wingo_ring_t *ring;

    if (capacity == 0) {
        return NULL;
    }

    /*
     * Round up to power of two.
     * This allows us to use bitwise AND instead of modulo,
     * which is much faster.
     */
    if (!WINGO_IS_POWER_OF_TWO(capacity)) {
        wingo_size pow2 = 1;
        while (pow2 < capacity) {
            pow2 <<= 1;
        }
        capacity = pow2;
    }

    ring = calloc(1, sizeof(wingo_ring_t));
    if (ring == NULL) {
        return NULL;
    }

    ring->items = calloc(capacity, sizeof(void *));
    if (ring->items == NULL) {
        free(ring);
        return NULL;
    }

    ring->capacity = capacity;
    ring->head     = 0;
    ring->tail     = 0;
    ring->count    = 0;
    ring->free_fn  = free_fn;

    return ring;
}

void wingo_ring_free(wingo_ring_t *ring)
{
    wingo_size i;

    if (ring == NULL) {
        return;
    }

    /* Free all items in ring */
    if (ring->items != NULL) {
        for (i = 0; i < ring->capacity; i++) {
            if (ring->items[i] != NULL && ring->free_fn != NULL) {
                ring->free_fn(ring->items[i]);
            }
        }
        free(ring->items);
    }

    free(ring);
}

wingo_error_t wingo_ring_push(wingo_ring_t *ring, void *data)
{
    if (ring == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Check if full */
    if (ring->count >= ring->capacity) {
        return WINGO_ERR_OVERFLOW;
    }

    /* Place at tail */
    ring->items[ring->tail] = data;

    /* Advance tail (wraps around) */
    ring->tail = (ring->tail + 1) & (ring->capacity - 1);

    ring->count++;

    return WINGO_SUCCESS;
}

void *wingo_ring_pop(wingo_ring_t *ring)
{
    void *data;

    if (ring == NULL || ring->count == 0) {
        return NULL;
    }

    /* Get from head */
    data = ring->items[ring->head];
    ring->items[ring->head] = NULL;

    /* Advance head (wraps around) */
    ring->head = (ring->head + 1) & (ring->capacity - 1);

    ring->count--;

    return data;
}

void *wingo_ring_peek(const wingo_ring_t *ring)
{
    if (ring == NULL || ring->count == 0) {
        return NULL;
    }

    return ring->items[ring->head];
}

void wingo_ring_clear(wingo_ring_t *ring)
{
    wingo_size i;

    if (ring == NULL) {
        return;
    }

    for (i = 0; i < ring->capacity; i++) {
        if (ring->items[i] != NULL) {
            if (ring->free_fn != NULL) {
                ring->free_fn(ring->items[i]);
            }
            ring->items[i] = NULL;
        }
    }

    ring->head  = 0;
    ring->tail  = 0;
    ring->count = 0;
}

/* ============================================================================
 * PRIORITY QUEUE (BINARY HEAP)
 * ============================================================================ */

/*
 * Binary heap implementation.
 *
 * Heap property: parent has higher priority than children.
 * For min-heap: parent <= children.
 *
 * Array representation:
 *   - Root at index 0
 *   - For node at index i:
 *       - Left child at 2i + 1
 *       - Right child at 2i + 2
 *       - Parent at (i - 1) / 2
 */

/*
 * Get parent index.
 */
static wingo_size pqueue_parent(wingo_size i)
{
    return (i - 1) / 2;
}

/*
 * Get left child index.
 */
static wingo_size pqueue_left(wingo_size i)
{
    return 2 * i + 1;
}

/*
 * Get right child index.
 */
static wingo_size pqueue_right(wingo_size i)
{
    return 2 * i + 2;
}

/*
 * Swap two items in heap.
 */
static void pqueue_swap(wingo_pqueue_t *pq, wingo_size i, wingo_size j)
{
    void *tmp = pq->items[i];
    pq->items[i] = pq->items[j];
    pq->items[j] = tmp;
}

/*
 * Sift up (bubble up) from index i.
 *
 * Used after inserting at end of heap.
 */
static void pqueue_sift_up(wingo_pqueue_t *pq, wingo_size i)
{
    while (i > 0) {
        wingo_size parent = pqueue_parent(i);

        /* If parent has higher or equal priority, stop */
        if (pq->cmp(pq->items[parent], pq->items[i]) <= 0) {
            break;
        }

        /* Swap with parent */
        pqueue_swap(pq, i, parent);
        i = parent;
    }
}

/*
 * Sift down (bubble down) from index i.
 *
 * Used after removing root.
 */
static void pqueue_sift_down(wingo_pqueue_t *pq, wingo_size i)
{
    wingo_size count = pq->count;

    while (1) {
        wingo_size left  = pqueue_left(i);
        wingo_size right = pqueue_right(i);
        wingo_size smallest = i;

        /* Find smallest among i, left, right */
        if (left < count && pq->cmp(pq->items[left], pq->items[smallest]) < 0) {
            smallest = left;
        }

        if (right < count && pq->cmp(pq->items[right], pq->items[smallest]) < 0) {
            smallest = right;
        }

        /* If i is smallest, heap property satisfied */
        if (smallest == i) {
            break;
        }

        /* Swap with smallest child */
        pqueue_swap(pq, i, smallest);
        i = smallest;
    }
}

/*
 * Grow priority queue.
 */
static wingo_error_t pqueue_grow(wingo_pqueue_t *pq)
{
    void **new_items;
    wingo_size new_capacity;

    new_capacity = pq->capacity * PQUEUE_GROWTH_FACTOR;
    if (new_capacity < pq->capacity) {
        return WINGO_ERR_OVERFLOW;
    }
    if (new_capacity > PQUEUE_MAX_CAPACITY) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    new_items = realloc(pq->items, new_capacity * sizeof(void *));
    if (new_items == NULL) {
        return WINGO_ERR_NOMEM;
    }

    pq->items    = new_items;
    pq->capacity = new_capacity;

    return WINGO_SUCCESS;
}

wingo_pqueue_t *wingo_pqueue_new(wingo_size capacity,
                                 int (*cmp)(const void *a, const void *b),
                                 void (*free_fn)(void *data))
{
    wingo_pqueue_t *pq;

    if (cmp == NULL) {
        return NULL;
    }

    if (capacity == 0) {
        capacity = PQUEUE_DEFAULT_CAPACITY;
    }

    if (capacity > PQUEUE_MAX_CAPACITY) {
        return NULL;
    }

    pq = calloc(1, sizeof(wingo_pqueue_t));
    if (pq == NULL) {
        return NULL;
    }

    pq->items = calloc(capacity, sizeof(void *));
    if (pq->items == NULL) {
        free(pq);
        return NULL;
    }

    pq->capacity = capacity;
    pq->count    = 0;
    pq->cmp      = cmp;
    pq->free_fn  = free_fn;

    return pq;
}

void wingo_pqueue_free(wingo_pqueue_t *pq)
{
    wingo_size i;

    if (pq == NULL) {
        return;
    }

    if (pq->items != NULL) {
        if (pq->free_fn != NULL) {
            for (i = 0; i < pq->count; i++) {
                if (pq->items[i] != NULL) {
                    pq->free_fn(pq->items[i]);
                }
            }
        }
        free(pq->items);
    }

    free(pq);
}

wingo_error_t wingo_pqueue_push(wingo_pqueue_t *pq, void *data)
{
    wingo_error_t rc;

    if (pq == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Grow if needed */
    if (pq->count >= pq->capacity) {
        rc = pqueue_grow(pq);
        if (rc != WINGO_SUCCESS) {
            return rc;
        }
    }

    /* Insert at end */
    pq->items[pq->count] = data;
    pq->count++;

    /* Restore heap property */
    pqueue_sift_up(pq, pq->count - 1);

    return WINGO_SUCCESS;
}

void *wingo_pqueue_pop(wingo_pqueue_t *pq)
{
    void *data;

    if (pq == NULL || pq->count == 0) {
        return NULL;
    }

    /* Save root data */
    data = pq->items[0];

    /* Move last item to root */
    pq->count--;
    if (pq->count > 0) {
        pq->items[0] = pq->items[pq->count];
        pq->items[pq->count] = NULL;

        /* Restore heap property */
        pqueue_sift_down(pq, 0);
    } else {
        pq->items[0] = NULL;
    }

    return data;
}

void *wingo_pqueue_peek(const wingo_pqueue_t *pq)
{
    if (pq == NULL || pq->count == 0) {
        return NULL;
    }

    return pq->items[0];
}

void wingo_pqueue_clear(wingo_pqueue_t *pq)
{
    wingo_size i;

    if (pq == NULL) {
        return;
    }

    if (pq->items != NULL && pq->free_fn != NULL) {
        for (i = 0; i < pq->count; i++) {
            if (pq->items[i] != NULL) {
                pq->free_fn(pq->items[i]);
                pq->items[i] = NULL;
            }
        }
    }

    pq->count = 0;
}
