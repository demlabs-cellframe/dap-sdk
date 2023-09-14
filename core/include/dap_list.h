/*
 * Doubly-Linked Lists — linked lists that can be iterated over in both directions
 *
 * Nano API for Simple linked list - by BadAss SysMan
 * Attention!!! No internaly locking is performed !
 *
 *  MODIFICATION HISTORY:
 *      17-MAY-2022 RRL Added description for the SLIST's routines;
 *                      renaming arguments to be relevant to the Dem Labs coding style. :-)
 *
 */

#ifndef __DAP_LIST_H__
#define __DAP_LIST_H__

#include    <errno.h>                                                       /* <errno> codes */

#include    "dap_common.h"                                                  /* DAP_ALLOC, DAP_FREE */
#include "utlist.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*dap_callback_destroyed_t)(void*);
typedef void (*dap_callback_t)(void*, void*);
typedef void *(*dap_callback_copy_t)(const void*, void*);
typedef int (*dap_callback_compare_t)(const void*, const void*);
typedef int (*dap_callback_compare_data_t)(const void*, const void*, void*);

/*typedef struct _dap_list dap_list_t;*/

typedef struct __dap_list__
{
    void* data;
    //uint64_t size;
    struct __dap_list__ *next, *prev;
} dap_list_t;

/* Doubly linked lists
 */
void dap_list_free(dap_list_t*);
void dap_list_free_full(dap_list_t*, dap_callback_destroyed_t);
dap_list_t* dap_list_append(dap_list_t*, void*);
dap_list_t* dap_list_prepend(dap_list_t*, void*);
dap_list_t* dap_list_insert(dap_list_t*, void*, uint64_t);
dap_list_t* dap_list_insert_sorted(dap_list_t*, void*, dap_callback_compare_t);
dap_list_t* dap_list_concat(dap_list_t*, dap_list_t*);
dap_list_t* dap_list_remove(dap_list_t*, const void*);
dap_list_t* dap_list_remove_all(dap_list_t*, const void*);
dap_list_t* dap_list_remove_link(dap_list_t*, dap_list_t*);
dap_list_t* dap_list_delete_link(dap_list_t*, dap_list_t*);
dap_list_t* dap_list_copy(dap_list_t*);

dap_list_t* dap_list_copy_deep(dap_list_t*, dap_callback_copy_t, void*);

dap_list_t* dap_list_nth(dap_list_t*, uint64_t);
dap_list_t* dap_list_nth_prev(dap_list_t*, uint64_t);
dap_list_t* dap_list_find(dap_list_t*, const void*, dap_callback_compare_t);
int dap_list_position(dap_list_t*, dap_list_t*);
int dap_list_index(dap_list_t*, const void*);
dap_list_t* dap_list_last(dap_list_t*);
dap_list_t* dap_list_first(dap_list_t*);
uint64_t dap_list_length(dap_list_t*);
dap_list_t* dap_list_sort(dap_list_t*, dap_callback_compare_t);
void* dap_list_nth_data(dap_list_t*, uint64_t);

#define dap_list_prev(list) ((dap_list_t*)(list))->prev
#define dap_list_next(list)	((dap_list_t*)(list))->next

#ifdef __cplusplus
}
#endif

#endif /* __DAP_LIST_H__ */
