/*
 * dap_client_pvt.h - Backward compatibility wrapper
 *
 * This header is maintained for backward compatibility.
 * The actual implementation has been split into:
 *   - dap_client_fsm.h    (FSM state machine, runs on dedicated FSM threads)
 *   - dap_client_esocket.h (IO/transport layer, runs on worker threads)
 *
 * Use dap_client_esocket_t and DAP_CLIENT_ESOCKET() from dap_client_esocket.h.
 *
 * For backward compatibility:
 *   - dap_client_pvt_t is typedef'd to dap_client_esocket_t
 *   - DAP_CLIENT_PVT() maps to DAP_CLIENT_ESOCKET()
 *   - All dap_client_pvt_* functions are #define'd to dap_client_esocket_* equivalents
 */
#pragma once

#include "dap_client_fsm.h"
#include "dap_client_esocket.h"

/**
 * @brief Backward compatibility typedef
 * dap_client_pvt_t is now an alias for dap_client_esocket_t
 */
typedef dap_client_esocket_t dap_client_pvt_t;

/**
 * @brief Backward compatibility macro
 * DAP_CLIENT_PVT() now maps to DAP_CLIENT_ESOCKET()
 */
#define DAP_CLIENT_PVT(a) DAP_CLIENT_ESOCKET(a)
