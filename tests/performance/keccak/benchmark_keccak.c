/**
 * @file benchmark_keccak.c
 * @brief Keccak/SHA3/SHAKE performance benchmark
 * @details Compares all DAP implementations against competitors:
 *          - DAP Reference C
 *          - DAP SSE2 / AVX2 / AVX-512 (x86)
 *          - DAP NEON / SVE / SVE2 (ARM)
 *          - XKCP (original reference)
 *          - tiny_sha3 (minimal implementation)
 *
 * @author DAP SDK Team
 * @copyright DeM Labs Inc. 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "dap_hash_sha3.h"
#include "dap_hash_keccak.h"
#include "dap_cpu_arch.h"
#include "dap_common.h"

#ifdef BENCHMARK_COMPETITORS
// XKCP - built with native platform optimizations
#include "SimpleFIPS202.h"
#include "KeccakHash.h"
// tiny_sha3 - minimal reference
#include "sha3.h"
#endif

// ============================================================================
// Timing utilities
// ============================================================================

#ifdef _WIN32
#include <windows.h>
static double get_time_us(void) {
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart * 1e6 / (double)freq.QuadPart;
}
#else
static double get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}
#endif

// ============================================================================
// Benchmark configuration
// ============================================================================

typedef struct {
    const char *name;
    size_t size;
} test_size_t;

static const test_size_t g_test_sizes[] = {
    {"64 B",      64},
    {"256 B",     256},
    {"1 KB",      1024},
    {"4 KB",      4096},
    {"64 KB",     65536},
    {"1 MB",      1024 * 1024},
};

#define NUM_SIZES (sizeof(g_test_sizes) / sizeof(g_test_sizes[0]))
#define WARMUP_ITERATIONS 10
#define MIN_TIME_US 100000.0  // Minimum 100ms per benchmark

// ============================================================================
// Implementation registry
// ============================================================================

typedef struct {
    const char *name;
    dap_cpu_arch_t arch;
    int is_competitor;
} impl_entry_t;

// DAP implementations (auto-detected availability)
static impl_entry_t g_dap_impls[] = {
    {"Ref",      DAP_CPU_ARCH_REFERENCE, 0},
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    {"SSE2",     DAP_CPU_ARCH_SSE2,      0},
    {"AVX2",     DAP_CPU_ARCH_AVX2,      0},
    {"AVX-512",  DAP_CPU_ARCH_AVX512,    0},
#elif defined(__arm__) || defined(__aarch64__)
    {"NEON",     DAP_CPU_ARCH_NEON,      0},
#ifdef __aarch64__
    {"SVE",      DAP_CPU_ARCH_SVE,       0},
    {"SVE2",     DAP_CPU_ARCH_SVE2,      0},
#endif
#endif
};

#define NUM_DAP_IMPLS (sizeof(g_dap_impls) / sizeof(g_dap_impls[0]))

// ============================================================================
// Benchmark runners
// ============================================================================

typedef void (*sha3_256_fn)(const uint8_t *in, size_t inlen, uint8_t *out);

static double benchmark_sha3_256(sha3_256_fn fn, const uint8_t *data, size_t size)
{
    uint8_t hash[32];
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        fn(data, size, hash);
    }
    
    // Benchmark
    double start = get_time_us();
    int iterations = 0;
    
    while (get_time_us() - start < MIN_TIME_US) {
        fn(data, size, hash);
        iterations++;
    }
    
    double elapsed = get_time_us() - start;
    double throughput = (double)(size * iterations) / elapsed;  // MB/s
    
    return throughput;
}

// ============================================================================
// Implementation wrappers
// ============================================================================

static void dap_sha3_256_wrapper(const uint8_t *in, size_t inlen, uint8_t *out)
{
    dap_hash_sha3_256_t hash;
    dap_hash_sha3_256(in, inlen, &hash);
    memcpy(out, hash.raw, 32);
}

#ifdef BENCHMARK_COMPETITORS
static void xkcp_sha3_256_wrapper(const uint8_t *in, size_t inlen, uint8_t *out)
{
    SHA3_256(out, in, inlen);
}

static void tiny_sha3_256_wrapper(const uint8_t *in, size_t inlen, uint8_t *out)
{
    sha3(in, inlen, out, 32);
}
#endif

// ============================================================================
// Correctness verification
// ============================================================================

static int verify_implementations(void)
{
    printf("=== Verifying implementation correctness ===\n");
    
    // Suppress log output during verification
    enum dap_log_level saved_log_level = dap_log_level_get();
    dap_log_level_set(L_WARNING);
    
    // Test vectors (empty string)
    const uint8_t empty[] = "";
    const uint8_t expected_sha3_256_empty[] = {
        0xa7, 0xff, 0xc6, 0xf8, 0xbf, 0x1e, 0xd7, 0x66,
        0x51, 0xc1, 0x47, 0x56, 0xa0, 0x61, 0xd6, 0x62,
        0xf5, 0x80, 0xff, 0x4d, 0xe4, 0x3b, 0x49, 0xfa,
        0x82, 0xd8, 0x0a, 0x4b, 0x80, 0xf8, 0x43, 0x4a
    };
    
    // Test "abc"
    const uint8_t abc[] = "abc";
    const uint8_t expected_sha3_256_abc[] = {
        0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2,
        0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
        0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b,
        0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32
    };
    
    dap_hash_sha3_256_t dap_hash;
    
    // Verify all DAP implementations
    for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
        if (dap_cpu_arch_set(g_dap_impls[i].arch) != 0) {
            printf("  [%s] not available, skipping\n", g_dap_impls[i].name);
            continue;
        }
        
        // Test empty
        dap_hash_sha3_256(empty, 0, &dap_hash);
        if (memcmp(dap_hash.raw, expected_sha3_256_empty, 32) != 0) {
            printf("  FAIL: %s SHA3-256(empty) mismatch!\n", g_dap_impls[i].name);
            return -1;
        }
        
        // Test "abc"
        dap_hash_sha3_256(abc, 3, &dap_hash);
        if (memcmp(dap_hash.raw, expected_sha3_256_abc, 32) != 0) {
            printf("  FAIL: %s SHA3-256('abc') mismatch!\n", g_dap_impls[i].name);
            return -1;
        }
        
        printf("  [%s] SHA3-256: OK\n", g_dap_impls[i].name);
    }
    
    // Reset to auto
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
#ifdef BENCHMARK_COMPETITORS
    uint8_t xkcp_hash[32];
    uint8_t tiny_hash[32];
    
    // Verify XKCP
    SHA3_256(xkcp_hash, empty, 0);
    if (memcmp(xkcp_hash, expected_sha3_256_empty, 32) != 0) {
        printf("  FAIL: XKCP SHA3-256(empty) mismatch!\n");
        return -1;
    }
    printf("  [XKCP] SHA3-256: OK\n");
    
    // Verify tiny_sha3
    sha3(empty, 0, tiny_hash, 32);
    if (memcmp(tiny_hash, expected_sha3_256_empty, 32) != 0) {
        printf("  FAIL: tiny_sha3 SHA3-256(empty) mismatch!\n");
        return -1;
    }
    printf("  [tiny_sha3] SHA3-256: OK\n");
#endif
    
    // Restore log level
    dap_log_level_set(saved_log_level);
    
    printf("All verification tests passed!\n\n");
    return 0;
}

// ============================================================================
// Monte Carlo edge case testing
// ============================================================================

#ifdef BENCHMARK_COMPETITORS
// SHA3-256 rate is 136 bytes (1088 bits)
#define SHA3_256_RATE 136

// Simple PRNG for deterministic random data
static uint32_t s_prng_state = 0x12345678;

static uint32_t prng_next(void) {
    s_prng_state ^= s_prng_state << 13;
    s_prng_state ^= s_prng_state >> 17;
    s_prng_state ^= s_prng_state << 5;
    return s_prng_state;
}

static void prng_fill(uint8_t *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        buf[i] = (uint8_t)(prng_next() & 0xFF);
    }
}

static int monte_carlo_edge_cases(void)
{
    printf("\n=== Monte Carlo Edge Case Testing ===\n");
    printf("Comparing DAP implementations against competitors...\n\n");
    
    // Suppress log output during Monte Carlo testing
    enum dap_log_level saved_log_level = dap_log_level_get();
    dap_log_level_set(L_WARNING);
    
    // Edge case sizes based on SHA3-256 rate (136 bytes)
    static const size_t edge_sizes[] = {
        // Boundary cases around rate
        0, 1, 2, 7, 8, 15, 16, 31, 32, 63, 64,
        SHA3_256_RATE - 2, SHA3_256_RATE - 1, SHA3_256_RATE,
        SHA3_256_RATE + 1, SHA3_256_RATE + 2,
        // Multiple blocks
        2 * SHA3_256_RATE - 1, 2 * SHA3_256_RATE, 2 * SHA3_256_RATE + 1,
        3 * SHA3_256_RATE - 1, 3 * SHA3_256_RATE, 3 * SHA3_256_RATE + 1,
        // Larger sizes
        4 * SHA3_256_RATE, 5 * SHA3_256_RATE,
        10 * SHA3_256_RATE, 100 * SHA3_256_RATE,
        // Non-aligned sizes
        127, 128, 129, 255, 256, 257, 511, 512, 513,
        1023, 1024, 1025, 2047, 2048, 2049,
        4095, 4096, 4097, 8191, 8192, 8193,
        // Random-ish sizes
        137, 271, 409, 547, 683, 821, 1091, 1637,
        2729, 4093, 6151, 8209, 12289, 16381,
    };
    
    #define NUM_EDGE_SIZES (sizeof(edge_sizes) / sizeof(edge_sizes[0]))
    #define MONTE_CARLO_ITERATIONS 10  // Random variations per size
    
    // Allocate buffers
    size_t max_size = 100 * SHA3_256_RATE + 4096;
    uint8_t *data = malloc(max_size);
    if (!data) {
        fprintf(stderr, "Failed to allocate buffer\n");
        return -1;
    }
    
    uint8_t dap_hash[32], xkcp_hash[32], tiny_hash[32];
    int total_tests = 0;
    int failed_tests = 0;
    
    // Test deterministic edge cases
    printf("Testing %zu deterministic edge case sizes...\n", NUM_EDGE_SIZES);
    
    for (size_t i = 0; i < NUM_EDGE_SIZES; i++) {
        size_t size = edge_sizes[i];
        if (size > max_size) continue;
        
        // Fill with deterministic pattern
        s_prng_state = (uint32_t)(0xDEADBEEF ^ size);
        prng_fill(data, size);
        
        // Test all DAP implementations
        for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
            if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) {
                continue;
            }
            
            dap_hash_sha3_256_t dap_result;
            dap_hash_sha3_256(data, size, &dap_result);
            memcpy(dap_hash, dap_result.raw, 32);
            
            // Compare with XKCP
            SHA3_256(xkcp_hash, data, size);
            total_tests++;
            
            if (memcmp(dap_hash, xkcp_hash, 32) != 0) {
                printf("  MISMATCH: DAP[%s] vs XKCP at size %zu\n", 
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
            
            // Compare with tiny_sha3
            sha3(data, size, tiny_hash, 32);
            total_tests++;
            
            if (memcmp(dap_hash, tiny_hash, 32) != 0) {
                printf("  MISMATCH: DAP[%s] vs tiny_sha3 at size %zu\n",
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
        }
    }
    
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    // Monte Carlo random tests
    printf("Running %d Monte Carlo random iterations per size...\n", MONTE_CARLO_ITERATIONS);
    
    for (size_t i = 0; i < NUM_EDGE_SIZES; i++) {
        size_t base_size = edge_sizes[i];
        if (base_size > max_size - 100) continue;
        
        for (int iter = 0; iter < MONTE_CARLO_ITERATIONS; iter++) {
            // Random variation around edge size
            s_prng_state = (uint32_t)(0xCAFEBABE ^ base_size ^ (iter * 12345));
            size_t size = base_size + (prng_next() % 16);  // ±15 bytes variation
            if (size > max_size) size = max_size;
            
            prng_fill(data, size);
            
            // Get reference result from XKCP
            SHA3_256(xkcp_hash, data, size);
            
            // Test each DAP implementation
            for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
                if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) {
                    continue;
                }
                
                dap_hash_sha3_256_t dap_result;
                dap_hash_sha3_256(data, size, &dap_result);
                total_tests++;
                
                if (memcmp(dap_result.raw, xkcp_hash, 32) != 0) {
                    printf("  MISMATCH (MC): DAP[%s] at size %zu iter %d\n",
                           g_dap_impls[impl].name, size, iter);
                    failed_tests++;
                }
            }
        }
    }
    
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    // Special edge cases: all zeros, all ones, patterns
    printf("Testing special pattern data...\n");
    
    static const size_t pattern_sizes[] = {
        1, SHA3_256_RATE - 1, SHA3_256_RATE, SHA3_256_RATE + 1,
        1024, 4096
    };
    
    for (size_t pi = 0; pi < sizeof(pattern_sizes)/sizeof(pattern_sizes[0]); pi++) {
        size_t size = pattern_sizes[pi];
        
        // Pattern 1: all zeros
        memset(data, 0x00, size);
        SHA3_256(xkcp_hash, data, size);
        
        for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
            if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) continue;
            
            dap_hash_sha3_256_t dap_result;
            dap_hash_sha3_256(data, size, &dap_result);
            total_tests++;
            
            if (memcmp(dap_result.raw, xkcp_hash, 32) != 0) {
                printf("  MISMATCH (zeros): DAP[%s] at size %zu\n",
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
        }
        
        // Pattern 2: all 0xFF
        memset(data, 0xFF, size);
        SHA3_256(xkcp_hash, data, size);
        
        for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
            if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) continue;
            
            dap_hash_sha3_256_t dap_result;
            dap_hash_sha3_256(data, size, &dap_result);
            total_tests++;
            
            if (memcmp(dap_result.raw, xkcp_hash, 32) != 0) {
                printf("  MISMATCH (0xFF): DAP[%s] at size %zu\n",
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
        }
        
        // Pattern 3: alternating 0xAA/0x55
        for (size_t j = 0; j < size; j++) {
            data[j] = (j & 1) ? 0x55 : 0xAA;
        }
        SHA3_256(xkcp_hash, data, size);
        
        for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
            if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) continue;
            
            dap_hash_sha3_256_t dap_result;
            dap_hash_sha3_256(data, size, &dap_result);
            total_tests++;
            
            if (memcmp(dap_result.raw, xkcp_hash, 32) != 0) {
                printf("  MISMATCH (alt): DAP[%s] at size %zu\n",
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
        }
        
        // Pattern 4: sequential bytes
        for (size_t j = 0; j < size; j++) {
            data[j] = (uint8_t)(j & 0xFF);
        }
        SHA3_256(xkcp_hash, data, size);
        
        for (size_t impl = 0; impl < NUM_DAP_IMPLS; impl++) {
            if (dap_cpu_arch_set(g_dap_impls[impl].arch) != 0) continue;
            
            dap_hash_sha3_256_t dap_result;
            dap_hash_sha3_256(data, size, &dap_result);
            total_tests++;
            
            if (memcmp(dap_result.raw, xkcp_hash, 32) != 0) {
                printf("  MISMATCH (seq): DAP[%s] at size %zu\n",
                       g_dap_impls[impl].name, size);
                failed_tests++;
            }
        }
    }
    
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    free(data);
    
    // Restore log level
    dap_log_level_set(saved_log_level);
    
    printf("\n");
    printf("Monte Carlo Results: %d tests, %d passed, %d failed\n",
           total_tests, total_tests - failed_tests, failed_tests);
    
    if (failed_tests > 0) {
        printf("MONTE CARLO TESTING FAILED!\n");
        return -1;
    }
    
    printf("All Monte Carlo tests PASSED!\n\n");
    return 0;
}
#endif

// ============================================================================
// Main benchmark
// ============================================================================

static void print_separator(int num_cols)
{
    for (int i = 0; i < num_cols; i++) {
        printf("%-10s", "--------");
    }
    printf("\n");
}

static void run_benchmarks(void)
{
    printf("=== SHA3-256 Performance Benchmark ===\n");
    printf("Auto-detected best: %s\n\n", dap_hash_keccak_get_impl_name());
    
    // Suppress log output during benchmarking for clean output
    enum dap_log_level saved_log_level = dap_log_level_get();
    dap_log_level_set(L_WARNING);
    
    // Allocate test data
    size_t max_size = g_test_sizes[NUM_SIZES - 1].size;
    uint8_t *data = malloc(max_size);
    if (!data) {
        fprintf(stderr, "Failed to allocate %zu bytes\n", max_size);
        return;
    }
    
    // Fill with pseudo-random data
    for (size_t i = 0; i < max_size; i++) {
        data[i] = (uint8_t)(i * 7 + 13);
    }
    
    // Count available implementations
    int available_impls[NUM_DAP_IMPLS];
    int num_available = 0;
    
    for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
        available_impls[i] = (dap_cpu_arch_set(g_dap_impls[i].arch) == 0);
        if (available_impls[i]) {
            num_available++;
        }
    }
    dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
    
    int num_competitors = 0;
#ifdef BENCHMARK_COMPETITORS
    num_competitors = 2;  // XKCP, tiny_sha3
#endif
    
    int total_cols = 1 + num_available + num_competitors;  // Size + impls
    
    // Print header
    printf("%-10s", "Size");
    for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
        if (available_impls[i]) {
            printf("%-10s", g_dap_impls[i].name);
        }
    }
#ifdef BENCHMARK_COMPETITORS
    printf("%-10s%-10s", "XKCP", "tiny_sha3");
#endif
    printf("\n");
    print_separator(total_cols);
    
    // Results storage for summary
    double results[NUM_SIZES][NUM_DAP_IMPLS + 2];  // +2 for competitors
    memset(results, 0, sizeof(results));
    
    // Run benchmarks for each size
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        const char *name = g_test_sizes[sz].name;
        size_t size = g_test_sizes[sz].size;
        
        printf("%-10s", name);
        
        // Benchmark each DAP implementation
        int col = 0;
        for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
            if (!available_impls[i]) {
                continue;
            }
            
            dap_cpu_arch_set(g_dap_impls[i].arch);
            double tp = benchmark_sha3_256(dap_sha3_256_wrapper, data, size);
            results[sz][col++] = tp;
            printf("%-10.1f", tp);
        }
        
        dap_cpu_arch_set(DAP_CPU_ARCH_AUTO);
        
#ifdef BENCHMARK_COMPETITORS
        // XKCP
        double xkcp_tp = benchmark_sha3_256(xkcp_sha3_256_wrapper, data, size);
        results[sz][col++] = xkcp_tp;
        printf("%-10.1f", xkcp_tp);
        
        // tiny_sha3
        double tiny_tp = benchmark_sha3_256(tiny_sha3_256_wrapper, data, size);
        results[sz][col++] = tiny_tp;
        printf("%-10.1f", tiny_tp);
#endif
        
        printf("\n");
    }
    
    print_separator(total_cols);
    printf("\nThroughput in MB/s (higher is better)\n\n");
    
    // Print summary - best implementation per size
    printf("=== Best Implementation Per Size ===\n");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        double best = 0;
        int best_idx = -1;
        int col = 0;
        
        for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
            if (available_impls[i]) {
                if (results[sz][col] > best) {
                    best = results[sz][col];
                    best_idx = (int)i;
                }
                col++;
            }
        }
        
#ifdef BENCHMARK_COMPETITORS
        // Check XKCP
        if (results[sz][col] > best) {
            best = results[sz][col];
            best_idx = 100;  // XKCP
        }
        col++;
        // Check tiny_sha3
        if (results[sz][col] > best) {
            best = results[sz][col];
            best_idx = 101;  // tiny_sha3
        }
        col++;
#endif
        
        const char *best_name = "Unknown";
        if (best_idx == 100) {
            best_name = "XKCP";
        } else if (best_idx == 101) {
            best_name = "tiny_sha3";
        } else if (best_idx >= 0 && best_idx < (int)NUM_DAP_IMPLS) {
            best_name = g_dap_impls[best_idx].name;
        }
        
        printf("  %-10s: %s (%.1f MB/s)\n", g_test_sizes[sz].name, best_name, best);
    }
    
    // Print speedup vs reference for ALL sizes
    printf("\n=== Speedup vs Reference (all sizes) ===\n");
    
    // Header
    printf("%-10s", "Impl");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        printf("%9s ", g_test_sizes[sz].name);
    }
    printf("\n");
    printf("%-10s", "--------");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        printf("%9s ", "--------");
    }
    printf("\n");
    
    // For each implementation, print speedup across all sizes
    int col_idx = 0;
    for (size_t i = 0; i < NUM_DAP_IMPLS; i++) {
        if (!available_impls[i]) continue;
        
        printf("%-10s", g_dap_impls[i].name);
        for (size_t sz = 0; sz < NUM_SIZES; sz++) {
            double ref_tp = results[sz][0];  // Reference is column 0
            double speedup = results[sz][col_idx] / ref_tp;
            printf("%7.2fx  ", speedup);
        }
        printf("\n");
        col_idx++;
    }
    
#ifdef BENCHMARK_COMPETITORS
    // XKCP speedup
    printf("%-10s", "XKCP");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        double ref_tp = results[sz][0];
        double speedup = results[sz][col_idx] / ref_tp;
        printf("%7.2fx  ", speedup);
    }
    printf("\n");
    col_idx++;
    
    // tiny_sha3 speedup
    printf("%-10s", "tiny_sha3");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        double ref_tp = results[sz][0];
        double speedup = results[sz][col_idx] / ref_tp;
        printf("%7.2fx  ", speedup);
    }
    printf("\n");
#endif
    
    printf("%-10s", "--------");
    for (size_t sz = 0; sz < NUM_SIZES; sz++) {
        printf("%9s ", "--------");
    }
    printf("\n");
    
    // Restore log level
    dap_log_level_set(saved_log_level);
    
    free(data);
}

// ============================================================================
// Entry point
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    printf("========================================\n");
    printf("   DAP Keccak/SHA3 Performance Test\n");
    printf("========================================\n\n");
    
    // Verify correctness first
    if (verify_implementations() != 0) {
        fprintf(stderr, "Correctness verification failed!\n");
        return 1;
    }
    
#ifdef BENCHMARK_COMPETITORS
    // Monte Carlo edge case testing
    if (monte_carlo_edge_cases() != 0) {
        fprintf(stderr, "Monte Carlo testing failed!\n");
        return 1;
    }
#endif
    
    // Run benchmarks
    run_benchmarks();
    
    return 0;
}
