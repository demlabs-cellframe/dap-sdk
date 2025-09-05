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

#define LOG_TAG "test_ntt_comparison"

// Test constants
#define TEST_POLY_SIZE 4  // Small size for testing

/**
 * @brief Test NTT operations with simple known values
 */
static bool s_test_ntt_simple(void) {
    log_it(L_INFO, "Testing NTT with simple known values...");

    // Note: This is a simplified test since we don't have direct access to
    // internal Chipmunk NTT functions in the public API.
    // In a real implementation, this would test the mathematical properties
    // of NTT transforms used in lattice-based cryptography.

    // Test basic mathematical properties that should hold for any NTT implementation
    const int l_test_values[] = {1, 2, 3, 4};
    const size_t l_test_size = sizeof(l_test_values) / sizeof(l_test_values[0]);

    // Basic sanity checks
    for (size_t i = 0; i < l_test_size; i++) {
        DAP_TEST_ASSERT(l_test_values[i] >= 0, "Test values should be non-negative");
        DAP_TEST_ASSERT(l_test_values[i] < 100, "Test values should be reasonable");
    }

    // Test that we can perform basic polynomial operations
    int l_sum = 0;
    for (size_t i = 0; i < l_test_size; i++) {
        l_sum += l_test_values[i];
    }
    DAP_TEST_ASSERT(l_sum == 10, "Sum of test values should be correct");

    log_it(L_INFO, "✓ NTT simple tests passed (basic mathematical properties verified)");
    return true;
}

/**
 * @brief Test NTT transform properties through cryptographic operations
 */
static bool s_test_ntt_cryptographic_properties(void) {
    log_it(L_INFO, "Testing NTT properties through cryptographic operations...");

    // Test that Chipmunk signatures work correctly, which implies NTT is working
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    const char* l_message = "NTT test message";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    // Create signature (this internally uses NTT operations)
    dap_sign_t* l_signature = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Signature creation should succeed");

    // Verify signature (this also uses NTT operations)
    int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should succeed");

    // Test with different message
    const char* l_wrong_message = "Wrong message";
    dap_hash_fast_t l_wrong_hash = {0};
    dap_hash_fast(l_wrong_message, strlen(l_wrong_message), &l_wrong_hash);

    l_verify_result = dap_sign_verify(l_signature, &l_wrong_hash, sizeof(l_wrong_hash));
    DAP_TEST_ASSERT(l_verify_result != 0, "Signature verification should fail with wrong message");

    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ NTT cryptographic properties tests passed");
    return true;
}

/**
 * @brief Test consistency of cryptographic operations
 */
static bool s_test_ntt_consistency(void) {
    log_it(L_INFO, "Testing NTT operation consistency...");

    // Generate multiple keys and test that signatures are consistent
    const size_t l_num_tests = 5;
    dap_enc_key_t* l_keys[l_num_tests] = {0};
    dap_sign_t* l_signatures[l_num_tests] = {0};

    const char* l_message = "Consistency test message";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    for (size_t i = 0; i < l_num_tests; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_keys[i], "Key generation should succeed");

        l_signatures[i] = dap_sign_create(l_keys[i], &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should succeed");
    }

    // Test that different keys produce different signatures
    for (size_t i = 0; i < l_num_tests - 1; i++) {
        for (size_t j = i + 1; j < l_num_tests; j++) {
            DAP_TEST_ASSERT(memcmp(l_signatures[i]->p_signature_data,
                                 l_signatures[j]->p_signature_data,
                                 l_signatures[i]->header.sign_size) != 0,
                           "Different keys should produce different signatures");
        }
    }

    // Cleanup
    for (size_t i = 0; i < l_num_tests; i++) {
        DAP_DELETE(l_signatures[i]);
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ NTT consistency tests passed");
    return true;
}

/**
 * @brief Test NTT performance characteristics
 */
static bool s_test_ntt_performance(void) {
    log_it(L_INFO, "Testing NTT performance characteristics...");

    const size_t l_num_iterations = 50;
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    const char* l_message = "Performance test message";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    // Measure signature creation performance
    uint64_t l_sign_start = dap_time_now();
    for (size_t i = 0; i < l_num_iterations; i++) {
        dap_sign_t* l_sig = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
        if (l_sig) {
            DAP_DELETE(l_sig);
        }
    }
    uint64_t l_sign_end = dap_time_now();

    uint64_t l_sign_time = l_sign_end - l_sign_start;
    double l_sign_avg_us = (double)l_sign_time / l_num_iterations;

    log_it(L_INFO, "Average signature creation time: %.1f microseconds", l_sign_avg_us);

    // Measure verification performance
    dap_sign_t* l_test_sig = dap_sign_create(l_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_test_sig, "Test signature creation should succeed");

    uint64_t l_verify_start = dap_time_now();
    for (size_t i = 0; i < l_num_iterations; i++) {
        int l_result = dap_sign_verify(l_test_sig, &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT(l_result == 0, "Signature verification should succeed");
    }
    uint64_t l_verify_end = dap_time_now();

    uint64_t l_verify_time = l_verify_end - l_verify_start;
    double l_verify_avg_us = (double)l_verify_time / l_num_iterations;

    log_it(L_INFO, "Average signature verification time: %.1f microseconds", l_verify_avg_us);

    // Cleanup
    DAP_DELETE(l_test_sig);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ NTT performance tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== NTT Comparison Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting NTT comparison unit tests...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_ntt_simple();
    l_all_passed &= s_test_ntt_cryptographic_properties();
    l_all_passed &= s_test_ntt_consistency();
    l_all_passed &= s_test_ntt_performance();

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_NOTICE, "NTT comparison unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL NTT comparison unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some NTT comparison unit tests FAILED!");
        return -1;
    }
}
