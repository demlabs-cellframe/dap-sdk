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
#include "dap_json.h"
#include "dap_hash.h"
#include "dap_enc_key.h"
#include "dap_sign.h"
#include "../fixtures/utilities/test_helpers.h"

#define LOG_TAG "test_functional_api"

/**
 * @brief Functional test: JSON API endpoints
 * @details Tests the JSON API functionality as it would be used by applications
 */
static bool s_test_json_api_functionality(void) {
    log_it(L_INFO, "Testing JSON API functionality");
    
    // Test 1: Create and manipulate JSON objects
    dap_json_t* l_root = dap_json_object_new();
    DAP_TEST_ASSERT_NOT_NULL(l_root, "JSON object creation");
    
    // Add various data types
    dap_json_object_add_string(l_root, "name", "test_node");
    dap_json_object_add_int64(l_root, "id", 12345);
    dap_json_object_add_bool(l_root, "active", true);
    
    // Test nested objects
    dap_json_t* l_config = dap_json_object_new();
    dap_json_object_add_string(l_config, "network", "testnet");
    dap_json_object_add_int64(l_config, "port", 8080);
    dap_json_object_add_object(l_root, "config", l_config);
    
    // Test arrays
    dap_json_t* l_array = dap_json_array_new();
    dap_json_array_add(l_array, dap_json_object_new_string("feature1"));
    dap_json_array_add(l_array, dap_json_object_new_string("feature2"));
    dap_json_array_add(l_array, dap_json_object_new_string("feature3"));
    dap_json_object_add_array(l_root, "features", l_array);
    
    // Serialize to string
    char* l_json_str = dap_json_to_string(l_root);
    DAP_TEST_ASSERT_NOT_NULL(l_json_str, "JSON serialization");
    
    log_it(L_DEBUG, "Generated JSON: %s", l_json_str);
    
    // Test parsing back
    dap_json_t* l_parsed = dap_json_parse_string(l_json_str);
    DAP_TEST_ASSERT_NOT_NULL(l_parsed, "JSON parsing");
    
    // Verify parsed data
    const char* l_name = dap_json_object_get_string(l_parsed, "name");
    DAP_TEST_ASSERT_STRING_EQUAL("test_node", l_name, "Parsed name");
    
    int64_t l_id = dap_json_object_get_int64(l_parsed, "id");
    DAP_TEST_ASSERT_EQUAL(12345, l_id, "Parsed ID");
    
    bool l_active = dap_json_object_get_bool(l_parsed, "active");
    DAP_TEST_ASSERT(l_active == true, "Parsed boolean");
    
    // Cleanup
    DAP_DELETE(l_json_str);
    dap_json_object_free(l_root);
    dap_json_object_free(l_parsed);
    
    log_it(L_INFO, "JSON API functionality test passed");
    return true;
}

/**
 * @brief Functional test: Crypto API functionality
 * @details Tests crypto API as it would be used in real applications
 */
static bool s_test_crypto_api_functionality(void) {
    log_it(L_INFO, "Testing Crypto API functionality");
    
    // Test 1: Key management workflow
    log_it(L_DEBUG, "Testing key management workflow");
    
    // Generate different types of keys
    dap_enc_key_t* l_sign_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_sign_key, "Signature key generation");
    
    // Test key properties
    dap_enc_key_type_t l_key_type = l_sign_key->type;
    DAP_TEST_ASSERT(l_key_type == DAP_ENC_KEY_TYPE_SIG_DILITHIUM, "Key type verification");
    
    // Test 2: Document signing workflow
    log_it(L_DEBUG, "Testing document signing workflow");
    
    const char* l_document = "Important document that needs to be signed";
    size_t l_doc_size = strlen(l_document);
    
    // Hash the document
    dap_hash_fast_t l_doc_hash = {0};
    bool l_hash_ret = dap_hash_fast(l_document, l_doc_size, &l_doc_hash);
    DAP_TEST_ASSERT(l_hash_ret == true, "Document hashing");
    
    // Sign the hash
    size_t l_signature_size = 0;
    dap_sign_t* l_signature = dap_sign_create(l_sign_key, &l_doc_hash, sizeof(l_doc_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Document signing");
    DAP_TEST_ASSERT(dap_sign_get_size(l_signature) > 0, "Signature size check");
    
    // Verify signature
    int l_verify_result = dap_sign_verify(l_signature, &l_doc_hash, sizeof(l_doc_hash));
    DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification");
    
    // Test 3: Multiple document workflow
    log_it(L_DEBUG, "Testing multiple document workflow");
    
    const char* l_documents[] = {
        "Document 1 for batch processing",
        "Document 2 with different content",
        "Document 3 final document"
    };
    size_t l_num_docs = sizeof(l_documents) / sizeof(l_documents[0]);
    
    for (size_t i = 0; i < l_num_docs; i++) {
        dap_hash_fast_t l_hash = {0};
        dap_hash_fast(l_documents[i], strlen(l_documents[i]), &l_hash);
        
        size_t l_sig_size = 0;
        dap_sign_t* l_sig = dap_sign_create(l_sign_key, &l_hash, sizeof(l_hash));
        DAP_TEST_ASSERT_NOT_NULL(l_sig, "Batch document signing");
        
        int l_verify = dap_sign_verify(l_sig, &l_hash, sizeof(l_hash));
        DAP_TEST_ASSERT(l_verify == 0, "Batch document verification");
        
        DAP_DELETE(l_sig);
    }
    
    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_sign_key);
    
    log_it(L_INFO, "Crypto API functionality test passed");
    return true;
}

