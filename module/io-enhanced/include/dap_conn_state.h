/**
 * @file dap_conn_state.h
 * @brief Bit flags in @c dap_conn_t::state (included from @ref dap_conn.h).
 */
#pragma once

/* ================================================================== */
/*  Connection state flags                                             */
/* ================================================================== */

enum {
    /** @brief Worker sets on EOF from peer. The worker uses this to skip further reads
     *  on the connection. The processor does not use this flag directly. */
    DAP_CONN_RECV_DONE = 1 << 0,
    /** @brief Read-side gate: read_cb is paused because a downstream buffer is saturated.
     *
     *  Two distinct producers set this flag:
     *    (a) Recv-side — worker sets it when recv_olb acquire fails (no space for more
     *        bytes).  The processor reads it in dap_worker_conn_notify_send and always sends a
     *        kick; the worker clears it after the Dekker re-check succeeds (see
     *        dap_worker_rx_olb).
     *    (b) Send-side — dap_io_tx_send* sets it when free space in send_olb falls below
     *        the per-connection watermark (send_olb->compact_threshold).  Cleared by
     *        drain_pending after dap_worker_tx_flush has drained enough bytes to make
     *        room for at least one more worst-case response.
     *
     *  While set, the main event loop skips read_cb for this connection, producing the
     *  backpressure we want without touching EPOLLET or EPOLLIN registration: the
     *  peer's TCP stack stops receiving ACKs and naturally slows down.  Once the
     *  downstream buffer drains, the same flag is cleared and read_cb resumes. */
    DAP_CONN_SUSPENDED = 1 << 1,
    /** @brief Set on graceful connection close. The processor checks in defer_drain and drops
     *  deferred batches for this connection. Pending WFQ tasks still run to completion. */
    DAP_CONN_CLOSED    = 1 << 2,
    /** @brief Set by the send pipeline (dap_io_tx_send* family) after writing to send_olb.
     *  The worker clears it in dap_worker_tx_flush when send_olb is fully drained (flush returns 0).
     *  The processor reads it in defer_drain; if set, the deferred batch is skipped (backpressure). */
    DAP_CONN_SEND_BUSY = 1 << 3,
    /** @brief Worker sets (always together with CLOSED) on fatal error or forced disconnect.
     *  The processor checks in exec_batch (WFQ path) and defer_drain; all pending tasks are
     *  dropped immediately without processing. */
    DAP_CONN_PURGE     = 1 << 4,
    /** @brief Worker sets together with SUSPENDED when a non-blocking WFQ push fails.
     *  Any resume path (ctrl_conn_flush or s_resume_read mini-pass) that sees this bit
     *  clears both SUSPENDED and RESCAN, then re-invokes read_cb to retry the push. */
    DAP_CONN_RESCAN    = 1 << 5,
    /** @brief Synchronous processing mode.  When set, dap_worker_rx_olb does inline
     *  ack instead of push_batch — no processor involvement.
     *
     *  ASYNC→SYNC transition is two-phase:
     *    1. set SYNC — stops push_batch, blocks recv until processor finishes
     *    2. event loop checks head_pos == tail_pos (processor drained all batches)
     *    3. once drained: recv resumes, worker acks inline
     *
     *  SYNC→ASYNC: clear SYNC — immediate, no in-flight state.
     *
     *  Orthogonal to SUSPENDED (OLB full): both block recv independently. */
    DAP_CONN_SYNC      = 1 << 6
};
