/**
 * @file dap_conn.h
 * @brief Expert-public connection structure (128 B, 2 cache lines) and lock-free slab allocator.
 *
 *  @c dap_conn_t is expert-visible (full layout), not an opaque handle. Direct field access
 *  is valid only under known-live pointers, owner-thread rules, and the documented slab
 *  lifetime contract. Normal protocol code should prefer helpers (e.g. @ref dap_conn_set_ext_dtor)
 *  and the send surface in @ref dap_io_send.h over ad-hoc field writes.
 *
 *  ┌─────────────────── dap_conn_t (128 B) ───────────────────┐
 *  │ CL0 (0–63):  cross-thread atomics + cold lifecycle       │
 *  │   generation  state  ext_dtor  _owner                    │
 *  │ CL1 (64–127): worker hot path (read-only after setup)    │
 *  │   fd  read_cb  write_cb  error_cb  olb  send_olb  ext   │
 *  └──────────────────────────────────────────────────────────┘
 *
 *  dap_conn_slab_t — fixed-capacity lock-free pool with quarantine.
 *  Three monotonic cursors carve the ring into zones:
 *
 *    q_ready            q_head                         q_tail
 *       │                  │                              │
 *       ▼                  ▼                              ▼
 *    ┌──────────────┬─ ─ ─ ─ ─ ─ ─ ─ ─ ─┬──────────────┐
 *    │ READY        │  IN USE            │ QUARANTINE    │
 *    │ (allocatable) │  (not in queue)    │ (pending drain)│
 *    └──────────────┴─ ─ ─ ─ ─ ─ ─ ─ ─ ─┴──────────────┘
 *
 *    alloc():  CAS q_head++            → takes slot from READY
 *    free():   fetch_add q_tail++      → puts slot into QUARANTINE
 *    drain():  CAS q_ready++           → moves slot from QUARANTINE → READY
 *              (only when processor passed the slot's wfq_seq marker)
 *
 *  All three ops are lock-free and safe for concurrent calls from
 *  multiple workers.  Typical hot-path cost: 1 CAS or 1 fetch_add.
 */
#pragma once

#include "dap_vmqueue_olb.h"
#include <stdlib.h>
#include "dap_conn_state.h"
#include <limits.h>

/* ================================================================== */
/*  Generic connection base                                            */
/* ================================================================== */

typedef struct dap_conn dap_conn_t;

#include "dap_conn_handle.h"

typedef void (*dap_conn_read_cb_t)(dap_conn_t *);
typedef void (*dap_conn_write_cb_t)(dap_conn_t *);
typedef void (*dap_conn_error_cb_t)(dap_conn_t *, int errnum);
typedef void (*dap_conn_ext_dtor_t)(void *ext);

/* ================================================================== */
/*  Generation counter (process-global, monotonic)                     */
/*                                                                     */
/*  Each slab_alloc takes a fresh generation via fetch_add on this     */
/*  counter and stores it in the slot.  Two properties follow:         */
/*                                                                     */
/*    1. The same slot gets a strictly increasing generation on every  */
/*       reuse, so a stale handle with an old gen never matches.       */
/*                                                                     */
/*    2. A slot that has never been allocated (or whose page was       */
/*       reclaimed via MADV_DONTNEED) reads as generation == 0, which  */
/*       is reserved and never issued to any handle.  This closes the  */
/*       ABA window that a per-slot counter would have opened when     */
/*       pages are returned to the OS.                                 */
/*                                                                     */
/*  64-bit width makes wrap effectively impossible (1.8·10¹⁹ allocs).  */
/* ================================================================== */
extern _Atomic(uint64_t) dap_conn_gen_counter;

/**
 * @brief Base connection — slab-allocated, 128-byte, cache-line optimized.
 *
 * CL0 (bytes 0–63): cross-thread atomics + cold lifecycle fields.
 *      _owner is set once in conn_add, read by processor via notify_send.
 *      ext_dtor fires only at quarantine drain — cold path.
 *
 * CL1 (bytes 64–127): worker hot path — touched on every epoll event.
 *      All fields set once at accept/bind, then read-only.
 */
