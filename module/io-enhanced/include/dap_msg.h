/**
 * @file dap_msg.h
 * @brief Universal linked message node and helper macros.
 *
 * Pure message contract: no transport, no platform, no application
 * dependencies.  The node carries no payload knowledge — concrete
 * types embed dap_msg_t as the first field (via DAP_MSG_TYPE) and
 * provide their own execute() handler.
 *
 * execute() takes only the node itself.  The owning thread's identity
 * is recovered through thread-local runtime pointers:
 *   - @c dap_tls_worker  — set inside a worker event loop;
 *   - @c dap_tls_proc    — set inside a processor main loop.
 * A handler knows which queue it was posted on (it was written for
 * that queue) and therefore which TLS to read.  This avoids the
 * "void *ctx, cast it blindly" pattern.
 *
 * Two drain contracts coexist in the system:
 *
 *   ext-stack (Treiber MPSC, heap-allocated dap_msg_t nodes):
 *     execute() returns dap_msg_rc_t {DONE, DEFER, DROP}.
 *     Handler owns the node lifetime — see dap_msg_drain_pending()
 *     in dap_proc_dispatch.h (processor TU).
 *
 *   SPSC lanes (WFQ byte buffer, value-copy):
 *     Typed callbacks return dap_msg_rc_t {DONE, DEFER, DROP}.
 *     Head always advances — DEFER entries are copied to the defer queue
 *     (heap-allocated dap_defer_entry_t) and retried each drain pass.
 *     No ownership — data is value-copied into the lane buffer.
 *     See s_proc_drain_lane() in dap_proc_dispatch.h (processor TU only).
 *
 * Posting is not part of the message contract — it belongs to the
 * transport layer.  See dap_proc_post() in dap_proc_thread_api.h.
 */
#pragma once

#include <stdlib.h>
#include <stdbool.h>

/* ================================================================== */
/*  Execute return code                                                */
/* ================================================================== */

typedef enum {
    DAP_MSG_DONE,   /* consumed: handler managed memory (freed or transferred ownership) */
    DAP_MSG_DEFER,  /* not ready: keep in pending, retry next drain pass (FIFO preserved) */
    DAP_MSG_DROP    /* discard: handler cleaned sub-resources, drain must free the node */
} dap_msg_rc_t;

/* ================================================================== */
/*  Base message node                                                  */
/* ================================================================== */

typedef struct dap_msg {
    struct dap_msg *next;
    /** Execute the message on its owning thread.
     *  The handler is written for a specific queue (worker ctrl_stack
     *  or processor ext_stack) and reads the owning context from the
     *  matching TLS pointer (dap_tls_worker / dap_tls_proc) when it
     *  needs runtime state — no opaque ctx parameter. */
    dap_msg_rc_t  (*execute)(struct dap_msg *self);
} dap_msg_t;

/* ================================================================== */
/*  Macros for custom message types                                    */
/*                                                                     */
/*  To add a new message type:                                         */
/*                                                                     */
/*  1. Define type — list fields like a normal struct body:             */
/*                                                                     */
/*       DAP_MSG_TYPE(my_query_msg_t,                                  */
/*           char *query;                                              */
/*           void (*on_done)(void *result)                             */
/*       );                                                            */
/*                                                                     */
/*  2. Write an execute handler — called once on the owning thread.   */
/*     Use DAP_MSG_CAST to get your type.  If you need the runtime    */
/*     context, read dap_tls_proc / dap_tls_worker — whichever fits   */
/*     the queue you are posting to.  Return DAP_MSG_DONE after       */
/*     freeing (or transferring ownership), DAP_MSG_DEFER to retry,   */
/*     or DAP_MSG_DROP if permanently unable (clean sub-resources     */
/*     first, drain will free the node):                              */
/*                                                                    */
/*       static dap_msg_rc_t s_query_exec(dap_msg_t *self) {          */
/*           my_query_msg_t *m = DAP_MSG_CAST(my_query_msg_t, self);  */
/*           m->on_done(run_query(m->query));                         */
/*           free(m->query);                                          */
/*           DAP_MSG_FREE(self);                                      */
/*       }                                                            */
/*                                                                     */
/*  3. Alloc, fill, post (dap_proc_post is in dap_proc_thread_api.h):  */
/*                                                                     */
/*       my_query_msg_t *m = DAP_MSG_ALLOC(my_query_msg_t,             */
/*                                          s_query_exec);             */
/*       if (!m) return false;                                         */
/*       m->query   = strdup(sql);                                     */
/*       m->on_done = cb;                                              */
/*       dap_proc_post(proc, &m->_msg);                                */
/* ================================================================== */

/** @brief Head+tail pair for O(1) append to pending message lists. */
typedef struct {
    dap_msg_t *head, *tail;
} dap_msg_pending_t;

#define DAP_MSG_PENDING_INIT { NULL, NULL }

/** Define a message struct: inserts dap_msg_t _msg as the first field. */
#define DAP_MSG_TYPE(name, ...)                                         \
    typedef struct name { dap_msg_t _msg; __VA_ARGS__; } name

/** Allocate message and set its execute handler.  Returns NULL on OOM. */
#define DAP_MSG_ALLOC(type, exec_fn)                                    \
    ({ type *_m = (type *)calloc(1, sizeof(type));                      \
       if (_m) _m->_msg.execute = (exec_fn); _m; })

/** Downcast dap_msg_t* to your concrete type inside execute(). */
#define DAP_MSG_CAST(type, self)  ((type *)(self))

/** Consume the message: free the node and return DONE. */
#define DAP_MSG_FREE(self)  do { free(self); return DAP_MSG_DONE; } while (0)

