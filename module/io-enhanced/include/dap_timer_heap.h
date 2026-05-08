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
 * @file dap_timer_heap.h
 * @brief Unified timer system for workers and processors.
 *
 * Sorted doubly-linked list with sentinel.  Owner-thread-private.
 * O(1) peek/extract-min/remove, ~O(1) insert for typical reschedule
 * (periodic timers land at the tail).  Free nodes reuse the same
 * next/prev pointers — no separate freelist structure.
 *
 * Each timer gets a unique monotonic ID (48-bit).  An intrusive hash
 * table (pprev trick) provides O(1) lookup by ID for cancel.
 *
 * Handle = packed uint64_t { owner_idx:16, id:48 }.  No raw pointers —
 * UAF-safe by construction.  owner_idx enables self-routing: any thread
 * can send a cancel to the correct owner's WFQ without extra context.
 */
#pragma once

#include "dap_io_plat.h"
#include "dap_msg_types.h"
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

/* ------------------------------------------------------------------ */
/*  Monotonic clock — struct timespec, zero conversions on Linux        */
/* ------------------------------------------------------------------ */

typedef struct timespec dap_time_t;

DAP_STATIC_INLINE dap_time_t dap_time_now(void)
{
    dap_time_t l_ts;
#ifdef DAP_OS_WINDOWS
    static volatile LONGLONG s_freq_val = 0;
    LONGLONG l_fv = s_freq_val;
    if (!l_fv) {
        LARGE_INTEGER l_f;
        QueryPerformanceFrequency(&l_f);
        s_freq_val = l_f.QuadPart;
        l_fv = l_f.QuadPart;
    }
    LARGE_INTEGER l_ctr;
    QueryPerformanceCounter(&l_ctr);
    uint64_t l_ns = (uint64_t)(l_ctr.QuadPart * 1000000000ULL / (uint64_t)l_fv);
    l_ts.tv_sec  = (time_t)(l_ns / 1000000000ULL);
    l_ts.tv_nsec = (long)(l_ns % 1000000000ULL);
#else
    clock_gettime(CLOCK_MONOTONIC, &l_ts);
#endif
    return l_ts;
}

/** @brief Current monotonic time as flat nanoseconds. */
DAP_STATIC_INLINE uint64_t dap_nanotime_now(void)
{
    dap_time_t l_t = dap_time_now();
    return (uint64_t)l_t.tv_sec * 1000000000ULL + (uint64_t)l_t.tv_nsec;
}

/* ------------------------------------------------------------------ */
/*  timespec arithmetic — no divisions                                 */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE bool dap_time_le(dap_time_t a, dap_time_t b)
{
    return a.tv_sec < b.tv_sec
        || (a.tv_sec == b.tv_sec && a.tv_nsec <= b.tv_nsec);
}

DAP_STATIC_INLINE dap_time_t dap_time_add(dap_time_t a, dap_time_t b)
{
    dap_time_t r = { a.tv_sec + b.tv_sec, a.tv_nsec + b.tv_nsec };
    if (r.tv_nsec >= 1000000000L) { r.tv_sec++; r.tv_nsec -= 1000000000L; }
    return r;
}

DAP_STATIC_INLINE dap_time_t dap_time_sub(dap_time_t a, dap_time_t b)
{
    dap_time_t r = { a.tv_sec - b.tv_sec, a.tv_nsec - b.tv_nsec };
    if (r.tv_nsec < 0) { r.tv_sec--; r.tv_nsec += 1000000000L; }
    return r;
}

/** @brief Convert microseconds to dap_time_t. */
DAP_STATIC_INLINE dap_time_t dap_time_from_us(uint64_t a_us)
{
    return (dap_time_t){ .tv_sec = (time_t)(a_us / 1000000ULL),
                         .tv_nsec = (long)((a_us % 1000000ULL) * 1000L) };
}

#define DAP_TIME_ZERO ((dap_time_t){0, 0})
#define DAP_TIME_NONZERO(t) ((t).tv_sec || (t).tv_nsec)

/* ------------------------------------------------------------------ */
/*  Timeout sentinel                                                   */
/* ------------------------------------------------------------------ */

#define DAP_TIMEOUT_INFINITE 0xFFFFFFFFU

/* ================================================================== */
/*  Coalescing threshold                                               */
/* ================================================================== */

#ifndef DAP_TIMER_COALESCE_US
#define DAP_TIMER_COALESCE_US  100   /* 100 µs */
#endif
#define DAP_TIMER_COALESCE  dap_time_from_us(DAP_TIMER_COALESCE_US)

