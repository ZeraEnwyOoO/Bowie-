
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

#include "wingo/util/list.h"

#include <string.h>

/* ============================================================================
 * INTERNAL HELPERS
 * ============================================================================ */

/*
 * Free a single singly-linked list node.
 * If list has free_fn, call it on data. Otherwise just free the node.
 */
static void slist_node_free(wingo_slist_t *list, wingo_slist_node_t *node)
{
    if (node == NULL) {
        return;
    }

    if (node->data != NULL && list != NULL && list->free_fn != NULL) {
        list->free_fn(node->data);
    }

    free(node);
}

/*
 * Free a single doubly-linked list node.
 */
static void list_node_free(wingo_list_t *list, wingo_list_node_t *node)
{
    if (node == NULL) {
        return;
    }

    if (node->data != NULL && list != NULL && list->free_fn != NULL) {
        list->free_fn(node->data);
    }

    free(node);
}

/*
 * Get singly-linked node at position.
 * Returns NULL if out of range.
 */
static wingo_slist_node_t *slist_node_at(const wingo_slist_t *list, wingo_size pos)
{
    wingo_slist_node_t *node;
    wingo_size i;

    if (list == NULL || pos >= list->count) {
        return NULL;
    }

    /*
     * Optimization: if pos is in the second half, we could
     * start from tail — but singly-linked list has no prev pointer,
     * so we must start from head.
     */
    node = list->head;
    for (i = 0; i < pos && node != NULL; i++) {
        node = node->next;
    }

    return node;
}

/*
 * Get doubly-linked node at position.
 * Optimized: start from head or tail whichever is closer.
 */
static wingo_list_node_t *list_node_at(const wingo_list_t *list, wingo_size pos)
{
    wingo_list_node_t *node;
    wingo_size i;

    if (list == NULL || pos >= list->count) {
        return NULL;
    }

    /*
     * Optimization: walk from whichever end is closer.
     * This makes list_get O(min(pos, count-pos)) instead of O(pos).
     */
    if (pos <= list->count / 2) {
        /* Walk from head */
        node = list->head;
        for (i = 0; i < pos && node != NULL; i++) {
            node = node->next;
        }
    } else {
        /* Walk from tail */
        node = list->tail;
        for (i = list->count - 1; i > pos && node != NULL; i--) {
            node = node->prev;
        }
    }

    return node;
}

/* ============================================================================
 * SINGLY-LINKED LIST
 * ============================================================================ */

wingo_slist_t *wingo_slist_new(void (*free_fn)(void *data))
{
    wingo_slist_t *list;

    list = calloc(1, sizeof(wingo_slist_t));
    if (list == NULL) {
        return NULL;
    }

    list->head    = NULL;
    list->tail    = NULL;
    list->count   = 0;
    list->free_fn = free_fn;

    return list;
}

void wingo_slist_free(wingo_slist_t *list)
{
    wingo_slist_node_t *node, *next;

    if (list == NULL) {
        return;
    }

    node = list->head;
    while (node != NULL) {
        next = node->next;
        slist_node_free(list, node);
        node = next;
    }

    free(list);
}

