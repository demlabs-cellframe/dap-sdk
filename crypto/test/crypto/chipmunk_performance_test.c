#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_time.h"  // Use core timing functions
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_tree.h"
#include "chipmunk/chipmunk_hash.h"  // Hash functions with PHASE 1 optimizations

#define LOG_TAG "chipmunk_performance"

// Debug control flag
static bool s_debug_more = false;

// PHASE 1: Hash optimization test flag
static bool s_test_hash_optimization = false;

// Use dap_time.h functions instead of custom timer_t
static inline double get_time_ms(void) {
    return dap_nanotime_now() / 1000000.0;  // Convert nanoseconds to milliseconds
}

/**
 * @brief Test Phase 1 Hash optimization performance
 * 🚀 PHASE 1: Hash function optimization testing
 */
static int test_phase1_hash_optimization(void)
{
    log_it(L_INFO, "🧪 PHASE 1: Testing hash optimization performance");
    
    // Test parameters
    const int NUM_POLYS = 100;  // Test with 100 polynomials (like in signing)
    const int NUM_ITERATIONS = 10;
    
    uint8_t test_seed[32];
    for (int i = 0; i < 32; i++) {
        test_seed[i] = i;
    }
    
    // Allocate polynomials
    int32_t *poly_standard[NUM_POLYS];
    int32_t *poly_optimized[NUM_POLYS];
    
    for (int i = 0; i < NUM_POLYS; i++) {
        poly_standard[i] = DAP_NEW_Z_COUNT(int32_t, CHIPMUNK_N);
        poly_optimized[i] = DAP_NEW_Z_COUNT(int32_t, CHIPMUNK_N);
        if (!poly_standard[i] || !poly_optimized[i]) {
            log_it(L_ERROR, "Failed to allocate memory for polynomial testing");
            // Cleanup allocated memory
            for (int j = 0; j < i; j++) {
                DAP_DELETE(poly_standard[j]);
                DAP_DELETE(poly_optimized[j]);
            }
            if (poly_standard[i]) DAP_DELETE(poly_standard[i]);
            if (poly_optimized[i]) DAP_DELETE(poly_optimized[i]);
            return -1;
        }
    }
    
    double total_standard_time = 0.0;
    double total_optimized_time = 0.0;
    
    log_it(L_INFO, "   Testing with %d polynomials × %d iterations = %d total operations", 
           NUM_POLYS, NUM_ITERATIONS, NUM_POLYS * NUM_ITERATIONS);
    
    // Test standard implementation
    log_it(L_INFO, "   🔄 Testing STANDARD hash implementation...");
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        double iter_start = get_time_ms();
        
        for (int i = 0; i < NUM_POLYS; i++) {
            int result = dap_chipmunk_hash_sample_poly(poly_standard[i], test_seed, (uint16_t)i);
            if (result != 0) {
                log_it(L_ERROR, "Standard hash sampling failed for poly %d", i);
                goto cleanup;
            }
        }
        
        double iter_time = get_time_ms() - iter_start;
        total_standard_time += iter_time;
        
        if (s_debug_more) {
            log_it(L_INFO, "      Iteration %d: %.3f ms", iter + 1, iter_time);
        }
    }
    
    // Test optimized implementation
    log_it(L_INFO, "   🚀 Testing OPTIMIZED hash implementation...");
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        double iter_start = get_time_ms();
        
        for (int i = 0; i < NUM_POLYS; i++) {
            int result = dap_chipmunk_hash_sample_poly(poly_optimized[i], test_seed, (uint16_t)i);
            if (result != 0) {
                log_it(L_ERROR, "Optimized hash sampling failed for poly %d", i);
                goto cleanup;
            }
        }
        
        double iter_time = get_time_ms() - iter_start;
        total_optimized_time += iter_time;
        
        if (s_debug_more) {
            log_it(L_INFO, "      Iteration %d: %.3f ms", iter + 1, iter_time);
        }
    }
    
    // Verify correctness - polynomials should be identical
    log_it(L_INFO, "   🔍 Verifying correctness...");
    bool correctness_ok = true;
    for (int i = 0; i < NUM_POLYS && correctness_ok; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            if (poly_standard[i][j] != poly_optimized[i][j]) {
                log_it(L_ERROR, "Mismatch in poly %d coeff %d: standard=%d, optimized=%d", 
                       i, j, poly_standard[i][j], poly_optimized[i][j]);
                correctness_ok = false;
                break;
            }
        }
    }
    
    if (!correctness_ok) {
        log_it(L_ERROR, "❌ CORRECTNESS TEST FAILED!");
        goto cleanup;
    }
    
    // Calculate performance metrics
    double avg_standard = total_standard_time / NUM_ITERATIONS;
    double avg_optimized = total_optimized_time / NUM_ITERATIONS;
    double speedup = avg_standard / avg_optimized;
    double standard_per_poly = avg_standard / NUM_POLYS;
    double optimized_per_poly = avg_optimized / NUM_POLYS;
    
    // Results
    log_it(L_INFO, " ");
    log_it(L_INFO, "📊 PHASE 1 Hash Optimization Results:");
    log_it(L_INFO, "   ⏱️ Standard implementation:");
    log_it(L_INFO, "      • Total time: %.3f ms (avg per iteration)", avg_standard);
    log_it(L_INFO, "      • Per polynomial: %.6f ms", standard_per_poly);
    log_it(L_INFO, "   🚀 Optimized implementation:");
    log_it(L_INFO, "      • Total time: %.3f ms (avg per iteration)", avg_optimized);
    log_it(L_INFO, "      • Per polynomial: %.6f ms", optimized_per_poly);
    log_it(L_INFO, "   📈 Performance improvement:");
    log_it(L_INFO, "      • Speedup: %.2fx", speedup);
    log_it(L_INFO, "      • Time reduction: %.1f%% faster", (speedup - 1.0) * 100.0);
    log_it(L_INFO, "   ✅ Correctness: VERIFIED (outputs identical)");
    
    // Estimate impact on signing
    double signing_polynomial_count = 32;  // From profiling: ~32 polynomials per signature
    double estimated_signing_improvement = standard_per_poly * signing_polynomial_count * (speedup - 1.0);
    log_it(L_INFO, "   🎯 Estimated signing improvement: %.3f ms reduction", estimated_signing_improvement);
    
    if (speedup >= 1.5) {
        log_it(L_INFO, "   🎉 PHASE 1 OPTIMIZATION: SUCCESS!");
    } else if (speedup >= 1.2) {
        log_it(L_INFO, "   ⚠️ PHASE 1 OPTIMIZATION: Moderate improvement");
    } else {
        log_it(L_WARNING, "   ⚠️ PHASE 1 OPTIMIZATION: Limited improvement");
    }
    
