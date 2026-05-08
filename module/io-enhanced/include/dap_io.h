/**
 * @file dap_io.h
 * @brief Normal public topology and setup entry point — automatic worker-to-processor layout.
 *
 *  Header surface (documentation): this file is the normal topology/setup entry; it
 *  intentionally keeps some expert-visible structs and cold-path inlines where the
 *  runtime model requires direct layout access for performance. Normal protocol setup
 *  uses @ref dap_io_proc_set_frame_cb and the other @c dap_io_proc_set_* helpers here,
 *  plus @ref dap_io_conn_open / @ref dap_io_conn_open_cfg from @ref dap_io_ops.h.
 *  Advanced queue/dispatch/defer details live in opt-in headers (see @ref dap_io_advanced.h).
 *
 *  Data flow (example: N=4 workers, M=2 processors):
 *
 *    Feeders ──► Workers (epoll + OLB) ──► WFQ ──► Processors
 *
 *    fd ─► Worker[0] (epoll+OLB) ─┐             ┌─► Proc[0].frame_cb
 *    fd ─► Worker[2] (epoll+OLB) ─┴─── WFQ[0] ──┘
 *    fd ─► Worker[1] (epoll+OLB) ─┐             ┌─► Proc[1].frame_cb
 *    fd ─► Worker[3] (epoll+OLB) ─┴─── WFQ[1] ──┘
 *
 *    Mapping:   worker[w] → proc[w % M], slot = w / M
 *    Kick-back: proc → worker via resume_conn (eventfd on Linux; platform wakeup on Windows)
 *
 *  Creates M independent WFQs, a shared connection slab, assigns each
 *  worker to proc[w % M].  per_proc = ceil(N / M) workers share one WFQ.
 *  WFQ lane layout: FAST[0..K-1] NORM[K..2K-1] BG[2K..3K-1], K = per_proc.
 *
 *  Usage:
 *    dap_io_t *io = dap_io_create(4, 2);
 *
 *    dap_io_proc_set_frame_cb(io, 0, my_frame_handler, &my_proc_data);
 *    Advanced users may still assign @c dap_proc_ctx_t fields directly.
 *
 *    Include @ref dap_io_ops.h for @c dap_io_conn_open, @c dap_io_conn_open_cfg, and
 *    @c dap_io_timer_cancel_async, and @ref dap_io_send.h for @c dap_io_tx_send / @c dap_io_tx_send_direct.
 *    The @c dap_io_ops.h file comment documents @c ext and the stock receive path.
 *
 *  Header dependency map (user-facing):
 *
 *              dap_io_advanced.h
 *              /      |        \
 *       dap_io_ops.h  |   dap_proc_frame_impl.h
 *              \      |        /
 *                 \   |   dap_proc_msg.h
 *                  \  |      /
 *                     dap_io.h
 *
 *  Include @ref dap_io_advanced.h when you need one of:
 *    - ext-stack messages to a processor from non-worker threads
 *      (@ref dap_proc_msg.h: @c dap_msg_post_callback / @c dap_msg_post_heap / @c dap_msg_post_timer);
 *    - custom WFQ lane message types or custom batch handling
 *      (set @c dap_proc_ctx_t::custom_cb / @c batch_cb; see @ref dap_proc_dispatch.h);
 *    - direct access to the built-in batch execution primitive
 *      (@ref dap_proc_exec_batch via @ref dap_proc_frame_impl.h).
 *
 *  Advanced / custom WFQ / @c batch_cb extensions: @ref dap_io_advanced.h
 *  (pull @ref dap_bus.h for the lane stack; this header does so below).
 *
 *  The setup / teardown routines below are out-of-line (see dap_io_lifecycle.c
 *  and dap_io_access.c):
 *  they run at process or connection lifetime frequency and bring in
 *  mmap/epoll_ctl/syscalls that dwarf any call overhead.  Inline accessors:
 *  @c dap_io_proc / @c dap_io_worker,
 *  @c dap_io_proc_set_* for cold-path processor callback setup (bounds-checked).
 */
#pragma once

#include "dap_io_plat.h"
#include "dap_bus.h"
#include "dap_io_stats.h"
#include "dap_worker.h"
#include "dap_proc.h"

typedef struct dap_io {
    unsigned             n_workers;
    unsigned             n_procs;
    unsigned             per_proc;   /* ceil(n_workers / n_procs) */
    dap_vmqueue_mpsc_t **wfqs;       /* [n_procs] — one WFQ per processor */
    dap_wfq_wait_state_t *wfq_waits;  /* [n_procs] — RESCAN bitmask per processor */
    dap_conn_slab_t     *slab;       /* shared connection slab */
    dap_worker_t    *workers;    /* [n_workers] aligned heap array */
    dap_proc_ctx_t      *procs;      /* [n_procs] heap array */
#ifdef DAP_IO_STATS
    dap_worker_stats_t  *worker_stats; /* [n_workers] cache-line aligned */
    dap_proc_stats_t    *proc_stats;   /* [n_procs] cache-line aligned */
#endif
    _Atomic(bool)        shutdown;   /* release-store by shutdown(), acquire-load in proc loop */
} dap_io_t;

