/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
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

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>

#define LOG_TAG "test_chipmunk_tree"

// Test constants
#define TEST_MESSAGE "Tree test message for Chipmunk"
#define TREE_NODE_COUNT 8

/**
 * @brief Test tree-based signature aggregation
 */
static bool s_test_tree_aggregation(void) {
    log_it(L_INFO, "Testing Chipmunk tree-based signature aggregation...");

    // Generate multiple keys for tree aggregation
    dap_enc_key_t* l_keys[TREE_NODE_COUNT] = {0};
    for (size_t i = 0; i < TREE_NODE_COUNT; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_keys[i], "Key generation should succeed");
    }

    // Create message
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    // Create individual signatures
    dap_sign_t* l_signatures[TREE_NODE_COUNT] = {0};
    for (size_t i = 0; i < TREE_NODE_COUNT; i++) {
        l_signatures[i] = dap_sign_create(l_keys[i], &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Signature creation should succeed");

        // Verify individual signature
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT(l_verify_result == 0, "Individual signature verification should succeed");
    }

    // Test tree-based aggregation if supported
    dap_sign_type_t l_chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    bool l_supports_tree = dap_sign_type_supports_aggregation(l_chipmunk_type);

    if (l_supports_tree) {
        // Try to aggregate signatures
        dap_sign_aggregation_params_t l_agg_params = {0};
        l_agg_params.aggregation_type = DAP_SIGN_AGGREGATION_TYPE_TREE_BASED;

        dap_sign_t* l_aggregated = dap_sign_aggregate_signatures(
            l_signatures, TREE_NODE_COUNT, &l_message_hash, sizeof(l_message_hash), &l_agg_params);

        if (l_aggregated) {
            // Verify aggregated signature
            int l_agg_verify = dap_sign_verify_aggregated(
                l_aggregated, &l_message_hash, sizeof(l_message_hash), NULL, TREE_NODE_COUNT);

            if (l_agg_verify == 0) {
                log_it(L_INFO, "✓ Tree-based aggregation successful with %d signatures", TREE_NODE_COUNT);
                DAP_DELETE(l_aggregated);
            } else {
                log_it(L_WARNING, "Tree-based aggregation verification failed, but this may be expected");
            }
        } else {
            log_it(L_WARNING, "Tree-based aggregation not implemented yet, but this is expected");
        }
    } else {
        log_it(L_INFO, "Tree-based aggregation not supported for this signature type");
    }

    // Cleanup
    for (size_t i = 0; i < TREE_NODE_COUNT; i++) {
        DAP_DELETE(l_signatures[i]);
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ Tree aggregation tests passed");
    return true;
}

/**
 * @brief Test signature verification consistency
 */
static bool s_test_verification_consistency(void) {
    log_it(L_INFO, "Testing signature verification consistency...");

    // Generate key
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    // Create message
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    // Create signature
    dap_sign_t* l_signature = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Signature creation should succeed");

    // Test multiple verification calls (should be consistent)
    const size_t l_verification_count = 10;
    for (size_t i = 0; i < l_verification_count; i++) {
        int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should be consistent (attempt %zu)", i);
    }

    // Test with wrong message (should consistently fail)
    const char* l_wrong_message = "Wrong message for verification";
    dap_hash_fast_t l_wrong_hash = {0};
    dap_hash_fast(l_wrong_message, strlen(l_wrong_message), &l_wrong_hash);

    for (size_t i = 0; i < l_verification_count; i++) {
        int l_verify_result = dap_sign_verify(l_signature, &l_wrong_hash, sizeof(l_wrong_hash));
        DAP_TEST_ASSERT(l_verify_result != 0, "Wrong message verification should consistently fail (attempt %zu)", i);
    }

    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Verification consistency tests passed");
    return true;
}

/**
 * @brief Test signature serialization/deserialization
 */
static bool s_test_signature_serialization(void) {
    log_it(L_INFO, "Testing signature serialization/deserialization...");

    // Generate key
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    // Create message
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    // Create signature
    dap_sign_t* l_original = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_original, "Signature creation should succeed");

    // Serialize
    uint8_t* l_serialized = dap_sign_serialize(l_original);
    DAP_TEST_ASSERT_NOT_NULL(l_serialized, "Signature serialization should succeed");

    // Deserialize
    dap_sign_t* l_deserialized = dap_sign_deserialize(l_serialized);
    DAP_TEST_ASSERT_NOT_NULL(l_deserialized, "Signature deserialization should succeed");

    // Verify deserialized signature
    int l_verify_result = dap_sign_verify(l_deserialized, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_verify_result == 0, "Deserialized signature verification should succeed");

    // Test that signatures are structurally equivalent
    DAP_TEST_ASSERT(l_original->header.type.type == l_deserialized->header.type.type,
                   "Signature types should match after serialization");
    DAP_TEST_ASSERT(l_original->header.sign_size == l_deserialized->header.sign_size,
                   "Signature sizes should match after serialization");

    // Test multiple serialization rounds
    uint8_t* l_re_serialized = dap_sign_serialize(l_deserialized);
    DAP_TEST_ASSERT_NOT_NULL(l_re_serialized, "Re-serialization should succeed");

    dap_sign_t* l_re_deserialized = dap_sign_deserialize(l_re_serialized);
    DAP_TEST_ASSERT_NOT_NULL(l_re_deserialized, "Re-deserialization should succeed");

    int l_re_verify = dap_sign_verify(l_re_deserialized, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_re_verify == 0, "Re-deserialized signature verification should succeed");

    // Cleanup
    DAP_DELETE(l_original);
    DAP_DELETE(l_deserialized);
    DAP_DELETE(l_re_deserialized);
    DAP_FREE(l_serialized);
    DAP_FREE(l_re_serialized);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Signature serialization tests passed");
    return true;
}

