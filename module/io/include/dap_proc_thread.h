/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2020
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <pthread.h>
#include "dap_common.h"

typedef struct dap_proc_thread dap_proc_thread_t;
typedef struct dap_context dap_context_t;
/// Callback for processor. Returns TRUE for repeat
typedef bool (*dap_proc_queue_callback_t)(void *a_arg);
typedef void (*dap_thread_timer_callback_t)(void *a_arg);
/// Opaque handle of a proc-thread timer (confcall W53-F8) — use with dap_proc_thread_timer_cancel()
typedef struct timer_arg *dap_proc_thread_timer_t;

typedef enum dap_queue_msg_priority {
    DAP_QUEUE_MSG_PRIORITY_IDLE = 0,                                        /* Lowest priority (Idle). Don't use Idle until you sure what you do */
    DAP_QUEUE_MSG_PRIORITY_LOW,                                             /* Low priority */
    DAP_QUEUE_MSG_PRIORITY_NORMAL,                                          /* Default priority for any queue's entry, has assigned implicitly */
    DAP_QUEUE_MSG_PRIORITY_HIGH,                                            /* High priority */
    DAP_QUEUE_MSG_PRIORITY_CRITICAL,                                        /* Highest priority, critical for reaction time*/
    DAP_QUEUE_MSG_PRIORITY_COUNT                                            /* End-of-list marker */
} dap_queue_msg_priority_t;

#define DAP_QUEUE_MSG_PRIORITY_MIN DAP_QUEUE_MSG_PRIORITY_IDLE
#define DAP_QUEUE_MSG_PRIORITY_MAX DAP_QUEUE_MSG_PRIORITY_CRITICAL

typedef struct dap_proc_queue_item {
     dap_proc_queue_callback_t  callback;                                   /* An address of the action routine */
                          void *callback_arg;                               /* Address of the action routine argument */
     /* confcall W58-F7: optional owner-release for callback_arg, invoked
      * INSTEAD of callback when the item is discarded without running
      * (thread stop / module deinit).  Without it every owned arg still
      * queued at shutdown leaked. */
     void                      (*arg_free)(void *a_arg);
    struct dap_proc_queue_item *prev;
    struct dap_proc_queue_item *next;
} dap_proc_queue_item_t;

typedef struct dap_proc_thread {
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_event;
    dap_proc_queue_item_t *queue[DAP_QUEUE_MSG_PRIORITY_COUNT];
    uint64_t proc_queue_size;
    dap_context_t *context;
} dap_proc_thread_t;

#define DAP_PROC_THREAD(a) (dap_proc_thread_t *)((a)->_inheritor);

int dap_proc_thread_create(dap_proc_thread_t *a_thread, int a_cpu_id);
int dap_proc_thread_init(uint32_t a_threads_count);
void dap_proc_thread_deinit();
int dap_proc_thread_loop(dap_context_t *a_context);

#if defined(DAP_OS_WASM_ST)
int dap_proc_thread_init_wasm_st(uint32_t a_threads_count);
void dap_proc_thread_poll_step(void);
#endif

dap_proc_thread_t *dap_proc_thread_get(uint32_t a_thread_number);
dap_proc_thread_t *dap_proc_thread_get_auto();
int dap_proc_thread_callback_add_pri(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback, void *a_callback_arg, dap_queue_msg_priority_t a_priority);
/* confcall W58-F7: like _add_pri, plus an owner-release for the arg that runs
 * if the item is discarded without executing (thread stop / deinit) — and on
 * a failed post (return != 0) the caller's arg is released HERE too, so the
 * caller never has to special-case the error path. */
int dap_proc_thread_callback_add_pri_owned(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback,
                                           void *a_callback_arg, void (*a_arg_free)(void *),
                                           dap_queue_msg_priority_t a_priority);
DAP_STATIC_INLINE int dap_proc_thread_callback_add(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback, void *a_callback_arg)
{
    return dap_proc_thread_callback_add_pri(a_thread, a_callback, a_callback_arg, DAP_QUEUE_MSG_PRIORITY_NORMAL);
}
int dap_proc_thread_timer_add_pri(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg, uint64_t a_timeout_ms, bool a_oneshot, dap_queue_msg_priority_t a_priority);
/* confcall W53-F8: same as _add_pri but returns a cancel handle in *a_handle (may be NULL).
 * W54-F1: for oneshot timers *a_handle is set to NULL — the wrapper self-frees on fire. */
int dap_proc_thread_timer_add_pri_ex(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg,
                                     uint64_t a_timeout_ms, bool a_oneshot, dap_queue_msg_priority_t a_priority,
                                     dap_proc_thread_timer_t *a_handle);
/* Stop a repeating proc-thread timer.  Thread-safe; the handle is consumed.  After return
 * no NEW user callback starts, but one that already passed its cancelled check may still be
 * running on the proc thread — do not free a_callback_arg until it drained (see _cancel_then). */
void dap_proc_thread_timer_cancel(dap_proc_thread_timer_t a_handle);
/* W54-F2: cancel + drain.  Posts a_finalizer(a_arg) to the timer's own proc thread; the
 * per-thread FIFO guarantees it runs after any in-flight callback.  0 = posted (finalizer
 * owns a_arg); <0 = could not post (module deinit / OOM) — the timer is still cancelled and
 * the caller must run the finalizer itself. */
int dap_proc_thread_timer_cancel_then(dap_proc_thread_timer_t a_handle,
                                      dap_proc_queue_callback_t a_finalizer, void *a_arg);
DAP_STATIC_INLINE int dap_proc_thread_timer_add(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg, uint64_t a_timeout_ms)
{
    return dap_proc_thread_timer_add_pri(a_thread, a_callback, a_callback_arg, a_timeout_ms, false, DAP_QUEUE_MSG_PRIORITY_NORMAL);
}
DAP_STATIC_INLINE int dap_proc_thread_timer_add_ex(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg,
                                                   uint64_t a_timeout_ms, dap_proc_thread_timer_t *a_handle)
{
    return dap_proc_thread_timer_add_pri_ex(a_thread, a_callback, a_callback_arg, a_timeout_ms, false, DAP_QUEUE_MSG_PRIORITY_NORMAL, a_handle);
}
size_t dap_proc_thread_get_avg_queue_size();
uint32_t dap_proc_thread_get_count();
