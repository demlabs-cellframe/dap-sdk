/**
 * @file dap_io_advanced.h
 * @brief Advanced/internal umbrella (opt-in) — not a baseline protocol documentation entry.
 *
 * Single include for non-trivial use on top of @ref dap_io.h.
 *
 * Typical services use @ref dap_io.h + @ref dap_io_ops.h (topology, workers,
 * processors, @c frame_cb, connection open / timer cancel) and @ref dap_io_send.h
 * for @c dap_io_tx_send*.
 *
 * This header additionally pulls in:
 *   — @ref dap_io_ops.h (conn open / timer cancel);
 *   — custom WFQ / @c dap_proc_post and helpers from @ref dap_proc_msg.h;
 *   — @c batch_cb / defer / @c dap_proc_exec_batch via @ref dap_proc_frame_impl.h;
 *   — @ref dap_proc_frame.h transitively where needed.
 *
 * Do not include vmqueue/wfq headers piecemeal in protocol code — use
 * @ref dap_bus.h (or @ref dap_io.h), @ref dap_io_ops.h, or this umbrella for advanced paths.
 */
#pragma once

#include "dap_io.h"
#include "dap_io_ops.h"
#include "dap_proc_frame_impl.h"
#include "dap_proc_msg.h"
