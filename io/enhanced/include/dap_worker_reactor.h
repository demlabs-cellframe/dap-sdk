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
 * @file dap_worker_reactor.h
 * @brief Advanced/internal relative to baseline protocol docs — enhanced worker thread (epoll on Linux; IOCP on Windows).
 *
 * Event loop, parse/compact typedefs, and worker-side hot path; not the first header to
 * name in minimal integration guides (prefer @ref dap_io_ops.h + @ref dap_io.h).
 *
 * All fds in the poller are dap_conn_t: user connections, resume eventfd,
 * and timer — uniform dispatch via conn->read_cb / write_cb / error_cb.
 *
 * Worker references a shared connection slab (set externally before init).
 * Internal connections (resume, timer) are slab-allocated; they use bare
 * eventfd/timerfd for wakeup, not OLB data transfer.
 *
 * Cross-thread signaling:
 *   processor → worker:  dap_io_tx_send* fold (SEND_BUSY + pending_bits + kick)
 *   worker → processor:  push_batch via WFQ lane + dap_bus_proc_wake (futex)
 *   any thread → worker:  dap_worker_post_ctrl (Treiber MPSC + kick)
 *
 *  ┌─────────── Worker event loop ───────────┐
 *  │                                         │
 *  │  epoll_wait(epfd)                       │
 *  │       │                                 │
 *  │       ├─ EPOLLERR → CLOSED + error_cb   │
 *  │       ├─ EPOLLOUT → write_cb / flush    │
 *  │       │                                 │
 *  │       ├─ EPOLLIN(resume_conn):          │
 *  │       │    drain eventfd                │
 *  │       │    drain ctrl_stack             │
 *  │       │    drain_pending                │
 *  │       │                                 │
 *  │       ├─ EPOLLIN(timer_conn):           │
 *  │       │    drain timerfd → timers       │
 *  │       │    rearm timerfd                │
 *  │       │                                 │
 *  │       └─ EPOLLIN(user conn):            │
 *  │            read_cb → recv loop          │
 *  │            ├─ OLB full → SUSPEND        │
 *  │            ├─ parse → push_batch → proc │
 *  │            └─ EOF/err → done/closed     │
 *  │                                         │
 *  │  shutdown? ─── no ──→ loop              │
 *  │              └ yes → return             │
 *  └─────────────────────────────────────────┘
 *
 *  Send inlines: @ref dap_io_send.h.
 */
#pragma once

#include "dap_worker_ipc.h"
#include "dap_worker_types.h"

/* ================================================================== */
/*  Worker control message — INTERNAL TYPE                             */
/*                                                                     */
/*  A separate node type from the public dap_msg_t (used for processor */
/*  ext-stack) so that pointers cannot cross the two queues at compile */
/*  time.  There is NO public constructor for dap_worker_msg_t — all   */
/*  producers of ctrl messages live inside the I/O runtime itself      */
/*  (cross-thread dap_io_tx_send, dap_io_timer_cancel_async, future      */
/*  migration / async close primitives).  User code always addresses   */
/*  the worker through specialised high-level APIs; it never posts     */
/*  ctrl messages directly.                                            */
/*                                                                     */
/*  Reuses the public dap_msg_rc_t enum and DAP_MSG_CAST / DAP_MSG_FREE */
/*  macros — they operate on any self pointer.                         */
/* ================================================================== */

/** Declare a worker ctrl-message subtype — embeds dap_worker_msg_t as
 *  the first field for "downcast via (self)" via DAP_MSG_CAST. */
#define DAP_WORKER_MSG_TYPE(name, ...)                                  \
    typedef struct name { dap_worker_msg_t _msg; __VA_ARGS__; } name

/** Allocate a worker ctrl-message subtype and wire its execute handler. */
#define DAP_WORKER_MSG_ALLOC(type, exec_fn)                             \
    ({ type *_m = (type *)calloc(1, sizeof(type));                      \
       if (_m) _m->_msg.execute = (exec_fn); _m; })

