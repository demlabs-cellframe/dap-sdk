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
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sched.h>
#include "dap_dl.h"
#include "dap_strfuncs.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_timerfd.h"

#define LOG_TAG "dap_proc_thread"

static uint32_t s_threads_count = 0;
static dap_proc_thread_t *s_threads = NULL;
static uint32_t *s_threads_api_refs = NULL;
static bool *s_threads_accepting = NULL;
static pthread_rwlock_t s_threads_state_lock = PTHREAD_RWLOCK_INITIALIZER;

static int s_context_callback_started(dap_context_t *a_context, void *a_arg);
static int s_context_callback_stopped(dap_context_t *a_context, void *a_arg);
static bool s_proc_thread_handle_resolve_index_unsafe(const dap_proc_thread_t *a_thread, size_t *a_index);
static bool s_proc_thread_handle_is_valid_unsafe(const dap_proc_thread_t *a_thread, size_t *a_index);
static bool s_proc_thread_handle_pin(dap_proc_thread_t *a_thread, size_t *a_index);
static void s_proc_thread_handle_unpin(size_t a_index);
static dap_proc_thread_t *s_proc_thread_get_auto_unsafe(size_t *a_index);
static bool s_proc_thread_get_auto_pinned(dap_proc_thread_t **a_thread, size_t *a_index, int *a_error);
static bool s_thread_timer_callback(void *a_arg);
static void s_timer_arg_unref(void *a_arg);
static inline uint64_t s_proc_queue_size_load(const dap_proc_thread_t *a_thread);
static inline void s_proc_queue_size_inc(dap_proc_thread_t *a_thread);
static inline void s_proc_queue_size_dec(dap_proc_thread_t *a_thread);

static inline uint64_t s_proc_queue_size_load(const dap_proc_thread_t *a_thread)
{
    return __atomic_load_n(&a_thread->proc_queue_size, __ATOMIC_RELAXED);
}

static inline void s_proc_queue_size_inc(dap_proc_thread_t *a_thread)
{
    (void)__atomic_add_fetch(&a_thread->proc_queue_size, 1, __ATOMIC_RELAXED);
}

static inline void s_proc_queue_size_dec(dap_proc_thread_t *a_thread)
{
    (void)__atomic_sub_fetch(&a_thread->proc_queue_size, 1, __ATOMIC_RELAXED);
}

static bool s_proc_thread_handle_resolve_index_unsafe(const dap_proc_thread_t *a_thread, size_t *a_index)
{
    if (!a_thread || !s_threads || !s_threads_count)
        return false;

    uintptr_t l_base = (uintptr_t)s_threads;
    size_t l_span = sizeof(*s_threads) * s_threads_count;
    uintptr_t l_ptr = (uintptr_t)a_thread;
    if (l_ptr < l_base || l_ptr >= l_base + l_span)
        return false;

    size_t l_offset = (size_t)(l_ptr - l_base);
    if (l_offset % sizeof(*s_threads))
        return false;

    size_t l_index = l_offset / sizeof(*s_threads);
    if (a_index)
        *a_index = l_index;
    return true;
}

static bool s_proc_thread_handle_is_valid_unsafe(const dap_proc_thread_t *a_thread, size_t *a_index)
{
    size_t l_index = 0;
    if (!s_proc_thread_handle_resolve_index_unsafe(a_thread, &l_index))
        return false;
    if (!s_threads_accepting || !s_threads_api_refs)
        return false;
    if (!s_threads_accepting[l_index] || !s_threads[l_index].context)
        return false;
    if (a_index)
        *a_index = l_index;
    return true;
}

static bool s_proc_thread_handle_pin(dap_proc_thread_t *a_thread, size_t *a_index)
{
    size_t l_index = 0;
    bool l_ret = false;

    pthread_rwlock_rdlock(&s_threads_state_lock);
    if (s_proc_thread_handle_is_valid_unsafe(a_thread, &l_index)) {
        (void)__atomic_add_fetch(&s_threads_api_refs[l_index], 1, __ATOMIC_ACQ_REL);
        l_ret = true;
    }
    pthread_rwlock_unlock(&s_threads_state_lock);

    if (l_ret && a_index)
        *a_index = l_index;
    return l_ret;
}

