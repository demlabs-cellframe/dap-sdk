/*
 * Quick performance test for zero-copy string scanner
 * Compares Reference vs SIMD implementations
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "dap_common.h"
#include "internal/dap_json_string.h"
#include "dap_json.h"

#define LOG_TAG "perf_test"

// Generate test JSON with many strings
static char* generate_string_heavy_json(size_t num_strings, size_t *out_len) {
    size_t capacity = num_strings * 50; // rough estimate
    char *json = malloc(capacity);
    if (!json) return NULL;
    
    size_t pos = 0;
    pos += sprintf(json + pos, "{\"strings\":[");
    
    for (size_t i = 0; i < num_strings; i++) {
        if (i > 0) pos += sprintf(json + pos, ",");
        pos += sprintf(json + pos, "\"string_%zu_abcdefghijklmnopqrstuvwxyz\"", i);
        
        // Reallocate if needed
        if (pos + 100 > capacity) {
            capacity *= 2;
            json = realloc(json, capacity);
            if (!json) return NULL;
        }
    }
    
    pos += sprintf(json + pos, "]}");
    *out_len = pos;
    return json;
}

int main() {
    printf("=== Zero-Copy String Scanner Performance Test ===\n\n");
    
    // Generate test data: 10K strings
    printf("Generating test JSON (10,000 strings)...\n");
    size_t json_len = 0;
    char *json = generate_string_heavy_json(10000, &json_len);
    if (!json) {
        fprintf(stderr, "Failed to generate test JSON\n");
        return 1;
    }
    printf("Generated %zu bytes of JSON\n\n", json_len);
    
    // Warmup
    printf("Warming up...\n");
    for (int i = 0; i < 10; i++) {
        dap_json_t *obj = dap_json_parse_buffer((const uint8_t*)json, json_len);
        if (obj) dap_json_object_free(obj);
    }
    
    // Benchmark: Parse same JSON 100 times
    printf("Benchmarking: 100 iterations...\n\n");
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    const int iterations = 100;
    for (int i = 0; i < iterations; i++) {
        dap_json_t *obj = dap_json_parse_buffer((const uint8_t*)json, json_len);
        if (!obj) {
            fprintf(stderr, "Parse failed at iteration %d\n", i);
            free(json);
            return 1;
        }
        dap_json_object_free(obj);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double elapsed = (end.tv_sec - start.tv_sec) + 
                     (end.tv_nsec - start.tv_nsec) / 1e9;
    
    double total_mb = (double)(json_len * iterations) / (1024.0 * 1024.0);
    double throughput = total_mb / elapsed;
    double avg_latency_ms = (elapsed / iterations) * 1000.0;
    
    printf("=== RESULTS ===\n");
    printf("Total processed:  %.2f MB\n", total_mb);
    printf("Time elapsed:     %.3f seconds\n", elapsed);
    printf("Throughput:       %.2f MB/s\n", throughput);
    printf("Avg latency:      %.3f ms/parse\n", avg_latency_ms);
    
    // Performance target check
    printf("\n=== TARGET CHECK ===\n");
    if (throughput >= 200.0) {
        printf("✅ EXCELLENT: %.0f%% of 200 MB/s target\n", (throughput / 200.0) * 100.0);
    } else if (throughput >= 100.0) {
        printf("✅ GOOD: %.0f%% of target\n", (throughput / 200.0) * 100.0);
    } else {
        printf("⚠️  SLOW: Only %.0f%% of 200 MB/s target\n", (throughput / 200.0) * 100.0);
    }
    
    free(json);
    return 0;
}
