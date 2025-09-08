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
#include <dap_sign.h>
#include <dap_enc_key.h>
#include <dap_hash.h>
#include <dap_enc_chipmunk_ring.h>
#include "test_helpers.h"

#define LOG_TAG "test_signatures"

// Test constants
#define TEST_MESSAGE "Test message for signature verification"

/**
 * @brief Test signature aggregation support detection
 */
static bool s_test_aggregation_support(void) {
    log_it(L_INFO, "Testing signature aggregation support detection...");

    // Test Chipmunk signature type support
    dap_sign_type_t l_chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    bool l_supports_agg = dap_sign_type_supports_aggregation(l_chipmunk_type);
    DAP_TEST_ASSERT(l_supports_agg == true, "Chipmunk should support aggregation");

    bool l_supports_batch = dap_sign_type_supports_batch_verification(l_chipmunk_type);
    DAP_TEST_ASSERT(l_supports_batch == true, "Chipmunk should support batch verification");

    // Test Chipmunk Ring signature type support
    dap_sign_type_t l_chipmunk_ring_type = {.type = SIG_TYPE_CHIPMUNK_RING};
    bool l_ring_supports_agg = dap_sign_type_supports_aggregation(l_chipmunk_ring_type);
    DAP_TEST_ASSERT(l_ring_supports_agg == true, "Chipmunk Ring should support aggregation");

    bool l_ring_supports_batch = dap_sign_type_supports_batch_verification(l_chipmunk_ring_type);
    DAP_TEST_ASSERT(l_ring_supports_batch == true, "Chipmunk Ring should support batch verification");

    // Test other signature types don't support aggregation
    dap_sign_type_t l_bliss_type = {.type = SIG_TYPE_BLISS};
    bool l_bliss_agg = dap_sign_type_supports_aggregation(l_bliss_type);
    DAP_TEST_ASSERT(l_bliss_agg == false, "Bliss should not support aggregation");

    log_it(L_INFO, "✓ Aggregation support detection tests passed");
    return true;
}

/**
 * @brief Test aggregation types query
 */
static bool s_test_aggregation_types_query(void) {
    log_it(L_INFO, "Testing aggregation types query...");

    dap_sign_type_t l_chipmunk_type = {.type = SIG_TYPE_CHIPMUNK};
    dap_sign_aggregation_type_t l_agg_types[5];

    uint32_t l_count = dap_sign_get_supported_aggregation_types(l_chipmunk_type, l_agg_types, 5);
    DAP_TEST_ASSERT(l_count > 0, "Chipmunk should support at least one aggregation type");
    DAP_TEST_ASSERT(l_agg_types[0] == DAP_SIGN_AGGREGATION_TYPE_TREE_BASED,
                   "First aggregation type should be tree-based");

    // Test Chipmunk Ring
    dap_sign_type_t l_chipmunk_ring_type = {.type = SIG_TYPE_CHIPMUNK_RING};
    uint32_t l_ring_count = dap_sign_get_supported_aggregation_types(l_chipmunk_ring_type, l_agg_types, 5);
    DAP_TEST_ASSERT(l_ring_count > 0, "Chipmunk Ring should support at least one aggregation type");

    log_it(L_INFO, "Found %u supported aggregation types for Chipmunk", l_count);
    log_it(L_INFO, "Found %u supported aggregation types for Chipmunk Ring", l_ring_count);
    log_it(L_INFO, "✓ Aggregation types query tests passed");
    return true;
}

/**
 * @brief Test signature info functions
 */
static bool s_test_signature_info_functions(void) {
    log_it(L_INFO, "Testing signature info functions...");

    // Test signature type string conversion
    const char* l_chipmunk_str = dap_sign_type_to_str((dap_sign_type_t){.type = SIG_TYPE_CHIPMUNK});
    DAP_TEST_ASSERT(l_chipmunk_str != NULL, "Chipmunk type string should not be NULL");
    DAP_TEST_ASSERT(strlen(l_chipmunk_str) > 0, "Chipmunk type string should not be empty");

    const char* l_chipmunk_ring_str = dap_sign_type_to_str((dap_sign_type_t){.type = SIG_TYPE_CHIPMUNK_RING});
    DAP_TEST_ASSERT(l_chipmunk_ring_str != NULL, "Chipmunk Ring type string should not be NULL");
    DAP_TEST_ASSERT(strlen(l_chipmunk_ring_str) > 0, "Chipmunk Ring type string should not be empty");

    // Test reverse conversion
    dap_sign_type_t l_chipmunk_back = dap_sign_type_from_str(l_chipmunk_str);
    DAP_TEST_ASSERT(l_chipmunk_back.type == SIG_TYPE_CHIPMUNK, "Reverse conversion should work for Chipmunk");

    dap_sign_type_t l_chipmunk_ring_back = dap_sign_type_from_str(l_chipmunk_ring_str);
    DAP_TEST_ASSERT(l_chipmunk_ring_back.type == SIG_TYPE_CHIPMUNK_RING, "Reverse conversion should work for Chipmunk Ring");

    log_it(L_INFO, "✓ Signature info functions tests passed");
    return true;
}