typedef struct dap_conn {
    /* --- CL0: cross-thread atomics + cold lifecycle -------------------- */
    _Alignas(DAP_VMQ_CACHELINE)
    _Atomic(uint64_t)    generation;   /*!< snapshot of global monotonic counter on slab_alloc;
                                           0 == slot never allocated or page reclaimed */
    _Atomic(uint8_t)     state;        /*!< RECV_DONE / SUSPENDED / CLOSED / SEND_BUSY */
    dap_conn_ext_dtor_t  ext_dtor;     /*!< type-aware ext destructor (quarantine drain) */
    _Atomic(void *)      _owner;       /*!< owning dap_worker_t; written once in
                                           conn_add (release), read by other threads
                                           in dap_io_tx_send's TLS fast-path and by
                                           notify_send / dap_worker_tx_flush on the owner side.
                                           slot_cleanup does NOT zero this field — a
                                           stale handle still reads a live worker
                                           pointer, and the generation check sorts
                                           out whether the slot still belongs to it. */

    /* --- CL1: worker hot path (read-only after setup) ------------------ */
    _Alignas(DAP_VMQ_CACHELINE)
    dap_conn_read_cb_t   read_cb;      /*!< EPOLLIN  — recv + parse */
    dap_conn_write_cb_t  write_cb;     /*!< EPOLLOUT — flush send_olb or user logic */
    dap_conn_error_cb_t  error_cb;     /*!< EPOLLERR / EPOLLHUP */
    dap_vmqueue_olb_t   *olb;          /*!< recv buffer (worker writes, processor acks) */
    dap_vmqueue_olb_t   *send_olb;     /*!< send buffer (processor writes, worker flushes) */
    void                *ext;          /*!< protocol context, malloc'd by caller */
    dap_fd_t             fd;
    uint16_t             _w_prev;      /*!< worker-only: prev conn idx in worker-owned list (UINT16_MAX = none) */
    uint16_t             _w_next;      /*!< worker-only: next conn idx in worker-owned list (UINT16_MAX = none) */
} dap_conn_t;

_Static_assert(sizeof(dap_conn_t) == 2 * DAP_VMQ_CACHELINE,
               "dap_conn_t must be exactly 2 cache lines");

/** @brief Load connection state flags with acquire ordering. */
DAP_STATIC_INLINE uint8_t dap_conn_state(dap_conn_t *a_c) {
    return atomic_load_explicit(&a_c->state, memory_order_acquire);
}
/** @brief Atomically set bits in the connection state (release). */
DAP_STATIC_INLINE void dap_conn_set(dap_conn_t *a_c, uint8_t a_f) {
    atomic_fetch_or_explicit(&a_c->state, a_f, memory_order_release);
}
/** @brief Atomically clear bits in the connection state (release). */
DAP_STATIC_INLINE void dap_conn_clear(dap_conn_t *a_c, uint8_t a_f) {
    atomic_fetch_and_explicit(&a_c->state, (uint8_t)~a_f, memory_order_release);
}

/**
 * @brief Return codes for dap_io_tx_send() / dap_io_tx_send_direct() (unified public write API).
 *
 *   DAP_SEND_OK       — data queued for transmission; worker will flush
 *                       it to the socket on the next opportunity.  The
 *                       runtime has already decided whether to gate
 *                       read_cb (DAP_CONN_SUSPENDED) and to notify the
 *                       owning worker.  The caller can move on.
 *
 *   DAP_SEND_CLOSED   — the handle does not point at a live connection:
 *                       either the slot has been recycled (generation
 *                       mismatch), its page was reclaimed by the OS
 *                       (generation reads back 0), or the connection is
 *                       marked CLOSED / has no send_olb.  The payload
 *                       was NOT written; safe to drop.
 *
 *   DAP_SEND_OVERFLOW — transient: send_olb has no space now, or allocation
 *                       pressure on the slow cross-thread send path.  The
 *                       runtime raises DAP_CONN_SUSPENDED on the owner
 *                       fast path when appropriate; caller may retry after
 *                       drain.  The payload was NOT written.
 *
 *   DAP_SEND_TOO_LARGE — permanent on the owner direct path: payload exceeds
 *                        send_olb capacity; caller must chunk or drop.  No
 *                        SUSPENDED is raised for this case.  The payload was
 *                        NOT written.
 */
typedef enum {
    DAP_SEND_OK       = 0,
    DAP_SEND_CLOSED   = 1,
    DAP_SEND_OVERFLOW = 2,
    DAP_SEND_TOO_LARGE = 3
} dap_send_rc_t;