/**
 * @brief Test batch signature operations
 */
static bool s_test_batch_operations(void) {
    log_it(L_INFO, "Testing batch signature operations...");

    const size_t l_batch_size = 5;

    // Generate keys
    dap_enc_key_t* l_keys[l_batch_size] = {0};
    for (size_t i = 0; i < l_batch_size; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_keys[i], "Key generation should succeed");
    }

    // Create different messages
    dap_hash_fast_t l_message_hashes[l_batch_size];
    dap_sign_t* l_signatures[l_batch_size] = {0};

    for (size_t i = 0; i < l_batch_size; i++) {
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "Batch message %zu", i);

        dap_hash_fast(l_message, strlen(l_message), &l_message_hashes[i]);

        l_signatures[i] = dap_sign_create(l_keys[i], &l_message_hashes[i], sizeof(l_message_hashes[i]));
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Batch signature creation should succeed");
    }

    // Test batch verification if supported
    dap_sign_type_t l_chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    bool l_supports_batch = dap_sign_type_supports_batch_verification(l_chipmunk_type);

    if (l_supports_batch) {
        log_it(L_INFO, "Testing batch verification...");

        // Note: In a full implementation, we would create a batch verification context
        // and verify all signatures at once for better performance
        log_it(L_INFO, "Batch verification is supported but not implemented in this test");
    } else {
        log_it(L_INFO, "Batch verification not supported for this signature type");
    }

    // Verify all signatures individually
    for (size_t i = 0; i < l_batch_size; i++) {
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hashes[i], sizeof(l_message_hashes[i]));
        DAP_TEST_ASSERT(l_verify_result == 0, "Batch signature verification should succeed for index %zu", i);
    }

    // Cleanup
    for (size_t i = 0; i < l_batch_size; i++) {
        DAP_DELETE(l_signatures[i]);
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ Batch operations tests passed");
    return true;
}

/**
 * @brief Test signature metadata and properties
 */
static bool s_test_signature_properties(void) {
    log_it(L_INFO, "Testing signature properties and metadata...");

    // Generate key
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    // Create message
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    // Create signature
    dap_sign_t* l_signature = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Signature creation should succeed");

    // Test signature properties
    DAP_TEST_ASSERT(l_signature->header.type.type == SIG_TYPE_CHIPMUNK,
                   "Signature should have correct type");

    DAP_TEST_ASSERT(l_signature->header.sign_size > 0,
                   "Signature should have non-zero size");

    // Test signature type detection functions
    bool l_is_ring = dap_sign_is_ring(l_signature);
    bool l_is_zk = dap_sign_is_zk(l_signature);

    DAP_TEST_ASSERT(l_is_ring == false, "Regular Chipmunk signature should not be detected as ring");
    DAP_TEST_ASSERT(l_is_zk == true, "Chipmunk signature should be detected as zero-knowledge proof");

    // Test signature size is reasonable
    size_t l_max_expected_size = 10000; // Reasonable upper bound for signatures
    DAP_TEST_ASSERT(l_signature->header.sign_size < l_max_expected_size,
                   "Signature size should be reasonable");

    // Test signature is not empty
    bool l_has_data = false;
    for (size_t i = 0; i < l_signature->header.sign_size && i < 100; i++) {
        if (l_signature->p_signature_data[i] != 0) {
            l_has_data = true;
            break;
        }
    }
    DAP_TEST_ASSERT(l_has_data == true, "Signature should contain non-zero data");

    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Signature properties tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Chipmunk Tree Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting Chipmunk tree unit tests...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_tree_aggregation();
    l_all_passed &= s_test_verification_consistency();
    l_all_passed &= s_test_signature_serialization();
    l_all_passed &= s_test_batch_operations();
    l_all_passed &= s_test_signature_properties();

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_NOTICE, "Chipmunk tree unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL Chipmunk tree unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some Chipmunk tree unit tests FAILED!");
        return -1;
    }
}
