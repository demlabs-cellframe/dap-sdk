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
#include "dap_hash.h"
#include "../../../fixtures/utilities/test_helpers.h"
#include "../../../fixtures/json_samples.h"

#define LOG_TAG "test_sha3"

/**
 * @brief Test SHA3-256 basic functionality
 */
static bool s_test_sha3_256_basic(void) {
    log_it(L_DEBUG, "Testing SHA3-256 basic functionality");
    
    const char* l_input = CRYPTO_SAMPLE_HASH_INPUT;
    size_t l_input_size = strlen(l_input);
    dap_hash_fast_t l_hash = {0};
    
    // Test hash calculation
    bool l_ret = dap_hash_fast(l_input, l_input_size, &l_hash);
    DAP_TEST_ASSERT(l_ret == true, "SHA3-256 hash calculation should succeed");
    
    // Verify hash is not all zeros
    bool l_non_zero = false;
    for (size_t i = 0; i < sizeof(l_hash.raw); i++) {
        if (l_hash.raw[i] != 0) {
            l_non_zero = true;
            break;
        }
    }
    DAP_TEST_ASSERT(l_non_zero, "Hash should not be all zeros");
    
    log_it(L_DEBUG, "SHA3-256 basic test passed");
    return true;
}

/**
 * @brief Test SHA3-256 consistency
 */
static bool s_test_sha3_256_consistency(void) {
    log_it(L_DEBUG, "Testing SHA3-256 consistency");
    
    const char* l_input = "DAP SDK consistent hash test";
    size_t l_input_size = strlen(l_input);
    dap_hash_fast_t l_hash1 = {0};
    dap_hash_fast_t l_hash2 = {0};
    
    // Calculate hash twice
    bool l_ret1 = dap_hash_fast(l_input, l_input_size, &l_hash1);
    bool l_ret2 = dap_hash_fast(l_input, l_input_size, &l_hash2);
    
    DAP_TEST_ASSERT(l_ret1 == true, "First hash calculation should succeed");
    DAP_TEST_ASSERT(l_ret2 == true, "Second hash calculation should succeed");
    
    // Verify hashes are identical
    int l_cmp = memcmp(&l_hash1, &l_hash2, sizeof(dap_hash_fast_t));
    DAP_TEST_ASSERT(l_cmp == 0, "Consistent input should produce identical hashes");
    
    log_it(L_DEBUG, "SHA3-256 consistency test passed");
    return true;
}

/**
 * @brief Test SHA3-256 with empty input
 */
static bool s_test_sha3_256_empty(void) {
    log_it(L_DEBUG, "Testing SHA3-256 with empty input");
    
    dap_hash_fast_t l_hash = {0};
    
    // Test with empty string (NULL pointer with size 0)
    bool l_ret = dap_hash_fast(NULL, 0, &l_hash);
    DAP_TEST_ASSERT(l_ret == true, "Hash of empty string should succeed");
    
    log_it(L_DEBUG, "SHA3-256 empty input test passed");
    return true;
}

/**
 * @brief Test SHA3-256 performance
 */
static bool s_test_sha3_256_performance(void) {
    log_it(L_DEBUG, "Testing SHA3-256 performance");
    
    const size_t l_iterations = 1000;
    const char* l_input = CRYPTO_SAMPLE_HASH_INPUT;
    size_t l_input_size = strlen(l_input);
    dap_hash_fast_t l_hash = {0};
    
    dap_test_timer_t l_timer;
    dap_test_timer_start(&l_timer);
    
    for (size_t i = 0; i < l_iterations; i++) {
        bool l_ret = dap_hash_fast(l_input, l_input_size, &l_hash);
        DAP_TEST_ASSERT(l_ret == true, "Hash calculation should succeed in performance test");
    }
    
    uint64_t l_elapsed = dap_test_timer_stop(&l_timer);
    double l_hashes_per_sec = (double)l_iterations / (l_elapsed / 1000000.0);
    
    log_it(L_INFO, "SHA3-256 performance: %.2f hashes/sec (%lu iterations in %lu us)", 
           l_hashes_per_sec, l_iterations, l_elapsed);
    
    // Basic performance threshold (should be able to do at least 100 hashes/sec)
    DAP_TEST_ASSERT(l_hashes_per_sec > 100.0, "SHA3-256 should achieve reasonable performance");
    
    log_it(L_DEBUG, "SHA3-256 performance test passed");
    return true;
}

/**
 * @brief Main test function for SHA3
 */
int main(void) {
    log_it(L_INFO, "Starting SHA3-256 unit tests");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_test_sha3_256_basic();
    l_all_passed &= s_test_sha3_256_consistency();
    l_all_passed &= s_test_sha3_256_empty();
    l_all_passed &= s_test_sha3_256_performance();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All SHA3-256 tests passed!");
        return 0;
    } else {
        log_it(L_ERROR, "Some SHA3-256 tests failed!");
        return -1;
    }
}

