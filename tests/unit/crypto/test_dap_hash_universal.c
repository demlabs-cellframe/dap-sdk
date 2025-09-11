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
#include "dap_enc_chipmunk_ring_params.h"
#include <string.h>
#include <stdio.h>

#define LOG_TAG "test_dap_hash_universal"

// Test message
#define TEST_MESSAGE "ChipmunkRing Universal Hash Test Message"
#define TEST_SALT "TestSalt123"

/**
 * @brief Test basic hash types and sizes
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
    
    // Verify different hash sizes produce different outputs
    dap_assert(memcmp(hash_256, hash_384, 32) != 0, "Different hash types should produce different outputs");
    dap_assert(memcmp(hash_256, hash_512, 32) != 0, "SHA3-256 and SHA3-512 should differ");
    
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
    size_t test_sizes[] = {16, 32, 64, 96, 128};
    size_t num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
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
        
        log_it(L_DEBUG, "SHAKE-128 with %zu bytes output: OK", output_size);
        DAP_DELETE(output);
    }
    
    log_it(L_INFO, "SHAKE arbitrary sizes test passed");
    return true;
}

/**
 * @brief Test domain separation functionality
 */
static bool s_test_domain_separation(void) {
    log_it(L_INFO, "Testing domain separation functionality...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Hash without domain separation
    uint8_t hash_no_domain[64];
    int result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                         hash_no_domain, sizeof(hash_no_domain), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash without domain separation should succeed");
    
    // Hash with default domain separation
    uint8_t hash_with_domain[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_with_domain, sizeof(hash_with_domain), 
                     DAP_HASH_FLAG_DOMAIN_SEPARATION, NULL);
    dap_assert(result == 0, "Hash with domain separation should succeed");
    
    // Verify domain separation changes the output
    dap_assert(memcmp(hash_no_domain, hash_with_domain, sizeof(hash_no_domain)) != 0,
               "Domain separation should change hash output");
    
    // Test custom domain separator
    dap_hash_params_t custom_params = {
        .salt = NULL,
        .salt_size = 0,
        .domain_separator = "CustomDomain",
        .iterations = 0,
        .security_level = 256
    };
    
    uint8_t hash_custom_domain[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_custom_domain, sizeof(hash_custom_domain),
                     DAP_HASH_FLAG_DOMAIN_SEPARATION, &custom_params);
    dap_assert(result == 0, "Hash with custom domain should succeed");
    
    // Verify custom domain produces different output
    dap_assert(memcmp(hash_with_domain, hash_custom_domain, sizeof(hash_with_domain)) != 0,
               "Custom domain separator should change hash output");
    
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
    const char *test_salt = TEST_SALT;
    size_t test_salt_size = strlen(test_salt);
    
    // Hash without salt
    uint8_t hash_no_salt[64];
    int result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                         hash_no_salt, sizeof(hash_no_salt), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Hash without salt should succeed");
    
    // Hash with salt
    dap_hash_params_t salt_params = {
        .salt = (const uint8_t*)test_salt,
        .salt_size = test_salt_size,
        .domain_separator = NULL,
        .iterations = 0,
        .security_level = 256
    };
    
    uint8_t hash_with_salt[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_with_salt, sizeof(hash_with_salt),
                     DAP_HASH_FLAG_SALT, &salt_params);
    dap_assert(result == 0, "Hash with salt should succeed");
    
    // Verify salt changes the output
    dap_assert(memcmp(hash_no_salt, hash_with_salt, sizeof(hash_no_salt)) != 0,
               "Salt should change hash output");
    
    // Test different salts produce different outputs
    const char *different_salt = "DifferentSalt456";
    dap_hash_params_t different_salt_params = {
        .salt = (const uint8_t*)different_salt,
        .salt_size = strlen(different_salt),
        .domain_separator = NULL,
        .iterations = 0,
        .security_level = 256
    };
    
    uint8_t hash_different_salt[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_different_salt, sizeof(hash_different_salt),
                     DAP_HASH_FLAG_SALT, &different_salt_params);
    dap_assert(result == 0, "Hash with different salt should succeed");
    
    dap_assert(memcmp(hash_with_salt, hash_different_salt, sizeof(hash_with_salt)) != 0,
               "Different salts should produce different outputs");
    
    log_it(L_INFO, "Salt functionality test passed");
    return true;
}

/**
 * @brief Test iterative hashing
 */
static bool s_test_iterative_hashing(void) {
    log_it(L_INFO, "Testing iterative hashing...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Single iteration
    uint8_t hash_single[64];
    int result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                         hash_single, sizeof(hash_single), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result == 0, "Single iteration hash should succeed");
    
    // Multiple iterations (small number for test speed)
    dap_hash_params_t iter_params = {
        .salt = NULL,
        .salt_size = 0,
        .domain_separator = NULL,
        .iterations = 10,  // Small number for testing
        .security_level = 256
    };
    
    uint8_t hash_multiple[64];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     hash_multiple, sizeof(hash_multiple),
                     DAP_HASH_FLAG_ITERATIVE, &iter_params);
    dap_assert(result == 0, "Iterative hash should succeed");
    
    // Verify iterative hashing changes output
    dap_assert(memcmp(hash_single, hash_multiple, sizeof(hash_single)) != 0,
               "Iterative hashing should change output");
    
    log_it(L_INFO, "Iterative hashing test passed");
    return true;
}

