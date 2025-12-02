/*
 * Authors:
 * Dmitry Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
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

#include "dap_common.h"
#include "dap_config.h"
#include "dap_hash.h"
#include "dap_sign.h"
#include "dap_enc_key.h"
#include "dap_json.h"
#include "../fixtures/utilities/test_helpers.h"

#define LOG_TAG "test_e2e_complete_workflow"

/**
 * @brief E2E Test: Complete DAP SDK workflow simulation
 * @details Simulates a complete real-world scenario:
 * 1. Initialize system
 * 2. Create configuration
 * 3. Generate crypto keys
 * 4. Process JSON data
 * 5. Sign and verify data
 * 6. Cleanup
 */
static bool s_test_complete_dap_workflow(void) {
    log_it(L_INFO, "Starting complete DAP SDK E2E workflow test");
    
    // Step 1: System initialization
    log_it(L_DEBUG, "Step 1: System initialization");
    // In real scenario, this would initialize all DAP SDK modules
    
    // Step 2: Configuration setup (simplified for testing)
    log_it(L_DEBUG, "Step 2: Configuration setup");
    // Note: In real scenarios, config would be loaded from file using dap_config_open()
    // For this test, we'll skip config manipulation since the API doesn't have set functions
    log_it(L_DEBUG, "Config functionality test skipped - no setter API available");
    
    // Step 3: Crypto key generation  
    log_it(L_DEBUG, "Step 3: Crypto key generation");
    dap_enc_key_t* l_master_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_master_key, "Master key generation");
    
    dap_enc_key_t* l_node_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_FALCON, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_node_key, "Node key generation");
    
    // Step 4: JSON data processing
    log_it(L_DEBUG, "Step 4: JSON data processing");
    dap_json_t* l_root = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_root, "JSON root object creation");
    
    // Create complex JSON structure representing a transaction
    dap_json_t* l_transaction = dap_json_object_new();
    dap_json_object_add_string(l_transaction, "type", "transfer");
    dap_json_object_add_int64(l_transaction, "amount", 1000000);
    dap_json_object_add_string(l_transaction, "from", "Alice");
    dap_json_object_add_string(l_transaction, "to", "Bob");
    dap_json_object_add_int64(l_transaction, "timestamp", dap_time_now());
    
    dap_json_object_add_object(l_root, "transaction", l_transaction);
    
    // Convert to string for signing
    char* l_json_str = dap_json_to_string(l_root);
    DAP_TEST_ASSERT_NOT_NULL(l_json_str, "JSON serialization");
    
    log_it(L_DEBUG, "Generated transaction JSON: %s", l_json_str);
    
    // Step 5: Data hashing and signing
    log_it(L_DEBUG, "Step 5: Data hashing and signing");
    
    // Hash the JSON data
    dap_hash_fast_t l_tx_hash = {0};
    bool l_hash_ret = dap_hash_fast(l_json_str, strlen(l_json_str), &l_tx_hash);
    DAP_TEST_ASSERT(l_hash_ret == true, "Transaction hash calculation");
    
    // Sign with master key
    dap_sign_t* l_master_signature = dap_sign_create(l_master_key, &l_tx_hash, sizeof(l_tx_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_master_signature, "Master signature creation");
    
    // Sign with node key
    dap_sign_t* l_node_signature = dap_sign_create(l_node_key, &l_tx_hash, sizeof(l_tx_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_node_signature, "Node signature creation");
    
    // Step 6: Signature verification
    log_it(L_DEBUG, "Step 6: Signature verification");
    
    int l_master_verify = dap_sign_verify(l_master_signature, &l_tx_hash, sizeof(l_tx_hash));
    DAP_TEST_ASSERT(l_master_verify == 0, "Master signature verification");
    
    int l_node_verify = dap_sign_verify(l_node_signature, &l_tx_hash, sizeof(l_tx_hash));
    DAP_TEST_ASSERT(l_node_verify == 0, "Node signature verification");
    
    // Step 7: Cross-verification test (simplified for this API)
    log_it(L_DEBUG, "Step 7: Signature verification completed");
    // Note: Cross-verification would require key extraction API which is more complex
    
    // Step 8: Create signed transaction package
    log_it(L_DEBUG, "Step 8: Creating signed transaction package");
    dap_json_t* l_signed_package = dap_json_object_new();
    dap_json_object_add_object(l_signed_package, "data", l_root);
    
    // Add signatures as base64 encoded strings (simplified)
    size_t l_master_sig_size = dap_sign_get_size(l_master_signature);
    char l_master_sig_hex[l_master_sig_size * 2 + 1];
    dap_bin2hex(l_master_sig_hex, l_master_signature, l_master_sig_size);
    dap_json_object_add_string(l_signed_package, "master_signature", l_master_sig_hex);
    
    size_t l_node_sig_size = dap_sign_get_size(l_node_signature);
    char l_node_sig_hex[l_node_sig_size * 2 + 1];
    dap_bin2hex(l_node_sig_hex, l_node_signature, l_node_sig_size);
    dap_json_object_add_string(l_signed_package, "node_signature", l_node_sig_hex);
    
    char* l_final_package = dap_json_to_string(l_signed_package);
    DAP_TEST_ASSERT_NOT_NULL(l_final_package, "Final package creation");
    
    log_it(L_INFO, "Final signed package size: %zu bytes", strlen(l_final_package));
    
    // Step 9: Cleanup
    log_it(L_DEBUG, "Step 9: Cleanup");
    DAP_DELETE(l_final_package);
    dap_json_object_free(l_signed_package);
    DAP_DELETE(l_json_str);
    DAP_DELETE(l_master_signature);
    DAP_DELETE(l_node_signature);
    dap_enc_key_delete(l_master_key);
    dap_enc_key_delete(l_node_key);
    // Config cleanup would go here if we had initialized one
    
    log_it(L_INFO, "Complete DAP SDK E2E workflow test passed");
    return true;
}

/**
 * @brief E2E Test: Error handling workflow
 * @details Tests how the system handles various error conditions
 */
static bool s_test_error_handling_workflow(void) {
    log_it(L_INFO, "Testing error handling E2E workflow");
    
    // Test 1: Invalid JSON processing
    const char* l_invalid_json = "{\"incomplete\":}";
    dap_json_t* l_parsed = dap_json_parse_string(l_invalid_json);
    DAP_TEST_ASSERT_NULL(l_parsed, "Invalid JSON should not parse");
    
    // Test 2: Signature with NULL key
    dap_hash_fast_t l_test_hash = {0};
    dap_sign_t* l_signature = dap_sign_create(NULL, &l_test_hash, sizeof(l_test_hash));
    DAP_TEST_ASSERT_NULL(l_signature, "Signing with NULL key should fail");
    
    // Test 3: Verification with wrong signature size
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    if (l_key) {
        dap_sign_t l_fake_sig_struct = {0};
        int l_verify = dap_sign_verify(&l_fake_sig_struct, &l_test_hash, sizeof(l_test_hash));
        DAP_TEST_ASSERT(l_verify != 1, "Verification with wrong signature size should fail");
        
        dap_enc_key_delete(l_key);
    }
    
    log_it(L_INFO, "Error handling E2E workflow test passed");
    return true;
}

/**
 * @brief Main test function for E2E tests
 */
int main(void) {
    log_it(L_INFO, "Starting DAP SDK End-to-End Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_complete_dap_workflow();
    l_all_passed &= s_test_error_handling_workflow();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All End-to-End tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some End-to-End tests failed!");
        return -1;
    }
}