static void s_proc_thread_handle_unpin(size_t a_index)
{
    (void)__atomic_sub_fetch(&s_threads_api_refs[a_index], 1, __ATOMIC_ACQ_REL);
}

static dap_proc_thread_t *s_proc_thread_get_auto_unsafe(size_t *a_index)
{
    if (!s_threads || !s_threads_count)
        return NULL;

    uint32_t l_id_start = rand() % s_threads_count,
             l_id_min = l_id_start;
    uint64_t l_size_min = UINT64_MAX;
    bool l_found = false;

    for (uint32_t i = l_id_start; i < s_threads_count + l_id_start; i++) {
        uint32_t l_id_cur = i < s_threads_count ? i : i - s_threads_count;
        if (!s_threads_accepting || !s_threads_accepting[l_id_cur] || !s_threads[l_id_cur].context)
            continue;
        uint64_t l_size_cur = s_proc_queue_size_load(&s_threads[l_id_cur]);
        if (!l_found || l_size_cur < l_size_min) {
            l_size_min = l_size_cur;
            l_id_min = l_id_cur;
            l_found = true;
            if (!l_size_min)
                break;
        }
    }
    if (!l_found)
        return NULL;
    if (a_index)
        *a_index = l_id_min;
    return &s_threads[l_id_min];
}

static bool s_proc_thread_get_auto_pinned(dap_proc_thread_t **a_thread, size_t *a_index, int *a_error)
{
    dap_proc_thread_t *l_thread = NULL;
    size_t l_index = 0;
    int l_error = -3;
    bool l_ret = false;

    pthread_rwlock_rdlock(&s_threads_state_lock);
    l_thread = s_proc_thread_get_auto_unsafe(&l_index);
    if (l_thread && s_threads_api_refs) {
        (void)__atomic_add_fetch(&s_threads_api_refs[l_index], 1, __ATOMIC_ACQ_REL);
        l_error = 0;
        l_ret = true;
    } else if (s_threads && s_threads_count) {
        l_error = -4;
    }
    pthread_rwlock_unlock(&s_threads_state_lock);

    if (l_ret) {
        if (a_thread)
            *a_thread = l_thread;
        if (a_index)
            *a_index = l_index;
    }
    if (a_error)
        *a_error = l_error;
    return l_ret;
}

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
    if (!a_thread->context)
        return -2;
    a_thread->context->_inheritor = a_thread;
    int l_ret = dap_context_run(a_thread->context, a_cpu_id, DAP_CONTEXT_POLICY_TIMESHARING,
                                DAP_CONTEXT_PRIORITY_NORMAL, DAP_CONTEXT_FLAG_WAIT_FOR_STARTED | DAP_CONTEXT_FLAG_EXIT_IF_ERROR,
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
    uint32_t l_threads_count = a_threads_count ? a_threads_count : dap_get_cpu_count();
    if (!l_threads_count) {
        log_it(L_CRITICAL, "Unknown threads count");
        return -1;
    }
    dap_proc_thread_t *l_threads = DAP_NEW_Z_SIZE(dap_proc_thread_t, sizeof(dap_proc_thread_t) * l_threads_count);
    uint32_t *l_api_refs = DAP_NEW_Z_SIZE(uint32_t, sizeof(uint32_t) * l_threads_count);
    bool *l_accepting = DAP_NEW_Z_SIZE(bool, sizeof(bool) * l_threads_count);
    if (!l_threads || !l_api_refs || !l_accepting) {
        DAP_DEL_Z(l_threads);
        DAP_DEL_Z(l_api_refs);
        DAP_DEL_Z(l_accepting);
        log_it(L_CRITICAL, "Insufficient memory");
        return -2;
    }

    pthread_rwlock_wrlock(&s_threads_state_lock);
    s_threads = l_threads;
    s_threads_api_refs = l_api_refs;
    s_threads_accepting = l_accepting;
    s_threads_count = l_threads_count;
    pthread_rwlock_unlock(&s_threads_state_lock);

    int l_ret = 0;
    uint32_t i = 0;
    for (; i < s_threads_count && !l_ret; ++i) {
        l_ret = dap_proc_thread_create(s_threads + i, i);
        if (!l_ret)
            s_threads_accepting[i] = true;
    }
    if (l_ret) {
        log_it(L_ERROR, "Proc thread init failed at index %u with code %d, rollback", i ? i - 1 : 0, l_ret);
        dap_proc_thread_deinit();
    }
    return l_ret;
}

