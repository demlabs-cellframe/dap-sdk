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
#include "dap_enc_base58.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include "../../../fixtures/json_samples.h"
#include <inttypes.h>
#include <string.h>

#define LOG_TAG "test_base58"

/**
 * @brief Test Base58 basic functionality
 */
static bool s_test_base58_basic(void) {
    log_it(L_DEBUG, "Testing Base58 basic functionality");
    
    const char* l_input = CRYPTO_SAMPLE_HASH_INPUT;
    size_t l_input_size = strlen(l_input);
    
    // Calculate encode size
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_input_size);
    char l_encoded[l_encode_size];
    
    // Encode
    size_t l_encoded_result = dap_enc_base58_encode(l_input, l_input_size, l_encoded);
    DAP_TEST_ASSERT(l_encoded_result > 0, "Base58 encoding should succeed");
    DAP_TEST_ASSERT(l_encoded_result <= l_encode_size, "Encoded size should not exceed calculated size");
    
    // Decode
    size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encoded_result);
    uint8_t l_decoded[l_decode_size];
    size_t l_decoded_result = dap_enc_base58_decode(l_encoded, l_decoded);
    
    DAP_TEST_ASSERT(l_decoded_result > 0, "Base58 decoding should succeed");
    DAP_TEST_ASSERT(l_decoded_result == l_input_size, "Decoded size should match original input size");
    
    // Verify decoded data matches original
    int l_cmp = memcmp(l_input, l_decoded, l_input_size);
    DAP_TEST_ASSERT(l_cmp == 0, "Decoded data should match original input");
    
    log_it(L_DEBUG, "Base58 basic test passed");
    return true;
}

/**
 * @brief Test Base58 consistency
 */
static bool s_test_base58_consistency(void) {
    log_it(L_DEBUG, "Testing Base58 consistency");
    
    const char* l_input = "DAP SDK consistent base58 test";
    size_t l_input_size = strlen(l_input);
    
    // Calculate encode size
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_input_size);
    char l_encoded1[l_encode_size];
    char l_encoded2[l_encode_size];
    
    // Encode twice
    size_t l_encoded_result1 = dap_enc_base58_encode(l_input, l_input_size, l_encoded1);
    size_t l_encoded_result2 = dap_enc_base58_encode(l_input, l_input_size, l_encoded2);
    
    DAP_TEST_ASSERT(l_encoded_result1 > 0, "First encoding should succeed");
    DAP_TEST_ASSERT(l_encoded_result2 > 0, "Second encoding should succeed");
    DAP_TEST_ASSERT(l_encoded_result1 == l_encoded_result2, "Both encodings should produce same size");
    
    // Verify encoded strings are identical
    int l_cmp = strcmp(l_encoded1, l_encoded2);
    DAP_TEST_ASSERT(l_cmp == 0, "Consistent input should produce identical encodings");
    
    // Verify both decode to same result
    size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encoded_result1);
    uint8_t l_decoded1[l_decode_size];
    uint8_t l_decoded2[l_decode_size];
    
    size_t l_decoded_result1 = dap_enc_base58_decode(l_encoded1, l_decoded1);
    size_t l_decoded_result2 = dap_enc_base58_decode(l_encoded2, l_decoded2);
    
    DAP_TEST_ASSERT(l_decoded_result1 == l_input_size, "First decode should match input size");
    DAP_TEST_ASSERT(l_decoded_result2 == l_input_size, "Second decode should match input size");
    
    int l_decoded_cmp = memcmp(l_decoded1, l_decoded2, l_input_size);
    DAP_TEST_ASSERT(l_decoded_cmp == 0, "Both decoded results should be identical");
    
    log_it(L_DEBUG, "Base58 consistency test passed");
    return true;
}

/**
 * @brief Test Base58 with empty input
 */
static bool s_test_base58_empty(void) {
    log_it(L_DEBUG, "Testing Base58 with empty input");
    
    // Test with empty string
    const char* l_empty = "";
    size_t l_empty_size = 0;
    
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_empty_size);
    char l_encoded[l_encode_size];
    
    size_t l_encoded_result = dap_enc_base58_encode(l_empty, l_empty_size, l_encoded);
    
    if (l_encoded_result > 0) {
        size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encoded_result);
        uint8_t l_decoded[l_decode_size];
        size_t l_decoded_result = dap_enc_base58_decode(l_encoded, l_decoded);
        
        DAP_TEST_ASSERT(l_decoded_result == l_empty_size, "Decoded empty string should have size 0");
    }
    
    log_it(L_DEBUG, "Base58 empty input test passed");
    return true;
}

/**
 * @brief Test Base58 performance
 */
static bool s_test_base58_performance(void) {
    log_it(L_DEBUG, "Testing Base58 performance");
    
    const size_t l_iterations = 1000;
    const char* l_input = CRYPTO_SAMPLE_HASH_INPUT;
    size_t l_input_size = strlen(l_input);
    
    size_t l_encode_size = DAP_ENC_BASE58_ENCODE_SIZE(l_input_size);
    char l_encoded[l_encode_size];
    size_t l_decode_size = DAP_ENC_BASE58_DECODE_SIZE(l_encode_size);
    uint8_t l_decoded[l_decode_size];
    
    dap_test_timer_t l_timer;
    dap_test_timer_start(&l_timer);
    
    for (size_t i = 0; i < l_iterations; i++) {
        size_t l_encoded_result = dap_enc_base58_encode(l_input, l_input_size, l_encoded);
        DAP_TEST_ASSERT(l_encoded_result > 0, "Encoding should succeed in performance test");
        
        size_t l_decoded_result = dap_enc_base58_decode(l_encoded, l_decoded);
        DAP_TEST_ASSERT(l_decoded_result == l_input_size, "Decoding should succeed in performance test");
        
        int l_cmp = memcmp(l_input, l_decoded, l_input_size);
        DAP_TEST_ASSERT(l_cmp == 0, "Round-trip should preserve data in performance test");
    }
    
    uint64_t l_elapsed = dap_test_timer_stop(&l_timer);
    double l_ops_per_sec = (double)l_iterations / (l_elapsed / 1000000.0);
    
    log_it(L_INFO, "Base58 performance: %.2f encode-decode cycles/sec (%zu iterations in %" PRIu64 " us)", 
           l_ops_per_sec, l_iterations, l_elapsed);
    
    // Basic performance threshold (should be able to do at least 100 cycles/sec)
    DAP_TEST_ASSERT(l_ops_per_sec > 100.0, "Base58 should achieve reasonable performance");
    
    log_it(L_DEBUG, "Base58 performance test passed");
    return true;
}

/**
 * @brief Main test function for Base58
 */
int main(void) {
    log_it(L_INFO, "Starting Base58 unit tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_base58_basic();
    l_all_passed &= s_test_base58_consistency();
    l_all_passed &= s_test_base58_empty();
    l_all_passed &= s_test_base58_performance();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Base58 tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Base58 tests failed!");
        return -1;
    }
}
