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
 * @file dap_io_access.c
 * @brief dap_io_conn_open, dap_io_rx_ctx_init, dap_io_rx_bridge, dap_io_timer_cancel_async,
 *  and the internal timer-cancel ctrl message.
 *
 * The internal ctrl-message subtype for timer cancel is defined only in this TU;
 * @ref dap_io_ops.h / @ref dap_io.h do not expose it.
 */
#include "dap_io_ops.h"

#include <errno.h>
#include <stddef.h>
#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <unistd.h>
#endif

_Static_assert(DAP_IO_KIND_COUNT == 3, "s_rx_pull[] must match dap_io_kind_t");

/* ================================================================== */
/*  Internal worker ctrl message: timer cancel                         */
/*  Only produced by dap_io_timer_cancel_async; lives entirely here.   */
/*  Payload: handle only.  The owner is the @a_w argument passed     */
/*  from ctrl drain, not from @c dap_tls_worker.                      */
/* ================================================================== */

DAP_WORKER_MSG_TYPE(dap_worker_ctrl_timer_cancel_t,
    dap_timer_handle_t handle
);

static dap_msg_rc_t
s_worker_ctrl_timer_cancel(dap_worker_t *a_w, dap_worker_msg_t *self)
{
    dap_worker_ctrl_timer_cancel_t *m =
        DAP_MSG_CAST(dap_worker_ctrl_timer_cancel_t, self);
    dap_timer_del(&a_w->timers, m->handle);
    DAP_MSG_FREE(self);
}

/* ================================================================== */
/*  Static rx pulls (kind table) — same TU as dap_io_conn_open         */
/* ================================================================== */

static ssize_t
s_rx_pull_sock(dap_conn_t *a_c, void *a_buf, size_t a_max, void *a_ctx)
{
    (void)a_ctx;
    for (;;) {
        ssize_t n = recv(a_c->fd, a_buf, a_max, 0);
        if (n >= 0)
            return n;
        if (errno == EINTR)
            continue;
        return n;
    }
}

#ifndef DAP_OS_WINDOWS
static ssize_t
s_rx_pull_file(dap_conn_t *a_c, void *a_buf, size_t a_max, void *a_ctx)
{
    (void)a_ctx;
    for (;;) {
        ssize_t n = read((int)a_c->fd, a_buf, a_max);
        if (n >= 0)
            return n;
        if (errno == EINTR)
            continue;
        return n;
    }
}
#endif

static dap_rx_pull_fn
s_rx_pull_for(dap_io_kind_t a_k)
{
    static const dap_rx_pull_fn s_rx_pull[] = {
        s_rx_pull_sock,
#ifndef DAP_OS_WINDOWS
        s_rx_pull_file,
#else
        NULL,
#endif
        NULL
    };
    if ((unsigned)a_k >= DAP_IO_KIND_COUNT)
        return NULL;
    return s_rx_pull[(unsigned)a_k];
}

void
dap_io_rx_ctx_init(dap_io_rx_ctx_t *a_rx, dap_io_kind_t a_kind)
{
    a_rx->pull = s_rx_pull_for(a_kind);
    a_rx->pull_ctx = NULL;
}

/* ================================================================== */
/*  Per-connection open                                                */
/* ================================================================== */

void
dap_io_rx_bridge(dap_conn_t *a_c)
{
    dap_io_olb_ext_t *l_e;
    dap_io_rx_ctx_t  *l_rx;
    l_e = a_c->ext;
    if (!l_e) return;
    l_rx = &l_e->rx;
    if (!dap_io_olb_ext_is_ready(l_e) || !l_rx->pull) return;
    dap_worker_rx_olb(a_c, l_e->parser, l_rx->pull, l_rx->pull_ctx);
}

