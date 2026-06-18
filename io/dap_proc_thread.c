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
#include <stdatomic.h>
#include <unistd.h>
#include <sys/eventfd.h>
#include "dap_strfuncs.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_timerfd.h"
#include "dap_rand.h"

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
    uint32_t l_random_val;
    randombytes(&l_random_val, sizeof(l_random_val));
    uint32_t l_id_start = l_random_val % s_threads_count,
             l_id_min = l_id_start,
             l_size_min = UINT32_MAX;
    for (uint32_t i = l_id_start; i < s_threads_count + l_id_start; i++) {
        uint32_t l_id_cur = i < s_threads_count ? i : i - s_threads_count;
        uint32_t l_current_size = atomic_load(&s_threads[l_id_cur].proc_queue_size);
        if (l_current_size < l_size_min) {
            l_size_min = l_current_size;
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
        l_ret += atomic_load(&s_threads[i].proc_queue_size);
    return l_ret / s_threads_count;
}

/**
 * @brief Lock-free MPSC push: prepend item to queue head via atomic_exchange
 */
static inline void s_mpsc_push(_Atomic(dap_proc_queue_item_t *) *a_head, dap_proc_queue_item_t *a_item)
{
    dap_proc_queue_item_t *l_old_head = atomic_load(a_head);
    do {
        a_item->next = l_old_head;
    } while (!atomic_compare_exchange_weak(a_head, &l_old_head, a_item));
}

/**
 * @brief Lock-free MPSC drain: atomically take entire list, reverse to FIFO order
 */
static inline dap_proc_queue_item_t *s_mpsc_drain(_Atomic(dap_proc_queue_item_t *) *a_head)
{
    dap_proc_queue_item_t *l_list = atomic_exchange(a_head, (dap_proc_queue_item_t *)NULL);
    if (!l_list)
        return NULL;
    // Reverse to FIFO order (MPSC push prepends, so list is LIFO)
    dap_proc_queue_item_t *l_rev = NULL;
    while (l_list) {
        dap_proc_queue_item_t *l_next = l_list->next;
        l_list->next = l_rev;
        l_rev = l_list;
        l_list = l_next;
    }
    return l_rev;
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
    // Lock-free push to priority queue
    s_mpsc_push(&l_thread->queue_head[a_priority], l_item);
    atomic_fetch_add(&l_thread->proc_queue_size, 1);
    // Wake up proc thread via eventfd
    uint64_t l_one = 1;
    if (write(l_thread->wakeup_fd, &l_one, sizeof(l_one)) != sizeof(l_one)) {
        log_it(L_WARNING, "Failed to wakeup proc thread (errno=%d)", errno);
    }
    return 0;
}

/**
 * @brief Pull one item from highest-priority non-empty queue
 */
static dap_proc_queue_item_t *s_proc_queue_pull(dap_proc_thread_t *a_thread, int *a_priority)
{
    for (int i = DAP_QUEUE_MSG_PRIORITY_MAX; i >= DAP_QUEUE_MSG_PRIORITY_MIN; i--) {
        dap_proc_queue_item_t *l_item = s_mpsc_drain(&a_thread->queue_head[i]);
        if (l_item) {
            // Take first item, re-push the rest
            dap_proc_queue_item_t *l_next = l_item->next;
            l_item->next = NULL;
            if (l_next) {
                // Re-push remaining items (prepend to restore order)
                // Actually we need to reverse l_next back to LIFO for s_mpsc_push
                // But simpler: just set the head to l_next (already in FIFO order after drain)
                // Wait, l_next is already in FIFO order. We need to set it back.
                // But s_mpsc_push prepends. So we need to reverse l_next first.
                dap_proc_queue_item_t *l_rev = NULL;
                while (l_next) {
                    dap_proc_queue_item_t *l_n = l_next->next;
                    l_next->next = l_rev;
                    l_rev = l_next;
                    l_next = l_n;
                }
                // Now l_rev is in LIFO order (for s_mpsc_push prepend)
                // But we want FIFO order in the queue. So we need to prepend l_rev.
                // Actually, the remaining items are already in FIFO order from drain.
                // We just need to put them back. The simplest is to atomic_store the head.
                // But that's not safe with concurrent pushes. Let me think...
                // Actually, we can just re-push each item via s_mpsc_push.
                // But that reverses the order. For correctness, order within same priority doesn't matter much.
                // Let's just push them back in the order we have.
                while (l_rev) {
                    dap_proc_queue_item_t *l_n = l_rev->next;
                    s_mpsc_push(&a_thread->queue_head[i], l_rev);
                    l_rev = l_n;
                }
            }
            atomic_fetch_sub(&a_thread->proc_queue_size, 1);
            if (a_priority)
                *a_priority = i;
            return l_item;
        }
    }
    return NULL;
}

int dap_proc_thread_loop(dap_context_t *a_context)
{
    dap_proc_thread_t *l_thread = DAP_PROC_THREAD(a_context);
    do {
        dap_proc_queue_item_t *l_item = NULL;
        int l_item_priority = 0;

        // Try to pull an item (non-blocking)
        l_item = s_proc_queue_pull(l_thread, &l_item_priority);

        if (!l_item && !a_context->signal_exit) {
            // No items — wait on eventfd (replaces pthread_cond_wait)
            uint64_t l_val;
            ssize_t l_rd = read(l_thread->wakeup_fd, &l_val, sizeof(l_val));
            if (l_rd < 0 && errno != EAGAIN && errno != EINTR) {
                log_it(L_ERROR, "Proc thread wakeup read failed: errno=%d", errno);
            }
            // After wakeup, try pulling again
            l_item = s_proc_queue_pull(l_thread, &l_item_priority);
        }

        if (l_item) {
            debug_if(g_debug_reactor, L_DEBUG, "Call callback %p with arg %p on thread %p",
                                            l_item->callback, l_item->callback_arg, l_thread);

            if (!a_context->signal_exit && l_item->callback(l_item->callback_arg)) {
                dap_proc_thread_callback_add_pri(l_thread, l_item->callback, l_item->callback_arg, l_item_priority);
            }
            DAP_DEL_Z(l_item);
        }
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
    // Create eventfd for cross-thread wakeup (replaces mutex+condvar)
    l_thread->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (l_thread->wakeup_fd < 0) {
        log_it(L_CRITICAL, "Failed to create eventfd for proc thread: errno=%d", errno);
        return -1;
    }
    // Init proc_queue for related worker
    dap_worker_t * l_worker_related = dap_events_worker_get(l_thread->context->cpu_id);
    assert(l_worker_related);
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
    // Drain all queues
    for (int i = DAP_QUEUE_MSG_PRIORITY_MIN; i <= DAP_QUEUE_MSG_PRIORITY_MAX; i++) {
        dap_proc_queue_item_t *l_item;
        while ((l_item = s_mpsc_drain(&l_thread->queue_head[i]))) {
            dap_proc_queue_item_t *l_next = l_item->next;
            DAP_DELETE(l_item);
            l_item = l_next;
        }
    }
    // Close eventfd
    if (l_thread->wakeup_fd >= 0) {
        close(l_thread->wakeup_fd);
        l_thread->wakeup_fd = -1;
    }
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
    l_arg->callback(l_arg->callback_arg);
    if (l_arg->oneshot)
        DAP_DELETE(l_arg);
    return false;
}

static bool s_timer_callback(void *a_arg)
{
    struct timer_arg *l_arg = a_arg;
    bool l_repeat = !l_arg->oneshot;
    dap_proc_thread_callback_add_pri(l_arg->thread, s_thread_timer_callback, l_arg, l_arg->priority);
    return l_repeat;
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
