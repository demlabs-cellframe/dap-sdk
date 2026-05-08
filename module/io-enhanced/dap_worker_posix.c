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
 * @file dap_worker_posix.c
 * @brief Non-Windows worker: epoll, timerfd, eventfd, @c dap_worker_rx_olb, main loop.
 */
#include "dap_io_ops.h"
#include "dap_worker_reactor.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#define DAP_WK_MAX_EV 64

/* ------------------------------------------------------------------ */
/*  Internal conn callbacks                                            */
/* ------------------------------------------------------------------ */

/** @brief resume_conn read_cb: drain eventfd, execute ctrl_stack
 *  messages in FIFO order, scan pending_bits, drain quarantine.
 *
 *  ctrl_stack is a Treiber stack — push is LIFO.  After atomically
 *  detaching the full chain (owner-only, no atomics in the rest of
 *  the pass) we reverse it into submission order.  Reversal is a
 *  single local pointer-flip loop over nodes that are still hot in
 *  L1 from the pusher — ~4 insns per node, far below the cost of
 *  the subsequent execute() handlers.  FIFO matters because the
 *  cross-thread send path allocates one ctrl node per call, and
 *  callers expect their outgoing writes on one connection to reach
 *  the wire in the order they were issued.
 *
 *  Owning worker is recovered from dap_tls_worker (set at loop entry);
 *  the conn's `ext` is intentionally NULL for internal connections. */
static void s_resume_read(dap_conn_t *a_self)
{
    dap_worker_t *l_w = dap_tls_worker;
    uint64_t l_val;
    if (read(a_self->fd, &l_val, sizeof(l_val)) < 0) {;}

    dap_worker_msg_t *l_chain =
        dap_worker_msg_list_reverse(
            dap_worker_msg_stack_detach(&l_w->ctrl_stack));
    while (l_chain) {
        dap_worker_msg_t *l_next = l_chain->next;
        dap_msg_rc_t l_rc = l_chain->execute(l_w, l_chain);
        if (l_rc == DAP_MSG_DROP)
            free(l_chain);
        l_chain = l_next;
    }

    dap_worker_drain_pending(l_w);
}

/** @brief Fire all expired timers from the worker's timer heap. */
static void s_worker_drain_timers(dap_worker_t *a_w)
{
    dap_timers_drain(&a_w->timers);
}

/** @brief timer_conn read_cb: drain timerfd, run expired timers, rearm.
 *  Worker is recovered from dap_tls_worker — conn->ext stays NULL. */
static void s_timer_read(dap_conn_t *a_self)
{
    dap_worker_t *l_w = dap_tls_worker;
    uint64_t l_exp;
    if (read(a_self->fd, &l_exp, sizeof(l_exp)) < 0) return;
    s_worker_drain_timers(l_w);
    dap_worker_timer_rearm(l_w);
}

/* ------------------------------------------------------------------ */
/*  Helper: register a conn fd in epoll                                */
/* ------------------------------------------------------------------ */

static int s_epoll_add(int a_epfd, dap_conn_t *a_conn, uint32_t a_events)
{
    struct epoll_event l_ev = { .events = a_events, .data.ptr = a_conn };
    return epoll_ctl(a_epfd, EPOLL_CTL_ADD, a_conn->fd, &l_ev);
}