size_t
dap_io_olb_parse_span(dap_conn_t *a_c, dap_io_olb_parser_t *a_p)
{
    if (!a_p->span_parse)
        return 0;
    dap_vmqueue_olb_t *l_olb = a_c->olb;
    uint64_t          l_we = l_olb->write_end;
    uint64_t          l_t = a_p->tail;
    if (l_t > l_we) {
        l_olb->bytes_needed = 0;
        a_p->tail = l_we;
        return 0;
    }
    size_t       l_span = (size_t)(l_we - l_t);
    const char  *l_data = l_olb->data + l_t;
    dap_io_parse_result_t l_r = a_p->span_parse(l_data, l_span, a_p->arg);
    if (l_r.consumed > l_span) {
        l_olb->bytes_needed = 0;
        return 0;
    }
    a_p->tail = l_t + l_r.consumed;
    l_olb->bytes_needed = l_r.bytes_needed;
    return l_r.consumed ? 1u : 0u;
}

static dap_conn_t *
s_io_conn_open_impl(dap_io_t *a_io, unsigned a_worker_id, dap_io_kind_t a_kind,
                    dap_fd_t a_fd, size_t a_olb_cap, dap_io_rx_ctx_t *a_rx,
                    dap_conn_read_cb_t a_read_cb, void *a_ext, size_t a_max_frame,
                    dap_conn_ext_dtor_t a_ext_dtor)
{
    if (!a_io || a_worker_id >= a_io->n_workers)
        return NULL;
#ifdef DAP_OS_WINDOWS
    if (a_fd == (dap_fd_t)-1)
        return NULL;
#else
    if (a_fd < 0)
        return NULL;
#endif
    if ((unsigned)a_kind >= DAP_IO_KIND_COUNT || a_kind == DAP_IO_TIMER)
        return NULL;
    dap_rx_pull_fn l_pull = s_rx_pull_for(a_kind);
    if (!l_pull)
        return NULL;
    dap_io_rx_ctx_t *l_targ = a_rx ? a_rx : (a_ext ? (dap_io_rx_ctx_t *)a_ext : NULL);
    if (a_read_cb == dap_io_rx_bridge) {
        dap_io_olb_ext_t *l_e = (dap_io_olb_ext_t *)a_ext;
        if (!dap_io_olb_ext_is_ready(l_e) || !l_targ)
            return NULL;
    }
    if (l_targ) {
        l_targ->pull = l_pull;
        l_targ->pull_ctx = NULL;
    }

    dap_worker_t *l_w = &a_io->workers[a_worker_id];

    size_t l_olb_cap = a_olb_cap ? a_olb_cap : DAP_IO_OLB_MIN_CAP;
    if (l_olb_cap < DAP_IO_OLB_MIN_CAP)
        l_olb_cap = DAP_IO_OLB_MIN_CAP;

    unsigned l_occ = dap_conn_slab_occupancy(a_io->slab);
    unsigned l_max = a_io->slab->max_slots;
    if (l_occ >= l_max * 3 / 4) {
        size_t l_down = l_olb_cap >> 2;
        l_olb_cap = l_down > DAP_IO_OLB_MIN_CAP ? l_down : DAP_IO_OLB_MIN_CAP;
    } else if (l_occ >= l_max / 2) {
        size_t l_down = l_olb_cap >> 1;
        l_olb_cap = l_down > DAP_IO_OLB_MIN_CAP ? l_down : DAP_IO_OLB_MIN_CAP;
    }

    dap_vmqueue_olb_t *l_recv = dap_vmqueue_olb_create(l_olb_cap, false);
    if (!l_recv) return NULL;
    dap_vmqueue_olb_t *l_send = dap_vmqueue_olb_create(l_olb_cap, false);
    if (!l_send) { dap_vmqueue_olb_destroy(l_recv); return NULL; }

    if (a_max_frame) {
        dap_vmqueue_olb_set_threshold(l_recv, a_max_frame);
        dap_vmqueue_olb_set_threshold(l_send, a_max_frame);
    }

    dap_conn_t *l_c = dap_conn_slab_alloc(a_io->slab);
    if (!l_c) {
        dap_vmqueue_olb_destroy(l_recv);
        dap_vmqueue_olb_destroy(l_send);
        return NULL;
    }
    dap_conn_attach(l_c, l_recv, l_send, a_fd);
    if (dap_worker_conn_add(l_w, l_c) < 0) {
        l_c->olb = NULL; l_c->send_olb = NULL;
        dap_vmqueue_olb_destroy(l_recv);
        dap_vmqueue_olb_destroy(l_send);
        dap_conn_slab_return(a_io->slab, l_c);
        return NULL;
    }
    l_c->ext = a_ext;
    if (a_read_cb) {
        l_c->read_cb = a_read_cb;
        if (dap_worker_conn_arm_read(l_w, l_c) < 0) {
            dap_worker_conn_del(l_w, l_c);
            return NULL;
        }
    }
    if (a_ext_dtor && a_ext)
        l_c->ext_dtor = a_ext_dtor;
    return l_c;
}

