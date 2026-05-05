/**
 * @file dap_proc_dispatch.h
 * @brief Advanced/internal — WFQ lane drain + @c s_proc_dispatch (processor hot path).
 *
 * Include only from @c dap_proc_thread_enh.c (after @ref dap_proc.h).
 * Keeps @c dap_proc_exec_batch and the per-message drain loop out of the
 * transitive @ref dap_io.h graph so normal app TUs do not parse or inline
 * this machine unless they include this header on purpose.
 */
#pragma once

#include "dap_proc.h"
#include "dap_proc_exec.h"

/**
 * @brief Processor-side lane message dispatcher.
 *
 * Returns dap_msg_rc_t — DONE/DEFER/DROP.  Never pushes to defer itself;
 * the caller (s_proc_drain_lane) handles DEFER by copying the task to the
 * defer queue.  Head always advances regardless of the return code.
 */
DAP_STATIC_INLINE dap_msg_rc_t
s_proc_dispatch(dap_vmqueue_hdr_t *a_hdr, dap_proc_ctx_t *a_ctx)
{
    const void *l_payload = a_hdr->payload;
    switch (a_hdr->type) {
    case DAP_MSG_BATCH: {
        const dap_batch_task_t *l_t = (const dap_batch_task_t *)l_payload;
        uint16_t l_idx = a_ctx->slab
                       ? dap_conn_slab_idx(a_ctx->slab, l_t->conn.c)
                       : 0;
        if (a_ctx->slab && dap_slab_bits_test(a_ctx->defer_q.mask, l_idx))
            return DAP_MSG_DEFER;
        if (a_ctx->batch_cb)
            return a_ctx->batch_cb(l_t, a_ctx->_inheritor);
        if (a_ctx->frame_rc_cb && a_ctx->slab) {
            dap_msg_rc_t l_mrc = dap_proc_exec_batch_rc(
                l_t, a_ctx->force_complete,
                a_ctx->frame_rc_cb, a_ctx->_inheritor);
            switch (l_mrc) {
            case DAP_MSG_DEFER:
                dap_stat(a_ctx->stats, batches_deferred, ++);
                return DAP_MSG_DEFER;
            case DAP_MSG_DROP:
                dap_stat(a_ctx->stats, batches_stale, ++);
                return DAP_MSG_DROP;
            default:
                dap_stat(a_ctx->stats, batches_ok, ++);
                return DAP_MSG_DONE;
            }
        }
        if (a_ctx->frame_cb && a_ctx->slab) {
            dap_proc_exec_result_t l_r = dap_proc_exec_batch(
                l_t, a_ctx->force_complete,
                a_ctx->frame_cb, a_ctx->_inheritor);
            switch (l_r) {
            case DAP_PROC_EXEC_DEFERRED:
                dap_stat(a_ctx->stats, batches_deferred, ++);
                return DAP_MSG_DEFER;
            case DAP_PROC_EXEC_STALE:
                dap_stat(a_ctx->stats, batches_stale, ++);
                return DAP_MSG_DROP;
            default:
                dap_stat(a_ctx->stats, batches_ok, ++);
                return DAP_MSG_DONE;
            }
        }
        return DAP_MSG_DONE;
    }
    case DAP_MSG_CALLBACK: {
        const dap_callback_task_t *l_ct = (const dap_callback_task_t *)l_payload;
        dap_msg_rc_t l_rc = DAP_MSG_DONE;
        if (a_ctx->callback_cb)
            l_rc = a_ctx->callback_cb(l_ct, a_ctx->_inheritor);
        if (l_rc == DAP_MSG_DONE) {
            if (l_ct->fn)
                l_ct->fn(l_ct->arg);
            dap_stat(a_ctx->stats, callbacks, ++);
        }
        return l_rc;
    }
    case DAP_MSG_HEAP: {
        const dap_heap_task_t *l_ht = (const dap_heap_task_t *)l_payload;
        dap_msg_rc_t l_rc = DAP_MSG_DONE;
        if (a_ctx->heap_cb)
            l_rc = a_ctx->heap_cb(l_ht, a_ctx->_inheritor);
        if (l_rc != DAP_MSG_DEFER) {
            if (l_ht->cleanup)
                l_ht->cleanup(l_ht->ptr);
            dap_stat(a_ctx->stats, heap_tasks, ++);
        }
        return l_rc;
    }
    case DAP_MSG_TIMER: {
        const dap_timer_request_t *l_req = (const dap_timer_request_t *)l_payload;
        dap_timer_t *l_t = dap_timer_alloc(&a_ctx->timers);
        if (l_t) {
            l_t->id         = l_req->id;
            l_t->deadline   = dap_time_add(dap_time_now(), l_req->delay);
            l_t->interval   = l_req->interval;
            l_t->iterations = l_req->iterations;
            l_t->exec       = l_req->exec;
            l_t->arg        = l_req->arg;
            s_timer_hash_insert(&a_ctx->timers, l_t);
            dap_timer_insert(&a_ctx->timers, l_t);
        }
        return DAP_MSG_DONE;
    }
    default:
        if (a_ctx->custom_cb)
            return a_ctx->custom_cb(a_hdr, a_ctx->_inheritor);
        return DAP_MSG_DONE;
    }
}

