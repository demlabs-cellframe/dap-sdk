/**
 * @file dap_proc_frame.h
 * @brief Normal public processor batch callbacks — void (@c dap_proc_batch_cb_t) or
 *  return-code (@c dap_io_frame_rc_cb_t).
 *
 * **Void callback** (@ref dap_io_proc_set_frame_cb): commit-after-return semantics.
 * Each invocation sees a zero-copy slice of recv_olb valid only for that call; recv
 * ack and @c dap_worker_after_batch_processed run after the callback. Use when the
 * protocol always commits the batch after processing (no recv-side backpressure from
 * your handler). Prefer @c dap_io_tx_send* (@ref dap_io_send.h) for outbound data;
 * @c dap_send_olb_write is a lower-level primitive (advanced header).
 *
 * **Return-code callback** (@ref dap_io_proc_set_frame_rc_cb): same zero-copy batch
 * pointer, but the callback returns @c dap_msg_rc_t — @c DAP_MSG_DONE / @c DAP_MSG_DROP
 * commit recv bytes; @c DAP_MSG_DEFER keeps the batch unacked until send pressure clears
 * or shutdown forces completion. Use when @c dap_io_tx_send_direct (or related)
 * may return @c DAP_SEND_OVERFLOW and you must defer the recv batch without losing data.
 * This path does not use @c dap_batch_task_t in application code.
 *
 * For fully custom WFQ dispatch (fan-out, alternative defer rules, or non-batch
 * types), use the advanced @c batch_cb path (@ref dap_io_proc_set_batch_cb,
 * @ref dap_msg_batch_cb_t).
 *
 * Typical usage:
 * @code
 *     #include "dap_io.h"
 *
 *     static void my_frame_cb(dap_conn_t *c, const char *batch,
 *                              uint32_t bytes, void *arg)
 *     {
 *         // parse `bytes` bytes of `batch` — it points straight into
 *         // recv_olb and is valid only for the duration of this call.
 *         // produce a response via dap_io_tx_send_direct(c, resp, resp_len);
 *     }
 *
 *     dap_io_proc_set_frame_cb(io, 0, my_frame_cb, &my_state);
 * @endcode
 *
 * Return-code path: use @ref dap_io_send_rc_to_msg_rc with @c dap_io_tx_send_direct (see @ref dap_io_send.h).
 * @code
 *     dap_msg_rc_t m = dap_io_send_rc_to_msg_rc(dap_io_tx_send_direct(c, p, n));
 * @endcode
 *
 * Advanced users that need to override the built-in dispatch wholesale
 * (custom defer policy, fan-out, etc.) should include
 * @ref dap_io_advanced.h (recommended single entry point) or, selectively,
 * @ref dap_proc_frame_impl.h / @ref dap_proc_defer.h for typed low-level callbacks
 * (@c dap_msg_batch_cb_t / @c callback_cb / @c heap_cb / @c custom_cb).
 */
#pragma once

#include "dap_conn.h"
#include "dap_msg.h"

/**
 * @brief User batch callback — observational, runs on the processor thread.
 *
 * Called once per contiguous recv_olb batch after worker parsing, before
 * the recv_olb ack and @c dap_worker_after_batch_processed.  @a a_batch
 * points directly into recv_olb and is valid only for the duration of
 * this call — copy out explicitly if you need to retain it.
 *
 * The callback returns @c void: recv-side progression (ack, post-batch
 * hook) is owned by the generic path.  Use @c dap_io_tx_send* for normal
 * outbound data; use advanced @c batch_cb / raw send-OLB primitives only
 * when you need explicit return-code/defer policy.
 *
 * @param[in] a_c      Connection pointer (owned by its worker; live for this call).
 * @param[in] a_batch  Zero-copy pointer to batch bytes in recv_olb.
 * @param[in] a_bytes  Batch length in bytes.
 * @param[in] a_arg    Opaque user argument set via dap_proc_ctx_t::_inheritor.
 */
typedef void (*dap_proc_batch_cb_t)(dap_conn_t *a_c, const char *a_batch,
                                     uint32_t a_bytes, void *a_arg);

/**
 * @brief Return-code batch handler for @ref dap_io_proc_set_frame_rc_cb.
 *
 * @return @c DAP_MSG_DONE or @c DAP_MSG_DROP to commit; @c DAP_MSG_DEFER to retain
 *         the recv batch until retry or forced completion.
 */
typedef dap_msg_rc_t (*dap_io_frame_rc_cb_t)(dap_conn_t *a_c, const char *a_batch,
                                              uint32_t a_bytes, void *a_arg);
