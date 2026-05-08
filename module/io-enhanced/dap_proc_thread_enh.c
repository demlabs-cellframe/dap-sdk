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
 * @file dap_proc_thread_enh.c
 * @brief Processor main loop and cold-path helpers.
 *
 * The hot part of the processor API — the per-lane drain, the
 * dispatch switch, pending harvest — stays inline in
 * dap_proc_dispatch.h (included only from this TU).  What lives here is:
 *
 *   - the main loop (dap_proc_loop_run), invoked once per processor
 *     thread for the process lifetime;
 *   - the defer-queue retry pass (s_defer_drain_generic);
 *   - the rescan-worker kick;
 *   - the shutdown helper;
 *   - the TLS anchor for dap_tls_proc.
 *
 * None of these are hot enough for call overhead to be measurable
 * against the work they do (defer walk, futex wait, eventfd writes).
 */

#include "dap_proc.h"
#include "dap_proc_dispatch.h"

/* TLS identity of the processor whose main loop this thread is running.
 *
 * Set at dap_proc_loop_run entry, cleared on return.  Read by ext-stack
 * message handlers (dap_proc_msg.h, user-defined DAP_MSG_TYPE handlers)
 * and timer callbacks that need the owning context without a void*
 * cast.  NULL outside a processor thread. */
DAP_THREADLOCAL dap_proc_ctx_t *dap_tls_proc = NULL;

/* ================================================================== */
/*  Internal helpers (file-local)                                      */
/* ================================================================== */

/** @brief Run due timers and bump the stats counter. */
static void s_proc_drain_timers(dap_proc_ctx_t *a_ctx)
{
    uint32_t l_n = dap_timers_drain(&a_ctx->timers);
    dap_stat(a_ctx->stats, timer_fires, += l_n);
}

/** @brief Kick workers whose non-blocking push failed (RESCAN).
 *  Called after the processor drain frees lane space so those workers
 *  can retry their pending batch_push. */
static void s_proc_kick_rescan_workers(dap_proc_ctx_t *a_ctx)
{
    uint64_t l_mask = atomic_exchange_explicit(
        &a_ctx->wfq_waiting->rescan_mask, 0, memory_order_acquire);
    while (l_mask) {
        unsigned l_bit = (unsigned)__builtin_ctzll(l_mask);
        l_mask &= l_mask - 1;
#ifdef DAP_OS_WINDOWS
        dap_conn_kick(a_ctx->worker_kicks[l_bit]);
#else
        dap_conn_kick(a_ctx->worker_kick_fds[l_bit]);
#endif
    }
}

/**
 * @brief Generic defer drain: retries all deferred entries regardless of type.
 *
 * BATCH: slab resolve + generation check + exec_batch retry.
 * CALLBACK: re-invoke callback_cb, call fn on DONE.
 * HEAP: re-invoke heap_cb, call cleanup on DONE/DROP.
 * CUSTOM: re-invoke custom_cb with the saved header copy; free copy on resolution.
 * Entries that remain DEFER are kept; others are freed.
 */
