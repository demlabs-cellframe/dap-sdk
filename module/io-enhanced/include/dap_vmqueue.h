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
 * @file dap_vmqueue.h
 * @brief Advanced/internal — lock-free SPSC / MPSC message queues (mmap-backed, gen|offset wrap).
 *
 * Buffer model: linear mmap(MAP_ANONYMOUS) region, not a circular ring buffer.
 * Wrap-around uses a packed gen|offset uint64_t:
 *   high 32 bits = generation counter,  low 32 bits = byte offset.
 * One atomic store per push (common path); batch drain with prefetch.
 *
 *   SPSC lane -- one writer (producer), one reader (consumer), no CAS:
 *
 *     Producer --push--> [ data[] buffer ] --drain--> Consumer
 *       writes tail_gen                      writes head_gen
 *
 *   Gen|offset buffer (linear, batch-reset wrap-around):
 *
 *     gen=0  [msg0][msg1][msg2][  free  ]   tail.off=120  head.off=40
 *            0    ^head       ^tail     capacity
 *
 *     Wrap condition:  off + msg_size > capacity
 *       head == tail (drained) --> gen++, off=0  (consumer syncs gen on drain)
 *       head != tail (busy)    --> return false   (backpressure)
 *
 *     gen=1  [msg3][msg4][ ... ]              new generation, offset from 0
 *            0          ^tail
 *
 * MPSC: array of SPSC lanes with independent capacity.  Consumer drains
 * all lanes in round-robin.  Per-lane FIFO guaranteed, cross-lane relaxed.
 *
 * Backpressure: push_wait() sets producer_waiting=1 (seq_cst) and sleeps
 * on a futex.  Consumer wakes producer via commit_head / ack_waiter
 * (Dekker pattern to avoid lost wakeups).
 */
#pragma once

#include "dap_io_plat.h"

/* ================================================================== */
/*  Packed gen|offset word — atomic consistency for tail/head           */
/* ================================================================== */

#define DAP_VMQ_GEN_SHIFT   32
#define DAP_VMQ_OFF_MASK    ((uint64_t)(uint32_t)-1)
#define DAP_VMQ_PACK(a_gen, a_off)  \
    (((uint64_t)(a_gen) << DAP_VMQ_GEN_SHIFT) | (uint64_t)(a_off))
#define DAP_VMQ_GEN(a_v)    ((uint32_t)((a_v) >> DAP_VMQ_GEN_SHIFT))
#define DAP_VMQ_OFF(a_v)    ((size_t)((a_v) & DAP_VMQ_OFF_MASK))

/* ================================================================== */
/*  Tunable constants                                                  */
/* ================================================================== */

#define DAP_VMQ_CACHELINE       64
#define DAP_VMQ_LANE_CTRL_SIZE  (3 * DAP_VMQ_CACHELINE)
#define DAP_VMQ_LANE_TG_OFF     0
#define DAP_VMQ_LANE_HG_OFF     DAP_VMQ_CACHELINE
#define DAP_VMQ_LANE_PW_OFF     (2 * DAP_VMQ_CACHELINE)

/* ================================================================== */
/*  Sizing constraints                                                 */
/*                                                                     */
/*  Offset fits in lower 32 bits  =>  lane capacity <= 4 GB.           */
/*  A single message occupies ALIGN_UP(8 + payload, 8) bytes.          */
/*  Lane must hold at least one max-sized message.                     */
/* ================================================================== */

#define DAP_VMQ_MSG_OVERHEAD    sizeof(dap_vmqueue_hdr_t)       /* 8 */
#define DAP_VMQ_MSG_ALIGNED(a_payload_sz) \
    DAP_ALIGN_UP(DAP_VMQ_MSG_OVERHEAD + (a_payload_sz), 8)

#define DAP_VMQ_MAX_LANE_CAP    ((size_t)(uint32_t)-1)
#define DAP_VMQ_MIN_LANE_CAP    ((size_t)256)
#define DAP_VMQ_SPSC_MAX_CAP    Mbytes(64)

