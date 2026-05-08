/**
 * @file dap_proc_thread_api.h
 * @brief Advanced/internal (opt-in for docs) — processor thread: full @c dap_proc_ctx_t, TLS,
 *  @c dap_proc_post, loop/shutdown.
 *
 * Pulled in by @ref dap_io.h for topology TUs; minimal protocol walkthroughs should cite
 * @ref dap_io_proc_set_* and @ref dap_proc_frame.h instead of leading with this layout.
 */
#pragma once

#include "dap_io_plat.h"
#include <stdatomic.h>

#include "dap_bus.h"
#include "dap_proc_defer.h"
#include "dap_proc_frame.h"
#include "dap_io_stats.h"

#ifndef DAP_MAX_WORKERS_PER_PROC
#define DAP_MAX_WORKERS_PER_PROC 64
#endif
_Static_assert(DAP_MAX_WORKERS_PER_PROC <= 64,
    "rescan_mask is uint64_t — cannot exceed 64 workers per processor");

#ifndef DAP_DEFER_RECHECK_MIN_MS
#define DAP_DEFER_RECHECK_MIN_MS 1U
#endif
#ifndef DAP_DEFER_RECHECK_MAX_MS
#define DAP_DEFER_RECHECK_MAX_MS 16U
#endif

/** @brief Processor context — single-threaded, fields ordered by access frequency. */
typedef struct dap_proc_ctx {
    dap_vmqueue_mpsc_t  *wfq;           /* every iteration: weighted drain */
    _Atomic(bool)       *shutdown;      /* every iteration: loop condition */
    dap_wfq_wait_state_t *wfq_waiting;  /* rescan_mask for worker kick-back */

    /* Typed drain callbacks — receive const pointer into lane buffer.
       s_proc_dispatch (dap_proc_dispatch.h) switches on hdr->type.
       Pointer valid for the callback duration; copy if needed later. */
    dap_msg_batch_cb_t    batch_cb;     /* DAP_MSG_BATCH  */
    dap_msg_callback_cb_t callback_cb;  /* DAP_MSG_CALLBACK */
    dap_msg_heap_cb_t     heap_cb;      /* DAP_MSG_HEAP */

    /* User-defined fallback for custom SPSC message types.
       Called for any type not handled by the built-in switch (0..3).
       Receives raw header — user casts (hdr+1) to their payload struct. */
    dap_msg_rc_t       (*custom_cb)(dap_vmqueue_hdr_t *hdr, void *inheritor);

    unsigned             n_workers;     /* loop init only (lane range computation) */
    bool                 force_complete; /* shutdown: force-complete deferred entries */

    /* Built-in batch pipeline: slab resolve + defer + user frame_cb / frame_rc_cb.
       When (frame_cb or frame_rc_cb) is set and batch_cb is NULL, dispatch handles
       the defer queue / exec_batch / exec_batch_rc / ack cycle automatically. */
    dap_conn_slab_t     *slab;
    dap_defer_queue_t    defer_q;
    dap_proc_batch_cb_t   frame_cb;     /* user: void batch */
    dap_io_frame_rc_cb_t  frame_rc_cb;   /* user: return-code batch (public) */

    dap_timers_t         timers;        /* every iteration: peek + optional drain */
    dap_msg_stack_t      ext_stack;     /* Treiber MPSC for non-worker threads */

#ifdef DAP_OS_WINDOWS
    HANDLE               worker_kicks[DAP_MAX_WORKERS_PER_PROC];
#else
    int                  worker_kick_fds[DAP_MAX_WORKERS_PER_PROC];
#endif

    dap_proc_stats_t    *stats;

    uint8_t              proc_idx;     /* processor group index */

    void                *_inheritor;
} dap_proc_ctx_t;

extern DAP_THREADLOCAL dap_proc_ctx_t *dap_tls_proc;

/** @brief Post a message to the processor's ext-stack and wake it. */
DAP_STATIC_INLINE void
dap_proc_post(dap_proc_ctx_t *a_proc, dap_msg_t *a_msg)
{
    dap_msg_stack_push(&a_proc->ext_stack, a_msg);
    dap_bus_proc_wake(a_proc->wfq);
}

void dap_proc_loop_run(dap_proc_ctx_t *a_ctx);
void dap_proc_shutdown(_Atomic(bool) *a_shutdown, dap_vmqueue_mpsc_t *a_wfq);

/**
 * @brief Select processor in round-robin order (thread-local counter).
 *
 * @param a_np  Number of processors
 * @return      Index of the selected processor
 */
DAP_STATIC_INLINE unsigned
dap_proc_select(unsigned a_np)
{
    if (__builtin_expect(!a_np, 0))
        return 0;
    static DAP_THREADLOCAL unsigned l_hint = 0;
    return l_hint++ % a_np;
}