static void s_worker_conn_fail(dap_conn_t *a_conn, uint8_t a_flags, int a_err)
{
    uint8_t l_old = atomic_fetch_or_explicit(&a_conn->state,
                                             (uint8_t)(DAP_CONN_CLOSED | a_flags),
                                             memory_order_release);
    if (a_conn->fd >= 0) {
        close(a_conn->fd);
        a_conn->fd = -1;
    }
    if (!(l_old & DAP_CONN_CLOSED) && a_conn->error_cb)
        a_conn->error_cb(a_conn, a_err);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

/** @brief Create epoll fd, allocate resume_conn (eventfd) and
 *  timer_conn (timerfd) from conn_slab, register both in epoll.
 *  conn_slab must be set before calling.
 *  @return 0 on success, -1 on failure (all fds cleaned up). */
int dap_worker_init(dap_worker_t *a_w)
{
    dap_timers_init(&a_w->timers);
    a_w->epfd = epoll_create1(0);
    if (a_w->epfd < 0)
        return -1;

    int l_efd = -1;
    int l_tfd = -1;

    l_efd = eventfd(0, EFD_NONBLOCK);
    if (l_efd < 0) goto fail;

    a_w->resume_conn = dap_conn_slab_alloc(a_w->conn_slab);
    if (!a_w->resume_conn) goto fail;
    a_w->resume_conn->fd      = l_efd;
    l_efd = -1;
    a_w->resume_conn->read_cb = s_resume_read;
    a_w->resume_conn->ext     = NULL;  /* internal conn — worker read via dap_tls_worker */
    if (s_epoll_add(a_w->epfd, a_w->resume_conn, EPOLLIN) < 0)
        goto fail;

    l_tfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (l_tfd < 0) goto fail;

    a_w->timer_conn = dap_conn_slab_alloc(a_w->conn_slab);
    if (!a_w->timer_conn) goto fail;
    a_w->timer_conn->fd      = l_tfd;
    l_tfd = -1;
    a_w->timer_conn->read_cb = s_timer_read;
    a_w->timer_conn->ext     = NULL;   /* internal conn — worker read via dap_tls_worker */
    if (s_epoll_add(a_w->epfd, a_w->timer_conn, EPOLLIN) < 0)
        goto fail;
    return 0;

fail:
    if (l_tfd >= 0) close(l_tfd);
    if (l_efd >= 0) close(l_efd);
    if (a_w->timer_conn) {
        if (a_w->timer_conn->fd >= 0) { close(a_w->timer_conn->fd); a_w->timer_conn->fd = -1; }
        dap_conn_slab_return(a_w->conn_slab, a_w->timer_conn);
        a_w->timer_conn = NULL;
    }
    if (a_w->resume_conn) {
        if (a_w->resume_conn->fd >= 0) { close(a_w->resume_conn->fd); a_w->resume_conn->fd = -1; }
        dap_conn_slab_return(a_w->conn_slab, a_w->resume_conn);
        a_w->resume_conn = NULL;
    }
    if (a_w->epfd >= 0) { close(a_w->epfd); a_w->epfd = -1; }
    return -1;
}

/* dap_worker_timer_rearm, dap_worker_timer_add, dap_worker_timer_del: dap_worker_timer.c */

enum { DAP_EPOLL_CONN_BASE = EPOLLOUT | EPOLLRDHUP | EPOLLET };

/** @brief Register user connection in epoll (base mask, no EPOLLIN),
 *  set _owner for cross-thread kick routing, append to conns[].
 *
 *  EPOLLIN is NOT set here — call dap_worker_conn_arm_read() after assigning
 *  read_cb to avoid losing an edge-triggered event before the callback is set. */
int dap_worker_conn_add(dap_worker_t *a_w, dap_conn_t *a_conn)
{
    if (s_epoll_add(a_w->epfd, a_conn, DAP_EPOLL_CONN_BASE) < 0)
        return -1;
    /* Publish the owner pointer.  This store is release to preserve program order
     * for readers that also synchronize via acquire operations on other objects
     * (e.g. connection generation checks, state handshakes).
     *
     * IMPORTANT: a relaxed load of _owner does NOT form a release/acquire
     * synchronisation edge by itself.  Do not rely on _owner as a "publish all
     * prior writes" mechanism unless the reader performs an acquire (or stronger)
     * load that pairs with this store (either on _owner itself, or via an
     * independent synchronisation path that already establishes the needed
     * visibility for the data being read). */
    atomic_store_explicit(&a_conn->_owner, a_w, memory_order_release);

    uint16_t l_idx = dap_conn_slab_idx(a_w->conn_slab, a_conn);
    uint16_t l_head = a_w->conn_head;
    a_conn->_w_prev = UINT16_MAX;
    a_conn->_w_next = l_head;
    if (l_head != UINT16_MAX) {
        dap_conn_t *l_hc = dap_conn_slab_slot(a_w->conn_slab, l_head);
        l_hc->_w_prev = l_idx;
    }
    a_w->conn_head = l_idx;
    return 0;
}

/** @brief Arm EPOLLIN for a connection already in epoll.
 *  Adds EPOLLIN to the base mask via EPOLL_CTL_MOD. */
int dap_worker_conn_arm_read(dap_worker_t *a_w, dap_conn_t *a_conn)
{
    struct epoll_event l_ev = {
        .events = DAP_EPOLL_CONN_BASE | EPOLLIN,
        .data.ptr = a_conn
    };
    return epoll_ctl(a_w->epfd, EPOLL_CTL_MOD, a_conn->fd, &l_ev);
}

/** @brief Deregister conn from epoll, swap-remove from conns[], quarantine the slab slot.
 *
 *  wfq_seq = current epoch + 1: the slot becomes reclaimable after the
 *  processor completes at least one more drain cycle (guaranteeing all
 *  pending WFQ tasks for this conn are processed). */
void dap_worker_conn_del(dap_worker_t *a_w, dap_conn_t *a_conn)
{
    epoll_ctl(a_w->epfd, EPOLL_CTL_DEL, a_conn->fd, NULL);
    uint16_t l_idx = dap_conn_slab_idx(a_w->conn_slab, a_conn);
    uint16_t l_p = a_conn->_w_prev;
    uint16_t l_n = a_conn->_w_next;
    if (l_p != UINT16_MAX) {
        dap_conn_t *l_pc = dap_conn_slab_slot(a_w->conn_slab, l_p);
        l_pc->_w_next = l_n;
    } else {
        a_w->conn_head = l_n;
    }
    if (l_n != UINT16_MAX) {
        dap_conn_t *l_nc = dap_conn_slab_slot(a_w->conn_slab, l_n);
        l_nc->_w_prev = l_p;
    }
    a_conn->_w_prev = UINT16_MAX;
    a_conn->_w_next = UINT16_MAX;
    uint64_t l_epoch = atomic_load_explicit(&a_w->conn_slab->wfq_epoch, memory_order_relaxed);
    dap_conn_slab_free(a_w->conn_slab, l_idx, l_epoch + 1);
}

/** @brief Destroy timer heap, disarm+close timerfd, close eventfd, close epoll. */
void dap_worker_cleanup(dap_worker_t *a_w)
{
    dap_timers_destroy(&a_w->timers);
    if (a_w->timer_conn && a_w->timer_conn->fd >= 0) {
        struct itimerspec l_zero = {{0,0},{0,0}};
        timerfd_settime(a_w->timer_conn->fd, 0, &l_zero, NULL);
        close(a_w->timer_conn->fd);
        a_w->timer_conn->fd = -1;
    }
    if (a_w->resume_conn && a_w->resume_conn->fd >= 0) {
        close(a_w->resume_conn->fd);
        a_w->resume_conn->fd = -1;
    }
    if (a_w->epfd >= 0) { close(a_w->epfd); a_w->epfd = -1; }
}

/* ================================================================== */
/*  Generic recv loop (platform-independent core)                      */
/*                                                                     */
/*  Recv flow:                                                         */
/*    1. Acquire OLB write space (Dekker suspend if full)              */
/*    2. recv() into OLB, call parse callback, advance tail            */
/*    3. Publish tail_pos (release), push batch task to WFQ            */
/*    4. On EAGAIN: commit accumulated data and return                 */
/*    5. On EOF: set RECV_DONE; on error: set CLOSED                   */
/*                                                                     */
/*  Dekker suspend protocol (OLB full):                                */
/*    Worker: set SUSPENDED (seq_cst) → apply_ack → re-check space    */
/*    If still full: publish tail, push batch, set pending_bits,       */
/*      notify processor via rescan_mask, return.                      */
/*    If space freed: clear SUSPENDED, continue recv.                  */
/* ================================================================== */

void dap_worker_rx_olb(dap_conn_t *a_conn, struct dap_io_olb_parser *a_parser,
                        dap_rx_pull_fn a_pull, void *a_pull_ctx)
{
    dap_worker_t *a_w = (dap_worker_t *)atomic_load_explicit(
        &a_conn->_owner, memory_order_relaxed);
    if (!a_pull || !a_parser || !a_parser->parse)
        return;
    bool l_has_data = false;
    for (;;) {
        char *l_ptr; size_t l_avail;
        /* ---------- OLB space ---------- */
        dap_olb_space_t l_sp = dap_vmqolb_try_space(a_conn->olb, &l_ptr, &l_avail);
        if (l_sp == DAP_OLB_FULL) {
            if (dap_conn_state(a_conn) & DAP_CONN_SYNC) {
                if (a_conn->olb->bytes_needed
                    && a_conn->olb->bytes_needed > a_conn->olb->capacity)
                {
                    s_worker_conn_fail(a_conn, DAP_CONN_PURGE, EMSGSIZE);
                    dap_stat(a_w->stats, err_count, ++);
                    return; /* SYNC + FULL is fatal only for true oversized frames. */
                }
                unsigned l_idx = dap_conn_slab_idx(a_w->conn_slab, a_conn);
                dap_slab_bits_set(a_w->pending_bits, l_idx);
                atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                                          (uint64_t)1 << a_w->worker_id, memory_order_release);
                dap_vmqueue_mpsc_notify(a_w->wfq);
                return; /* SYNC transient FULL: retry via pending/rescan path (EPOLLET-safe). */
            }
            /* ---------- Dekker suspend (ASYNC only) ---------- */
            atomic_fetch_or_explicit(&a_conn->state, DAP_CONN_SUSPENDED,
                                      memory_order_seq_cst);
            dap_vmqolb_apply_ack(a_conn->olb);
            l_sp = dap_vmqolb_try_space(a_conn->olb, &l_ptr, &l_avail);
            if (l_sp == DAP_OLB_FULL) {
                unsigned l_idx = dap_conn_slab_idx(a_w->conn_slab, a_conn);
                if (l_has_data) {
                    atomic_store_explicit(&a_conn->olb->tail_pos,
                                          a_parser->tail, memory_order_release);
                    if (!dap_worker_push_batch(a_w,
                            dap_conn_handle_from_live(a_conn), (uint32_t)a_parser->tail)) {
                        atomic_fetch_or_explicit(&a_conn->state, DAP_CONN_RESCAN,
                                                  memory_order_relaxed);
                    }
                }
                dap_slab_bits_set(a_w->pending_bits, l_idx);
                atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                                          (uint64_t)1 << a_w->worker_id, memory_order_release);
                dap_vmqueue_mpsc_notify(a_w->wfq);
                dap_stat(a_w->stats, suspends, ++);
                return;
            }
            dap_conn_clear(a_conn, DAP_CONN_SUSPENDED);
        }
        if (l_sp == DAP_OLB_COMPACTED) {
            a_parser->tail = 0;
            if (a_parser->compact) a_parser->compact(a_conn);
            dap_stat(a_w->stats, compacts, ++);
            l_has_data = false;
        }
        /* ---------- recv: limit to bytes_needed near compact threshold -------- */
        size_t l_recv_len = l_avail;
        if (a_conn->olb->bytes_needed
            && l_avail < a_conn->olb->compact_threshold
            && a_conn->olb->bytes_needed < l_avail)
            l_recv_len = a_conn->olb->bytes_needed;
        ssize_t l_rd = a_pull(a_conn, l_ptr, l_recv_len, a_pull_ctx);
        if (l_rd > 0) {
            a_conn->olb->write_end += (size_t)l_rd;
            dap_stat(a_w->stats, recv_bytes, += (size_t)l_rd);
            uint64_t l_tail_before = a_parser->tail;
            a_parser->parse(a_conn, a_parser);
            if (a_parser->tail > l_tail_before) {
                if (dap_conn_state(a_conn) & DAP_CONN_SYNC) {
                    dap_vmqolb_sync_ack(a_conn->olb, a_parser->tail);
                } else {
                    atomic_store_explicit(&a_conn->olb->tail_pos,
                                          a_parser->tail, memory_order_release);
                    l_has_data = true;
                }
            }
            continue;
        }
        /* ---------- exit: commit accumulated data (ASYNC only) ---------- */
        /* In SYNC mode l_has_data is always false — data acked inline in parse path */
        if (l_has_data) {
            atomic_store_explicit(&a_conn->olb->tail_pos,
                                  a_parser->tail, memory_order_release);
            if (!dap_worker_push_batch(a_w,
                    dap_conn_handle_from_live(a_conn), (uint32_t)a_parser->tail)) {
                atomic_fetch_or_explicit(&a_conn->state,
                                          DAP_CONN_SUSPENDED | DAP_CONN_RESCAN,
                                          memory_order_seq_cst);
                dap_slab_bits_set(a_w->pending_bits, dap_conn_slab_idx(a_w->conn_slab, a_conn));
                atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                                          (uint64_t)1 << a_w->worker_id, memory_order_release);
                dap_vmqueue_mpsc_notify(a_w->wfq);
                dap_stat(a_w->stats, suspends, ++);
                return;
            }
        }
        /* ---------- EOF / error ---------- */
        if (l_rd == 0) {
            dap_conn_set(a_conn, DAP_CONN_RECV_DONE);
            dap_stat(a_w->stats, eof_count, ++);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            s_worker_conn_fail(a_conn, 0, errno);
            dap_stat(a_w->stats, err_count, ++);
        }
        return;
    }
}