/** @brief Check if SYNC transition is complete (processor drained all batches).
 *
 *  Compares ack_pos (advanced by processor's dap_vmqolb_ack) against tail_pos.
 *  head_pos lags behind ack_pos (advanced only by apply_ack in try_space),
 *  but during the transition wait recv is blocked, so apply_ack never runs.
 *  Using ack_pos avoids the false-negative that would stall the transition. */
DAP_STATIC_INLINE bool dap_conn_sync_ready(dap_conn_t *a_c) {
    return atomic_load_explicit(&a_c->olb->ack_pos,  memory_order_acquire)
        >= atomic_load_explicit(&a_c->olb->tail_pos, memory_order_acquire);
}

/** @brief Switch connection to synchronous (worker-inline) processing.
 *
 *  Call from worker thread only (read_cb, write_cb, or after conn_open).
 *  If ASYNC→SYNC: recv is blocked until processor drains in-flight batches.
 *  If already SYNC or no in-flight: takes effect immediately. */
DAP_STATIC_INLINE void dap_conn_enter_sync(dap_conn_t *a_c) {
    dap_conn_set(a_c, DAP_CONN_SYNC);
}

/** @brief Switch connection to asynchronous (processor) processing.
 *
 *  Call from worker thread only.
 *  Immediate — no in-flight state in SYNC mode. */
DAP_STATIC_INLINE void dap_conn_enter_async(dap_conn_t *a_c) {
    dap_conn_clear(a_c, DAP_CONN_SYNC);
}

/**
 * @brief Bind @c ext destructor for slab quarantine cleanup (cold setup only).
 *
 * Caller-owned @c ext stays caller-owned unless this destructor is set; when set,
 * it runs during slab quarantine drain, not immediately on close. Call after
 * @ref dap_io_conn_open succeeds or when the caller otherwise holds a known-live
 * connection. Setting a destructor when @c ext is stack- or static-backed is a
 * bug unless the destructor is intentionally @c NULL — use only with heap or
 * otherwise individually owned @c ext storage matching @a a_dtor.
 *
 * @param a_conn Open connection; @c a_conn->ext must already be non-NULL.
 * @param a_dtor Callback invoked with @c ext during quarantine cleanup.
 * @return True if @a a_conn, @a a_dtor, and @c a_conn->ext are all non-NULL.
 */
DAP_STATIC_INLINE bool dap_conn_set_ext_dtor(dap_conn_t *a_conn, dap_conn_ext_dtor_t a_dtor) {
    if (!a_conn || !a_dtor || !a_conn->ext)
        return false;
    a_conn->ext_dtor = a_dtor;
    return true;
}

/** @brief Wake the worker via platform-appropriate mechanism. */
#ifdef DAP_OS_WINDOWS
#ifndef DAP_IOCP_KEY_RESUME
#  define DAP_IOCP_KEY_RESUME 0xFFFF
#endif
DAP_STATIC_INLINE void dap_conn_kick(HANDLE a_iocp) {
    PostQueuedCompletionStatus(a_iocp, 0, DAP_IOCP_KEY_RESUME, NULL);
}
#else
DAP_STATIC_INLINE void dap_conn_kick(int a_efd) {
    uint64_t l_one = 1;
    if (write(a_efd, &l_one, sizeof(l_one)) < 0) {;}
}
#endif

/* ================================================================== */
/*  Connection slab — lock-free pool with generation-based quarantine   */
/*                                                                     */
/*  Slots hold dap_conn_t (128 B, cacheline-aligned).                  */
/*  Protocol-specific data lives behind conn->ext (malloc'd by caller).*/
/* ================================================================== */

#ifndef DAP_CONN_SLAB_MAX
#define DAP_CONN_SLAB_MAX 1024
#endif

/* ================================================================== */
/*  Slab-sized bitset                                                  */
/*                                                                     */
/*  Used for: pending_bits (worker → processor send kick),             */
/*            defer_q.mask (defer queue membership),                   */
/*            rescan_mask  (processor → worker WFQ retry kick).        */
/*                                                                     */
/*  Indexed by slab slot index.  One uint64_t word covers 64 slots.   */
/* ================================================================== */

#define DAP_SLAB_BITS_WORDS  ((DAP_CONN_SLAB_MAX + 63) / 64)

