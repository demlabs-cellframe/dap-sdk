/*
 * Doubly-Linked Lists — linked lists that can be iterated over in both directions
 *
 * Native DAP SDK implementation without external dependencies
 */

#include <stddef.h>
#include <stdlib.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_list.h"

#define LOG_TAG "dap_list"

/**
 * dap_list_free:
 * Frees all of the memory used by a DapList.
 * The freed elements are returned to the slice allocator.
 * If list elements contain dynamically-allocated memory, you should
 * either use dap_list_free_full() or free them manually first.
 */
void dap_list_free(dap_list_t *a_list)
{
    dap_list_t *l_el = a_list;
    while (l_el) {
        dap_list_t *l_next = l_el->next;
        DAP_DELETE(l_el);
        l_el = l_next;
    }
}

/**
 * dap_list_free_full:
 * @list: a pointer to a DapList
 * @free_func: the function to be called to free each element's data
 *  if NULL it calls DAP_DELETE() for it
 * Convenience method, which frees all the memory used by a DapList,
 * and calls @free_func on every element's data.
 */
void dap_list_free_full(dap_list_t *a_list, dap_callback_destroyed_t a_free_func)
{
    dap_list_t *l_el = a_list;
    while (l_el) {
        dap_list_t *l_next = l_el->next;
        if (l_el->data)
            a_free_func ? a_free_func(l_el->data) : DAP_DELETE(l_el->data);
        DAP_DELETE(l_el);
        l_el = l_next;
    }
}

/**
 * Internal: append element to doubly-linked list (O(1) operation)
 * Uses prev pointer of head to track tail for O(1) append
 */
static inline void s_dl_append(dap_list_t **a_head, dap_list_t *a_add)
{
    if (*a_head) {
        a_add->prev = (*a_head)->prev;
        (*a_head)->prev->next = a_add;
        (*a_head)->prev = a_add;
        a_add->next = NULL;
    } else {
        *a_head = a_add;
        (*a_head)->prev = *a_head;
        (*a_head)->next = NULL;
    }
}

/**
 * Internal: prepend element to doubly-linked list
 */
static inline void s_dl_prepend(dap_list_t **a_head, dap_list_t *a_add)
{
    a_add->next = *a_head;
    if (*a_head) {
        a_add->prev = (*a_head)->prev;
        (*a_head)->prev = a_add;
    } else {
        a_add->prev = a_add;
    }
    *a_head = a_add;
}

/**
 * Internal: delete element from doubly-linked list
 */
static inline void s_dl_delete(dap_list_t **a_head, dap_list_t *a_del)
{
    if (!*a_head || !a_del || !a_del->prev)
        return;
    
    if (a_del->prev == a_del) {
        // Single element
        *a_head = NULL;
    } else if (a_del == *a_head) {
        // Deleting head
        a_del->next->prev = a_del->prev;
        *a_head = a_del->next;
    } else {
        // Deleting middle or tail
        a_del->prev->next = a_del->next;
        if (a_del->next) {
            a_del->next->prev = a_del->prev;
        } else {
            (*a_head)->prev = a_del->prev;
        }
    }
}

/**
 * dap_list_append:
 * @list: a pointer to a DapList
 * @data: the data for the new element
 *
 * Adds a new element on to the end of the list.
 *
 * Note that the return value is the new start of the list,
 * if @list was empty; make sure you store the new value.
 *
 * Returns: either @list or the new start of the DapList if @list was %NULL
 */
dap_list_t *dap_list_append(dap_list_t *a_list, void *a_data)
{
    if (!a_data)
        return a_list;
    dap_list_t *l_el = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_list_t, a_list);
    l_el->data = a_data;
    s_dl_append(&a_list, l_el);
    return a_list;
}

/**
 * dap_list_prepend:
 * @list: a pointer to a DapList, this must point to the top of the list
 * @data: the data for the new element
 *
 * Prepends a new element on to the start of the list.
 *
 * Note that the return value is the new start of the list,
 * which will have changed, so make sure you store the new value.
 *
 * Returns: a pointer to the newly prepended element, which is the new
 *     start of the DapList
 */
dap_list_t *dap_list_prepend(dap_list_t *a_list, void *a_data)
{
    dap_return_val_if_pass(!a_data, a_list);
    dap_list_t *l_el = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_list_t, a_list);
    l_el->data = a_data;
    s_dl_prepend(&a_list, l_el);
    return a_list;
}

