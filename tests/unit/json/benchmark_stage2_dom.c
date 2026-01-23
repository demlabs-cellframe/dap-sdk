/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP (Distributed Applications Platform) the open source project
 *
 *    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    DAP is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file benchmark_stage2_dom.c
 * @brief Stage 2 (DOM Building) performance benchmarks
 * @details Benchmarks Stage 2 DOM building with zero-copy strings:
 *          - String scanning (reference vs SIMD)
 *          - Number parsing (strtod vs future fast_float)
 *          - Memory allocation (Arena vs malloc)
 *          - Overall DOM building throughput
 * 
 * Tests zero-copy string optimization impact:
 *   - Strings without escapes: zero-copy (just pointer)
 *   - Strings with escapes: lazy unescaping (future)
 * 
 * Metrics:
 *   - Throughput (MB/s)
 *   - Latency (ms per parse)
 *   - Memory allocations
 *   - Cache efficiency
 * 
 * Test datasets:
 *   - String-heavy JSON: 10,000 strings
 *   - Number-heavy JSON: 10,000 numbers
 *   - Mixed JSON: realistic workload
 * 
 * @date 2026-01-13
 */

#define LOG_TAG "benchmark_stage2_dom"

#include "dap_common.h"
#include "dap_time.h"
#include "dap_json.h"
#include "internal/dap_json_string.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// TEST DATASET GENERATORS
// =============================================================================

/**
 * @brief Generate string-heavy JSON with N strings
 * @details Used to test zero-copy string optimization
 */
static char* s_generate_string_heavy_json(size_t a_num_strings, size_t *a_out_len)
{
    size_t l_capacity = a_num_strings * 60;
    char *l_json = malloc(l_capacity);
    if (!l_json) return NULL;
    
    size_t l_pos = 0;
    l_pos += sprintf(l_json + l_pos, "{\"strings\":[");
    
    for (size_t i = 0; i < a_num_strings; i++) {
        if (i > 0) l_pos += sprintf(l_json + l_pos, ",");
        l_pos += sprintf(l_json + l_pos, "\"string_%zu_abcdefghijklmnopqrstuvwxyz_0123456789\"", i);
        
        if (l_pos + 100 > l_capacity) {
            l_capacity *= 2;
            l_json = realloc(l_json, l_capacity);
            if (!l_json) return NULL;
        }
    }
    
    l_pos += sprintf(l_json + l_pos, "]}");
    *a_out_len = l_pos;
    return l_json;
}

/**
 * @brief Generate number-heavy JSON with N numbers
 * @details Used to test number parsing performance
 */
static char* s_generate_number_heavy_json(size_t a_num_numbers, size_t *a_out_len)
{
    size_t l_capacity = a_num_numbers * 30;
    char *l_json = malloc(l_capacity);
    if (!l_json) return NULL;
    
    size_t l_pos = 0;
    l_pos += sprintf(l_json + l_pos, "{\"numbers\":[");
    
    for (size_t i = 0; i < a_num_numbers; i++) {
        if (i > 0) l_pos += sprintf(l_json + l_pos, ",");
        l_pos += sprintf(l_json + l_pos, "%zu.%zu", i, i % 1000);
        
        if (l_pos + 50 > l_capacity) {
            l_capacity *= 2;
            l_json = realloc(l_json, l_capacity);
            if (!l_json) return NULL;
        }
    }
    
    l_pos += sprintf(l_json + l_pos, "]}");
    *a_out_len = l_pos;
    return l_json;
}

/**
 * @brief Generate mixed JSON (strings + numbers + nested objects)
 * @details Realistic workload
 */
static char* s_generate_mixed_json(size_t a_num_items, size_t *a_out_len)
{
    size_t l_capacity = a_num_items * 150;
    char *l_json = malloc(l_capacity);
    if (!l_json) return NULL;
    
    size_t l_pos = 0;
    l_pos += sprintf(l_json + l_pos, "{\"items\":[");
    
    for (size_t i = 0; i < a_num_items; i++) {
        if (i > 0) l_pos += sprintf(l_json + l_pos, ",");
        l_pos += sprintf(l_json + l_pos, 
            "{\"id\":%zu,\"name\":\"Item_%zu\",\"price\":%.2f,"
            "\"in_stock\":%s,\"tags\":[\"tag1\",\"tag2\",\"tag3\"]}",
            i, i, (double)i * 1.5, (i % 2) ? "true" : "false");
        
        if (l_pos + 200 > l_capacity) {
            l_capacity *= 2;
            l_json = realloc(l_json, l_capacity);
            if (!l_json) return NULL;
        }
    }
    
    l_pos += sprintf(l_json + l_pos, "]}");
    *a_out_len = l_pos;
    return l_json;
}