/* ================================================================== */
/*  Timer diagnostics (behind DAP_IO_STATS)                            */
/* ================================================================== */

typedef struct {
    uint64_t fires;
    uint64_t coalesced;
    uint64_t early_count;
    int64_t  early_sum_ns;
    int64_t  late_sum_ns;
    int64_t  late_min_ns;
    int64_t  late_max_ns;
} dap_timer_diag_t;

DAP_STATIC_INLINE int64_t dap_time_diff_ns(dap_time_t a_a, dap_time_t a_b)
{
    return (int64_t)(a_a.tv_sec - a_b.tv_sec) * 1000000000LL
         + (int64_t)(a_a.tv_nsec - a_b.tv_nsec);
}

/* ================================================================== */
/*  Handle — packed {proc_idx:8, worker_slot:8, local_id:48}           */
/*  Self-routing: no dependency on n_heaps at cancel time.             */
/* ================================================================== */

typedef uint64_t dap_timer_handle_t;

#define DAP_TIMER_HANDLE_NULL  ((dap_timer_handle_t)0)
#define DAP_TIMER_ID_BITS      48u
#define DAP_TIMER_ID_MASK      ((1ULL << DAP_TIMER_ID_BITS) - 1ULL)
#define DAP_TIMER_SLOT_PROC    0xFFu   /* worker_slot value for processor timers */

DAP_STATIC_INLINE dap_timer_handle_t
dap_timer_make_handle(uint8_t a_proc_idx, uint8_t a_worker_slot, uint64_t a_local_id)
{
    return ((uint64_t)a_proc_idx << 56)
         | ((uint64_t)a_worker_slot << DAP_TIMER_ID_BITS)
         | (a_local_id & DAP_TIMER_ID_MASK);
}

DAP_STATIC_INLINE uint8_t  dap_timer_handle_proc(dap_timer_handle_t a_h)
{   return (uint8_t)(a_h >> 56); }

DAP_STATIC_INLINE uint8_t  dap_timer_handle_worker(dap_timer_handle_t a_h)
{   return (uint8_t)(a_h >> DAP_TIMER_ID_BITS); }

DAP_STATIC_INLINE uint64_t dap_timer_handle_id(dap_timer_handle_t a_h)
{   return a_h & DAP_TIMER_ID_MASK; }

/* ================================================================== */
/*  Global monotonic local-ID generator (thread-safe)                  */
/* ================================================================== */

extern _Atomic uint64_t dap_timer_g_next_id;

DAP_STATIC_INLINE uint64_t dap_timer_gen_local_id(void)
{
    uint64_t l_id = atomic_fetch_add_explicit(&dap_timer_g_next_id, 1,
                                               memory_order_relaxed);
    if (__builtin_expect(!l_id, 0))
        l_id = atomic_fetch_add_explicit(&dap_timer_g_next_id, 1,
                                          memory_order_relaxed);
    return l_id & DAP_TIMER_ID_MASK;
}

/* ================================================================== */
/*  Hash table config                                                  */
/* ================================================================== */

#ifndef DAP_TIMER_HASH_BITS
#define DAP_TIMER_HASH_BITS 3   /* 8 buckets = 64 bytes */
#endif
#define DAP_TIMER_HASH_SIZE (1u << DAP_TIMER_HASH_BITS)
#define DAP_TIMER_HASH_MASK (DAP_TIMER_HASH_SIZE - 1u)

/* ================================================================== */
/*  Timer node                                                         */
/* ================================================================== */

typedef struct dap_timer dap_timer_t;
struct dap_timer {
    dap_timer_t    *next;
    dap_timer_t    *prev;        /* NULL → node is in the free chain */
    dap_timer_t    *hash_next;   /* intrusive hash chain (forward) */
    dap_timer_t   **hash_pprev;  /* &buckets[i] or &prev->hash_next */
    uint64_t        id;
    dap_time_t      deadline;
    dap_time_t      interval;    /* {0,0} = one-shot */
    void           *arg;
    dap_timer_cb_t  exec;
    uint32_t        iterations;  /* 0 = infinite, 1 = one-shot, N = fire N times */
#ifdef DAP_IO_STATS
    dap_timer_diag_t diag;
    bool             diag_warm;
#endif
};

/* ================================================================== */
/*  Timer list — sorted doubly-linked with sentinel + hash index       */
/* ================================================================== */

