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
 * @file dap_vmqueue_olb.c
 * @brief Allocation / deallocation for Oversized Linear Buffer.
 *
 * OLB is a linear mmap buffer (not a ring), used for recv and send paths.
 * No double-mmap mirror trick — wrap-around is handled by cursor compaction
 * (see s_recv_compact / s_olb_reset in dap_vmqueue_olb.h).
 *
 * Two separate mmap regions are allocated: the dap_vmqueue_olb_t struct
 * and the data buffer.  On Linux the data buffer optionally uses
 * MAP_HUGETLB (DAP_HUGEPAGE_SIZE = Mbytes(2)) with fallback to regular pages.
 * Size macros Kbytes()/Mbytes() are defined in dap_io_plat.h.
 */

#include "dap_vmqueue_olb.h"

/**
 * @brief Allocate an OLB: struct + data buffer as two separate mmap regions.
 *
 * @param a_capacity Data buffer size in bytes; 0 defaults to Mbytes(1).
 * @param a_huge     If true (Linux only), attempt MAP_HUGETLB for data;
 *                   falls back to regular pages on failure.
 * @return Allocated OLB or NULL on mmap failure.
 */
dap_vmqueue_olb_t *dap_vmqueue_olb_create(size_t a_capacity, bool a_huge)
{
    size_t l_pgsz = dap_pagesize();
    if (!a_capacity)
        a_capacity = Mbytes(1);

#ifdef DAP_OS_WINDOWS
    (void)a_huge;
    a_capacity = DAP_ALIGN_UP(a_capacity, l_pgsz);
    dap_vmqueue_olb_t *l_q = (dap_vmqueue_olb_t *)VirtualAlloc(
        NULL, DAP_ALIGN_UP(sizeof(*l_q), l_pgsz),
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    dap_return_val_if_pass(!l_q, NULL);
    char *l_data = (char *)VirtualAlloc(NULL, a_capacity,
                                         MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!l_data) {
        VirtualFree(l_q, 0, MEM_RELEASE);
        return NULL;
    }
#else
    a_capacity = a_huge
        ? DAP_ALIGN_UP(a_capacity, DAP_HUGEPAGE_SIZE)
        : DAP_ALIGN_UP(a_capacity, l_pgsz);
    dap_vmqueue_olb_t *l_q = (dap_vmqueue_olb_t *)mmap(
        NULL, DAP_ALIGN_UP(sizeof(*l_q), l_pgsz),
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    dap_return_val_if_pass(l_q == MAP_FAILED, NULL);
    int l_flags = MAP_PRIVATE | MAP_ANONYMOUS;
    char *l_data = MAP_FAILED;
    if (a_huge)  /* hugepage attempt: aligned to DAP_HUGEPAGE_SIZE above */
        l_data = (char *)mmap(NULL, a_capacity, PROT_READ | PROT_WRITE,
                               l_flags | MAP_HUGETLB, -1, 0);
    if (l_data == MAP_FAILED)  /* fallback to regular pages */
        l_data = (char *)mmap(NULL, a_capacity, PROT_READ | PROT_WRITE,
                               l_flags, -1, 0);
    if (l_data == MAP_FAILED) {
        munmap(l_q, DAP_ALIGN_UP(sizeof(*l_q), l_pgsz));  /* cleanup struct mmap */
        return NULL;
    }
#endif
    atomic_init(&l_q->tail_pos,  (uint64_t)0);
    atomic_init(&l_q->head_pos,  (uint64_t)0);
    atomic_init(&l_q->compacted, (uint32_t)0);
    atomic_init(&l_q->ack_pos,   (uint64_t)0);
    atomic_init(&l_q->watermark_pending, false);
    l_q->write_end         = 0;
    l_q->data              = l_data;
    l_q->capacity          = a_capacity;
    l_q->compact_threshold = a_capacity / 8;
    l_q->early_compacts    = 0;
    l_q->bytes_needed      = 0;
    return l_q;
}

/** @brief Release both mmap regions (data buffer + struct). */
void dap_vmqueue_olb_destroy(dap_vmqueue_olb_t *a_q)
{
    if (!a_q) return;
#ifdef DAP_OS_WINDOWS
    VirtualFree(a_q->data, 0, MEM_RELEASE);
    VirtualFree(a_q, 0, MEM_RELEASE);
#else
    munmap(a_q->data, a_q->capacity);
    munmap(a_q, DAP_ALIGN_UP(sizeof(*a_q), dap_pagesize()));
#endif
}