/**
 * @brief dap_proc_thread_deinit
 */
void dap_proc_thread_deinit()
{
    dap_proc_thread_t *l_threads = NULL;
    uint32_t *l_api_refs = NULL;
    bool *l_accepting = NULL;
    uint32_t l_threads_count = 0;

    pthread_rwlock_wrlock(&s_threads_state_lock);
    if (!s_threads || !s_threads_count) {
        pthread_rwlock_unlock(&s_threads_state_lock);
        return;
    }
    l_threads = s_threads;
    l_threads_count = s_threads_count;
    l_api_refs = s_threads_api_refs;
    l_accepting = s_threads_accepting;
    if (l_accepting) {
        for (uint32_t i = 0; i < l_threads_count; i++)
            l_accepting[i] = false;
    }
    pthread_rwlock_unlock(&s_threads_state_lock);

    if (l_api_refs) {
        bool l_busy = false;
        do {
            l_busy = false;
            for (uint32_t i = 0; i < l_threads_count; i++) {
                if (__atomic_load_n(&l_api_refs[i], __ATOMIC_ACQUIRE)) {
                    l_busy = true;
                    break;
                }
            }
            if (l_busy)
                sched_yield();
        } while (l_busy);
    }

    for (uint32_t i = l_threads_count; i--; ) {
        if (l_threads[i].context)
            dap_context_stop_n_kill(l_threads[i].context);
    }

    pthread_rwlock_wrlock(&s_threads_state_lock);
    DAP_DEL_Z(s_threads);
    DAP_DEL_Z(s_threads_api_refs);
    DAP_DEL_Z(s_threads_accepting);
    s_threads_count = 0;
    pthread_rwlock_unlock(&s_threads_state_lock);
}

/**
 * @brief dap_proc_thread_get
 * @param a_cpu_id
 * @return
 */
dap_proc_thread_t *dap_proc_thread_get(uint32_t a_cpu_id)
{
    dap_proc_thread_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_threads_state_lock);
    l_ret = (a_cpu_id < s_threads_count) ? &s_threads[a_cpu_id] : NULL;
    pthread_rwlock_unlock(&s_threads_state_lock);
    return l_ret;
}

/**
 * @brief dap_proc_thread_get_count
 * @return s_threads_count
 */
DAP_INLINE uint32_t dap_proc_thread_get_count()
{
    uint32_t l_count = 0;
    pthread_rwlock_rdlock(&s_threads_state_lock);
    l_count = s_threads_count;
    pthread_rwlock_unlock(&s_threads_state_lock);
    return l_count;
}

/**
 * @brief dap_proc_thread_get_auto
 * @return
 */
dap_proc_thread_t *dap_proc_thread_get_auto()
{
    dap_proc_thread_t *l_ret = NULL;
    pthread_rwlock_rdlock(&s_threads_state_lock);
    l_ret = s_proc_thread_get_auto_unsafe(NULL);
    pthread_rwlock_unlock(&s_threads_state_lock);
    return l_ret;
}

size_t dap_proc_thread_get_avg_queue_size()
{
    size_t l_ret = 0;
    pthread_rwlock_rdlock(&s_threads_state_lock);
    if (!s_threads || !s_threads_count) {
        pthread_rwlock_unlock(&s_threads_state_lock);
        return 0;
    }

    for (uint32_t i = 0; i < s_threads_count; i++)
        l_ret += s_proc_queue_size_load(&s_threads[i]);
    pthread_rwlock_unlock(&s_threads_state_lock);
    return l_ret / s_threads_count;
}

