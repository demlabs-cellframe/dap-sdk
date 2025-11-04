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
#include "dap_test_async.h"
#include "dap_common.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "dap_stream.h"
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "dap_client_test_fixtures"

// Stream node address certificate name (from dap_enc_ks.h)
#include "dap_enc_ks.h"

// ============================================================================
// Certificate Test Setup Functions
// ============================================================================

int dap_test_generate_unique_node_addr(const char *a_cert_name, dap_enc_key_type_t a_key_type, 
                                      dap_stream_node_addr_t *a_node_addr_out) {
    if (!a_cert_name || !a_node_addr_out) {
        log_it(L_ERROR, "Invalid parameters for node address generation");
        return -1;
    }
    
    // Generate certificate in memory (no file needed)
    dap_cert_t *l_cert = dap_cert_generate_mem(a_cert_name, a_key_type);
    if (!l_cert) {
        log_it(L_ERROR, "Failed to generate certificate in memory: %s", a_cert_name);
        return -2;
    }
    
    // Extract node address from certificate
    *a_node_addr_out = dap_stream_node_addr_from_cert(l_cert);
    
    // Add certificate to registry (required for some operations)
    if (dap_cert_add(l_cert) != 0) {
        log_it(L_WARNING, "Failed to add certificate to registry: %s", a_cert_name);
        // Continue anyway - node address is already extracted
    }
    
    log_it(L_DEBUG, "Generated unique node address for certificate: %s", a_cert_name);
    return 0;
}

int dap_test_setup_certificates(const char *a_test_dir) {
    if (!a_test_dir) {
        log_it(L_ERROR, "Test directory is NULL");
        return -1;
    }
    
    // Create test certificate folder
    char l_cert_folder[512];
    snprintf(l_cert_folder, sizeof(l_cert_folder), "%s/test_ca", a_test_dir);
    
    // Create directory if it doesn't exist
    struct stat l_st = {0};
    if (stat(l_cert_folder, &l_st) == -1) {
        if (mkdir(l_cert_folder, 0755) != 0) {
            log_it(L_ERROR, "Failed to create certificate folder: %s", l_cert_folder);
            return -1;
        }
    }
    
    // Add certificate folder to DAP cert manager
    dap_cert_add_folder(l_cert_folder);
    
    // Check if node-addr certificate already exists
    dap_cert_t *l_addr_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    if (!l_addr_cert) {
        // Generate node-addr certificate
        char l_cert_path[520];  // Increased buffer size
        int l_ret = snprintf(l_cert_path, sizeof(l_cert_path), "%s/%s.dcert", l_cert_folder, DAP_STREAM_NODE_ADDR_CERT_NAME);
        if (l_ret < 0 || (size_t)l_ret >= sizeof(l_cert_path)) {
            log_it(L_ERROR, "Certificate path too long");
            return -1;
        }
        
        log_it(L_INFO, "Generating test certificate: %s", l_cert_path);
        l_addr_cert = dap_cert_generate(DAP_STREAM_NODE_ADDR_CERT_NAME, l_cert_path, DAP_STREAM_NODE_ADDR_CERT_TYPE);
        if (!l_addr_cert) {
            log_it(L_ERROR, "Failed to generate test certificate");
            return -1;
        }
    }
    
    log_it(L_INFO, "Test certificate environment setup complete");
    return 0;
}

int dap_test_cleanup_certificates(const char *a_test_dir) {
    if (!a_test_dir) {
        return -1;
    }
    
    // Note: Certificate cleanup is optional - certificates can remain for next test run
    // If needed, certificates can be deleted here
    UNUSED(a_test_dir);
    
    return 0;
}

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
            client_pvt->reconnect_timer == NULL);
}

// ============================================================================
// Event System State Check Functions
// ============================================================================

bool dap_test_events_check_ready_for_deinit(void *a_user_data) {
    UNUSED(a_user_data);
    
    // After dap_events_stop_all(), workers should be stopping
    // Check if events system is still initialized - if not, it's ready
    if (!dap_events_workers_init_status()) {
        return true; // Already deinitialized
    }
    
    // Give workers a bit more time to process stop signal
    // This is a simple delay - actual implementation would check thread states
    // For now, we rely on the timeout mechanism
    dap_test_sleep_ms(100);
    
    return false; // Not ready yet, will be checked again
}

// ============================================================================
// Convenience Functions for Waiting
// ============================================================================

bool dap_test_wait_client_initialized(dap_client_t *client, uint32_t timeout_ms) {
    dap_test_async_config_t l_cfg = {
        .timeout_ms = timeout_ms,
        .poll_interval_ms = 50,
        .fail_on_timeout = false,
        .operation_name = "client_initialization"
    };
    return dap_test_wait_condition(dap_test_client_check_initialized, client, &l_cfg);
}

bool dap_test_wait_client_ready_for_deletion(dap_client_t *client, uint32_t timeout_ms) {
    dap_test_async_config_t l_cfg = {
        .timeout_ms = timeout_ms,
        .poll_interval_ms = 50,
        .fail_on_timeout = false,
        .operation_name = "client_cleanup"
    };
    return dap_test_wait_condition(dap_test_client_check_ready_for_deletion, client, &l_cfg);
}

bool dap_test_wait_events_ready_for_deinit(uint32_t timeout_ms) {
    UNUSED(timeout_ms);
    
    // After dap_events_stop_all(), give workers time to process stop signal
    // dap_events_deinit() will wait for threads via dap_events_wait()
    // So we just need to give workers a moment to start stopping
    dap_test_sleep_ms(300);
    
    return true; // Always return true - dap_events_deinit() will handle waiting
}