/**
 * @brief Test ZK proof generation for ChipmunkRing
 */
static bool s_test_zk_proof_generation(void) {
    log_it(L_INFO, "Testing ZK proof generation for ChipmunkRing...");
    
    const char *test_data = "ZK Proof Test Data";
    size_t test_data_size = strlen(test_data);
    
    // Test different ZK proof sizes
    size_t proof_sizes[] = {32, 64, 96, 128};
    size_t num_sizes = sizeof(proof_sizes) / sizeof(proof_sizes[0]);
    
    for (size_t i = 0; i < num_sizes; i++) {
        size_t proof_size = proof_sizes[i];
        uint8_t *zk_proof = DAP_NEW_Z_SIZE(uint8_t, proof_size);
        dap_assert(zk_proof != NULL, "ZK proof buffer allocation should succeed");
        
        int result = chipmunk_ring_generate_zk_proof((const uint8_t*)test_data, test_data_size,
                                                    proof_size, zk_proof);
        dap_assert(result == 0, "ZK proof generation should succeed");
        
        // Verify proof is not all zeros
        bool all_zeros = true;
        for (size_t j = 0; j < proof_size; j++) {
            if (zk_proof[j] != 0) {
                all_zeros = false;
                break;
            }
        }
        dap_assert(!all_zeros, "ZK proof should not be all zeros");
        
        log_it(L_DEBUG, "ZK proof generation (%zu bytes): OK", proof_size);
        DAP_DELETE(zk_proof);
    }
    
    log_it(L_INFO, "ZK proof generation test passed");
    return true;
}

/**
 * @brief Test enterprise ZK proof generation
 */
static bool s_test_enterprise_zk_proof(void) {
    log_it(L_INFO, "Testing enterprise ZK proof generation...");
    
    const char *test_data = "Enterprise ZK Test";
    size_t test_data_size = strlen(test_data);
    const char *test_salt = "EnterpriseSalt";
    size_t test_salt_size = strlen(test_salt);
    
    // Generate standard ZK proof
    uint8_t standard_proof[64];
    int result = chipmunk_ring_generate_zk_proof((const uint8_t*)test_data, test_data_size,
                                                sizeof(standard_proof), standard_proof);
    dap_assert(result == 0, "Standard ZK proof should succeed");
    
    // Generate enterprise ZK proof with salt and iterations
    uint8_t enterprise_proof[64];
    result = chipmunk_ring_generate_zk_proof_enterprise((const uint8_t*)test_data, test_data_size,
                                                       sizeof(enterprise_proof),
                                                       (const uint8_t*)test_salt, test_salt_size,
                                                       5, // Small number of iterations for test
                                                       enterprise_proof);
    dap_assert(result == 0, "Enterprise ZK proof should succeed");
    
    // Verify enterprise proof is different from standard
    dap_assert(memcmp(standard_proof, enterprise_proof, sizeof(standard_proof)) != 0,
               "Enterprise ZK proof should differ from standard");
    
    // Test with different iteration counts
    uint8_t enterprise_proof_10[64];
    result = chipmunk_ring_generate_zk_proof_enterprise((const uint8_t*)test_data, test_data_size,
                                                       sizeof(enterprise_proof_10),
                                                       (const uint8_t*)test_salt, test_salt_size,
                                                       10, // Different iteration count
                                                       enterprise_proof_10);
    dap_assert(result == 0, "Enterprise ZK proof with 10 iterations should succeed");
    
    // Different iteration counts should produce different results
    dap_assert(memcmp(enterprise_proof, enterprise_proof_10, sizeof(enterprise_proof)) != 0,
               "Different iteration counts should produce different proofs");
    
    log_it(L_INFO, "Enterprise ZK proof test passed");
    return true;
}

