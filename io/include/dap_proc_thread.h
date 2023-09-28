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
#include "dap_context.h"
#include "dap_list.h"                                                       /* Simple List routines */
#include "dap_proc_thread.h"

typedef bool (*dap_proc_queue_callback_t)(struct dap_proc_queue_t *, void *);// Callback for processor. Returns true if
                                                                            // we want to stop callback execution and
                                                                            // not to go on next loop
enum dap_queue_msg_priority {
    DAP_QUEUE_MSG_PRIORITY_0 = 0,                                           /* Lowest priority (Idle)  */
    DAP_QUEUE_MSG_PRIORITY_IDLE = DAP_QUEUE_MSG_PRIORITY_0,                 /* Don't use Idle if u are not sure that understand how it works */

    DAP_QUEUE_MSG_PRIORITY_1 = 1,                                           /* Low priority */
    DAP_QUEUE_MSG_PRIORITY_LOW = DAP_QUEUE_MSG_PRIORITY_1,

    DAP_QUEUE_MSG_PRIORITY_2 = 2,
    DAP_QUEUE_MSG_PRIORITY_NORMAL = DAP_QUEUE_MSG_PRIORITY_2,               /* Default priority for any queue's entry, has assigned implicitly */

    DAP_QUEUE_MSG_PRIORITY_3 = 3,                                           /* Higest priority */
    DAP_QUEUE_MSG_PRIORITY_HIGH = DAP_QUEUE_MSG_PRIORITY_3,

    DAP_QUEUE_MSG_PRIORITY_COUNT = 4                                          /* End-of-list marker */
};

typedef struct dap_proc_queue_item {
     dap_proc_queue_callback_t  callback;                                   /* An address of the action routine */
                          void *callback_arg;                               /* Address of the action routine argument */
    struct dap_proc_queue_item *prev;
    struct dap_proc_queue_item *next;
} dap_proc_queue_item_t;

typedef struct dap_proc_thread {
    pthread_mutex_t queue_lock;                                             /* To coordinate access to the queuee's entries */
    pthread_cond_t queue_event;                                             /* Conditional variable for waiting thread event queue */
    dap_proc_queue_item_t *queue[DAP_QUEUE_MSG_PRIORITY_COUNT];               /* List of the queue' entries in array of list according of priority numbers */
    uint64_t proc_queue_size;                                               /* Thread's load factor */
    dap_context_t *context;
} dap_proc_thread_t;

#define DAP_PROC_THREAD(a) (dap_proc_thread_t *)((a)->_inheritor);

int dap_proc_thread_init(uint32_t a_threads_count);
void dap_proc_thread_deinit();

dap_proc_thread_t *dap_proc_thread_get(uint32_t a_thread_number);
dap_proc_thread_t *dap_proc_thread_get_auto();

int dap_proc_thread_add_callback_pri(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback, void *a_callback_arg, enum dap_queue_msg_priority a_priority);
DAP_STATIC_INLINE dap_proc_thread_add_callback(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback, void *a_callback_arg)
{
    return dap_proc_thread_add_callback_pri(a_thread, a_callback, a_callback_arg, DAP_QUEUE_MSG_PRIORITY_NORMAL);
}