#ifndef DAP_TIMER_TRIM_THRESHOLD
#define DAP_TIMER_TRIM_THRESHOLD 16
#endif

typedef struct {
    dap_timer_t  sentinel;
    dap_timer_t *free_head;
    unsigned     free_count;
    dap_timer_t *buckets[DAP_TIMER_HASH_SIZE];
} dap_timers_t;

/** @brief Initialize: empty sorted list, empty free chain, empty hash. */
DAP_STATIC_INLINE void dap_timers_init(dap_timers_t *a_tl)
{
    a_tl->sentinel.next = a_tl->sentinel.prev = &a_tl->sentinel;
    a_tl->free_head  = NULL;
    a_tl->free_count = 0;
    memset(a_tl->buckets, 0, sizeof(a_tl->buckets));
}

/** @brief Destroy: free all nodes (active + free chain), zero hash. */
DAP_STATIC_INLINE void dap_timers_destroy(dap_timers_t *a_tl)
{
    dap_timer_t *l_sen = &a_tl->sentinel;
    for (dap_timer_t *l_t = l_sen->next; l_t != l_sen; ) {
        dap_timer_t *l_n = l_t->next;
        free(l_t);
        l_t = l_n;
    }
    for (dap_timer_t *l_t = a_tl->free_head; l_t; ) {
        dap_timer_t *l_n = l_t->next;
        free(l_t);
        l_t = l_n;
    }
    a_tl->sentinel.next = a_tl->sentinel.prev = &a_tl->sentinel;
    a_tl->free_head  = NULL;
    a_tl->free_count = 0;
    memset(a_tl->buckets, 0, sizeof(a_tl->buckets));
}

/** @brief True if no active timers. */
DAP_STATIC_INLINE bool dap_timers_empty(const dap_timers_t *a_tl)
{
    return a_tl->sentinel.next == &a_tl->sentinel;
}

/* ------------------------------------------------------------------ */
/*  Splitmix64 finalizer for bucket index                              */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE unsigned s_timer_hash_idx(uint64_t a_id)
{
    a_id ^= a_id >> 30;  a_id *= 0xbf58476d1ce4e5b9ULL;
    a_id ^= a_id >> 27;  a_id *= 0x94d049bb133111ebULL;
    a_id ^= a_id >> 31;
    return (unsigned)(a_id & DAP_TIMER_HASH_MASK);
}

/* ------------------------------------------------------------------ */
/*  Hash helpers (O(1) insert / remove / find via pprev trick)         */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE void s_timer_hash_insert(dap_timers_t *a_tl, dap_timer_t *a_t)
{
    unsigned l_idx = s_timer_hash_idx(a_t->id);
    dap_timer_t **l_head = &a_tl->buckets[l_idx];
    a_t->hash_next  = *l_head;
    a_t->hash_pprev = l_head;
    if (*l_head)
        (*l_head)->hash_pprev = &a_t->hash_next;
    *l_head = a_t;
}

DAP_STATIC_INLINE void s_timer_hash_remove(dap_timer_t *a_t)
{
    if (!a_t->hash_pprev) return;
    *a_t->hash_pprev = a_t->hash_next;
    if (a_t->hash_next)
        a_t->hash_next->hash_pprev = a_t->hash_pprev;
    a_t->hash_next  = NULL;
    a_t->hash_pprev = NULL;
}

DAP_STATIC_INLINE dap_timer_t *s_timer_hash_find(dap_timers_t *a_tl, uint64_t a_id)
{
    unsigned l_idx = s_timer_hash_idx(a_id);
    for (dap_timer_t *l_t = a_tl->buckets[l_idx]; l_t; l_t = l_t->hash_next)
        if (l_t->id == a_id)
            return l_t;
    return NULL;
}

/* ------------------------------------------------------------------ */
/*  Alloc / free                                                       */
/* ------------------------------------------------------------------ */

DAP_STATIC_INLINE dap_timer_t *dap_timer_alloc(dap_timers_t *a_tl)
{
    dap_timer_t *l_t = a_tl->free_head;
    if (l_t) {
        a_tl->free_head = l_t->next;
        if (a_tl->free_count) a_tl->free_count--;
    } else {
        l_t = (dap_timer_t *)calloc(1, sizeof(dap_timer_t));
        if (!l_t) return NULL;
    }
    l_t->next = l_t->prev = NULL;
    l_t->hash_next = NULL;
    l_t->hash_pprev = NULL;
    l_t->id = 0;
#ifdef DAP_IO_STATS
    l_t->diag = (dap_timer_diag_t){ .late_min_ns = (int64_t)((uint64_t)-1 >> 1) };
    l_t->diag_warm = false;
#endif
    return l_t;
}