dap_conn_t *
dap_io_conn_open(dap_io_t *a_io, unsigned a_worker_id, dap_io_kind_t a_kind,
                 dap_fd_t a_fd, size_t a_olb_cap, dap_io_rx_ctx_t *a_rx,
                 dap_conn_read_cb_t a_read_cb, void *a_ext,
                 size_t a_max_frame)
{
    return s_io_conn_open_impl(a_io, a_worker_id, a_kind, a_fd, a_olb_cap, a_rx, a_read_cb, a_ext,
        a_max_frame, NULL);
}

dap_conn_t *
dap_io_conn_open_with_ext_dtor(dap_io_t *a_io, unsigned a_worker_id, dap_io_kind_t a_kind,
                               dap_fd_t a_fd, size_t a_olb_cap, dap_io_rx_ctx_t *a_rx,
                               dap_conn_read_cb_t a_read_cb, void *a_ext, size_t a_max_frame,
                               dap_conn_ext_dtor_t a_ext_dtor)
{
    if (a_ext_dtor && !a_ext)
        return NULL;
    return s_io_conn_open_impl(a_io, a_worker_id, a_kind, a_fd, a_olb_cap, a_rx, a_read_cb, a_ext,
        a_max_frame, a_ext_dtor);
}

dap_conn_t *
dap_io_conn_open_cfg(const dap_io_conn_cfg_t *a_cfg)
{
    if (!a_cfg || (a_cfg->ext_dtor && !a_cfg->ext))
        return NULL;
    return s_io_conn_open_impl(a_cfg->io, a_cfg->worker_id, a_cfg->kind, a_cfg->fd, a_cfg->olb_cap,
        a_cfg->rx, a_cfg->read_cb, a_cfg->ext, a_cfg->max_frame, a_cfg->ext_dtor);
}

/* ================================================================== */
/*  Timer cancel — user action, routed to the owning thread            */
/* ================================================================== */

bool
dap_io_timer_cancel_async(dap_io_t *a_io, dap_timer_handle_t a_h)
{
    if (!a_h) return false;
    uint8_t l_pidx  = dap_timer_handle_proc(a_h);
    uint8_t l_wslot = dap_timer_handle_worker(a_h);
    if (l_pidx >= a_io->n_procs) return false;

    if (l_wslot == DAP_TIMER_SLOT_PROC) {
        dap_proc_ctx_t *l_p = &a_io->procs[l_pidx];
        return dap_wfq_post_timer_cancel(l_p->wfq, 0,
                                          &l_p->timers, a_h);
    }
    /* w = slot * n_procs + proc */
    unsigned l_gw = (unsigned)l_wslot * a_io->n_procs + l_pidx;
    if (l_gw >= a_io->n_workers) return false;
    dap_worker_t *l_w = &a_io->workers[l_gw];

    dap_worker_ctrl_timer_cancel_t *l_m =
        DAP_WORKER_MSG_ALLOC(dap_worker_ctrl_timer_cancel_t,
                             s_worker_ctrl_timer_cancel);
    if (!l_m) return false;
    l_m->handle = a_h;
    dap_worker_post_ctrl(l_w, &l_m->_msg);
    return true;
}
