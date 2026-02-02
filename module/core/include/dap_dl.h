/**
 * @file dap_dl.h
 * @brief DAP Doubly-Linked List - intrusive macros for embedded lists
 *
 * Intrusive doubly-linked list implementation. "Intrusive" means the
 * list pointers (prev/next) are embedded directly in your structure,
 * not in a separate wrapper.
 *
 * Usage:
 *   struct my_item {
 *       int value;
 *       struct my_item *prev, *next;  // List pointers
 *   };
 *
 *   struct my_item *head = NULL;
 *   struct my_item *item = malloc(sizeof(*item));
 *   item->value = 42;
 *   dap_dl_append(head, item);
 *
 * All operations are O(1) except:
 * - dap_dl_count: O(n)
 * - dap_dl_search: O(n)
 *
 * Copyright (c) 2024-2026 Demlabs
 * License: GNU GPL v3
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Type helpers
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define DAP_DL_TYPEOF(x) __typeof(x)
#else
#define DAP_DL_TYPEOF(x) void*
#endif

// ============================================================================
// Doubly-Linked List Macros
// ============================================================================

/**
 * @brief Append element to end of list
 * @param head List head pointer (will be modified if list was empty)
 * @param add Element to append
 *
 * O(1) operation - uses head->prev as tail pointer
 */
#define dap_dl_append(head, add) do { \
    (add)->next = NULL; \
    if (head) { \
        (add)->prev = (head)->prev; \
        (head)->prev->next = (add); \
        (head)->prev = (add); \
    } else { \
        (head) = (add); \
        (head)->prev = (head); \
    } \
} while (0)

/**
 * @brief Prepend element to beginning of list
 * @param head List head pointer (will be modified)
 * @param add Element to prepend
 *
 * O(1) operation
 */
#define dap_dl_prepend(head, add) do { \
    (add)->next = (head); \
    if (head) { \
        (add)->prev = (head)->prev; \
        (head)->prev = (add); \
    } else { \
        (add)->prev = (add); \
    } \
    (head) = (add); \
} while (0)

/**
 * @brief Delete element from list
 * @param head List head pointer (may be modified)
 * @param del Element to delete
 *
 * O(1) operation
 * Note: Does NOT free the element, only unlinks it
 */
#define dap_dl_delete(head, del) do { \
    if ((del)->prev == (del)) { \
        (head) = NULL; \
    } else if ((del) == (head)) { \
        (del)->next->prev = (del)->prev; \
        (head) = (del)->next; \
    } else { \
        (del)->prev->next = (del)->next; \
        if ((del)->next) { \
            (del)->next->prev = (del)->prev; \
        } else { \
            (head)->prev = (del)->prev; \
        } \
    } \
} while (0)

/**
 * @brief Iterate over list (safe for deletion during iteration)
 * @param head List head
 * @param el Iterator variable
 * @param tmp Temporary variable for safe iteration
 */
#define dap_dl_foreach_safe(head, el, tmp) \
    for ((el) = (head), (tmp) = (el) ? (el)->next : NULL; \
         (el); \
         (el) = (tmp), (tmp) = (el) ? (el)->next : NULL)

/**
 * @brief Iterate over list (NOT safe for deletion)
 * @param head List head
 * @param el Iterator variable
 */
#define dap_dl_foreach(head, el) \
    for ((el) = (head); (el); (el) = (el)->next)

/**
 * @brief Count elements in list
 * @param head List head
 * @param counter Variable to store count
 *
 * O(n) operation
 */
#define dap_dl_count(head, counter) do { \
    DAP_DL_TYPEOF(head) _dl_tmp; \
    (counter) = 0; \
    dap_dl_foreach(head, _dl_tmp) { (counter)++; } \
} while (0)

/**
 * @brief Search for element by field value
 * @param head List head
 * @param out Output variable (found element or NULL)
 * @param field Field name to compare
 * @param val Value to search for
 *
 * O(n) operation
 */
#define dap_dl_search(head, out, field, val) do { \
    (out) = NULL; \
    DAP_DL_TYPEOF(head) _dl_el; \
    dap_dl_foreach(head, _dl_el) { \
        if (_dl_el->field == (val)) { \
            (out) = _dl_el; \
            break; \
        } \
    } \
} while (0)

/**
 * @brief Insert element after another
 * @param head List head (may be modified if listel is tail)
 * @param listel Element after which to insert
 * @param add Element to insert
 *
 * O(1) operation
 */
#define dap_dl_insert_after(head, listel, add) do { \
    (add)->prev = (listel); \
    (add)->next = (listel)->next; \
    if ((listel)->next) { \
        (listel)->next->prev = (add); \
    } else { \
        (head)->prev = (add); \
    } \
    (listel)->next = (add); \
} while (0)

/**
 * @brief Insert element before another
 * @param head List head (may be modified if listel is head)
 * @param listel Element before which to insert
 * @param add Element to insert
 *
 * O(1) operation
 */
#define dap_dl_insert_before(head, listel, add) do { \
    (add)->next = (listel); \
    (add)->prev = (listel)->prev; \
    if ((listel) == (head)) { \
        (head) = (add); \
    } else { \
        (listel)->prev->next = (add); \
    } \
    (listel)->prev = (add); \
} while (0)

/**
 * @brief Concatenate two lists (append list2 to list1)
 * @param head1 First list head (will contain result)
 * @param head2 Second list head (will be empty after)
 *
 * O(1) operation
 */
