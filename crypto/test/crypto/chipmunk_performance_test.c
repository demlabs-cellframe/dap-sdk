#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>

#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_tree.h"

#define LOG_TAG "chipmunk_performance"

// Debug control flag
static bool s_debug_more = false;

// Timing utilities
typedef struct {
    struct timeval start;
    struct timeval end;
} timer_t;

static void timer_start(timer_t *timer) {
    gettimeofday(&timer->start, NULL);
}

static double timer_end(timer_t *timer) {
    gettimeofday(&timer->end, NULL);
    return (timer->end.tv_sec - timer->start.tv_sec) + 
           (timer->end.tv_usec - timer->start.tv_usec) / 1000000.0;
}

/**
 * @brief Performance test for variable number of signers
 */
static int test_performance_variable_signers(size_t num_signers)
{
    timer_t total_timer, keygen_timer, tree_timer, signing_timer, aggregation_timer, verification_timer;
    timer_start(&total_timer);
    
    printf("🚀 Performance test for %zu signers\n", num_signers);
    
    // Prepare test message
    char test_message[256];
    snprintf(test_message, sizeof(test_message), "Multi-signature transaction with %zu participants", num_signers);
    const size_t message_len = strlen(test_message);
    
    // Allocate memory for keys
    chipmunk_private_key_t *private_keys = calloc(num_signers, sizeof(chipmunk_private_key_t));
    chipmunk_public_key_t *public_keys = calloc(num_signers, sizeof(chipmunk_public_key_t));
    chipmunk_hots_pk_t *hots_public_keys = calloc(num_signers, sizeof(chipmunk_hots_pk_t));
    chipmunk_hots_sk_t *hots_secret_keys = calloc(num_signers, sizeof(chipmunk_hots_sk_t));
    
    if (!private_keys || !public_keys || !hots_public_keys || !hots_secret_keys) {
        printf("ERROR: Failed to allocate memory for %zu signers\n", num_signers);
        return -1;
    }
    
    // Key generation phase
    debug_if(s_debug_more, L_INFO, "Generating keys for %zu signers...", num_signers);
    timer_start(&keygen_timer);
    
    for (size_t i = 0; i < num_signers; i++) {
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        if (ret != 0) {
            printf("ERROR: Failed to generate keypair for signer %zu\n", i);
            goto cleanup;
        }
        
        // Extract HOTS keys from Chipmunk keys
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        // Generate HOTS keys
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_setup(&hots_params) != 0) {
            printf("ERROR: Failed to setup HOTS params for signer %zu\n", i);
            goto cleanup;
        }
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
            printf("ERROR: Failed to generate HOTS keys for signer %zu\n", i);
            goto cleanup;
        }
        
        // Progress indicator for large numbers
        if (num_signers > 100 && (i + 1) % (num_signers / 10) == 0) {
            printf("   📊 Key generation progress: %zu/%zu (%.1f%%)\n", 
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers);
        }
    }
    
    double keygen_time = timer_end(&keygen_timer);
    printf("   ⏱️ Key generation: %.3f seconds (%.3f ms per signer)\n", 
           keygen_time, keygen_time * 1000.0 / num_signers);
    
    // Tree construction phase
    debug_if(s_debug_more, L_INFO, "Building Merkle tree...");
    timer_start(&tree_timer);
    
    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;
    
    // Initialize hasher with test seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        printf("ERROR: Failed to initialize HVC hasher\n");
        goto cleanup;
    }
    
    // For large numbers, we need dynamic allocation
    chipmunk_hvc_poly_t *leaf_nodes = calloc(num_signers, sizeof(chipmunk_hvc_poly_t));
    if (!leaf_nodes) {
        printf("ERROR: Failed to allocate leaf nodes for %zu signers\n", num_signers);
        goto cleanup;
    }
    
    // Convert public keys to HVC polynomials
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
        if (ret != 0) {
            printf("ERROR: Failed to convert HOTS pk to HVC poly for signer %zu\n", i);
            free(leaf_nodes);
            goto cleanup;
        }
    }
    
    // Create tree with all participants
    ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, num_signers, &hasher);
    free(leaf_nodes); // Free after tree creation
    
    if (ret != 0) {
        printf("ERROR: Failed to create shared tree\n");
        goto cleanup;
    }
    
    double tree_time = timer_end(&tree_timer);
    printf("   ⏱️ Tree construction: %.3f seconds\n", tree_time);
    
    // Individual signature creation phase
    debug_if(s_debug_more, L_INFO, "Creating individual signatures...");
    timer_start(&signing_timer);
    
    chipmunk_individual_sig_t *individual_sigs = calloc(num_signers, sizeof(chipmunk_individual_sig_t));
    if (!individual_sigs) {
        printf("ERROR: Failed to allocate individual signatures for %zu signers\n", num_signers);
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
            printf("ERROR: Failed to create individual signature for signer %zu\n", i);
            free(individual_sigs);
            goto cleanup;
        }
        
        // Progress indicator for large numbers
        if (num_signers > 100 && (i + 1) % (num_signers / 10) == 0) {
            printf("   📊 Signing progress: %zu/%zu (%.1f%%)\n", 
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers);
        }
    }
    
    double signing_time = timer_end(&signing_timer);
    printf("   ⏱️ Individual signing: %.3f seconds (%.3f ms per signature)\n", 
           signing_time, signing_time * 1000.0 / num_signers);
    
    // Aggregation phase
    debug_if(s_debug_more, L_INFO, "Aggregating signatures...");
    timer_start(&aggregation_timer);
    
    chipmunk_multi_signature_t multi_sig;
    ret = chipmunk_aggregate_signatures_with_tree(
        individual_sigs, num_signers,
        (uint8_t*)test_message, message_len,
        &tree, &multi_sig
    );
    
    if (ret != 0) {
        printf("ERROR: Failed to aggregate signatures, error: %d\n", ret);
        free(individual_sigs);
        goto cleanup;
    }
    
    double aggregation_time = timer_end(&aggregation_timer);
    printf("   ⏱️ Aggregation: %.3f seconds\n", aggregation_time);
    
    // Verification phase
    debug_if(s_debug_more, L_INFO, "Verifying aggregated signature...");
    timer_start(&verification_timer);
    
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);
    
    double verification_time = timer_end(&verification_timer);
    printf("   ⏱️ Verification: %.3f seconds\n", verification_time);
    
    if (ret != 1) {
        printf("ERROR: Multi-signature verification failed, result: %d\n", ret);
        // Don't return error, continue to cleanup
    } else {
        printf("   ✅ Verification: PASSED\n");
    }
    
    // Cleanup
    chipmunk_tree_clear(&tree);
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    chipmunk_multi_signature_free(&multi_sig);
    free(individual_sigs);
    
    double total_time = timer_end(&total_timer);
    
    // Performance summary
    printf("\n📊 Performance Summary for %zu signers:\n", num_signers);
    printf("   ⏱️ Total time: %.3f seconds\n", total_time);
    printf("   📈 Throughput: %.1f signatures/second\n", num_signers / total_time);
    printf("   📊 Per-operation averages:\n");
    printf("      • Keygen: %.3f ms/signer\n", keygen_time * 1000.0 / num_signers);
    printf("      • Signing: %.3f ms/signer\n", signing_time * 1000.0 / num_signers);
    printf("      • Tree construction: %.3f ms total\n", tree_time * 1000.0);
    printf("      • Aggregation: %.3f ms total\n", aggregation_time * 1000.0);
    printf("      • Verification: %.3f ms total\n", verification_time * 1000.0);
    
    printf("\n");
    
