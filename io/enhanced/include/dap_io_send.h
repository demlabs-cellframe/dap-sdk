/**
 * @file dap_io_send.h
 * @brief Normal public send surface — @c dap_io_tx_send_direct, @c dap_io_tx_send,
 *  and @c dap_io_send_rc_to_msg_rc (cross-thread and owner-thread enqueue into send OLB).
 *
 *  Include this header from protocol code; worker lane helpers live in
 *  @ref dap_worker_send.h.
 */
#pragma once

#include "dap_msg.h"
#include "dap_worker_ipc.h"
#include "dap_worker_types.h"
#include "dap_send_olb.h"

extern DAP_THREADLOCAL dap_worker_t *dap_tls_worker;

/* ================================================================== */
/*  Unified public write API — dap_io_tx_send / dap_io_tx_send_direct  */
/* ================================================================== */

/**
 * @brief Write to a connection whose pointer is known to be live.
 *
 * Use this form INSIDE an owner-side callback — read_cb, write_cb,
 * error_cb, parse_fn — where the worker has just handed you the
 * dap_conn_t * as an argument.  The owning worker is currently busy
 * running your callback and therefore cannot quarantine the slot,
 * so the pointer is guaranteed stable for the duration of the call.
 *
 * Also safe immediately after dap_io_conn_open() on the thread that
 * opened the connection, because the slot cannot be recycled before
 * it enters the owning worker's poll set.
 *
 * Does NOT check the generation counter — it trusts the caller's
 * lifetime contract.  If you only have a dap_conn_handle_t (and
 * therefore cannot prove liveness), call dap_io_tx_send() instead.
 *
 * All the runtime plumbing (watermark-driven DAP_CONN_SUSPENDED
 * on the read side, DAP_CONN_SEND_BUSY hand-off to dap_worker_tx_flush,
 * Dekker notify + eventfd kick) happens inside — the caller just
 * supplies the bytes.
 *
 * Returns DAP_SEND_CLOSED when the connection is already closed or
 * has no send_olb; DAP_SEND_TOO_LARGE when the payload exceeds send_olb
 * capacity; DAP_SEND_OVERFLOW when send_olb is transiently full (SUSPENDED
 * raised); DAP_SEND_OK on success (post-write watermark may still set SUSPENDED).
 */
DAP_STATIC_INLINE dap_send_rc_t
dap_io_tx_send_direct(dap_conn_t *a_c, const void *a_data, size_t a_len)
{
    if (__builtin_expect(!a_c || !a_c->send_olb, 0))
        return DAP_SEND_CLOSED;
    if (__builtin_expect((dap_conn_state(a_c) & DAP_CONN_CLOSED) != 0, 0))
        return DAP_SEND_CLOSED;

    dap_send_olb_result_t l_wr = dap_send_olb_write(a_c->send_olb, a_data, a_len);
    if (__builtin_expect(l_wr != DAP_SEND_OLB_OK, 0)) {
        if (l_wr == DAP_SEND_OLB_TOO_LARGE)
            return DAP_SEND_TOO_LARGE;
        atomic_fetch_or_explicit(&a_c->state, DAP_CONN_SUSPENDED,
                                  memory_order_release);
        return DAP_SEND_OVERFLOW;
    }

    /* Post-write watermark: raise SUSPENDED when free space drops
     * below compact_threshold, so the next read_cb invocation is
     * skipped until dap_worker_tx_flush relieves the gate. */
    if (a_c->send_olb->compact_threshold
        && dap_send_olb_free(a_c->send_olb) < a_c->send_olb->compact_threshold)
    {
        atomic_fetch_or_explicit(&a_c->state, DAP_CONN_SUSPENDED,
                                  memory_order_release);
    }

    /* Same-thread owner fast path: we are inside the owning worker's
     * loop iteration (read_cb / parse_fn / ctrl_stack handler / timer
     * callback).  The post-read_cb dap_worker_tx_flush or the drain_pending
     * sweep that follows ctrl_stack drain will empty send_olb within
     * this same iteration — so we must make the slot visible to that
     * sweep, but we do NOT need the full cross-thread Dekker handshake:
     *   - no seq_cst barrier (no racing processor is involved);
     *   - no eventfd kick (the worker is us, we are not sleeping).
     * Release-set SEND_BUSY and mark pending_bit cheaply. */
    dap_worker_t *l_owner = (dap_worker_t *)atomic_load_explicit(
        &a_c->_owner, memory_order_relaxed);
    if (__builtin_expect(l_owner != NULL && l_owner == dap_tls_worker, 1)) {
        atomic_fetch_or_explicit(&a_c->state, DAP_CONN_SEND_BUSY,
                                  memory_order_release);
        dap_slab_bits_set(l_owner->pending_bits,
                          dap_conn_slab_idx(l_owner->conn_slab, a_c));
        return DAP_SEND_OK;
    }
    /* Cross-thread path: full Dekker handshake with eventfd kick. */
    dap_worker_conn_notify_send(a_c);
    return DAP_SEND_OK;
}

