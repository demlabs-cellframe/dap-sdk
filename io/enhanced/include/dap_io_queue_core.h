/**
 * @file dap_io_queue_core.h
 * @brief Low-level WFQ queue bundle (vmqueue, msg, lane types, timer heap).
 *
 * Single include point for @ref dap_wfq.h and any header that only needs
 * these primitives, without duplicating the #include chain.
 */
#pragma once

#include "dap_vmqueue.h"
#include "dap_msg.h"
#include "dap_msg_types.h"
#include "dap_timer_heap.h"
