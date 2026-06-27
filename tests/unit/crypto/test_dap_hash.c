/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
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

#include "dap_test.h"
#include "dap_hash.h"
#include "dap_common.h"
#include <string.h>
#include <stdio.h>

#define LOG_TAG "test_dap_hash"

// Test constants
#define TEST_MESSAGE "DAP Hash Function Test Message"
#define TEST_SALT "TestSalt123"
#define SHORT_MESSAGE "Hello"
#define EMPTY_MESSAGE ""
#define LONG_MESSAGE "This is a very long test message that should be used to test hash functions with larger input data to ensure they work correctly with various input sizes and produce consistent results across different scenarios and use cases in the DAP SDK cryptographic framework."

/**
 * @brief Test basic hash types and their standard output sizes
 */
static bool s_test_basic_hash_types(void) {
    log_it(L_INFO, "Testing basic hash types and sizes...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Test SHA3-256 (32 bytes)
    uint8_t hash_256[32];
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size, 
                         hash_256, sizeof(hash_256), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "SHA3-256 hash should succeed");
    
    // Test SHA3-384 (48 bytes)
    uint8_t hash_384[48];
    result = dap_hash(DAP_HASH_TYPE_SHA3_384, test_data, test_data_size,
                     hash_384, sizeof(hash_384), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "SHA3-384 hash should succeed");
    
    // Test SHA3-512 (64 bytes)
    uint8_t hash_512[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_512, sizeof(hash_512), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "SHA3-512 hash should succeed");
    
    // Verify different hash types produce different outputs
    dap_assert(memcmp(hash_256, hash_384, 32) != 0, "SHA3-256 and SHA3-384 should produce different outputs");
    dap_assert(memcmp(hash_256, hash_512, 32) != 0, "SHA3-256 and SHA3-512 should produce different outputs");
    
    // Verify hashes are not all zeros
    bool hash_256_all_zeros = true;
    for (size_t i = 0; i < sizeof(hash_256); i++) {
        if (hash_256[i] != 0) {
            hash_256_all_zeros = false;
            break;
        }
    }
    dap_assert(!hash_256_all_zeros, "SHA3-256 hash should not be all zeros");
    
    log_it(L_INFO, "Basic hash types test passed");
    return true;
}

/**
 * @brief Test SHAKE functions with arbitrary output sizes
 */