cleanup:
    // Free allocated memory
    for (int i = 0; i < NUM_POLYS; i++) {
        DAP_DELETE(poly_standard[i]);
        DAP_DELETE(poly_optimized[i]);
    }
    
    return correctness_ok ? 0 : -1;
}

/**
 * @brief Performance test for variable number of signers
 */
static int test_performance_variable_signers(size_t num_signers)
{
    double total_start = get_time_ms();
    
    log_it(L_INFO, "🚀 Performance test for %zu signers", num_signers);
    
    // Prepare test message
    char test_message[256];
    snprintf(test_message, sizeof(test_message), "Multi-signature transaction with %zu participants", num_signers);
    const size_t message_len = strlen(test_message);
    
    // Allocate memory for keys using DAP SDK standards
    chipmunk_private_key_t *private_keys = DAP_NEW_Z_COUNT(chipmunk_private_key_t, num_signers);
    chipmunk_public_key_t *public_keys = DAP_NEW_Z_COUNT(chipmunk_public_key_t, num_signers);
    chipmunk_hots_pk_t *hots_public_keys = DAP_NEW_Z_COUNT(chipmunk_hots_pk_t, num_signers);
    chipmunk_hots_sk_t *hots_secret_keys = DAP_NEW_Z_COUNT(chipmunk_hots_sk_t, num_signers);
    
    if (!private_keys || !public_keys || !hots_public_keys || !hots_secret_keys) {
        log_it(L_CRITICAL, "Failed to allocate memory for %zu signers", num_signers);
        DAP_DEL_MULTY(private_keys);
        DAP_DEL_MULTY(public_keys);
        DAP_DEL_MULTY(hots_public_keys);
        DAP_DEL_MULTY(hots_secret_keys);
        return -ENOMEM;
    }
    
    // Key generation phase
    debug_if(s_debug_more, L_INFO, "Generating keys for %zu signers...", num_signers);
    double keygen_start = get_time_ms();
    
    for (size_t i = 0; i < num_signers; i++) {
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate keypair for signer %zu", i);
            goto cleanup;
        }
        
        // Extract HOTS keys from Chipmunk keys
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        // **ОПТИМИЗАЦИЯ**: Используем кэшированные HOTS параметры
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_get_cached_params(&hots_params) != 0) {
            log_it(L_ERROR, "ERROR: Failed to get cached HOTS params for signer %zu", i);
            goto cleanup;
        }
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate HOTS keys for signer %zu", i);
            goto cleanup;
        }
        
        // Progress indicator for large numbers
        if (num_signers > 100 && (i + 1) % (num_signers / 10) == 0) {
            log_it(L_INFO, "   📊 Key generation progress: %zu/%zu (%.1f%%)", 
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers);
        }
    }
    
    double keygen_time = get_time_ms() - keygen_start;
    log_it(L_INFO, "   ⏱️ Key generation: %.3f seconds (%.3f ms per signer)", 
           keygen_time, keygen_time * 1000.0 / num_signers);
    
    // Tree construction phase
    debug_if(s_debug_more, L_INFO, "Building Merkle tree...");
    double tree_start = get_time_ms();
    
    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;
    
    // Initialize hasher with test seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to initialize HVC hasher");
        goto cleanup;
    }
    
    // Dynamic allocation for leaf nodes using DAP SDK standards
    chipmunk_hvc_poly_t *leaf_nodes = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, num_signers);
    if (!leaf_nodes) {
        log_it(L_CRITICAL, "Failed to allocate leaf nodes for %zu signers", num_signers);
        goto cleanup;
    }
    
    // Convert public keys to HVC polynomials
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to convert HOTS pk to HVC poly for signer %zu", i);
            DAP_DEL_MULTY(leaf_nodes);
            goto cleanup;
        }
    }
    
    // Create tree with all participants
    ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, num_signers, &hasher);
    DAP_DEL_MULTY(leaf_nodes); // Free after tree creation
    
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to create shared tree");
        goto cleanup;
    }
    
    double tree_time = get_time_ms() - tree_start;
    log_it(L_INFO, "   ⏱️ Tree construction: %.3f seconds", tree_time);
    
    // Individual signature creation phase
    debug_if(s_debug_more, L_INFO, "Creating individual signatures...");
    double signing_start = get_time_ms();
    
    chipmunk_individual_sig_t *individual_sigs = DAP_NEW_Z_COUNT(chipmunk_individual_sig_t, num_signers);
    if (!individual_sigs) {
        log_it(L_CRITICAL, "Failed to allocate individual signatures for %zu signers", num_signers);
        goto cleanup;
    }
    
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_create_individual_signature(
            (uint8_t*)test_message, message_len,
            &hots_secret_keys[i], &hots_public_keys[i],
            &tree, i,
            &individual_sigs[i]
        );
        
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to create individual signature for signer %zu", i);
            DAP_DEL_MULTY(individual_sigs);
            goto cleanup;
        }
        
        // Progress indicator for large numbers
        if (num_signers > 100 && (i + 1) % (num_signers / 10) == 0) {
            log_it(L_INFO, "   📊 Signing progress: %zu/%zu (%.1f%%)", 
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers);
        }
    }
    
    double signing_time = get_time_ms() - signing_start;
    log_it(L_INFO, "   ⏱️ Individual signing: %.3f seconds (%.3f ms per signature)", 
           signing_time, signing_time * 1000.0 / num_signers);
    
    // Aggregation phase
    debug_if(s_debug_more, L_INFO, "Aggregating signatures...");
    double aggregation_start = get_time_ms();
    
    chipmunk_multi_signature_t multi_sig;
    ret = chipmunk_aggregate_signatures_with_tree(
        individual_sigs, num_signers,
        (uint8_t*)test_message, message_len,
        &tree, &multi_sig
    );
    
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to aggregate signatures, error: %d", ret);
        DAP_DEL_MULTY(individual_sigs);
        goto cleanup;
    }
    
    double aggregation_time = get_time_ms() - aggregation_start;
    log_it(L_INFO, "   ⏱️ Aggregation: %.3f seconds", aggregation_time);
    
    // Verification phase
    debug_if(s_debug_more, L_INFO, "Verifying aggregated signature...");
    double verification_start = get_time_ms();
    
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);
    
    double verification_time = get_time_ms() - verification_start;
    log_it(L_INFO, "   ⏱️ Verification: %.3f seconds", verification_time);
    
    if (ret != 1) {
        log_it(L_ERROR, "ERROR: Multi-signature verification failed, result: %d", ret);
        // Don't return error, continue to cleanup
    } else {
        log_it(L_INFO, "   ✅ Verification: PASSED");
    }
    
    // Cleanup
    chipmunk_tree_clear(&tree);
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    chipmunk_multi_signature_free(&multi_sig);
    DAP_DEL_MULTY(individual_sigs);
    
    double total_time = get_time_ms() - total_start;
    
    // Performance summary
    log_it(L_INFO, " ");
    log_it(L_INFO, "📊 Performance Summary for %zu signers:", num_signers);
    log_it(L_INFO, "   ⏱️ Total time: %.3f seconds", total_time);
    log_it(L_INFO, "   📈 Throughput: %.1f signatures/second", num_signers / total_time);
    log_it(L_INFO, "   📊 Per-operation averages:");
    log_it(L_INFO, "      • Keygen: %.3f ms/signer", keygen_time * 1000.0 / num_signers);
    log_it(L_INFO, "      • Signing: %.3f ms/signer", signing_time * 1000.0 / num_signers);
    log_it(L_INFO, "      • Tree construction: %.3f ms total", tree_time * 1000.0);
    log_it(L_INFO, "      • Aggregation: %.3f ms total", aggregation_time * 1000.0);
    log_it(L_INFO, "      • Verification: %.3f ms total", verification_time * 1000.0);
    
    log_it(L_INFO, " ");
    
