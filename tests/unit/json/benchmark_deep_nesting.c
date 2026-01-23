/*
 * Authors:
 * DAP JSON Native Implementation Team
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP is free software: you can redistribute it and/or modify
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
 * @file benchmark_deep_nesting.c
 * @brief Deep nesting JSON benchmark - Phase 2.4
 * 
 * Tests parser performance on deeply nested JSON structures.
 * Key metric: recursion overhead + allocation overhead.
 * 
 * With node pool optimization (Phase 2.4):
 * - Expected: ~1ns per node allocation (vs ~5-10ns before)
 * - Target: >= 1.5 GB/s for 1000 levels
 * 
 * @date 2026-01-13
 */

#include "dap_common.h"
#include "dap_json.h"
#include <time.h>
#include <sys/time.h>

#define LOG_TAG "benchmark_deep_nesting"

/**
 * @brief Get current time in microseconds
 */
static inline uint64_t s_get_time_us(void)
{
    struct timeval l_tv;
    gettimeofday(&l_tv, NULL);
    return (uint64_t)l_tv.tv_sec * 1000000ULL + (uint64_t)l_tv.tv_usec;
}

/**
 * @brief Generate deeply nested array JSON
 * @param a_depth Nesting depth
 * @return JSON string (caller must free)
 */
static char *s_generate_nested_array(size_t a_depth)
{
    // Each level: "[\n" (2 bytes) + "]" (1 byte) = 3 bytes
    // Total: depth * 3 + 1 (null terminator)
    size_t l_size = a_depth * 3 + 1;
    char *l_json = DAP_NEW_SIZE(char, l_size);
    
    char *l_ptr = l_json;
    
    // Opening brackets
    for (size_t i = 0; i < a_depth; i++) {
        *l_ptr++ = '[';
    }
    
    // Closing brackets
    for (size_t i = 0; i < a_depth; i++) {
        *l_ptr++ = ']';
    }
    
    *l_ptr = '\0';
    return l_json;
}

/**
 * @brief Generate deeply nested object JSON
 * @param a_depth Nesting depth
 * @return JSON string (caller must free)
 */
static char *s_generate_nested_object(size_t a_depth)
{
    // Each level: {"a": (6 bytes) + } (1 byte) = 7 bytes
    size_t l_size = a_depth * 7 + 1;
    char *l_json = DAP_NEW_SIZE(char, l_size);
    
    char *l_ptr = l_json;
    
    // Opening
    for (size_t i = 0; i < a_depth; i++) {
        memcpy(l_ptr, "{\"a\":", 5);
        l_ptr += 5;
    }
    
    // Value at deepest level
    *l_ptr++ = '1';
    
    // Closing
    for (size_t i = 0; i < a_depth; i++) {
        *l_ptr++ = '}';
    }
    
    *l_ptr = '\0';
    return l_json;
}

/**
 * @brief Benchmark deep nesting
 * @param a_type "array" or "object"
 * @param a_depth Nesting depth
 * @param a_iterations Number of iterations
 */
static void s_benchmark_deep_nesting(const char *a_type, size_t a_depth, size_t a_iterations)
{
    printf("\n═══════════════════════════════════════════\n");
    printf("DEEP NESTING: %s (depth=%zu)\n", a_type, a_depth);
    printf("═══════════════════════════════════════════\n");
    
    // Generate JSON
    char *l_json = NULL;
    if (strcmp(a_type, "array") == 0) {
        l_json = s_generate_nested_array(a_depth);
    } else {
        l_json = s_generate_nested_object(a_depth);
    }
    
    size_t l_json_len = strlen(l_json);
    printf("JSON size: %zu bytes\n", l_json_len);
    printf("Iterations: %zu\n\n", a_iterations);
    
    // Warmup
    for (size_t i = 0; i < 10; i++) {
        dap_json_t *l_doc = dap_json_parse_string(l_json);
        dap_json_object_free(l_doc);
    }
    
    // Benchmark
    uint64_t l_start = s_get_time_us();
    
    for (size_t i = 0; i < a_iterations; i++) {
        dap_json_t *l_doc = dap_json_parse_string(l_json);
        dap_json_object_free(l_doc);
    }
    
    uint64_t l_end = s_get_time_us();
    uint64_t l_elapsed_us = l_end - l_start;
    
    // Calculate metrics
    double l_elapsed_sec = l_elapsed_us / 1000000.0;
    double l_latency_us = (double)l_elapsed_us / a_iterations;
    double l_parses_per_sec = a_iterations / l_elapsed_sec;
    
    // Throughput: total bytes parsed / time
    uint64_t l_total_bytes = l_json_len * a_iterations;
    double l_throughput_mbps = (l_total_bytes / (1024.0 * 1024.0)) / l_elapsed_sec;
    
    printf("Results:\n");
    printf("───────────────────────────────────────────\n");
    printf("Total time:       %.3f seconds\n", l_elapsed_sec);
    printf("Latency:          %.2f μs/parse\n", l_latency_us);
    printf("Throughput:       %.2f MB/s\n", l_throughput_mbps);
    printf("Parses/second:    %.0f\n", l_parses_per_sec);
    printf("───────────────────────────────────────────\n");
    
    // Performance analysis
    double l_ns_per_level = (l_latency_us * 1000.0) / a_depth;
    printf("\nPer-level cost:   %.2f ns/level\n", l_ns_per_level);
    printf("(Lower is better: <5ns = excellent, <10ns = good, >20ns = needs work)\n");
    
    DAP_DELETE(l_json);
}

int main(void)
{
    dap_log_level_set(L_WARNING);  // Reduce noise
    
    printf("\n╔═══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║           DEEP NESTING BENCHMARK - Phase 2.4 (Node Pool)                    ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════════╝\n");
    
    // Test different depths and structures
    s_benchmark_deep_nesting("array", 100, 10000);
    s_benchmark_deep_nesting("array", 1000, 1000);
    s_benchmark_deep_nesting("array", 5000, 200);
    
    s_benchmark_deep_nesting("object", 100, 10000);
    s_benchmark_deep_nesting("object", 1000, 1000);
    s_benchmark_deep_nesting("object", 5000, 200);
    
    printf("\n╔═══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                           BENCHMARK COMPLETE                                  ║\n");
    printf("╚═══════════════════════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