int dap_proc_thread_callback_add_pri(dap_proc_thread_t *a_thread, dap_proc_queue_callback_t a_callback,
                                     void *a_callback_arg, dap_queue_msg_priority_t a_priority)
{
    dap_return_val_if_fail(a_callback && a_priority >= DAP_QUEUE_MSG_PRIORITY_MIN && a_priority <= DAP_QUEUE_MSG_PRIORITY_MAX, -1);
    dap_proc_thread_t *l_thread = NULL;
    size_t l_thread_index = 0;
    int l_auto_error = -3;
    if (a_thread) {
        l_thread = a_thread;
        if (!s_proc_thread_handle_pin(l_thread, &l_thread_index)) {
            log_it(L_ERROR, "Can't add callback with invalid or stale processing thread handle %p", l_thread);
            return -4;
        }
    } else if (!s_proc_thread_get_auto_pinned(&l_thread, &l_thread_index, &l_auto_error)) {
        if (l_auto_error == -3)
            log_it(L_ERROR, "Can't add callback before dap_proc_thread_init()");
        else
            log_it(L_ERROR, "Can't add callback while processing threads are shutting down");
        return l_auto_error;
    }

    dap_proc_queue_item_t *l_item = DAP_NEW_Z(dap_proc_queue_item_t);
    if (!l_item) {
        s_proc_thread_handle_unpin(l_thread_index);
        log_it(L_CRITICAL, "Insufficient memory");
        return -2;
    }
    *l_item = (dap_proc_queue_item_t){ .callback = a_callback,
                                       .callback_arg = a_callback_arg };
    debug_if(g_debug_reactor, L_DEBUG, "Add callback %p with arg %p to thread %p", a_callback, a_callback_arg, l_thread);
    pthread_mutex_lock(&l_thread->queue_lock);
    dap_dl_append(l_thread->queue[a_priority], l_item);
    s_proc_queue_size_inc(l_thread);
    pthread_cond_signal(&l_thread->queue_event);
    pthread_mutex_unlock(&l_thread->queue_lock);
    s_proc_thread_handle_unpin(l_thread_index);
    return 0;
}

static dap_proc_queue_item_t *s_proc_queue_pull(dap_proc_thread_t *a_thread, int *a_priority)
{
    if (!s_proc_queue_size_load(a_thread))
        return NULL;
    dap_proc_queue_item_t *l_item = NULL;
    int i = DAP_QUEUE_MSG_PRIORITY_MAX;
    for (; !l_item && i >= 0; i--)
        if ((l_item = a_thread->queue[i]))
            break;
    if (l_item) {
        dap_dl_delete(a_thread->queue[i], l_item);
        s_proc_queue_size_dec(a_thread);
        if (a_priority)
            *a_priority = i;
    } else
        log_it(L_ERROR, "No item found in all piority levels of"
                        " message queue with size %"DAP_UINT64_FORMAT_U,
                                                 s_proc_queue_size_load(a_thread));
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
    dap_worker_proc_queue_input_store(l_worker_related, l_thread);
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
    uint32_t l_cpu_id = l_thread->context->cpu_id;
    log_it(L_ATT, "Stop processing thread #%u", l_cpu_id);
    // cleanup queue
    pthread_mutex_lock(&l_thread->queue_lock);
    while (s_proc_queue_size_load(l_thread)) {
        dap_proc_queue_item_t *l_item = s_proc_queue_pull(l_thread, NULL);
        if (!l_item)
            break;
        if (l_item->callback == s_thread_timer_callback)
            s_timer_arg_unref(l_item->callback_arg);
        DAP_DEL_Z(l_item);
    }
    pthread_cond_destroy(&l_thread->queue_event);
    pthread_mutex_unlock(&l_thread->queue_lock);
    pthread_mutex_destroy(&l_thread->queue_lock);
    dap_worker_t *l_worker_related = dap_events_worker_get(l_cpu_id);
    if (l_worker_related) {
        dap_proc_thread_t *l_expected = l_thread;
        (void)dap_worker_proc_queue_input_cas(l_worker_related, &l_expected, NULL);
    }
    l_thread->context = NULL;
    return 0;
}