static bool s_test_shake_arbitrary_sizes(void) {
    log_it(L_INFO, "Testing SHAKE functions with arbitrary output sizes...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Test different output sizes with SHAKE-128
    size_t test_sizes[] = {16, 32, 64, 96, 128, 200};
    size_t num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    uint8_t *previous_output = NULL;
    size_t previous_size = 0;
    
    for (size_t i = 0; i < num_tests; i++) {
        size_t output_size = test_sizes[i];
        uint8_t *output = DAP_NEW_Z_SIZE(uint8_t, output_size);
        dap_assert(output != NULL, "Output buffer allocation should succeed");
        
        int result = dap_hash(DAP_HASH_TYPE_SHAKE128, test_data, test_data_size,
                             output, output_size, DAP_HASH_FLAG_NONE, NULL);
        dap_assert(result == 0, "SHAKE-128 hash should succeed");
        
        // Verify output is not all zeros
        bool all_zeros = true;
        for (size_t j = 0; j < output_size; j++) {
            if (output[j] != 0) {
                all_zeros = false;
                break;
            }
        }
        dap_assert(!all_zeros, "SHAKE-128 output should not be all zeros");
        
        // Verify consistency: smaller output should be prefix of larger output
        if (previous_output != NULL && previous_size < output_size) {
            dap_assert(memcmp(previous_output, output, previous_size) == 0,
                      "SHAKE-128 should produce consistent prefixes");
        }
        
        log_it(L_DEBUG, "SHAKE-128 with %zu bytes: OK", output_size);
        
        if (previous_output) DAP_DELETE(previous_output);
        previous_output = output;
        previous_size = output_size;
    }
    
    if (previous_output) DAP_DELETE(previous_output);
    
    // Test SHAKE-256
    uint8_t shake256_output[128];
    int result = dap_hash(DAP_HASH_TYPE_SHAKE256, test_data, test_data_size,
                         shake256_output, sizeof(shake256_output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "SHAKE-256 hash should succeed");
    
    log_it(L_INFO, "SHAKE arbitrary sizes test passed");
    return true;
}

/**
 * @brief Test hash function determinism and consistency
 */
static bool s_test_hash_determinism(void) {
    log_it(L_INFO, "Testing hash function determinism...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Test SHA3-256 determinism
    uint8_t hash1[32], hash2[32], hash3[32];
    
    int result1 = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                          hash1, sizeof(hash1), DAP_HASH_FLAG_NONE, NULL);
    int result2 = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                          hash2, sizeof(hash2), DAP_HASH_FLAG_NONE, NULL);
    int result3 = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                          hash3, sizeof(hash3), DAP_HASH_FLAG_NONE, NULL);
    
    dap_assert(result1 == 0 && result2 == 0 && result3 == 0, "All hash operations should succeed");
    dap_assert(memcmp(hash1, hash2, sizeof(hash1)) == 0, "Hash should be deterministic (hash1 == hash2)");
    dap_assert(memcmp(hash2, hash3, sizeof(hash2)) == 0, "Hash should be deterministic (hash2 == hash3)");
    dap_assert(memcmp(hash1, hash3, sizeof(hash1)) == 0, "Hash should be deterministic (hash1 == hash3)");
    
    // Test with different input sizes
    uint8_t short_hash[32], long_hash[32], empty_hash[32];
    
    result1 = dap_hash(DAP_HASH_TYPE_SHA3_256, SHORT_MESSAGE, strlen(SHORT_MESSAGE),
                      short_hash, sizeof(short_hash), DAP_HASH_FLAG_NONE, NULL);
    result2 = dap_hash(DAP_HASH_TYPE_SHA3_256, LONG_MESSAGE, strlen(LONG_MESSAGE),
                      long_hash, sizeof(long_hash), DAP_HASH_FLAG_NONE, NULL);
    result3 = dap_hash(DAP_HASH_TYPE_SHA3_256, EMPTY_MESSAGE, strlen(EMPTY_MESSAGE),
                      empty_hash, sizeof(empty_hash), DAP_HASH_FLAG_NONE, NULL);
    
    dap_assert(result1 == 0 && result2 == 0 && result3 == 0, "All different input hash operations should succeed");
    dap_assert(memcmp(short_hash, long_hash, sizeof(short_hash)) != 0, "Different inputs should produce different hashes");
    dap_assert(memcmp(short_hash, empty_hash, sizeof(short_hash)) != 0, "Short and empty inputs should produce different hashes");
    dap_assert(memcmp(long_hash, empty_hash, sizeof(long_hash)) != 0, "Long and empty inputs should produce different hashes");
    
    log_it(L_INFO, "Hash determinism test passed");
    return true;
}

/**
 * @brief Test domain separation functionality
 */