/* ================================================================== */
/*  Message header (8 bytes, 8-byte aligned)                           */
/*                                                                     */
/*  Every message in the buffer:   [hdr 8B] [payload 0..N B] [pad].   */
/*  The `type` field enables polymorphic payloads — different message  */
/*  types carry different payload layouts and sizes.  The consumer     */
/*  dispatches on type and casts (hdr+1) to the appropriate struct.    */
/*                                                                     */
/*  Built-in types (see dap_msg_types.h):                              */
/*                                                                     */
/*    DAP_MSG_BATCH    →  dap_batch_task_t     (conn_idx, gen, etc.)   */
/*    DAP_MSG_CALLBACK →  dap_callback_task_t  (fn + arg)             */
/*    DAP_MSG_HEAP     →  dap_heap_task_t      (ptr, len, cleanup)    */
/*    DAP_MSG_TIMER    →  dap_timer_request_t  (delay, interval, etc.)*/
/*                                                                     */
/*  Payload memory is copied into the buffer (value semantics).  For   */
/*  large data, pass a pointer/offset descriptor — the buffer carries  */
/*  the descriptor, not the data itself.                               */
/* ================================================================== */

typedef struct dap_vmqueue_hdr {
    uint8_t  type;          /* DAP_MSG_BATCH / CALLBACK / HEAP / TIMER */
    uint8_t  pri;           /* DAP_WFQ_PRI_NORM / FAST / BG / ... */
    uint32_t total_len;     /* total message size: sizeof(hdr) + payload */
    char     payload[];     /* variable-length payload, 8-byte aligned */
} dap_vmqueue_hdr_t;
_Static_assert(sizeof(dap_vmqueue_hdr_t) == 8, "hdr 8B");
_Static_assert(offsetof(dap_vmqueue_hdr_t, payload) == 8,
               "payload starts at 8B boundary for zero-copy access");

/* ================================================================== */
/*  SPSC message queue                                                 */
/*                                                                     */
/*  dap_vmqueue_t cacheline layout:                                    */
/*    CL0 [  0.. 63] tail_gen   -- written by producer (push)          */
/*    CL1 [ 64..127] head_gen   -- written by consumer (drain)         */
/*    CL2 [128..191] producer_waiting, shutdown, capacity, total_size  */
/*    CL3 [192..   ] data[]     -- message buffer (mmap-backed)        */
/* ================================================================== */

typedef struct dap_vmqueue {
    /* --- cacheline 0: producer-hot (written on every push) --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint64_t) tail_gen;
    /* --- cacheline 1: consumer-hot (written on every drain) --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint64_t) head_gen;
    /* --- cacheline 2: backpressure + cold metadata --- */
    _Alignas(DAP_VMQ_CACHELINE) _Atomic(uint32_t) producer_waiting;
                 _Atomic(uint32_t) shutdown;
                 size_t            capacity;
                 size_t            total_size;
    _Alignas(DAP_VMQ_CACHELINE) char data[];
} dap_vmqueue_t;

_Static_assert(offsetof(dap_vmqueue_t, tail_gen) % DAP_VMQ_CACHELINE == 0, "tail_gen align");
_Static_assert(offsetof(dap_vmqueue_t, head_gen) % DAP_VMQ_CACHELINE == 0, "head_gen align");
_Static_assert(offsetof(dap_vmqueue_t, data)     % DAP_VMQ_CACHELINE == 0, "data align");

/**
 * @brief Non-blocking single-producer push of one message into the lane buffer.
 * @param[in] a_tg Producer tail (packed generation and byte offset).
 * @param[in] a_hg Consumer head (wrap coordination).
 * @param[in] a_data Lane buffer base address.
 * @param[in] a_capacity Lane capacity in bytes.
 * @param[in] a_type Message type identifier (DAP_MSG_BATCH, etc.).
 * @param[in] a_pri  Priority tag stored in hdr.pri (routing hint, not lane selection).
 * @param[in] a_payload Pointer to payload bytes (may be null if size is zero).
 * @param[in] a_payload_size Payload length in bytes.
 * @return True if the message was queued, false if the buffer cannot accept it yet.
 */