/**
 * @brief Test error handling
 */
static bool s_test_error_handling(void) {
    log_it(L_INFO, "Testing error handling...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    uint8_t output[64];
    
    // Test NULL output
    int result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                         NULL, sizeof(output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result < 0, "NULL output should fail");
    
    // Test zero output size
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, test_data, test_data_size,
                     output, 0, DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result < 0, "Zero output size should fail");
    
    // Test NULL input with non-zero size
    result = dap_hash(DAP_HASH_TYPE_SHA3_256, NULL, test_data_size,
                     output, sizeof(output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result < 0, "NULL input with non-zero size should fail");
    
    // Test buffer too small for hash type
    uint8_t small_output[16];
    result = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                     small_output, sizeof(small_output), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result < 0, "Buffer too small for SHA3-512 should fail");
    
    log_it(L_INFO, "Error handling test passed");
    return true;
}

/**
 * @brief Test reproducibility and consistency
 */
static bool s_test_reproducibility(void) {
    log_it(L_INFO, "Testing hash reproducibility and consistency...");
    
    const char *test_data = TEST_MESSAGE;
    size_t test_data_size = strlen(test_data);
    
    // Generate same hash twice
    uint8_t hash1[64], hash2[64];
    
    int result1 = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                          hash1, sizeof(hash1), DAP_HASH_FLAG_NONE, NULL);
    int result2 = dap_hash(DAP_HASH_TYPE_SHA3_512, test_data, test_data_size,
                          hash2, sizeof(hash2), DAP_HASH_FLAG_NONE, NULL);
    
    dap_assert(result1 == 0 && result2 == 0, "Both hash operations should succeed");
    dap_assert(memcmp(hash1, hash2, sizeof(hash1)) == 0, "Same input should produce same hash");
    
    // Test with same parameters but different data
    const char *different_data = "Different Test Data";
    uint8_t hash_different[64];
    
    int result3 = dap_hash(DAP_HASH_TYPE_SHA3_512, different_data, strlen(different_data),
                          hash_different, sizeof(hash_different), DAP_HASH_FLAG_NONE, NULL);
    dap_assert(result3 == 0, "Hash with different data should succeed");
    dap_assert(memcmp(hash1, hash_different, sizeof(hash1)) != 0,
               "Different input should produce different hash");
    
    log_it(L_INFO, "Reproducibility test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    dap_test_init("test_dap_hash_universal", argc, argv);
    
    log_it(L_INFO, "=== DAP Universal Hash Function Tests ===");
    
    bool test_results[] = {
        s_test_basic_hash_types(),
        s_test_shake_arbitrary_sizes(),
        s_test_domain_separation(),
        s_test_salt_functionality(),
        s_test_iterative_hashing(),
        s_test_zk_proof_generation(),
        s_test_enterprise_zk_proof(),
        s_test_error_handling(),
        s_test_reproducibility()
    };
    
    size_t num_tests = sizeof(test_results) / sizeof(test_results[0]);
    size_t passed_tests = 0;
    
    for (size_t i = 0; i < num_tests; i++) {
        if (test_results[i]) {
            passed_tests++;
        }
    }
    
    log_it(L_INFO, "=== Test Results: %zu/%zu tests passed ===", passed_tests, num_tests);
    
    if (passed_tests == num_tests) {
        log_it(L_INFO, "All DAP universal hash tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some DAP universal hash tests FAILED");
        return 1;
    }
}
