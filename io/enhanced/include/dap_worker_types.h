/**
 * @file dap_worker_types.h
 * @brief Advanced/internal — @c dap_worker_t and worker ctrl-message node layout (data + typedef only).
 *
 * Split out of @ref dap_worker_reactor.h.  @ref dap_proc_exec.h should include
 * only @ref dap_worker_ipc.h (not this file); for the full @c dap_worker_t
 * definition include @ref dap_worker_reactor.h.
 */
#pragma once

#include "dap_bus.h"
#include "dap_conn.h"
#include "dap_io_stats.h"

/* Forward tags only: @c dap_worker_msg_execute_t appears before the full
 * @c dap_worker_msg_t body but references both worker and message pointers.
 * File-scope incomplete structs avoid GCC's "declared inside parameter list"
 * for the @c execute function type. */
struct dap_worker;
struct dap_worker_msg;

/** Ctrl messages are drained in the owner worker thread; @a_w is the owner. */
typedef dap_msg_rc_t (*dap_worker_msg_execute_t)(struct dap_worker     *a_w,
                                                 struct dap_worker_msg *a_self);

typedef struct dap_worker_msg {
    struct dap_worker_msg *next;
    dap_worker_msg_execute_t execute;
} dap_worker_msg_t;

typedef struct {
    _Atomic(dap_worker_msg_t *) top;
} dap_worker_msg_stack_t;

#define DAP_WORKER_MSG_STACK_INIT  { .top = NULL }

typedef struct dap_worker {
    _Alignas(DAP_VMQ_CACHELINE)
#ifdef DAP_OS_WINDOWS
    HANDLE              iocp;
#else
    int                 epfd;
#endif
    dap_vmqueue_mpsc_t *wfq;
    dap_wfq_wait_state_t *wfq_waiting;
    dap_conn_slab_t    *conn_slab;
    unsigned            conn_lane;
    unsigned            bg_lane;

    dap_conn_t         *resume_conn;
    dap_conn_t         *timer_conn;

    uint16_t            conn_head;     /* worker-only: head conn idx (UINT16_MAX = none) */

    _Atomic uint64_t    pending_bits[DAP_SLAB_BITS_WORDS];

    _Atomic(bool)       shutdown;

    dap_timers_t        timers;

    dap_worker_msg_stack_t ctrl_stack;

    dap_worker_stats_t     *stats;

    unsigned            worker_id;
    unsigned            n_workers;
    uint8_t             proc_idx;
} dap_worker_t;
