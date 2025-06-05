/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file chipmunk_hots_test.c
 * @brief Test suite for HOTS (Homomorphic One-Time Signatures) implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

// DAP includes
#include "dap_common.h"
#include "dap_strfuncs.h"

// Chipmunk includes
#include "chipmunk_hots.h"
#include "chipmunk.h"

// Test message
static const char *TEST_MESSAGE = "Hello, Chipmunk HOTS!";

/**
 * @brief Test basic HOTS functionality
 */
static int test_hots_basic(void) {
    printf("Setting up HOTS parameters...\n");
    
    chipmunk_hots_params_t l_params;
    int l_result = chipmunk_hots_setup(&l_params);
    if (l_result != 0) {
        printf("❌ HOTS setup failed with code %d\n", l_result);
        return -1;
    }
    printf("✓ HOTS setup successful\n");
    
    // Generate keys
    printf("Generating HOTS keys...\n");
    uint8_t l_seed[32];
    // Use fixed seed for reproducible results
    memset(l_seed, 0x42, 32);
    
    chipmunk_hots_pk_t l_pk;
    chipmunk_hots_sk_t l_sk;
    
    l_result = chipmunk_hots_keygen(l_seed, 0, &l_params, &l_pk, &l_sk);
    if (l_result != 0) {
        printf("❌ HOTS keygen failed with code %d\n", l_result);
        return -1;
    }
    printf("✓ HOTS key generation successful\n");
    
    // Print some debug info about keys
    printf("Debug: pk.v0 first coeffs: %d %d %d %d\n", 
           l_pk.v0.coeffs[0], l_pk.v0.coeffs[1], l_pk.v0.coeffs[2], l_pk.v0.coeffs[3]);
    printf("Debug: pk.v1 first coeffs: %d %d %d %d\n", 
           l_pk.v1.coeffs[0], l_pk.v1.coeffs[1], l_pk.v1.coeffs[2], l_pk.v1.coeffs[3]);
    
    // Sign message
    printf("Signing test message...\n");
    const char *l_test_message = "Hello, HOTS!";
    chipmunk_hots_signature_t l_signature;
    
    l_result = chipmunk_hots_sign(&l_sk, (const uint8_t*)l_test_message, 
                                  strlen(l_test_message), &l_signature);
    if (l_result != 0) {
        printf("❌ HOTS signing failed with code %d\n", l_result);
        return -1;
    }
    printf("✓ HOTS signing successful\n");
    
    // Print signature debug info
    printf("Debug: signature[0] first coeffs: %d %d %d %d\n", 
           l_signature.sigma[0].coeffs[0], l_signature.sigma[0].coeffs[1], 
           l_signature.sigma[0].coeffs[2], l_signature.sigma[0].coeffs[3]);
    
    // Verify signature
    printf("Verifying signature...\n");
    l_result = chipmunk_hots_verify(&l_pk, (const uint8_t*)l_test_message, 
                                   strlen(l_test_message), &l_signature, &l_params);
    printf("Verification result: %d\n", l_result);
    
    if (l_result == 1) {
        printf("✓ HOTS verification successful\n");
        return 0;
    } else if (l_result == 0) {
        printf("❌ HOTS verification failed - signature invalid\n");
        return -1;
    } else {
        printf("❌ HOTS verification failed with error code %d\n", l_result);
        return -1;
    }
}

/**
 * @brief Test multiple HOTS keys
 */
static int test_hots_multiple_keys(void) {
    printf("Testing multiple HOTS keys...\n");
    
    // Setup parameters
    chipmunk_hots_params_t l_params;
    if (chipmunk_hots_setup(&l_params) != 0) {
        printf("❌ HOTS setup failed\n");
        return -1;
    }
    
    uint8_t l_seed[32];
    for (int i = 0; i < 32; i++) {
        l_seed[i] = (uint8_t)(rand() % 256);
    }
    
    // Test multiple key pairs with different counters
    for (uint32_t l_counter = 0; l_counter < 5; l_counter++) {
        chipmunk_hots_pk_t l_pk;
        chipmunk_hots_sk_t l_sk;
        
        if (chipmunk_hots_keygen(l_seed, l_counter, &l_params, &l_pk, &l_sk) != 0) {
            printf("❌ HOTS key generation failed for counter %u\n", l_counter);
            return -1;
        }
        
        chipmunk_hots_signature_t l_signature;
        if (chipmunk_hots_sign(&l_sk, (const uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), &l_signature) != 0) {
            printf("❌ HOTS signing failed for counter %u\n", l_counter);
            return -1;
        }
        
        int l_verify_result = chipmunk_hots_verify(&l_pk, (const uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), &l_signature, &l_params);
        if (l_verify_result != 1) {
            printf("❌ HOTS verification failed for counter %u\n", l_counter);
            return -1;
        }
    }
    
    printf("✓ Multiple HOTS keys test successful\n");
    return 0;
}

/**
 * @brief Main test function
 */
int main() {
    printf("=== CHIPMUNK HOTS TEST ===\n\n");
    
    // Initialize DAP with fixed parameters instead of random
    if (dap_common_init("chipmunk-hots-test", NULL) != 0) {
        printf("❌ DAP initialization failed\n");
        return 1;
    }
    
    int l_tests_passed = 0;
    int l_total_tests = 0;
    
    // Test 1: Basic HOTS functionality
    l_total_tests++;
    printf("Testing basic HOTS functionality...\n");
    
    if (test_hots_basic() == 0) {
        printf("✓ Basic HOTS test passed\n");
        l_tests_passed++;
    } else {
        printf("❌ Basic HOTS test failed\n");
    }
    
    // Test 2: Multiple HOTS keys
    l_total_tests++;
    printf("\nTesting multiple HOTS keys...\n");
    
    if (test_hots_multiple_keys() == 0) {
        printf("✓ Multiple keys HOTS test passed\n");
        l_tests_passed++;
    } else {
        printf("❌ Multiple keys HOTS test failed\n");
    }
    
    // Print summary
    printf("\n=== TEST SUMMARY ===\n");
    printf("Tests passed: %d/%d\n", l_tests_passed, l_total_tests);
    
    if (l_tests_passed == l_total_tests) {
        printf("🎉 ALL HOTS TESTS PASSED! 🎉\n");
        return 0;
    } else {
        printf("💥 SOME HOTS TESTS FAILED! 💥\n");
        return 1;
    }
} 