/*
 * Authors:
 * Constantin Papizh <papizh.konstantin@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
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

/**
 * @file dap_wfq.h
 * @brief Advanced/internal — weighted fair queue (priority layout, post, wake, Treiber ext-stack).
 *
 * WFQ is a thin protocol layer on top of dap_vmqueue_mpsc_t.  It provides:
 *   - Lane selection via DAP_WFQ_PRI_LANE(pri, worker_id, N)
 *   - Post = push + wake  (dap_wfq_post)
 *   - Backpressure via Dekker futex handshake (slow path in s_vmq_push_wait_ex)
 *   - Treiber MPSC stack (dap_msg_stack_t) for external (non-worker) threads
 *
 * There is no dap_wfq_t struct — the MPSC queue (dap_vmqueue_mpsc_t) IS the
 * WFQ, and fairness is enforced at the processor level by draining lane
 * ranges with per-priority quotas (see DAP_WFQ_*_QUOTA).
 *
 * Lane index layout for N workers (3N total lanes):
 *
 *   lane:     0   1  ... N-1 | N  N+1 ... 2N-1 | 2N 2N+1 ... 3N-1
 *   tier:     ---- FAST ---- | ---- NORM ------ | ----- BG -------
 *   worker:    w0  w1    wN-1|  w0  w1     wN-1 |  w0  w1     wN-1
 *
 *   Formula:  lane = (PRI_MAX - pri) * N + worker_id
 *     FAST (pri=2):  lane = worker_id          [0   .. N-1 ]
 *     NORM (pri=1):  lane = N + worker_id      [N   .. 2N-1]
 *     BG   (pri=0):  lane = 2N + worker_id     [2N  .. 3N-1]
 *
 * Quota-based drain order (applied by the processor loop, not this header):
 *
 *   per cycle:  FAST [0..N-1]    up to FAST_QUOTA msgs/lane
 *               NORM [N..2N-1]   up to NORM_QUOTA msgs/lane
 *               BG   [2N..3N-1]  up to BG_QUOTA   msgs/lane
 *               ext-stack        up to EXT_DRAIN_QUOTA msgs
 *
 * External (non-worker) threads use the Treiber MPSC message stack
 * (dap_msg_stack_t) instead of SPSC lanes — see dap_msg_stack_push().
 */
#pragma once

#include "dap_io_queue_core.h"

/* ================================================================== */
/*  Priority / lane layout                                             */
/* ================================================================== */

#define DAP_WFQ_PRI_COUNT 3
#define DAP_WFQ_PRI_MAX   2

enum {
    DAP_WFQ_PRI_BG   = 0, /* background: timer ticks, keepalive, housekeeping */
    DAP_WFQ_PRI_NORM = 1, /* normal: frame batches from recv path */
    DAP_WFQ_PRI_FAST = 2  /* expedited: reserved for latency-critical messages */
};

/** @brief Map (priority, worker id, worker count) to an absolute lane index.
 *
 *  Layout for N workers:
 *    lane 0..N-1               — FAST  (pri=2)
 *    lane N..2N-1              — NORM  (pri=1)
 *    lane 2N..3N-1             — BG    (pri=0)
 *
 *  External (non-worker) threads use the Treiber ext-stack instead of lanes.
 */
#define DAP_WFQ_PRI_LANE(a_pri, a_wid, a_nw) \
    ((unsigned)((DAP_WFQ_PRI_MAX - (a_pri)) * (a_nw) + (a_wid)))

/* Per-priority drain quotas — consumed by the processor drain loop.
   Override before including this header to tune scheduling weights. */
#ifndef DAP_WFQ_FAST_QUOTA
#  define DAP_WFQ_FAST_QUOTA  32
#endif
#ifndef DAP_WFQ_NORM_QUOTA
#  define DAP_WFQ_NORM_QUOTA  16
#endif
#ifndef DAP_WFQ_BG_QUOTA
#  define DAP_WFQ_BG_QUOTA     8
#endif

/* Default per-lane buffer capacities (overridable before include).
   When a lane fills, the producer enters the Dekker backpressure path
   (pw=1, futex_wait) until the consumer drains and wakes it. */
#ifndef DAP_WFQ_CAP_FAST
#  define DAP_WFQ_CAP_FAST  (64  * 1024)
#endif
#ifndef DAP_WFQ_CAP_NORM
#  define DAP_WFQ_CAP_NORM  (256 * 1024)
#endif
#ifndef DAP_WFQ_CAP_BG
#  define DAP_WFQ_CAP_BG    (64  * 1024)
#endif

/** @brief Per-WFQ wait state for the RESCAN protocol.
 *
 *  rescan_mask: bitmap indexed by worker_id.  When a worker cannot push
 *  to its WFQ lane (lane full), it ORs its bit into rescan_mask (release)
 *  and notifies the processor via MPSC notify.  After each drain cycle
 *  the processor atomically exchanges rescan_mask → 0 (acquire) and
 *  kicks the set of workers whose bits were set, allowing them to retry.
 *  See dap_proc_kick_rescan_workers(). */