DAP_STATIC_INLINE bool s_vmq_push(
    _Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
    char *a_data, size_t a_capacity,
    uint8_t a_type, uint8_t a_pri,
    const void *a_payload, uint32_t a_payload_size)
{
    size_t   l_total   = sizeof(dap_vmqueue_hdr_t) + a_payload_size;
    size_t   l_aligned = DAP_ALIGN_UP(l_total, 8);
    uint64_t l_tg  = atomic_load_explicit(a_tg, memory_order_relaxed);
    size_t   l_off = DAP_VMQ_OFF(l_tg);
    uint32_t l_gen = DAP_VMQ_GEN(l_tg);
    if (__builtin_expect(l_off + l_aligned > a_capacity, 0)) { /* overflow: won't fit */
        if (l_aligned > a_capacity)
            return false;                           /* msg exceeds total lane capacity */
        uint64_t l_hg = atomic_load_explicit(a_hg, memory_order_acquire);
        if (l_hg != l_tg)
            return false;                           /* not drained -- backpressure */
        l_off = 0;                                  /* gen-wrap: reset offset */
        atomic_store_explicit(a_tg, DAP_VMQ_PACK(++l_gen, 0), memory_order_release);
    }
    dap_vmqueue_hdr_t *l_hdr = (dap_vmqueue_hdr_t *)(a_data + l_off);
    l_hdr->type      = a_type;
    l_hdr->pri       = a_pri;
    l_hdr->total_len = (uint32_t)l_total;
    if (a_payload_size)
        memcpy(l_hdr->payload, a_payload, a_payload_size);
    /* Release: header+payload writes visible to consumer before tail advances */
    atomic_store_explicit(a_tg, DAP_VMQ_PACK(l_gen, l_off + l_aligned), memory_order_release);
    return true;
}

/** @brief Optional consumer-wake callback for s_vmq_push_wait_ex. */
typedef void (*dap_vmq_wake_fn)(void *a_arg);

/**
 * @brief Blocking push with optional consumer-wake (Dekker pattern).
 *
 * Wait loop per iteration:
 *   1. pw=1 (seq_cst) — declare "I am waiting for space"
 *   2. a_wake(arg)    — notify consumer after pw publish
 *   3. retry push     — consumer may have drained by now
 *   4. if still full  — futex_wait(pw, 1)
 *
 * The consumer drain guarantees ACK: on every pass
 * it either commits head (which wakes pw via commit_head) or calls
 * s_vmq_ack_waiter(pw) on empty lanes. No external compensator needed.
 *
 * @return True if queued, false on shutdown or if the message exceeds capacity.
 */
DAP_STATIC_INLINE bool s_vmq_push_wait_ex(
    _Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
    _Atomic(uint32_t) *a_pw, _Atomic(uint32_t) *a_shutdown,
    char *a_data, size_t a_capacity,
    uint8_t a_type, uint8_t a_pri,
    const void *a_payload, uint32_t a_payload_size,
    dap_vmq_wake_fn a_wake, void *a_wake_arg)
{
    size_t l_aligned = DAP_ALIGN_UP(sizeof(dap_vmqueue_hdr_t) + a_payload_size, 8);
    bool l_wait_armed = false;
    if (l_aligned > a_capacity)
        return false;
    while (!s_vmq_push(a_tg, a_hg, a_data, a_capacity,
                       a_type, a_pri, a_payload, a_payload_size)) {
        if (a_shutdown && atomic_load_explicit(a_shutdown, memory_order_relaxed))
            return false;
        if (!l_wait_armed) {
            atomic_store_explicit(a_pw, 1, memory_order_seq_cst);
            l_wait_armed = true;
            if (a_wake) a_wake(a_wake_arg);
        }
        if (s_vmq_push(a_tg, a_hg, a_data, a_capacity,
                        a_type, a_pri, a_payload, a_payload_size)) {
            atomic_store_explicit(a_pw, 0, memory_order_relaxed);
            return true;
        }
        if (a_shutdown && atomic_load_explicit(a_shutdown, memory_order_seq_cst)) {
            atomic_store_explicit(a_pw, 0, memory_order_relaxed);
            return false;
        }
        dap_futex_wait(a_pw, 1);
        atomic_store_explicit(a_pw, 0, memory_order_relaxed);
        l_wait_armed = false;
    }
    if (a_wake) a_wake(a_wake_arg);
    return true;
}

