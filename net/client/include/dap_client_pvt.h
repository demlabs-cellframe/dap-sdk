/*
 * dap_client_pvt.h - Backward compatibility wrapper
 *
 * This header is maintained for backward compatibility.
 * The actual implementation has been split into:
 *   - dap_client_fsm.h    (FSM state machine, runs on dedicated FSM threads)
 *   - dap_client_esocket.h (IO/transport layer, runs on worker threads)
 *
 * dap_client_pvt_t is now an alias for dap_client_esocket_t.
 * DAP_CLIENT_PVT() macro is redefined to route through the FSM.
 */
#pragma once

#include "dap_client_fsm.h"
#include "dap_client_esocket.h"
