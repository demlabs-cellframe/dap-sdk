/**
 * @file dap_proc_msg.h
 * @brief Built-in ext-stack message types and their post helpers.
 *
 * These are the standard dap_msg_t subtypes that the processor can
 * receive through the Treiber ext-stack:
 *   - callback  — fire-and-forget fn(arg)
 *   - heap      — external payload with cleanup policy
 *   - timer     — insertion into the processor timer list
 *
 * Separated from the processor loop TU to keep the runtime loop free
 * of concrete message knowledge.  The loop only sees msg->execute().
 *
 * Self-contained: includes @ref dap_proc.h for @c dap_proc_ctx_t
 * and @c dap_proc_post().
 */
#pragma once

#include "dap_proc.h"

/* ================================================================== */
/*  Callback message: fire-and-forget fn(arg)                          */
/* ================================================================== */

DAP_MSG_TYPE(s_callback_msg_t,
    void (*fn)(void *);
    void *arg
);

DAP_STATIC_INLINE dap_msg_rc_t s_callback_msg_exec(dap_msg_t *self)
{
    s_callback_msg_t *m = DAP_MSG_CAST(s_callback_msg_t, self);
    if (m->fn) m->fn(m->arg);
    DAP_MSG_FREE(self);
}

DAP_STATIC_INLINE bool
dap_msg_post_callback(dap_proc_ctx_t *a_proc,
                      void (*a_fn)(void *), void *a_arg)
{
    s_callback_msg_t *l_m = DAP_MSG_ALLOC(s_callback_msg_t, s_callback_msg_exec);
    if (__builtin_expect(!l_m, 0)) return false;
    l_m->fn  = a_fn;
    l_m->arg = a_arg;
    dap_proc_post(a_proc, &l_m->_msg);
    return true;
}

/* ================================================================== */
/*  Heap message: external payload with cleanup policy                 */
/* ================================================================== */

DAP_MSG_TYPE(s_heap_msg_t,
    void    *ptr;
    uint32_t len;
    dap_heap_cleanup_fn cleanup
);

DAP_STATIC_INLINE dap_msg_rc_t s_heap_msg_exec(dap_msg_t *self)
{
    dap_proc_ctx_t *l_ctx = dap_tls_proc;
    s_heap_msg_t *m = DAP_MSG_CAST(s_heap_msg_t, self);
    if (l_ctx && l_ctx->heap_cb) {
        const dap_heap_task_t l_ht = { .ptr = m->ptr, .len = m->len,
                                        .cleanup = m->cleanup };
        dap_msg_rc_t l_rc = l_ctx->heap_cb(&l_ht, l_ctx->_inheritor);
        if (l_rc == DAP_MSG_DEFER && !l_ctx->force_complete)
            return DAP_MSG_DEFER;
    }
    if (m->cleanup)
        m->cleanup(m->ptr);
    DAP_MSG_FREE(self);
}

DAP_STATIC_INLINE bool
dap_msg_post_heap(dap_proc_ctx_t *a_proc,
                  void *a_ptr, uint32_t a_len, dap_heap_cleanup_fn a_cleanup)
{
    s_heap_msg_t *l_m = DAP_MSG_ALLOC(s_heap_msg_t, s_heap_msg_exec);
    if (__builtin_expect(!l_m, 0)) return false;
    l_m->ptr     = a_ptr;
    l_m->len     = a_len;
    l_m->cleanup = a_cleanup;
    dap_proc_post(a_proc, &l_m->_msg);
    return true;
}

/* ================================================================== */
/*  Timer message: allocate into owner-thread timer list               */
/* ================================================================== */

DAP_MSG_TYPE(s_timer_msg_t,
    dap_time_t      delay;
    dap_time_t      interval;
    dap_timer_cb_t  exec;
    void           *arg;
    uint64_t        id;
    uint32_t        iterations
);

DAP_STATIC_INLINE dap_msg_rc_t s_timer_msg_exec(dap_msg_t *self)
{
    dap_proc_ctx_t *l_ctx = dap_tls_proc;
    s_timer_msg_t *m = DAP_MSG_CAST(s_timer_msg_t, self);
    if (l_ctx) {
        dap_timer_t *l_t = dap_timer_alloc(&l_ctx->timers);
        if (l_t) {
            l_t->id         = m->id;
            l_t->deadline   = dap_time_add(dap_time_now(), m->delay);
            l_t->interval   = m->interval;
            l_t->iterations = m->iterations;
            l_t->exec       = m->exec;
            l_t->arg        = m->arg;
            s_timer_hash_insert(&l_ctx->timers, l_t);
            dap_timer_insert(&l_ctx->timers, l_t);
        }
    }
    DAP_MSG_FREE(self);
}

/** @brief Post a timer request to a processor via ext-stack.
 *  Returns a self-routing handle (0 on failure). */
DAP_STATIC_INLINE dap_timer_handle_t
dap_msg_post_timer(dap_proc_ctx_t *a_proc,
                   uint8_t a_proc_idx, uint8_t a_worker_slot,
                   uint64_t a_delay_us, uint64_t a_interval_us,
                   uint32_t a_iterations,
                   dap_timer_cb_t a_exec, void *a_arg)
{
    s_timer_msg_t *l_m = DAP_MSG_ALLOC(s_timer_msg_t, s_timer_msg_exec);
    if (__builtin_expect(!l_m, 0)) return DAP_TIMER_HANDLE_NULL;
    uint64_t l_lid = dap_timer_gen_local_id();
    l_m->id         = l_lid;
    l_m->delay      = dap_time_from_us(a_delay_us);
    l_m->interval   = dap_time_from_us(a_interval_us);
    l_m->iterations = a_iterations;
    l_m->exec       = a_exec;
    l_m->arg        = a_arg;
    dap_proc_post(a_proc, &l_m->_msg);
    return dap_timer_make_handle(a_proc_idx, a_worker_slot, l_lid);
}
