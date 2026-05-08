/**
 * @file dap_bus.h
 * @brief Worker→processor bus — single include for WFQ lanes, vmqueue, lane payloads, wake.
 *
 * Umbrella over @ref dap_wfq.h (which pulls @ref dap_io_queue_core.h: vmqueue, msg,
 * @ref dap_msg_types.h, @ref dap_timer_heap.h).  Worker and processor headers should
 * depend on this instead of including @ref dap_wfq.h / queue primitives separately.
 */
#pragma once

#include "dap_wfq.h"

/** @brief Wake the processor MPSC via the bus-level API. */
DAP_STATIC_INLINE void dap_bus_proc_wake(dap_vmqueue_mpsc_t *a_wfq)
{ dap_wfq_wake(a_wfq); }