/** @brief Blocking push without consumer wake — pure producer-side backpressure. */
DAP_STATIC_INLINE bool s_vmq_push_wait(
    _Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
    _Atomic(uint32_t) *a_pw, _Atomic(uint32_t) *a_shutdown,
    char *a_data, size_t a_capacity,
    uint8_t a_type, uint8_t a_pri,
    const void *a_payload, uint32_t a_payload_size)
{
    return s_vmq_push_wait_ex(a_tg, a_hg, a_pw, a_shutdown,
                               a_data, a_capacity,
                               a_type, a_pri, a_payload, a_payload_size,
                               NULL, NULL);
}

/** @brief Acknowledge a waiting producer and perform futex wake once. */
DAP_STATIC_INLINE bool s_vmq_ack_waiter(_Atomic(uint32_t) *a_pw)
{
    if (!a_pw)
        return false;
    if (!atomic_load_explicit(a_pw, memory_order_seq_cst))
        return false;
    uint32_t l_expected = 1;
    if (!atomic_compare_exchange_strong_explicit(a_pw, &l_expected, 0,
                                                 memory_order_seq_cst,
                                                 memory_order_acquire))
        return false;
    dap_futex_wake(a_pw, 1);
    return true;
}

/** @brief Commit new head position and wake the producer if it is waiting. */
DAP_STATIC_INLINE void
s_vmq_commit_head(_Atomic(uint64_t) *a_hg, uint32_t a_gen, size_t a_head,
                  _Atomic(uint32_t) *a_pw)
{
    atomic_store_explicit(a_hg, DAP_VMQ_PACK(a_gen, a_head),
                          memory_order_release);
    if (a_pw && atomic_load_explicit(a_pw, memory_order_seq_cst)) {
        atomic_store_explicit(a_pw, 0, memory_order_seq_cst);
        dap_futex_wake(a_pw, 1);
    }
}

/* ---- drain iteration helpers ------------------------------------------ */

/** Snapshot of buffer position used by drain helpers. */
typedef struct {
    size_t   head, tail, step;
    uint32_t gen;
} s_vmq_drain_pos_t;

/** @brief Load head/tail/gen atomics and handle generation wrap.
 *  On gen mismatch, resets head to 0 and wakes the producer (via @a a_pw). */
DAP_STATIC_INLINE s_vmq_drain_pos_t
s_vmq_drain_begin(_Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
                  _Atomic(uint32_t) *a_pw)
{
    uint64_t l_tg = atomic_load_explicit(a_tg, memory_order_acquire);
    uint64_t l_hg = atomic_load_explicit(a_hg, memory_order_relaxed);
    uint32_t l_gen  = DAP_VMQ_GEN(l_tg);
    size_t   l_head = DAP_VMQ_OFF(l_hg);
    if (__builtin_expect(l_gen != DAP_VMQ_GEN(l_hg), 0)) { /* gen-wrap: producer reset */
        l_head = 0;                                        /* sync head to new gen */
        s_vmq_commit_head(a_hg, l_gen, 0, a_pw);
    }
    return (s_vmq_drain_pos_t){
        .head = l_head, .tail = DAP_VMQ_OFF(l_tg), .step = 0, .gen = l_gen };
}

