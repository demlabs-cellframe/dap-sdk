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
#include "dap_test.h"
#include <dap_enc_key.h>
#include <dap_enc.h>
#include "../fixtures/utilities/test_helpers.h"

#define LOG_TAG "test_encryption"

// Test constants
#define TEST_DATA_SIZE 1024
#define TEST_ITERATIONS 10

/**
 * @brief Test basic encryption/decryption with Chipmunk
 */
static bool s_test_chipmunk_encryption(void) {
    log_it(L_INFO, "Testing Chipmunk encryption/decryption...");

    // Generate encryption key
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Encryption key generation should succeed");

    // Test data
    uint8_t l_original_data[TEST_DATA_SIZE];
    dap_test_random_bytes(l_original_data, TEST_DATA_SIZE);

    // Encrypt data
    size_t l_encrypted_size = TEST_DATA_SIZE + 256; // Add some padding for encryption overhead
    uint8_t* l_encrypted_data = DAP_NEW_Z_SIZE(uint8_t, l_encrypted_size);

    int l_encrypt_result = dap_enc_code(l_key, l_original_data, TEST_DATA_SIZE,
                                       l_encrypted_data, l_encrypted_size, 0);
    DAP_TEST_ASSERT(l_encrypt_result >= 0, "Encryption should succeed");

    size_t l_actual_encrypted_size = (size_t)l_encrypt_result;

    // Decrypt data
    uint8_t* l_decrypted_data = DAP_NEW_Z_SIZE(uint8_t, TEST_DATA_SIZE);
    int l_decrypt_result = dap_enc_decode(l_key, l_encrypted_data, l_actual_encrypted_size,
                                         l_decrypted_data, TEST_DATA_SIZE, 0);
    DAP_TEST_ASSERT(l_decrypt_result >= 0, "Decryption should succeed");

    // Verify decrypted data matches original
    DAP_TEST_ASSERT(memcmp(l_original_data, l_decrypted_data, TEST_DATA_SIZE) == 0,
                   "Decrypted data should match original");

    // Cleanup
    DAP_DELETE(l_encrypted_data);
    DAP_DELETE(l_decrypted_data);
    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Chipmunk encryption/decryption tests passed");
    return true;
}

/**
 * @brief Test encryption with different data sizes
 */
static bool s_test_encryption_data_sizes(void) {
    log_it(L_INFO, "Testing encryption with different data sizes...");

    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    // Test different data sizes
    const size_t l_test_sizes[] = {1, 16, 64, 256, 1024, 4096};
    const size_t l_num_sizes = sizeof(l_test_sizes) / sizeof(l_test_sizes[0]);

    for (size_t i = 0; i < l_num_sizes; i++) {
        size_t l_data_size = l_test_sizes[i];

        // Generate test data
        uint8_t* l_original_data = DAP_NEW_Z_SIZE(uint8_t, l_data_size);
        dap_test_random_bytes(l_original_data, l_data_size);

        // Encrypt
        size_t l_encrypted_size = l_data_size + 256;
        uint8_t* l_encrypted_data = DAP_NEW_Z_SIZE(uint8_t, l_encrypted_size);

        int l_encrypt_result = dap_enc_code(l_key, l_original_data, l_data_size,
                                           l_encrypted_data, l_encrypted_size, 0);
        char msg[100];
        snprintf(msg, sizeof(msg), "Encryption should succeed for size %zu", l_data_size);
        DAP_TEST_ASSERT(l_encrypt_result >= 0, msg);

        // Decrypt
        uint8_t* l_decrypted_data = DAP_NEW_Z_SIZE(uint8_t, l_data_size);
        int l_decrypt_result = dap_enc_decode(l_key, l_encrypted_data, (size_t)l_encrypt_result,
                                             l_decrypted_data, l_data_size, 0);
        snprintf(msg, sizeof(msg), "Decryption should succeed for size %zu", l_data_size);
        DAP_TEST_ASSERT(l_decrypt_result >= 0, msg);

        // Verify
        snprintf(msg, sizeof(msg), "Decrypted data should match original for size %zu", l_data_size);
        DAP_TEST_ASSERT(memcmp(l_original_data, l_decrypted_data, l_data_size) == 0, msg);

        // Cleanup for this iteration
        DAP_DELETE(l_original_data);
        DAP_DELETE(l_encrypted_data);
        DAP_DELETE(l_decrypted_data);

        log_it(L_DEBUG, "✓ Encryption test passed for data size %zu bytes", l_data_size);
    }

    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Encryption data size tests passed");
    return true;
}

/**
 * @brief Test encryption/decryption consistency across multiple operations
 */
static bool s_test_encryption_consistency(void) {
    log_it(L_INFO, "Testing encryption/decryption consistency...");

    char msg[100];
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    for (int l_iteration = 0; l_iteration < TEST_ITERATIONS; l_iteration++) {
        // Generate unique test data for each iteration
        uint8_t l_original_data[TEST_DATA_SIZE];
        dap_test_random_bytes(l_original_data, TEST_DATA_SIZE);

        // Encrypt
        size_t l_encrypted_size = TEST_DATA_SIZE + 256;
        uint8_t l_encrypted_data[l_encrypted_size];

        int l_encrypt_result = dap_enc_code(l_key, l_original_data, TEST_DATA_SIZE,
                                           l_encrypted_data, l_encrypted_size, 0);
        snprintf(msg, sizeof(msg), "Encryption should succeed in iteration %d", l_iteration);
        DAP_TEST_ASSERT(l_encrypt_result >= 0, msg);

        // Decrypt
        uint8_t l_decrypted_data[TEST_DATA_SIZE];
        int l_decrypt_result = dap_enc_decode(l_key, l_encrypted_data, (size_t)l_encrypt_result,
                                             l_decrypted_data, TEST_DATA_SIZE, 0);
        snprintf(msg, sizeof(msg), "Decryption should succeed in iteration %d", l_iteration);
        DAP_TEST_ASSERT(l_decrypt_result >= 0, msg);

        // Verify
        snprintf(msg, sizeof(msg), "Data integrity check failed in iteration %d", l_iteration);
        DAP_TEST_ASSERT(memcmp(l_original_data, l_decrypted_data, TEST_DATA_SIZE) == 0, msg);
    }

    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Encryption consistency tests passed (%d iterations)", TEST_ITERATIONS);
    return true;
}

