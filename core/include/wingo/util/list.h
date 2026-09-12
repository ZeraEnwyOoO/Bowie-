
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

#ifndef WINGO_UTIL_LIST_H
#define WINGO_UTIL_LIST_H

/*
 * ============================================================================
 * WINGO LINKED LIST
 * ============================================================================
 *
 * This header provides:
 *   - Singly-linked list
 *   - Doubly-linked list
 *   - List operations (insert, remove, find, etc.)
 *   - List iteration macros
 *
 * ============================================================================
 */

#include "wingo/common.h"
#include "wingo/error.h"

/* ============================================================================
 * SINGLY-LINKED LIST
 * ============================================================================ */

/*
 * Singly-linked list node.
 */

typedef struct wingo_slist_node {
    void *data;
    struct wingo_slist_node *next;
} wingo_slist_node_t;

/*
 * Singly-linked list.
 */

typedef struct {
    wingo_slist_node_t *head;
    wingo_slist_node_t *tail;
    wingo_size count;
    void (*free_fn)(void *data);
} wingo_slist_t;

/*
 * Create a new singly-linked list.
 *
 * @param free_fn   Function to free data (NULL if not needed)
 * @return          New list, or NULL on error
 */

wingo_slist_t *wingo_slist_new(void (*free_fn)(void *data));

/*
 * Free a singly-linked list.
 *
 * @param list      List to free
 */

void wingo_slist_free(wingo_slist_t *list);

/*
 * Append data to the end of the list.
 *
 * @param list      List
 * @param data      Data to append
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_slist_append(wingo_slist_t *list, void *data);

/*
 * Prepend data to the beginning of the list.
 *
 * @param list      List
 * @param data      Data to prepend
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_slist_prepend(wingo_slist_t *list, void *data);

/*
 * Insert data at a specific position.
 *
 * @param list      List
 * @param pos       Position (0 to count)
 * @param data      Data to insert
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_slist_insert(wingo_slist_t *list, wingo_size pos, void *data);

/*
 * Remove data at a specific position.
 *
 * @param list      List
 * @param pos       Position (0 to count-1)
 * @return          Removed data, or NULL if not found
 */

void *wingo_slist_remove(wingo_slist_t *list, wingo_size pos);

/*
 * Remove the first occurrence of data.
 *
 * @param list      List
 * @param data      Data to remove
 * @param cmp       Comparison function
 * @return          Removed data, or NULL if not found
 */

void *wingo_slist_remove_data(wingo_slist_t *list, const void *data,
                              int (*cmp)(const void *a, const void *b));

/*
 * Remove all occurrences of data.
 *
 * @param list      List
 * @param data      Data to remove
 * @param cmp       Comparison function
 * @return          Number of items removed
 */

wingo_size wingo_slist_remove_all(wingo_slist_t *list, const void *data,
                                  int (*cmp)(const void *a, const void *b));

/*
 * Find data in the list.
 *
 * @param list      List
 * @param data      Data to find
 * @param cmp       Comparison function
 * @return          Found data, or NULL if not found
 */

void *wingo_slist_find(const wingo_slist_t *list, const void *data,
                       int (*cmp)(const void *a, const void *b));

/*
 * Get data at a specific position.
 *
 * @param list      List
 * @param pos       Position
 * @return          Data, or NULL if out of range
 */

void *wingo_slist_get(const wingo_slist_t *list, wingo_size pos);

/*
 * Get the first data.
 *
 * @param list      List
 * @return          First data, or NULL if empty
 */

void *wingo_slist_first(const wingo_slist_t *list);

/*
 * Get the last data.
 *
 * @param list      List
 * @return          Last data, or NULL if empty
 */

void *wingo_slist_last(const wingo_slist_t *list);

/*
 * Clear the list (remove all items).
 *
 * @param list      List
 */

void wingo_slist_clear(wingo_slist_t *list);

/*
 * Check if list is empty.
 *
 * @param list      List
 * @return          true if empty, false otherwise
 */

static inline bool wingo_slist_is_empty(const wingo_slist_t *list)
{
    return list == NULL || list->count == 0;
}

/*
 * Get list count.
 *
 * @param list      List
 * @return          Number of items
 */

static inline wingo_size wingo_slist_count(const wingo_slist_t *list)
{
    return list == NULL ? 0 : list->count;
}

/*
 * Sort the list.
 *
 * @param list      List
 * @param cmp       Comparison function
 */

void wingo_slist_sort(wingo_slist_t *list, int (*cmp)(const void *a, const void *b));

/* ============================================================================
 * DOUBLY-LINKED LIST
 * ============================================================================ */

/*
 * Doubly-linked list node.
 */

typedef struct wingo_list_node {
    void *data;
    struct wingo_list_node *prev;
    struct wingo_list_node *next;
} wingo_list_node_t;

/*
 * Doubly-linked list.
 */

typedef struct {
    wingo_list_node_t *head;
    wingo_list_node_t *tail;
    wingo_size count;
    void (*free_fn)(void *data);
} wingo_list_t;

/*
 * Create a new doubly-linked list.
 *
 * @param free_fn   Function to free data (NULL if not needed)
 * @return          New list, or NULL on error
 */

wingo_list_t *wingo_list_new(void (*free_fn)(void *data));

/*
 * Free a doubly-linked list.
 *
 * @param list      List to free
 */

void wingo_list_free(wingo_list_t *list);

