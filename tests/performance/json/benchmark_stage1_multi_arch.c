/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * @file benchmark_stage1_multi_arch.c
 * @brief Multi-architecture Stage 1 Performance Benchmark
 * @details Benchmarks all SIMD implementations (Reference, SSE2, AVX2, AVX-512, NEON)
 * 
 * Metrics:
 *   - Throughput (MB/s)
 *   - Latency (ns/byte)
 *   - Iterations per second
 * 
 * Test scenarios:
 *   - Small JSON (< 1 KB)
 *   - Medium JSON (10-100 KB)
 *   - Large JSON (1+ MB)
 */

#include "dap_common.h"
#include "dap_json.h"
#include "dap_cpu_arch.h"
#include "internal/dap_json_stage1.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

#define LOG_TAG "benchmark_stage1"

/* ========================================================================== */
/*                          BENCHMARK CONFIGURATION                           */
/* ========================================================================== */

#define WARMUP_ITERATIONS 100
#define BENCHMARK_ITERATIONS 1000
#define MIN_BENCHMARK_TIME_NS (1000000000LL) // 1 second minimum

/* ========================================================================== */
/*                            BENCHMARK INFRASTRUCTURE                        */
/* ========================================================================== */

typedef struct {
    const char *name;
    size_t size_bytes;
    int64_t elapsed_ns;
    size_t iterations;
    size_t tokens;
    double throughput_mbps;
    double latency_ns_per_byte;
    double iterations_per_sec;
} benchmark_result_t;

/**
 * @brief Get current time in nanoseconds
 */
static inline int64_t get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/**
 * @brief Run benchmark for specific architecture
 */
static benchmark_result_t benchmark_architecture(
    dap_cpu_arch_t a_arch,
    const char *a_json_input,
    size_t a_input_len
)
{
    benchmark_result_t result = {0};
    result.name = dap_cpu_arch_get_name(a_arch);
    result.size_bytes = a_input_len;
    
    // Set architecture
    if (dap_json_set_arch(a_arch) != 0) {
        log_it(L_ERROR, "[%s] Architecture not available", result.name);
        return result;
    }
    
    // Create parser (reusable)
    dap_json_stage1_t *parser = dap_json_stage1_new(a_input_len / 4); // Estimate capacity
    if (!parser) {
        log_it(L_ERROR, "[%s] Failed to create parser", result.name);
        return result;
    }
    
    // Warmup
    for (size_t i = 0; i < WARMUP_ITERATIONS; i++) {
        dap_json_stage1_reset(parser, (const uint8_t*)a_json_input, a_input_len);
        dap_json_stage1_run(parser);
    }
    
    // Benchmark
    int64_t start_ns = get_time_ns();
    size_t iterations = 0;
    int64_t elapsed_ns = 0;
    
    while (elapsed_ns < MIN_BENCHMARK_TIME_NS || iterations < BENCHMARK_ITERATIONS) {
        dap_json_stage1_reset(parser, (const uint8_t*)a_json_input, a_input_len);
        int ret = dap_json_stage1_run(parser);
        
        if (ret != 0) {
            log_it(L_ERROR, "[%s] Parse error at iteration %zu", result.name, iterations);
            break;
        }
        
        iterations++;
        elapsed_ns = get_time_ns() - start_ns;
        
        // Safety limit
        if (iterations >= BENCHMARK_ITERATIONS * 100) {
            break;
        }
    }
    
    result.iterations = iterations;
    result.elapsed_ns = elapsed_ns;
    result.tokens = dap_json_stage1_get_token_count(parser);
    
    // Calculate metrics
    if (iterations > 0 && elapsed_ns > 0) {
        double seconds = (double)elapsed_ns / 1e9;
        double total_mb = ((double)a_input_len * iterations) / (1024.0 * 1024.0);
        result.throughput_mbps = total_mb / seconds;
        result.latency_ns_per_byte = (double)elapsed_ns / (double)(a_input_len * iterations);
        result.iterations_per_sec = (double)iterations / seconds;
    }
    
    dap_json_stage1_free(parser);
    
    return result;
}

/* ========================================================================== */
/*                               TEST DATA                                    */
/* ========================================================================== */

static const char *TEST_JSON_SMALL = 
    "{\"name\":\"John\",\"age\":30,\"city\":\"New York\"}";