/** @brief Peek at the next valid header without advancing.
 *  Stores step in @c a_pos->step; caller advances with
 *  @c a_pos->head += a_pos->step after successful dispatch.
 *  @return Header pointer, or NULL when the lane is exhausted / corrupted. */
DAP_STATIC_INLINE dap_vmqueue_hdr_t *
s_vmq_drain_peek(const char *a_data, s_vmq_drain_pos_t *a_pos)
{
    if (a_pos->head >= a_pos->tail) return NULL;
    dap_vmqueue_hdr_t *l_hdr = (dap_vmqueue_hdr_t *)(a_data + a_pos->head);
    a_pos->step = DAP_ALIGN_UP(l_hdr->total_len, 8);
    if (!a_pos->step || a_pos->step > a_pos->tail - a_pos->head) return NULL;
    __builtin_prefetch(a_data + a_pos->head + a_pos->step, 0, 0);
    return l_hdr;
}

/* ---- drain functions -------------------------------------------------- */

/**
 * @brief Drain available messages for a single consumer, invoking a callback per message.
 * @return Number of messages delivered to @a a_cb.
 */
DAP_STATIC_INLINE size_t s_vmq_drain(
    _Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
    _Atomic(uint32_t) *a_pw, char *a_data,
    void (*a_cb)(dap_vmqueue_hdr_t *, void *), void *a_arg)
{
    s_vmq_drain_pos_t l_ = s_vmq_drain_begin(a_tg, a_hg, a_pw);
    size_t l_count = 0;
    dap_vmqueue_hdr_t *l_hdr;
    while ((l_hdr = s_vmq_drain_peek(a_data, &l_))) {
        a_cb(l_hdr, a_arg);
        l_.head += l_.step;
        ++l_count;
    }
    if (l_count)
        s_vmq_commit_head(a_hg, l_.gen, l_.head, a_pw);
    else
        s_vmq_ack_waiter(a_pw); /* empty lane -- still wake producer if waiting */
    return l_count;
}

/* ================================================================== */
/*  SPSC public API                                                    */
/* ================================================================== */

dap_vmqueue_t     *dap_vmqueue_create(size_t a_capacity);
void               dap_vmqueue_destroy(dap_vmqueue_t *a_q);

DAP_STATIC_INLINE void dap_vmqueue_shutdown(dap_vmqueue_t *a_q)
{
    atomic_store_explicit(&a_q->shutdown, 1, memory_order_release);
    atomic_store_explicit(&a_q->producer_waiting, 0, memory_order_seq_cst);
    dap_futex_wake(&a_q->producer_waiting, (int)((uint32_t)-1 >> 1));
}

DAP_STATIC_INLINE bool dap_vmqueue_push(dap_vmqueue_t *a_q,
                                         uint8_t a_type, uint8_t a_pri,
                                         const void *a_payload, uint32_t a_payload_size)
{
    return s_vmq_push(&a_q->tail_gen, &a_q->head_gen, a_q->data, a_q->capacity,
                      a_type, a_pri, a_payload, a_payload_size);
}

DAP_STATIC_INLINE bool dap_vmqueue_push_wait(dap_vmqueue_t *a_q,
                                              uint8_t a_type, uint8_t a_pri,
                                              const void *a_payload, uint32_t a_payload_size)
{
    return s_vmq_push_wait(&a_q->tail_gen, &a_q->head_gen, &a_q->producer_waiting,
                            &a_q->shutdown, a_q->data, a_q->capacity,
                            a_type, a_pri, a_payload, a_payload_size);
}

DAP_STATIC_INLINE size_t dap_vmqueue_drain(dap_vmqueue_t *a_q,
                                            void (*a_cb)(dap_vmqueue_hdr_t *, void *),
                                            void *a_arg)
{
    return s_vmq_drain(&a_q->tail_gen, &a_q->head_gen, &a_q->producer_waiting,
                       a_q->data, a_cb, a_arg);
}