/** @brief Remove from hash + add to free chain. */
DAP_STATIC_INLINE void dap_timer_free(dap_timers_t *a_tl, dap_timer_t *a_t)
{
    s_timer_hash_remove(a_t);
    a_t->next = a_tl->free_head;
    a_t->prev = NULL;
    a_tl->free_head = a_t;
    a_tl->free_count++;
}

/** @brief Release all nodes in the free chain back to the heap allocator. */
DAP_STATIC_INLINE void dap_timers_trim(dap_timers_t *a_tl)
{
    dap_timer_t *l_t = a_tl->free_head;
    while (l_t) {
        dap_timer_t *l_n = l_t->next;
        free(l_t);
        l_t = l_n;
    }
    a_tl->free_head  = NULL;
    a_tl->free_count = 0;
}

/** @brief Trim the free chain if it has grown past the threshold.
 *  Intended to be called on the idle path (before sleep). */
DAP_STATIC_INLINE void dap_timers_maybe_trim(dap_timers_t *a_tl)
{
    if (a_tl->free_count >= DAP_TIMER_TRIM_THRESHOLD)
        dap_timers_trim(a_tl);
}

/* ------------------------------------------------------------------ */
/*  Sorted-list operations                                             */
/* ------------------------------------------------------------------ */

/** @brief Unlink a node from the sorted list.  No-op if not linked. */
DAP_STATIC_INLINE void dap_timer_unlink(dap_timer_t *a_t)
{
    if (!a_t->prev) return;
    a_t->prev->next = a_t->next;
    a_t->next->prev = a_t->prev;
    a_t->next = a_t->prev = NULL;
}

/** @brief Insert into sorted list (ascending deadline).
 *  Checks tail first — O(1) for typical reschedule. */
DAP_STATIC_INLINE void dap_timer_insert(dap_timers_t *a_tl, dap_timer_t *a_t)
{
    dap_timer_t *l_sen = &a_tl->sentinel;
    dap_timer_t *l_p;
    if (l_sen->prev == l_sen || !dap_time_le(a_t->deadline, l_sen->prev->deadline)) {
        l_p = l_sen;
    } else {
        l_p = l_sen->prev;
        while (l_p != l_sen && !dap_time_le(l_p->deadline, a_t->deadline))
            l_p = l_p->prev;
        l_p = l_p->next;
    }
    a_t->next = l_p;
    a_t->prev = l_p->prev;
    l_p->prev->next = a_t;
    l_p->prev = a_t;
}

/* ------------------------------------------------------------------ */
/*  Convenience: add / remove by handle                                */
/* ------------------------------------------------------------------ */

/** @brief Create a periodic timer on the local heap, return self-routing handle.
 *  @param a_proc_idx    Owning processor group index.
 *  @param a_worker_slot Worker slot (DAP_TIMER_SLOT_PROC for processor timers).
 *  @param a_interval_us First deadline offset / period (microseconds).
 *  @param a_iterations  0 = infinite, 1 = one-shot, N = N fires (see @c dap_timer_t::iterations). */
DAP_STATIC_INLINE dap_timer_handle_t
dap_timer_add(dap_timers_t *a_tl, uint8_t a_proc_idx, uint8_t a_worker_slot,
              uint64_t a_interval_us, uint32_t a_iterations,
              dap_timer_cb_t a_exec, void *a_arg)
{
    dap_timer_t *l_t = dap_timer_alloc(a_tl);
    if (!l_t) return DAP_TIMER_HANDLE_NULL;
    uint64_t l_lid  = dap_timer_gen_local_id();
    l_t->id         = l_lid;
    dap_time_t l_iv = dap_time_from_us(a_interval_us);
    l_t->deadline   = dap_time_add(dap_time_now(), l_iv);
    l_t->interval   = l_iv;
    l_t->exec       = a_exec;
    l_t->arg        = a_arg;
    l_t->iterations = a_iterations;
    s_timer_hash_insert(a_tl, l_t);
    dap_timer_insert(a_tl, l_t);
    return dap_timer_make_handle(a_proc_idx, a_worker_slot, l_lid);
}

/** @brief Remove a timer by handle (owner-thread only).  Returns true if found.
 *  Searches by the local_id portion of the handle. */