/** CAS-push onto a worker ctrl stack (any thread, lock-free). */
DAP_STATIC_INLINE void
dap_worker_msg_stack_push(dap_worker_msg_stack_t *a_s, dap_worker_msg_t *a_m)
{
    dap_worker_msg_t *l_old =
        atomic_load_explicit(&a_s->top, memory_order_relaxed);
    do {
        a_m->next = l_old;
    } while (!atomic_compare_exchange_weak_explicit(
                &a_s->top, &l_old, a_m,
                memory_order_release, memory_order_acquire));
}

/** Atomically detach the whole chain (worker-owner only). */
DAP_STATIC_INLINE dap_worker_msg_t *
dap_worker_msg_stack_detach(dap_worker_msg_stack_t *a_s)
{
    return atomic_exchange_explicit(&a_s->top, NULL, memory_order_acquire);
}

/** Reverse a detached chain in place to deliver drain in FIFO order.
 *  Treiber push is LIFO; reversing a private chain is a single
 *  local pass with no atomics — see comment in s_resume_read. */
DAP_STATIC_INLINE dap_worker_msg_t *
dap_worker_msg_list_reverse(dap_worker_msg_t *a_head)
{
    dap_worker_msg_t *l_prev = NULL;
    while (a_head) {
        dap_worker_msg_t *l_next = a_head->next;
        a_head->next = l_prev;
        l_prev = a_head;
        a_head = l_next;
    }
    return l_prev;
}

/* ================================================================== */
/*  TLS: "the worker whose loop this thread is running"                */
/*                                                                     */
/*  Set at dap_worker_loop entry, cleared on return.  Used by      */
/*  dap_io_tx_send to decide whether the caller IS the owner of the   */
/*  target connection (fast path — inline direct write) or NOT         */
/*  (slow path — post_ctrl to the owning worker).  NULL outside a      */
/*  worker thread, which routes everything through the slow path.     */
/* ================================================================== */
extern DAP_THREADLOCAL dap_worker_t *dap_tls_worker;

/** @brief Request the worker to stop its event loop.
 *  Sets the shutdown flag and kicks the worker so it wakes from
 *  the poller wait and exits on the next iteration. */
DAP_STATIC_INLINE void dap_worker_request_stop(dap_worker_t *a_w)
{
    if (!a_w)
        return;
    atomic_store_explicit(&a_w->shutdown, true, memory_order_release);
    dap_worker_kick(a_w);
}

/* ================================================================== */
/*  Control channel — any thread → worker (Treiber MPSC + eventfd)     */
/* ================================================================== */

/** @brief Post a control message to the worker (any thread, lock-free).
 *
 *  The message must be a dap_worker_msg_t — a type only producible by
 *  the runtime itself (see DAP_WORKER_MSG_TYPE).  This pins at compile
 *  time the fact that ctrl_stack is worker-only: you cannot accidentally
 *  mix it with a processor-bound dap_msg_t because the types are
 *  distinct.
 *
 *  CAS-push onto a Treiber stack, then kick the worker via its
 *  platform wakeup primitive.  Drained by s_resume_read in FIFO order
 *  (private chain is reversed after detach — ~4 insns per node, no
 *  atomics). */
DAP_STATIC_INLINE void
dap_worker_post_ctrl(dap_worker_t *a_w, dap_worker_msg_t *a_msg)
{
    dap_worker_msg_stack_push(&a_w->ctrl_stack, a_msg);
    dap_worker_kick(a_w);
}

/* ================================================================== */
/*  Push helpers                                                       */
/* ================================================================== */

/** @brief Push a batch task to the NORM lane (push + processor wake).
 *  @param a_conn       Generation-checked handle for the source connection.
 *  @param a_batch_end  Absolute OLB offset (tail_pos at batch boundary). */
DAP_STATIC_INLINE bool
dap_worker_push_batch(dap_worker_t *a_w,
                       dap_conn_handle_t a_conn, uint32_t a_batch_end)
{
    dap_batch_task_t l_t = { .conn = a_conn, .batch_end = a_batch_end };
    if (!dap_vmqueue_mpsc_push(a_w->wfq, a_w->conn_lane,
                                DAP_MSG_BATCH, DAP_WFQ_PRI_NORM,
                                &l_t, sizeof(l_t)))
        return false;
    dap_bus_proc_wake(a_w->wfq);
    return true;
}

