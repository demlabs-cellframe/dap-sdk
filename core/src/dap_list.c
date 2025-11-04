/*
 * Doubly-Linked Lists — linked lists that can be iterated over in both directions
 */

#include <stddef.h>
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
    dap_list_t *l_el, *l_tmp;
    DL_FOREACH_SAFE(a_list, l_el, l_tmp) {
        DL_DELETE(a_list, l_el);
        DAP_DELETE(l_el);
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
    if (!a_list) {
        return; // Safe to call with NULL
    }
    dap_list_t *l_el, *l_tmp;
    DL_FOREACH_SAFE(a_list, l_el, l_tmp) {
        DL_DELETE(a_list, l_el);
        if (l_el->data)
            a_free_func ? a_free_func(l_el->data) : DAP_DELETE(l_el->data);
        DAP_DELETE(l_el);
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
 * |[<!-- language="C" -->
 * // Notice that these are initialized to the empty list.
 * DapList *string_list = NULL, *number_list = NULL;
 *
 * // This is a list of strings.
 * string_list = dap_list_append (string_list, "first");
 * string_list = dap_list_append (string_list, "second");
 *
 * // This is a list of integers.
 * number_list = dap_list_append (number_list, INT_TO_POINTER (27));
 * number_list = dap_list_append (number_list, INT_TO_POINTER (14));
 * ]|
 *
 * Returns: either @list or the new start of the DapList if @list was %NULL
 */
dap_list_t *dap_list_append(dap_list_t *a_list, void *a_data)
{
    if(!a_data)
        return a_list;
    dap_list_t *l_el = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_list_t, a_list);
    l_el->data = a_data;
    return ({ DL_APPEND(a_list, l_el); a_list; });
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
 * |[<!-- language="C" -->
 * // Notice that it is initialized to the empty list.
 * DapList *list = NULL;
 *
 * list = dap_list_prepend (list, "last");
 * list = dap_list_prepend (list, "first");
 * ]|
 *
 * Do not use this function to prepend a new element to a different
 * element than the start of the list. Use dap_list_insert_before() instead.
 *
 * Returns: a pointer to the newly prepended element, which is the new
 *     start of the DapList
 */
dap_list_t *dap_list_prepend(dap_list_t *a_list, void *a_data)
{
    dap_return_val_if_pass(!a_data, a_list);
    dap_list_t *l_el = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_list_t, a_list);
    l_el->data = a_data;
    return ({ DL_PREPEND(a_list, l_el); a_list; });
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
    return ({ DL_PREPEND_ELEM(a_list, l_pos, l_el); a_list; });
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
 * This function is for example used to move an element in the list.
 * The following example moves an element to the top of the list:
 * |[<!-- language="C" -->
 * list = dap_list_remove_link (list, llink);
 * list = dap_list_concat (llink, list);
 * ]|
 *
 * Returns: the start of the new DapList, which equals @list1 if not %NULL
 */
dap_list_t *dap_list_concat(dap_list_t *a_list1, dap_list_t *a_list2)
{
    return ({ DL_CONCAT(a_list1, a_list2); a_list1; });
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
    dap_list_t *l_el, *l_tmp;
    DL_FOREACH_SAFE(a_list, l_el, l_tmp) {
        if (l_el->data == a_data) {
            DL_DELETE(a_list, l_el);
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
    dap_list_t *l_el, *l_tmp;
    DL_FOREACH_SAFE(a_list, l_el, l_tmp) {
        if (l_el->data == a_data) {
            DL_DELETE(a_list, l_el);
            DAP_DELETE(l_el);
        }
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
 * This function is for example used to move an element in the list
 * (see the example for dap_list_concat()) or to remove an element in
 * the list before freeing its data:
 * |[<!-- language="C" -->
 * list = dap_list_remove_link (list, llink);
 * free_some_data_that_may_access_the_list_again (llink->data);
 * dap_list_free (llink);
 * ]|
 *
 * Returns: the (possibly changed) start of the DapList
 */
inline dap_list_t *dap_list_remove_link(dap_list_t *a_list, dap_list_t *a_link)
{
    return ({ DL_DELETE(a_list, a_link); a_list; });
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
    return ({ DL_DELETE(a_list, a_link); DAP_DELETE(a_link); a_list; });
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
 * In contrast with dap_list_copy(), this function uses @func to make
 * a copy of each list element, in addition to copying the list
 * container itself.
 *
 * @func, as a #DapCopyFunc, takes two arguments, the data to be copied
 * and a @user_data pointer. It's safe to pass %NULL as user_data,
 * if the copy function takes only one argument.
 *
 * For instance,
 * |[<!-- language="C" -->
 * another_list = dap_list_copy_deep (list, (DapCopyFunc) dap_object_ref, NULL);
 * ]|
 *
 * And, to entirely free the new list, you could do:
 * |[<!-- language="C" -->
 * dap_list_free_full (another_list, dap_object_unref);
 * ]|
 *
 * Returns: the start of the new list that holds a full copy of @list,
 *     use dap_list_free_full() to free it
 */
dap_list_t *dap_list_copy_deep(dap_list_t *a_list, dap_callback_copy_t a_func, void *a_user_data)
{
    dap_list_t *l_deep_copy = NULL, *l_el;
    DL_FOREACH(a_list, l_el)
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
    while((n-- > 0) && a_list)
        a_list = a_list->next;

    return a_list;
}

/**
 * dap_list_find:
 * @list: a DapList, this must point to the top of the list
 * @data: the element data to find
 *
 * Finds the element in a DapList which contains the given data.
 *
 * Returns: the found DapList element, or %NULL if it is not found
 */
dap_list_t *dap_list_find(dap_list_t *a_list, const void *a_data, dap_callback_compare_t a_cmp)
{
    dap_list_t *l_el = NULL;
    return a_cmp
            ? ({ dap_list_t l_sought = { .data = (void*)a_data }; DL_SEARCH(a_list, l_el, &l_sought, a_cmp); l_el; })
            : ({ DL_SEARCH_SCALAR(a_list, l_el, data, a_data); l_el; });
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
    dap_list_t *l_el;
    DL_FOREACH(a_list, l_el) {
        if (l_el == a_link)
            return i;
        ++i;
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
    dap_list_t *l_el;
    DL_FOREACH(a_list, l_el) {
        if (l_el->data == a_data)
            return i;
        ++i;
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
dap_list_t * dap_list_last(dap_list_t *a_list)
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
    dap_list_t *l_el;
    uint64_t l_len;
    return ({ DL_COUNT(a_list, l_el, l_len); l_len; });
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
 * If you are adding many new elements to a list, and the number of
 * new elements is much larger than the length of the list, use
 * dap_list_prepend() to add the new items and sort the list afterwards
 * with dap_list_sort().
 *
 * Returns: the (possibly changed) start of the DapList
 */
dap_list_t *dap_list_insert_sorted(dap_list_t *a_list, void *a_data, dap_callback_compare_t a_func)
{
    dap_list_t *l_new_el = DAP_NEW_Z(dap_list_t);
    l_new_el->data = a_data;
    return ({ DL_INSERT_INORDER(a_list, l_new_el, a_func); a_list; });
}

/**
 * dap_list_sort:
 * @list: a DapList, this must point to the top of the list
 * @compare_func: the comparison function used to sort the DapList.
 *     This function is passed the data from 2 elements of the DapList
 *     and should return 0 if they are equal, a negative value if the
 *     first element comes before the second, or a positive value if
 *     the first element comes after the second.
 *
 * Sorts a DapList using the given comparison function. The algorithm
 * used is a stable sort.
 *
 * Returns: the (possibly changed) start of the DapList
 */
/**
 * DapCompareFunc:
 * @a: a value
 * @b: a value to compare with
 *
 * Specifies the type of a comparison function used to compare two
 * values.  The function should return a negative integer if the first
 * value comes before the second, 0 if they are equal, or a positive
 * integer if the first value comes after the second.
 *
 * Returns: negative value if @a < @b; zero if @a = @b; positive
 *          value if @a > @b
 */
dap_list_t *dap_list_sort(dap_list_t *a_list, dap_callback_compare_t a_cmp)
{
    return ({ DL_SORT(a_list, a_cmp); a_list; });
}

static int s_random_compare(dap_list_t UNUSED_ARG *a_list1, dap_list_t UNUSED_ARG *a_list2)
{
    return rand() % 2 ? -1 : 1;
}

dap_list_t *dap_list_shuffle(dap_list_t *a_list)
{
    return dap_list_sort(a_list, s_random_compare);
}
