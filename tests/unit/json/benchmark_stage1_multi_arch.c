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
 * @file benchmark_stage1_multi_arch.c
 * @brief Multi-architecture performance benchmarks for Stage 1 tokenization
 * @details Comprehensive benchmarks comparing all SIMD implementations:
 *          - Reference C (-O3 optimized)
 *          - SSE2 (128-bit SIMD)
 *          - AVX2 (256-bit SIMD)
 *          - AVX-512 (512-bit SIMD)
 *          - ARM NEON (128-bit SIMD)
 * 
 * Metrics:
 *   - Throughput (GB/s)
 *   - Cycles per byte (estimated)
 *   - Tokens per second
 * 
 * Test datasets:
 *   - Small JSON (< 1 KB): API responses, config files
 *   - Medium JSON (1-100 KB): User profiles, product catalogs
 *   - Large JSON (> 100 KB): Analytics data, logs
 * 
 * @date 2026-01-11
 */

#define LOG_TAG "benchmark_stage1_multi_arch"

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "internal/dap_json_stage1.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

// =============================================================================
// TEST DATASETS
// =============================================================================

// Small JSON (< 1 KB) - typical API response
static const char *s_small_json =
    "{\"id\":12345,\"name\":\"John Doe\",\"email\":\"john@example.com\","
    "\"age\":30,\"verified\":true,\"tags\":[\"user\",\"premium\"],"
    "\"metadata\":{\"created\":\"2026-01-01\",\"updated\":\"2026-01-10\"}}";

// Medium JSON (~5 KB) - user profile with nested data
static const char *s_medium_json =
    "{\"user\":{\"id\":12345,\"profile\":{\"name\":\"John Doe\","
    "\"email\":\"john@example.com\",\"phone\":\"+1-555-0123\","
    "\"address\":{\"street\":\"123 Main St\",\"city\":\"San Francisco\","
    "\"state\":\"CA\",\"zip\":\"94102\",\"country\":\"USA\"},"
    "\"preferences\":{\"theme\":\"dark\",\"language\":\"en\","
    "\"notifications\":{\"email\":true,\"sms\":false,\"push\":true}},"
    "\"social\":{\"twitter\":\"@johndoe\",\"linkedin\":\"johndoe\","
    "\"github\":\"johndoe\"}},\"account\":{\"type\":\"premium\","
    "\"status\":\"active\",\"created\":\"2020-01-15T10:30:00Z\","
    "\"expires\":\"2027-01-15T10:30:00Z\",\"credits\":1000,"
    "\"usage\":{\"api_calls\":5432,\"storage_mb\":1234,\"bandwidth_gb\":567}},"
    "\"permissions\":[\"read\",\"write\",\"admin\",\"billing\"],"
    "\"tags\":[\"vip\",\"early_adopter\",\"developer\",\"enterprise\"],"
    "\"metadata\":{\"last_login\":\"2026-01-10T15:45:00Z\","
    "\"login_count\":1523,\"ip\":\"192.168.1.100\","
    "\"user_agent\":\"Mozilla/5.0 (X11; Linux x86_64)\"}}}";

// =============================================================================
// TIMING UTILITIES
// =============================================================================

typedef struct {
    struct timespec start;
    struct timespec end;
    uint64_t total_ns;
} dap_benchmark_timer_t;

static inline void s_timer_start(dap_benchmark_timer_t *a_timer)
{
    clock_gettime(CLOCK_MONOTONIC, &a_timer->start);
}

static inline void s_timer_stop(dap_benchmark_timer_t *a_timer)
{
    clock_gettime(CLOCK_MONOTONIC, &a_timer->end);
    
    uint64_t start_ns = (uint64_t)a_timer->start.tv_sec * 1000000000ULL + a_timer->start.tv_nsec;
    uint64_t end_ns = (uint64_t)a_timer->end.tv_sec * 1000000000ULL + a_timer->end.tv_nsec;
    a_timer->total_ns = end_ns - start_ns;
}

// =============================================================================
// BENCHMARK CORE
// =============================================================================

typedef struct {
    const char *arch_name;
    dap_cpu_arch_t arch;
    double throughput_gbps;  // GB/s
    double tokens_per_sec;   // tokens/s
    uint64_t time_ns;        // nanoseconds
    bool available;
} dap_bench_result_t;

/**
 * @brief Benchmark Stage 1 with specific architecture
 */