DAP_STATIC_INLINE bool dap_slab_bits_test(const _Atomic uint64_t *a_m, unsigned a_idx) {
    return atomic_load_explicit(&a_m[a_idx >> 6], memory_order_relaxed) & (1ULL << (a_idx & 63));
}
DAP_STATIC_INLINE void dap_slab_bits_set(_Atomic uint64_t *a_m, unsigned a_idx) {
    atomic_fetch_or_explicit(&a_m[a_idx >> 6], 1ULL << (a_idx & 63), memory_order_release);
}
/** @brief Set bit and report whether it transitioned 0->1. */
DAP_STATIC_INLINE bool dap_slab_bits_set_if_new(_Atomic uint64_t *a_m, unsigned a_idx) {
    uint64_t l_mask = 1ULL << (a_idx & 63);
    return !(atomic_fetch_or_explicit(&a_m[a_idx >> 6], l_mask, memory_order_release) & l_mask);
}
DAP_STATIC_INLINE void dap_slab_bits_clear(_Atomic uint64_t *a_m, unsigned a_idx) {
    atomic_fetch_and_explicit(&a_m[a_idx >> 6], ~(1ULL << (a_idx & 63)), memory_order_relaxed);
}
/** @brief Atomically grab all set bits in word and clear them (acquire). */
DAP_STATIC_INLINE uint64_t dap_slab_bits_grab(_Atomic uint64_t *a_m, unsigned a_word) {
    return atomic_exchange_explicit(&a_m[a_word], 0, memory_order_acquire);
}

/**
 * @brief Quarantine queue entry.
 *
 *  wfq_seq protocol (two-phase commit in free → drain):
 *    (uint64_t)-1 = sentinel: entry is either empty or free() hasn't
 *                   committed yet (idx written, wfq_seq pending).
 *    other value  = WFQ sequence at free time; drain may reclaim the
 *                   slot once the processor's head passes this mark.
 */
typedef struct {
    uint16_t              idx;
    _Atomic(uint64_t)     wfq_seq;
} dap_conn_slab_entry_t;

/**
 * @brief Shared connection slab — lock-free alloc / free / quarantine drain.
 *
 *  Ring layout (all cursors are monotonically increasing, mod MAX):
 *
 *    queue[ q_ready % MAX ] ─── READY ───▶ queue[ (q_head-1) % MAX ]
 *    (gap: connections in use, not tracked in queue)
 *    queue[ ??? ]           ─── QUARANTINE ─▶ queue[ (q_tail-1) % MAX ]
 *
 *  Invariant: q_ready ≤ q_head ≤ q_tail   (unsigned wrap-safe subtraction)
 *  Capacity:  q_head - q_ready = allocatable slots
 *             q_tail - q_ready = occupied (active + quarantine)
 */
typedef struct {
    size_t                slot_size;
    unsigned              max_slots;
    size_t                map_bytes;  /* size of the underlying mmap region (0 on
                                         non-mmap builds, signals free() vs munmap()) */
    _Atomic(uint64_t)     wfq_epoch;  /* processor increments after each drain cycle;
                                         conn_del stores epoch+1 as quarantine marker */
    dap_conn_slab_entry_t queue[DAP_CONN_SLAB_MAX];
    _Atomic(unsigned)     q_head;   /* next slot to allocate (CAS in alloc)     */
    _Atomic(unsigned)     q_ready;  /* first allocatable slot (CAS in drain)    */
    _Atomic(unsigned)     q_tail;   /* next quarantine position (fetch_add in free) */
    _Alignas(DAP_VMQ_CACHELINE)
    char                  slots[];  /* FAM: max_slots × slot_size bytes —
                                       pages backed by mmap, committed lazily on
                                       first slot_alloc write to that slot */
} dap_conn_slab_t;

/** @brief Return the connection pointer for slab slot @a a_i. */
DAP_STATIC_INLINE dap_conn_t *dap_conn_slab_slot(dap_conn_slab_t *a_s, unsigned a_i) {
    return (dap_conn_t *)(a_s->slots + a_i * a_s->slot_size);
}

/** @brief Number of occupied slots (active + quarantine, OLBs still mapped). */
DAP_STATIC_INLINE unsigned dap_conn_slab_occupancy(const dap_conn_slab_t *a_s) {
    return atomic_load_explicit(&a_s->q_head, memory_order_relaxed)
         - atomic_load_explicit(&a_s->q_ready, memory_order_relaxed);
}