typedef struct dap_wfq_wait_state {
    _Atomic(uint64_t) rescan_mask;
} dap_wfq_wait_state_t;

/* Forward decl — ext push calls dap_wfq_wake which is defined below. */
DAP_STATIC_INLINE void dap_wfq_wake(dap_vmqueue_mpsc_t *a_wfq);

/* ================================================================== */
/*  Message stack — Treiber MPSC over dap_msg_t nodes                  */
/* ================================================================== */

typedef struct dap_msg_stack {
    _Atomic(dap_msg_t *) top;
} dap_msg_stack_t;

#define DAP_MSG_STACK_INIT { .top = NULL }

/* Max messages drained from the ext-stack per processor pass */
#ifndef DAP_EXT_DRAIN_QUOTA
#  define DAP_EXT_DRAIN_QUOTA 16
#endif

/** @brief CAS-push a prepared message node (any thread). */
DAP_STATIC_INLINE void
dap_msg_stack_push(dap_msg_stack_t *a_stack, dap_msg_t *a_msg)
{
    dap_msg_t *l_old = atomic_load_explicit(&a_stack->top, memory_order_relaxed);
    do {
        a_msg->next = l_old;
    } while (!atomic_compare_exchange_weak_explicit(
                &a_stack->top, &l_old, a_msg,
                memory_order_release, memory_order_acquire));
}

/** @brief Detach the entire chain atomically (processor only). */
DAP_STATIC_INLINE dap_msg_t *dap_msg_stack_detach(dap_msg_stack_t *a_stack)
{
    return atomic_exchange_explicit(&a_stack->top, NULL, memory_order_acquire);
}

/** @brief Reverse a detached chain for FIFO order. */
DAP_STATIC_INLINE dap_msg_t *dap_msg_list_reverse(dap_msg_t *a_head)
{
    dap_msg_t *l_prev = NULL;
    while (a_head) {
        dap_msg_t *l_next = a_head->next;
        a_head->next = l_prev;
        l_prev = a_head;
        a_head = l_next;
    }
    return l_prev;
}

/* ================================================================== */
/*  Processor wakeup                                                   */
/* ================================================================== */

/** @brief Wake the processor via notify_latch protocol.
 *
 *  Delegates to dap_vmqueue_mpsc_notify: exchange(1, release),
 *  futex_wake only on 0→1 transition. */
DAP_STATIC_INLINE void dap_wfq_wake(dap_vmqueue_mpsc_t *a_wfq)
{
    if (!a_wfq)
        return;
    dap_vmqueue_mpsc_notify(a_wfq);
}


/* ================================================================== */
/*  Post: push + wake + backpressure (the canonical send protocol)     */
/* ================================================================== */

/** @brief Wake trampoline: type-safe cast for dap_wfq_wake via dap_vmq_wake_fn. */
DAP_STATIC_INLINE void s_wfq_wake_cb(void *a_arg)
{
    dap_wfq_wake((dap_vmqueue_mpsc_t *)a_arg);
}

/**
 * @brief Push a message into a WFQ lane and wake the processor.
 *
 * Fast path: non-blocking s_vmq_push into the target lane, then
 *            dap_wfq_wake (notify_latch exchange + futex_wake on 0->1).
 * Slow path: s_vmq_push_wait_ex — Dekker backpressure (pw=1, wake
 *            consumer, retry push, futex_wait until space is freed).
 * The drain side guarantees ACK on every pass (commit_head or ack_waiter),
 * so the producer is never stuck indefinitely (barring shutdown).
 *
 * @return true if the message was queued, false on shutdown / capacity error.
 */
DAP_STATIC_INLINE bool
dap_wfq_post(dap_vmqueue_mpsc_t *a_wfq, unsigned a_lane,
             uint8_t a_type, uint8_t a_pri,
             const void *a_payload, uint32_t a_size)
{
    if (__builtin_expect(a_lane >= a_wfq->n_lanes, 0))
        return false;
    _Atomic(uint64_t) *l_tg = s_mpsc_tg(a_wfq, a_lane);
    _Atomic(uint64_t) *l_hg = s_mpsc_hg(a_wfq, a_lane);
    char   *l_data = s_mpsc_data(a_wfq, a_lane);
    size_t  l_cap  = s_mpsc_cap(a_wfq, a_lane);

    if (s_vmq_push(l_tg, l_hg, l_data, l_cap, a_type, a_pri, a_payload, a_size)) {
        dap_wfq_wake(a_wfq);  /* fast path succeeded — notify processor */
        return true;
    }
    /* slow path: Dekker backpressure — pw=1, wake consumer, retry, futex_wait */
    return s_vmq_push_wait_ex(l_tg, l_hg, s_mpsc_pw(a_wfq, a_lane),
                               &a_wfq->shutdown,
                               l_data, l_cap, a_type, a_pri, a_payload, a_size,
                               s_wfq_wake_cb, (void *)a_wfq);
}

