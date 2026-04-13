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

#include "dap_client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Check if client is connected (in STREAM_STREAMING stage with COMPLETE status)
 * @param a_client Client to check
 * @return true if connected, false otherwise
 */
bool dap_client_is_connected(dap_client_t *a_client);

/**
 * @brief Check if client is in specific stage
 * @param a_client Client to check
 * @param a_stage Stage to check for
 * @return true if client is in the specified stage, false otherwise
 */
bool dap_client_is_in_stage(dap_client_t *a_client, dap_client_stage_t a_stage);

/**
 * @brief Check if client has error
 * @param a_client Client to check
 * @return true if client has error status, false otherwise
 */
bool dap_client_has_error(dap_client_t *a_client);

/**
 * @brief Wait for client to reach specific stage
 * @param a_client Client to wait for
 * @param a_target_stage Target stage to wait for
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if stage reached, false on timeout or error
 */
bool dap_client_wait_for_stage(dap_client_t *a_client, dap_client_stage_t a_target_stage, uint32_t a_timeout_ms);

/**
 * @brief Wait for client to be deleted
 * @param a_client_ptr Pointer to client pointer (will be set to NULL when deleted)
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if client was deleted, false on timeout
 */
bool dap_client_wait_for_deletion(dap_client_t **a_client_ptr, uint32_t a_timeout_ms);

/**
 * @brief Wait for stream channels to be ready
 * @param a_client Client to check
 * @param a_expected_channels Expected channel IDs (e.g., "ABC")
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if channels are ready, false on timeout
 */
bool dap_client_wait_for_channels(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms);

#ifdef __cplusplus
}
#endif