/* ================================================================== */
/*  Generic worker helpers                                             */
/*                                                                     */
/*  Implemented in dap_worker_common.c.  Out-of-line because each  */
/*  performs real work (syscalls on the flush path, ~90-line pending   */
/*  scan on the drain path) that dwarfs any call overhead.  The hot    */
/*  inlineable primitives are dap_io_tx_send_direct / dap_io_tx_send   */
/*  below — everything here is support code for them.                  */
/* ================================================================== */

/** @brief Flush send_olb → socket.  Clears SEND_BUSY when fully drained.
 *  Wakes the processor so deferred entries can be retried.
 *
 *  Dekker handshake: notify_send (processor) vs dap_worker_tx_flush (worker)
 *
 *    Processor                       Worker
 *    ─────────                       ──────
 *    write to send_olb               ┌─ flush send_olb → socket
 *    set SEND_BUSY (seq_cst)──┐      │
 *    set pending_bits          │      ├─ drained? clear SEND_BUSY (seq_cst)
 *    kick eventfd ────────────│──    │    re-flush (Dekker catch)
 *                              └─▶   │    ├─ data appeared → flush it
 *                                    │    └─ EAGAIN → re-set SEND_BUSY
 *                                    └─ wake processor (deferred retry)
 *
 *  seq_cst on both sides: if processor writes after worker's last
 *  empty-check, the re-flush sees the new data.
 *
 *  Owner worker + wfq are resolved from @p a_c->_owner — must be the
 *  connection's owner thread.  Use dap_io_tx_send*() from any other
 *  thread. */
size_t dap_worker_tx_flush(dap_conn_t *a_c);

/** @brief Scan pending_bits and service marked connections (flush / unsuspend).
 *  Called from s_resume_read (Linux) and IOCP resume handler (Windows). */
void dap_worker_drain_pending(dap_worker_t *a_w);

/* dap_worker_conn_notify_send: see @ref dap_worker_ipc.h */

#include "dap_worker_send.h" /* IWYU pragma: export */

/* ================================================================== */
/*  Generic recv loop                                                  */
/* ================================================================== */
/*  Read-side transport (recv vs read, pull fn) is *not* stored in      */
/*  dap_conn_t.  dap_worker_loop only calls conn->read_cb; I/O "kind"   */
/*  and dap_rx_pull_fn live in conn->ext (see dap_io_ops.h).            */
/* ================================================================== */

/**
 * @brief User compact callback: OLB was compacted (memmove), reset
 *        protocol parser state.  Recv tail is already set to 0 by
 *        @ref dap_worker_rx_olb before this callback fires.  Access protocol context
 *        via `a_conn->ext`.
 */
typedef void (*dap_worker_compact_fn)(dap_conn_t *a_conn);

/**
 * @brief Pull the next byte slice into the OLB (recv/read path injected).
 */
typedef ssize_t (*dap_rx_pull_fn)(dap_conn_t *a_c, void *a_buf, size_t a_max,
                                 void *a_ctx);

struct dap_io_olb_parser;

/**
 * @brief OLB receive loop: OLB space, @a a_pull into buffer, Dekker suspend,
 *        compaction, tail_pos publish, batch push, EOF/error.
 *
 * The owning worker is resolved from @p a_conn (`a_conn->_owner`). @a a_pull
 * must be non-NULL (e.g. from @ref dap_io_conn_open / @ref dap_io_rx_ctx_init;
 * see @ref dap_io_ops.h). Error policy: pulls implemented for POSIX retry
 * @c EINTR; @c EAGAIN / @c EWOULDBLOCK end the current read round; @c 0
 * is EOF. Same logical behaviour as the prior hard-coded @c recv path
 * for stream sockets.
 *
 * @param a_parser   Parser descriptor (@c tail, @c parse, optional @c compact); see @ref dap_io_ops.h.
 * @param a_pull     Byte source; must not be NULL for normal I/O.
 * @param a_pull_ctx Opaque context for @a a_pull (often NULL for SOCK/FILE).
 */
