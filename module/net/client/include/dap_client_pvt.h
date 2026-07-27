/*
 * dap_client_pvt.h - Backward compatibility wrapper
 *
 * This header is maintained for backward compatibility.
 * The actual implementation has been split into:
 *   - dap_client_fsm.h    (FSM state machine, runs on dedicated FSM threads)
 *   - dap_client_esocket.h (IO/transport layer, runs on worker threads)
 *
 * Use dap_client_esocket_t and DAP_CLIENT_ESOCKET() from dap_client_esocket.h.
 */
#pragma once

#include "dap_client_fsm.h"
#include "dap_client_esocket.h"