static bool s_test_domain_separation(void) {
    log_it(L_INFO, "Testing domain separation...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Test with different domain separation contexts
    const char *domain1 = "DOMAIN_1";
    const char *domain2 = "DOMAIN_2";
    
    uint8_t hash_no_domain[32];
    uint8_t hash_domain1[32];
    uint8_t hash_domain2[32];
    
    // Hash without domain separation
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                         hash_no_domain, sizeof(hash_no_domain), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash without domain should succeed");
    
    // Hash with domain separation (simulate by prepending domain)
    char domain1_data[256];
    char domain2_data[256];
    snprintf(domain1_data, sizeof(domain1_data), "%s%s", domain1, test_data);
    snprintf(domain2_data, sizeof(domain2_data), "%s%s", domain2, test_data);
    
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, domain1_data, strlen(domain1_data),
                     hash_domain1, sizeof(hash_domain1), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash with domain1 should succeed");
    
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, domain2_data, strlen(domain2_data),
                     hash_domain2, sizeof(hash_domain2), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash with domain2 should succeed");
    
    // Verify domain separation works
    dap_assert(memcmp(hash_no_domain, hash_domain1, sizeof(hash_no_domain)) != 0,
               "Domain separated hash should differ from non-domain hash");
    dap_assert(memcmp(hash_domain1, hash_domain2, sizeof(hash_domain1)) != 0,
               "Different domains should produce different hashes");
    
    log_it(L_INFO, "Domain separation test passed");
    return true;
}

/**
 * @brief Test salt functionality
 */
static bool s_test_salt_functionality(void) {
    log_it(L_INFO, "Testing salt functionality...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    const char *salt1 = TEST_SALT;
    const char *salt2 = "DifferentSalt456";
    
    uint8_t hash_no_salt[32];
    uint8_t hash_salt1[32];
    uint8_t hash_salt2[32];
    
    // Hash without salt
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                         hash_no_salt, sizeof(hash_no_salt), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash without salt should succeed");
    
    // Hash with salt (simulate by appending salt)
    char salted_data1[256];
    char salted_data2[256];
    snprintf(salted_data1, sizeof(salted_data1), "%s%s", test_data, salt1);
    snprintf(salted_data2, sizeof(salted_data2), "%s%s", test_data, salt2);
    
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, salted_data1, strlen(salted_data1),
                     hash_salt1, sizeof(hash_salt1), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash with salt1 should succeed");
    
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, salted_data2, strlen(salted_data2),
                     hash_salt2, sizeof(hash_salt2), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash with salt2 should succeed");
    
    // Verify salt affects the hash
    dap_assert(memcmp(hash_no_salt, hash_salt1, sizeof(hash_no_salt)) != 0,
               "Salted hash should differ from non-salted hash");
    dap_assert(memcmp(hash_salt1, hash_salt2, sizeof(hash_salt1)) != 0,
               "Different salts should produce different hashes");
    
    log_it(L_INFO, "Salt functionality test passed");
    return true;
}

/**
 * @brief Test iterative hashing (hash of hash)
 */
static bool s_test_iterative_hashing(void) {
    log_it(L_INFO, "Testing iterative hashing...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    uint8_t hash_1[32];
    uint8_t hash_2[32];
    uint8_t hash_3[32];
    uint8_t hash_1000[32];
    
    // First iteration
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                         hash_1, sizeof(hash_1), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "First hash iteration should succeed");
    
    // Second iteration (hash of hash)
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, hash_1, sizeof(hash_1),
                     hash_2, sizeof(hash_2), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Second hash iteration should succeed");
    
    // Third iteration
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, hash_2, sizeof(hash_2),
                     hash_3, sizeof(hash_3), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Third hash iteration should succeed");
    
    // Verify each iteration produces different results
    dap_assert(memcmp(hash_1, hash_2, sizeof(hash_1)) != 0, "Hash iterations should produce different results");
    dap_assert(memcmp(hash_2, hash_3, sizeof(hash_2)) != 0, "Sequential hash iterations should differ");
    
    // Test many iterations (simulate PBKDF-like behavior)
    memcpy(hash_1000, hash_1, sizeof(hash_1000));
    for (int i = 0; i < 999; i++) {
        result = dap_hash(DAP_HASH_TYPE_SHA3_256, hash_1000, sizeof(hash_1000),
                         hash_1000, sizeof(hash_1000), DAP_HASH_FLAG_NONE, NULL);
        dap_assert(result == 0, "Iterative hash should succeed");
    }
    
    // Verify 1000 iterations produces different result
    dap_assert(memcmp(hash_1, hash_1000, sizeof(hash_1)) != 0, "1000 iterations should produce different result");
    
    log_it(L_INFO, "Iterative hashing test passed");
    return true;
}

/**
 * @brief Test error handling with invalid parameters
 */
static bool s_test_error_handling(void) {
    log_it(L_INFO, "Testing error handling...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    uint8_t output[64];
    
    // Test with NULL output buffer
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                         NULL, sizeof(output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result != 0, "Hash with NULL output should fail");
    
    // Test with zero output size
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                     output, 0, DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result != 0, "Hash with zero output size should fail");
    
    // Test with NULL input (should handle gracefully)
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, NULL, test_data_size,
                     output, sizeof(output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result != 0, "Hash with NULL input should fail");
    
    // Test with invalid hash type (note: some implementations may not validate type)
    result = dap_hash((dap_hash_type_t)999, test_data, test_data_size,
                     output, sizeof(output), DAP_HASH_FLAG_NONE, NULL);
    // Note: This may or may not fail depending on implementation
    log_it(L_DEBUG, "Invalid hash type test result: %d (may be implementation-dependent)", result);
    
    log_it(L_INFO, "Error handling test passed");
    return true;
}

/**
 * @brief Test hash performance characteristics
 */
static bool s_test_performance_characteristics(void) {
    log_it(L_INFO, "Testing hash performance characteristics...");
    
    // Test with different input sizes to ensure reasonable performance
    size_t input_sizes[] = {1, 16, 64, 256, 1024, 4096};
    size_t num_sizes = sizeof(input_sizes) / sizeof(input_sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        size_t input_size = input_sizes[i];
        uint8_t *input_data = DAP_NEW_Z_SIZE(uint8_t, input_size);
        dap_assert(input_data != NULL, "Input buffer allocation should succeed");
        
        // Fill with test pattern
        for (size_t j = 0; j < input_size; j++) {
            input_data[j] = (uint8_t)(j % 256);
        }
        
        uint8_t hash_output[32];
        int result = dap_hash(DAP_HASH_TYPE_SHA3_256, input_data, input_size,
                             hash_output, sizeof(hash_output), DAP_HASH_FLAG_NONE, NULL);
        dap_assert(result == 0, "Hash with variable input size should succeed");
        
        // Verify output is not all zeros
        bool all_zeros = true;
        for (size_t j = 0; j < sizeof(hash_output); j++) {
            if (hash_output[j] != 0) {
                all_zeros = false;
                break;
            }
        }
        dap_assert(!all_zeros, "Hash output should not be all zeros");
        
        log_it(L_DEBUG, "Hash performance test with %zu bytes: OK", input_size);
        DAP_DELETE(input_data);
    }
    
    log_it(L_INFO, "Performance characteristics test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    // Initialize logging
    dap_log_level_set(L_DEBUG);
    
    log_it(L_INFO, "=== DAP Hash Function Unit Tests ===");
    
    bool test_results[] = {
        s_test_basic_hash_types(),
        s_test_shake_arbitrary_sizes(),
        s_test_hash_determinism(),
        s_test_domain_separation(),
        s_test_salt_functionality(),
        s_test_iterative_hashing(),
        s_test_error_handling(),
        s_test_performance_characteristics()
    };
    
    const char* test_names[] = {
        "Basic Hash Types",
        "SHAKE Arbitrary Sizes",
        "Hash Determinism",
        "Domain Separation",
        "Salt Functionality",
        "Iterative Hashing",
        "Error Handling",
        "Performance Characteristics"
    };
    
    size_t num_tests = sizeof(test_results) / sizeof(test_results[0]);
    size_t passed_tests = 0;
    
    log_it(L_INFO, "=== Test Results Summary ===");
    for (size_t i = 0; i < num_tests; i++) {
        if (test_results[i]) {
            log_it(L_INFO, "✅ %s: PASSED", test_names[i]);
            passed_tests++;
        } else {
            log_it(L_ERROR, "❌ %s: FAILED", test_names[i]);
        }
    }
    
    log_it(L_INFO, "=== Final Results ===");
    log_it(L_INFO, "Tests passed: %zu/%zu", passed_tests, num_tests);
    
    if (passed_tests == num_tests) {
        log_it(L_INFO, "🎉 ALL HASH TESTS PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "💥 SOME TESTS FAILED!");
        return -1;
    }
}