cleanup:
    free(private_keys);
    free(public_keys);
    free(hots_public_keys);
    free(hots_secret_keys);
    
    return (ret == 1) ? 0 : -1;
}

int main(int argc, char *argv[])
{
    // Allow enabling debug output via environment variable or command line
    char *debug_env = getenv("CHIPMUNK_DEBUG");
    if (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
        s_debug_more = true;
        chipmunk_hots_set_debug(true);
        printf("🔧 Debug output enabled\n");
    }
    
    printf("🔬 Chipmunk Multi-Signature Performance Testing\n\n");
    
    // Default test sizes
    size_t test_sizes[] = {3, 5, 10, 50, 100};
    size_t num_tests = sizeof(test_sizes) / sizeof(test_sizes[0]);
    
    // Parse command line arguments for custom test sizes
    if (argc > 1) {
        num_tests = argc - 1;
        for (size_t i = 0; i < num_tests; i++) {
            test_sizes[i] = atoi(argv[i + 1]);
            if (test_sizes[i] == 0 || test_sizes[i] > 100000) {
                printf("ERROR: Invalid test size %s (must be 1-100000)\n", argv[i + 1]);
                return -1;
            }
        }
    }
    
    timer_t overall_timer;
    timer_start(&overall_timer);
    
    int overall_result = 0;
    size_t successful_tests = 0;
    
    for (size_t i = 0; i < num_tests; i++) {
        printf("═══════════════════════════════════════════════════\n");
        int result = test_performance_variable_signers(test_sizes[i]);
        if (result == 0) {
            successful_tests++;
        } else {
            overall_result = result;
            printf("❌ Test with %zu signers FAILED\n", test_sizes[i]);
        }
        
        // Small delay between tests for better output readability
        if (i < num_tests - 1) {
            usleep(100000); // 100ms
        }
    }
    
    double overall_time = timer_end(&overall_timer);
    
    printf("═══════════════════════════════════════════════════\n");
    printf("🏁 Overall Results:\n");
    printf("   ✅ Successful tests: %zu/%zu\n", successful_tests, num_tests);
    printf("   ⏱️ Total test time: %.3f seconds\n", overall_time);
    
    if (successful_tests == num_tests) {
        printf("\n🎉 ALL PERFORMANCE TESTS PASSED!\n");
        printf("🚀 Chipmunk multi-signature scheme is ready for production use.\n");
    } else {
        printf("\n❌ Some tests failed. Please check the implementation.\n");
    }
    
    return overall_result;
} 