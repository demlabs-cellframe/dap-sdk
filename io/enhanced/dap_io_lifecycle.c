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
 * @file dap_io_lifecycle.c
 * @brief dap_io_create, dap_io_destroy, dap_io_shutdown (cold path, once per process or shutdown).
 */
#include "dap_io.h"

dap_io_t *dap_io_create(unsigned a_nw, unsigned a_np)
{
    if (!a_np || a_np > a_nw) return NULL;
    unsigned l_pp = (a_nw + a_np - 1) / a_np;
    if (l_pp > DAP_MAX_WORKERS_PER_PROC) return NULL;

    /* Single allocation: dap_io_t + wfqs[] + wfq_waits[] tail-packed */
    size_t l_sz = sizeof(dap_io_t)
                + a_np * sizeof(dap_vmqueue_mpsc_t *)
                + a_np * sizeof(dap_wfq_wait_state_t);
    dap_io_t *l_io = calloc(1, l_sz);
    if (!l_io) return NULL;
    l_io->n_workers = a_nw;
    l_io->n_procs   = a_np;
    l_io->per_proc  = l_pp;
    l_io->wfqs      = (dap_vmqueue_mpsc_t **)(l_io + 1);
    l_io->wfq_waits = (dap_wfq_wait_state_t *)((char *)l_io->wfqs
                        + a_np * sizeof(dap_vmqueue_mpsc_t *));
    for (unsigned p = 0; p < a_np; ++p) {
        atomic_init(&l_io->wfq_waits[p].rescan_mask, 0);
    }

    l_io->slab = dap_conn_slab_create(DAP_CONN_SLAB_MAX, sizeof(dap_conn_t));
    if (!l_io->slab) goto fail;

    for (unsigned p = 0; p < a_np; ++p) {
        l_io->wfqs[p] = dap_wfq_create_standard(
            l_pp, DAP_WFQ_CAP_FAST, DAP_WFQ_CAP_NORM, DAP_WFQ_CAP_BG);
        if (!l_io->wfqs[p]) goto fail;
    }

    /* --- Workers (cache-line aligned array) --- */
    {
        size_t l_wsz = DAP_ALIGN_UP(a_nw * sizeof(dap_worker_t),
                                     DAP_VMQ_CACHELINE);
        l_io->workers = aligned_alloc(DAP_VMQ_CACHELINE, l_wsz);
        if (!l_io->workers) goto fail;
        memset(l_io->workers, 0, l_wsz);
#ifndef DAP_OS_WINDOWS
        for (unsigned w = 0; w < a_nw; ++w)
            l_io->workers[w].epfd = -1;
#endif
    }
    /* Round-robin: w → proc[w % M], worker slot = w / M within that proc */
    for (unsigned w = 0; w < a_nw; ++w) {
        dap_worker_t *l_w = &l_io->workers[w];
        unsigned l_proc = w % a_np;
        unsigned l_slot = w / a_np;
        l_w->conn_slab   = l_io->slab;
        l_w->wfq         = l_io->wfqs[l_proc];
        l_w->wfq_waiting = &l_io->wfq_waits[l_proc];
        l_w->worker_id   = l_slot;
        l_w->n_workers   = l_pp;
        l_w->proc_idx    = (uint8_t)l_proc;
        l_w->conn_head   = UINT16_MAX;
        dap_worker_set_lanes(l_w);
        if (dap_worker_init(l_w) < 0) goto fail;
    }

    /* --- Processors --- */
    l_io->procs = calloc(a_np, sizeof(dap_proc_ctx_t));
    if (!l_io->procs) goto fail;
    for (unsigned p = 0; p < a_np; ++p) {
        dap_proc_ctx_t *l_p = &l_io->procs[p];
        l_p->wfq         = l_io->wfqs[p];
        l_p->wfq_waiting = &l_io->wfq_waits[p];
        l_p->shutdown    = &l_io->shutdown;
        l_p->slab        = l_io->slab;
        l_p->n_workers   = l_pp;
        l_p->proc_idx    = (uint8_t)p;
        l_p->ext_stack   = (dap_msg_stack_t)DAP_MSG_STACK_INIT;
        dap_timers_init(&l_p->timers);
    }
    /* Fill per-worker kick targets so processor can signal "space available" */
    for (unsigned w = 0; w < a_nw; ++w) {
        dap_worker_t *l_w = &l_io->workers[w];
        dap_proc_ctx_t   *l_p = &l_io->procs[w % a_np];
#ifdef DAP_OS_WINDOWS
        l_p->worker_kicks[l_w->worker_id] = l_w->iocp;
#else
        l_p->worker_kick_fds[l_w->worker_id] = l_w->resume_conn->fd;
#endif
    }

#ifdef DAP_IO_STATS
    {
        size_t l_wsz = DAP_ALIGN_UP(a_nw * sizeof(dap_worker_stats_t),
                                     DAP_VMQ_CACHELINE);
        l_io->worker_stats = aligned_alloc(DAP_VMQ_CACHELINE, l_wsz);
        if (!l_io->worker_stats) goto fail;
        memset(l_io->worker_stats, 0, l_wsz);
        for (unsigned w = 0; w < a_nw; ++w)
            l_io->workers[w].stats = &l_io->worker_stats[w];

        size_t l_psz = DAP_ALIGN_UP(a_np * sizeof(dap_proc_stats_t),
                                     DAP_VMQ_CACHELINE);
        l_io->proc_stats = aligned_alloc(DAP_VMQ_CACHELINE, l_psz);
        if (!l_io->proc_stats) goto fail;
        memset(l_io->proc_stats, 0, l_psz);
        for (unsigned p = 0; p < a_np; ++p)
            l_io->procs[p].stats = &l_io->proc_stats[p];
    }
#endif

    return l_io;

fail:
    dap_io_destroy(l_io);
    return NULL;
}

void dap_io_destroy(dap_io_t *a_io)
{
    if (!a_io) return;
#ifdef DAP_IO_STATS
    free(a_io->worker_stats);
    free(a_io->proc_stats);
#endif
    if (a_io->workers) {
        for (unsigned w = 0; w < a_io->n_workers; ++w)
            dap_worker_cleanup(&a_io->workers[w]);
        free(a_io->workers);
    }
    if (a_io->procs) {
        for (unsigned p = 0; p < a_io->n_procs; ++p) {
            dap_proc_defer_queue_clear(&a_io->procs[p].defer_q);
            dap_timers_destroy(&a_io->procs[p].timers);
        }
        free(a_io->procs);
    }
    for (unsigned p = 0; p < a_io->n_procs; ++p)
        if (a_io->wfqs[p]) dap_vmqueue_mpsc_destroy(a_io->wfqs[p]);
    dap_conn_slab_destroy(a_io->slab);
    free(a_io);
}

void dap_io_shutdown(dap_io_t *a_io)
{
    /* release-store pairs with acquire-load in the processor loop */
    atomic_store_explicit(&a_io->shutdown, true, memory_order_release);
    for (unsigned p = 0; p < a_io->n_procs; ++p)
        dap_proc_shutdown(&a_io->shutdown, a_io->wfqs[p]);
}
