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
#include <dap_enc_base58.h>
#include "rand/dap_rand.h"

#define LOG_TAG "test_base58_encoding"

// Test constants
#define TEST_ITERATIONS 100
#define MAX_TEST_SIZE 1024

/**
 * @brief Test basic Base58 encode/decode functionality
 */
static bool s_test_base58_encode_decode(void) {
    log_it(L_INFO, "Testing Base58 encode/decode operations...");

    // Test with various data sizes
    for (size_t l_test_size = 1; l_test_size <= MAX_TEST_SIZE; l_test_size *= 2) {
        uint8_t l_source_data[l_test_size];
        randombytes(l_source_data, l_test_size);

        // Calculate encoded size
        size_t l_encoded_size = DAP_ENC_BASE58_ENCODE_SIZE(l_test_size);
        char l_encoded_data[l_encoded_size];

        // Encode
        size_t l_actual_encoded_size = dap_enc_base58_encode(l_source_data, l_test_size, l_encoded_data);
        DAP_TEST_ASSERT(l_actual_encoded_size <= l_encoded_size, "Encoded size should not exceed calculated size");

        // Decode
        uint8_t l_decoded_data[l_test_size];
        size_t l_decoded_size = dap_enc_base58_decode(l_encoded_data, l_decoded_data);
        DAP_TEST_ASSERT(l_decoded_size == l_test_size, "Decoded size should match original size");

        // Verify data integrity
        DAP_TEST_ASSERT(memcmp(l_source_data, l_decoded_data, l_test_size) == 0,
                       "Decoded data should match original data");

        if (l_test_size <= 32) {
            log_it(L_DEBUG, "✓ Base58 test passed for size %zu", l_test_size);
        }
    }

    log_it(L_INFO, "✓ Base58 encode/decode tests passed");
    return true;
}

/**
 * @brief Test Base58 encoding with edge cases
 */
static bool s_test_base58_edge_cases(void) {
    log_it(L_INFO, "Testing Base58 edge cases...");

    // Test empty data
    char l_empty_encoded[10];
    size_t l_empty_encoded_size = dap_enc_base58_encode(NULL, 0, l_empty_encoded);
    DAP_TEST_ASSERT(l_empty_encoded_size == 0, "Empty data should encode to empty string");

    // Test single byte
    uint8_t l_single_byte = 0xFF;
    char l_single_encoded[10];
    size_t l_single_encoded_size = dap_enc_base58_encode(&l_single_byte, 1, l_single_encoded);
    DAP_TEST_ASSERT(l_single_encoded_size > 0, "Single byte should encode successfully");

    uint8_t l_single_decoded;
    size_t l_single_decoded_size = dap_enc_base58_decode(l_single_encoded, &l_single_decoded);
    DAP_TEST_ASSERT(l_single_decoded_size == 1, "Single byte decode size should be correct");
    DAP_TEST_ASSERT(l_single_decoded == l_single_byte, "Single byte decode should match original");

    // Test leading zeros
    uint8_t l_leading_zeros[3] = {0, 0, 1};
    char l_zeros_encoded[10];
    size_t l_zeros_encoded_size = dap_enc_base58_encode(l_leading_zeros, 3, l_zeros_encoded);
    DAP_TEST_ASSERT(l_zeros_encoded_size > 0, "Leading zeros should encode successfully");

    uint8_t l_zeros_decoded[3];
    size_t l_zeros_decoded_size = dap_enc_base58_decode(l_zeros_encoded, l_zeros_decoded);
    DAP_TEST_ASSERT(l_zeros_decoded_size == 3, "Leading zeros decode size should be correct");
    DAP_TEST_ASSERT(memcmp(l_leading_zeros, l_zeros_decoded, 3) == 0,
                   "Leading zeros decode should match original");

    log_it(L_INFO, "✓ Base58 edge case tests passed");
    return true;
}

/**
 * @brief Test Base58 encoding with random data
 */
static bool s_test_base58_random_data(void) {
    log_it(L_INFO, "Testing Base58 with random data...");

    for (int i = 0; i < TEST_ITERATIONS; i++) {
        // Random size between 1 and 256
        size_t l_test_size = (random_uint32_t() % 256) + 1;
        uint8_t l_source_data[l_test_size];
        randombytes(l_source_data, l_test_size);

        // Encode
        size_t l_encoded_size = DAP_ENC_BASE58_ENCODE_SIZE(l_test_size);
        char* l_encoded_data = DAP_NEW_Z_SIZE(char, l_encoded_size);
        DAP_TEST_ASSERT_NOT_NULL(l_encoded_data, "Memory allocation should succeed");

        size_t l_actual_encoded_size = dap_enc_base58_encode(l_source_data, l_test_size, l_encoded_data);
        DAP_TEST_ASSERT(l_actual_encoded_size <= l_encoded_size, "Encoded size should be valid");

        // Decode
        uint8_t* l_decoded_data = DAP_NEW_Z_SIZE(uint8_t, l_test_size);
        DAP_TEST_ASSERT_NOT_NULL(l_decoded_data, "Memory allocation should succeed");

        size_t l_decoded_size = dap_enc_base58_decode(l_encoded_data, l_decoded_data);
        DAP_TEST_ASSERT(l_decoded_size == l_test_size, "Decoded size should match original");

        // Verify
        DAP_TEST_ASSERT(memcmp(l_source_data, l_decoded_data, l_test_size) == 0,
                       "Decoded data should match original");

        // Cleanup
        DAP_DELETE(l_encoded_data);
        DAP_DELETE(l_decoded_data);
    }

    log_it(L_INFO, "✓ Base58 random data tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Starting Base58 Encoding Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting Base58 encoding unit tests...");

    // Initialize DAP SDK
    dap_test_logging_init();
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_base58_encode_decode();
    l_all_passed &= s_test_base58_edge_cases();
    l_all_passed &= s_test_base58_random_data();

    // Cleanup
    dap_test_logging_restore();

    log_it(L_NOTICE, "Base58 encoding unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL Base58 encoding unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some Base58 encoding unit tests FAILED!");
        return -1;
    }
}
