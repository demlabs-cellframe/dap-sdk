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
 * @file dap_worker_common.c
 * @brief Process-wide worker globals, send-side flush, and @c dap_worker_drain_pending
 *  (shared by epoll and IOCP loops; event loops: @c dap_worker_posix / @c dap_worker_win).
 */
#include "dap_worker_reactor.h"
#include "dap_send_olb.h"
#include <errno.h>

_Atomic uint64_t dap_timer_g_next_id = 1;

/* Process-global monotonic generation counter (extern in dap_conn.h).
 *
 * Starts at 0 so the first slab_alloc issues generation == 1; the value
 * 0 is reserved as the "slot never allocated / page reclaimed" sentinel
 * that dap_conn_resolve() rejects unconditionally.  At 1 alloc/ns it
 * would take ~584 years to wrap a uint64_t — the counter is effectively
 * monotonic for the life of the process. */
_Atomic(uint64_t) dap_conn_gen_counter = 0;

/* TLS identity of the worker running the current thread's event loop.
 *
 * Set at dap_worker_loop entry, cleared on return.  dap_io_tx_send
 * consults it to decide whether to inline send_direct or enqueue a
 * ctrl message for the owning worker — see dap_worker_reactor.h.
 *
 * NULL outside a worker thread, so external callers (unit tests,
 * user-owned producers, etc.) always take the slow path. */
DAP_THREADLOCAL dap_worker_t *dap_tls_worker = NULL;

/* ================================================================== */
/*  Platform-independent send helpers                                  */
/*                                                                     */
/*  All the functions below drive send_olb flushing and cross-thread   */
/*  delivery.  They use only atomics, the OLB interface, and           */
/*  dap_conn_kick — which is itself cross-platform in dap_conn.h — so  */
/*  one implementation serves both the Linux and Windows event loops.  */
/* ================================================================== */

/** @brief Flush send_olb → socket.  Clears SEND_BUSY when fully drained.
 *  Wakes the processor so deferred entries can be retried.
 *
 *  Dekker handshake: notify_send (processor) vs dap_worker_tx_flush (worker).
 *  seq_cst on the SEND_BUSY set/clear pair closes the race where the
 *  processor queues a new write right after the worker's last empty
 *  check but before it clears the flag.  See dap_worker_reactor.h. */
size_t dap_worker_tx_flush(dap_conn_t *a_c)
{
    dap_worker_t *l_w = (dap_worker_t *)atomic_load_explicit(
        &a_c->_owner, memory_order_relaxed);
    size_t l_flushed = 0;
    ssize_t l_f = 0;
    while ((l_f = dap_send_olb_flush(a_c->send_olb, a_c->fd)) > 0)
        l_flushed += (size_t)l_f;
    bool l_wake = l_flushed > 0;
    if (l_f == 0) {
        /* Dekker step 1: clear SEND_BUSY with seq_cst (pairs with notify_send's set) */
        uint8_t l_old = atomic_fetch_and_explicit(&a_c->state,
                                                  (uint8_t)~DAP_CONN_SEND_BUSY,
                                                  memory_order_seq_cst);
        l_wake = l_wake || (l_old & DAP_CONN_SEND_BUSY);
        /* Dekker step 2: re-flush catches data written between empty-check and clear */
        while ((l_f = dap_send_olb_flush(a_c->send_olb, a_c->fd)) > 0)
            l_flushed += (size_t)l_f;
        if (l_f < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            atomic_fetch_or_explicit(&a_c->state, DAP_CONN_SEND_BUSY,
                                      memory_order_release);
        }
    }
    /* Send-side backpressure relief: if read_cb was SUSPENDED because of
     * send_olb watermark, and we have now freed enough space for at least
     * one more worst-case response, schedule a drain_pending pass so the
     * flag can be cleared and read_cb resumed. */
    if (l_flushed > 0 && l_w) {
        uint8_t l_st = dap_conn_state(a_c);
        if ((l_st & DAP_CONN_SUSPENDED) && a_c->send_olb) {
            size_t l_wm = a_c->send_olb->compact_threshold;
            if (l_wm && dap_send_olb_free(a_c->send_olb) >= l_wm) {
                dap_slab_bits_set(l_w->pending_bits,
                                  dap_conn_slab_idx(l_w->conn_slab, a_c));
                dap_worker_kick(l_w);
            }
        }
    }
    if (l_wake && l_w)
        dap_bus_proc_wake(l_w->wfq);
    return l_flushed;
}

/* dap_worker_kick, dap_worker_after_batch_processed, dap_worker_conn_notify_send: dap_worker_ipc.c */

