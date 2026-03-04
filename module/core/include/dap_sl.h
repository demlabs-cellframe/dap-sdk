/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * DAP SDK singly-linked list macros (replacement for utlist.h LL_* macros)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Append element to singly-linked list
 * @param head List head (may be modified)
 * @param add Element to add
 * 
 * O(n) operation - walks to end of list
 */
#define dap_sl_append(head, add) do { \
    (add)->next = NULL; \
    if (!(head)) { \
        (head) = (add); \
    } else { \
        typeof(head) _sl_tmp = (head); \
        while (_sl_tmp->next) _sl_tmp = _sl_tmp->next; \
        _sl_tmp->next = (add); \
    } \
} while (0)

/**
 * @brief Prepend element to singly-linked list
 * @param head List head (modified)
 * @param add Element to add
 * 
 * O(1) operation
 */
#define dap_sl_prepend(head, add) do { \
    (add)->next = (head); \
    (head) = (add); \
} while (0)

/**
 * @brief Delete element from singly-linked list
 * @param head List head (may be modified)
 * @param del Element to delete
 * 
 * O(n) operation - needs to find previous element
 */
#define dap_sl_delete(head, del) do { \
    if ((head) == (del)) { \
        (head) = (head)->next; \
    } else { \
        typeof(head) _sl_tmp = (head); \
        while (_sl_tmp->next && _sl_tmp->next != (del)) \
            _sl_tmp = _sl_tmp->next; \
        if (_sl_tmp->next) \
            _sl_tmp->next = (del)->next; \
    } \
} while (0)

/**
 * @brief Iterate over singly-linked list (NOT safe for deletion)
 * @param head List head
 * @param el Iterator variable
 */
#define dap_sl_foreach(head, el) \
    for ((el) = (head); (el); (el) = (el)->next)

/**
 * @brief Iterate over singly-linked list (safe for deletion)
 * @param head List head
 * @param el Iterator variable
 * @param tmp Temporary variable
 */
#define dap_sl_foreach_safe(head, el, tmp) \
    for ((el) = (head), (tmp) = (el) ? (el)->next : NULL; \
         (el); \
         (el) = (tmp), (tmp) = (el) ? (el)->next : NULL)

/**
 * @brief Insert element maintaining sorted order (ascending per cmp)
 * @param head List head (may be modified)
 * @param add Element to add
 * @param cmp Comparison function: int cmp(typeof(head) a, typeof(head) b)
 */
#define dap_sl_insert_inorder(head, add, cmp) do { \
    if (!(head) || cmp((add), (head)) <= 0) { \
        (add)->next = (head); \
        (head) = (add); \
    } else { \
        typeof(head) _sl_tmp = (head); \
        while (_sl_tmp->next && cmp((add), _sl_tmp->next) > 0) \
            _sl_tmp = _sl_tmp->next; \
        (add)->next = _sl_tmp->next; \
        _sl_tmp->next = (add); \
    } \
} while (0)

/**
 * @brief Count elements in singly-linked list
 * @param head List head
 * @param counter Output variable (will be set to count)
 */
#define dap_sl_count(head, counter) do { \
    (counter) = 0; \
    typeof(head) _sl_el; \
    dap_sl_foreach(head, _sl_el) { \
        (counter)++; \
    } \
} while (0)

#ifdef __cplusplus
}
#endif