/** @brief Main event loop — blocks on epoll_wait, dispatches via conn callbacks.
 *  All fds resolve to dap_conn_t via data.ptr.  Internal conns (resume_conn,
 *  timer_conn) use dedicated read_cb handlers; user conns go through the
 *  standard read_cb/write_cb/error_cb interface with state guards.
 *  Exits when shutdown flag is set (checked after each epoll batch). */
void dap_worker_loop(dap_worker_t *a_w)
{
    struct epoll_event l_evs[DAP_WK_MAX_EV];
    dap_tls_worker = a_w;    /* publish owner identity for dap_io_tx_send's fast path */
    for (;;) {
        dap_timers_maybe_trim(&a_w->timers); /* periodic free-node reclaim */
        int l_n = epoll_wait(a_w->epfd, l_evs, DAP_WK_MAX_EV, -1);
        if (l_n < 0) {
            if (errno == EINTR)
                continue;
            dap_tls_worker = NULL;
            return;
        }
        for (int i = 0; i < l_n; ++i) {
            dap_conn_t *l_c = l_evs[i].data.ptr;
            uint32_t l_ev = l_evs[i].events;

            if (l_ev & EPOLLERR) {
                int l_err = 0;
                socklen_t l_len = sizeof(l_err);
                if (l_c->fd >= 0 && getsockopt(l_c->fd, SOL_SOCKET, SO_ERROR, &l_err, &l_len) < 0)
                    l_err = errno;
                if (!l_err)
                    l_err = EIO;
                s_worker_conn_fail(l_c, 0, l_err);
                continue;
            }
            if ((l_ev & EPOLLOUT) && !(dap_conn_state(l_c) & DAP_CONN_CLOSED)) {
                if (l_c->write_cb)
                    l_c->write_cb(l_c);
                else if (l_c->send_olb) { /* flush processor-written send buffer */
                    size_t l_sent = dap_worker_tx_flush(l_c);
                    dap_stat(a_w->stats, send_bytes, += l_sent);
                }
            }
            if ((l_ev & (EPOLLIN | EPOLLRDHUP | EPOLLHUP)) && l_c->read_cb) {
                if (l_c->olb) {
                    uint8_t l_st = dap_conn_state(l_c);
                    if (l_st & (DAP_CONN_SUSPENDED | DAP_CONN_CLOSED | DAP_CONN_RECV_DONE))
                        continue;
                    if ((l_st & DAP_CONN_SYNC) && !dap_conn_sync_ready(l_c)) {
                        dap_slab_bits_set(a_w->pending_bits,
                                          dap_conn_slab_idx(a_w->conn_slab, l_c));
                        atomic_fetch_or_explicit(&a_w->wfq_waiting->rescan_mask,
                                                  (uint64_t)1 << a_w->worker_id,
                                                  memory_order_release);
                        dap_vmqueue_mpsc_notify(a_w->wfq);
                        continue; /* ASYNC→SYNC transition: wait for processor drain;
                                     pending_bit ensures drain_pending retries read_cb
                                     after processor's explicit sync-ready wake
                                     (EPOLLET-safe, no lost edge). */
                    }
                }
                l_c->read_cb(l_c);
                if (l_c->send_olb) {
                    size_t l_sent = dap_worker_tx_flush(l_c);
                    dap_stat(a_w->stats, send_bytes, += l_sent);
                }
            }
        }
        if (atomic_load_explicit(&a_w->shutdown, memory_order_acquire)) {
            dap_tls_worker = NULL;
            return;
        }
    }
}
