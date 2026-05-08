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
 * @file dap_vmqueue_olb.h
 * @brief Oversized Linear Buffer — cursor management and compaction.
 *
 * Core data structure for inbound and outbound linear buffers.
 * Network I/O functions (send/flush) live in dap_send_olb.h.
 *
 * recv_olb layout (cursor ordering: head <= ack <= tail <= write_end):
 *
 *   0      head_pos   ack_pos    tail_pos   write_end    capacity
 *   |         |          |          |           |            |
 *   v         v          v          v           v            v
 *   +---------+----------+----------+-----------+------------+
 *   |  freed  | ack pend | readable | partial   |    free    |
 *   +---------+----------+----------+-----------+------------+
 *
 *   freed    — [0, head): consumed+acked, reclaimable by compaction
 *   ack pend — [head, ack): processor acked, worker hasn't applied yet
 *   readable — [ack, tail): committed frames, visible to processor
 *   partial  — [tail, write_end): incomplete frame, not yet committed
 *   free     — [write_end, capacity): recv() target area
 *
 * send_olb layout (no ack_pos; cursor ordering: head <= tail <= write_end):
 *
 *   0      head_pos              tail_pos   write_end    capacity
 *   |         |                     |           |            |
 *   v         v                     v           v            v
 *   +---------+---------------------+-----------+------------+
 *   |  sent   |      sendable       | partial   |    free    |
 *   +---------+---------------------+-----------+------------+
 *
 * Compaction (recv_olb, worker-only, requires head >= tail):
 *
 *   0  head=tail  write_end  cap       0 write_end'     capacity
 *   |      |         |        |        |     |             |
 *   +------+---------+--------+  →     +-----+-------------+
 *   | dead | partial  | free  |        |part |    free     |
 *   +------+---------+--------+        +-----+-------------+
 *   memmove partial frame to offset 0, reset all cursors.
 *   Clean reset (tail == write_end): no memmove needed.
 *
 *   Watermark (free < threshold, can't compact — processor behind):
 *     free >= threshold/2 → set watermark_pending, return OK
 *     free <  threshold/2 → return FULL (Dekker SUSPENDED path)
 *
 * Usage patterns:
 *
 *   recv_olb (worker fills, processor reads):
 *     Worker:    try_space → advance_write → commit tail_pos
 *     Processor: readable [ack_pos, tail_pos) → ack
 *     Worker:    apply_ack → head_pos = ack_pos → compact if needed
 *     Dekker:    CONN_SUSPENDED (seq_cst) <-> ack_pos (seq_cst), external to OLB
 *
 *   send_olb (processor fills, worker flushes):
 *     Processor: dap_send_olb_write (dap_send_olb.h) — advances write_end/tail_pos
 *     Worker:    s_olb_snapshot + consume → advances head_pos
 *     No Dekker needed (backpressure via defer + kick).
 *
 * Cursors:
 *   tail_pos  (atomic) — committed frame boundary, published by writer (release)
 *   head_pos  (atomic) — freed-up-to position, advanced by consumer
 *   ack_pos   (atomic) — processor's consumed-up-to cursor (recv_olb only, seq_cst)
 *   write_end (local)  — raw bytes written, includes partial frames, not atomic
 */
#pragma once

#include "dap_io_plat.h"

/* ================================================================== */
/*  Constants                                                           */
/* ================================================================== */

#ifndef DAP_VMQ_CACHELINE
#  define DAP_VMQ_CACHELINE 64
#endif


/* ================================================================== */
/*  OLB structure                                                       */
/* ================================================================== */

typedef struct dap_vmqueue_olb {
    /* --- cacheline 0: writer domain (single producer) --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint64_t)  tail_pos;       /* committed boundary; writer stores (release), consumer loads (acquire) */
                 uint64_t            write_end;                     /* local to writer */
                 _Atomic(uint32_t)   compacted;                     /* send_olb: processor→worker flag, signals cursor wrap-around */
    /* --- cacheline 1: consumer domain (single consumer) --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint64_t)  head_pos;       /* freed-up-to; written by consumer, read by writer for space check */
                 size_t              capacity;
                 size_t              compact_threshold;              /* watermark: compact when capacity - write_end < threshold */
                 size_t              memmove_compacts;
                 size_t              early_compacts;                 /* compacts triggered proactively at watermark */
                 size_t              bytes_needed;                   /* parser hint: min bytes for next frame, limits recv to avoid partial spillover */
                 char               *data;
    /* --- cacheline 2: Dekker rendezvous (recv_olb only) --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint64_t)  ack_pos;        /* processor's consumed-up-to cursor; seq_cst for Dekker with CONN_SUSPENDED */
                 _Atomic(bool)       watermark_pending;              /* worker → processor: request compact kick after next ack */
} dap_vmqueue_olb_t;

dap_vmqueue_olb_t *dap_vmqueue_olb_create(size_t a_capacity, bool a_huge);
void               dap_vmqueue_olb_destroy(dap_vmqueue_olb_t *a_q);

/** @brief Set watermark: 2× max frame guarantees space for one full frame after compact. */
DAP_STATIC_INLINE void
dap_vmqueue_olb_set_threshold(dap_vmqueue_olb_t *a_q, size_t a_max_frame)
{
    a_q->compact_threshold = a_max_frame ? a_max_frame * 2 : a_q->capacity / 8;
}

/* ================================================================== */
/*  Stat macros (active only when DAP_BENCH is defined)                 */
/* ================================================================== */

#ifdef DAP_BENCH
#  define DAP_STAT_INC(a_p)       do { if (a_p) ++(*(a_p)); } while (0)
#  define DAP_STAT_ADD(a_p, a_v)  do { if (a_p) (*(a_p)) += (a_v); } while (0)
#  define DAP_STAT_FIELD(a_type, a_name)  a_type a_name
#else
#  define DAP_STAT_INC(a_p)       ((void)0)
#  define DAP_STAT_ADD(a_p, a_v)  ((void)(a_v))
#  define DAP_STAT_FIELD(a_type, a_name)
#endif

/* ================================================================== */
/*  recv_olb: worker side (non-blocking, event loop)                    */
/* ================================================================== */

/** @brief Advance write cursor after recv(). Does not publish data to consumer. */
DAP_STATIC_INLINE void dap_vmqolb_advance_write(dap_vmqueue_olb_t *a_q, size_t a_n)
{
    a_q->write_end += a_n;
}

/**
 * @brief Apply processor's ack to head_pos, freeing recv buffer space.
 * @return true if head_pos was advanced.
 */
DAP_STATIC_INLINE bool dap_vmqolb_apply_ack(dap_vmqueue_olb_t *a_q)
{
    uint64_t l_ack  = atomic_load_explicit(&a_q->ack_pos,  memory_order_seq_cst); /* Dekker pair with dap_vmqolb_ack()'s seq_cst store */
    uint64_t l_head = atomic_load_explicit(&a_q->head_pos, memory_order_relaxed);
    if (l_ack <= l_head)
        return false;
    atomic_store_explicit(&a_q->head_pos, l_ack, memory_order_relaxed);
    return true;
}

typedef enum {
    DAP_OLB_OK,         /* space available at write_end */
    DAP_OLB_COMPACTED,  /* compaction freed space (memmove happened) */
    DAP_OLB_FULL        /* no space, head < tail (processor hasn't acked) */
} dap_olb_space_t;

/**
 * @brief Compact recv buffer: move [tail, write_end) to offset 0, reset cursors.
 *
 * Precondition: head >= tail (all committed data consumed).
 * If tail == write_end (no partial frame), clean reset without memmove.
 */
DAP_STATIC_INLINE void
s_recv_compact(dap_vmqueue_olb_t *a_q, uint64_t a_tail,
               char **a_ptr, size_t *a_avail)
{
    size_t l_partial = a_q->write_end - (size_t)a_tail;
    if (l_partial) {
        memmove(a_q->data, a_q->data + (size_t)a_tail, l_partial);
        ++a_q->memmove_compacts;
    }
    a_q->write_end = l_partial;
    a_q->bytes_needed = 0;
    /* Relaxed ok: worker is sole writer during compact.
     * tail_pos release publishes memmoved data and resets to consumer. */
    atomic_store_explicit(&a_q->watermark_pending, false, memory_order_relaxed);
    atomic_store_explicit(&a_q->head_pos, 0, memory_order_relaxed);
    atomic_store_explicit(&a_q->ack_pos,  0, memory_order_relaxed);
    atomic_store_explicit(&a_q->tail_pos, 0, memory_order_release);
    *a_avail = a_q->capacity - l_partial;
    *a_ptr   = a_q->data + l_partial;
}

/**
 * @brief Check available recv space, compact proactively at the watermark.
 *
 *   free >= threshold             → OK (fast path)
 *   free <  threshold             → watermark zone:
 *     apply_ack, then:
 *     head >= tail && tail > 0    → s_recv_compact → COMPACTED (FULL if still no space)
 *     else (processor behind):
 *       free >= threshold/2       → OK + set watermark_pending
 *       free <  threshold/2       → FULL (Dekker SUSPENDED path)
 *
 * @return ok / compacted / full.
 */
DAP_STATIC_INLINE dap_olb_space_t
dap_vmqolb_try_space(dap_vmqueue_olb_t *a_q, char **a_ptr, size_t *a_avail)
{
    *a_avail = a_q->capacity - a_q->write_end;
    *a_ptr   = a_q->data + a_q->write_end;
    if (__builtin_expect(*a_avail >= a_q->compact_threshold, 1))
        return DAP_OLB_OK;

    /* Watermark zone: apply latest processor ack, attempt early compact */
    dap_vmqolb_apply_ack(a_q);
    uint64_t l_tail = atomic_load_explicit(&a_q->tail_pos, memory_order_relaxed);
    uint64_t l_head = atomic_load_explicit(&a_q->head_pos, memory_order_relaxed);
    if (l_head >= l_tail && l_tail) {
        s_recv_compact(a_q, l_tail, a_ptr, a_avail);
        ++a_q->early_compacts;
        if (__builtin_expect(!*a_avail, 0))
            return DAP_OLB_FULL;
        return DAP_OLB_COMPACTED;
    }

    /* Cannot compact (processor behind or no committed data).
     * If parser already knows exact bytes_needed and they fit, allow recv
     * even below watermark to avoid false FULL stalls near compact threshold. */
    if (a_q->bytes_needed && *a_avail >= a_q->bytes_needed)
        return DAP_OLB_OK;

    /* Still usable space for at least one frame below watermark? */
    if (*a_avail >= a_q->compact_threshold / 2) {
        atomic_store_explicit(&a_q->watermark_pending, true, memory_order_release);
        return DAP_OLB_OK;
    }
    return DAP_OLB_FULL;
}

/* ================================================================== */
/*  recv_olb: processor side                                            */
/* ================================================================== */

/**
 * @brief Get the committed data range [ack_pos, tail_pos) for the processor.
 * @return true if there is data to read.
 */
DAP_STATIC_INLINE bool dap_vmqolb_readable(dap_vmqueue_olb_t *a_q,
                                            char **a_ptr, size_t *a_size)
{
    uint64_t l_tail = atomic_load_explicit(&a_q->tail_pos, memory_order_acquire); /* acquire: pairs with worker's release on commit and compact */
    uint64_t l_ack  = atomic_load_explicit(&a_q->ack_pos,  memory_order_relaxed);
    *a_size = (l_tail > l_ack) ? (size_t)(l_tail - l_ack) : 0;
    *a_ptr  = a_q->data + (size_t)l_ack;
    return *a_size > 0;
}

/** @brief Acknowledge consumed bytes on the processor side. */
DAP_STATIC_INLINE void dap_vmqolb_ack(dap_vmqueue_olb_t *a_q, size_t a_nbytes)
{
    uint64_t l_ack = atomic_load_explicit(&a_q->ack_pos, memory_order_relaxed);
    atomic_store_explicit(&a_q->ack_pos, l_ack + a_nbytes, memory_order_seq_cst); /* seq_cst: Dekker pair with apply_ack; orders with CONN_SUSPENDED check */
}

/** @brief SYNC-mode inline consume: advance all three OLB cursors to @a a_pos.
 *
 *  In SYNC the worker is both producer and consumer.  Keeping head == ack == tail
 *  lets try_space compact freely and keeps dap_conn_sync_ready() == true on the
 *  next event-loop iteration.  Worker-thread only — no concurrent readers. */
DAP_STATIC_INLINE void dap_vmqolb_sync_ack(dap_vmqueue_olb_t *a_q, uint64_t a_pos)
{
    atomic_store_explicit(&a_q->ack_pos,  a_pos, memory_order_relaxed);
    atomic_store_explicit(&a_q->head_pos, a_pos, memory_order_relaxed);
    atomic_store_explicit(&a_q->tail_pos, a_pos, memory_order_release);
}

/* ================================================================== */
/*  send_olb: consumer side (worker flushes to socket)                  */
/* ================================================================== */

/**
 * @brief Take a readable snapshot of send_olb, handling wrap-around.
 * @return true if data is available.
 */
DAP_STATIC_INLINE bool s_olb_snapshot(dap_vmqueue_olb_t *a_q,
                                       uint64_t *a_head, size_t *a_avail)
{
    uint64_t l_head = atomic_load_explicit(&a_q->head_pos, memory_order_relaxed);
    uint64_t l_tail = atomic_load_explicit(&a_q->tail_pos, memory_order_acquire);
    if (__builtin_expect(
            atomic_load_explicit(&a_q->compacted, memory_order_acquire), 0)) {
        l_head = 0; /* processor wrapped — reset consumer to match */
        atomic_store_explicit(&a_q->head_pos, 0, memory_order_release);
        atomic_store_explicit(&a_q->compacted, 0, memory_order_release);
        l_tail = atomic_load_explicit(&a_q->tail_pos, memory_order_acquire);
    }
    *a_head  = l_head;
    *a_avail = (l_tail > l_head) ? (size_t)(l_tail - l_head) : 0;
    return *a_avail > 0;
}

/** @brief Advance head_pos after send(), freeing space for the processor writer. */
DAP_STATIC_INLINE void dap_vmqolb_consume(dap_vmqueue_olb_t *a_q, size_t a_nbytes)
{
    uint64_t l_head = atomic_load_explicit(&a_q->head_pos, memory_order_relaxed);
    atomic_store_explicit(&a_q->head_pos, l_head + a_nbytes, memory_order_release);
}

/** @brief Reset send_olb cursors on wrap-around, signal compaction to consumer. */
DAP_STATIC_INLINE void
s_olb_reset(dap_vmqueue_olb_t *a_q, char **a_ptr, size_t *a_avail)
{
    a_q->write_end = 0;
    atomic_store_explicit(&a_q->tail_pos, 0, memory_order_release);
    atomic_store_explicit(&a_q->compacted, 1, memory_order_release);
    *a_avail = a_q->capacity;
    *a_ptr   = a_q->data;
}


