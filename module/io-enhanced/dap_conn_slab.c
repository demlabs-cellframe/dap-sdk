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
 * @file dap_conn_slab.c
 * @brief Slab lifecycle (create / destroy / per-slot cleanup).
 *
 * The hot path — alloc / free / return / drain / accessors — stays
 * inline in dap_conn.h because each is a handful of atomic ops with
 * no syscalls.  The routines here are invoked at most once per
 * process (create/destroy) or per connection close (slot_cleanup),
 * and each touches either mmap/munmap or a user-supplied destructor,
 * so call overhead is strictly dominated by the actual work.
 */

#include "dap_conn.h"

/* ================================================================== */
/*  Slab create / destroy                                              */
/* ================================================================== */

dap_conn_slab_t *
dap_conn_slab_create(unsigned a_max, size_t a_slot_size)
{
    if (a_max > DAP_CONN_SLAB_MAX) return NULL;
    size_t l_ss    = DAP_ALIGN_UP(a_slot_size, DAP_VMQ_CACHELINE);
    size_t l_raw   = sizeof(dap_conn_slab_t) + a_max * l_ss;

#ifdef DAP_OS_WINDOWS
    size_t l_total = DAP_ALIGN_UP(l_raw, DAP_VMQ_CACHELINE);
    dap_conn_slab_t *l_s = aligned_alloc(DAP_VMQ_CACHELINE, l_total);
    if (!l_s) return NULL;
    memset(l_s, 0, l_total);
    l_s->map_bytes = 0;  /* signals free() in destroy */
#else
    /* Page-align the mmap request: mmap would round up internally
     * anyway, but tracking the exact size keeps munmap() honest. */
    size_t l_total = DAP_ALIGN_UP(l_raw, (size_t)dap_pagesize());
    void *l_raw_mem = mmap(NULL, l_total,
                            PROT_READ | PROT_WRITE,
                            MAP_ANONYMOUS | MAP_PRIVATE,
                            -1, 0);
    if (l_raw_mem == MAP_FAILED) return NULL;
    dap_conn_slab_t *l_s = (dap_conn_slab_t *)l_raw_mem;
    /* mmap guarantees zero-filled pages; do NOT memset — it would force
     * every slot page to commit immediately and defeat the whole point.
     * Only fields that need non-zero initial values are touched below. */
    l_s->map_bytes = l_total;
#endif

    l_s->slot_size  = l_ss;
    l_s->max_slots  = a_max;
    for (unsigned i = 0; i < a_max; ++i) {
        l_s->queue[i].idx = (uint16_t)i;
        atomic_init(&l_s->queue[i].wfq_seq, (uint64_t)-1);
    }
    atomic_init(&l_s->wfq_epoch, (uint64_t)0);
    atomic_init(&l_s->q_head,  0);
    atomic_init(&l_s->q_ready, a_max);
    atomic_init(&l_s->q_tail,  a_max);
    return l_s;
}

/**
 * @brief Release resources owned by a connection slot (ext, OLBs).
 *
 * Called from dap_conn_slab_drain (per-conn close) and from
 * dap_conn_slab_destroy (per-slab teardown).  Both sites are cold
 * enough that the call overhead is irrelevant next to the OLB munmap
 * and the user-provided ext_dtor.
 */
void dap_conn_slot_cleanup(dap_conn_t *a_c)
{
    if (a_c->ext_dtor && a_c->ext) a_c->ext_dtor(a_c->ext);
    a_c->ext = NULL; a_c->ext_dtor = NULL;
    if (a_c->olb) { dap_vmqueue_olb_destroy(a_c->olb); a_c->olb = NULL; }
    if (a_c->send_olb) { dap_vmqueue_olb_destroy(a_c->send_olb); a_c->send_olb = NULL; }
}

void dap_conn_slab_destroy(dap_conn_slab_t *a_s)
{
    if (!a_s) return;
    /* A slot whose generation has never been bumped (generation == 0)
     * was never allocated — its backing page is almost certainly still
     * the kernel's zero-page.  Reading generation from it only triggers
     * a minor fault to the shared zero-page, not a real CoW, so the
     * scan stays cheap even when the slab is mostly idle.  Non-zero
     * generation flags a slot that once held resources; re-running
     * slot_cleanup is idempotent because it null-checks olb/ext before
     * touching them. */
    for (unsigned i = 0; i < a_s->max_slots; ++i) {
        dap_conn_t *l_c = dap_conn_slab_slot(a_s, i);
        if (atomic_load_explicit(&l_c->generation, memory_order_relaxed) != 0)
            dap_conn_slot_cleanup(l_c);
    }
#ifdef DAP_OS_WINDOWS
    free(a_s);
#else
    munmap(a_s, a_s->map_bytes);
#endif
}