#define dap_dl_concat(head1, head2) do { \
    if (head2) { \
        if (head1) { \
            DAP_DL_TYPEOF(head1) _dl_tail1 = (head1)->prev; \
            DAP_DL_TYPEOF(head1) _dl_tail2 = (head2)->prev; \
            _dl_tail1->next = (head2); \
            (head2)->prev = _dl_tail1; \
            (head1)->prev = _dl_tail2; \
        } else { \
            (head1) = (head2); \
        } \
        (head2) = NULL; \
    } \
} while (0)

/**
 * @brief Replace element in list
 * @param head List head (may be modified if replacing head)
 * @param old Element to replace
 * @param newel New element
 *
 * O(1) operation
 */
#define dap_dl_replace(head, old, newel) do { \
    (newel)->next = (old)->next; \
    (newel)->prev = (old)->prev; \
    if ((old) == (head)) { \
        (head) = (newel); \
        if ((old)->next) (old)->next->prev = (newel); \
        if ((old)->prev == (old)) (newel)->prev = (newel); \
        else (head)->prev = (old)->prev; \
    } else { \
        (old)->prev->next = (newel); \
        if ((old)->next) (old)->next->prev = (newel); \
        else (head)->prev = (newel); \
    } \
} while (0)

/**
 * @brief Search element by scalar field value (no comparator)
 * @param head List head
 * @param out Output variable (found element or NULL)
 * @param field Field name to compare
 * @param val Value to search for
 *
 * O(n) operation
 */
#define dap_dl_search_scalar(head, out, field, val) do { \
    (out) = NULL; \
    DAP_DL_TYPEOF(head) _dl_el; \
    dap_dl_foreach(head, _dl_el) { \
        if (_dl_el->field == (val)) { \
            (out) = _dl_el; \
            break; \
        } \
    } \
} while (0)

/**
 * @brief Search element using comparator function
 * @param head List head
 * @param out Output variable
 * @param elt Element to compare against
 * @param cmp Comparator function: int cmp(a, b)
 *
 * O(n) operation
 */
#define dap_dl_search_cmp(head, out, elt, cmp) do { \
    (out) = NULL; \
    DAP_DL_TYPEOF(head) _dl_el; \
    dap_dl_foreach(head, _dl_el) { \
        if ((cmp)(_dl_el, elt) == 0) { \
            (out) = _dl_el; \
            break; \
        } \
    } \
} while (0)

/**
 * @brief Insert element in sorted order
 * @param head List head (may be modified)
 * @param add Element to insert
 * @param cmp Comparator function: int cmp(a, b) - returns <0 if a<b
 *
 * O(n) operation
 */
#define dap_dl_insert_inorder(head, add, cmp) do { \
    if (!(head)) { \
        (add)->prev = (add)->next = NULL; \
        (add)->prev = (add); \
        (head) = (add); \
    } else { \
        DAP_DL_TYPEOF(head) _dl_pos = NULL; \
        DAP_DL_TYPEOF(head) _dl_el; \
        dap_dl_foreach(head, _dl_el) { \
            if ((cmp)(add, _dl_el) <= 0) { \
                _dl_pos = _dl_el; \
                break; \
            } \
        } \
        if (_dl_pos) { \
            dap_dl_insert_before(head, _dl_pos, add); \
        } else { \
            dap_dl_append(head, add); \
        } \
    } \
} while (0)

/**
 * @brief Sort list using merge sort
 * @param head List head (will be modified)
 * @param cmp Comparator function
 *
 * O(n log n) operation
 */
#define dap_dl_sort(head, cmp) do { \
    if ((head) && (head)->next) { \
        DAP_DL_TYPEOF(head) _dl_p, _dl_q, _dl_e, _dl_tail, _dl_list; \
        int _dl_insize, _dl_nmerges, _dl_psize, _dl_qsize, _dl_i; \
        _dl_list = (head); \
        _dl_insize = 1; \
        while (1) { \
            _dl_p = _dl_list; \
            _dl_list = NULL; \
            _dl_tail = NULL; \
            _dl_nmerges = 0; \
            while (_dl_p) { \
                _dl_nmerges++; \
                _dl_q = _dl_p; \
                _dl_psize = 0; \
                for (_dl_i = 0; _dl_i < _dl_insize; _dl_i++) { \
                    _dl_psize++; \
                    _dl_q = _dl_q->next; \
                    if (!_dl_q) break; \
                } \
                _dl_qsize = _dl_insize; \
                while (_dl_psize > 0 || (_dl_qsize > 0 && _dl_q)) { \
                    if (_dl_psize == 0) { \
                        _dl_e = _dl_q; _dl_q = _dl_q->next; _dl_qsize--; \
                    } else if (_dl_qsize == 0 || !_dl_q) { \
                        _dl_e = _dl_p; _dl_p = _dl_p->next; _dl_psize--; \
                    } else if ((cmp)(_dl_p, _dl_q) <= 0) { \
                        _dl_e = _dl_p; _dl_p = _dl_p->next; _dl_psize--; \
                    } else { \
                        _dl_e = _dl_q; _dl_q = _dl_q->next; _dl_qsize--; \
                    } \
                    if (_dl_tail) { \
                        _dl_tail->next = _dl_e; \
                    } else { \
                        _dl_list = _dl_e; \
                    } \
                    _dl_e->prev = _dl_tail; \
                    _dl_tail = _dl_e; \
                } \
                _dl_p = _dl_q; \
            } \
            if (_dl_tail) _dl_tail->next = NULL; \
            if (_dl_nmerges <= 1) { \
                (head) = _dl_list; \
                if (_dl_list) { \
                    DAP_DL_TYPEOF(head) _dl_last = _dl_list; \
                    while (_dl_last->next) _dl_last = _dl_last->next; \
                    (head)->prev = _dl_last; \
                } \
                break; \
            } \
            _dl_insize *= 2; \
        } \
    } \
} while (0)

#ifdef __cplusplus
}
#endif