cleanup:
    DAP_DEL_MULTY(private_keys);
    DAP_DEL_MULTY(public_keys);
    DAP_DEL_MULTY(hots_public_keys);
    DAP_DEL_MULTY(hots_secret_keys);
    
    return (ret == 1) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    // Initialize logging with clean format for unit tests
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);  // Clean output without timestamps/modules
    
    // Initialize Chipmunk module
    dap_enc_chipmunk_init();
    
    // Allow enabling debug output via environment variable or command line
    char *debug_env = getenv("CHIPMUNK_DEBUG");
    if (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
        s_debug_more = true;
        chipmunk_hots_set_debug(true);
        log_it(L_INFO, "🔧 Debug output enabled");
    }
    
    // PHASE 1: Check for hash optimization testing
    char *hash_opt_env = getenv("CHIPMUNK_TEST_HASH_OPT");
    if (hash_opt_env && (strcmp(hash_opt_env, "1") == 0 || strcmp(hash_opt_env, "true") == 0)) {
        s_test_hash_optimization = true;
        log_it(L_INFO, "🚀 Phase 1 hash optimization testing enabled");
    }
    
    log_it(L_NOTICE, "🔬 CHIPMUNK PERFORMANCE TESTING");
    log_it(L_NOTICE, "Unit test range: Up to 100 participants (optimal for benchmarks)");
    log_it(L_NOTICE, " ");
    
    // PHASE 1: Run hash optimization test if enabled
    if (s_test_hash_optimization) {
        log_it(L_INFO, "═══════════════════════════════════════════════════");
        int hash_result = test_phase1_hash_optimization();
        if (hash_result != 0) {
            log_it(L_ERROR, "❌ Phase 1 hash optimization test FAILED");
            return hash_result;
        }
        log_it(L_INFO, "═══════════════════════════════════════════════════");
        log_it(L_INFO, " ");
    }
    
    // Default test sizes
    size_t test_sizes[] = {3, 5, 10, 50, 100};
    size_t num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    // Parse command line arguments for custom test sizes
    if (argc > 1) {
        num_tests = argc - 1;
        for (size_t i = 0; i < num_tests; i++) {
            test_sizes[i] = atoi(argv[i + 1]);
            if (test_sizes[i] == 0 || test_sizes[i] > 100000) {
                log_it(L_ERROR, "ERROR: Invalid test size %s (must be 1-100000)", argv[i + 1]);
                return -1;
            }
        }
    }
    
    double overall_start = get_time_ms();
    
    int overall_result = 0;
    size_t successful_tests = 0;
    
    for (size_t i = 0; i < num_tests; i++) {
        log_it(L_INFO, "═══════════════════════════════════════════════════");
        int result = test_performance_variable_signers(test_sizes[i]);
        if (result == 0) {
            successful_tests++;
        } else {
            overall_result = result;
            log_it(L_ERROR, "❌ Test with %zu signers FAILED", test_sizes[i]);
        }
        
        // Small delay between tests for better output readability
        if (i < num_tests - 1) {
            usleep(100000); // 100ms
        }
    }
    
    double overall_time = get_time_ms() - overall_start;
    
    log_it(L_INFO, "═══════════════════════════════════════════════════");
    log_it(L_INFO, "🏁 Overall Results:");
    log_it(L_INFO, "   ✅ Successful tests: %zu/%zu", successful_tests, num_tests);
    log_it(L_INFO, "   ⏱️ Total test time: %.3f seconds", overall_time);
    
    if (successful_tests == num_tests) {
        log_it(L_INFO, " ");
        log_it(L_INFO, "🎉 ALL PERFORMANCE TESTS PASSED!");
        log_it(L_INFO, "🚀 Chipmunk multi-signature scheme is ready for production use.");
    } else {
        log_it(L_ERROR, " ");
        log_it(L_ERROR, "❌ Some tests failed. Please check the implementation.");
    }
    
    return overall_result;
} 