/**
 * dap_list_insert:
 * @list: a pointer to a DapList, this must point to the top of the list
 * @data: the data for the new element
 * @position: the position to insert the element. If this is
 *     negative, or is larger than the number of elements in the
 *     list, the new element is added on to the end of the list.
 *
 * Inserts a new element into the list at the given position.
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_insert(dap_list_t *a_list, void* a_data, uint64_t a_position)
{
    if (!a_position)
        return dap_list_prepend(a_list, a_data);
    
    dap_list_t *l_el = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_list_t, a_list);
    dap_list_t *l_pos = dap_list_nth(a_list, a_position);
    l_el->data = a_data;
    
    // Insert before l_pos (DL_PREPEND_ELEM logic)
    if (l_pos) {
        l_el->next = l_pos;
        l_el->prev = l_pos->prev;
        l_pos->prev = l_el;
        if (a_list == l_pos) {
            a_list = l_el;
        } else {
            l_el->prev->next = l_el;
        }
    } else {
        // Position beyond list, append
        s_dl_append(&a_list, l_el);
    }
    return a_list;
}

/**
 * dap_list_concat:
 * @list1: a DapList, this must point to the top of the list
 * @list2: the DapList to add to the end of the first DapList,
 *     this must point  to the top of the list
 *
 * Adds the second DapList onto the end of the first DapList.
 * Note that the elements of the second DapList are not copied.
 * They are used directly.
 *
 * Returns: the start of the new DapList, which equals @list1 if not %NULL
 */
dap_list_t *dap_list_concat(dap_list_t *a_list1, dap_list_t *a_list2)
{
    if (!a_list2)
        return a_list1;
    
    if (a_list1) {
        dap_list_t *l_tmp = a_list2->prev;
        a_list2->prev = a_list1->prev;
        a_list1->prev->next = a_list2;
        a_list1->prev = l_tmp;
    } else {
        a_list1 = a_list2;
    }
    return a_list1;
}

/**
 * dap_list_remove:
 * @list: a DapList, this must point to the top of the list
 * @data: the data of the element to remove
 *
 * Removes an element from a DapList.
 * If two elements contain the same data, only the first is removed.
 * If none of the elements contain the data, the DapList is unchanged.
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_remove(dap_list_t *a_list, const void *a_data)
{
    for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next) {
        if (l_el->data == a_data) {
            s_dl_delete(&a_list, l_el);
            DAP_DELETE(l_el);
            break;
        }
    }
    return a_list;
}

/**
 * dap_list_remove_all:
 * @list: a DapList, this must point to the top of the list
 * @data: data to remove
 *
 * Removes all list nodes with data equal to @data.
 * Returns the new head of the list. Contrast with
 * dap_list_remove() which removes only the first node
 * matching the given data.
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_remove_all(dap_list_t *a_list, const void *a_data)
{
    dap_list_t *l_el = a_list;
    while (l_el) {
        dap_list_t *l_next = l_el->next;
        if (l_el->data == a_data) {
            s_dl_delete(&a_list, l_el);
            DAP_DELETE(l_el);
        }
        l_el = l_next;
    }
    return a_list;
}

/**
 * dap_list_remove_link:
 * @list: a DapList, this must point to the top of the list
 * @llink: an element in the DapList
 *
 * Removes an element from a DapList, without freeing the element.
 * The removed element's prev and next links are set to %NULL, so
 * that it becomes a self-contained list with one element.
 *
 * Returns: the (possibly changed) start of the DapList
 */
inline dap_list_t *dap_list_remove_link(dap_list_t *a_list, dap_list_t *a_link)
{
    s_dl_delete(&a_list, a_link);
    if (a_link) {
        a_link->next = NULL;
        a_link->prev = NULL;
    }
    return a_list;
}

/**
 * dap_list_delete_link:
 * @list: a DapList, this must point to the top of the list
 * @link_: node to delete from @list
 *
 * Removes the node link_ from the list and frees it.
 * Compare this to dap_list_remove_link() which removes the node
 * without freeing it.
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_delete_link(dap_list_t *a_list, dap_list_t *a_link)
{
    s_dl_delete(&a_list, a_link);
    DAP_DELETE(a_link);
    return a_list;
}

/**
 * dap_list_copy:
 * @list: a DapList, this must point to the top of the list
 *
 * Copies a DapList.
 *
 * Note that this is a "shallow" copy. If the list elements
 * consist of pointers to data, the pointers are copied but
 * the actual data is not. See dap_list_copy_deep() if you need
 * to copy the data as well.
 *
 * Returns: the start of the new list that holds the same data as @list
 */