// =============================================================================
// TIMING UTILITIES
// =============================================================================

typedef struct {
    dap_nanotime_t start_ns;
    dap_nanotime_t end_ns;
    uint64_t total_ns;
} dap_benchmark_timer_t;

static inline void s_timer_start(dap_benchmark_timer_t *a_timer)
{
    a_timer->start_ns = dap_nanotime_now();
}

static inline void s_timer_stop(dap_benchmark_timer_t *a_timer)
{
    a_timer->end_ns = dap_nanotime_now();
    a_timer->total_ns = a_timer->end_ns - a_timer->start_ns;
}

static inline double s_timer_get_seconds(const dap_benchmark_timer_t *a_timer)
{
    return (double)a_timer->total_ns / 1e9;
}

// =============================================================================
// BENCHMARK FUNCTIONS
// =============================================================================

/**
 * @brief Benchmark Stage 2 DOM building on a dataset
 */
static void s_benchmark_stage2(
    const char *a_name,
    const char *a_json,
    size_t a_json_len,
    int a_iterations
)
{
    printf("\n=== %s ===\n", a_name);
    printf("JSON size: %zu bytes\n", a_json_len);
    printf("Iterations: %d\n", a_iterations);
    
    // Warmup
    for (int i = 0; i < 10; i++) {
        dap_json_t *l_obj = dap_json_parse_buffer((const uint8_t*)a_json, a_json_len);
        if (l_obj) dap_json_object_free(l_obj);
    }
    
    // Benchmark
    dap_benchmark_timer_t l_timer;
    s_timer_start(&l_timer);
    
    for (int i = 0; i < a_iterations; i++) {
        dap_json_t *l_obj = dap_json_parse_buffer((const uint8_t*)a_json, a_json_len);
        if (!l_obj) {
            fprintf(stderr, "Parse failed at iteration %d\n", i);
            return;
        }
        dap_json_object_free(l_obj);
    }
    
    s_timer_stop(&l_timer);
    
    double l_elapsed = s_timer_get_seconds(&l_timer);
    double l_total_mb = (double)(a_json_len * a_iterations) / (1024.0 * 1024.0);
    double l_throughput_mb = l_total_mb / l_elapsed;
    double l_avg_latency_ms = (l_elapsed / a_iterations) * 1000.0;
    
    printf("\n📊 Results:\n");
    printf("  Total processed: %.2f MB\n", l_total_mb);
    printf("  Time elapsed:    %.3f s\n", l_elapsed);
    printf("  Throughput:      %.2f MB/s\n", l_throughput_mb);
    printf("  Avg latency:     %.3f ms\n\n", l_avg_latency_ms);
}

// =============================================================================
// MAIN
// =============================================================================

int main(void)
{
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║      Stage 2 (DOM Building) Performance Benchmark         ║\n");
    printf("║                 Zero-Copy Strings Test                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Disable debug logging for clean benchmark output
    dap_log_level_set(L_INFO);
    
    // Test 1: String-heavy JSON (tests zero-copy string optimization)
    {
        size_t l_len = 0;
        char *l_json = s_generate_string_heavy_json(10000, &l_len);
        if (l_json) {
            s_benchmark_stage2("String-Heavy (10K strings)", l_json, l_len, 100);
            free(l_json);
        }
    }
    
    // Test 2: Number-heavy JSON (tests number parsing)
    {
        size_t l_len = 0;
        char *l_json = s_generate_number_heavy_json(10000, &l_len);
        if (l_json) {
            s_benchmark_stage2("Number-Heavy (10K numbers)", l_json, l_len, 100);
            free(l_json);
        }
    }
    
    // Test 3: Mixed JSON (realistic workload)
    {
        size_t l_len = 0;
        char *l_json = s_generate_mixed_json(1000, &l_len);
        if (l_json) {
            s_benchmark_stage2("Mixed (1K items)", l_json, l_len, 100);
            free(l_json);
        }
    }
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                  Benchmark Complete!                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}
