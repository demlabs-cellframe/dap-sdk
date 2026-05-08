/**
 * @file dap_proc_frame_impl.h
 * @brief Umbrella for processor-side advanced I/O primitives.
 *
 * Split for compile-time isolation:
 *   - @ref dap_proc_defer.h — defer queue + typed lane callbacks
 *   - @ref dap_proc_exec.h  — generic @c frame_cb batch + recv ack
 *   - @ref dap_proc_dispatch.h — WFQ drain (include only from processor .c)
 *
 * User code SHOULD NOT include this header for normal @c frame_cb apps;
 * use @ref dap_io.h / @ref dap_proc_frame.h.  Include this header (or the
 * split pieces above) for benches and custom @c batch_cb / @c custom_cb paths.
 */
#pragma once

#include "dap_proc_defer.h"
#include "dap_proc_exec.h"