static bool s_benchmark_stage1(
    const char *a_json,
    size_t a_json_len,
    int a_iterations,
    dap_cpu_arch_t a_arch,
    dap_bench_result_t *a_result
)
{
    // Set architecture
    if (dap_cpu_arch_set(a_arch) != 0) {
        a_result->available = false;
        return false;
    }
    
    a_result->arch = a_arch;
    a_result->arch_name = dap_cpu_arch_get_name(a_arch);
    a_result->available = true;
    
    // Warm-up (1 iteration)
    {
        dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)a_json, a_json_len);
        if (!stage1) {
            log_it(L_ERROR, "Failed to create stage1 parser for warm-up");
            return false;
        }
        
        int ret = dap_json_stage1_run(stage1);
        if (ret != 0) {
            log_it(L_ERROR, "Warm-up run failed with error %d", ret);
            dap_json_stage1_free(stage1);
            return false;
        }
        
        dap_json_stage1_free(stage1);
    }
    
    // Benchmark
    dap_benchmark_timer_t timer = {0};
    size_t total_tokens = 0;
    
    s_timer_start(&timer);
    
    for (int i = 0; i < a_iterations; i++) {
        dap_json_stage1_t *stage1 = dap_json_stage1_create((const uint8_t *)a_json, a_json_len);
        if (!stage1) {
            log_it(L_ERROR, "Failed to create stage1 parser at iteration %d", i);
            return false;
        }
        
        int ret = dap_json_stage1_run(stage1);
        if (ret != 0) {
            log_it(L_ERROR, "Run failed at iteration %d with error %d", i, ret);
            dap_json_stage1_free(stage1);
            return false;
        }
        
        total_tokens += dap_json_stage1_get_token_count(stage1);
        
        dap_json_stage1_free(stage1);
    }
    
    s_timer_stop(&timer);
    
    // Calculate metrics
    a_result->time_ns = timer.total_ns;
    
    // Throughput: (bytes * iterations) / (time_ns / 1e9) / 1e9
    double total_gb = (double)(a_json_len * a_iterations) / 1e9;
    double time_sec = (double)timer.total_ns / 1e9;
    a_result->throughput_gbps = total_gb / time_sec;
    
    // Tokens per second
    a_result->tokens_per_sec = (double)total_tokens / time_sec;
    
    return true;
}

/**
 * @brief Print benchmark results table
 */
static void s_print_results_table(
    const char *a_dataset_name,
    size_t a_json_len,
    int a_iterations,
    dap_bench_result_t *a_results,
    int a_num_results
)
{
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "Benchmark: %s (size: %zu bytes, iterations: %d)",
           a_dataset_name, a_json_len, a_iterations);
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "%-20s %15s %15s %15s",
           "Architecture", "Throughput", "Tokens/sec", "Time (ms)");
    log_it(L_INFO, "%-20s %15s %15s %15s",
           "--------------------", "---------------", "---------------", "---------------");
    
    for (int i = 0; i < a_num_results; i++) {
        if (!a_results[i].available) {
            log_it(L_INFO, "%-20s %15s %15s %15s",
                   a_results[i].arch_name, "-", "-", "-");
            continue;
        }
        
        double time_ms = (double)a_results[i].time_ns / 1e6;
        
        log_it(L_INFO, "%-20s %12.2f GB/s %12.0f M/s %12.3f ms",
               a_results[i].arch_name,
               a_results[i].throughput_gbps,
               a_results[i].tokens_per_sec / 1e6,
               time_ms);
    }
    
    log_it(L_INFO, "====================================================================");
    
    // Calculate speedup vs Reference C
    if (a_results[0].available) {
        log_it(L_INFO, "Speedup vs Reference C:");
        double ref_throughput = a_results[0].throughput_gbps;
        
        for (int i = 1; i < a_num_results; i++) {
            if (!a_results[i].available) continue;
            
            double speedup = a_results[i].throughput_gbps / ref_throughput;
            log_it(L_INFO, "  %s: %.2fx", a_results[i].arch_name, speedup);
        }
    }
    log_it(L_INFO, " ");
}

/**
 * @brief Main benchmark runner
 */
static int s_run_benchmarks(void)
{
    log_it(L_INFO, " ");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "DAP JSON Stage 1: Multi-Architecture Performance Benchmarks");
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, " ");
    
    // Define architectures to test
    dap_cpu_arch_t archs[] = {
        DAP_CPU_ARCH_REFERENCE,
        DAP_CPU_ARCH_SSE2,
        DAP_CPU_ARCH_AVX2,
        DAP_CPU_ARCH_AVX512,
        DAP_CPU_ARCH_NEON
    };
    int num_archs = sizeof(archs) / sizeof(archs[0]);
    
    // Small JSON benchmark (10,000 iterations)
    {
        dap_bench_result_t results[5] = {0};
        size_t json_len = strlen(s_small_json);
        int iterations = 10000;
        
        for (int i = 0; i < num_archs; i++) {
            s_benchmark_stage1(s_small_json, json_len, iterations, archs[i], &results[i]);
        }
        
        s_print_results_table("Small JSON", json_len, iterations, results, num_archs);
    }
    
    // Medium JSON benchmark (5,000 iterations)
    {
        dap_bench_result_t results[5] = {0};
        size_t json_len = strlen(s_medium_json);
        int iterations = 5000;
        
        for (int i = 0; i < num_archs; i++) {
            s_benchmark_stage1(s_medium_json, json_len, iterations, archs[i], &results[i]);
        }
        
        s_print_results_table("Medium JSON", json_len, iterations, results, num_archs);
    }
    
    log_it(L_INFO, "====================================================================");
    log_it(L_INFO, "Benchmarks Complete");
    log_it(L_INFO, "====================================================================");
    
    // Reset to AUTO
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    return 0;
}

int main(void)
{
    return s_run_benchmarks();
}