static bool
s_defer_drain_generic(dap_defer_queue_t *a_q, dap_proc_ctx_t *a_ctx)
{
    if (!a_q->head) return false;
    dap_defer_entry_t *l_kept = NULL, *l_tail = NULL;
    bool l_progress = false;
    _Atomic uint64_t l_mask[DAP_SLAB_BITS_WORDS];
    memset(l_mask, 0, sizeof(l_mask));

    for (dap_defer_entry_t *l_e = a_q->head; l_e; ) {
        dap_defer_entry_t *l_next = l_e->next;
        dap_msg_rc_t l_rc = DAP_MSG_DROP;

        switch (l_e->msg_type) {
        case DAP_MSG_BATCH: {
            if (dap_slab_bits_test(l_mask, l_e->conn_idx)) {
                l_rc = DAP_MSG_DEFER;
                break;
            }
            if (a_ctx->batch_cb) {
                l_rc = a_ctx->batch_cb(&l_e->batch, a_ctx->_inheritor);
            } else if (a_ctx->frame_rc_cb && a_ctx->slab) {
                l_rc = dap_proc_exec_batch_rc(
                    &l_e->batch, a_ctx->force_complete,
                    a_ctx->frame_rc_cb, a_ctx->_inheritor);
            } else if (a_ctx->frame_cb && a_ctx->slab) {
                dap_proc_exec_result_t l_r = dap_proc_exec_batch(
                    &l_e->batch, a_ctx->force_complete,
                    a_ctx->frame_cb, a_ctx->_inheritor);
                switch (l_r) {
                case DAP_PROC_EXEC_OK:       l_rc = DAP_MSG_DONE; break;
                case DAP_PROC_EXEC_DEFERRED: l_rc = DAP_MSG_DEFER; break;
                case DAP_PROC_EXEC_STALE:    l_rc = DAP_MSG_DROP; break;
                }
            }
            break;
        }
        case DAP_MSG_CALLBACK: {
            dap_msg_rc_t l_cb_rc = DAP_MSG_DONE;
            if (a_ctx->callback_cb)
                l_cb_rc = a_ctx->callback_cb(&l_e->callback, a_ctx->_inheritor);
            if (l_cb_rc == DAP_MSG_DONE && l_e->callback.fn)
                l_e->callback.fn(l_e->callback.arg);
            l_rc = l_cb_rc;
            break;
        }
        case DAP_MSG_HEAP: {
            l_rc = DAP_MSG_DONE;
            if (a_ctx->heap_cb)
                l_rc = a_ctx->heap_cb(&l_e->heap, a_ctx->_inheritor);
            if (l_rc != DAP_MSG_DEFER && l_e->heap.cleanup)
                l_e->heap.cleanup(l_e->heap.ptr);
            break;
        }
        default: {
            if (a_ctx->custom_cb && l_e->custom_hdr)
                l_rc = a_ctx->custom_cb(l_e->custom_hdr, a_ctx->_inheritor);
            if (l_rc != DAP_MSG_DEFER) {
                free(l_e->custom_hdr);
                l_e->custom_hdr = NULL;
            }
            break;
        }
        }

        if (l_rc == DAP_MSG_DEFER) {
            if (l_e->msg_type == DAP_MSG_BATCH)
                dap_slab_bits_set(l_mask, l_e->conn_idx);
            l_e->next = NULL;
            if (l_tail) l_tail->next = l_e;
            else        l_kept = l_e;
            l_tail = l_e;
        } else {
            l_progress = true;
            free(l_e);
        }
        l_e = l_next;
    }

    a_q->head = l_kept;
    a_q->tail = l_tail;
    memcpy(a_q->mask, l_mask, sizeof(l_mask));
    return l_progress;
}

/* ================================================================== */
/*  Public API                                                         */
/* ================================================================== */

/**
 * @brief Processor main loop: drain all inbound queues, sleep when idle.
 *
 * Main-loop state machine (double-drain pattern):
 *
 *   +--------------------------------------------------------------+
 *   |                    MAIN LOOP ITERATION                        |
 *   |                                                               |
 *   |  1. timers --> drain due timers, compute next timeout         |
 *   |  2. defer  --> retry deferred tasks (backoff: 1ms..16ms)      |
 *   |  3. WFQ    --> drain ext-pending + FAST/NORM/BG lanes         |
 *   |       |-- work found? --> kick rescan workers, continue       |
 *   |       `-- empty?                                              |
 *   |            4. ACK --> notify_latch := 0 (release)             |
 *   |            5. re-drain WFQ (close race with producer push)    |
 *   |                 |-- work found? --> continue                  |
 *   |                 `-- still empty?                              |
 *   |                      6. clamp timeout (ext/defer/timer)       |
 *   |                      7. backoff defer recheck interval        |
 *   |                      8. trim timer heap if needed             |
 *   |                      9. kick rescan workers if pending        |
 *   |                     10. futex_wait(latch, exp=0, timeout)     |
 *   +--------------------------------------------------------------+
 *
 * notify_latch protocol (avoids missed wakeups):
 *   Producer: push to WFQ, then exchange(notify_latch, 1, release);
 *             if old value was 0 → futex_wake (consumer was idle).
 *   Consumer: set latch=0 (step 4), re-drain (step 5), sleep on 0.
 *   Re-drain closes the window where producer pushed between
 *   the drain and the ACK; if latch was set to 1 meanwhile,
 *   futex_wait returns immediately (expected=0 != actual=1).
 *
 * Shutdown sequence:
 *   1. force_complete = true
 *   2. defer_drain (force-resolve remaining deferred entries)
 *   3. final drain loop: all lanes, unlimited quota, until empty
 *   4. clear defer queue
 *   5. force-execute remaining ext-pending messages
 */