static const char *TEST_JSON_MEDIUM = 
    "{"
    "\"users\":["
    "{\"id\":1,\"name\":\"Alice\",\"email\":\"alice@example.com\",\"age\":25},"
    "{\"id\":2,\"name\":\"Bob\",\"email\":\"bob@example.com\",\"age\":30},"
    "{\"id\":3,\"name\":\"Charlie\",\"email\":\"charlie@example.com\",\"age\":35},"
    "{\"id\":4,\"name\":\"Diana\",\"email\":\"diana@example.com\",\"age\":28},"
    "{\"id\":5,\"name\":\"Eve\",\"email\":\"eve@example.com\",\"age\":32}"
    "],"
    "\"metadata\":{"
    "\"version\":\"1.0\",\"timestamp\":1234567890,\"checksum\":\"abc123\""
    "}"
    "}";

/**
 * @brief Generate large JSON test data
 */
static char* generate_large_json(size_t *out_len)
{
    const size_t target_size = 1024 * 1024; // 1 MB
    const size_t item_size = 100;
    const size_t num_items = target_size / item_size;
    
    char *json = DAP_NEW_Z_SIZE(char, target_size + 1000);
    if (!json) {
        return NULL;
    }
    
    size_t offset = 0;
    
    offset += sprintf(json + offset, "{\"items\":[");
    
    for (size_t i = 0; i < num_items; i++) {
        offset += sprintf(json + offset,
            "{\"id\":%zu,\"name\":\"Item%zu\",\"value\":%zu,\"active\":true}%s",
            i, i, i * 100, (i < num_items - 1) ? "," : "");
    }
    
    offset += sprintf(json + offset, "]}");
    
    *out_len = offset;
    return json;
}

/* ========================================================================== */
/*                               MAIN BENCHMARK                               */
/* ========================================================================== */

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    
    // Set log level to DEBUG as required
    dap_log_level_set(L_DEBUG);
    
    log_it(L_INFO, "=== Stage 1 Multi-Architecture Benchmark ===");
    log_it(L_INFO, "Warmup: %d iterations, Benchmark: minimum %d iterations or 1 second",
           WARMUP_ITERATIONS, BENCHMARK_ITERATIONS);
    printf("\n");
    
    // Initialize JSON module
    dap_json_init();
    
    // Test scenarios
    struct {
        const char *name;
        const char *data;
        size_t len;
        bool needs_free;
    } scenarios[] = {
        {"Small (50 B)", TEST_JSON_SMALL, strlen(TEST_JSON_SMALL), false},
        {"Medium (500 B)", TEST_JSON_MEDIUM, strlen(TEST_JSON_MEDIUM), false},
        {"Large (1 MB)", NULL, 0, true} // Will be generated
    };
    
    // Generate large JSON
    scenarios[2].data = generate_large_json(&scenarios[2].len);
    if (!scenarios[2].data) {
        log_it(L_ERROR, "Failed to generate large JSON");
        return 1;
    }
    
    // Architectures to benchmark
    dap_cpu_arch_t architectures[] = {
        DAP_CPU_ARCH_REFERENCE,
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        DAP_CPU_ARCH_SSE2,
        DAP_CPU_ARCH_AVX2,
        DAP_CPU_ARCH_AVX512,
#elif defined(__arm__) || defined(__aarch64__)
        DAP_CPU_ARCH_NEON,
#endif
    };
    const size_t num_archs = sizeof(architectures) / sizeof(architectures[0]);
    
    // Run benchmarks
    for (size_t s = 0; s < sizeof(scenarios) / sizeof(scenarios[0]); s++) {
        log_it(L_INFO, "--- Scenario: %s ---", scenarios[s].name);
        
        benchmark_result_t baseline = {0};
        
        for (size_t a = 0; a < num_archs; a++) {
            if (!dap_cpu_arch_is_available(architectures[a])) {
                continue;
            }
            
            benchmark_result_t result = benchmark_architecture(
                architectures[a],
                scenarios[s].data,
                scenarios[s].len
            );
            
            if (result.iterations > 0) {
                // Save baseline for speedup calculation
                if (architectures[a] == DAP_CPU_ARCH_REFERENCE) {
                    baseline = result;
                }
                
                double speedup = 1.0;
                if (baseline.throughput_mbps > 0) {
                    speedup = result.throughput_mbps / baseline.throughput_mbps;
                }
                
                log_it(L_INFO, "  [%-12s] %8.2f MB/s | %6.2f ns/byte | %zu tokens | %.2fx speedup",
                       result.name,
                       result.throughput_mbps,
                       result.latency_ns_per_byte,
                       result.tokens,
                       speedup);
            }
        }
        
        printf("\n");
    }
    
    // Cleanup
    if (scenarios[2].needs_free) {
        DAP_DELETE(scenarios[2].data);
    }
    
    log_it(L_INFO, "=== Benchmark Complete ===");
    
    return 0;
}

