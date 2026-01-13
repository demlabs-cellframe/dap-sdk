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
 * @file dap_enc_chipmunk_test.h
 * @brief Comprehensive Chipmunk Post-Quantum Signature Test Suite
 * 
 * This test suite includes:
 * - Basic key generation and signature tests
 * - HOTS (Homomorphic One-Time Signatures) tests
 * - Deterministic key generation tests
 * - Multi-signature aggregation tests
 * - Batch verification tests
 * - Scalability tests (up to 30K signers)
 * - Performance benchmarks
 * - Stress tests
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test configuration constants
 */
#define CHIPMUNK_TEST_MAX_SIGNERS        30000   ///< Maximum signers for scalability tests
#define CHIPMUNK_TEST_BATCH_SIZE         100     ///< Default batch size for batch tests
#define CHIPMUNK_TEST_STRESS_ITERATIONS  1000    ///< Iterations for stress tests
#define CHIPMUNK_TEST_PERFORMANCE_RUNS   100     ///< Runs for performance benchmarks

/**
 * @brief Test result structure
 */
typedef struct {
    const char *test_name;          ///< Name of the test
    int result;                     ///< Test result (0 = success, <0 = failure)
    double execution_time_ms;       ///< Execution time in milliseconds
    size_t memory_used_bytes;       ///< Memory used during test
} chipmunk_test_result_t;

/**
 * @brief Test suite statistics
 */
typedef struct {
    int total_tests;                ///< Total number of tests run
    int passed_tests;               ///< Number of tests that passed
    int failed_tests;               ///< Number of tests that failed
    double total_time_ms;           ///< Total execution time
    size_t peak_memory_bytes;       ///< Peak memory usage
} chipmunk_test_suite_stats_t;

// =============================================================================
// MAIN TEST SUITE FUNCTIONS
// =============================================================================

/**
 * @brief Run all Chipmunk tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int dap_enc_chipmunk_tests_run(void);

/**
 * @brief Run basic functionality tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_basic_tests(void);

/**
 * @brief Run HOTS-specific tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_hots_tests(void);

/**
 * @brief Run deterministic key generation tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_deterministic_tests(void);

/**
 * @brief Run multi-signature tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_multisig_tests(void);

/**
 * @brief Run scalability tests (up to 30K signers)
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_scalability_tests(void);

/**
 * @brief Run performance benchmark tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_performance_tests(void);

/**
 * @brief Run stress tests
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int chipmunk_run_stress_tests(void);

// =============================================================================
// INDIVIDUAL TEST FUNCTIONS
// =============================================================================

// Basic functionality tests
int test_chipmunk_key_creation(void);
int test_chipmunk_key_generation(void);
int test_chipmunk_signature_creation_verification(void);
int test_chipmunk_signature_size_calculation(void);
int test_chipmunk_key_deletion(void);

// HOTS-specific tests
int test_hots_basic_functionality(void);
int test_hots_multiple_keys(void);
int test_hots_verification_diagnostic(void);

// Deterministic key generation tests
int test_deterministic_key_generation(void);
int test_deterministic_key_reproducibility(void);
int test_deterministic_different_seeds(void);

// Multi-signature tests
int test_multisig_aggregation_3_signers(void);
int test_multisig_aggregation_10_signers(void);
int test_multisig_aggregation_100_signers(void);
int test_batch_verification_small(void);
int test_batch_verification_large(void);

// Scalability tests
int test_scalability_1k_signers(void);
int test_scalability_10k_signers(void);
int test_scalability_30k_signers(void);

// Performance tests
int test_performance_key_generation(void);
int test_performance_signing(void);
int test_performance_verification(void);
int test_performance_multisig_aggregation(void);

// Stress tests
int test_stress_continuous_signing(void);
int test_stress_memory_usage(void);
int test_stress_concurrent_operations(void);

// Edge case and security tests
int test_corrupted_signature_rejection(void);
int test_wrong_key_verification(void);
int test_cross_verification(void);
int test_challenge_polynomial_generation(void);

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

/**
 * @brief Initialize test environment
 * 
 * @return int 0 on success, non-zero on failure
 */
int chipmunk_test_init(void);

/**
 * @brief Cleanup test environment
 */
void chipmunk_test_cleanup(void);

/**
 * @brief Print test suite statistics
 * 
 * @param stats Test suite statistics
 */
void chipmunk_print_test_stats(const chipmunk_test_suite_stats_t *stats);

/**
 * @brief Get current memory usage in bytes
 * 
 * @return size_t Memory usage in bytes
 */
size_t chipmunk_get_memory_usage(void);

/**
 * @brief Get current time in milliseconds
 * 
 * @return double Time in milliseconds
 */
double chipmunk_get_time_ms(void);

#ifdef __cplusplus
}
#endif 