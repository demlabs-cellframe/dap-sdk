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
 * @file dap_worker_win.c
 * @brief Windows IOCP worker: init, main loop, timers.
 */
#include "dap_worker_reactor.h"
#include <windows.h>

#define DAP_IOCP_KEY_CONN   0
#define DAP_IOCP_KEY_RESUME 0xFFFF

/** @brief Windows: create IOCP handle, allocate resume_conn from slab. */
int dap_worker_init(dap_worker_t *a_w)
{
    dap_timers_init(&a_w->timers);
    a_w->iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);
    if (!a_w->iocp) return -1;
    a_w->resume_conn = dap_conn_slab_alloc(a_w->conn_slab);
    if (!a_w->resume_conn) { CloseHandle(a_w->iocp); a_w->iocp = NULL; return -1; }
    a_w->resume_conn->ext = a_w;
    return 0;
}

/** @brief Windows: set _owner, append to conns[] (no IOCP association here). */
int dap_worker_conn_add(dap_worker_t *a_w, dap_conn_t *a_conn)
{
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

/** @brief Windows: unlink from worker-owned list. */
void dap_worker_conn_del(dap_worker_t *a_w, dap_conn_t *a_conn)
{
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
}

/** @brief Windows: close IOCP handle. */
void dap_worker_cleanup(dap_worker_t *a_w)
{
    if (a_w->iocp) { CloseHandle(a_w->iocp); a_w->iocp = NULL; }
}

/** @brief Windows IOCP event loop: GQCS with timer-driven timeout,
 *  fires timers after each wake, drains ctrl_stack on KEY_RESUME. */
void dap_worker_loop(dap_worker_t *a_w)
{
    dap_tls_worker = a_w;    /* publish owner identity for dap_io_tx_send's fast path */
    for (;;) {
        DWORD l_timeout = dap_timers_timeout(&a_w->timers);
        dap_timers_maybe_trim(&a_w->timers);

        DWORD l_bytes = 0;
        ULONG_PTR l_key = 0;
        OVERLAPPED *l_ov = NULL;
        BOOL l_ok = GetQueuedCompletionStatus(a_w->iocp, &l_bytes,
                                               &l_key, &l_ov, l_timeout);
        dap_timers_drain(&a_w->timers);

        if (l_ok && l_key == DAP_IOCP_KEY_RESUME) {
            /* FIFO drain: detach Treiber chain, reverse into submission
             * order — mirror of the Linux s_resume_read path. */
            dap_worker_msg_t *l_chain =
                dap_worker_msg_list_reverse(
                    dap_worker_msg_stack_detach(&a_w->ctrl_stack));
            while (l_chain) {
                dap_worker_msg_t *l_next = l_chain->next;
                dap_msg_rc_t l_rc = l_chain->execute(a_w, l_chain);
                if (l_rc == DAP_MSG_DROP)
                    free(l_chain);
                l_chain = l_next;
            }
            dap_worker_drain_pending(a_w);
        }
        if (atomic_load_explicit(&a_w->shutdown, memory_order_acquire)) {
            dap_tls_worker = NULL;
            return;
        }
    }
}

/* dap_worker_timer_rearm, dap_worker_timer_add, dap_worker_timer_del: dap_worker_timer.c */