/**
 * @brief Create a connection slab with @a a_max slots of @a a_slot_size bytes each.
 *
 * On POSIX the slab is backed by a single MAP_ANONYMOUS|MAP_PRIVATE
 * mmap region.  Anonymous pages start out backed by the kernel's
 * zero-page and are CoW'd to real physical memory only when first
 * written to, so the slots[] FAM stays "free" in RSS until a slot is
 * actually allocated.  Over the slab's lifetime only pages covering
 * live slots accrue physical cost; idle slots cost one virtual-address
 * range and nothing more.
 *
 * On non-POSIX builds the slab is cacheline-aligned on the heap as a
 * compatibility fallback (memory stays resident).  Both paths share
 * identical semantics for alloc/free/drain.
 *
 * @return Allocated slab or NULL on failure / a_max exceeds DAP_CONN_SLAB_MAX.
 */
dap_conn_slab_t *dap_conn_slab_create(unsigned a_max, size_t a_slot_size);

/** @brief Return the slot index of connection @a a_c within slab @a a_s. */
DAP_STATIC_INLINE uint16_t dap_conn_slab_idx(dap_conn_slab_t *a_s, dap_conn_t *a_c) {
    return (uint16_t)(((char *)a_c - a_s->slots) / (ptrdiff_t)a_s->slot_size);
}


/**
 * @brief Allocate a connection slot.
 *
 *  CAS loop on q_head; fails only on contention (another worker won the race).
 *  Generation bump invalidates any stale WFQ tasks referencing this slot.
 *
 *  Ordering:
 *    q_ready load: acquire — sees drain's cleanup writes before we reuse the slot.
 *    q_head CAS success: acquire — serializes alloc ordering among competing workers
 *      (does NOT pair with free's wfq_seq — those are different atomics; actual reuse
 *      synchronization happens via q_ready/wfq_seq in drain).
 *    generation bump: release — processor will see new gen via WFQ acquire.
 */
DAP_STATIC_INLINE dap_conn_t *dap_conn_slab_alloc(dap_conn_slab_t *a_s)
{
    unsigned l_h = atomic_load_explicit(&a_s->q_head, memory_order_relaxed);
    for (;;) {
        if (l_h == atomic_load_explicit(&a_s->q_ready, memory_order_acquire))
            return NULL;                /* no ready slots — all in use or quarantine */
        if (atomic_compare_exchange_weak_explicit(&a_s->q_head, &l_h, l_h + 1,
                                                   memory_order_acquire,
                                                   memory_order_relaxed))
            break;                      /* won the slot at position l_h */
    }
    dap_conn_t *l_c = dap_conn_slab_slot(a_s, a_s->queue[l_h % DAP_CONN_SLAB_MAX].idx);
    /* Globally monotonic generation: never repeats across reuses and
     * stays strictly > 0 for every live handle, so a recycled or
     * DONTNEED'd page (which reads back zero) never false-matches. */
    uint64_t l_gen = atomic_fetch_add_explicit(&dap_conn_gen_counter, 1,
                                                memory_order_relaxed) + 1;
    atomic_store_explicit(&l_c->generation, l_gen, memory_order_release);
    atomic_store_explicit(&l_c->state, 0, memory_order_relaxed);
    l_c->ext_dtor = NULL;
    atomic_store_explicit(&l_c->_owner, NULL, memory_order_relaxed);
    l_c->fd = -1;
    l_c->read_cb = NULL; l_c->write_cb = NULL; l_c->error_cb = NULL;
    l_c->olb = NULL; l_c->send_olb = NULL;
    l_c->ext = NULL;
    l_c->_w_prev = UINT16_MAX;
    l_c->_w_next = UINT16_MAX;
    return l_c;
}

/**
 * @brief Return a freshly allocated slot that was never used in WFQ.
 *
 *  For error paths only (e.g. conn_add failed after slab_alloc).
 *  Caller MUST have already detached/destroyed OLBs and ext.
 *  No quarantine needed — no WFQ tasks reference this slot.
 *  wfq_seq = 0 makes the slot immediately drainable in next drain cycle.
 */