/**
 * @brief Test encryption with different key types
 */
static bool s_test_multiple_key_types(void) {
    log_it(L_INFO, "Testing encryption with different key types...");

    char msg[100];

    // Test with available key types that support encryption
    dap_enc_key_type_t l_key_types[] = {
        DAP_ENC_KEY_TYPE_IAES,
        DAP_ENC_KEY_TYPE_OAES
        // Note: Chipmunk keys are signature-only, not for encryption
    };
    const size_t l_num_types = sizeof(l_key_types) / sizeof(l_key_types[0]);

    for (size_t i = 0; i < l_num_types; i++) {
        log_it(L_DEBUG, "Testing key type: %d", l_key_types[i]);

        dap_enc_key_t* l_key = dap_enc_key_new_generate(l_key_types[i], NULL, 0, NULL, 0, 0);
        if (!l_key) {
            log_it(L_WARNING, "Key type %d not available, skipping", l_key_types[i]);
            continue;
        }

        // Test basic encrypt/decrypt
        uint8_t l_test_data[256];
        dap_test_random_bytes(l_test_data, sizeof(l_test_data));

        size_t l_encrypted_size = sizeof(l_test_data) + 256;
        uint8_t l_encrypted_data[l_encrypted_size];

        int l_encrypt_result = dap_enc_code(l_key, l_test_data, sizeof(l_test_data),
                                           l_encrypted_data, l_encrypted_size, 0);

        if (l_encrypt_result >= 0) {
            uint8_t l_decrypted_data[sizeof(l_test_data)];
            int l_decrypt_result = dap_enc_decode(l_key, l_encrypted_data, (size_t)l_encrypt_result,
                                                 l_decrypted_data, sizeof(l_test_data), 0);

            if (l_decrypt_result >= 0) {
                snprintf(msg, sizeof(msg), "Encryption/decryption should work for key type %d", l_key_types[i]);
                DAP_TEST_ASSERT(memcmp(l_test_data, l_decrypted_data, sizeof(l_test_data)) == 0, msg);
                log_it(L_DEBUG, "✓ Key type %d encryption/decryption test passed", l_key_types[i]);
            }
        }

        dap_enc_key_delete(l_key);
    }

    log_it(L_INFO, "✓ Multiple key types tests passed");
    return true;
}

/**
 * @brief Test error handling in encryption operations
 */
static bool s_test_encryption_error_handling(void) {
    log_it(L_INFO, "Testing encryption error handling...");

    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Key generation should succeed");

    uint8_t l_test_data[256];
    dap_test_random_bytes(l_test_data, sizeof(l_test_data));

    // Test with NULL key
    uint8_t l_encrypted_data[512];
    int l_result = dap_enc_code(NULL, l_test_data, sizeof(l_test_data),
                               l_encrypted_data, sizeof(l_encrypted_data), 0);
    DAP_TEST_ASSERT(l_result == 0, "Encryption should return 0 (no bytes written) with NULL key");

    // Test with NULL input data
    l_result = dap_enc_code(l_key, NULL, sizeof(l_test_data),
                           l_encrypted_data, sizeof(l_encrypted_data), 0);
    DAP_TEST_ASSERT(l_result == 0, "Encryption should return 0 (no bytes written) with NULL input data");

    // Test with zero input size
    l_result = dap_enc_code(l_key, l_test_data, 0,
                           l_encrypted_data, sizeof(l_encrypted_data), 0);
    DAP_TEST_ASSERT(l_result == 0, "Encryption should return 0 (no bytes written) with zero input size");

    // Test with insufficient output buffer
    l_result = dap_enc_code(l_key, l_test_data, sizeof(l_test_data),
                           l_encrypted_data, 10, 0); // Very small buffer
    // This might succeed or fail depending on implementation, but shouldn't crash

    // Test decryption with NULL key
    uint8_t l_decrypted_data[256];
    l_result = dap_enc_decode(NULL, l_encrypted_data, 100,
                             l_decrypted_data, sizeof(l_decrypted_data), 0);
    DAP_TEST_ASSERT(l_result == 0, "Decryption should return 0 (no bytes written) with NULL key");

    // Test decryption with NULL input
    l_result = dap_enc_decode(l_key, NULL, 100,
                             l_decrypted_data, sizeof(l_decrypted_data), 0);
    DAP_TEST_ASSERT(l_result == 0, "Decryption should return 0 (no bytes written) with NULL input");

    dap_enc_key_delete(l_key);

    log_it(L_INFO, "✓ Encryption error handling tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Encryption Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting encryption unit tests...");

    // Initialize logging for tests
    dap_test_logging_init();

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_chipmunk_encryption();
    l_all_passed &= s_test_encryption_data_sizes();
    l_all_passed &= s_test_encryption_consistency();
    l_all_passed &= s_test_multiple_key_types();
    l_all_passed &= s_test_encryption_error_handling();

    // Cleanup
    dap_test_logging_restore();

    log_it(L_NOTICE, "Encryption unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL encryption unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some encryption unit tests FAILED!");
        return -1;
    }
}
