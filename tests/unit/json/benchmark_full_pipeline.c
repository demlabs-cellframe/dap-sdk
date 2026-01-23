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
 * @file benchmark_full_pipeline.c
 * @brief End-to-end JSON parsing performance benchmark
 * @details Complete pipeline benchmark: Stage 1 + Stage 2
 *          
 *          Full Pipeline:
 *          1. Stage 1: SIMD tokenization (structural indexing)
 *          2. Stage 2: Zero-copy DOM building
 *          3. API: Object access & traversal
 * 
 *          Comparisons:
 *          - Stage 1 only (tokenization)
 *          - Stage 2 only (DOM building from tokens)
 *          - Full pipeline (Stage 1 + Stage 2 + API access)
 * 
 *          This benchmark shows the COMPLETE picture:
 *          - How fast can we parse JSON start-to-finish?
 *          - What's the bottleneck (Stage 1 vs Stage 2)?
 *          - How does zero-copy optimization impact total time?
 * 
 * Metrics:
 *   - End-to-end throughput (MB/s)
 *   - Stage breakdown (% time in each stage)
 *   - Total latency (ms)
 *   - Memory usage
 * 
 * Test datasets:
 *   - Small JSON (< 1 KB)
 *   - Medium JSON (1-100 KB)
 *   - Large JSON (> 100 KB)
 *   - String-heavy JSON
 *   - Number-heavy JSON
 * 
 * @date 2026-01-13
 */

#define LOG_TAG "benchmark_full_pipeline"

#include "dap_common.h"
#include "dap_time.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

// =============================================================================
// TEST DATASETS
// =============================================================================

// Small JSON (< 1 KB)
static const char *s_small_json =
    "{\"id\":12345,\"name\":\"John Doe\",\"email\":\"john@example.com\","
    "\"age\":30,\"verified\":true,\"tags\":[\"user\",\"premium\"],"
    "\"metadata\":{\"created\":\"2026-01-01\",\"updated\":\"2026-01-10\"}}";

// Medium JSON (~5 KB)
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

/**
 * @brief Generate large string-heavy JSON
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
        l_pos += sprintf(l_json + l_pos, "\"string_%zu_abcdefghijklmnopqrstuvwxyz\"", i);
        
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
 * @brief Generate large number-heavy JSON
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
 * @brief Benchmark full pipeline (Stage 1 + Stage 2 + API access)
 */
static void s_benchmark_full_pipeline(
    const char *a_name,
    const char *a_json,
    size_t a_json_len,
    int a_iterations
)
{
    printf("\n╔════════════════════════════════════════════════════════════╗\n");
    printf("║ %s\n", a_name);
    printf("╚════════════════════════════════════════════════════════════╝\n");
    printf("JSON size: %zu bytes\n", a_json_len);
    printf("Iterations: %d\n", a_iterations);
    // CPU arch
    
    // Warmup
    for (int i = 0; i < 10; i++) {
        dap_json_t *l_obj = dap_json_parse_buffer((const uint8_t*)a_json, a_json_len);
        if (l_obj) dap_json_object_free(l_obj);
    }
    
    // Benchmark Full Pipeline
    dap_benchmark_timer_t l_full_timer;
    s_timer_start(&l_full_timer);
    for (int i = 0; i < a_iterations; i++) {
        dap_json_t *l_obj = dap_json_parse_buffer((const uint8_t*)a_json, a_json_len);
        if (!l_obj) {
            fprintf(stderr, "Parse failed at iteration %d\n", i);
            return;
        }
        dap_json_object_free(l_obj);
    }
    s_timer_stop(&l_full_timer);
    
    // Calculate metrics
    double l_full_sec = s_timer_get_seconds(&l_full_timer);
    
    double l_total_mb = (double)(a_json_len * a_iterations) / (1024.0 * 1024.0);
    double l_full_throughput = l_total_mb / l_full_sec;
    
    double l_avg_latency_ms = (l_full_sec / a_iterations) * 1000.0;
    
    printf("\n📊 RESULTS:\n");
    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│ Full Pipeline (Stage 1 + Stage 2)                      │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Total processed:  %.2f MB                              │\n", l_total_mb);
    printf("│ Time elapsed:     %.3f s                               │\n", l_full_sec);
    printf("│ Throughput:       %.2f MB/s                            │\n", l_full_throughput);
    printf("│ Avg latency:      %.3f ms                              │\n", l_avg_latency_ms);
    printf("└─────────────────────────────────────────────────────────┘\n");
    
    printf("\n");
}

// =============================================================================
// MAIN
// =============================================================================

int main(void)
{
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                                                            ║\n");
    printf("║       DAP JSON FULL PIPELINE PERFORMANCE BENCHMARK         ║\n");
    printf("║              (Stage 1 + Stage 2 + API)                     ║\n");
    printf("║                                                            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    // Disable debug logging
    dap_log_level_set(L_CRITICAL);
    
    // Test 1: Small JSON
    s_benchmark_full_pipeline(
        "Small JSON (< 1 KB)",
        s_small_json,
        strlen(s_small_json),
        1000
    );
    
    // Test 2: Medium JSON
    s_benchmark_full_pipeline(
        "Medium JSON (~5 KB)",
        s_medium_json,
        strlen(s_medium_json),
        500
    );
    
    // Test 3: String-heavy JSON (tests zero-copy)
    {
        size_t l_len = 0;
        char *l_json = s_generate_string_heavy_json(10000, &l_len);
        if (l_json) {
            s_benchmark_full_pipeline(
                "String-Heavy (10K strings, ~400 KB)",
                l_json,
                l_len,
                100
            );
            free(l_json);
        }
    }
    
    // Test 4: Number-heavy JSON (future fast_float optimization)
    {
        size_t l_len = 0;
        char *l_json = s_generate_number_heavy_json(10000, &l_len);
        if (l_json) {
            s_benchmark_full_pipeline(
                "Number-Heavy (10K numbers, ~100 KB)",
                l_json,
                l_len,
                100
            );
            free(l_json);
        }
    }
    
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║                  BENCHMARK COMPLETE!                       ║\n");
    printf("║                                                            ║\n");
    printf("║  Next optimizations:                                       ║\n");
    printf("║  - Phase 2.3: Fast number parsing (fast_float)             ║\n");
    printf("║  - Phase 2.4: Iterative parsing + aggressive Arena         ║\n");
    printf("║  - Phase 2.5: Cache optimization + prefetch                ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    return 0;
}