/** @brief Push a deferred message from WFQ header to the defer queue. */
DAP_STATIC_INLINE bool
s_defer_push_from_hdr(dap_defer_queue_t *a_q, const dap_vmqueue_hdr_t *a_hdr,
                       dap_proc_ctx_t *a_ctx)
{
    const void *l_p = a_hdr->payload;
    switch (a_hdr->type) {
    case DAP_MSG_BATCH: {
        const dap_batch_task_t *l_t = (const dap_batch_task_t *)l_p;
        uint16_t l_idx = a_ctx->slab
                       ? dap_conn_slab_idx(a_ctx->slab, l_t->conn.c)
                       : 0;
        return dap_defer_push_batch(a_q, l_t, l_idx);
    }
    case DAP_MSG_CALLBACK: return dap_defer_push_callback(a_q, (const dap_callback_task_t *)l_p);
    case DAP_MSG_HEAP:     return dap_defer_push_heap(a_q, (const dap_heap_task_t *)l_p);
    default:               return dap_defer_push_custom(a_q, a_hdr);
    }
}

/** @brief Drain one lane up to @a a_max messages via s_proc_dispatch. */
DAP_STATIC_INLINE size_t
s_proc_drain_lane(_Atomic(uint64_t) *a_tg, _Atomic(uint64_t) *a_hg,
                  _Atomic(uint32_t) *a_pw, char *a_data,
                  dap_proc_ctx_t *a_ctx, size_t a_max)
{
    s_vmq_drain_pos_t l_ = s_vmq_drain_begin(a_tg, a_hg, a_pw);
    size_t l_n = 0;
    dap_vmqueue_hdr_t *l_hdr;
    while (l_n < a_max && (l_hdr = s_vmq_drain_peek(a_data, &l_))) {
        dap_msg_rc_t l_rc = s_proc_dispatch(l_hdr, a_ctx);
        l_.head += l_.step;
        ++l_n;
        if (l_rc == DAP_MSG_DEFER
            && !s_defer_push_from_hdr(&a_ctx->defer_q, l_hdr, a_ctx))
        {
            switch (l_hdr->type) {
            case DAP_MSG_BATCH:
                /* Defer queue full after DEFER: force-complete recv batch without
                 * re-entering batch_cb; frame paths use exec_batch / exec_batch_rc(..., true).
                 * Non-frame path must still ack + dap_worker_after_batch_processed. */
                if (!a_ctx->batch_cb && a_ctx->frame_rc_cb && a_ctx->slab) {
                    const dap_batch_task_t *l_t =
                        (const dap_batch_task_t *)l_hdr->payload;
                    dap_proc_exec_batch_rc(l_t, true, a_ctx->frame_rc_cb,
                                          a_ctx->_inheritor);
                } else if (!a_ctx->batch_cb && a_ctx->frame_cb && a_ctx->slab) {
                    const dap_batch_task_t *l_t =
                        (const dap_batch_task_t *)l_hdr->payload;
                    dap_proc_exec_batch(l_t, true,
                                        a_ctx->frame_cb, a_ctx->_inheritor);
                } else if (a_ctx->slab) {
                    const dap_batch_task_t *l_t =
                        (const dap_batch_task_t *)l_hdr->payload;
                    dap_conn_t *l_c = dap_conn_resolve(l_t->conn);
                    if (l_c) {
                        uint64_t l_ack = atomic_load_explicit(
                            &l_c->olb->ack_pos, memory_order_relaxed);
                        uint32_t l_bytes = l_t->batch_end - (uint32_t)l_ack;
                        if (l_bytes && l_bytes <= l_c->olb->capacity) {
                            dap_vmqolb_ack(l_c->olb, l_bytes);
                            dap_worker_after_batch_processed(l_c);
                        }
                    }
                }
                break;
            case DAP_MSG_HEAP: {
                const dap_heap_task_t *l_ht =
                    (const dap_heap_task_t *)l_hdr->payload;
                if (l_ht->cleanup) l_ht->cleanup(l_ht->ptr);
                break;
            }
            default:
                break;
            }
            dap_stat(a_ctx->stats, defer_oom, ++);
        }
    }
    if (l_n) s_vmq_commit_head(a_hg, l_.gen, l_.head, a_pw);
    else     s_vmq_ack_waiter(a_pw);
    return l_n;
}

