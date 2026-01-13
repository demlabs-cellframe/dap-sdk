/*
 * Authors:
 * DAP SDK Team
 * Copyright (c) 2026
 * All rights reserved.
 *
 * This file is part of DAP SDK
 */

/**
 * @file profile_numbers.c
 * @brief Benchmark for number-heavy JSON parsing (Phase 2.2)
 * @details Measures performance on JSON with many integers and doubles
 * @date 2026-01-13
 */

#include "dap_json.h"
#include "dap_common.h"
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define LOG_TAG "profile_numbers"

/**
 * @brief Generate number-heavy JSON for benchmarking
 */
static char* s_generate_number_json(size_t a_count, bool a_mix_floats) {
    // Estimate: each number entry ~20 bytes: {"id":123},
    size_t l_estimated_size = a_count * 30 + 100;
    char *l_json = malloc(l_estimated_size);
    if (!l_json) return NULL;
    
    size_t l_pos = 0;
    l_pos += sprintf(l_json + l_pos, "{\"numbers\":[");
    
    for (size_t i = 0; i < a_count; i++) {
        if (i > 0) {
            l_json[l_pos++] = ',';
        }
        
        if (a_mix_floats && (i % 3 == 0)) {
            // Float: 3.141592653589793
            l_pos += sprintf(l_json + l_pos, "%.15f", 3.141592653589793 * (i + 1));
        } else {
            // Integer
            l_pos += sprintf(l_json + l_pos, "%zu", i * 12345);
        }
    }
    
    l_pos += sprintf(l_json + l_pos, "]}");
    
    return l_json;
}

int main() {
    dap_log_level_set(L_WARNING);  // Suppress debug logs for benchmark
    
    printf("═══════════════════════════════════════════\n");
    printf("NUMBER-HEAVY JSON BENCHMARK (Phase 2.2)\n");
    printf("═══════════════════════════════════════════\n\n");
    
    // Test 1: Integers only (fast path)
    {
        printf("TEST 1: Integers Only (10000 numbers)\n");
        printf("───────────────────────────────────────────\n");
        
        char *l_json = s_generate_number_json(10000, false);
        if (!l_json) {
            printf("ERROR: Failed to generate JSON\n");
            return 1;
        }
        
        size_t l_json_size = strlen(l_json);
        printf("JSON size: %zu bytes\n", l_json_size);
        
        // Warm-up
        dap_json_t *l_warmup = dap_json_parse_string(l_json);
        if (!l_warmup) {
            printf("ERROR: Failed to parse JSON\n");
            free(l_json);
            return 1;
        }
        dap_json_object_free(l_warmup);
        dap_json_cleanup_thread_arena();
        
        // Benchmark
        const int l_iterations = 1000;
        struct timespec l_start, l_end;
        clock_gettime(CLOCK_MONOTONIC, &l_start);
        
        for (int i = 0; i < l_iterations; i++) {
            dap_json_t *l_parsed = dap_json_parse_string(l_json);
            dap_json_object_free(l_parsed);
            dap_json_cleanup_thread_arena();
        }
        
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        
        double l_elapsed = (l_end.tv_sec - l_start.tv_sec) + 
                          (l_end.tv_nsec - l_start.tv_nsec) / 1e9;
        double l_throughput_mb_s = (l_json_size * l_iterations) / (l_elapsed * 1024 * 1024);
        double l_latency_ns = (l_elapsed / l_iterations) * 1e9;
        
        printf("Elapsed time:   %.3f seconds\n", l_elapsed);
        printf("Throughput:     %.2f MB/s\n", l_throughput_mb_s);
        printf("Latency:        %.0f ns/parse\n", l_latency_ns);
        printf("Parses/second:  %.0f\n\n", l_iterations / l_elapsed);
        
        free(l_json);
    }
    
    // Test 2: Mixed (integers + floats)
    {
        printf("TEST 2: Mixed Numbers (10000 numbers, 33%% floats)\n");
        printf("───────────────────────────────────────────\n");
        
        char *l_json = s_generate_number_json(10000, true);
        if (!l_json) {
            printf("ERROR: Failed to generate JSON\n");
            return 1;
        }
        
        size_t l_json_size = strlen(l_json);
        printf("JSON size: %zu bytes\n", l_json_size);
        
        // Warm-up
        dap_json_t *l_warmup = dap_json_parse_string(l_json);
        if (!l_warmup) {
            printf("ERROR: Failed to parse JSON\n");
            free(l_json);
            return 1;
        }
        dap_json_object_free(l_warmup);
        dap_json_cleanup_thread_arena();
        
        // Benchmark
        const int l_iterations = 1000;
        struct timespec l_start, l_end;
        clock_gettime(CLOCK_MONOTONIC, &l_start);
        
        for (int i = 0; i < l_iterations; i++) {
            dap_json_t *l_parsed = dap_json_parse_string(l_json);
            dap_json_object_free(l_parsed);
            dap_json_cleanup_thread_arena();
        }
        
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        
        double l_elapsed = (l_end.tv_sec - l_start.tv_sec) + 
                          (l_end.tv_nsec - l_start.tv_nsec) / 1e9;
        double l_throughput_mb_s = (l_json_size * l_iterations) / (l_elapsed * 1024 * 1024);
        double l_latency_ns = (l_elapsed / l_iterations) * 1e9;
        
        printf("Elapsed time:   %.3f seconds\n", l_elapsed);
        printf("Throughput:     %.2f MB/s\n", l_throughput_mb_s);
        printf("Latency:        %.0f ns/parse\n", l_latency_ns);
        printf("Parses/second:  %.0f\n\n", l_iterations / l_elapsed);
        
        free(l_json);
    }
    
    printf("═══════════════════════════════════════════\n");
    printf("BASELINE: Current performance\n");
    printf("TARGET: 1+ GB/s with Lemire's fast_float\n");
    printf("═══════════════════════════════════════════\n");
    
    return 0;
}
