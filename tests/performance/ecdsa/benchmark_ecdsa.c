/*
 * ECDSA Performance Benchmark
 * 
 * Benchmarks DAP native ECDSA implementation (dap_sig_ecdsa).
 * Optionally compares with competitors:
 *   - bitcoin-core/secp256k1 (if downloaded)
 *   - OpenSSL ECDSA (if available)
 * 
 * Tests:
 *   - Key generation (pubkey from privkey)
 *   - ECDSA signing
 *   - ECDSA verification
 *   - Scalar arithmetic (architecture-specific optimizations)
 * 
 * Build with -DBENCHMARK_ECDSA_COMPETITORS=ON to include competitors.
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
#include "dap_sig_ecdsa.h"
#include "dap_hash_sha2.h"

// Internal headers for debugging
#include "sig_ecdsa/ecdsa_field.h"
#include "sig_ecdsa/ecdsa_group.h"
#include "sig_ecdsa/ecdsa_scalar.h"
#include "sig_ecdsa/ecdsa_impl.h"
#include "sig_ecdsa/arch/ecdsa_field_arch.h"  // For dispatch control

// Architecture-specific scalar implementations
#ifdef BENCHMARK_SCALAR_ARCH
#include "sig_ecdsa/arch/ecdsa_scalar_mul_arch.h"
#include "sig_ecdsa/arch/ecdsa_field_arch.h"
#endif

// Competitor: bitcoin-core/secp256k1 (downloaded via download_competitors.sh)
#ifdef HAVE_SECP256K1_COMPETITOR
#include "secp256k1.h"
static secp256k1_context *g_secp_ctx = NULL;

// External functions from secp256k1_scalar_bench.c for internal scalar benchmarking
extern void secp256k1_bench_scalar_set_b32(void *r, const unsigned char *b32);
extern void secp256k1_bench_scalar_mul(void *r, const void *a, const void *b);
extern void secp256k1_bench_scalar_mul_shift_var(void *r, const void *a, const void *b, unsigned int shift);
extern size_t secp256k1_bench_scalar_size(void);
#endif

#ifdef HAVE_OPENSSL_COMPETITOR
#include <openssl/ecdsa.h>
#include <openssl/obj_mac.h>
#include <openssl/ec.h>
#include <openssl/bn.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>
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

// =============================================================================
// Test Data Generation
// =============================================================================

static void generate_test_data(void) {
    printf("Generating test data (%d keys, %d messages)...\n", NUM_TEST_KEYS, NUM_TEST_KEYS);
    
    // Generate valid private keys using DAP native
    for (int i = 0; i < NUM_TEST_KEYS; i++) {
        do {
            randombytes(g_privkeys[i], 32);
        } while (!dap_sig_ecdsa_seckey_verify(NULL, g_privkeys[i]));
        
        // Generate random message hashes
        randombytes(g_messages[i], 32);
    }
    
#ifdef HAVE_SECP256K1_COMPETITOR
    g_secp_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    if (g_secp_ctx) {
        uint8_t seed[32];
        randombytes(seed, sizeof(seed));
        (void)secp256k1_context_randomize(g_secp_ctx, seed);
    }
#endif
    
    printf("Test data generated.\n\n");
}

static void cleanup_test_data(void) {
#ifdef HAVE_SECP256K1_COMPETITOR
    if (g_secp_ctx) {
        secp256k1_context_destroy(g_secp_ctx);
        g_secp_ctx = NULL;
    }
#endif
}

// =============================================================================
// Benchmark Results
// =============================================================================

typedef struct {
    const char *name;
    double keygen_us;
    double sign_us;
    double verify_us;
    int ops_count;
} benchmark_result_t;

// =============================================================================
// DAP Native Benchmarks
// =============================================================================

static void benchmark_dap_keygen(benchmark_result_t *result) {
    dap_sig_ecdsa_pubkey_t pubkey;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        dap_sig_ecdsa_pubkey_create(NULL, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        dap_sig_ecdsa_pubkey_create(NULL, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->name = "DAP Native";
    result->keygen_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    result->ops_count = BENCHMARK_ITERATIONS;
}

static void benchmark_dap_sign(benchmark_result_t *result) {
    dap_sig_ecdsa_signature_t sig;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        dap_sig_ecdsa_sign(NULL, &sig, g_messages[i % NUM_TEST_KEYS], 
                          g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        dap_sig_ecdsa_sign(NULL, &sig, g_messages[i % NUM_TEST_KEYS], 
                          g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    end = get_time_ns();
    
    result->sign_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}

static void benchmark_dap_verify(benchmark_result_t *result) {
    // Pre-generate signatures and pubkeys
    dap_sig_ecdsa_signature_t sigs[NUM_TEST_KEYS];
    dap_sig_ecdsa_pubkey_t pubkeys[NUM_TEST_KEYS];
    
    for (int i = 0; i < NUM_TEST_KEYS; i++) {
        dap_sig_ecdsa_pubkey_create(NULL, &pubkeys[i], g_privkeys[i]);
        dap_sig_ecdsa_sign(NULL, &sigs[i], g_messages[i], g_privkeys[i], NULL, NULL);
    }
    
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        dap_sig_ecdsa_verify(NULL, &sigs[i % NUM_TEST_KEYS], 
                            g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        dap_sig_ecdsa_verify(NULL, &sigs[i % NUM_TEST_KEYS], 
                            g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->verify_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}

// =============================================================================
// Competitor Benchmarks: bitcoin-core/secp256k1
// =============================================================================

#ifdef HAVE_SECP256K1_COMPETITOR
static void benchmark_secp256k1_keygen(benchmark_result_t *result) {
    secp256k1_pubkey pubkey;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        (void)secp256k1_ec_pubkey_create(g_secp_ctx, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        (void)secp256k1_ec_pubkey_create(g_secp_ctx, &pubkey, g_privkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->name = "bitcoin-core/secp256k1";
    result->keygen_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
    result->ops_count = BENCHMARK_ITERATIONS;
}

static void benchmark_secp256k1_sign(benchmark_result_t *result) {
    secp256k1_ecdsa_signature sig;
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        (void)secp256k1_ecdsa_sign(g_secp_ctx, &sig, g_messages[i % NUM_TEST_KEYS], 
                                   g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        (void)secp256k1_ecdsa_sign(g_secp_ctx, &sig, g_messages[i % NUM_TEST_KEYS], 
                                   g_privkeys[i % NUM_TEST_KEYS], NULL, NULL);
    }
    end = get_time_ns();
    
    result->sign_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}

static void benchmark_secp256k1_verify(benchmark_result_t *result) {
    secp256k1_ecdsa_signature sigs[NUM_TEST_KEYS];
    secp256k1_pubkey pubkeys[NUM_TEST_KEYS];
    
    for (int i = 0; i < NUM_TEST_KEYS; i++) {
        (void)secp256k1_ec_pubkey_create(g_secp_ctx, &pubkeys[i], g_privkeys[i]);
        (void)secp256k1_ecdsa_sign(g_secp_ctx, &sigs[i], g_messages[i], g_privkeys[i], NULL, NULL);
    }
    
    uint64_t start, end;
    
    // Warmup
    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        (void)secp256k1_ecdsa_verify(g_secp_ctx, &sigs[i % NUM_TEST_KEYS], 
                                     g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    
    // Benchmark
    start = get_time_ns();
    for (int i = 0; i < BENCHMARK_ITERATIONS; i++) {
        (void)secp256k1_ecdsa_verify(g_secp_ctx, &sigs[i % NUM_TEST_KEYS], 
                                     g_messages[i % NUM_TEST_KEYS], &pubkeys[i % NUM_TEST_KEYS]);
    }
    end = get_time_ns();
    
    result->verify_us = ns_to_us(end - start) / BENCHMARK_ITERATIONS;
}
#endif

// =============================================================================
// Competitor Benchmarks: OpenSSL
// =============================================================================

#ifdef HAVE_OPENSSL_COMPETITOR
static void benchmark_openssl(benchmark_result_t *result) {
    result->name = "OpenSSL ECDSA";
    
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

static bool verify_correctness(void) {
    printf("=== Correctness Verification ===\n\n");
    
    dap_sig_ecdsa_pubkey_t pubkey;
    dap_sig_ecdsa_signature_t sig;
    int passed = 0, failed = 0;
    
    // Test sign and verify
    for (int i = 0; i < 10; i++) {
        if (!dap_sig_ecdsa_pubkey_create(NULL, &pubkey, g_privkeys[i])) {
            printf("FAIL: Pubkey create failed for key %d\n", i);
            failed++;
            continue;
        }
        
        if (!dap_sig_ecdsa_sign(NULL, &sig, g_messages[i], g_privkeys[i], NULL, NULL)) {
            printf("FAIL: Sign failed for key %d\n", i);
            failed++;
            continue;
        }
        
        if (!dap_sig_ecdsa_verify(NULL, &sig, g_messages[i], &pubkey)) {
            printf("FAIL: Verify failed for key %d\n", i);
            failed++;
            continue;
        }
        
        // Test with wrong message
        uint8_t wrong_msg[32];
        memcpy(wrong_msg, g_messages[i], 32);
        wrong_msg[0] ^= 0xFF;
        
        if (dap_sig_ecdsa_verify(NULL, &sig, wrong_msg, &pubkey)) {
            printf("FAIL: Verify should fail for wrong message %d\n", i);
            failed++;
            continue;
        }
        
        passed++;
    }
    
    printf("Correctness tests: %d passed, %d failed\n\n", passed, failed);
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
    
    // Speedup vs DAP Native
    if (count > 1) {
        printf("=== Speedup vs DAP Native ===\n\n");
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
// Architecture-specific Scalar Multiplication Benchmark
// =============================================================================

#ifdef BENCHMARK_SCALAR_ARCH

// Scalar benchmark result
typedef struct {
    const char *name;
    const char *description;
    double time_us;
    double ops_per_sec;
    bool available;
} scalar_bench_result_t;

#define MAX_SCALAR_RESULTS 16

static void benchmark_scalar_arch(void) {
    printf("\n====================================================\n");
    printf("Scalar Multiplication Architecture Benchmark\n");
    printf("====================================================\n\n");
    
    scalar_bench_result_t results[MAX_SCALAR_RESULTS];
    int result_count = 0;
    
    const int SCALAR_ITERATIONS = 100000;
    
    // =========================================================================
    // DAP Native Implementations
    // =========================================================================
    
    printf("--- DAP Native Scalar Implementations ---\n\n");
    
    // Initialize dispatcher
    ecdsa_scalar_dispatch_init();
    
    // Get all DAP implementations
    size_t num_impls;
    const ecdsa_scalar_impl_info_t *impls = ecdsa_scalar_get_all_impls(&num_impls);
    
    printf("Available DAP implementations:\n");
    for (size_t i = 0; i < num_impls; i++) {
        printf("  [%s] %s: %s\n", 
               impls[i].available ? "OK" : "--",
               impls[i].name, 
               impls[i].description);
    }
    printf("\n");
    
    // Create test scalars for DAP
    ecdsa_scalar_t a, b, r;
    uint8_t a_bytes[32], b_bytes[32];
    randombytes(a_bytes, 32);
    randombytes(b_bytes, 32);
    
    extern void ecdsa_scalar_set_b32(ecdsa_scalar_t *r, const uint8_t *b32, int *overflow);
    ecdsa_scalar_set_b32(&a, a_bytes, NULL);
    ecdsa_scalar_set_b32(&b, b_bytes, NULL);
    
    // Benchmark each DAP implementation
    for (size_t i = 0; i < num_impls && result_count < MAX_SCALAR_RESULTS; i++) {
        if (!impls[i].available || !impls[i].mul_shift_384) continue;
        
        // Warmup
        for (int w = 0; w < 1000; w++) {
            impls[i].mul_shift_384(&r, &a, &b);
        }
        
        // Benchmark
        uint64_t start = get_time_ns();
        for (int j = 0; j < SCALAR_ITERATIONS; j++) {
            impls[i].mul_shift_384(&r, &a, &b);
        }
        uint64_t elapsed = get_time_ns() - start;
        
        double time_us = ns_to_us(elapsed);
        double ops_per_sec = (double)SCALAR_ITERATIONS / (time_us / 1e6);
        
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "DAP %s", impls[i].name);
        
        results[result_count].name = strdup(name_buf);
        results[result_count].description = impls[i].description;
        results[result_count].time_us = time_us;
        results[result_count].ops_per_sec = ops_per_sec;
        results[result_count].available = true;
        result_count++;
    }
    
    // =========================================================================
    // bitcoin-core/secp256k1 Internal Scalar Operations
    // =========================================================================
    
#ifdef HAVE_SECP256K1_COMPETITOR
    printf("--- bitcoin-core/secp256k1 Scalar Operations ---\n\n");
    
    // Verify scalar size matches
    size_t bc_scalar_size = secp256k1_bench_scalar_size();
    printf("  bitcoin-core scalar size: %zu bytes\n", bc_scalar_size);
    printf("  DAP scalar size: %zu bytes\n", sizeof(ecdsa_scalar_t));
    
    // Create bitcoin-core scalars (same layout as DAP: uint64_t[4])
    uint64_t bc_a[4], bc_b[4], bc_r[4];
    secp256k1_bench_scalar_set_b32(bc_a, a_bytes);
    secp256k1_bench_scalar_set_b32(bc_b, b_bytes);
    
    // Benchmark secp256k1_scalar_mul (full modular multiplication)
    // Warmup
    for (int w = 0; w < 1000; w++) {
        secp256k1_bench_scalar_mul(bc_r, bc_a, bc_b);
    }
    
    uint64_t start = get_time_ns();
    for (int j = 0; j < SCALAR_ITERATIONS; j++) {
        secp256k1_bench_scalar_mul(bc_r, bc_a, bc_b);
    }
    uint64_t elapsed = get_time_ns() - start;
    
    double bc_mul_time_us = ns_to_us(elapsed);
    double bc_mul_ops = (double)SCALAR_ITERATIONS / (bc_mul_time_us / 1e6);
    
    results[result_count].name = "bitcoin-core scalar_mul";
    results[result_count].description = "secp256k1 modular multiplication";
    results[result_count].time_us = bc_mul_time_us;
    results[result_count].ops_per_sec = bc_mul_ops;
    results[result_count].available = true;
    result_count++;
    
    // Benchmark secp256k1_scalar_mul_shift_var (GLV helper - shift 384)
    // Warmup
    for (int w = 0; w < 1000; w++) {
        secp256k1_bench_scalar_mul_shift_var(bc_r, bc_a, bc_b, 384);
    }
    
    start = get_time_ns();
    for (int j = 0; j < SCALAR_ITERATIONS; j++) {
        secp256k1_bench_scalar_mul_shift_var(bc_r, bc_a, bc_b, 384);
    }
    elapsed = get_time_ns() - start;
    
    double bc_shift_time_us = ns_to_us(elapsed);
    double bc_shift_ops = (double)SCALAR_ITERATIONS / (bc_shift_time_us / 1e6);
    
    results[result_count].name = "bitcoin-core mul_shift_384";
    results[result_count].description = "secp256k1 GLV mul_shift (shift=384)";
    results[result_count].time_us = bc_shift_time_us;
    results[result_count].ops_per_sec = bc_shift_ops;
    results[result_count].available = true;
    result_count++;
    
    printf("  secp256k1_scalar_mul: available\n");
    printf("  secp256k1_scalar_mul_shift_var: available\n\n");
#else
    printf("--- bitcoin-core/secp256k1: NOT AVAILABLE ---\n\n");
#endif
    
    // =========================================================================
    // OpenSSL BN_mod_mul (Competitor scalar multiplication)
    // =========================================================================
    
#ifdef HAVE_OPENSSL_COMPETITOR
    printf("--- OpenSSL BN Scalar Multiplication ---\n\n");
    
    // secp256k1 order n
    static const char *secp256k1_n_hex = 
        "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";
    
    BN_CTX *bn_ctx = BN_CTX_new();
    BIGNUM *bn_a = BN_new();
    BIGNUM *bn_b = BN_new();
    BIGNUM *bn_r = BN_new();
    BIGNUM *bn_n = BN_new();
    
    BN_hex2bn(&bn_n, secp256k1_n_hex);
    BN_bin2bn(a_bytes, 32, bn_a);
    BN_bin2bn(b_bytes, 32, bn_b);
    
    // Warmup
    for (int w = 0; w < 1000; w++) {
        BN_mod_mul(bn_r, bn_a, bn_b, bn_n, bn_ctx);
    }
    
    // Benchmark OpenSSL BN_mod_mul
    start = get_time_ns();
    for (int j = 0; j < SCALAR_ITERATIONS; j++) {
        BN_mod_mul(bn_r, bn_a, bn_b, bn_n, bn_ctx);
    }
    elapsed = get_time_ns() - start;
    
    double ossl_time_us = ns_to_us(elapsed);
    double ossl_ops_per_sec = (double)SCALAR_ITERATIONS / (ossl_time_us / 1e6);
    
    results[result_count].name = "OpenSSL BN_mod_mul";
    results[result_count].description = "OpenSSL BIGNUM modular multiplication";
    results[result_count].time_us = ossl_time_us;
    results[result_count].ops_per_sec = ossl_ops_per_sec;
    results[result_count].available = true;
    result_count++;
    
    printf("  OpenSSL BN_mod_mul: available\n\n");
    
    BN_free(bn_a);
    BN_free(bn_b);
    BN_free(bn_r);
    BN_free(bn_n);
    BN_CTX_free(bn_ctx);
#else
    printf("--- OpenSSL BN: NOT AVAILABLE ---\n\n");
#endif
    
    // =========================================================================
    // Results Table
    // =========================================================================
    
    printf("====================================================\n");
    printf("Scalar Multiplication Benchmark Results\n");
    printf("(%d iterations of 256-bit modular multiplication)\n", SCALAR_ITERATIONS);
    printf("====================================================\n\n");
    
    printf("%-25s %-35s %12s %15s\n", 
           "Implementation", "Description", "Time(µs)", "Ops/sec");
    printf("%-25s %-35s %12s %15s\n", 
           "-------------------------", "-----------------------------------", 
           "------------", "---------------");
    
    double dap_generic_time = 0;
    double dap_best_time = 0;
    const char *dap_best_name = NULL;
    
    for (int i = 0; i < result_count; i++) {
        if (!results[i].available) continue;
        
        // Track DAP implementations
        if (strncmp(results[i].name, "DAP ", 4) == 0) {
            if (strstr(results[i].name, "generic")) {
                dap_generic_time = results[i].time_us;
            }
            if (dap_best_time == 0 || results[i].time_us < dap_best_time) {
                dap_best_time = results[i].time_us;
                dap_best_name = results[i].name;
            }
        }
        
        printf("%-25s %-35s %12.2f %15.0f\n",
               results[i].name,
               results[i].description,
               results[i].time_us,
               results[i].ops_per_sec);
    }
    printf("\n");
    
    // =========================================================================
    // Speedup Analysis
    // =========================================================================
    
    printf("=== Speedup Analysis ===\n\n");
    
    if (dap_generic_time > 0) {
        printf("Baseline: DAP generic (%.2f µs)\n\n", dap_generic_time);
        
        printf("%-25s %15s %15s\n", "Implementation", "vs Generic", "vs Best DAP");
        printf("%-25s %15s %15s\n", 
               "-------------------------", "---------------", "---------------");
        
        for (int i = 0; i < result_count; i++) {
            if (!results[i].available) continue;
            
            double vs_generic = dap_generic_time / results[i].time_us;
            double vs_best = dap_best_time / results[i].time_us;
            
            printf("%-25s %14.2fx %14.2fx\n",
                   results[i].name,
                   vs_generic,
                   vs_best);
        }
        printf("\n");
    }
    
    // =========================================================================
    // Summary
    // =========================================================================
    
    printf("=== Summary ===\n\n");
    
    if (dap_best_name) {
        printf("Best DAP implementation: %s (%.2f µs, %.0f ops/sec)\n",
               dap_best_name, dap_best_time, 
               (double)SCALAR_ITERATIONS / (dap_best_time / 1e6));
    }
    
    if (dap_generic_time > 0 && dap_best_time > 0) {
        printf("DAP optimization speedup: %.2fx (generic -> best)\n",
               dap_generic_time / dap_best_time);
    }
    
#ifdef HAVE_OPENSSL_COMPETITOR
    // Find OpenSSL result
    for (int i = 0; i < result_count; i++) {
        if (strcmp(results[i].name, "OpenSSL BN_mod_mul") == 0) {
            double ossl_time = results[i].time_us;
            printf("\nOpenSSL BN_mod_mul: %.2f µs (%.0f ops/sec)\n",
                   ossl_time, results[i].ops_per_sec);
            
            if (dap_best_time > 0) {
                if (dap_best_time < ossl_time) {
                    printf("DAP best is %.2fx FASTER than OpenSSL BN\n",
                           ossl_time / dap_best_time);
                } else {
                    printf("DAP best is %.2fx slower than OpenSSL BN\n",
                           dap_best_time / ossl_time);
                }
            }
            break;
        }
    }
#endif
    
    printf("\n");
    
    // Show current active implementation
    ecdsa_scalar_impl_t current = ecdsa_scalar_get_impl();
    const ecdsa_scalar_impl_info_t *current_info = ecdsa_scalar_get_impl_info(current);
    if (current_info) {
        printf("Active DAP scalar implementation: %s\n", current_info->name);
    }
    printf("\n");
    
    // Free allocated names
    for (int i = 0; i < result_count; i++) {
        if (results[i].name && strncmp(results[i].name, "DAP ", 4) == 0) {
            free((void*)results[i].name);
        }
    }
}

// =============================================================================
// Field Arithmetic Architecture Benchmark
// =============================================================================

static void benchmark_field_arch(void) {
    printf("\n====================================================\n");
    printf("Field Arithmetic Architecture Benchmark\n");
    printf("====================================================\n\n");
    
    typedef struct {
        const char *name;
        const char *description;
        double mul_time_us;
        double sqr_time_us;
        double mul_ops_per_sec;
        double sqr_ops_per_sec;
        bool available;
    } field_bench_result_t;
    
    field_bench_result_t results[16];
    int result_count = 0;
    
    const int FIELD_ITERATIONS = 100000;
    
    // Initialize dispatcher
    ecdsa_field_dispatch_init();
    
    // Get all implementations
    size_t num_impls;
    const ecdsa_field_impl_info_t *impls = ecdsa_field_get_all_impls(&num_impls);
    
    printf("Available field implementations:\n");
    for (size_t i = 0; i < num_impls; i++) {
        if (impls[i].name) {
            printf("  [%s] %s: %s\n", 
                   impls[i].available ? "OK" : "--",
                   impls[i].name, 
                   impls[i].description ? impls[i].description : "N/A");
        }
    }
    printf("\n");
    
    // Create test field elements
    ecdsa_field_t a, b, r;
    memset(&a, 0, sizeof(a));
    memset(&b, 0, sizeof(b));
    
    // Initialize with random data (masked to 52-bit limbs)
    for (int i = 0; i < 5; i++) {
        a.n[i] = ((uint64_t)rand() << 20) ^ rand();
        a.n[i] &= 0xFFFFFFFFFFFFFULL;
        b.n[i] = ((uint64_t)rand() << 20) ^ rand();
        b.n[i] &= 0xFFFFFFFFFFFFFULL;
    }
    
    uint64_t start, elapsed;
    
    // Benchmark each implementation
    for (size_t i = 0; i < num_impls && result_count < 16; i++) {
        if (!impls[i].available || !impls[i].mul || !impls[i].name) continue;
        
        // Warmup mul
        for (int w = 0; w < 1000; w++) {
            impls[i].mul(&r, &a, &b);
        }
        
        // Benchmark mul
        start = get_time_ns();
        for (int j = 0; j < FIELD_ITERATIONS; j++) {
            impls[i].mul(&r, &a, &b);
        }
        elapsed = get_time_ns() - start;
        double mul_time_us = ns_to_us(elapsed);
        
        // Warmup sqr
        for (int w = 0; w < 1000; w++) {
            impls[i].sqr(&r, &a);
        }
        
        // Benchmark sqr
        start = get_time_ns();
        for (int j = 0; j < FIELD_ITERATIONS; j++) {
            impls[i].sqr(&r, &a);
        }
        elapsed = get_time_ns() - start;
        double sqr_time_us = ns_to_us(elapsed);
        
        char name_buf[64];
        snprintf(name_buf, sizeof(name_buf), "DAP %s", impls[i].name);
        
        results[result_count].name = strdup(name_buf);
        results[result_count].description = impls[i].description;
        results[result_count].mul_time_us = mul_time_us;
        results[result_count].sqr_time_us = sqr_time_us;
        results[result_count].mul_ops_per_sec = (double)FIELD_ITERATIONS / (mul_time_us / 1e6);
        results[result_count].sqr_ops_per_sec = (double)FIELD_ITERATIONS / (sqr_time_us / 1e6);
        results[result_count].available = true;
        result_count++;
    }
    
    // Results table
    printf("====================================================\n");
    printf("Field Multiplication/Squaring Benchmark Results\n");
    printf("(%d iterations each)\n", FIELD_ITERATIONS);
    printf("====================================================\n\n");
    
    printf("%-20s %-30s %12s %12s %12s %12s\n", 
           "Implementation", "Description", "Mul(µs)", "Sqr(µs)", "Mul ops/s", "Sqr ops/s");
    printf("%-20s %-30s %12s %12s %12s %12s\n", 
           "--------------------", "------------------------------", 
           "------------", "------------", "------------", "------------");
    
    double generic_mul = 0, generic_sqr = 0;
    double best_mul = 0, best_sqr = 0;
    const char *best_mul_name = NULL, *best_sqr_name = NULL;
    
    for (int i = 0; i < result_count; i++) {
        if (!results[i].available) continue;
        
        // Track generic
        if (strstr(results[i].name, "generic")) {
            generic_mul = results[i].mul_time_us;
            generic_sqr = results[i].sqr_time_us;
        }
        
        // Track best
        if (best_mul == 0 || results[i].mul_time_us < best_mul) {
            best_mul = results[i].mul_time_us;
            best_mul_name = results[i].name;
        }
        if (best_sqr == 0 || results[i].sqr_time_us < best_sqr) {
            best_sqr = results[i].sqr_time_us;
            best_sqr_name = results[i].name;
        }
        
        printf("%-20s %-30s %12.2f %12.2f %12.0f %12.0f\n",
               results[i].name,
               results[i].description ? results[i].description : "N/A",
               results[i].mul_time_us,
               results[i].sqr_time_us,
               results[i].mul_ops_per_sec,
               results[i].sqr_ops_per_sec);
    }
    printf("\n");
    
    // Speedup analysis
    if (generic_mul > 0 && best_mul > 0) {
        printf("=== Field Speedup Analysis ===\n\n");
        printf("%-20s %15s %15s\n", "Implementation", "Mul speedup", "Sqr speedup");
        printf("%-20s %15s %15s\n", "--------------------", "---------------", "---------------");
        
        for (int i = 0; i < result_count; i++) {
            if (!results[i].available) continue;
            printf("%-20s %14.2fx %14.2fx\n",
                   results[i].name,
                   generic_mul / results[i].mul_time_us,
                   generic_sqr / results[i].sqr_time_us);
        }
        printf("\n");
        
        printf("Best multiplication: %s (%.2fx faster than generic)\n", 
               best_mul_name, generic_mul / best_mul);
        printf("Best squaring: %s (%.2fx faster than generic)\n", 
               best_sqr_name, generic_sqr / best_sqr);
    }
    
    // Show current active implementation
    ecdsa_field_impl_t current = ecdsa_field_get_impl();
    const ecdsa_field_impl_info_t *current_info = ecdsa_field_get_impl_info(current);
    if (current_info && current_info->name) {
        printf("\nActive DAP field implementation: %s\n", current_info->name);
    }
    printf("\n");
    
    // Free allocated names
    for (int i = 0; i < result_count; i++) {
        if (results[i].name) {
            free((void*)results[i].name);
        }
    }
}
#endif

// =============================================================================
// Main
// =============================================================================

// Debug function to test ecmult operations
static void debug_ecmult_test(void) {
    // Report which field implementation is being used
    const ecdsa_field_impl_info_t *impl_info = ecdsa_field_get_impl_info(ecdsa_field_get_impl());
    printf("Field implementation: %s (%s)\n", impl_info->name, impl_info->description);
    
    // Disable verbose debug logging for normal operation
    ecdsa_group_set_debug(false);
    
    printf("=== DEBUG: Testing ecmult_gen ===\n");
    
    // Test ecmult_gen(1)
    ecdsa_scalar_t one, two;
    ecdsa_scalar_set_int(&one, 1);
    ecdsa_scalar_set_int(&two, 2);
    
    ecdsa_gej_t g1_jac, g2_jac;
    ecdsa_ecmult_gen(&g1_jac, &one);
    ecdsa_ecmult_gen(&g2_jac, &two);
    
    ecdsa_ge_t g1_aff, g2_aff;
    ecdsa_ge_set_gej(&g1_aff, &g1_jac);
    ecdsa_ge_set_gej(&g2_aff, &g2_jac);
    
    // Print G
    uint8_t buf[32];
    ecdsa_field_t tmp;
    
    tmp = g1_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("1*G.x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("Expected: 79be667ef9dcbbac55a06295ce870b07029bfcdb2dce28d959f2815b16f81798\n");
    printf("1*G is_valid: %s\n\n", ecdsa_ge_is_valid(&g1_aff) ? "YES" : "NO");
    
    tmp = g2_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("2*G.x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("Expected: c6047f9441ed7d6d3045406e95c07cd85c778e4b8cef3ca7abac09b95c709ee5\n");
    tmp = g2_aff.y; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("2*G.y: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("Expected: 1ae168fea63dc339a3c58419466ceaeef7f632653266d0e1236431a950cfe52a\n");
    printf("2*G is_valid: %s\n\n", ecdsa_ge_is_valid(&g2_aff) ? "YES" : "NO");
    
    // Test ecdsa_pubkey_create with simple key (scalar = 3)
    ecdsa_scalar_t seckey;
    ecdsa_scalar_set_int(&seckey, 3);
    
    ecdsa_ge_t pubkey;
    bool ok = ecdsa_pubkey_create(&pubkey, &seckey);
    printf("ecdsa_pubkey_create(3): %s\n", ok ? "OK" : "FAILED");
    printf("pubkey is_valid: %s\n", ecdsa_ge_is_valid(&pubkey) ? "YES" : "NO");
    
    tmp = pubkey.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("pubkey.x (3*G): "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("Expected 3*G.x: f9308a019258c31049344f85f89d5229b531c845836f99b08601f113bce036f9\n");
    
    // Also test via ecmult_gen directly
    ecdsa_scalar_t three;
    ecdsa_scalar_set_int(&three, 3);
    
    ecdsa_gej_t g3_jac;
    ecdsa_ecmult_gen(&g3_jac, &three);
    
    ecdsa_ge_t g3_aff;
    ecdsa_ge_set_gej(&g3_aff, &g3_jac);
    
    tmp = g3_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("ecmult_gen(3).x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("ecmult_gen(3) is_valid: %s\n", ecdsa_ge_is_valid(&g3_aff) ? "YES" : "NO");
    
    // Compute 3*G manually: 2*G + G
    printf("\n--- Manual 3*G = 2*G + G ---\n");
    
    // Get 2*G as Jacobian
    ecdsa_gej_t g2_manual_jac;
    ecdsa_gej_double(&g2_manual_jac, &g1_jac);  // g2_manual_jac = 2 * g1_jac = 2*G
    
    // Verify 2*G before addition
    ecdsa_ge_t g2_check;
    ecdsa_ge_set_gej(&g2_check, &g2_manual_jac);
    tmp = g2_check.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("double(G).x before add: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("double(G) is_valid: %s\n", ecdsa_ge_is_valid(&g2_check) ? "YES" : "NO");
    
    // Check g1_jac Z coordinate
    tmp = g1_jac.z; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("g1_jac.z: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    
    // Check g2_manual_jac Z coordinate
    tmp = g2_manual_jac.z; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("g2_manual_jac.z: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    
    // Verify G (affine) before addition
    tmp = g1_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("G.x before add: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    
    // Also test using ECDSA_GENERATOR directly
    printf("\n--- Using ECDSA_GENERATOR constant ---\n");
    tmp = ECDSA_GENERATOR.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("ECDSA_GENERATOR.x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("ECDSA_GENERATOR is_valid: %s\n", ecdsa_ge_is_valid(&ECDSA_GENERATOR) ? "YES" : "NO");
    
    ecdsa_gej_t g3_via_const;
    ecdsa_gej_add_ge(&g3_via_const, &g2_manual_jac, &ECDSA_GENERATOR);
    ecdsa_ge_t g3_via_const_aff;
    ecdsa_ge_set_gej(&g3_via_const_aff, &g3_via_const);
    tmp = g3_via_const_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("(2G + ECDSA_GENERATOR).x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("(2G + ECDSA_GENERATOR) is_valid: %s\n", ecdsa_ge_is_valid(&g3_via_const_aff) ? "YES" : "NO");
    
    // Add G (affine) to 2*G (Jacobian)
    ecdsa_gej_t g3_manual_jac;
    ecdsa_gej_add_ge(&g3_manual_jac, &g2_manual_jac, &g1_aff);  // g3 = 2*G + G
    
    ecdsa_ge_t g3_manual_aff;
    ecdsa_ge_set_gej(&g3_manual_aff, &g3_manual_jac);
    
    tmp = g3_manual_aff.x; ecdsa_field_normalize(&tmp); ecdsa_field_get_b32(buf, &tmp);
    printf("manual 3*G.x: "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("manual 3*G is_valid: %s\n", ecdsa_ge_is_valid(&g3_manual_aff) ? "YES" : "NO");
    
    // Test field_add correctness
    printf("\n--- Testing field operations ---\n");
    ecdsa_field_t fa, fb, fc;
    ecdsa_field_set_int(&fa, 5);
    ecdsa_field_set_int(&fb, 3);
    ecdsa_field_add(&fc, &fa, &fb);
    ecdsa_field_normalize(&fc);
    printf("5 + 3 = %llu (expected 8)\n", (unsigned long long)fc.n[0]);
    
    // Test negate
    ecdsa_field_negate(&fc, &fa, 1);
    ecdsa_field_normalize(&fc);
    ecdsa_field_get_b32(buf, &fc);
    printf("-5 mod p = "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    
    // Test add negative
    ecdsa_field_add(&fc, &fb, &fc);  // fc = 3 + (-5) = -2 mod p
    ecdsa_field_normalize(&fc);
    ecdsa_field_get_b32(buf, &fc);
    printf("3 + (-5) mod p = "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    
    // Test field_half
    printf("\n--- Testing field_half ---\n");
    ecdsa_field_set_int(&fa, 10);
    ecdsa_field_half(&fa);
    ecdsa_field_normalize(&fa);
    printf("10/2 = %llu (expected 5)\n", (unsigned long long)fa.n[0]);
    
    ecdsa_field_set_int(&fa, 11);
    ecdsa_field_half(&fa);
    ecdsa_field_normalize(&fa);
    // Verify: 2*(11/2) should equal 11
    ecdsa_field_add(&fb, &fa, &fa);
    ecdsa_field_normalize(&fb);
    printf("2*(11/2) = %llu (expected 11)\n", (unsigned long long)fb.n[0]);
    
    // Test with G.x
    const uint8_t gx[32] = {
        0x79, 0xBE, 0x66, 0x7E, 0xF9, 0xDC, 0xBB, 0xAC,
        0x55, 0xA0, 0x62, 0x95, 0xCE, 0x87, 0x0B, 0x07,
        0x02, 0x9B, 0xFC, 0xDB, 0x2D, 0xCE, 0x28, 0xD9,
        0x59, 0xF2, 0x81, 0x5B, 0x16, 0xF8, 0x17, 0x98
    };
    ecdsa_field_set_b32(&fa, gx);
    ecdsa_field_half(&fa);
    ecdsa_field_normalize(&fa);
    ecdsa_field_add(&fb, &fa, &fa);
    ecdsa_field_normalize(&fb);
    ecdsa_field_get_b32(buf, &fb);
    printf("2*(Gx/2) = "); for(int i=0;i<32;i++) printf("%02x", buf[i]); printf("\n");
    printf("Expected = "); for(int i=0;i<32;i++) printf("%02x", gx[i]); printf("\n");
    printf("field_half test: %s\n", memcmp(buf, gx, 32) == 0 ? "PASS" : "FAIL");
    
    printf("=== END DEBUG ===\n\n");
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    
    // DEBUG: Test ecmult_gen
    debug_ecmult_test();
    
    printf("====================================================\n");
    printf("ECDSA Performance Benchmark\n");
    printf("====================================================\n\n");
    
    printf("Configuration:\n");
    printf("  Warmup iterations:    %d\n", WARMUP_ITERATIONS);
    printf("  Benchmark iterations: %d\n", BENCHMARK_ITERATIONS);
    printf("  Test keys:            %d\n", NUM_TEST_KEYS);
    printf("\n");
    
    printf("Implementations:\n");
    printf("  DAP Native (dap_sig_ecdsa): YES\n");
#ifdef HAVE_SECP256K1_COMPETITOR
    printf("  bitcoin-core/secp256k1:     YES\n");
#else
    printf("  bitcoin-core/secp256k1:     NO (run ./download_competitors.sh)\n");
#endif
#ifdef HAVE_OPENSSL_COMPETITOR
    printf("  OpenSSL ECDSA:              YES\n");
#else
    printf("  OpenSSL ECDSA:              NO\n");
#endif
    printf("\n");
    
    // Initialize
    dap_common_init("benchmark_ecdsa", NULL);
    generate_test_data();
    
    // Run verification tests
    if (!verify_correctness()) {
        fprintf(stderr, "Correctness tests failed!\n");
        cleanup_test_data();
        return 1;
    }
    
    // Benchmarks
    benchmark_result_t results[4];
    int result_count = 0;
    
    printf("Running benchmarks...\n\n");
    
    // DAP Native
    benchmark_dap_keygen(&results[result_count]);
    benchmark_dap_sign(&results[result_count]);
    benchmark_dap_verify(&results[result_count]);
    result_count++;
    
#ifdef HAVE_SECP256K1_COMPETITOR
    benchmark_secp256k1_keygen(&results[result_count]);
    benchmark_secp256k1_sign(&results[result_count]);
    benchmark_secp256k1_verify(&results[result_count]);
    result_count++;
#endif
    
#ifdef HAVE_OPENSSL_COMPETITOR
    benchmark_openssl(&results[result_count]);
    result_count++;
#endif
    
    // Display results
    print_results(results, result_count);
    
#ifdef BENCHMARK_SCALAR_ARCH
    // Benchmark architecture-specific implementations
    benchmark_scalar_arch();
    benchmark_field_arch();
#endif
    
    // Cleanup
    cleanup_test_data();
    dap_common_deinit();
    
    printf("Benchmark complete.\n");
    return 0;
}
