/**
 * @file dap_msg_types.h
 * @brief WFQ SPSC lane payload type descriptors.
 *
 * Batch lane payload: @ref dap_batch_task_t (@ref dap_conn_handle.h only).
 * Include @ref dap_conn.h where you call @c dap_conn_resolve or touch @c dap_conn_t.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <time.h>
#include "dap_batch_task.h"

/* ================================================================== */
/*  WFQ message type enum                                              */
/* ================================================================== */

typedef enum {
    DAP_MSG_BATCH    = 0,
    DAP_MSG_CALLBACK = 1,
    DAP_MSG_HEAP     = 2,
    DAP_MSG_TIMER    = 3,
} dap_wfq_msg_type_t;

/* ================================================================== */
/*  Payload descriptors (value-copied into SPSC lane buffer)           */
/* ================================================================== */

/* dap_batch_task_t — @ref dap_batch_task.h */

typedef struct {
    void (*fn)(void *);
    void  *arg;
} dap_callback_task_t;

typedef void (*dap_heap_cleanup_fn)(void *ptr);

typedef struct {
    void               *ptr;
    uint32_t            len;
    dap_heap_cleanup_fn cleanup;
} dap_heap_task_t;

/** Timer callback.
 *
 *  Receives only the user argument registered at schedule time.  The
 *  owning runtime context (worker or processor) is accessible via the
 *  matching TLS pointer (dap_tls_worker / dap_tls_proc) if the handler
 *  needs it — the queue the timer was posted to determines which one. */
typedef void (*dap_timer_cb_t)(void *a_arg);

typedef struct {
    struct timespec  delay;
    struct timespec  interval;
    dap_timer_cb_t   exec;
    void            *arg;
    uint64_t         id;         /**< pre-generated globally-unique timer ID */
    uint32_t         iterations; /**< 0 = infinite, 1 = one-shot, N = fire N times */
} dap_timer_request_t;
