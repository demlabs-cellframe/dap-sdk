/**
 * @file dap_proc_defer.h
 * @brief Advanced/internal — processor defer queue + typed WFQ drain callbacks (data layer).
 *
 * Pulled in by @ref dap_proc_thread_api.h for @c dap_proc_ctx_t layout.
 * The batch execution inlines (@ref dap_proc_exec.h) and the lane drain
 * machine (@ref dap_proc_dispatch.h) are not pulled from here; only
 * @ref dap_proc_thread_api.h / @ref dap_io.h bring this defer layer in.
 */
#pragma once

#include <stdlib.h>
#include <string.h>

#include "dap_bus.h"
#include "dap_conn.h"

/* ================================================================== */
/*  Typed drain callbacks — const pointer into lane buffer              */
/* ================================================================== */

typedef dap_msg_rc_t (*dap_msg_batch_cb_t)(const dap_batch_task_t *a_task, void *a_arg);
typedef dap_msg_rc_t (*dap_msg_callback_cb_t)(const dap_callback_task_t *a_task, void *a_arg);
typedef dap_msg_rc_t (*dap_msg_heap_cb_t)(const dap_heap_task_t *a_task, void *a_arg);

typedef struct dap_defer_entry {
    uint8_t                  msg_type;
    uint16_t                 conn_idx;
    struct dap_defer_entry  *next;
    union {
        dap_batch_task_t     batch;
        dap_callback_task_t  callback;
        dap_heap_task_t      heap;
        dap_vmqueue_hdr_t   *custom_hdr;
    };
} dap_defer_entry_t;

typedef struct {
    dap_defer_entry_t *head, *tail;
    _Atomic uint64_t   mask[DAP_SLAB_BITS_WORDS];
} dap_defer_queue_t;

DAP_STATIC_INLINE void dap_proc_defer_queue_clear(dap_defer_queue_t *a_q)
{
    for (dap_defer_entry_t *l_e = a_q->head; l_e; ) {
        dap_defer_entry_t *l_next = l_e->next;
        switch (l_e->msg_type) {
        case DAP_MSG_HEAP:
            if (l_e->heap.cleanup)
                l_e->heap.cleanup(l_e->heap.ptr);
            break;
        case DAP_MSG_BATCH:
        case DAP_MSG_CALLBACK:
        case DAP_MSG_TIMER:
            break;
        default:
            free(l_e->custom_hdr);
            break;
        }
        free(l_e);
        l_e = l_next;
    }
    a_q->head = a_q->tail = NULL;
    memset(a_q->mask, 0, sizeof(a_q->mask));
}

DAP_STATIC_INLINE bool
dap_defer_push_batch(dap_defer_queue_t *a_q, const dap_batch_task_t *a_t,
                      uint16_t a_conn_idx)
{
    dap_defer_entry_t *l_e = malloc(sizeof(*l_e));
    if (!l_e) return false;
    l_e->msg_type = DAP_MSG_BATCH;
    l_e->conn_idx = a_conn_idx;
    l_e->next = NULL;
    l_e->batch = *a_t;
    if (a_q->tail) a_q->tail->next = l_e;
    else           a_q->head = l_e;
    a_q->tail = l_e;
    dap_slab_bits_set(a_q->mask, a_conn_idx);
    return true;
}

DAP_STATIC_INLINE bool
dap_defer_push_callback(dap_defer_queue_t *a_q, const dap_callback_task_t *a_t)
{
    dap_defer_entry_t *l_e = malloc(sizeof(*l_e));
    if (!l_e) return false;
    l_e->msg_type = DAP_MSG_CALLBACK;
    l_e->conn_idx = 0;
    l_e->next = NULL;
    l_e->callback = *a_t;
    if (a_q->tail) a_q->tail->next = l_e;
    else           a_q->head = l_e;
    a_q->tail = l_e;
    return true;
}

DAP_STATIC_INLINE bool
dap_defer_push_heap(dap_defer_queue_t *a_q, const dap_heap_task_t *a_t)
{
    dap_defer_entry_t *l_e = malloc(sizeof(*l_e));
    if (!l_e) return false;
    l_e->msg_type = DAP_MSG_HEAP;
    l_e->conn_idx = 0;
    l_e->next = NULL;
    l_e->heap = *a_t;
    if (a_q->tail) a_q->tail->next = l_e;
    else           a_q->head = l_e;
    a_q->tail = l_e;
    return true;
}

DAP_STATIC_INLINE bool
dap_defer_push_custom(dap_defer_queue_t *a_q, const dap_vmqueue_hdr_t *a_hdr)
{
    dap_vmqueue_hdr_t *l_copy = malloc(a_hdr->total_len);
    if (!l_copy) return false;
    memcpy(l_copy, a_hdr, a_hdr->total_len);
    dap_defer_entry_t *l_e = malloc(sizeof(*l_e));
    if (!l_e) { free(l_copy); return false; }
    l_e->msg_type = a_hdr->type;
    l_e->conn_idx = 0;
    l_e->next = NULL;
    l_e->custom_hdr = l_copy;
    if (a_q->tail) a_q->tail->next = l_e;
    else           a_q->head = l_e;
    a_q->tail = l_e;
    return true;
}
