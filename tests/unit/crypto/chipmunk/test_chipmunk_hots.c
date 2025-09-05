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
 * @file test_chipmunk_hots.c
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
#include "dap_enc_chipmunk.h"

// Chipmunk includes
#include "chipmunk_hots.h"
#include "chipmunk.h"

#define LOG_TAG "chipmunk_hots_test"

// Test message
static const char *TEST_MESSAGE = "Hello, Chipmunk HOTS!";

/**
 * @brief Test basic HOTS functionality
 */
static int test_hots_basic(void) {
    log_it(L_INFO, "Setting up HOTS parameters...");

    chipmunk_hots_params_t l_params;
    int l_result = chipmunk_hots_setup(&l_params);
    if (l_result != 0) {
        log_it(L_ERROR, "❌ HOTS setup failed with code %d", l_result);
        return -1;
    }
    log_it(L_INFO, "✓ HOTS setup successful");

    // Generate keys
    log_it(L_INFO, "Generating HOTS keys...");
    uint8_t l_seed[32];
    // Use fixed seed for reproducible results
    memset(l_seed, 0x42, 32);

    chipmunk_hots_pk_t l_pk;
    chipmunk_hots_sk_t l_sk;

    l_result = chipmunk_hots_keygen(l_seed, 0, &l_params, &l_pk, &l_sk);
    if (l_result != 0) {
        log_it(L_ERROR, "❌ HOTS keygen failed with code %d", l_result);
        return -1;
    }
    log_it(L_INFO, "✓ HOTS key generation successful");

    // Print some debug info about keys
    debug_if(true, L_DEBUG, "Debug: pk.v0 first coeffs: %d %d %d %d",
           l_pk.v0.coeffs[0], l_pk.v0.coeffs[1], l_pk.v0.coeffs[2], l_pk.v0.coeffs[3]);
    debug_if(true, L_DEBUG, "Debug: pk.v1 first coeffs: %d %d %d %d",
           l_pk.v1.coeffs[0], l_pk.v1.coeffs[1], l_pk.v1.coeffs[2], l_pk.v1.coeffs[3]);

    // Sign message
    log_it(L_INFO, "Signing test message...");
    const char *l_test_message = "Hello, HOTS!";
    chipmunk_hots_signature_t l_signature;

    l_result = chipmunk_hots_sign(&l_sk, (const uint8_t*)l_test_message,
                                  strlen(l_test_message), &l_signature);
    if (l_result != 0) {
        log_it(L_ERROR, "❌ HOTS signing failed with code %d", l_result);
        return -1;
    }
    log_it(L_INFO, "✓ HOTS signing successful");

    // Print signature debug info
    debug_if(true, L_DEBUG, "Debug: signature[0] first coeffs: %d %d %d %d",
           l_signature.sigma[0].coeffs[0], l_signature.sigma[0].coeffs[1],
           l_signature.sigma[0].coeffs[2], l_signature.sigma[0].coeffs[3]);

    // Verify signature
    log_it(L_INFO, "Verifying signature...");
    l_result = chipmunk_hots_verify(&l_pk, (const uint8_t*)l_test_message,
                                   strlen(l_test_message), &l_signature, &l_params);
    debug_if(true, L_DEBUG, "Verification result: %d", l_result);

    if (l_result == 0) {
        log_it(L_INFO, "✓ HOTS verification successful");
        return 0;
    } else {
        log_it(L_ERROR, "❌ HOTS verification failed with error code %d", l_result);
        return -1;
    }
}

/**
 * @brief Test multiple HOTS keys
 */
static int test_hots_multiple_keys(void) {
    log_it(L_INFO, "Testing multiple HOTS keys...");

    // Setup parameters
    chipmunk_hots_params_t l_params;
    if (chipmunk_hots_setup(&l_params) != 0) {
        log_it(L_ERROR, "❌ HOTS setup failed");
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
            log_it(L_ERROR, "❌ HOTS key generation failed for counter %u", l_counter);
            return -1;
        }

        chipmunk_hots_signature_t l_signature;
        if (chipmunk_hots_sign(&l_sk, (const uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), &l_signature) != 0) {
            log_it(L_ERROR, "❌ HOTS signing failed for counter %u", l_counter);
            return -1;
        }

        int l_verify_result = chipmunk_hots_verify(&l_pk, (const uint8_t*)TEST_MESSAGE, strlen(TEST_MESSAGE), &l_signature, &l_params);
        if (l_verify_result != 0) {
            log_it(L_ERROR, "❌ HOTS verification failed for counter %u", l_counter);
            return -1;
        }
    }

    log_it(L_INFO, "✓ Multiple HOTS keys test successful");
    return 0;
}

/**
 * @brief Main test function
 */
int main() {
    // Initialize logging with clean format for unit tests
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);  // Clean output without timestamps/modules

    // Initialize Chipmunk module
    dap_enc_chipmunk_init();

    log_it(L_NOTICE, "🔬 CHIPMUNK HOTS UNIT TESTS");
    log_it(L_NOTICE, "Homomorphic One-Time Signatures verification");
    log_it(L_NOTICE, " ");

    // Initialize DAP with fixed parameters instead of random
    if (dap_common_init("chipmunk-hots-test", NULL) != 0) {
        log_it(L_ERROR, "❌ DAP initialization failed");
        return 1;
    }

    int l_tests_passed = 0;
    int l_total_tests = 0;

    // Test 1: Basic HOTS functionality
    l_total_tests++;
    log_it(L_INFO, "Testing basic HOTS functionality...");

    if (test_hots_basic() == 0) {
        log_it(L_NOTICE, "✓ Basic HOTS test passed");
        l_tests_passed++;
    } else {
        log_it(L_ERROR, "❌ Basic HOTS test failed");
    }

    // Test 2: Multiple HOTS keys
    l_total_tests++;
    log_it(L_INFO, " ");
    log_it(L_INFO, "Testing multiple HOTS keys...");

    if (test_hots_multiple_keys() == 0) {
        log_it(L_NOTICE, "✓ Multiple keys HOTS test passed");
        l_tests_passed++;
    } else {
        log_it(L_ERROR, "❌ Multiple keys HOTS test failed");
    }

    // Print summary
    log_it(L_NOTICE, " ");
    log_it(L_NOTICE, "=== TEST SUMMARY ===");
    log_it(L_NOTICE, "Tests passed: %d/%d", l_tests_passed, l_total_tests);

    if (l_tests_passed == l_total_tests) {
        log_it(L_NOTICE, "🎉 ALL HOTS TESTS PASSED! 🎉");
        return 0;
    } else {
        log_it(L_ERROR, "💥 SOME HOTS TESTS FAILED! 💥");
        return 1;
    }
}
