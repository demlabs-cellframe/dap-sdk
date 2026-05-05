/**
 * @file dap_proc_exec.h
 * @brief Advanced/internal — processor-side recv batch → @c frame_cb → recv ack + post-batch hook.
 *
 * Hot inlines used from @ref dap_proc_dispatch.h (processor TU) and from
 * @c dap_proc_thread_enh.c defer retry.  Not included from @ref dap_io.h
 * transitively — include explicitly for advanced/bench code, or use
 * @ref dap_proc_frame_impl.h as an umbrella.
 *
 * Does not copy inbound bytes into send_olb; send via @c dap_io_tx_send* or
 * @c dap_send_olb_write from @c frame_cb or a custom @c batch_cb.
 *
 * For custom @c batch_cb implementations that use @ref dap_msg_rc_t (DONE /
 * DEFER / DROP), @ref dap_proc_exec_batch_rc centralizes recv OLB resolve,
 * ack, and @c dap_worker_after_batch_processed — no implicit echo or send.
 */
#pragma once

#include "dap_batch_task.h"
#include "dap_msg.h"
#include "dap_proc_frame.h"
#include "dap_worker_ipc.h"

typedef enum {
    DAP_PROC_EXEC_OK,
    DAP_PROC_EXEC_DEFERRED,
    DAP_PROC_EXEC_STALE
} dap_proc_exec_result_t;

/**
 * @brief Low-level recv-batch helper: resolve @a a_task, invoke @a a_cb, own ack/post-batch.
 *
 * Intended for tests and advanced @c batch_cb paths that need explicit DEFER
 * handling without duplicating @c dap_conn_resolve, @c ack_pos / @c batch_end
 * arithmetic, @c dap_vmqolb_ack, and @c dap_worker_after_batch_processed.
 * Does not write send_olb or notify the worker for send — the callback must
 * perform any @c dap_send_olb_write / @c dap_io_tx_send* and notifications.
 *
 * Stale or invalid inputs (@c NULL conn after resolve, @c DAP_CONN_PURGE,
 * zero or oversized batch span): returns @c DAP_MSG_DROP without calling @a a_cb.
 *
 * @param a_force  If true and the callback returns @c DAP_MSG_DEFER, the batch
 *                 is acked and post-batch runs anyway, and the return value
 *                 is @c DAP_MSG_DONE (shutdown / force-complete must not spin).
 */
DAP_STATIC_INLINE dap_msg_rc_t
dap_proc_exec_batch_rc(const dap_batch_task_t *a_task, bool a_force,
                       dap_io_frame_rc_cb_t a_cb, void *a_cb_arg)
{
    dap_conn_t *l_c = dap_conn_resolve(a_task->conn);
    if (!l_c || (dap_conn_state(l_c) & DAP_CONN_PURGE))
        return DAP_MSG_DROP;
    dap_vmqueue_olb_t *l_olb = l_c->olb;
    uint64_t l_ack = atomic_load_explicit(&l_olb->ack_pos, memory_order_relaxed);
    uint32_t l_bytes = a_task->batch_end - (uint32_t)l_ack;
    if (!l_bytes || l_bytes > l_olb->capacity)
        return DAP_MSG_DROP;
    char *l_batch = l_olb->data + (size_t)l_ack;
    dap_msg_rc_t l_rc = a_cb ? a_cb(l_c, l_batch, l_bytes, a_cb_arg) : DAP_MSG_DONE;
    if (l_rc == DAP_MSG_DEFER) {
        if (!a_force)
            return DAP_MSG_DEFER;
        l_rc = DAP_MSG_DONE;
    }
    if (l_rc == DAP_MSG_DONE || l_rc == DAP_MSG_DROP) {
        dap_vmqolb_ack(l_olb, l_bytes);
        dap_worker_after_batch_processed(l_c);
    }
    return l_rc;
}

DAP_STATIC_INLINE dap_proc_exec_result_t
dap_proc_exec_batch(const dap_batch_task_t *a_task,
                     bool a_force, dap_proc_batch_cb_t a_cb, void *a_cb_arg)
{
    (void)a_force;
    dap_conn_t *a_c = dap_conn_resolve(a_task->conn);
    if (!a_c || (dap_conn_state(a_c) & DAP_CONN_PURGE))
        return DAP_PROC_EXEC_STALE;

    dap_vmqueue_olb_t *l_olb = a_c->olb;
    uint64_t l_ack = atomic_load_explicit(&l_olb->ack_pos, memory_order_relaxed);
    uint32_t l_bytes = a_task->batch_end - (uint32_t)l_ack;
    if (!l_bytes || l_bytes > l_olb->capacity)
        return DAP_PROC_EXEC_STALE;
    char *l_batch = l_olb->data + (size_t)l_ack;
    if (a_cb)
        a_cb(a_c, l_batch, l_bytes, a_cb_arg);
    dap_vmqolb_ack(l_olb, l_bytes);
    dap_worker_after_batch_processed(a_c);
    return DAP_PROC_EXEC_OK;
}