DAP_STATIC_INLINE size_t
s_proc_drain_range(dap_vmqueue_mpsc_t *a_q, unsigned a_from, unsigned a_to,
                   dap_proc_ctx_t *a_ctx, size_t a_max_per_lane)
{
    size_t l_total = 0;
    for (unsigned i = a_from; i < a_to; ++i)
        l_total += s_proc_drain_lane(s_mpsc_tg(a_q, i), s_mpsc_hg(a_q, i),
                                      s_mpsc_pw(a_q, i), s_mpsc_data(a_q, i),
                                      a_ctx, a_max_per_lane);
    return l_total;
}

DAP_STATIC_INLINE size_t
dap_msg_drain_pending(dap_msg_pending_t *a_p)
{
    dap_msg_t *l_cur = a_p->head;
    if (!l_cur) return 0;
    dap_msg_t *l_saved_tail = a_p->tail;
    a_p->head = a_p->tail = NULL;
    size_t l_n = 0;
    while (l_cur && l_n < DAP_EXT_DRAIN_QUOTA) {
        dap_msg_t *l_next = l_cur->next;
        switch (l_cur->execute(l_cur)) {
        case DAP_MSG_DONE:
            break;
        case DAP_MSG_DEFER:
            a_p->head = l_cur;
            a_p->tail = l_saved_tail;
            return l_n;
        case DAP_MSG_DROP:
            free(l_cur);
            break;
        }
        l_cur = l_next;
        ++l_n;
    }
    if (l_cur) {
        a_p->head = l_cur;
        a_p->tail = l_saved_tail;
    }
    return l_n;
}

DAP_STATIC_INLINE void
dap_msg_harvest(dap_msg_stack_t *a_stack, dap_msg_pending_t *a_p)
{
    dap_msg_t *l_chain = dap_msg_stack_detach(a_stack);
    if (!l_chain) return;
    dap_msg_t *l_new_tail = l_chain;
    dap_msg_t *l_fifo = dap_msg_list_reverse(l_chain);
    if (!a_p->head)
        a_p->head = l_fifo;
    else
        a_p->tail->next = l_fifo;
    a_p->tail = l_new_tail;
}

DAP_STATIC_INLINE size_t
dap_proc_drain(dap_vmqueue_mpsc_t *a_q, unsigned a_fe, unsigned a_ne,
               unsigned a_be, dap_msg_pending_t *a_ext, dap_proc_ctx_t *a_ctx)
{
    size_t l_d  = dap_msg_drain_pending(a_ext);
    l_d += s_proc_drain_range(a_q, 0,    a_fe, a_ctx, DAP_WFQ_FAST_QUOTA);
    l_d += s_proc_drain_range(a_q, a_fe, a_ne, a_ctx, DAP_WFQ_NORM_QUOTA);
    l_d += s_proc_drain_range(a_q, a_ne, a_be, a_ctx, DAP_WFQ_BG_QUOTA);
    return l_d;
}
