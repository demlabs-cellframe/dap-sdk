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
#include <errno.h>
#include <pthread.h>
#include <utlist.h>
#include "dap_strfuncs.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_timerfd.h"

#define LOG_TAG "dap_proc_thread"

static uint32_t s_threads_count = 0;
static dap_proc_thread_t *s_threads = NULL;

static int s_context_callback_started(dap_context_t *a_context, void *a_arg);
static int s_context_callback_stopped(dap_context_t *a_context, void *a_arg);

/**
 * @brief add and run context to thread
 * @param a_thread alocated thread memory
 * @param a_cpu_id cpu id to thread assign
 * @return result of dap_context_run (0 all OK)
 */

int dap_proc_thread_create(dap_proc_thread_t *a_thread, int a_cpu_id)
{
    dap_return_val_if_pass(!a_thread || a_thread->context, -1);

    a_thread->context = dap_context_new(DAP_CONTEXT_TYPE_PROC_THREAD);
    a_thread->context->_inheritor = a_thread;
    int l_ret = dap_context_run(a_thread->context, a_cpu_id, DAP_CONTEXT_POLICY_TIMESHARING,
                                DAP_CONTEXT_PRIORITY_NORMAL, DAP_CONTEXT_FLAG_WAIT_FOR_STARTED,
                                s_context_callback_started, s_context_callback_stopped, a_thread);
    if (l_ret) {
        log_it(L_CRITICAL, "Create thread failed with code %d", l_ret);
    }
    return l_ret;
}

/**
 * @brief dap_proc_thread_init
 * @param a_threads_count 0 means autodetect
 * @return
 */

int dap_proc_thread_init(uint32_t a_threads_count)
{
    if (!(s_threads_count = a_threads_count ? a_threads_count : dap_get_cpu_count())) {
        log_it(L_CRITICAL, "Unknown threads count");
        return -1;
    }
    s_threads = DAP_NEW_Z_SIZE(dap_proc_thread_t, sizeof(dap_proc_thread_t) * s_threads_count);
    int l_ret = 0;
    for (uint32_t i = 0; i < s_threads_count && !l_ret; ++i) {
        l_ret = dap_proc_thread_create(s_threads + i, i);
    }
    return l_ret;
}

/**
 * @brief dap_proc_thread_deinit
 */
void dap_proc_thread_deinit()
{
    for (uint32_t i = s_threads_count; i--; )
        dap_context_stop_n_kill(s_threads[i].context);
    DAP_DEL_Z(s_threads);
}

/**
 * @brief dap_proc_thread_get
 * @param a_cpu_id
 * @return
 */
dap_proc_thread_t *dap_proc_thread_get(uint32_t a_cpu_id)
{
    return (a_cpu_id < s_threads_count) ? &s_threads[a_cpu_id] : NULL;
}

/**
 * @brief dap_proc_thread_get_count
 * @return s_threads_count
 */
DAP_INLINE uint32_t dap_proc_thread_get_count()
{
    return s_threads_count;
}

/**
 * @brief dap_proc_thread_get_auto
 * @return
 */
dap_proc_thread_t *dap_proc_thread_get_auto()
{
    uint32_t l_id_start = rand() % s_threads_count,
             l_id_min = l_id_start,
             l_size_min = UINT32_MAX;
    for (uint32_t i = l_id_start; i < s_threads_count + l_id_start; i++) {
        uint32_t l_id_cur = i < s_threads_count ? i : i - s_threads_count;
        if (s_threads[l_id_cur].proc_queue_size < l_size_min) {
            l_size_min = s_threads[l_id_cur].proc_queue_size;
            l_id_min = l_id_cur;
            if (!l_size_min)
                break;
        }
    }
    return &s_threads[l_id_min];
}

size_t dap_proc_thread_get_avg_queue_size()
{
    size_t l_ret = 0;
    for (uint32_t i = 0; i < s_threads_count; i++)
        l_ret += s_threads[i].proc_queue_size;
    return l_ret / s_threads_count;
}

int dap_proc_thread_callback_add_pri(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback,
                                     void *a_callback_arg, dap_queue_msg_priority_t a_priority)
{
    dap_return_val_if_fail(a_callback && a_priority >= DAP_QUEUE_MSG_PRIORITY_MIN && a_priority <= DAP_QUEUE_MSG_PRIORITY_MAX, -1);
    dap_proc_thread_t *l_thread = a_thread ? a_thread : dap_proc_thread_get_auto();
    dap_proc_queue_item_t *l_item = DAP_NEW_Z(dap_proc_queue_item_t);
    if (!l_item) {
        log_it(L_CRITICAL, "Insufficient memory");
        return -2;
    }
    *l_item = (dap_proc_queue_item_t){ .callback = a_callback,
                                       .callback_arg = a_callback_arg };
    debug_if(g_debug_reactor, L_DEBUG, "Add callback %p with arg %p to thread %p", a_callback, a_callback_arg, l_thread);
    pthread_mutex_lock(&l_thread->queue_lock);
    DL_APPEND(l_thread->queue[a_priority], l_item);
    l_thread->proc_queue_size++;
    pthread_cond_signal(&l_thread->queue_event);
    pthread_mutex_unlock(&l_thread->queue_lock);
    return 0;
}