/* ------------------------------------------------------------------ */
/*  Slow-path cross-thread send: implementation in dap_worker_ipc.c.    */
/*  Payload is copied into a ctrl message with a trailing flexible     */
/*  buffer, posted onto the owner's ctrl_stack, and drained there —    */
/*  so send_olb is only ever touched from the owning worker.  The     */
/*  msg execute callback lives in dap_worker_ipc.c.                   */
/* ------------------------------------------------------------------ */

dap_send_rc_t dap_worker_ctrl_send(dap_worker_t *a_owner,
                                             dap_conn_handle_t a_h,
                                             const void *a_data, size_t a_len);

/**
 * @brief Queue a response on the connection addressed by @a a_h.
 *
 * THE public cross-thread write primitive.  Works from any thread —
 * worker, processor, external — with one rule: the send is always
 * executed in the owning worker's thread.  When the caller IS that
 * worker (the common fast path inside an owner-side callback that
 * only has the handle at hand), the call folds into an inline
 * dap_io_tx_send_direct() with a single generation re-check — no
 * heap allocation, no eventfd wake, no ctrl_stack traffic.
 *
 * When the caller is somewhere else (a processor's batch_cb, a
 * broadcast fan-out from another worker, an external coroutine),
 * the payload is copied into a small heap node, pushed onto the
 * owner's ctrl_stack, and the owner is kicked.  The owner drains
 * its ctrl_stack on the very next iteration, re-validates the
 * handle, and performs the write in its own thread — so send_olb
 * is only ever touched from the single thread that owns it, which
 * means no RCU / no per-processor epoch coordination is needed
 * for the send path to be correct under arbitrary processor counts.
 *
 * Backpressure is automatic: dap_io_tx_send_direct raises
 * DAP_CONN_SUSPENDED when send_olb free space drops below the
 * connection's watermark; dap_worker_tx_flush clears it once enough bytes
 * have gone out.  No user code participates in this — callers can
 * treat DAP_SEND_OK as "accepted, will be flushed at the runtime's
 * earliest opportunity".
 *
 * @param a_h     Handle captured at allocation time or carried in a
 *                WFQ / defer task.  Null or stale → DAP_SEND_CLOSED.
 * @param a_data  Payload bytes (copied on the slow path).
 * @param a_len   Payload size.
 *
 * @return DAP_SEND_OK         — fast path wrote to send_olb directly, or
 *                               slow path enqueued a send message.
 *         DAP_SEND_CLOSED     — handle is stale, connection is CLOSED,
 *                               or the slot has no owner yet.
 *         DAP_SEND_TOO_LARGE  — fast path only: payload exceeds send_olb
 *                               capacity; chunk or drop (not returned from
 *                               slow path synchronously).
 *         DAP_SEND_OVERFLOW   — fast path: send_olb full / backpressure
 *                               (SUSPENDED may be set).  Slow path: ctrl
 *                               message allocation failed; retry later.
 */
DAP_STATIC_INLINE dap_send_rc_t
dap_io_tx_send(dap_conn_handle_t a_h, const void *a_data, size_t a_len)
{
    if (__builtin_expect(!a_h.c, 0))
        return DAP_SEND_CLOSED;

    /* TLS fast path — caller is the owning worker.  _owner never zeros
     * on slot_cleanup and workers outlive the process, so this load
     * always yields a valid pointer; at worst it names the slot's
     * previous owner, and the gen re-check below rejects that case. */
    dap_worker_t *l_me    = dap_tls_worker;
    dap_worker_t *l_owner = (dap_worker_t *)atomic_load_explicit(
        &a_h.c->_owner, memory_order_relaxed);
    if (__builtin_expect(l_me != NULL && l_me == l_owner, 1)) {
        if (__builtin_expect(
                atomic_load_explicit(&a_h.c->generation,
                                     memory_order_acquire) != a_h.gen, 0))
            return DAP_SEND_CLOSED;
        return dap_io_tx_send_direct(a_h.c, a_data, a_len);
    }
    /* Slow path — owner is someone else (or we are outside any worker).
     * Route through the owner's ctrl_stack; the owner will resolve and
     * send_direct there. */
    return dap_worker_ctrl_send(l_owner, a_h, a_data, a_len);
}

/**
 * @brief Map @ref dap_send_rc_t from @c dap_io_tx_send* to @c dap_msg_rc_t for @ref dap_io_frame_rc_cb_t.
 */
DAP_STATIC_INLINE dap_msg_rc_t
dap_io_send_rc_to_msg_rc(dap_send_rc_t a_rc)
{
    switch (a_rc) {
    case DAP_SEND_OK:         return DAP_MSG_DONE;
    case DAP_SEND_OVERFLOW:  return DAP_MSG_DEFER;
    case DAP_SEND_CLOSED:    return DAP_MSG_DROP;
    case DAP_SEND_TOO_LARGE: return DAP_MSG_DROP;
    default:                 return DAP_MSG_DROP;
    }
}
