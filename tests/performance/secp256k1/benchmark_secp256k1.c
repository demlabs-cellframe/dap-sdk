/*
 * secp256k1 ECDSA Performance Benchmark
 * 
 * Compares DAP native implementation with:
 *   - bitcoin-core/secp256k1 (reference)
 *   - OpenSSL ECDSA
 * 
 * Tests:
 *   - Key generation (pubkey from privkey)
 *   - ECDSA signing
 *   - ECDSA verification
 *   - Signature serialization/parsing
 * 
 * Build with -DBENCHMARK_SECP256K1_COMPETITORS=ON to include competitors.
 * Run ./download_competitors.sh first to download competitor libraries.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <stdbool.h>

#include "dap_common.h"
#include "rand/dap_rand.h"

// DAP secp256k1 (when implemented)
// #include "dap_secp256k1.h"

// Current 3rdparty secp256k1 (baseline until DAP implementation is ready)
#include "secp256k1.h"

#ifdef BENCHMARK_SECP256K1_BITCOIN_CORE
// bitcoin-core/secp256k1 competitor
// Already included above as baseline
#define HAVE_BITCOIN_CORE_COMPETITOR 1
#endif

#ifdef BENCHMARK_OPENSSL_ECDSA
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#define HAVE_OPENSSL_COMPETITOR 1
#endif

// =============================================================================
// Benchmark Configuration
// =============================================================================

#define WARMUP_ITERATIONS       100
#define BENCHMARK_ITERATIONS    10000
#define NUM_TEST_KEYS           100

// Test data
static uint8_t g_privkeys[NUM_TEST_KEYS][32];
static uint8_t g_messages[NUM_TEST_KEYS][32];
static secp256k1_context *g_ctx = NULL;

// =============================================================================
// Timing Utilities
// =============================================================================

static inline uint64_t get_time_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static inline double ns_to_us(uint64_t ns) {
    return (double)ns / 1000.0;
}

static inline double ops_per_sec(int count, uint64_t ns) {
    return (double)count / ((double)ns / 1000000000.0);
}

// =============================================================================
// Test Data Generation
// =============================================================================

static void generate_test_data(void) {
    printf("Generating test data (%d keys, %d messages)...\n", NUM_TEST_KEYS, NUM_TEST_KEYS);
    
    g_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (!g_ctx) {
        fprintf(stderr, "Failed to create secp256k1 context\n");
        exit(1);
    }
    
    // Randomize context
    uint8_t seed[32];
    randombytes(seed, sizeof(seed));
    secp256k1_context_randomize(g_ctx, seed);
    
    // Generate valid private keys
    for (int i = 0; i < NUM_TEST_KEYS; i++) {
        do {
            randombytes(g_privkeys[i], 32);
        } while (!secp256k1_ec_seckey_verify(g_ctx, g_privkeys[i]));
        
        // Generate random message hashes
        randombytes(g_messages[i], 32);
    }
    
    printf("Test data generated.\n\n");
}

static void cleanup_test_data(void) {
    if (g_ctx) {
        secp256k1_context_destroy(g_ctx);
        g_ctx = NULL;
    }
}

// =============================================================================
// Benchmark: Key Generation (pubkey from privkey)
// =============================================================================

typedef struct {
    const char *name;
    double keygen_us;
    double sign_us;
    double verify_us;
    int ops_count;
} benchmark_result_t;

static void benchmark_keygen_baseline(benchmark_result_t *result) {
    secp256k1_pubkey pubkey;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        secp256k1_ec_pubkey_create(g_ctx, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        secp256k1_ec_pubkey_create(g_ctx, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->name = "secp256k1 (3rdparty)";
    result->keygen_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    result->ops_count = BENCHMARK_ITERATIONS;
}

static void benchmark_sign_baseline(benchmark_result_t *result) {
    secp256k1_ecdsa_signature sig;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        secp256k1_ecdsa_sign(g_ctx, &sig, g_messages[i % NUM_TEST_KEYS], 
                            g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        secp256k1_ecdsa_sign(g_ctx, &sig, g_messages[i % NUM_TEST_KEYS], 
                            g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    end = get_time_ns();
    
    result->sign_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}

static void benchmark_verify_baseline(benchmark_result_t *result) {
    // Pre-generate signatures and pubkeys
    secp256k1_ecdsa_signature sigs[NUM_TEST_KEYS];
    secp256k1_pubkey pubkeys[NUM_TEST_KEYS];
    
    for (int i = 0; i < NUM_TEST_KEYS; i++) {
        secp256k1_ec_pubkey_create(g_ctx, &pubkeys[i], g_privkeys[i]);
        secp256k1_ecdsa_sign(g_ctx, &sigs[i], g_messages[i], g_privkeys[i], NULL, NULL);
    }
    
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        secp256k1_ecdsa_verify(g_ctx, &sigs[i % NUM_TEST_KEYS], 
                              g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        secp256k1_ecdsa_verify(g_ctx, &sigs[i % NUM_TEST_KEYS], 
                              g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->verify_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}

#ifdef HAVE_OPENSSL_COMPETITOR
static void benchmark_openssl(benchmark_result_t *result) {
    result->name = "OpenSSL ECDSA";
    
    // Create EC_KEY
    EC_KEY *eckey = EC_KEY_new_by_curve_name(NID_secp256k1);
    if (!eckey) {
        fprintf(stderr, "OpenSSL: Failed to create EC_KEY\n");
        return;
    }
    
    uint64_t start, end;
    
    // Keygen benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        EC_KEY_generate_key(eckey);
    }
    end = get_time_ns();
    result->keygen_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    
    // Generate a key for sign/verify
    EC_KEY_generate_key(eckey);
    
    // Sign benchmark
    unsigned char sig[128];
    unsigned int siglen;
    
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        siglen = sizeof(sig);
        ECDSA_sign(0, g_messages[i % NUM_TEST_KEYS], 32, sig, &siglen, eckey);
    }
    end = get_time_ns();
    result->sign_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    
    // Pre-sign for verify
    siglen = sizeof(sig);
    ECDSA_sign(0, g_messages[0], 32, sig, &siglen, eckey);
    
    // Verify benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        ECDSA_verify(0, g_messages[0], 32, sig, siglen, eckey);
    }
    end = get_time_ns();
    result->verify_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    
    result->ops_count = BENCHMARK_ITERATIONS;
    
    EC_KEY_free(eckey);
}
#endif

// =============================================================================
// Verification Tests
// =============================================================================

static bool verify_signatures(void) {
    printf("=== Signature Verification Tests ===\n\n");
    
    secp256k1_pubkey pubkey;
    secp256k1_ecdsa_signature sig;
    int passed = 0, failed = 0;
    
    // Test sign and verify
    for (int i = 0; i < 10; i++) {
        secp256k1_ec_pubkey_create(g_ctx, &pubkey, g_privkeys[i]);
        
        if (secp256k1_ecdsa_sign(g_ctx, &sig, g_messages[i], g_privkeys[i], NULL, NULL) != 1) {
            printf("FAIL: Sign failed for key %d\n", i);
            failed++;
            continue;
        }
        
        if (secp256k1_ecdsa_verify(g_ctx, &sig, g_messages[i], &pubkey) != 1) {
            printf("FAIL: Verify failed for key %d\n", i);
            failed++;
            continue;
        }
        
        // Test with wrong message
        uint8_t wrong_msg[32];
        memcpy(wrong_msg, g_messages[i], 32);
        wrong_msg[0] ^= 0xFF;
        
        if (secp256k1_ecdsa_verify(g_ctx, &sig, wrong_msg, &pubkey) == 1) {
            printf("FAIL: Verify should fail for wrong message %d\n", i);
            failed++;
            continue;
        }
        
        passed++;
    }
    
    printf("Verification tests: %d passed, %d failed\n\n", passed, failed);
    return failed == 0;
}

// =============================================================================
// Results Display
// =============================================================================

static void print_results(benchmark_result_t *results, int count) {
    printf("=== Performance Benchmark Results ===\n\n");
    printf("%-25s %12s %12s %12s\n", "Implementation", "KeyGen(µs)", "Sign(µs)", "Verify(µs)");
    printf("%-25s %12s %12s %12s\n", "-------------------------", "------------", "------------", "------------");
    
    for (int i = 0; i < count; i++) {
        printf("%-25s %12.2f %12.2f %12.2f\n", 
               results[i].name, 
               results[i].keygen_us,
               results[i].sign_us,
               results[i].verify_us);
    }
    
    printf("\n");
    
    // Throughput
    printf("=== Throughput (ops/sec) ===\n\n");
    printf("%-25s %12s %12s %12s\n", "Implementation", "KeyGen", "Sign", "Verify");
    printf("%-25s %12s %12s %12s\n", "-------------------------", "------------", "------------", "------------");
    
    for (int i = 0; i < count; i++) {
        printf("%-25s %12.0f %12.0f %12.0f\n", 
               results[i].name,
               1000000.0 / results[i].keygen_us,
               1000000.0 / results[i].sign_us,
               1000000.0 / results[i].verify_us);
    }
    
    printf("\n");
    
    // Speedup vs baseline
    if (count > 1) {
        printf("=== Speedup vs secp256k1 (3rdparty) ===\n\n");
        printf("%-25s %12s %12s %12s\n", "Implementation", "KeyGen", "Sign", "Verify");
        printf("%-25s %12s %12s %12s\n", "-------------------------", "------------", "------------", "------------");
        
        for (int i = 1; i < count; i++) {
            printf("%-25s %11.2fx %11.2fx %11.2fx\n", 
                   results[i].name,
                   results[0].keygen_us / results[i].keygen_us,
                   results[0].sign_us / results[i].sign_us,
                   results[0].verify_us / results[i].verify_us);
        }
        printf("\n");
    }
}

// =============================================================================
// Main
// =============================================================================

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    printf("====================================================\n");
    printf("secp256k1 ECDSA Performance Benchmark\n");
    printf("====================================================\n\n");
    
    printf("Configuration:\n");
    printf("  Warmup iterations:    %d\n", WARMUP_ITERATIONS);
    printf("  Benchmark iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("  Test keys:            %d\n", NUM_TEST_KEYS);
    printf("\n");
    
    printf("Competitors:\n");
    printf("  secp256k1 (3rdparty): YES (baseline)\n");
#ifdef HAVE_BITCOIN_CORE_COMPETITOR
    printf("  bitcoin-core/secp256k1: YES\n");
#else
    printf("  bitcoin-core/secp256k1: NO (run ./download_competitors.sh)\n");
#endif
#ifdef HAVE_OPENSSL_COMPETITOR
    printf("  OpenSSL ECDSA:        YES\n");
#else
    printf("  OpenSSL ECDSA:        NO\n");
#endif
    printf("\n");
    
    // Initialize
    dap_common_init("benchmark_secp256k1", NULL, NULL);
    generate_test_data();
    
    // Run verification tests
    if (!verify_signatures()) {
        fprintf(stderr, "Verification tests failed!\n");
        cleanup_test_data();
        return 1;
    }
    
    // Benchmarks
    benchmark_result_t results[4];
    int result_count = 0;
    
    printf("Running benchmarks...\n\n");
    
    // Baseline (current 3rdparty secp256k1)
    benchmark_keygen_baseline(&results[result_count]);
    benchmark_sign_baseline(&results[result_count]);
    benchmark_verify_baseline(&results[result_count]);
    result_count++;
    
    // TODO: DAP native implementation benchmark
    // benchmark_dap_secp256k1(&results[result_count]);
    // result_count++;
    
#ifdef HAVE_OPENSSL_COMPETITOR
    benchmark_openssl(&results[result_count]);
    result_count++;
#endif
    
    // Display results
    print_results(results, result_count);
    
    // Cleanup
    cleanup_test_data();
    dap_common_deinit();
    
    printf("Benchmark complete.\n");
    return 0;
}
