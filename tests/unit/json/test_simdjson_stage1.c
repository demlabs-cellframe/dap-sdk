/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file test_simdjson_stage1.c
 * @brief Tests for SimdJSON-style Stage 1 implementation (Phase 2.1)
 * @details Correctness tests and performance benchmarks for simdjson algorithm
 * @date 2026-01-11
 */

#define LOG_TAG "test_simdjson_stage1"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "dap_time.h"
#include "internal/dap_json_stage1.h"
#include <string.h>
#include <stdio.h>

/* Forward declaration for AVX2 SimdJSON function */
int dap_json_stage1_run_avx2_simdjson(dap_json_stage1_t *a_stage1);

/* ========================================================================== */
/*                          TEST DATA                                         */
/* ========================================================================== */

// Small JSON (< 32 bytes) - fits in single AVX2 chunk
static const char *s_small_json = "{\"key\":\"value\"}";

// Medium JSON (> 32 bytes) - requires multiple AVX2 chunks
static const char *s_medium_json = 
    "{\"name\":\"John\",\"age\":30,\"city\":\"New York\",\"active\":true}";

// Large JSON (> 1KB) - extensive testing
static const char *s_large_json = 
    "{"
    "\"users\":[{\"id\":1,\"name\":\"Alice\",\"email\":\"alice@example.com\"},"
    "{\"id\":2,\"name\":\"Bob\",\"email\":\"bob@example.com\"},"
    "{\"id\":3,\"name\":\"Charlie\",\"email\":\"charlie@example.com\"}],"
    "\"metadata\":{\"total\":3,\"page\":1,\"perPage\":10},"
    "\"timestamp\":\"2026-01-11T12:00:00Z\""
    "}";

/* ========================================================================== */
/*                          CORRECTNESS TESTS                                 */
/* ========================================================================== */

/**
 * @brief Test SimdJSON Stage 1 correctness against reference implementation
 */
static bool s_test_correctness(const char *a_json, const char *a_description)
{
    log_it(L_DEBUG, "Testing correctness: %s", a_description);
    
    size_t json_len = strlen(a_json);
    
    // Run reference implementation
    dap_json_stage1_t *ref_stage1 = dap_json_stage1_create((const uint8_t *)a_json, json_len);
    if (!ref_stage1) {
        log_it(L_ERROR, "Failed to create reference Stage 1 parser");
        return false;
    }
    
    int ref_result = dap_json_stage1_run_ref(ref_stage1);
    if (ref_result != STAGE1_SUCCESS) {
        log_it(L_ERROR, "Reference Stage 1 failed: error %d", ref_result);
        dap_json_stage1_free(ref_stage1);
        return false;
    }
    
    // Run SimdJSON implementation
    dap_json_stage1_t *simd_stage1 = dap_json_stage1_create((const uint8_t *)a_json, json_len);
    if (!simd_stage1) {
        log_it(L_ERROR, "Failed to create SimdJSON Stage 1 parser");
        dap_json_stage1_free(ref_stage1);
        return false;
    }
    
    int simd_result = dap_json_stage1_run_avx2_simdjson(simd_stage1);
    if (simd_result != STAGE1_SUCCESS) {
        log_it(L_ERROR, "SimdJSON Stage 1 failed: error %d", simd_result);
        dap_json_stage1_free(ref_stage1);
        dap_json_stage1_free(simd_stage1);
        return false;
    }
    
    // Compare token counts
    if (ref_stage1->indices_count != simd_stage1->indices_count) {
        log_it(L_ERROR, "Token count mismatch: ref=%zu, simd=%zu",
               ref_stage1->indices_count, simd_stage1->indices_count);
        dap_json_stage1_free(ref_stage1);
        dap_json_stage1_free(simd_stage1);
        return false;
    }
    
    // Compare tokens
    for (size_t i = 0; i < ref_stage1->indices_count; i++) {
        dap_json_struct_index_t *ref_token = &ref_stage1->indices[i];
        dap_json_struct_index_t *simd_token = &simd_stage1->indices[i];
        
        if (ref_token->position != simd_token->position ||
            ref_token->type != simd_token->type ||
            ref_token->character != simd_token->character) {
            log_it(L_ERROR, "Token %zu mismatch: ref={pos=%u,type=%d,char='%c'}, simd={pos=%u,type=%d,char='%c'}",
                   i, ref_token->position, ref_token->type, ref_token->character,
                   simd_token->position, simd_token->type, simd_token->character);
            dap_json_stage1_free(ref_stage1);
            dap_json_stage1_free(simd_stage1);
            return false;
        }
    }
    
    log_it(L_DEBUG, "✓ Correctness test passed: %s (%zu tokens)", a_description, ref_stage1->indices_count);
    
    dap_json_stage1_free(ref_stage1);
    dap_json_stage1_free(simd_stage1);
    return true;
}

