/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK Phase 2 Profiling
 * Copyright  (c) 2025
 */

/**
 * @file profile_stage2.c
 * @brief Profiling tool for Stage 2 DOM building hotspots
 * 
 * Usage: perf record -g ./profile_stage2 test.json
 *        perf report
 */

#include "dap_common.h"
#include "dap_json.h"
#include "dap_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "profile_stage2"
#define ITERATIONS 10000

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        log_it(L_ERROR, "Failed to open %s", path);
        return NULL;
    }
    
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buffer = malloc(size + 1);
    if (!buffer) {
        fclose(f);
        return NULL;
    }
    
    fread(buffer, 1, size, f);
    buffer[size] = '\0';
    fclose(f);
    
    if (out_size) *out_size = size;
    return buffer;
}

int main(int argc, char **argv) {
    dap_log_level_set(L_WARNING);  // Minimal logging for profiling
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <json_file>\n", argv[0]);
        return 1;
    }
    
    // Read JSON file
    size_t json_size;
    char *json_data = read_file(argv[1], &json_size);
    if (!json_data) {
        return 1;
    }
    
    printf("JSON size: %zu bytes\n", json_size);
    printf("Iterations: %d\n", ITERATIONS);
    printf("Starting profiling...\n");
    
    // Warm-up
    for (int i = 0; i < 100; i++) {
        dap_json_t *json = dap_json_parse_string(json_data);
        if (json) {
            dap_json_object_free(json);
        }
    }
    
    // Profiling run
    dap_nanotime_t start_ns = dap_nanotime_now();
    
    for (int i = 0; i < ITERATIONS; i++) {
        dap_json_t *json = dap_json_parse_string(json_data);
        if (!json) {
            fprintf(stderr, "Parse failed at iteration %d\n", i);
            break;
        }
        dap_json_object_free(json);
    }
    
    dap_nanotime_t end_ns = dap_nanotime_now();
    
    // Cleanup thread-local arena
    dap_json_cleanup_thread_arena();
    
    // Calculate stats
    double elapsed = (end_ns - start_ns) / 1e9;
    double throughput_mb = (json_size * ITERATIONS / (1024.0 * 1024.0)) / elapsed;
    double ns_per_parse = (elapsed * 1e9) / ITERATIONS;
    
    printf("\n");
    printf("═══════════════════════════════════════════\n");
    printf("PROFILING RESULTS:\n");
    printf("═══════════════════════════════════════════\n");
    printf("Total time:     %.3f seconds\n", elapsed);
    printf("Throughput:     %.2f MB/s\n", throughput_mb);
    printf("Parse latency:  %.0f ns/parse\n", ns_per_parse);
    printf("Parses/second:  %.0f\n", ITERATIONS / elapsed);
    printf("═══════════════════════════════════════════\n");
    
    free(json_data);
    return 0;
}