static dap_proc_queue_item_t *s_proc_queue_pull(dap_proc_thread_t *a_thread, int *a_priority)
{
    if (!a_thread->proc_queue_size)
        return NULL;
    dap_proc_queue_item_t *l_item = NULL;
    int i = DAP_QUEUE_MSG_PRIORITY_MAX;
    for (; !l_item && i >= 0; i--)
        if ((l_item = a_thread->queue[i]))
            break;
    if (l_item) {
        DL_DELETE(a_thread->queue[i], l_item);
        a_thread->proc_queue_size--;
        if (a_priority)
            *a_priority = i;
    } else
        log_it(L_ERROR, "No item found in all piority levels of"
                        " message queue with size %"DAP_UINT64_FORMAT_U,
                                                 a_thread->proc_queue_size);
    return l_item;
}

int dap_proc_thread_loop(dap_context_t *a_context)
{
    dap_proc_thread_t *l_thread = DAP_PROC_THREAD(a_context);
    do {
        pthread_mutex_lock(&l_thread->queue_lock);
        dap_proc_queue_item_t *l_item = NULL;
        int l_item_priority = 0;
        while (!a_context->signal_exit &&
               !(l_item = s_proc_queue_pull(l_thread, &l_item_priority)))
            pthread_cond_wait(&l_thread->queue_event, &l_thread->queue_lock);
        pthread_mutex_unlock(&l_thread->queue_lock);
        if (l_item)
            debug_if(g_debug_reactor, L_DEBUG, "Call callback %p with arg %p on thread %p",
                                            l_item->callback, l_item->callback_arg, l_thread);
        if (!a_context->signal_exit &&
                l_item->callback(l_item->callback_arg))
            dap_proc_thread_callback_add_pri(l_thread, l_item->callback, l_item->callback_arg, l_item_priority);
        DAP_DEL_Z(l_item);
    } while (!a_context->signal_exit);
    return 0;
}

/**
 * @brief s_context_callback_started
 * @param a_context
 * @param a_arg
 */
static int s_context_callback_started(dap_context_t UNUSED_ARG *a_context, void *a_arg)
{
    dap_proc_thread_t *l_thread = a_arg;
    assert(l_thread);
    pthread_mutex_init(&l_thread->queue_lock, NULL);
    pthread_cond_init(&l_thread->queue_event, NULL);
    // Init proc_queue for related worker
    dap_worker_t * l_worker_related = dap_events_worker_get(l_thread->context->cpu_id);
    if (!l_worker_related) {
        log_it(L_ERROR, "s_context_callback_started(): Cannot get worker for CPU ID %u", l_thread->context->cpu_id);
        return -1;
    }
    l_worker_related->proc_queue_input = l_thread;
    return 0;
}

/**
 * @brief s_context_callback_stopped
 * @param a_context
 * @param a_arg
 */
static int s_context_callback_stopped(dap_context_t UNUSED_ARG *a_context, void *a_arg)
{
    dap_proc_thread_t *l_thread = a_arg;
    assert(l_thread);
    log_it(L_ATT, "Stop processing thread #%u", l_thread->context->cpu_id);
    // cleanup queue
    pthread_mutex_lock(&l_thread->queue_lock);
    while (l_thread->proc_queue_size)
        if (!s_proc_queue_pull(l_thread, NULL))
            break;
    pthread_cond_destroy(&l_thread->queue_event);
    pthread_mutex_unlock(&l_thread->queue_lock);
    pthread_mutex_destroy(&l_thread->queue_lock);
    return 0;
}

struct timer_arg {
    dap_proc_thread_t *thread;
    dap_thread_timer_callback_t callback;
    void *callback_arg;
    bool oneshot;
    dap_queue_msg_priority_t priority;
};

static bool s_thread_timer_callback(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    return l_arg->callback(l_arg->callback_arg), false;
}

static bool s_timer_callback(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    // Repeat after exit, if not oneshot
    return dap_proc_thread_callback_add_pri(l_arg->thread, s_thread_timer_callback, l_arg, l_arg->priority), !l_arg->oneshot;
}

int dap_proc_thread_timer_add_pri(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg, uint64_t a_timeout_ms, bool a_oneshot, dap_queue_msg_priority_t a_priority)
{
    dap_return_val_if_fail(a_callback && a_timeout_ms, -1);
    dap_proc_thread_t *l_thread = a_thread ? a_thread : dap_proc_thread_get_auto();
    dap_worker_t *l_worker = dap_events_worker_get(l_thread->context->cpu_id);
    if (!l_worker) {
        log_it(L_CRITICAL, "Worker with ID corresonding to specified processing thread ID %u doesn't exists", l_thread->context->id);
        return -2;
    }
    struct timer_arg *l_timer_arg = DAP_NEW_Z(struct timer_arg);
    *l_timer_arg = (struct timer_arg){  .thread = l_thread, .callback = a_callback,
                                        .callback_arg = a_callback_arg,
                                        .oneshot = a_oneshot, .priority = a_priority };
    dap_timerfd_start_on_worker(l_worker, a_timeout_ms, s_timer_callback, l_timer_arg);
    return 0;
}
