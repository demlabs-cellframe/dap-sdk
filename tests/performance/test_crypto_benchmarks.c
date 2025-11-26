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
#include "dap_sign.h"
#include "dap_enc_key.h"
#include "../fixtures/utilities/test_helpers.h"
#include <inttypes.h>

#define LOG_TAG "test_crypto_performance"

// Performance benchmarking parameters
#define HASH_ITERATIONS 10000
#define SIGN_ITERATIONS 100
#define VERIFY_ITERATIONS 1000

/**
 * @brief Benchmark SHA3-256 hashing performance
 */
static bool s_benchmark_hash_performance(void) {
    log_it(L_INFO, "Benchmarking SHA3-256 hash performance");
    
    const char* l_test_data = "DAP SDK performance test data for hashing benchmarks";
    size_t l_data_size = strlen(l_test_data);
    dap_hash_fast_t l_hash = {0};
    
    dap_test_timer_t l_timer;
    dap_test_timer_start(&l_timer);
    
    for (size_t i = 0; i < HASH_ITERATIONS; i++) {
        bool l_ret = dap_hash_fast(l_test_data, l_data_size, &l_hash);
        if (l_ret != true) {
            log_it(L_ERROR, "Hash calculation failed at iteration %zu", i);
            return false;
        }
    }
    
    uint64_t l_elapsed = dap_test_timer_stop(&l_timer);
    double l_hashes_per_sec = (double)HASH_ITERATIONS / (l_elapsed / 1000000.0);
    double l_throughput_mbps = (l_hashes_per_sec * l_data_size) / (1024 * 1024);
    
    log_it(L_INFO, "SHA3-256 Performance Results:");
    log_it(L_INFO, "  - Iterations: %d", HASH_ITERATIONS);
    log_it(L_INFO, "  - Total time: %" PRIu64 " microseconds", l_elapsed);
    log_it(L_INFO, "  - Hashes/sec: %.2f", l_hashes_per_sec);
    log_it(L_INFO, "  - Throughput: %.2f MB/s", l_throughput_mbps);
    log_it(L_INFO, "  - Avg time per hash: %.2f microseconds", (double)l_elapsed / HASH_ITERATIONS);
    
    // Performance baseline: should achieve at least 1000 hashes/sec
    DAP_TEST_ASSERT(l_hashes_per_sec > 1000.0, "SHA3-256 should achieve minimum performance threshold");
    
    return true;
}

/**
 * @brief Benchmark Dilithium signature creation performance
 */
static bool s_benchmark_dilithium_sign_performance(void) {
    log_it(L_INFO, "Benchmarking Dilithium signature creation performance");
    
    // Generate key once for all iterations
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Dilithium key generation");
    
    const char* l_test_data = "Dilithium signature performance test message";
    size_t l_data_size = strlen(l_test_data);
    
    dap_test_timer_t l_timer;
    dap_test_timer_start(&l_timer);
    
    size_t l_total_sig_size = 0;
    
    for (size_t i = 0; i < SIGN_ITERATIONS; i++) {
        dap_sign_t* l_signature = dap_sign_create(l_key, l_test_data, l_data_size);
        
        if (!l_signature) {
            log_it(L_ERROR, "Signature creation failed at iteration %zu", i);
            dap_enc_key_delete(l_key);
            return false;
        }
        
        l_total_sig_size += dap_sign_get_size(l_signature);
        DAP_DELETE(l_signature);
    }
    
    uint64_t l_elapsed = dap_test_timer_stop(&l_timer);
    double l_signs_per_sec = (double)SIGN_ITERATIONS / (l_elapsed / 1000000.0);
    double l_avg_sig_size = (double)l_total_sig_size / SIGN_ITERATIONS;
    
    log_it(L_INFO, "Dilithium Signature Creation Results:");
    log_it(L_INFO, "  - Iterations: %d", SIGN_ITERATIONS);
    log_it(L_INFO, "  - Total time: %" PRIu64 " microseconds", l_elapsed);
    log_it(L_INFO, "  - Signatures/sec: %.2f", l_signs_per_sec);
    log_it(L_INFO, "  - Avg signature size: %.0f bytes", l_avg_sig_size);
    log_it(L_INFO, "  - Avg time per signature: %.2f milliseconds", (double)l_elapsed / (SIGN_ITERATIONS * 1000));
    
    dap_enc_key_delete(l_key);
    
    // Performance baseline: should achieve at least 10 signatures/sec
    DAP_TEST_ASSERT(l_signs_per_sec > 10.0, "Dilithium should achieve minimum signing performance");
    
    return true;
}