dap_list_t *dap_list_copy(dap_list_t *a_list)
{
    return dap_list_copy_deep(a_list, NULL, NULL);
}

/**
 * dap_list_copy_deep:
 * @list: a DapList, this must point to the top of the list
 * @func: a copy function used to copy every element in the list
 * @user_data: user data passed to the copy function @func, or %NULL
 *
 * Makes a full (deep) copy of a DapList.
 *
 * Returns: the start of the new list that holds a full copy of @list,
 *     use dap_list_free_full() to free it
 */
dap_list_t *dap_list_copy_deep(dap_list_t *a_list, dap_callback_copy_t a_func, void *a_user_data)
{
    dap_list_t *l_deep_copy = NULL;
    for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next)
        l_deep_copy = dap_list_append(l_deep_copy, a_func ? a_func(l_el->data, a_user_data) : l_el->data);
    return l_deep_copy;
}

/**
 * dap_list_nth:
 * @list: a DapList, this must point to the top of the list
 * @n: the position of the element, counting from 0
 *
 * Gets the element at the given position in a DapList.
 *
 * Returns: the element, or %NULL if the position is off
 *     the end of the DapList
 */
dap_list_t *dap_list_nth(dap_list_t *a_list, uint64_t n)
{
    while ((n-- > 0) && a_list)
        a_list = a_list->next;
    return a_list;
}

/**
 * dap_list_find:
 * @list: a DapList, this must point to the top of the list
 * @data: the element data to find
 * @cmp: optional comparison function (can be NULL for pointer comparison)
 *
 * Finds the element in a DapList which contains the given data.
 *
 * Returns: the found DapList element, or %NULL if it is not found
 */
dap_list_t *dap_list_find(dap_list_t *a_list, const void *a_data, dap_callback_compare_t a_cmp)
{
    if (a_cmp) {
        // Search with comparison function
        dap_list_t l_sought = { .data = (void*)a_data };
        for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next) {
            if (a_cmp(l_el, &l_sought) == 0)
                return l_el;
        }
    } else {
        // Search by pointer equality
        for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next) {
            if (l_el->data == a_data)
                return l_el;
        }
    }
    return NULL;
}

/**
 * dap_list_position:
 * @list: a DapList, this must point to the top of the list
 * @llink: an element in the DapList
 *
 * Gets the position of the given element
 * in the DapList (starting from 0).
 *
 * Returns: the position of the element in the DapList,
 *     or -1 if the element is not found
 */
int dap_list_position(dap_list_t *a_list, dap_list_t *a_link)
{
    int i = 0;
    for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next, ++i) {
        if (l_el == a_link)
            return i;
    }
    return -1;
}

/**
 * dap_list_index:
 * @list: a DapList, this must point to the top of the list
 * @data: the data to find
 *
 * Gets the position of the element containing
 * the given data (starting from 0).
 *
 * Returns: the index of the element containing the data,
 *     or -1 if the data is not found
 */
int dap_list_index(dap_list_t *a_list, const void *a_data)
{
    int i = 0;
    for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next, ++i) {
        if (l_el->data == a_data)
            return i;
    }
    return -1;
}

/**
 * dap_list_last:
 * @list: any DapList element
 *
 * Gets the last element in a DapList.
 *
 * Returns: the last element in the DapList,
 *     or %NULL if the DapList has no elements
 */
dap_list_t *dap_list_last(dap_list_t *a_list)
{
    return a_list ? dap_list_first(a_list)->prev : NULL;
}

/**
 * dap_list_first:
 * @list: any DapList element
 *
 * Gets the first element in a DapList.
 *
 * Returns: the first element in the DapList,
 *     or %NULL if the DapList has no elements
 */
dap_list_t *dap_list_first(dap_list_t *a_list)
{
    if (!a_list)
        return NULL;
    if (!a_list->prev)
        return a_list;
    while (a_list->prev->next)
        a_list = a_list->prev;
    return a_list;
}