DAP_STATIC_INLINE void
dap_conn_slab_return(dap_conn_slab_t *a_s, dap_conn_t *a_c)
{
    uint16_t l_idx = dap_conn_slab_idx(a_s, a_c);
    unsigned l_t = atomic_fetch_add_explicit(&a_s->q_tail, 1, memory_order_relaxed);
    dap_conn_slab_entry_t *l_e = &a_s->queue[l_t % DAP_CONN_SLAB_MAX];
    l_e->idx = l_idx;
    atomic_store_explicit(&l_e->wfq_seq, 0, memory_order_release);
}

/**
 * @brief Bind buffers and descriptor to a freshly allocated connection.
 *
 * Sets the three "hardware" fields that every connection needs.
 * Callbacks (read_cb, write_cb, error_cb) and ext are assigned directly by
 * the caller — they are plain struct fields, no indirection required.
 *
 *   dap_conn_t *c = dap_conn_slab_alloc(slab);
 *   dap_conn_attach(c, olb, send_olb, fd);
 *   c->read_cb = my_read;
 *   c->ext     = my_ext;
 */
DAP_STATIC_INLINE void
dap_conn_attach(dap_conn_t *a_c,
                dap_vmqueue_olb_t *a_olb,
                dap_vmqueue_olb_t *a_send_olb,
                dap_fd_t a_fd)
{
    a_c->fd       = a_fd;
    a_c->olb      = a_olb;
    a_c->send_olb = a_send_olb;
}

/**
 * @brief Capture a handle for a connection the caller KNOWS is live.
 *
 *  The generation snapshot is taken from the pointed-at slot, so the
 *  input pointer MUST currently own the slot.  Acceptable call sites:
 *
 *    - right after dap_io_conn_open(): the creating thread holds the
 *      only live reference, no other thread can quarantine the slot;
 *    - inside an owner-side callback (read_cb / write_cb / error_cb /
 *      parse_fn): the worker that dispatched the callback is the only
 *      party that can call conn_del, and it's currently running user
 *      code — so it cannot be freeing this slot at the same time;
 *    - inside framework internals that carry a known-live dap_conn_t *
 *      (worker's own conns[] entry, a freshly drained WFQ task, etc.).
 *
 *  Calling this from any OTHER context (a cached dap_conn_t * read
 *  from shared state, a pointer recovered from a stale handle, etc.)
 *  is unsafe: the slot may have been reclaimed, and reading generation
 *  from it would encode a spurious "valid" gen snapshot into the
 *  handle, giving a dap_conn_handle_t that points at the wrong
 *  connection.  For cross-thread use, always propagate the handle as a
 *  value type alongside the pointer — never reconstruct it from the
 *  pointer later.
 */
DAP_STATIC_INLINE dap_conn_handle_t dap_conn_handle_from_live(dap_conn_t *a_c) {
    /* Relaxed is sufficient: the caller holds a known-live pointer, meaning
     * generation was published either (a) earlier in this same thread (slab_alloc
     * did a release-store, program order gives us visibility), or (b) by another
     * thread whose publication we've already synchronized with through the path
     * that produced this pointer (epoll data.ptr, conns[] entry, freshly drained
     * WFQ task).  No extra acquire fence is needed here. */
    return (dap_conn_handle_t){
        .c   = a_c,
        .gen = a_c ? atomic_load_explicit(&a_c->generation, memory_order_relaxed)
                   : 0,
    };
}

/**
 * @brief Resolve a handle to a live connection pointer.
 *
 * Thread-safe from any context — the only input is the handle itself.
 * Returns NULL when the slot has been recycled, its page reclaimed by
 * MADV_DONTNEED (generation reads back 0), or the handle is null.
 *
 * The slab pointer never appears here by design: the slot address is
 * stable for the life of its owning slab, and generation alone decides
 * validity.  See dap_conn_handle_t for the lifetime contract.
 */
DAP_STATIC_INLINE dap_conn_t *dap_conn_resolve(dap_conn_handle_t a_h) {
    if (!a_h.c) return NULL;
    uint64_t l_cur = atomic_load_explicit(&a_h.c->generation, memory_order_acquire);
    return (l_cur != 0 && l_cur == a_h.gen) ? a_h.c : NULL;
}