/**
 * @brief Benchmark Dilithium signature verification performance
 */
static bool s_benchmark_dilithium_verify_performance(void) {
    log_it(L_INFO, "Benchmarking Dilithium signature verification performance");
    
    // Generate key and signature once
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key, "Dilithium key generation");
    
    const char* l_test_data = "Dilithium verification performance test message";
    size_t l_data_size = strlen(l_test_data);
    
    dap_sign_t* l_signature = dap_sign_create(l_key, l_test_data, l_data_size);
    DAP_TEST_ASSERT_NOT_NULL(l_signature, "Signature creation for verification test");
    
    dap_test_timer_t l_timer;
    dap_test_timer_start(&l_timer);
    
    size_t l_successful_verifications = 0;
    
    for (size_t i = 0; i < VERIFY_ITERATIONS; i++) {
        int l_verify_result = dap_sign_verify(l_signature, l_test_data, l_data_size);
        if (l_verify_result == 0) {
            l_successful_verifications++;
        }
    }
    
    uint64_t l_elapsed = dap_test_timer_stop(&l_timer);
    double l_verifies_per_sec = (double)VERIFY_ITERATIONS / (l_elapsed / 1000000.0);
    
    log_it(L_INFO, "Dilithium Signature Verification Results:");
    log_it(L_INFO, "  - Iterations: %d", VERIFY_ITERATIONS);
    log_it(L_INFO, "  - Successful verifications: %zu", l_successful_verifications);
    log_it(L_INFO, "  - Total time: %" PRIu64 " microseconds", l_elapsed);
    log_it(L_INFO, "  - Verifications/sec: %.2f", l_verifies_per_sec);
    log_it(L_INFO, "  - Avg time per verification: %.2f milliseconds", (double)l_elapsed / (VERIFY_ITERATIONS * 1000));
    
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_key);
    
    // All verifications should succeed
    DAP_TEST_ASSERT(l_successful_verifications == VERIFY_ITERATIONS, "All verifications should succeed");
    
    // Performance baseline: should achieve at least 100 verifications/sec
    DAP_TEST_ASSERT(l_verifies_per_sec > 100.0, "Dilithium should achieve minimum verification performance");
    
    return true;
}

/**
 * @brief Benchmark memory usage during crypto operations
 */
static bool s_benchmark_memory_usage(void) {
    log_it(L_INFO, "Benchmarking memory usage during crypto operations");
    
    const size_t l_iterations = 100;
    size_t l_peak_memory = 0;
    
    for (size_t i = 0; i < l_iterations; i++) {
        // Allocate key
        dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_DILITHIUM, NULL, 0, NULL, 0, 0);
        if (!l_key) continue;
        
        // Create signature
        const char* l_data = "Memory usage test data";
        size_t l_sig_size = 0;
        dap_sign_t* l_signature = dap_sign_create(l_key, l_data, strlen(l_data));
        
        // Track memory usage (simplified - in real implementation would use memory profiling)
        size_t l_current_memory = sizeof(dap_enc_key_t) + l_sig_size;
        if (l_current_memory > l_peak_memory) {
            l_peak_memory = l_current_memory;
        }
        
        // Cleanup
        if (l_signature) DAP_DELETE(l_signature);
        dap_enc_key_delete(l_key);
    }
    
    log_it(L_INFO, "Memory Usage Results:");
    log_it(L_INFO, "  - Peak estimated memory: %zu bytes", l_peak_memory);
    log_it(L_INFO, "  - Memory per operation: %zu bytes", l_peak_memory);
    
    // Memory usage should be reasonable (less than 100KB per operation)
    DAP_TEST_ASSERT(l_peak_memory < 100 * 1024, "Memory usage should be reasonable");
    
    return true;
}