/* Forward-declare the typed adapter (defined in MPSC section below).
 * NB: a_len = hdr->total_len (sizeof(hdr) + payload), not raw payload size. */
typedef void (*dap_vmq_read_cb)(const void *a_payload, uint32_t a_len,
                                 uint8_t a_type, uint8_t a_pri, void *a_arg);
typedef struct { dap_vmq_read_cb cb; void *arg; } s_vmq_typed_ctx_t;
DAP_STATIC_INLINE void s_vmq_typed_adapter(dap_vmqueue_hdr_t *, void *);

/** @brief Typed drain for SPSC — user gets parsed payload, no hdr exposure. */
DAP_STATIC_INLINE size_t
dap_vmqueue_drain_typed(dap_vmqueue_t *a_q, dap_vmq_read_cb a_cb, void *a_arg)
{
    s_vmq_typed_ctx_t l_ctx = { .cb = a_cb, .arg = a_arg };
    return dap_vmqueue_drain(a_q, s_vmq_typed_adapter, &l_ctx);
}

/* ================================================================== */
/*  MPSC -- per-producer SPSC lanes (fan-in)                           */
/*                                                                     */
/*  Each producer owns exactly one lane (SPSC).  Consumer drains all   */
/*  lanes in round-robin.  Per-lane FIFO guaranteed; cross-lane        */
/*  ordering relaxed.                                                  */
/*                                                                     */
/*  Flat mmap layout (single allocation):                              */
/*                                                                     */
/*   +------------------+-------------------+-----------------------+  */
/*   | mpsc header      | ctrl blocks       | data regions          |  */
/*   | + lane_off[n+1]  | N x 192B          | lane0 | lane1 | ...  |  */
/*   +------------------+-------------------+-----------------------+  */
/*   0             ctrl_offset          data_offset          total_size */
/*                                                                     */
/*  Per-lane ctrl block (3 cachelines = 192 B):                        */
/*    CL0 [0..63]    tail_gen          -- producer-hot                 */
/*    CL1 [64..127]  head_gen          -- consumer-hot                 */
/*    CL2 [128..191] producer_waiting  -- backpressure futex           */
/* ================================================================== */

/* MPSC header. All lanes live in one contiguous allocation:
   [mpsc_header + lane_off[n+1]] [ctrl blocks: n * 192 B] [data regions]
   lane_off[i] stores the cumulative data offset for lane i. */
typedef struct dap_vmqueue_mpsc {
    unsigned          n_lanes;
    _Atomic(uint32_t) shutdown;
    _Atomic(uint32_t) notify_latch; /* 0 = consumer ACK'd, 1 = new data posted */
    size_t            ctrl_offset;  /* byte offset to lane control blocks */
    size_t            data_offset;  /* byte offset to data region start */
    size_t            total_size;   /* full allocation size (for munmap) */
    size_t            lane_off[];   /* lane_off[n+1]: cumulative data offsets */
} dap_vmqueue_mpsc_t;

/* Per-lane accessor helpers — resolve control block pointers from
 * the flat mmap region using ctrl_offset + lane index * stride */

DAP_STATIC_INLINE _Atomic(uint64_t) *s_mpsc_tg(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    return (_Atomic(uint64_t) *)((char *)a_q + a_q->ctrl_offset
                                 + (size_t)a_i * DAP_VMQ_LANE_CTRL_SIZE
                                 + DAP_VMQ_LANE_TG_OFF);
}

DAP_STATIC_INLINE _Atomic(uint64_t) *s_mpsc_hg(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    return (_Atomic(uint64_t) *)((char *)a_q + a_q->ctrl_offset
                                 + (size_t)a_i * DAP_VMQ_LANE_CTRL_SIZE
                                 + DAP_VMQ_LANE_HG_OFF);
}

DAP_STATIC_INLINE _Atomic(uint32_t) *s_mpsc_pw(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    return (_Atomic(uint32_t) *)((char *)a_q + a_q->ctrl_offset
                                 + (size_t)a_i * DAP_VMQ_LANE_CTRL_SIZE
                                 + DAP_VMQ_LANE_PW_OFF);
}