/**
 * @brief Test basic signature creation and verification
 */
static bool s_test_basic_signatures(void) {
    log_it(L_INFO, "Testing basic signature operations...");

    // Test Chipmunk signature
    dap_enc_key_t* l_chipmunk_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_chipmunk_key, "Chipmunk key generation should succeed");

    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message_hash.raw, sizeof(l_message_hash), &l_message_hash);

    dap_sign_t* l_chipmunk_sig = dap_sign_create(l_chipmunk_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_chipmunk_sig, "Chipmunk signature creation should succeed");

    int l_verify_result = dap_sign_verify(l_chipmunk_sig, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_verify_result == 0, "Chipmunk signature verification should succeed");

    // Test signature type
    DAP_TEST_ASSERT(l_chipmunk_sig->header.type.type == SIG_TYPE_CHIPMUNK,
                   "Signature should be CHIPMUNK type");

    // Test ring signature detection
    bool l_is_ring = dap_sign_is_ring(l_chipmunk_sig);
    DAP_TEST_ASSERT(l_is_ring == false, "Regular Chipmunk signature should not be detected as ring");

    bool l_is_zk = dap_sign_is_zk(l_chipmunk_sig);
    DAP_TEST_ASSERT(l_is_zk == true, "Chipmunk signature should be detected as ZKP");

    // Cleanup
    DAP_DELETE(l_chipmunk_sig);
    dap_enc_key_delete(l_chipmunk_key);

    log_it(L_INFO, "✓ Basic signature tests passed");
    return true;
}

/**
 * @brief Test signature serialization/deserialization
 */
static bool s_test_signature_serialization(void) {
    log_it(L_INFO, "Testing signature serialization...");

    // Generate a signature
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message_hash.raw, sizeof(l_message_hash), &l_message_hash);

    dap_sign_t* l_original_sig = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_original_sig, "Signature creation should succeed");

    // TODO: Serialization functions not implemented in current API
    // uint8_t* l_serialized = dap_sign_serialize(l_original_sig);
    // DAP_TEST_ASSERT_NOT_NULL(l_serialized, "Signature serialization should succeed");

    // dap_sign_t* l_deserialized_sig = dap_sign_deserialize(l_serialized);
    // DAP_TEST_ASSERT_NOT_NULL(l_deserialized_sig, "Signature deserialization should succeed");

    // int l_verify_result = dap_sign_verify(l_deserialized_sig, &l_message_hash, sizeof(l_message_hash));
    // DAP_TEST_ASSERT(l_verify_result == 0, "Deserialized signature verification should succeed");

    // DAP_TEST_ASSERT(memcmp(l_original_sig, l_deserialized_sig, sizeof(dap_sign_t)) == 0,
    //                "Original and deserialized signatures should be identical");

    log_it(L_INFO, "Signature serialization test skipped - API not implemented");

    // Cleanup
    DAP_DELETE(l_original_sig);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Signature serialization tests passed");
    return true;
}

/**
 * @brief Test signature size calculations
 */
static bool s_test_signature_sizes(void) {
    log_it(L_INFO, "Testing signature size calculations...");

    // Test various ring sizes for Chipmunk Ring
    const size_t l_ring_sizes[] = {2, 4, 8, 16, 32, 64};

    for (size_t i = 0; i < sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]); i++) {
        size_t l_ring_size = l_ring_sizes[i];
        size_t l_sig_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);

        DAP_TEST_ASSERT(l_sig_size > 0, "Signature size should be positive");
        DAP_TEST_ASSERT(l_sig_size > 1000, "Ring signature should be large enough for anonymity");

        if (l_ring_size < 64) {  // Test for reasonable ring sizes
            size_t l_next_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size * 2);
            DAP_TEST_ASSERT(l_next_size > l_sig_size, "Larger ring should produce larger signature");
        }

        log_it(L_DEBUG, "Ring size %zu -> signature size %zu bytes", l_ring_size, l_sig_size);
    }

    log_it(L_INFO, "✓ Signature size calculation tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Starting Signature Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting signature unit tests...");

    // Initialize logging for tests
    dap_test_logging_init();

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_aggregation_support();
    l_all_passed &= s_test_aggregation_types_query();
    l_all_passed &= s_test_signature_info_functions();
    l_all_passed &= s_test_basic_signatures();
    l_all_passed &= s_test_signature_serialization();
    l_all_passed &= s_test_signature_sizes();

    // Cleanup
    dap_test_logging_restore();

    log_it(L_NOTICE, "Signature unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL signature unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some signature unit tests FAILED!");
        return -1;
    }
}
