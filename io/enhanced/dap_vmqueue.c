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
 * @file dap_vmqueue.c
 * @brief Allocation / deallocation for SPSC and MPSC queues.
 *
 * All queues are mmap-backed (VirtualAlloc on Windows) with flat
 * MAP_ANONYMOUS allocations — no mirror mapping, no hugepages.
 * Page-aligned buffers avoid heap fragmentation; wrap-around is
 * handled by the gen|offset protocol (see dap_vmqueue.h).
 */

#include "dap_vmqueue.h"

/* ================================================================== */
/*  Platform mmap/munmap wrappers                                      */
/* ================================================================== */

static void *s_mmap_alloc(size_t a_size)
{
#ifdef DAP_OS_WINDOWS
    return VirtualAlloc(NULL, a_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void *l_ptr = mmap(NULL, a_size, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return l_ptr == MAP_FAILED ? NULL : l_ptr;
#endif
}

static void s_mmap_free(void *a_ptr, size_t a_size)
{
    if (!a_ptr) return;
#ifdef DAP_OS_WINDOWS
    (void)a_size;
    VirtualFree(a_ptr, 0, MEM_RELEASE);
#else
    munmap(a_ptr, a_size);
#endif
}

/* ================================================================== */
/*  SPSC message queue                                                 */
/* ================================================================== */

/**
 * @brief Allocate an SPSC queue via a single mmap(MAP_ANONYMOUS).
 *
 * Layout: [dap_vmqueue_t header | data[] FAM] in one contiguous region,
 * page-aligned. Default capacity: Mbytes(1); capped at DAP_VMQ_SPSC_MAX_CAP.
 * No mirror mapping — wrap-around uses gen|offset protocol (see header).
 */
dap_vmqueue_t *dap_vmqueue_create(size_t a_capacity)
{
    if (!a_capacity) a_capacity = Mbytes(1);
    if (a_capacity > DAP_VMQ_SPSC_MAX_CAP)
        a_capacity = DAP_VMQ_SPSC_MAX_CAP;
    size_t l_total = DAP_ALIGN_UP(sizeof(dap_vmqueue_t) + a_capacity, dap_pagesize());
    dap_vmqueue_t *l_q = (dap_vmqueue_t *)s_mmap_alloc(l_total);
    if (!l_q) return NULL;
    atomic_init(&l_q->tail_gen, (uint64_t)0);
    atomic_init(&l_q->head_gen, (uint64_t)0);
    atomic_init(&l_q->producer_waiting, (uint32_t)0);
    atomic_init(&l_q->shutdown, (uint32_t)0);
    l_q->total_size = l_total;
    l_q->capacity   = l_total - offsetof(dap_vmqueue_t, data);
    if (l_q->capacity > DAP_VMQ_SPSC_MAX_CAP)
        l_q->capacity = DAP_VMQ_SPSC_MAX_CAP;
    return l_q;
}

/** @brief Release the single mmap region backing the SPSC queue. */
void dap_vmqueue_destroy(dap_vmqueue_t *a_q)
{
    if (!a_q) return;
    s_mmap_free(a_q, a_q->total_size);
}

/* ================================================================== */
/*  MPSC — per-producer SPSC lanes                                     */
/* ================================================================== */

/**
 * @brief Create an MPSC queue — multi-lane layout in a single flat mmap.
 *
 * @param a_lanes       Number of producer lanes
 * @param a_capacities  Array of per-lane capacities; 0 = flex (share remaining mmap)
 * @param a_tail_reserve Extra mmap pages reserved after the data region
 * @return Allocated MPSC queue or NULL on failure
 *
 * One mmap(MAP_ANONYMOUS) call allocates the entire structure:
 *   [header + lane_off FAM] [ctrl blocks: N×192 B] [data regions] [tail_reserve]
 *    0                ctrl_offset              data_offset         total_size
 *
 * Flex lanes (capacity == 0) split the remaining space equally.
 * On failure (flex lanes too small), munmap is called before returning NULL.
 */
dap_vmqueue_mpsc_t *dap_vmqueue_mpsc_create_ex(unsigned a_lanes,
                                                const size_t *a_capacities,
                                                size_t a_tail_reserve)
{
    dap_return_val_if_pass(!a_lanes || !a_capacities, NULL);
    size_t l_hdr  = DAP_ALIGN_UP(sizeof(dap_vmqueue_mpsc_t)
                                  + (size_t)(a_lanes + 1) * sizeof(size_t), 64);
    size_t l_ctrl = DAP_ALIGN_UP(l_hdr + (size_t)a_lanes * DAP_VMQ_LANE_CTRL_SIZE, 64);
    size_t l_fixed = 0;
    unsigned l_n_flex = 0;
    for (unsigned i = 0; i < a_lanes; ++i) {
        if (a_capacities[i])
            l_fixed += a_capacities[i] > DAP_VMQ_MAX_LANE_CAP
                       ? DAP_VMQ_MAX_LANE_CAP : a_capacities[i];
        else
            ++l_n_flex;
    }
    size_t l_total = DAP_ALIGN_UP(l_ctrl + l_fixed, dap_pagesize());
    if (l_n_flex) {
        size_t l_spare = l_total - l_ctrl - l_fixed;
        if (l_spare < l_n_flex * 4096)
            l_total += DAP_ALIGN_UP(l_n_flex * 4096 - l_spare, dap_pagesize());
    }
    if (a_tail_reserve)
        l_total += DAP_ALIGN_UP(a_tail_reserve, dap_pagesize());
    dap_vmqueue_mpsc_t *l_q = (dap_vmqueue_mpsc_t *)s_mmap_alloc(l_total);
    dap_return_val_if_pass(!l_q, NULL);
    l_q->n_lanes     = a_lanes;
    atomic_init(&l_q->shutdown, (uint32_t)0);
    atomic_init(&l_q->notify_latch, (uint32_t)0);
    l_q->ctrl_offset = l_hdr;
    l_q->data_offset = l_ctrl;
    l_q->total_size  = l_total;
    size_t l_remain = l_total - l_ctrl - l_fixed
                      - DAP_ALIGN_UP(a_tail_reserve, dap_pagesize());
    size_t l_flex_each = l_n_flex ? (l_remain / l_n_flex) & ~(size_t)7 : 0;
    if (l_n_flex && l_flex_each < DAP_VMQ_MIN_LANE_CAP) {
        s_mmap_free(l_q, l_total);  /* munmap: flex lanes can't meet min capacity */
        return NULL;
    }
    if (l_flex_each > DAP_VMQ_MAX_LANE_CAP)
        l_flex_each = DAP_VMQ_MAX_LANE_CAP;
    size_t l_off = 0;
    for (unsigned i = 0; i < a_lanes; ++i) {
        l_q->lane_off[i] = l_off;
        l_off += a_capacities[i]
            ? (a_capacities[i] > DAP_VMQ_MAX_LANE_CAP ? DAP_VMQ_MAX_LANE_CAP : a_capacities[i])
            : l_flex_each;
    }
    l_q->lane_off[a_lanes] = l_off;
    for (unsigned i = 0; i < a_lanes; ++i) {
        atomic_init(s_mpsc_tg(l_q, i), (uint64_t)0);
        atomic_init(s_mpsc_hg(l_q, i), (uint64_t)0);
        atomic_init(s_mpsc_pw(l_q, i), (uint32_t)0);
    }
    return l_q;
}

dap_vmqueue_mpsc_t *dap_vmqueue_mpsc_create(unsigned a_lanes, const size_t *a_capacities)
{
    return dap_vmqueue_mpsc_create_ex(a_lanes, a_capacities, 0);
}

/** @brief Release the single mmap region backing the MPSC queue. */
void dap_vmqueue_mpsc_destroy(dap_vmqueue_mpsc_t *a_q)
{
    if (!a_q) return;
    s_mmap_free(a_q, a_q->total_size);
}

/* Lane pool removed — external threads now use dap_msg_stack_t (Treiber MPSC). */