DAP_STATIC_INLINE size_t s_mpsc_cap(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    return a_q->lane_off[a_i + 1] - a_q->lane_off[a_i];
}

DAP_STATIC_INLINE char *s_mpsc_data(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    return (char *)a_q + a_q->data_offset + a_q->lane_off[a_i];
}

/* ------------------------------------------------------------------ */
/*  MPSC lifecycle                                                     */
/* ------------------------------------------------------------------ */

dap_vmqueue_mpsc_t *dap_vmqueue_mpsc_create(unsigned a_lanes, const size_t *a_capacities);
dap_vmqueue_mpsc_t *dap_vmqueue_mpsc_create_ex(unsigned a_lanes, const size_t *a_capacities,
                                                size_t a_tail_reserve);
void                dap_vmqueue_mpsc_destroy(dap_vmqueue_mpsc_t *a_q);

/**
 * @brief Notify the consumer that new data is available.
 *
 * Latch protocol: exchange(1, release); wake only on 0→1 transition.
 *
 * No relaxed pre-check: a relaxed load can return a stale 1 after the
 * consumer has already ACK'd (stored 0, release), causing a missed
 * transition and lost wake.  The unconditional exchange(release) is
 * the minimum correct primitive (~20 cyc x86, ~10 cyc ARM) and still
 * cheaper than the former CAS(seq_cst).
 *
 * Burst amortisation: after the first 0→1 exchange wakes the consumer,
 * subsequent pushes see exchange returning 1 (no wake syscall).
 */
DAP_STATIC_INLINE void dap_vmqueue_mpsc_notify(dap_vmqueue_mpsc_t *a_q)
{
    if (atomic_exchange_explicit(&a_q->notify_latch, 1,
                                  memory_order_release) == 0)
        dap_futex_wake(&a_q->notify_latch, 1);
}

DAP_STATIC_INLINE void dap_vmqueue_mpsc_shutdown(dap_vmqueue_mpsc_t *a_q)
{
    atomic_store_explicit(&a_q->shutdown, 1, memory_order_release);
    for (unsigned i = 0; i < a_q->n_lanes; ++i) {
        atomic_store_explicit(s_mpsc_pw(a_q, i), 0, memory_order_seq_cst);
        dap_futex_wake(s_mpsc_pw(a_q, i), (int)((uint32_t)-1 >> 1));
    }
    atomic_exchange_explicit(&a_q->notify_latch, 1, memory_order_release);
    dap_futex_wake(&a_q->notify_latch, 1);
}

/* ------------------------------------------------------------------ */
/*  MPSC push / push_wait / drain                                      */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE bool dap_vmqueue_mpsc_push(dap_vmqueue_mpsc_t *a_q, unsigned a_lane,
                                              uint8_t a_type, uint8_t a_pri,
                                              const void *a_payload, uint32_t a_payload_size)
{
    if (__builtin_expect(a_lane >= a_q->n_lanes, 0))
        return false;
    return s_vmq_push(s_mpsc_tg(a_q, a_lane), s_mpsc_hg(a_q, a_lane),
                      s_mpsc_data(a_q, a_lane), s_mpsc_cap(a_q, a_lane),
                      a_type, a_pri, a_payload, a_payload_size);
}

DAP_STATIC_INLINE size_t dap_vmqueue_mpsc_drain(dap_vmqueue_mpsc_t *a_q,
                                                 void (*a_cb)(dap_vmqueue_hdr_t *, void *),
                                                 void *a_arg)
{
    size_t l_total = 0;
    for (unsigned i = 0; i < a_q->n_lanes; ++i)
        l_total += s_vmq_drain(s_mpsc_tg(a_q, i), s_mpsc_hg(a_q, i),
                               s_mpsc_pw(a_q, i), s_mpsc_data(a_q, i),
                               a_cb, a_arg);
    return l_total;
}