/*
 * Append data to the end of the list.
 *
 * @param list      List
 * @param data      Data to append
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_list_append(wingo_list_t *list, void *data);

/*
 * Prepend data to the beginning of the list.
 *
 * @param list      List
 * @param data      Data to prepend
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_list_prepend(wingo_list_t *list, void *data);

/*
 * Insert data before a node.
 *
 * @param list      List
 * @param node      Node to insert before
 * @param data      Data to insert
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_list_insert_before(wingo_list_t *list,
                                       wingo_list_node_t *node, void *data);

/*
 * Insert data after a node.
 *
 * @param list      List
 * @param node      Node to insert after
 * @param data      Data to insert
 * @return          WINGO_SUCCESS on success, error code on failure
 */

wingo_error_t wingo_list_insert_after(wingo_list_t *list,
                                      wingo_list_node_t *node, void *data);

/*
 * Remove a node from the list.
 *
 * @param list      List
 * @param node      Node to remove
 * @return          Removed data
 */

void *wingo_list_remove_node(wingo_list_t *list, wingo_list_node_t *node);

/*
 * Remove data at a specific position.
 *
 * @param list      List
 * @param pos       Position (0 to count-1)
 * @return          Removed data, or NULL if not found
 */

void *wingo_list_remove(wingo_list_t *list, wingo_size pos);

/*
 * Find data in the list.
 *
 * @param list      List
 * @param data      Data to find
 * @param cmp       Comparison function
 * @return          Found node, or NULL if not found
 */

wingo_list_node_t *wingo_list_find_node(const wingo_list_t *list,
                                        const void *data,
                                        int (*cmp)(const void *a, const void *b));

/*
 * Find data in the list.
 *
 * @param list      List
 * @param data      Data to find
 * @param cmp       Comparison function
 * @return          Found data, or NULL if not found
 */

void *wingo_list_find(const wingo_list_t *list, const void *data,
                      int (*cmp)(const void *a, const void *b));

/*
 * Get node at a specific position.
 *
 * @param list      List
 * @param pos       Position
 * @return          Node, or NULL if out of range
 */

wingo_list_node_t *wingo_list_get_node(const wingo_list_t *list, wingo_size pos);

/*
 * Get data at a specific position.
 *
 * @param list      List
 * @param pos       Position
 * @return          Data, or NULL if out of range
 */

void *wingo_list_get(const wingo_list_t *list, wingo_size pos);

/*
 * Get the first data.
 *
 * @param list      List
 * @return          First data, or NULL if empty
 */

void *wingo_list_first(const wingo_list_t *list);

/*
 * Get the last data.
 *
 * @param list      List
 * @return          Last data, or NULL if empty
 */

void *wingo_list_last(const wingo_list_t *list);

/*
 * Clear the list (remove all items).
 *
 * @param list      List
 */

void wingo_list_clear(wingo_list_t *list);

/*
 * Check if list is empty.
 *
 * @param list      List
 * @return          true if empty, false otherwise
 */

static inline bool wingo_list_is_empty(const wingo_list_t *list)
{
    return list == NULL || list->count == 0;
}

/*
 * Get list count.
 *
 * @param list      List
 * @return          Number of items
 */

static inline wingo_size wingo_list_count(const wingo_list_t *list)
{
    return list == NULL ? 0 : list->count;
}

/*
 * Sort the list.
 *
 * @param list      List
 * @param cmp       Comparison function
 */

void wingo_list_sort(wingo_list_t *list, int (*cmp)(const void *a, const void *b));

/*
 * Reverse the list.
 *
 * @param list      List
 */

void wingo_list_reverse(wingo_list_t *list);

/* ============================================================================
 * ITERATION MACROS
 * ============================================================================ */

/*
 * Iterate over a singly-linked list.
 *
 * Usage:
 *   wingo_slist_node_t *node;
 *   WINGO_SLIST_FOREACH(list, node) {
 *       printf("%p\n", node->data);
 *   }
 */

#define WINGO_SLIST_FOREACH(list, node) \
    for ((node) = (list)->head; (node) != NULL; (node) = (node)->next)

/*
 * Iterate over a doubly-linked list.
 *
 * Usage:
 *   wingo_list_node_t *node;
 *   WINGO_LIST_FOREACH(list, node) {
 *       printf("%p\n", node->data);
 *   }
 */

#define WINGO_LIST_FOREACH(list, node) \
    for ((node) = (list)->head; (node) != NULL; (node) = (node)->next)

/*
 * Iterate over a doubly-linked list in reverse.
 *
 * Usage:
 *   wingo_list_node_t *node;
 *   WINGO_LIST_FOREACH_REVERSE(list, node) {
 *       printf("%p\n", node->data);
 *   }
 */

#define WINGO_LIST_FOREACH_REVERSE(list, node) \
    for ((node) = (list)->tail; (node) != NULL; (node) = (node)->prev)

/*
 * Iterate over a doubly-linked list safely (allows removal).
 *
 * Usage:
 *   wingo_list_node_t *node, *tmp;
 *   WINGO_LIST_FOREACH_SAFE(list, node, tmp) {
 *       wingo_list_remove_node(list, node);
 *   }
 */

#define WINGO_LIST_FOREACH_SAFE(list, node, tmp) \
    for ((node) = (list)->head, (tmp) = (node) ? (node)->next : NULL; \
         (node) != NULL; \
         (node) = (tmp), (tmp) = (node) ? (node)->next : NULL)

/* ============================================================================
 * END OF HEADER
 * ============================================================================ */

#endif /* WINGO_UTIL_LIST_H */
