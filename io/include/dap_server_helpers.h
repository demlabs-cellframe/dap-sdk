/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include "dap_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wait for server to be ready (listening)
 * 
 * Checks if server has listener sockets attached to workers.
 * 
 * @param a_server Server to check
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if server is ready, false on timeout
 */
bool dap_server_wait_for_ready(dap_server_t *a_server, uint32_t a_timeout_ms);

/**
 * @brief Check if server is ready (has listeners)
 * @param a_server Server to check
 * @return true if server has listeners attached to workers, false otherwise
 */
bool dap_server_is_ready(dap_server_t *a_server);

#ifdef __cplusplus
}
#endif