/** @brief Estimate unread bytes in a single lane (relaxed snapshot). */
DAP_STATIC_INLINE size_t s_mpsc_lane_pending(dap_vmqueue_mpsc_t *a_q, unsigned a_i)
{
    uint64_t l_tg = atomic_load_explicit(s_mpsc_tg(a_q, a_i), memory_order_relaxed);
    uint64_t l_hg = atomic_load_explicit(s_mpsc_hg(a_q, a_i), memory_order_relaxed);
    size_t l_tail = DAP_VMQ_OFF(l_tg), l_head = DAP_VMQ_OFF(l_hg);
    return (DAP_VMQ_GEN(l_tg) != DAP_VMQ_GEN(l_hg))
        ? l_tail : (l_tail > l_head ? l_tail - l_head : 0);
}

/** @brief Estimate total unread bytes across all lanes (relaxed snapshot).
 *  (Public API — not called internally; for diagnostics/monitoring.) */
DAP_STATIC_INLINE size_t dap_vmqueue_mpsc_pending(dap_vmqueue_mpsc_t *a_q)
{
    size_t l_total = 0;
    for (unsigned i = 0; i < a_q->n_lanes; ++i)
        l_total += s_mpsc_lane_pending(a_q, i);
    return l_total;
}

/* ------------------------------------------------------------------ */
/*  Typed drain API — hides dap_vmqueue_hdr_t from the user            */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE void
s_vmq_typed_adapter(dap_vmqueue_hdr_t *a_hdr, void *a_ctx)
{
    s_vmq_typed_ctx_t *l = a_ctx;
    l->cb(a_hdr->payload, a_hdr->total_len, a_hdr->type, a_hdr->pri, l->arg);
}

/** @brief Typed drain — all lanes, user gets parsed payload. */
DAP_STATIC_INLINE size_t
dap_vmqueue_mpsc_drain_typed(dap_vmqueue_mpsc_t *a_q,
                              dap_vmq_read_cb a_cb, void *a_arg)
{
    s_vmq_typed_ctx_t l_ctx = { .cb = a_cb, .arg = a_arg };
    return dap_vmqueue_mpsc_drain(a_q, s_vmq_typed_adapter, &l_ctx);
}

/**
 * @brief Blocking consume loop for an MPSC queue.
 *
 * Runs until shutdown is signalled. Drains all lanes, sleeps on
 * a futex when idle (Dekker pattern — no busy-spin, no atomics
 * needed on the user side).  Performs a final drain after shutdown
 * to guarantee no messages are lost.
 *
 * Producers must call dap_vmqueue_mpsc_notify() after each push.
 */
DAP_STATIC_INLINE void
dap_vmqueue_mpsc_consume(dap_vmqueue_mpsc_t *a_q,
                          dap_vmq_read_cb a_cb, void *a_arg)
{
    s_vmq_typed_ctx_t l_ctx = { .cb = a_cb, .arg = a_arg };
    while (!atomic_load_explicit(&a_q->shutdown, memory_order_acquire)) {
        size_t l_n = dap_vmqueue_mpsc_drain(a_q, s_vmq_typed_adapter, &l_ctx);
        if (l_n)
            continue;
        /* Dekker: ACK -- clear latch before re-check */
        atomic_store_explicit(&a_q->notify_latch, 0, memory_order_release);
        /* Dekker: re-drain -- catch pushes racing between drain and ACK */
        l_n = dap_vmqueue_mpsc_drain(a_q, s_vmq_typed_adapter, &l_ctx);
        if (l_n)
            continue;
        /* Dekker: no new data after ACK -- safe to sleep */
        if (!atomic_load_explicit(&a_q->notify_latch, memory_order_acquire))
            dap_futex_wait(&a_q->notify_latch, 0);
    }
    for (;;) {                          /* Final drain: no message loss on shutdown */
        if (!dap_vmqueue_mpsc_drain(a_q, s_vmq_typed_adapter, &l_ctx))
            break;
    }
}