/**
 * @brief Functional test: Combined API workflow
 * @details Tests how JSON and Crypto APIs work together
 */
static bool s_test_combined_api_workflow(void) {
    log_it(L_INFO, "Testing combined API workflow");
    
    // Create a signed JSON document workflow
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation for combined workflow");
    
    // Step 1: Create structured data in JSON
    dap_json_t* l_transaction = dap_json_object_new();
    dap_json_object_add_string(l_transaction, "type", "transfer");
    dap_json_object_add_string(l_transaction, "from", "Alice");
    dap_json_object_add_string(l_transaction, "to", "Bob");
    dap_json_object_add_int64(l_transaction, "amount", 500000);
    dap_json_object_add_int64(l_transaction, "timestamp", dap_time_now());
    dap_json_object_add_string(l_transaction, "currency", "DAP");
    
    // Step 2: Serialize JSON for signing
    char* l_json_data = dap_json_to_string(l_transaction);
    DAP_TEST_ASSERT_NOT_NULL(l_json_data, "Transaction JSON serialization");
    
    // Step 3: Create hash of JSON data
    dap_hash_fast_t l_tx_hash = {0};
    bool l_hash_ret = dap_hash_fast(l_json_data, strlen(l_json_data), &l_tx_hash);
    DAP_TEST_ASSERT(l_hash_ret == true, "Transaction hash creation");
    
    // Step 4: Sign the transaction
    size_t l_signature_size = 0;
    dap_sign_t* l_signature = dap_sign_create(l_key, &l_tx_hash, sizeof(l_tx_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Transaction signing");
    
    // Step 5: Create signed transaction envelope
    dap_json_t* l_signed_tx = dap_json_object_new();
    dap_json_object_add_object(l_signed_tx, "transaction", l_transaction);
    
    // Add signature as hex string
    char* l_signature_hex = DAP_NEW_SIZE(char, l_signature_size * 2 + 1);
    dap_bin2hex(l_signature_hex, l_signature, l_signature_size);
    dap_json_object_add_string(l_signed_tx, "signature", l_signature_hex);
    
    // Add hash as hex string
    char l_hash_hex[sizeof(l_tx_hash) * 2 + 1];
    dap_bin2hex(l_hash_hex, l_tx_hash.raw, sizeof(l_tx_hash));
    dap_json_object_add_string(l_signed_tx, "hash", l_hash_hex);
    
    // Step 6: Serialize final signed transaction
    char* l_final_json = dap_json_to_string(l_signed_tx);
    DAP_TEST_ASSERT_NOT_NULL(l_final_json, "Signed transaction serialization");
    
    log_it(L_DEBUG, "Final signed transaction: %s", l_final_json);
    
    // Step 7: Verification workflow - parse and verify
    dap_json_t* l_parsed_tx = dap_json_parse_string(l_final_json);
    DAP_TEST_ASSERT_NOT_NULL(l_parsed_tx, "Signed transaction parsing");
    
    // Extract original transaction
    dap_json_t* l_orig_tx = dap_json_object_get_object(l_parsed_tx, "transaction");
    DAP_TEST_ASSERT_NOT_NULL(l_orig_tx, "Original transaction extraction");
    
    // Serialize original transaction for verification
    char* l_orig_json = dap_json_to_string(l_orig_tx);
    DAP_TEST_ASSERT_NOT_NULL(l_orig_json, "Original transaction serialization");
    
    // Verify hash matches
    dap_hash_fast_t l_verify_hash = {0};
    dap_hash_fast(l_orig_json, strlen(l_orig_json), &l_verify_hash);
    
    int l_hash_compare = memcmp(&l_tx_hash, &l_verify_hash, sizeof(dap_hash_fast_t));
    DAP_TEST_ASSERT(l_hash_compare == 0, "Hash verification");
    
    // Verify signature
    int l_sig_verify = dap_sign_verify(l_signature, &l_verify_hash, sizeof(l_verify_hash));
    DAP_TEST_ASSERT(l_sig_verify == 0, "Signature verification in combined workflow");
    
    // Cleanup
    DAP_DELETE(l_final_json);
    DAP_DELETE(l_orig_json);
    DAP_DELETE(l_json_data);
    DAP_DELETE(l_signature_hex);
    DAP_DELETE(l_signature);
    dap_json_object_free(l_signed_tx);
    dap_json_object_free(l_parsed_tx);
    dap_enc_key_delete(l_key);
    
    log_it(L_INFO, "Combined API workflow test passed");
    return true;
}

/**
 * @brief Main test function for functional API tests
 */
int main(void) {
    log_it(L_INFO, "Starting DAP SDK Functional API Tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_json_api_functionality();
    l_all_passed &= s_test_crypto_api_functionality();
    l_all_passed &= s_test_combined_api_workflow();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Functional API tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Functional API tests failed!");
        return -1;
    }
}