/**
 * dap_list_length:
 * @list: a DapList, this must point to the top of the list
 *
 * Gets the number of elements in a DapList.
 *
 * This function iterates over the whole list to count its elements.
 *
 * Returns: the number of elements in the DapList
 */
uint64_t dap_list_length(dap_list_t *a_list)
{
    uint64_t l_len = 0;
    for (dap_list_t *l_el = a_list; l_el; l_el = l_el->next)
        ++l_len;
    return l_len;
}

/**
 * dap_list_insert_sorted:
 * @list: a pointer to a DapList, this must point to the top of the
 *     already sorted list
 * @data: the data for the new element
 * @func: the function to compare elements in the list. It should
 *     return a number > 0 if the first parameter comes after the
 *     second parameter in the sort order.
 *
 * Inserts a new element into the list, using the given comparison
 * function to determine its position.
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_insert_sorted(dap_list_t *a_list, void *a_data, dap_callback_compare_t a_func)
{
    dap_list_t *l_new_el = DAP_NEW_Z(dap_list_t);
    l_new_el->data = a_data;
    
    if (!a_list) {
        l_new_el->prev = l_new_el;
        l_new_el->next = NULL;
        return l_new_el;
    }
    
    // Find insertion position (DL_LOWER_BOUND logic)
    dap_list_t *l_pos = NULL;
    if (a_func(a_list, l_new_el) < 0) {
        for (l_pos = a_list; l_pos->next; l_pos = l_pos->next) {
            if (a_func(l_pos->next, l_new_el) >= 0)
                break;
        }
    }
    
    // Insert after l_pos (DL_APPEND_ELEM logic)
    if (l_pos) {
        l_new_el->next = l_pos->next;
        l_new_el->prev = l_pos;
        l_pos->next = l_new_el;
        if (l_new_el->next) {
            l_new_el->next->prev = l_new_el;
        } else {
            a_list->prev = l_new_el;
        }
    } else {
        // Insert at head
        s_dl_prepend(&a_list, l_new_el);
    }
    return a_list;
}

/**
 * dap_list_sort:
 * @list: a DapList, this must point to the top of the list
 * @compare_func: the comparison function used to sort the DapList.
 *
 * Sorts a DapList using the given comparison function. The algorithm
 * used is a stable sort (merge sort).
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_sort(dap_list_t *a_list, dap_callback_compare_t a_cmp)
{
    if (!a_list)
        return NULL;
    
    // Merge sort implementation (O(n log n))
    int l_insize = 1;
    int l_looping = 1;
    
    while (l_looping) {
        dap_list_t *l_p = a_list;
        a_list = NULL;
        dap_list_t *l_tail = NULL;
        int l_nmerges = 0;
        
        while (l_p) {
            l_nmerges++;
            dap_list_t *l_q = l_p;
            int l_psize = 0;
            
            for (int i = 0; i < l_insize; i++) {
                l_psize++;
                l_q = l_q->next;
                if (!l_q) break;
            }
            
            int l_qsize = l_insize;
            
            while (l_psize > 0 || (l_qsize > 0 && l_q)) {
                dap_list_t *l_e;
                if (l_psize == 0) {
                    l_e = l_q;
                    l_q = l_q->next;
                    l_qsize--;
                } else if (l_qsize == 0 || !l_q) {
                    l_e = l_p;
                    l_p = l_p->next;
                    l_psize--;
                } else if (a_cmp(l_p, l_q) <= 0) {
                    l_e = l_p;
                    l_p = l_p->next;
                    l_psize--;
                } else {
                    l_e = l_q;
                    l_q = l_q->next;
                    l_qsize--;
                }
                
                if (l_tail) {
                    l_tail->next = l_e;
                } else {
                    a_list = l_e;
                }
                l_e->prev = l_tail;
                l_tail = l_e;
            }
            l_p = l_q;
        }
        
        if (a_list) {
            a_list->prev = l_tail;
        }
        if (l_tail) {
            l_tail->next = NULL;
        }
        
        if (l_nmerges <= 1) {
            l_looping = 0;
        }
        l_insize *= 2;
    }
    return a_list;
}

static int s_random_compare(dap_list_t UNUSED_ARG *a_list1, dap_list_t UNUSED_ARG *a_list2)
{
    return rand() % 2 ? -1 : 1;
}

dap_list_t *dap_list_shuffle(dap_list_t *a_list)
{
    return dap_list_sort(a_list, s_random_compare);
}