/** @brief Estimate unread bytes across lanes [@a a_base, @a a_base + @a a_count).
 *  Useful for diagnostics, load monitoring, and test assertions. */
DAP_STATIC_INLINE size_t
dap_wfq_pending(dap_vmqueue_mpsc_t *a_q, unsigned a_base, unsigned a_count)
{
    size_t l_total = 0;
    for (unsigned i = a_base; i < a_base + a_count; ++i)
        l_total += s_mpsc_lane_pending(a_q, i);
    return l_total;
}

/* ================================================================== */
/*  Standard WFQ layout: FAST + NORM + BG (3N lanes for N workers)     */
/* ================================================================== */

/**
 * @brief Create MPSC with standard lane layout for a_nw workers (3N lanes).
 */
DAP_STATIC_INLINE dap_vmqueue_mpsc_t *
dap_wfq_create_standard(unsigned a_nw, size_t a_cap_fast, size_t a_cap_norm,
                          size_t a_cap_bg)
{
    unsigned l_total = DAP_WFQ_PRI_COUNT * a_nw;
    size_t *l_caps = (size_t *)calloc(l_total, sizeof(size_t));
    if (!l_caps) return NULL;
    for (unsigned i = 0; i < a_nw; i++) {
        l_caps[i]              = a_cap_fast;
        l_caps[a_nw + i]       = a_cap_norm;
        l_caps[2 * a_nw + i]   = a_cap_bg;
    }
    dap_vmqueue_mpsc_t *l_q = dap_vmqueue_mpsc_create(l_total, l_caps);
    free(l_caps);
    return l_q;
}

/* ================================================================== */
/*  Typed WFQ post wrappers                                            */
/* ================================================================== */

/** @brief Post a timer creation request via WFQ.  Returns a self-routing handle
 *  (0 on failure).  The caller provides proc_idx and worker_slot so the handle
 *  encodes the owner for later cross-thread cancel. */
DAP_STATIC_INLINE dap_timer_handle_t
dap_wfq_post_timer(dap_vmqueue_mpsc_t *a_wfq, unsigned a_lane,
                    uint8_t a_proc_idx, uint8_t a_worker_slot,
                    uint64_t a_delay_us, uint64_t a_interval_us,
                    uint32_t a_iterations,
                    dap_timer_cb_t a_exec, void *a_arg)
{
    uint64_t l_lid = dap_timer_gen_local_id();
    dap_timer_handle_t l_h = dap_timer_make_handle(a_proc_idx, a_worker_slot, l_lid);
    dap_timer_request_t l_t = {
        .delay    = { .tv_sec = (time_t)(a_delay_us / 1000000ULL),
                      .tv_nsec = (long)((a_delay_us % 1000000ULL) * 1000L) },
        .interval = { .tv_sec = (time_t)(a_interval_us / 1000000ULL),
                      .tv_nsec = (long)((a_interval_us % 1000000ULL) * 1000L) },
        .iterations = a_iterations,
        .exec       = a_exec,
        .arg        = a_arg,
        .id         = l_lid
    };
    bool l_ok = dap_wfq_post(a_wfq, a_lane,
                              DAP_MSG_TIMER, DAP_WFQ_PRI_NORM, &l_t, sizeof(l_t));
    return l_ok ? l_h : DAP_TIMER_HANDLE_NULL;
}

/** @brief Post a timer-cancel callback with DAP_WFQ_PRI_FAST priority.
 *
 *  The lane is caller-determined (typically the FAST-tier lane for the timer
 *  owner, computed via DAP_WFQ_PRI_LANE(DAP_WFQ_PRI_FAST, wid, N)).
 *  The cancel runs asynchronously on the owner thread that drains this WFQ.
 *  Allocates a heap dap_timer_cancel_ctx_t — freed by dap_timer_cancel_fn.
 *  @param a_tl  Pointer to the owner-thread's dap_timers_t (must outlive delivery).
 *  @return true if posted, false on allocation failure or backpressure. */
DAP_STATIC_INLINE bool
dap_wfq_post_timer_cancel(dap_vmqueue_mpsc_t *a_wfq, unsigned a_lane,
                           dap_timers_t *a_tl, dap_timer_handle_t a_h)
{
    dap_timer_cancel_ctx_t *l_ctx =
        (dap_timer_cancel_ctx_t *)malloc(sizeof(dap_timer_cancel_ctx_t));
    if (!l_ctx) return false;
    l_ctx->tl = a_tl;
    l_ctx->h  = a_h;
    dap_callback_task_t l_t = { .fn = dap_timer_cancel_fn, .arg = l_ctx };
    bool l_ok = dap_wfq_post(a_wfq, a_lane,
                              DAP_MSG_CALLBACK, DAP_WFQ_PRI_FAST,
                              &l_t, sizeof(l_t));
    if (!l_ok)
        free(l_ctx);
    return l_ok;
}
