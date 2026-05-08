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
 * @file dap_worker_ipc.c
 * @brief Out-of-line proc/foreign-thread → worker reactor path (kick, batch tail, send notify).
 *
 * Declared in @ref dap_worker_ipc.h.  Same symbol cost as when these lived in
 * dap_worker_*.c; split only for TU size.
 */
#include "dap_worker_reactor.h"
#include "dap_io_send.h"
#include <stdlib.h>
#include <string.h>

void dap_worker_kick(dap_worker_t *a_w)
{
#ifdef DAP_OS_WINDOWS
    dap_conn_kick(a_w->iocp);
#else
    dap_conn_kick(a_w->resume_conn->fd);
#endif
}

void dap_worker_after_batch_processed(dap_conn_t *a_c)
{
    dap_worker_t *l_w = (dap_worker_t *)atomic_load_explicit(
        &a_c->_owner, memory_order_relaxed);
    unsigned l_idx = dap_conn_slab_idx(l_w->conn_slab, a_c);
    if ((dap_conn_state(a_c) & DAP_CONN_SYNC) && dap_conn_sync_ready(a_c)) {
        dap_slab_bits_set(l_w->pending_bits, l_idx);
        dap_worker_kick(l_w);
    } else if (atomic_load_explicit(&a_c->olb->watermark_pending, memory_order_acquire)
               && dap_slab_bits_set_if_new(l_w->pending_bits, l_idx)) {
        dap_worker_kick(l_w);
    }
}

/** @brief Cross-thread Dekker notify: SEND_BUSY + pending_bits + eventfd kick.
 *  Internal — only dap_io_tx_send*() and the processor-side batch pipeline
 *  reach for this.  seq_cst set pairs with dap_worker_tx_flush's seq_cst clear. */
void dap_worker_conn_notify_send(dap_conn_t *a_c)
{
    dap_worker_t *l_w = (dap_worker_t *)atomic_load_explicit(
        &a_c->_owner, memory_order_relaxed);
    uint8_t l_old = atomic_fetch_or_explicit(&a_c->state, DAP_CONN_SEND_BUSY,
                                              memory_order_seq_cst);
    if ((l_old & DAP_CONN_SUSPENDED) || !(l_old & DAP_CONN_SEND_BUSY)) {
        dap_slab_bits_set(l_w->pending_bits,
                          dap_conn_slab_idx(l_w->conn_slab, a_c));
        dap_worker_kick(l_w);
    }
}

/* ================================================================== */
/*  Cross-thread send: ctrl message to owner thread                    */
/*  Allocation failure -> DAP_SEND_OVERFLOW.  Oversize vs send_olb is only  */
/*  decided on the owner in send_direct after dequeue; not surfaced here.   */
/* ================================================================== */

typedef struct dap_io_tx_send_msg {
    dap_worker_msg_t   _msg;
    dap_conn_handle_t  h;
    size_t             len;
} dap_io_tx_send_msg_t;

static dap_msg_rc_t
s_dap_io_tx_send_msg_exec(dap_worker_t *a_w, dap_worker_msg_t *a_self)
{
    (void)a_w;
    dap_io_tx_send_msg_t *l_m = (dap_io_tx_send_msg_t *)a_self;
    dap_conn_t *l_c = dap_conn_resolve(l_m->h);
    if (l_c) {
        const void *l_p = (const char *)l_m + sizeof(*l_m);
        (void)dap_io_tx_send_direct(l_c, l_p, l_m->len);
    }
    DAP_MSG_FREE(a_self);
}

dap_send_rc_t dap_worker_ctrl_send(dap_worker_t *a_owner,
                                            dap_conn_handle_t a_h,
                                            const void *a_data, size_t a_len)
{
    if (__builtin_expect(!a_owner, 0))
        return DAP_SEND_CLOSED;
    dap_io_tx_send_msg_t *l_m =
        (dap_io_tx_send_msg_t *)calloc(1, sizeof(*l_m) + a_len);
    if (!l_m) return DAP_SEND_OVERFLOW;
    l_m->_msg.execute = s_dap_io_tx_send_msg_exec;
    l_m->h   = a_h;
    l_m->len = a_len;
    if (a_len) memcpy((char *)l_m + sizeof(*l_m), a_data, a_len);
    dap_worker_post_ctrl(a_owner, &l_m->_msg);
    return DAP_SEND_OK;
}