/** @brief Bounds-checked accessor for a processor context by index; @a a_io must be non-NULL. */
DAP_STATIC_INLINE dap_proc_ctx_t *dap_io_proc(dap_io_t *a_io, unsigned a_idx) {
    return a_idx < a_io->n_procs ? a_io->procs + a_idx : NULL;
}
/** @brief Bounds-checked accessor for a worker context by index; @a a_io must be non-NULL. */
DAP_STATIC_INLINE dap_worker_t *dap_io_worker(dap_io_t *a_io, unsigned a_idx) {
    return a_idx < a_io->n_workers ? &a_io->workers[a_idx] : NULL;
}

/** @brief Set @c frame_cb and @c _inheritor. If @a a_cb is non-NULL, clears @c batch_cb
 *         and @c frame_rc_cb (built-in void frame path is exclusive). */
DAP_STATIC_INLINE bool
dap_io_proc_set_frame_cb(dap_io_t *a_io, unsigned a_proc_idx,
                         dap_proc_batch_cb_t a_cb, void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->frame_cb = a_cb;
    l_p->_inheritor = a_arg;
    if (a_cb) {
        l_p->batch_cb = NULL;
        l_p->frame_rc_cb = NULL;
    }
    return true;
}

/** @brief Set @c frame_rc_cb and @c _inheritor. If @a a_cb is non-NULL, clears @c frame_cb
 *         and @c batch_cb (return-code frame path is exclusive). */
DAP_STATIC_INLINE bool
dap_io_proc_set_frame_rc_cb(dap_io_t *a_io, unsigned a_proc_idx,
                            dap_io_frame_rc_cb_t a_cb, void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->frame_rc_cb = a_cb;
    l_p->_inheritor = a_arg;
    if (a_cb) {
        l_p->frame_cb = NULL;
        l_p->batch_cb = NULL;
    }
    return true;
}

/** @brief Set @c batch_cb and @c _inheritor. If @a a_cb is non-NULL, clears @c frame_cb
 *         and @c frame_rc_cb to avoid mixing the custom batch drain with built-in frame paths. */
DAP_STATIC_INLINE bool
dap_io_proc_set_batch_cb(dap_io_t *a_io, unsigned a_proc_idx,
                         dap_msg_batch_cb_t a_cb, void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->batch_cb = a_cb;
    l_p->_inheritor = a_arg;
    if (a_cb) {
        l_p->frame_cb = NULL;
        l_p->frame_rc_cb = NULL;
    }
    return true;
}

/** @brief Set @c heap_cb and @c _inheritor (does not alter @c frame_cb / @c batch_cb). */
DAP_STATIC_INLINE bool
dap_io_proc_set_heap_cb(dap_io_t *a_io, unsigned a_proc_idx,
                        dap_msg_heap_cb_t a_cb, void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->heap_cb = a_cb;
    l_p->_inheritor = a_arg;
    return true;
}

/** @brief Set @c callback_cb and @c _inheritor (does not alter @c frame_cb / @c batch_cb). */
DAP_STATIC_INLINE bool
dap_io_proc_set_callback_cb(dap_io_t *a_io, unsigned a_proc_idx,
                            dap_msg_callback_cb_t a_cb, void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->callback_cb = a_cb;
    l_p->_inheritor = a_arg;
    return true;
}

/** @brief Set @c custom_cb and @c _inheritor (does not alter @c frame_cb / @c batch_cb). */
DAP_STATIC_INLINE bool
dap_io_proc_set_custom_cb(dap_io_t *a_io, unsigned a_proc_idx,
                          dap_msg_rc_t (*a_cb)(dap_vmqueue_hdr_t *, void *), void *a_arg)
{
    if (!a_io || a_proc_idx >= a_io->n_procs)
        return false;
    dap_proc_ctx_t *l_p = a_io->procs + a_proc_idx;
    l_p->custom_cb = a_cb;
    l_p->_inheritor = a_arg;
    return true;
}

/**
 * @brief Create a fully initialised I/O topology.
 *
 * Allocates and wires up: WFQs, connection slab, workers (epoll +
 * disarmed timerfd + resume_conn), and processors (timers + ext_stack).
 *
 * After this call the caller only needs to:
 *   - set processor callbacks via @ref dap_io_proc_set_frame_cb, @ref dap_io_proc_set_frame_rc_cb,
 *     @ref dap_io_proc_set_batch_cb,
 *     @ref dap_io_proc_set_heap_cb, @ref dap_io_proc_set_callback_cb, and/or
 *     @ref dap_io_proc_set_custom_cb (or assign @c dap_proc_ctx_t fields directly for advanced use)
 *   - optionally add timers via @ref dap_worker_timer_add()
 *   - open user connections via @ref dap_io_conn_open() / @ref dap_io_conn_open_cfg in @ref dap_io_ops.h
 *     (file-level documentation there covers receive @c ext layout and
 *     @c dap_io_tx_send / @c dap_io_tx_send_direct from @ref dap_io_send.h).
 *
 * @param a_nw  Number of workers.
 * @param a_np  Number of processors (must be >= 1, <= a_nw).
 * @return Heap-allocated topology, or NULL on failure.
 */
dap_io_t *dap_io_create(unsigned a_nw, unsigned a_np);

/** @brief Tear down the I/O topology: workers, processors, WFQs, slab. */
void dap_io_destroy(dap_io_t *a_io);

/** @brief Signal shutdown to all processors and wake them from futex sleep. */
void dap_io_shutdown(dap_io_t *a_io);
