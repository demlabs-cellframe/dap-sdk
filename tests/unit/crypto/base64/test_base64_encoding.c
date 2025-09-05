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
#include "../../../../module/test/dap_test.h"
#include <dap_enc_base64.h>
#include <dap_random.h>

#define LOG_TAG "test_base64_encoding"

// Test constants
#define TEST_ITERATIONS 100
#define MAX_TEST_SIZE 1024

/**
 * @brief Test Base64 encode/decode with standard encoding
 */
static bool s_test_base64_standard(void) {
    log_it(L_INFO, "Testing Base64 standard encoding...");

    for (int l_iteration = 0; l_iteration < TEST_ITERATIONS; l_iteration++) {
        // Random size between 1 and MAX_TEST_SIZE
        size_t l_test_size = (random_uint32_t() % MAX_TEST_SIZE) + 1;
        uint8_t l_source_data[l_test_size];
        randombytes(l_source_data, l_test_size);

        // Encode
        size_t l_encoded_size = DAP_ENC_BASE64_ENCODE_SIZE(l_test_size);
        char l_encoded_data[l_encoded_size];

        size_t l_actual_encoded_size = dap_enc_base64_encode(
            l_source_data, l_test_size, l_encoded_data, DAP_ENC_DATA_TYPE_B64);

        DAP_TEST_ASSERT(l_actual_encoded_size == l_encoded_size, "Encoded size should match expected");

        // Decode
        uint8_t l_decoded_data[l_test_size];
        size_t l_decoded_size = dap_enc_base64_decode(
            l_encoded_data, l_actual_encoded_size, l_decoded_data, DAP_ENC_DATA_TYPE_B64);

        DAP_TEST_ASSERT(l_decoded_size == l_test_size, "Decoded size should match original");
        DAP_TEST_ASSERT(memcmp(l_source_data, l_decoded_data, l_test_size) == 0,
                       "Decoded data should match original");
    }

    log_it(L_INFO, "✓ Base64 standard encoding tests passed");
    return true;
}

/**
 * @brief Test Base64 encode/decode with URL-safe encoding
 */
static bool s_test_base64_urlsafe(void) {
    log_it(L_INFO, "Testing Base64 URL-safe encoding...");

    for (int l_iteration = 0; l_iteration < TEST_ITERATIONS; l_iteration++) {
        // Random size between 1 and MAX_TEST_SIZE
        size_t l_test_size = (random_uint32_t() % MAX_TEST_SIZE) + 1;
        uint8_t l_source_data[l_test_size];
        randombytes(l_source_data, l_test_size);

        // Encode
        size_t l_encoded_size = DAP_ENC_BASE64_ENCODE_SIZE(l_test_size);
        char l_encoded_data[l_encoded_size];

        size_t l_actual_encoded_size = dap_enc_base64_encode(
            l_source_data, l_test_size, l_encoded_data, DAP_ENC_DATA_TYPE_B64_URLSAFE);

        DAP_TEST_ASSERT(l_actual_encoded_size == l_encoded_size, "Encoded size should match expected");

        // Decode
        uint8_t l_decoded_data[l_test_size];
        size_t l_decoded_size = dap_enc_base64_decode(
            l_encoded_data, l_actual_encoded_size, l_decoded_data, DAP_ENC_DATA_TYPE_B64_URLSAFE);

        DAP_TEST_ASSERT(l_decoded_size == l_test_size, "Decoded size should match original");
        DAP_TEST_ASSERT(memcmp(l_source_data, l_decoded_data, l_test_size) == 0,
                       "Decoded data should match original");
    }

    log_it(L_INFO, "✓ Base64 URL-safe encoding tests passed");
    return true;
}

/**
 * @brief Test Base64 edge cases
 */