/**
 * @brief Run all correctness tests
 */
static bool s_run_correctness_tests(void)
{
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "SimdJSON Stage 1: Correctness Tests");
    log_it(L_INFO, "====================================================================");
    
    int passed = 0;
    int total = 3;
    
    if (s_test_correctness(s_small_json, "Small JSON (< 32 bytes)")) passed++;
    if (s_test_correctness(s_medium_json, "Medium JSON (> 32 bytes)")) passed++;
    if (s_test_correctness(s_large_json, "Large JSON (> 1KB)")) passed++;
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "Correctness Tests: %d/%d passed (%d%%)", passed, total, (passed * 100) / total);
    log_it(L_INFO, "====================================================================");
    
    return (passed == total);
}

/* ========================================================================== */
/*                          PERFORMANCE BENCHMARKS                            */
/* ========================================================================== */

/**
 * @brief Benchmark SimdJSON Stage 1 performance
 */
static void s_benchmark_simdjson(const char *a_json, const char *a_description, int a_iterations)
{
    log_it(L_INFO, " ");
    log_it(L_INFO, "Benchmarking: %s (%d iterations)", a_description, a_iterations);
    
    size_t json_len = strlen(a_json);
    
    // Warmup
    for (int i = 0; i < 100; i++) {
        dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)a_json, json_len);
        dap_json_stage1_run_avx2_simdjson(stage1);
        dap_json_stage1_free(stage1);
    }
    
    // Benchmark
    uint64_t start_time = dap_time_now();
    
    for (int i = 0; i < a_iterations; i++) {
        dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)a_json, json_len);
        dap_json_stage1_run_avx2_simdjson(stage1);
        dap_json_stage1_free(stage1);
    }
    
    uint64_t end_time = dap_time_now();
    uint64_t elapsed_us = end_time - start_time;
    
    // Calculate metrics
    double elapsed_sec = elapsed_us / 1000000.0;
    double total_bytes = (double)(json_len * a_iterations);
    double throughput_mb = (total_bytes / (1024.0 * 1024.0)) / elapsed_sec;
    double throughput_gb = throughput_mb / 1024.0;
    double time_per_iteration = (double)elapsed_us / a_iterations;
    
    log_it(L_INFO, "  JSON size:          %zu bytes", json_len);
    log_it(L_INFO, "  Iterations:         %d", a_iterations);
    log_it(L_INFO, "  Total time:         %.3f ms", elapsed_us / 1000.0);
    log_it(L_INFO, "  Time per iteration: %.3f μs", time_per_iteration);
    log_it(L_INFO, "  Throughput:         %.2f MB/s (%.3f GB/s)", throughput_mb, throughput_gb);
    
    // Check if we achieved target
    if (throughput_gb >= 4.0) {
        log_it(L_INFO, "  ✅ TARGET ACHIEVED: %.3f GB/s >= 4.0 GB/s", throughput_gb);
    } else {
        log_it(L_WARNING, "  ⚠️  Below target: %.3f GB/s < 4.0 GB/s", throughput_gb);
    }
}

/**
 * @brief Run all performance benchmarks
 */
static void s_run_benchmarks(void)
{
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "SimdJSON Stage 1: Performance Benchmarks");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "Target: 4-5 GB/s throughput (AVX2)");
    log_it(L_INFO, "====================================================================");
    
    s_benchmark_simdjson(s_small_json, "Small JSON (< 32 bytes)", 50000);
    s_benchmark_simdjson(s_medium_json, "Medium JSON (> 32 bytes)", 20000);
    s_benchmark_simdjson(s_large_json, "Large JSON (> 1KB)", 10000);
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "Benchmarks Complete");
    log_it(L_INFO, "====================================================================");
}

/* ========================================================================== */
/*                          MAIN TEST RUNNER                                  */
/* ========================================================================== */

int main(void)
{
    dap_log_level_set(L_DEBUG);
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "DAP JSON: SimdJSON Stage 1 Tests (Phase 2.1)");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "CPU Architecture: %s", dap_cpu_arch_get_name(dap_cpu_arch_get()));
    log_it(L_INFO, "====================================================================");
    
    // Check AVX2 availability
    if (!dap_cpu_arch_is_available(DAP_CPU_ARCH_AVX2)) {
        log_it(L_WARNING, "AVX2 not available on this CPU, skipping tests");
        return 0;
    }
    
    // Run correctness tests
    bool correctness_passed = s_run_correctness_tests();
    
    if (!correctness_passed) {
        log_it(L_ERROR, "❌ Correctness tests FAILED");
        return -1;
    }
    
    // Run performance benchmarks
    s_run_benchmarks();
    
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "✅ ALL TESTS PASSED");
    log_it(L_INFO, "====================================================================");
    
    return 0;
}