/** @brief Scan pending_bits and service marked connections (flush / unsuspend).
 *  Called from s_resume_read (Linux) and the IOCP resume handler (Windows).
 *
 *  pending_bits scan pattern (O(K) where K = set bits):
 *
 *    for each word w in pending_bits[]:
 *      bits = exchange(&pending_bits[w], 0)      ← grab + clear atomically
 *      while (bits):
 *        idx = w*64 + ctz(bits)
 *        bits &= bits - 1                        ← clear lowest set bit
 *        conn = slab_slot(idx), state = conn->state
 *        ├── CLOSED      → skip
 *        ├── SEND_BUSY   → apply_ack + dap_worker_tx_flush
 *        ├── RESCAN      → retry push_batch
 *        │    ├── ok     → clear RESCAN (+SUSPENDED → read_cb)
 *        │    └── fail   → re-set bit, notify proc for retry
 *        ├── SUSPENDED   → clear SUSPENDED → read_cb (resume recv)
 *        ├── SYNC+ready  → read_cb + dap_worker_tx_flush (EPOLLET re-arm after
 *        │                 ASYNC→SYNC transition; if !ready, re-set bit
 *        │                 and schedule processor-driven worker re-kick
 *        │                 via rescan_mask + wfq notify)
 *        └── watermark   → read_cb (backpressure relief) */
void dap_worker_drain_pending(dap_worker_t *a_w)
{
    for (unsigned l_w_idx = 0; l_w_idx < DAP_SLAB_BITS_WORDS; ++l_w_idx) {
        uint64_t l_word = dap_slab_bits_grab(a_w->pending_bits, l_w_idx);
        while (l_word) {
            unsigned l_bit = (unsigned)__builtin_ctzll(l_word);
            l_word &= l_word - 1;
            unsigned l_idx = l_w_idx * 64 + l_bit;
            dap_conn_t *l_c = dap_conn_slab_slot(a_w->conn_slab, l_idx);
            uint8_t l_st = dap_conn_state(l_c);
            if (l_st & DAP_CONN_CLOSED)
                continue;
            if (l_st & DAP_CONN_SEND_BUSY) {
                if (l_c->olb)
                    dap_vmqolb_apply_ack(l_c->olb);
                size_t l_sent = dap_worker_tx_flush(l_c);
                dap_stat(a_w->stats, send_bytes, += l_sent);
            }
            if (l_st & DAP_CONN_RESCAN) {
                /* WFQ push failed during recv; retry now that processor drained space */
                uint32_t l_end = (uint32_t)atomic_load_explicit(
                    &l_c->olb->tail_pos, memory_order_acquire);
                if (dap_worker_push_batch(a_w, dap_conn_handle_from_live(l_c), l_end)) {
                    atomic_fetch_and_explicit(&l_c->state,
                        (uint8_t)~DAP_CONN_RESCAN, memory_order_release);
                    if (l_st & DAP_CONN_SUSPENDED) {
                        /* Dekker resume: SUSPENDED was set together with RESCAN in recv */
                        atomic_fetch_and_explicit(&l_c->state,
                            (uint8_t)~DAP_CONN_SUSPENDED, memory_order_release);
                        if (l_c->read_cb) {
                            l_c->read_cb(l_c);
                            /* Self-wake epilogue: if read_cb wrote via
                             * dap_io_tx_send_direct owner fast-path, its
                             * SEND_BUSY + pending_bit may land in a word
                             * already grabbed this sweep — drain here so
                             * send_olb is not left waiting for an unrelated
                             * kick. */
                            if (l_c->send_olb) {
                                size_t l_sent = dap_worker_tx_flush(l_c);
                                dap_stat(a_w->stats, send_bytes, += l_sent);
                            }
                        }
                    }
                } else {
                    /* Still no space — re-mark for next resume cycle */
                    dap_slab_bits_set(a_w->pending_bits, l_idx);
                    atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                        (uint64_t)1 << a_w->worker_id, memory_order_release);
                    dap_vmqueue_mpsc_notify(a_w->wfq);
                }
            } else if (l_st & DAP_CONN_SUSPENDED) {
                atomic_fetch_and_explicit(&l_c->state,
                    (uint8_t)~DAP_CONN_SUSPENDED, memory_order_release);
                if (l_c->read_cb) {
                    l_c->read_cb(l_c);
                    if (l_c->send_olb) {
                        size_t l_sent = dap_worker_tx_flush(l_c);
                        dap_stat(a_w->stats, send_bytes, += l_sent);
                    }
                }
            } else if ((l_st & DAP_CONN_SYNC)
                       && !(l_st & DAP_CONN_RECV_DONE))
            {
                if (dap_conn_sync_ready(l_c)) {
                    if (l_c->read_cb) {
                        l_c->read_cb(l_c);
                        if (l_c->send_olb) {
                            size_t l_sent = dap_worker_tx_flush(l_c);
                            dap_stat(a_w->stats, send_bytes, += l_sent);
                        }
                    }
                } else {
                    dap_slab_bits_set(a_w->pending_bits, l_idx);
                    atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                        (uint64_t)1 << a_w->worker_id, memory_order_release);
                    dap_vmqueue_mpsc_notify(a_w->wfq);
                }
            } else if (l_c->olb
                       && atomic_load_explicit(&l_c->olb->watermark_pending,
                                               memory_order_relaxed)
                       && !(l_st & DAP_CONN_RECV_DONE))
            {
                if (l_c->read_cb) {
                    l_c->read_cb(l_c);
                    if (l_c->send_olb) {
                        size_t l_sent = dap_worker_tx_flush(l_c);
                        dap_stat(a_w->stats, send_bytes, += l_sent);
                    }
                }
            }
        }
    }
}

/* dap_worker_ctrl_send, kick/notify batch: dap_worker_ipc.c */