void dap_worker_rx_olb(dap_conn_t *a_conn, struct dap_io_olb_parser *a_parser,
                      dap_rx_pull_fn a_pull, void *a_pull_ctx);

/* ================================================================== */
/*  Public API (init/loop/recv/timers: dap_worker_posix|win;       */
/*  see also dap_worker_common.c)                                */
/* ================================================================== */

/**
 * @brief Create poller and internal connections (resume, timer).
 *
 * Allocates resume_conn and timer_conn from conn_slab, sets up their
 * fds and read_cb handlers, registers them in the poller.
 * conn_slab must be set before calling.
 *
 * @return 0 on success, -1 on failure.
 */
int  dap_worker_init(dap_worker_t *a_w);

/**
 * @brief Register a periodic timer on the worker and rearm timerfd (POSIX).
 * @param a_interval_us  First/period tick interval; must be non-zero for a meaningful timer.
 * @param a_iterations  @c 0 = infinite, @c 1 = one-shot, @c N = fire N times
 *       (same as @c dap_msg_post_timer on the processor; see @ref dap_timer_t::iterations).
 * @return Handle for cross-thread cancellation, or DAP_TIMER_HANDLE_NULL.
 */
dap_timer_handle_t dap_worker_timer_add(dap_worker_t *a_w,
                                        uint64_t a_interval_us,
                                        uint32_t a_iterations,
                                        dap_timer_cb_t a_exec,
                                        void *a_arg);

/** @brief Remove a timer by handle.  Owner-thread only. */
void dap_worker_timer_del(dap_worker_t *a_w, dap_timer_handle_t a_h);

/** @brief Rearm timerfd to fire at the earliest deadline.
 *  Called internally after drain and after timer_add. */
void dap_worker_timer_rearm(dap_worker_t *a_w);

/**
 * @brief Register a user connection in the worker's poller.
 *
 * Adds conn->fd to epoll (EPOLLOUT|EPOLLRDHUP|EPOLLET — no EPOLLIN).
 * Sets conn->_owner = worker, links into the worker-owned conn list.
 * Call dap_worker_conn_arm_read() after assigning read_cb to add EPOLLIN.
 *
 * @return 0 on success, -1 on failure (limit or epoll_ctl error).
 */
int  dap_worker_conn_add(dap_worker_t *a_w, dap_conn_t *a_conn);

/**
 * @brief Arm EPOLLIN for a connection already added via conn_add.
 *
 * Call AFTER setting read_cb to avoid EPOLLET edge loss.
 * conn_add registers EPOLLOUT|EPOLLRDHUP|EPOLLET only;
 * this function adds EPOLLIN via EPOLL_CTL_MOD atomically.
 * @return 0 on success, -1 on epoll_ctl error.
 */
int  dap_worker_conn_arm_read(dap_worker_t *a_w, dap_conn_t *a_conn);

/**
 * @brief Remove a user connection from the worker's poller,
 * quarantine the slab slot (wfq_epoch + 1).
 *
 * Removes conn->fd from the worker poller.  Swap-removes from
 * the worker-owned conn list (O(1)).
 */
void dap_worker_conn_del(dap_worker_t *a_w, dap_conn_t *a_conn);

/**
 * @brief Close poller and internal fds created by init.
 */
void dap_worker_cleanup(dap_worker_t *a_w);

/**
 * @brief Run the worker event loop (blocks until shutdown flag is set).
 *
 * Dispatch is uniform: every epoll event resolves to a dap_conn_t via
 * data.ptr, then conn->read_cb / write_cb / error_cb are called with
 * state guards (SUSPENDED, CLOSED, RECV_DONE skip further reads).
 * Shutdown is checked after each batch of events via acquire load.
 */
void dap_worker_loop(dap_worker_t *a_w);