struct timer_arg {
    dap_proc_thread_t *thread;
    dap_thread_timer_callback_t callback;
    void *callback_arg;
    bool oneshot;
    dap_queue_msg_priority_t priority;
    atomic_uint_fast32_t ref_count;
};

static void s_timer_arg_unref(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    if (!l_arg)
        return;
    if (atomic_fetch_sub_explicit(&l_arg->ref_count, 1, memory_order_acq_rel) == 1)
        DAP_DELETE(l_arg);
}

static void s_timer_es_delete_callback(dap_events_socket_t UNUSED_ARG *a_es, void *a_arg)
{
    s_timer_arg_unref(a_arg);
}

static bool s_thread_timer_callback(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    if (!l_arg)
        return false;
    l_arg->callback(l_arg->callback_arg);
    s_timer_arg_unref(l_arg);
    return false;
}

static bool s_timer_callback(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    if (!l_arg)
        return false;
    bool l_oneshot = l_arg->oneshot;
    atomic_fetch_add_explicit(&l_arg->ref_count, 1, memory_order_relaxed);
    // Repeat after exit, if not oneshot
    int l_rc = dap_proc_thread_callback_add_pri(l_arg->thread, s_thread_timer_callback, l_arg, l_arg->priority);
    if (l_rc)
        s_timer_arg_unref(l_arg);
    return !l_oneshot && !l_rc;
}

int dap_proc_thread_timer_add_pri(dap_proc_thread_t *a_thread, dap_thread_timer_callback_t a_callback, void *a_callback_arg, uint64_t a_timeout_ms, bool a_oneshot, dap_queue_msg_priority_t a_priority)
{
    dap_return_val_if_fail(a_callback && a_timeout_ms, -1);
    dap_proc_thread_t *l_thread = NULL;
    size_t l_thread_index = 0;
    int l_auto_error = -3;
    if (a_thread) {
        l_thread = a_thread;
        if (!s_proc_thread_handle_pin(l_thread, &l_thread_index)) {
            log_it(L_ERROR, "Can't add timer with invalid or stale processing thread handle %p", l_thread);
            return -4;
        }
    } else if (!s_proc_thread_get_auto_pinned(&l_thread, &l_thread_index, &l_auto_error)) {
        if (l_auto_error == -3)
            log_it(L_ERROR, "Can't add timer before dap_proc_thread_init()");
        else
            log_it(L_ERROR, "Can't add timer while processing threads are shutting down");
        return l_auto_error;
    }

    dap_worker_t *l_worker = dap_events_worker_get(l_thread->context->cpu_id);
    if (!l_worker) {
        s_proc_thread_handle_unpin(l_thread_index);
        log_it(L_CRITICAL, "Worker with ID corresonding to specified processing thread ID %u doesn't exists", l_thread->context->id);
        return -2;
    }
    struct timer_arg *l_timer_arg = DAP_NEW_Z(struct timer_arg);
    if (!l_timer_arg) {
        s_proc_thread_handle_unpin(l_thread_index);
        log_it(L_CRITICAL, "Insufficient memory");
        return -2;
    }
    l_timer_arg->thread = l_thread;
    l_timer_arg->callback = a_callback;
    l_timer_arg->callback_arg = a_callback_arg;
    l_timer_arg->oneshot = a_oneshot;
    l_timer_arg->priority = a_priority;
    atomic_init(&l_timer_arg->ref_count, 1);

    dap_timerfd_t *l_timerfd = dap_timerfd_start_on_worker(l_worker, a_timeout_ms, s_timer_callback, l_timer_arg);
    if (!l_timerfd) {
        s_timer_arg_unref(l_timer_arg);
        s_proc_thread_handle_unpin(l_thread_index);
        return -2;
    }
    l_timerfd->events_socket->callbacks.delete_callback = s_timer_es_delete_callback;
    l_timerfd->events_socket->callbacks.arg = l_timer_arg;
    s_proc_thread_handle_unpin(l_thread_index);
    return 0;
}