wingo_error_t wingo_slist_append(wingo_slist_t *list, void *data)
{
    wingo_slist_node_t *node;

    if (list == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Allocate new node */
    node = calloc(1, sizeof(wingo_slist_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->next = NULL;

    /* Append to tail */
    if (list->tail == NULL) {
        /* Empty list */
        list->head = node;
        list->tail = node;
    } else {
        list->tail->next = node;
        list->tail = node;
    }

    list->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_slist_prepend(wingo_slist_t *list, void *data)
{
    wingo_slist_node_t *node;

    if (list == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    node = calloc(1, sizeof(wingo_slist_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->next = list->head;

    list->head = node;

    /* If list was empty, tail = head */
    if (list->tail == NULL) {
        list->tail = node;
    }

    list->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_slist_insert(wingo_slist_t *list, wingo_size pos, void *data)
{
    wingo_slist_node_t *prev, *node;

    if (list == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* Position 0 = prepend */
    if (pos == 0) {
        return wingo_slist_prepend(list, data);
    }

    /* Position count = append */
    if (pos >= list->count) {
        return wingo_slist_append(list, data);
    }

    /* Find node at pos-1 */
    prev = slist_node_at(list, pos - 1);
    if (prev == NULL) {
        return WINGO_ERR_OUT_OF_RANGE;
    }

    node = calloc(1, sizeof(wingo_slist_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->next = prev->next;
    prev->next = node;

    list->count++;

    return WINGO_SUCCESS;
}

void *wingo_slist_remove(wingo_slist_t *list, wingo_size pos)
{
    wingo_slist_node_t *prev, *node;
    void *data;

    if (list == NULL || pos >= list->count) {
        return NULL;
    }

    /* Remove head */
    if (pos == 0) {
        node = list->head;
        list->head = node->next;

        if (list->head == NULL) {
            list->tail = NULL;
        }

        data = node->data;
        free(node);  /* Don't call free_fn — we return data */
        list->count--;

        return data;
    }

    /* Find prev */
    prev = slist_node_at(list, pos - 1);
    if (prev == NULL || prev->next == NULL) {
        return NULL;
    }

    node = prev->next;
    prev->next = node->next;

    /* If removing tail, update tail */
    if (node == list->tail) {
        list->tail = prev;
    }

    data = node->data;
    free(node);
    list->count--;

    return data;
}

void *wingo_slist_remove_data(wingo_slist_t *list, const void *data,
                              int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t *node, *prev;
    void *result;

    if (list == NULL || cmp == NULL) {
        return NULL;
    }

    prev = NULL;
    node = list->head;

    while (node != NULL) {
        if (cmp(node->data, data) == 0) {
            /* Unlink */
            if (prev == NULL) {
                list->head = node->next;
            } else {
                prev->next = node->next;
            }

            if (node == list->tail) {
                list->tail = prev;
            }

            result = node->data;
            free(node);
            list->count--;

            return result;
        }

        prev = node;
        node = node->next;
    }

    return NULL;
}

wingo_size wingo_slist_remove_all(wingo_slist_t *list, const void *data,
                                  int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t *node, *prev, *next;
    wingo_size removed = 0;

    if (list == NULL || cmp == NULL) {
        return 0;
    }

    prev = NULL;
    node = list->head;

    while (node != NULL) {
        next = node->next;

        if (cmp(node->data, data) == 0) {
            /* Unlink */
            if (prev == NULL) {
                list->head = next;
            } else {
                prev->next = next;
            }

            if (node == list->tail) {
                list->tail = prev;
            }

            /* Free data if free_fn is set */
            if (list->free_fn != NULL && node->data != NULL) {
                list->free_fn(node->data);
            }

            free(node);
            list->count--;
            removed++;

            /* Don't update prev — we removed node */
        } else {
            prev = node;
        }

        node = next;
    }

    return removed;
}

void *wingo_slist_find(const wingo_slist_t *list, const void *data,
                       int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t *node;

    if (list == NULL || cmp == NULL) {
        return NULL;
    }

    node = list->head;
    while (node != NULL) {
        if (cmp(node->data, data) == 0) {
            return node->data;
        }
        node = node->next;
    }

    return NULL;
}

void *wingo_slist_get(const wingo_slist_t *list, wingo_size pos)
{
    wingo_slist_node_t *node;

    node = slist_node_at(list, pos);
    return node != NULL ? node->data : NULL;
}

void *wingo_slist_first(const wingo_slist_t *list)
{
    if (list == NULL || list->head == NULL) {
        return NULL;
    }
    return list->head->data;
}

void *wingo_slist_last(const wingo_slist_t *list)
{
    if (list == NULL || list->tail == NULL) {
        return NULL;
    }
    return list->tail->data;
}

void wingo_slist_clear(wingo_slist_t *list)
{
    wingo_slist_node_t *node, *next;

    if (list == NULL) {
        return;
    }

    node = list->head;
    while (node != NULL) {
        next = node->next;
        slist_node_free(list, node);
        node = next;
    }

    list->head  = NULL;
    list->tail  = NULL;
    list->count = 0;
}

/*
 * Merge sort for singly-linked list.
 *
 * Merge sort is the standard choice for linked lists because:
 *   - O(n log n) guaranteed
 *   - Stable sort
 *   - Works with sequential access (no random access needed)
 *   - No extra memory beyond recursion stack
 */

/*
 * Split list into two halves.
 * Returns pointer to second half.
 */
static wingo_slist_node_t *slist_split(wingo_slist_node_t *head)
{
    wingo_slist_node_t *slow, *fast, *prev;

    if (head == NULL || head->next == NULL) {
        return NULL;
    }

    /* Use slow/fast pointer technique */
    slow = head;
    fast = head;
    prev = NULL;

    while (fast != NULL && fast->next != NULL) {
        prev = slow;
        slow = slow->next;
        fast = fast->next->next;
    }

    /* Split */
    if (prev != NULL) {
        prev->next = NULL;
    }

    return slow;
}

/*
 * Merge two sorted lists.
 */
static wingo_slist_node_t *slist_merge(wingo_slist_node_t *a,
                                       wingo_slist_node_t *b,
                                       int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t dummy;
    wingo_slist_node_t *tail = &dummy;

    dummy.next = NULL;

    while (a != NULL && b != NULL) {
        if (cmp(a->data, b->data) <= 0) {
            tail->next = a;
            a = a->next;
        } else {
            tail->next = b;
            b = b->next;
        }
        tail = tail->next;
    }

    /* Attach remaining */
    tail->next = (a != NULL) ? a : b;

    return dummy.next;
}

/*
 * Recursive merge sort.
 */
static wingo_slist_node_t *slist_merge_sort(wingo_slist_node_t *head,
                                            int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t *second;

    if (head == NULL || head->next == NULL) {
        return head;
    }

    second = slist_split(head);

    head   = slist_merge_sort(head, cmp);
    second = slist_merge_sort(second, cmp);

    return slist_merge(head, second, cmp);
}

void wingo_slist_sort(wingo_slist_t *list, int (*cmp)(const void *a, const void *b))
{
    wingo_slist_node_t *node;

    if (list == NULL || cmp == NULL || list->count < 2) {
        return;
    }

    list->head = slist_merge_sort(list->head, cmp);

    /*
     * After sorting, we need to find the new tail.
     * We could have tracked it during merge, but walking is simpler.
     */
    node = list->head;
    while (node != NULL && node->next != NULL) {
        node = node->next;
    }
    list->tail = node;
}

/* ============================================================================
 * DOUBLY-LINKED LIST
 * ============================================================================ */

wingo_list_t *wingo_list_new(void (*free_fn)(void *data))
{
    wingo_list_t *list;

    list = calloc(1, sizeof(wingo_list_t));
    if (list == NULL) {
        return NULL;
    }

    list->head    = NULL;
    list->tail    = NULL;
    list->count   = 0;
    list->free_fn = free_fn;

    return list;
}

void wingo_list_free(wingo_list_t *list)
{
    wingo_list_node_t *node, *next;

    if (list == NULL) {
        return;
    }

    node = list->head;
    while (node != NULL) {
        next = node->next;
        list_node_free(list, node);
        node = next;
    }

    free(list);
}

wingo_error_t wingo_list_append(wingo_list_t *list, void *data)
{
    wingo_list_node_t *node;

    if (list == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    node = calloc(1, sizeof(wingo_list_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->prev = list->tail;
    node->next = NULL;

    if (list->tail == NULL) {
        /* Empty list */
        list->head = node;
    } else {
        list->tail->next = node;
    }

    list->tail = node;
    list->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_list_prepend(wingo_list_t *list, void *data)
{
    wingo_list_node_t *node;

    if (list == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    node = calloc(1, sizeof(wingo_list_node_t));
    if (node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    node->data = data;
    node->prev = NULL;
    node->next = list->head;

    if (list->head == NULL) {
        list->tail = node;
    } else {
        list->head->prev = node;
    }

    list->head = node;
    list->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_list_insert_before(wingo_list_t *list,
                                       wingo_list_node_t *node, void *data)
{
    wingo_list_node_t *new_node;

    if (list == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* If inserting before head, it's a prepend */
    if (node == list->head) {
        return wingo_list_prepend(list, data);
    }

    new_node = calloc(1, sizeof(wingo_list_node_t));
    if (new_node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    new_node->data = data;
    new_node->next = node;
    new_node->prev = node->prev;

    if (node->prev != NULL) {
        node->prev->next = new_node;
    }
    node->prev = new_node;

    list->count++;

    return WINGO_SUCCESS;
}

wingo_error_t wingo_list_insert_after(wingo_list_t *list,
                                      wingo_list_node_t *node, void *data)
{
    wingo_list_node_t *new_node;

    if (list == NULL || node == NULL) {
        return WINGO_ERR_INVALID_ARG;
    }

    /* If inserting after tail, it's an append */
    if (node == list->tail) {
        return wingo_list_append(list, data);
    }

    new_node = calloc(1, sizeof(wingo_list_node_t));
    if (new_node == NULL) {
        return WINGO_ERR_NOMEM;
    }

    new_node->data = data;
    new_node->prev = node;
    new_node->next = node->next;

    if (node->next != NULL) {
        node->next->prev = new_node;
    }
    node->next = new_node;

    list->count++;

    return WINGO_SUCCESS;
}

void *wingo_list_remove_node(wingo_list_t *list, wingo_list_node_t *node)
{
    void *data;

    if (list == NULL || node == NULL) {
        return NULL;
    }

    /* Unlink from prev */
    if (node->prev != NULL) {
        node->prev->next = node->next;
    } else {
        list->head = node->next;
    }

    /* Unlink from next */
    if (node->next != NULL) {
        node->next->prev = node->prev;
    } else {
        list->tail = node->prev;
    }

    data = node->data;
    free(node);
    list->count--;

    return data;
}

void *wingo_list_remove(wingo_list_t *list, wingo_size pos)
{
    wingo_list_node_t *node;

    if (list == NULL) {
        return NULL;
    }

    node = list_node_at(list, pos);
    if (node == NULL) {
        return NULL;
    }

    return wingo_list_remove_node(list, node);
}

wingo_list_node_t *wingo_list_find_node(const wingo_list_t *list,
                                        const void *data,
                                        int (*cmp)(const void *a, const void *b))
{
    wingo_list_node_t *node;

    if (list == NULL || cmp == NULL) {
        return NULL;
    }

    node = list->head;
    while (node != NULL) {
        if (cmp(node->data, data) == 0) {
            return node;
        }
        node = node->next;
    }

    return NULL;
}

void *wingo_list_find(const wingo_list_t *list, const void *data,
                      int (*cmp)(const void *a, const void *b))
{
    wingo_list_node_t *node;

    node = wingo_list_find_node(list, data, cmp);
    return node != NULL ? node->data : NULL;
}

wingo_list_node_t *wingo_list_get_node(const wingo_list_t *list, wingo_size pos)
{
    return list_node_at(list, pos);
}

void *wingo_list_get(const wingo_list_t *list, wingo_size pos)
{
    wingo_list_node_t *node;

    node = list_node_at(list, pos);
    return node != NULL ? node->data : NULL;
}

void *wingo_list_first(const wingo_list_t *list)
{
    if (list == NULL || list->head == NULL) {
        return NULL;
    }
    return list->head->data;
}

void *wingo_list_last(const wingo_list_t *list)
{
    if (list == NULL || list->tail == NULL) {
        return NULL;
    }
    return list->tail->data;
}

void wingo_list_clear(wingo_list_t *list)
{
    wingo_list_node_t *node, *next;

    if (list == NULL) {
        return;
    }

    node = list->head;
    while (node != NULL) {
        next = node->next;
        list_node_free(list, node);
        node = next;
    }

    list->head  = NULL;
    list->tail  = NULL;
    list->count = 0;
}

/*
 * Merge sort for doubly-linked list.
 *
 * For doubly-linked list, we can use simpler approach:
 * convert to array-like via head pointers, or use same
 * merge sort as singly-linked (ignoring prev pointers,
 * then fix them up).
 *
 * We use the same algorithm as singly-linked and fix prev
 * pointers after sorting.
 */

static wingo_list_node_t *list_split(wingo_list_node_t *head)
{
    wingo_list_node_t *slow, *fast, *prev;

    if (head == NULL || head->next == NULL) {
        return NULL;
    }

    slow = head;
    fast = head;
    prev = NULL;

    while (fast != NULL && fast->next != NULL) {
        prev = slow;
        slow = slow->next;
        fast = fast->next->next;
    }

    if (prev != NULL) {
        prev->next = NULL;
    }

    /* Clear prev of new head */
    if (slow != NULL) {
        slow->prev = NULL;
    }

    return slow;
}

static wingo_list_node_t *list_merge(wingo_list_node_t *a,
                                     wingo_list_node_t *b,
                                     int (*cmp)(const void *a, const void *b))
{
    wingo_list_node_t dummy;
    wingo_list_node_t *tail = &dummy;

    dummy.next = NULL;
    dummy.prev = NULL;

    while (a != NULL && b != NULL) {
        if (cmp(a->data, b->data) <= 0) {
            tail->next = a;
            a->prev = tail;
            a = a->next;
        } else {
            tail->next = b;
            b->prev = tail;
            b = b->next;
        }
        tail = tail->next;
    }

    /* Attach remaining */
    if (a != NULL) {
        tail->next = a;
        a->prev = tail;
    } else if (b != NULL) {
        tail->next = b;
        b->prev = tail;
    }

    return dummy.next;
}

static wingo_list_node_t *list_merge_sort(wingo_list_node_t *head,
                                          int (*cmp)(const void *a, const void *b))
{
    wingo_list_node_t *second;

    if (head == NULL || head->next == NULL) {
        return head;
    }

    second = list_split(head);

    head   = list_merge_sort(head, cmp);
    second = list_merge_sort(second, cmp);

    return list_merge(head, second, cmp);
}

void wingo_list_sort(wingo_list_t *list, int (*cmp)(const void *a, const void *b))
{
    wingo_list_node_t *node;

    if (list == NULL || cmp == NULL || list->count < 2) {
        return;
    }

    list->head = list_merge_sort(list->head, cmp);
    list->head->prev = NULL;

    /* Find new tail */
    node = list->head;
    while (node != NULL && node->next != NULL) {
        node = node->next;
    }
    list->tail = node;
}

void wingo_list_reverse(wingo_list_t *list)
{
    wingo_list_node_t *node, *tmp;

    if (list == NULL || list->count < 2) {
        return;
    }

    /* Swap head and tail */
    tmp = list->head;
    list->head = list->tail;
    list->tail = tmp;

    /* Reverse all next/prev pointers */
    node = list->head;
    while (node != NULL) {
        tmp = node->next;
        node->next = node->prev;
        node->prev = tmp;
        node = node->next;  /* Move to next (which was prev) */
    }
}