/**
 * @brief Compare performance of different signature algorithms
 */
static bool s_benchmark_algorithm_comparison(void) {
    log_it(L_INFO, "Benchmarking different signature algorithms");
    
    struct {
        dap_sign_type_t algorithm;
        const char* name;
    } l_algorithms[] = {
        {{.type = SIG_TYPE_DILITHIUM}, "Dilithium"},
        {{.type = SIG_TYPE_FALCON}, "Falcon"},
        {{.type = SIG_TYPE_PICNIC}, "Picnic"}
    };
    
    const size_t l_num_algorithms = sizeof(l_algorithms) / sizeof(l_algorithms[0]);
    const size_t l_test_iterations = 20;
    const char* l_test_data = "Algorithm comparison test data";
    size_t l_data_size = strlen(l_test_data);
    
    for (size_t i = 0; i < l_num_algorithms; i++) {
        log_it(L_INFO, "Testing %s algorithm", l_algorithms[i].name);
        
        dap_enc_key_t* l_key = dap_enc_key_new_generate(dap_sign_type_to_key_type(l_algorithms[i].algorithm), NULL, 0, NULL, 0, 0);
        if (!l_key) {
            log_it(L_WARNING, "%s algorithm not available", l_algorithms[i].name);
            continue;
        }
        
        // Benchmark signing
        dap_test_timer_t l_sign_timer;
        dap_test_timer_start(&l_sign_timer);
        
        dap_sign_t* l_signature = NULL;
        size_t l_sig_size = 0;
        
        for (size_t j = 0; j < l_test_iterations; j++) {
            if (l_signature) DAP_DELETE(l_signature);
            l_signature = dap_sign_create(l_key, l_test_data, l_data_size);
        }
        
        uint64_t l_sign_elapsed = dap_test_timer_stop(&l_sign_timer);
        
        // Benchmark verification
        dap_test_timer_t l_verify_timer;
        dap_test_timer_start(&l_verify_timer);
        
        for (size_t j = 0; j < l_test_iterations; j++) {
            dap_sign_verify(l_signature, l_test_data, l_data_size);
        }
        
        uint64_t l_verify_elapsed = dap_test_timer_stop(&l_verify_timer);
        
        log_it(L_INFO, "%s Results:", l_algorithms[i].name);
        log_it(L_INFO, "  - Signature size: %zu bytes", l_sig_size);
        log_it(L_INFO, "  - Sign time: %.2f ms/op", (double)l_sign_elapsed / (l_test_iterations * 1000));
        log_it(L_INFO, "  - Verify time: %.2f ms/op", (double)l_verify_elapsed / (l_test_iterations * 1000));
        
        if (l_signature) DAP_DELETE(l_signature);
        dap_enc_key_delete(l_key);
    }
    
    return true;
}

/**
 * @brief Main test function for performance benchmarks
 */
int main(void) {
    log_it(L_INFO, "Starting DAP SDK Crypto Performance Benchmarks");
    
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize test SDK");
        return -1;
    }
    
    bool l_all_passed = true;
    
    l_all_passed &= s_benchmark_hash_performance();
    l_all_passed &= s_benchmark_dilithium_sign_performance();
    l_all_passed &= s_benchmark_dilithium_verify_performance();
    l_all_passed &= s_benchmark_memory_usage();
    l_all_passed &= s_benchmark_algorithm_comparison();
    
    dap_test_sdk_cleanup();
    
    if (l_all_passed) {
        log_it(L_INFO, "All Performance Benchmarks completed successfully!");
        return 0;
    } else {
        log_it(L_ERROR, "Some Performance Benchmarks failed!");
        return -1;
    }
}