DAP_STATIC_INLINE bool
dap_timer_del(dap_timers_t *a_tl, dap_timer_handle_t a_h)
{
    uint64_t l_lid = dap_timer_handle_id(a_h);
    dap_timer_t *l_t = s_timer_hash_find(a_tl, l_lid);
    if (!l_t) return false;
    dap_timer_unlink(l_t);
    dap_timer_free(a_tl, l_t);
    return true;
}

/* ------------------------------------------------------------------ */
/*  Query                                                              */
/* ------------------------------------------------------------------ */

/** @brief Deadline of the soonest timer (or DAP_TIME_ZERO if empty). */
DAP_STATIC_INLINE dap_time_t dap_timers_nearest(const dap_timers_t *a_tl)
{
    const dap_timer_t *l_sen = &a_tl->sentinel;
    return (l_sen->next == l_sen) ? DAP_TIME_ZERO : l_sen->next->deadline;
}

/** @brief Convert an absolute deadline to ms-until-then (min 1, or INFINITE if zero). */
DAP_STATIC_INLINE uint32_t dap_deadline_to_ms(dap_time_t a_deadline)
{
    if (!DAP_TIME_NONZERO(a_deadline)) return DAP_TIMEOUT_INFINITE;
    dap_time_t l_now = dap_time_now();
    if (dap_time_le(a_deadline, l_now)) return 1;
    dap_time_t l_d = dap_time_sub(a_deadline, l_now);
    uint32_t l_ms = (uint32_t)(l_d.tv_sec * 1000 + l_d.tv_nsec / 1000000);
    return l_ms < 1 ? 1 : l_ms;
}

/** @brief Milliseconds until the soonest timer (or DAP_TIMEOUT_INFINITE). */
DAP_STATIC_INLINE uint32_t dap_timers_timeout(const dap_timers_t *a_tl)
{
    return dap_deadline_to_ms(dap_timers_nearest(a_tl));
}

/* ------------------------------------------------------------------ */
/*  Drain — fire expired timers with coalescing and catch-up           */
/* ------------------------------------------------------------------ */

/** @brief Fire all timers whose deadline <= now + coalesce window.
 *
 *  Periodic timers advance deadline past now via addition loop (no division)
 *  and reinsert (remain in hash).  One-shot / exhausted timers go to free
 *  chain and are removed from hash.
 *
 *  Timer callbacks receive only their user @c arg; the second context pointer
 *  was removed from @c dap_timer_cb_t — use @c dap_tls_worker / @c dap_tls_proc
 *  when the owning runtime context is required.
 *
 *  @return Number of timers fired.
 */
DAP_STATIC_INLINE uint32_t dap_timers_drain(dap_timers_t *a_tl)
{
    dap_time_t l_now     = dap_time_now();
    dap_time_t l_horizon = dap_time_add(l_now, DAP_TIMER_COALESCE);
    dap_timer_t *l_sen   = &a_tl->sentinel;
    uint32_t l_fired = 0;

    while (l_sen->next != l_sen) {
        dap_timer_t *l_t = l_sen->next;
        if (!dap_time_le(l_t->deadline, l_horizon))
            break;
        dap_timer_unlink(l_t);

        if (l_t->exec)
            l_t->exec(l_t->arg);
        l_fired++;

        bool l_alive = DAP_TIME_NONZERO(l_t->interval) && l_t->iterations != 1;
        if (l_alive) {
            if (l_t->iterations > 1)
                l_t->iterations--;
#ifdef DAP_IO_STATS
            int64_t l_true_lat = dap_time_diff_ns(l_now, l_t->deadline);
#endif
            uint32_t l_advances = 0;
            do {
                l_t->deadline = dap_time_add(l_t->deadline, l_t->interval);
                l_advances++;
            } while (dap_time_le(l_t->deadline, l_now));
#ifdef DAP_IO_STATS
            if (l_t->diag_warm) {
                l_t->diag.fires++;
                if (l_true_lat < 0) {
                    l_t->diag.early_count++;
                    l_t->diag.early_sum_ns += -l_true_lat;
                } else {
                    l_t->diag.late_sum_ns += l_true_lat;
                    if (l_true_lat < l_t->diag.late_min_ns)
                        l_t->diag.late_min_ns = l_true_lat;
                    if (l_true_lat > l_t->diag.late_max_ns)
                        l_t->diag.late_max_ns = l_true_lat;
                }
                if (l_advances > 1)
                    l_t->diag.coalesced += l_advances - 1;
            } else {
                l_t->diag_warm = true;
            }
#endif
            dap_timer_insert(a_tl, l_t);
        } else {
#ifdef DAP_IO_STATS
            {
                int64_t l_true_lat = dap_time_diff_ns(l_now, l_t->deadline);
                l_t->diag.fires++;
                if (l_true_lat < 0) {
                    l_t->diag.early_count++;
                    l_t->diag.early_sum_ns += -l_true_lat;
                } else {
                    l_t->diag.late_sum_ns += l_true_lat;
                    if (l_true_lat < l_t->diag.late_min_ns)
                        l_t->diag.late_min_ns = l_true_lat;
                    if (l_true_lat > l_t->diag.late_max_ns)
                        l_t->diag.late_max_ns = l_true_lat;
                }
            }
#endif
            dap_timer_free(a_tl, l_t);
        }
    }
    return l_fired;
}

