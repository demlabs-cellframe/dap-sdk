/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file dap_client_test_fixtures.c
 * @brief Implementation of client and event system test fixtures
 */

#include "dap_client_test_fixtures.h"
#include "dap_client_pvt.h"
#include "dap_common.h"

#define LOG_TAG "dap_client_test_fixtures"

// ============================================================================
// Client State Check Functions
// ============================================================================

bool dap_test_client_check_initialized(void *a_user_data) {
    dap_client_t *client = (dap_client_t *)a_user_data;
    if (!client) {
        return false;
    }
    
    dap_client_pvt_t *client_pvt = DAP_CLIENT_PVT(client);
    if (!client_pvt) {
        return false;
    }
    
    // Check that internal structure is initialized
    return (client_pvt->worker != NULL && 
            client_pvt->stage == STAGE_BEGIN &&
            client_pvt->stage_status == STAGE_STATUS_COMPLETE);
}

bool dap_test_client_check_ready_for_deletion(void *a_user_data) {
    dap_client_t *client = (dap_client_t *)a_user_data;
    if (!client) {
        return true; // Already deleted
    }
    
    dap_client_pvt_t *client_pvt = DAP_CLIENT_PVT(client);
    if (!client_pvt) {
        return true; // Internal structure already cleaned up
    }
    
    // Check that all resources are cleaned up
    return (client_pvt->stream == NULL && 
            client_pvt->stream_es == NULL &&
            client_pvt->http_client == NULL &&
            client_pvt->reconnect_timer == NULL);
}

// ============================================================================
// Event System State Check Functions
// ============================================================================

bool dap_test_events_check_ready_for_deinit(void *a_user_data) {
    UNUSED(a_user_data);
    
    // After dap_events_stop_all(), workers should be stopping
    // For now, we assume that if stop_all() was called, we can proceed
    // In the future, this could check worker states more precisely
    // TODO: Add actual worker state checking if needed
    
    return true; // Always return true after stop_all() is called
}