void dap_proc_loop_run(dap_proc_ctx_t *a_ctx)
{
    /* Publish this thread's identity: ext-stack handlers read dap_tls_proc
     * to recover the owning context without a void * cast. */
    dap_tls_proc = a_ctx;
    dap_vmqueue_mpsc_t *l_q = a_ctx->wfq;
    /* Lane ranges: N workers -> lanes [0,N) FAST, [N,2N) NORM, [2N,3N) BG; clamped to n_lanes */
    unsigned l_nl = l_q->n_lanes;
    unsigned l_nw = a_ctx->n_workers;
    unsigned l_fe = l_nw     < l_nl ? l_nw     : l_nl;  /* end of FAST range  */
    unsigned l_ne = 2 * l_nw < l_nl ? 2 * l_nw : l_nl;  /* end of NORM range  */
    unsigned l_be = 3 * l_nw < l_nl ? 3 * l_nw : l_nl;  /* end of BG range    */

    dap_msg_pending_t l_ext = DAP_MSG_PENDING_INIT;

    uint32_t l_timeout = DAP_TIMEOUT_INFINITE;
    uint32_t l_defer_recheck_ms = DAP_DEFER_RECHECK_MIN_MS;

    while (!atomic_load_explicit(a_ctx->shutdown, memory_order_relaxed)) {
        struct timespec l_ts0;
        clock_gettime(CLOCK_MONOTONIC, &l_ts0);

        s_proc_drain_timers(a_ctx);
        l_timeout = dap_timers_timeout(&a_ctx->timers);

        bool l_defer_progress = s_defer_drain_generic(&a_ctx->defer_q, a_ctx);
        /* Reset backoff when progress made or defer queue fully drained */
        if (l_defer_progress || !a_ctx->defer_q.head)
            l_defer_recheck_ms = DAP_DEFER_RECHECK_MIN_MS;

        size_t l_d = dap_proc_drain(l_q, l_fe, l_ne, l_be, &l_ext, a_ctx);
        dap_msg_harvest(&a_ctx->ext_stack, &l_ext);
        if (l_d) {
            if (atomic_load_explicit(&a_ctx->wfq_waiting->rescan_mask, memory_order_relaxed))
                s_proc_kick_rescan_workers(a_ctx);
            dap_stat(a_ctx->stats, drain_rounds, ++);
            l_defer_recheck_ms = DAP_DEFER_RECHECK_MIN_MS;
            struct timespec l_ts1;
            clock_gettime(CLOCK_MONOTONIC, &l_ts1);
            dap_stat(a_ctx->stats, busy_ns, +=
                (uint64_t)(l_ts1.tv_sec - l_ts0.tv_sec) * 1000000000ULL
                + (uint64_t)(l_ts1.tv_nsec - l_ts0.tv_nsec));
            continue;
        }

        /* Advance epoch + reclaim quarantined slab slots (cold path) */
        if (a_ctx->slab) {
            uint64_t l_ep = atomic_fetch_add_explicit(&a_ctx->slab->wfq_epoch, 1, memory_order_relaxed) + 1;
            dap_conn_slab_drain(a_ctx->slab, l_ep);
        }

        /* No work found -- prepare to sleep. Clamp timeout for ext/defer */
        if (l_ext.head && l_timeout > 1)
            l_timeout = 1;
        if (a_ctx->defer_q.head && l_timeout > l_defer_recheck_ms)
            l_timeout = l_defer_recheck_ms;

        /* ACK: set latch=0, telling producers "I saw everything, going idle" */
        atomic_store_explicit(&l_q->notify_latch, 0, memory_order_release);

        /* Re-drain: close the race window between ACK and sleep */
        l_d = dap_proc_drain(l_q, l_fe, l_ne, l_be, &l_ext, a_ctx);
        dap_msg_harvest(&a_ctx->ext_stack, &l_ext);
        if (l_d) {
            l_defer_recheck_ms = DAP_DEFER_RECHECK_MIN_MS;
            struct timespec l_ts1;
            clock_gettime(CLOCK_MONOTONIC, &l_ts1);
            dap_stat(a_ctx->stats, busy_ns, +=
                (uint64_t)(l_ts1.tv_sec - l_ts0.tv_sec) * 1000000000ULL
                + (uint64_t)(l_ts1.tv_nsec - l_ts0.tv_nsec));
            continue;
        }
        if (atomic_load_explicit(a_ctx->shutdown, memory_order_acquire))
            break;
        if (l_ext.head && l_timeout > 1)
            l_timeout = 1;
        /* Defer backoff: double recheck interval each idle pass (1->2->4->8->16ms) */
        if (a_ctx->defer_q.head) {
            if (l_timeout > l_defer_recheck_ms)
                l_timeout = l_defer_recheck_ms;
            if (l_defer_recheck_ms < DAP_DEFER_RECHECK_MAX_MS) {
                uint32_t l_next = l_defer_recheck_ms << 1;
                l_defer_recheck_ms = l_next < DAP_DEFER_RECHECK_MAX_MS
                                    ? l_next : DAP_DEFER_RECHECK_MAX_MS;
            }
        } else {
            l_defer_recheck_ms = DAP_DEFER_RECHECK_MIN_MS;
        }
        dap_timers_maybe_trim(&a_ctx->timers);
        /* Kick workers with pending rescan before sleeping */
        if (atomic_load_explicit(&a_ctx->wfq_waiting->rescan_mask, memory_order_relaxed)) {
            s_proc_kick_rescan_workers(a_ctx);
            continue;
        }
        dap_stat(a_ctx->stats, futex_sleeps, ++);
        /* Sleep protocol: if latch still 0 (no producer pushed since ACK),
           block until latch changes or timeout expires */
        if (!atomic_load_explicit(&l_q->notify_latch, memory_order_acquire))
            dap_futex_wait_timed((void *)&l_q->notify_latch, 0, l_timeout);
    }
    /* --- shutdown: force-complete all remaining work --- */
    a_ctx->force_complete = true;
    s_defer_drain_generic(&a_ctx->defer_q, a_ctx);
    for (;;) {
        size_t l_d = s_proc_drain_range(l_q, 0, l_nl, a_ctx, (size_t)-1);
        dap_msg_harvest(&a_ctx->ext_stack, &l_ext);
        l_d += dap_msg_drain_pending(&l_ext);
        if (!l_d) break;
    }
    dap_proc_defer_queue_clear(&a_ctx->defer_q);
    /* Force-execute remaining ext messages under force_complete;
       DONE = handler freed, DEFER/DROP = drain frees the node */
    while (l_ext.head) {
        dap_msg_t *l_next = l_ext.head->next;
        if (l_ext.head->execute(l_ext.head) != DAP_MSG_DONE)
            free(l_ext.head);
        l_ext.head = l_next;
    }
    dap_tls_proc = NULL;
}

/** @brief Signal the processor loop to shut down and wake it from futex sleep.
 *
 *  Uses unconditional exchange+wake (not the fast-path notify) to guarantee
 *  the consumer sees the shutdown flag via release-acquire on notify_latch. */
void dap_proc_shutdown(_Atomic(bool) *a_shutdown, dap_vmqueue_mpsc_t *a_wfq)
{
    atomic_store_explicit(a_shutdown, true, memory_order_release);
    if (!a_wfq)
        return;
    atomic_exchange_explicit(&a_wfq->notify_latch, 1, memory_order_release);
    dap_futex_wake(&a_wfq->notify_latch, 1);
}