/**
 * @brief Return a connection to quarantine (two-phase commit).
 *
 *  Phase 1: fetch_add(q_tail) — reserves a ring position (cannot fail).
 *  Phase 2: release-store wfq_seq — "commits" the entry so drain can see it.
 *
 *  Between phases, wfq_seq is still the sentinel (-1) from the previous
 *  drain cycle.  drain() treats sentinel as "not yet committed" and stops.
 *
 *  Ordering:
 *    q_tail fetch_add: relaxed — position reservation, no data dependency.
 *    wfq_seq store: release — drain's acquire-load will see idx written above.
 */
DAP_STATIC_INLINE void
dap_conn_slab_free(dap_conn_slab_t *a_s, uint16_t a_idx, uint64_t a_wfq_seq)
{
    unsigned l_t = atomic_fetch_add_explicit(&a_s->q_tail, 1, memory_order_relaxed);
    dap_conn_slab_entry_t *l_e = &a_s->queue[l_t % DAP_CONN_SLAB_MAX];
    l_e->idx = a_idx;                  /* plain store: only this thread writes here */
    atomic_store_explicit(&l_e->wfq_seq, a_wfq_seq, memory_order_release);
}

/** @brief Release resources owned by a connection slot (ext, OLBs).
 *
 *  Called from dap_conn_slab_drain (per-conn close) and from
 *  dap_conn_slab_destroy (per-slab teardown).  Out-of-line because both
 *  sites are cold and the body already performs a user-provided
 *  destructor plus up to two OLB destroys (munmap syscalls); call
 *  overhead is strictly dominated by the actual work. */
void dap_conn_slot_cleanup(dap_conn_t *a_c);

/**
 * @brief Drain quarantine — reclaim slots whose WFQ tasks are fully processed.
 *
 *  Walks from q_ready towards q_tail, checking each entry:
 *
 *    wfq_seq == sentinel (-1) → free() hasn't committed yet → stop
 *    a_wfq_head < wfq_seq    → processor hasn't reached this task → stop
 *    otherwise                → safe to reclaim: cleanup OLBs, call ext_dtor
 *
 *  CAS on q_ready ensures exactly one winner per slot when called concurrently.
 *  After cleanup, wfq_seq is reset to sentinel for the next alloc→free cycle.
 *
 *  Ordering:
 *    q_tail load: acquire — sees free's committed wfq_seq.
 *    wfq_seq load: acquire — sees idx written by free() before the release-store.
 *    q_ready CAS: acq_rel — publish cleanup to alloc (which reads q_ready).
 */
DAP_STATIC_INLINE void dap_conn_slab_drain(dap_conn_slab_t *a_s, uint64_t a_wfq_head)
{
    unsigned l_r = atomic_load_explicit(&a_s->q_ready, memory_order_relaxed);
    for (;;) {
        if (l_r == atomic_load_explicit(&a_s->q_tail, memory_order_acquire))
            break;                      /* quarantine empty */
        dap_conn_slab_entry_t *l_e = &a_s->queue[l_r % DAP_CONN_SLAB_MAX];
        uint64_t l_seq = atomic_load_explicit(&l_e->wfq_seq, memory_order_acquire);
        if (l_seq == (uint64_t)-1)
            break;                      /* free() in progress — not committed yet */
        if (a_wfq_head < l_seq)
            break;                      /* processor hasn't consumed this conn's tasks */
        if (!atomic_compare_exchange_weak_explicit(&a_s->q_ready, &l_r, l_r + 1,
                                                    memory_order_acq_rel,
                                                    memory_order_relaxed))
            continue;                   /* lost CAS race — retry with updated l_r */
        dap_conn_slot_cleanup(dap_conn_slab_slot(a_s, l_e->idx));
        atomic_store_explicit(&l_e->wfq_seq, (uint64_t)-1, memory_order_relaxed);
        l_r = l_r + 1;
    }
}

/**
 * @brief Destroy the slab: cleanup all live slots, then release backing memory.
 *
 * A slot whose generation has never been bumped (generation == 0) was
 * never allocated — its backing page is almost certainly still the
 * kernel's zero-page.  Reading generation from it only triggers a
 * minor fault to the shared zero-page, not a real CoW, so the scan
 * stays cheap even when the slab is mostly idle.  Non-zero generation
 * flags a slot that once held resources; re-running slot_cleanup is
 * idempotent because it null-checks olb/ext before touching them.
 */
void dap_conn_slab_destroy(dap_conn_slab_t *a_s);