static bool s_test_base64_edge_cases(void) {
    log_it(L_INFO, "Testing Base64 edge cases...");

    // Test empty data
    char l_empty_encoded[10];
    size_t l_empty_encoded_size = dap_enc_base64_encode(NULL, 0, l_empty_encoded, DAP_ENC_DATA_TYPE_B64);
    DAP_TEST_ASSERT(l_empty_encoded_size == 0, "Empty data should encode to empty string");

    // Test single byte
    uint8_t l_single_byte = 0xFF;
    char l_single_encoded[10];
    size_t l_single_encoded_size = dap_enc_base64_encode(&l_single_byte, 1, l_single_encoded, DAP_ENC_DATA_TYPE_B64);
    DAP_TEST_ASSERT(l_single_encoded_size > 0, "Single byte should encode successfully");

    uint8_t l_single_decoded;
    size_t l_single_decoded_size = dap_enc_base64_decode(l_single_encoded, l_single_encoded_size,
                                                        &l_single_decoded, DAP_ENC_DATA_TYPE_B64);
    DAP_TEST_ASSERT(l_single_decoded_size == 1, "Single byte decode size should be correct");
    DAP_TEST_ASSERT(l_single_decoded == l_single_byte, "Single byte decode should match original");

    // Test various data patterns
    uint8_t l_test_patterns[][4] = {
        {0x00, 0x00, 0x00, 0x00},  // All zeros
        {0xFF, 0xFF, 0xFF, 0xFF},  // All ones
        {0x00, 0x00, 0x00, 0x01},  // Leading zeros
        {0x01, 0x00, 0x00, 0x00}   // Trailing zeros
    };

    for (size_t i = 0; i < sizeof(l_test_patterns) / sizeof(l_test_patterns[0]); i++) {
        char l_pattern_encoded[10];
        size_t l_pattern_encoded_size = dap_enc_base64_encode(
            l_test_patterns[i], 4, l_pattern_encoded, DAP_ENC_DATA_TYPE_B64);
        DAP_TEST_ASSERT(l_pattern_encoded_size > 0, "Pattern should encode successfully");

        uint8_t l_pattern_decoded[4];
        size_t l_pattern_decoded_size = dap_enc_base64_decode(
            l_pattern_encoded, l_pattern_encoded_size, l_pattern_decoded, DAP_ENC_DATA_TYPE_B64);
        DAP_TEST_ASSERT(l_pattern_decoded_size == 4, "Pattern decode size should be correct");
        DAP_TEST_ASSERT(memcmp(l_test_patterns[i], l_pattern_decoded, 4) == 0,
                       "Pattern decode should match original");
    }

    log_it(L_INFO, "✓ Base64 edge case tests passed");
    return true;
}

/**
 * @brief Test Base64 encoding consistency between standard and URL-safe
 */
static bool s_test_base64_consistency(void) {
    log_it(L_INFO, "Testing Base64 encoding consistency...");

    // Test data that should produce different encodings
    uint8_t l_test_data[] = {0xFF, 0xEF, 0xBF, 0x00}; // Contains '+' and '/' characters

    char l_standard_encoded[10];
    char l_urlsafe_encoded[10];

    size_t l_standard_size = dap_enc_base64_encode(
        l_test_data, sizeof(l_test_data), l_standard_encoded, DAP_ENC_DATA_TYPE_B64);
    size_t l_urlsafe_size = dap_enc_base64_encode(
        l_test_data, sizeof(l_test_data), l_urlsafe_encoded, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    // Both should decode to the same original data
    uint8_t l_standard_decoded[sizeof(l_test_data)];
    uint8_t l_urlsafe_decoded[sizeof(l_test_data)];

    size_t l_standard_decoded_size = dap_enc_base64_decode(
        l_standard_encoded, l_standard_size, l_standard_decoded, DAP_ENC_DATA_TYPE_B64);
    size_t l_urlsafe_decoded_size = dap_enc_base64_decode(
        l_urlsafe_encoded, l_urlsafe_size, l_urlsafe_decoded, DAP_ENC_DATA_TYPE_B64_URLSAFE);

    DAP_TEST_ASSERT(l_standard_decoded_size == sizeof(l_test_data), "Standard decode size should be correct");
    DAP_TEST_ASSERT(l_urlsafe_decoded_size == sizeof(l_test_data), "URL-safe decode size should be correct");
    DAP_TEST_ASSERT(memcmp(l_test_data, l_standard_decoded, sizeof(l_test_data)) == 0,
                   "Standard decode should match original");
    DAP_TEST_ASSERT(memcmp(l_test_data, l_urlsafe_decoded, sizeof(l_test_data)) == 0,
                   "URL-safe decode should match original");

    log_it(L_INFO, "✓ Base64 consistency tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Starting Base64 Encoding Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting Base64 encoding unit tests...");

    // Initialize DAP SDK
    dap_test_logging_init();
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_base64_standard();
    l_all_passed &= s_test_base64_urlsafe();
    l_all_passed &= s_test_base64_edge_cases();
    l_all_passed &= s_test_base64_consistency();

    // Cleanup
    dap_test_logging_restore();

    log_it(L_NOTICE, "Base64 encoding unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL Base64 encoding unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some Base64 encoding unit tests FAILED!");
        return -1;
    }
}