/* ------------------------------------------------------------------ */
/*  Cross-thread cancel context                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    dap_timers_t       *tl;
    dap_timer_handle_t  h;
} dap_timer_cancel_ctx_t;

/** @brief Callback for cross-thread timer cancel (runs on owner thread). */
DAP_STATIC_INLINE void dap_timer_cancel_fn(void *a_arg)
{
    dap_timer_cancel_ctx_t *c = (dap_timer_cancel_ctx_t *)a_arg;
    dap_timer_del(c->tl, c->h);
    free(c);
}

/* ------------------------------------------------------------------ */
/*  Diagnostics helpers (DAP_IO_STATS only)                            */
/* ------------------------------------------------------------------ */

#ifdef DAP_IO_STATS

DAP_STATIC_INLINE void dap_timers_diag_reset(dap_timers_t *a_tl)
{
    dap_timer_t *l_sen = &a_tl->sentinel;
    for (dap_timer_t *l_t = l_sen->next; l_t != l_sen; l_t = l_t->next) {
        l_t->diag = (dap_timer_diag_t){ .late_min_ns = (int64_t)((uint64_t)-1 >> 1) };
        l_t->diag_warm = false;
    }
}

DAP_STATIC_INLINE uint64_t dap_time_to_us(dap_time_t a_t)
{
    return (uint64_t)a_t.tv_sec * 1000000ULL + (uint64_t)a_t.tv_nsec / 1000ULL;
}

DAP_STATIC_INLINE void dap_timers_diag_print(const dap_timers_t *a_tl,
                                              const char *a_label)
{
    const dap_timer_t *l_sen = &a_tl->sentinel;
    if (l_sen->next == l_sen) {
        printf("  %s: (no active timers)\n", a_label);
        return;
    }
    printf("  %s:\n", a_label);
    for (const dap_timer_t *l_t = l_sen->next; l_t != l_sen; l_t = l_t->next) {
        uint64_t l_ival_us = dap_time_to_us(l_t->interval);
        const dap_timer_diag_t *d = &l_t->diag;

        if (!DAP_TIME_NONZERO(l_t->interval))
            printf("    T[one-shot]: ");
        else if (l_ival_us >= 1000000)
            printf("    T[%llu s]: ", (unsigned long long)(l_ival_us / 1000000));
        else if (l_ival_us >= 1000)
            printf("    T[%llu ms]: ", (unsigned long long)(l_ival_us / 1000));
        else
            printf("    T[%llu us]: ", (unsigned long long)l_ival_us);

        if (!d->fires) {
            printf("0 fires\n");
            continue;
        }
        uint64_t l_late_n = d->fires - d->early_count;
        printf("%llu fires", (unsigned long long)d->fires);
        if (l_late_n) {
            double l_avg = (double)d->late_sum_ns / (double)l_late_n / 1000.0;
            double l_min = (double)d->late_min_ns / 1000.0;
            double l_max = (double)d->late_max_ns / 1000.0;
            printf("  late avg=%.1f us  min=%.1f us  max=%.1f us", l_avg, l_min, l_max);
        }
        if (d->early_count) {
            double l_eavg = (double)d->early_sum_ns / (double)d->early_count / 1000.0;
            printf("  early=%llu avg=%.1f us",
                   (unsigned long long)d->early_count, l_eavg);
        }
        if (d->coalesced)
            printf("  coalesced=%llu", (unsigned long long)d->coalesced);
        printf("\n");
    }
}

#endif /* DAP_IO_STATS */
