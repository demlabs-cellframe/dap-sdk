#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <math.h>
#include <errno.h>

#include "dap_common.h"
#include "dap_time.h"  // Use core timing functions
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_tree.h"

#define LOG_TAG "chipmunk_large_scale"

// Debug control flag
static bool s_debug_more = false;

// Use dap_time.h functions instead of custom timer_t
static inline double get_time_ms(void) {
    return dap_nanotime_now() / 1000000.0;  // Convert nanoseconds to milliseconds
}

/**
 * @brief Calculate memory usage for given number of participants
 */
static size_t calculate_memory_usage(size_t num_participants) {
    size_t memory = 0;

    // Keys storage
    memory += num_participants * sizeof(chipmunk_private_key_t);
    memory += num_participants * sizeof(chipmunk_public_key_t);
    memory += num_participants * sizeof(chipmunk_hots_pk_t);
    memory += num_participants * sizeof(chipmunk_hots_sk_t);

    // Tree storage (approximate)
    uint32_t height = chipmunk_tree_calculate_height(num_participants);
    size_t leaf_count = 1UL << (height - 1);
    size_t non_leaf_count = leaf_count - 1;
    memory += leaf_count * sizeof(chipmunk_hvc_poly_t);
    memory += non_leaf_count * sizeof(chipmunk_hvc_poly_t);

    // Individual signatures
    memory += num_participants * sizeof(chipmunk_individual_sig_t);

    return memory;
}

/**
 * @brief Format memory size in human-readable format
 */
static void format_memory_size(size_t bytes, char *buffer, size_t buffer_size) {
    if (bytes < 1024) {
        snprintf(buffer, buffer_size, "%zu B", bytes);
    } else if (bytes < 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f KB", bytes / 1024.0);
    } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buffer, buffer_size, "%.1f MB", bytes / (1024.0 * 1024.0));
    } else {
        snprintf(buffer, buffer_size, "%.1f GB", bytes / (1024.0 * 1024.0 * 1024.0));
    }
}

/**
 * @brief Large-scale performance test
 */
static int test_large_scale_performance(size_t num_signers)
{
    int ret = 0;  // Initialize ret to avoid uninitialized variable warning
    double total_timer = get_time_ms();

    // Memory usage estimation
    size_t estimated_memory = calculate_memory_usage(num_signers);
    char memory_str[64];
    format_memory_size(estimated_memory, memory_str, sizeof(memory_str));

    log_it(L_NOTICE, "🚀 LARGE SCALE TEST: %zu participants", num_signers);
    log_it(L_NOTICE, "   💾 Estimated memory usage: %s", memory_str);

    // Validate participant count
    if (!chipmunk_tree_validate_participant_count(num_signers)) {
        log_it(L_ERROR, "ERROR: Invalid participant count %zu (max %d)",
               num_signers, CHIPMUNK_TREE_MAX_PARTICIPANTS);
        return -1;
    }

    // Calculate required tree height
    uint32_t required_height = chipmunk_tree_calculate_height(num_signers);
    log_it(L_NOTICE, "   🌳 Tree height: %u levels (capacity: %zu participants)",
           required_height, 1UL << (required_height - 1));

    // Prepare test message
    char test_message[256];
    snprintf(test_message, sizeof(test_message),
             "Large-scale blockchain transaction with %zu participants", num_signers);
    const size_t message_len = strlen(test_message);

    // Allocate memory for keys with error checking using DAP SDK standards
    log_it(L_INFO, "   🔧 Allocating memory for %zu participants...", num_signers);

    // Calculate total memory requirement for transparency
    size_t total_memory = num_signers * (sizeof(chipmunk_private_key_t) +
                                        sizeof(chipmunk_public_key_t) +
                                        sizeof(chipmunk_hots_pk_t) +
                                        sizeof(chipmunk_hots_sk_t));
    format_memory_size(total_memory, memory_str, sizeof(memory_str));
    log_it(L_INFO, "   💾 Allocating %s for key storage", memory_str);

    chipmunk_private_key_t *private_keys = DAP_NEW_Z_COUNT(chipmunk_private_key_t, num_signers);
    chipmunk_public_key_t *public_keys = DAP_NEW_Z_COUNT(chipmunk_public_key_t, num_signers);
    chipmunk_hots_pk_t *hots_public_keys = DAP_NEW_Z_COUNT(chipmunk_hots_pk_t, num_signers);
    chipmunk_hots_sk_t *hots_secret_keys = DAP_NEW_Z_COUNT(chipmunk_hots_sk_t, num_signers);

    if (!private_keys || !public_keys || !hots_public_keys || !hots_secret_keys) {
        log_it(L_CRITICAL, "Failed to allocate memory for %zu signers (%s required)", num_signers, memory_str);
        DAP_DEL_MULTY(private_keys);
        DAP_DEL_MULTY(public_keys);
        DAP_DEL_MULTY(hots_public_keys);
        DAP_DEL_MULTY(hots_secret_keys);
        return -ENOMEM;
    }

    // Phase 1: Key Generation
    log_it(L_INFO, "   🔑 Phase 1: Key generation...");
    double keygen_timer = get_time_ms();

    size_t progress_interval = (num_signers > 1000) ? (num_signers / 20) : 0; // 5% intervals for large tests

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

        // Generate HOTS keys
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_setup(&hots_params) != 0) {
            log_it(L_ERROR, "ERROR: Failed to setup HOTS params for signer %zu", i);
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
        if (progress_interval > 0 && (i + 1) % progress_interval == 0) {
            double elapsed = get_time_ms() - keygen_timer;
            double rate = (i + 1) / elapsed;
            double eta = (num_signers - i - 1) / rate;

            log_it(L_INFO, "   📊 Keygen progress: %zu/%zu (%.1f%%) - Rate: %.1f keys/sec - ETA: %.1f sec",
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers, rate, eta);
            keygen_timer = get_time_ms(); // Restart timer for rate calculation
        }
    }

    double keygen_time = get_time_ms() - keygen_timer;
    log_it(L_NOTICE, "   ✅ Key generation: %.3f seconds (%.3f ms per signer, %.1f keys/sec)",
           keygen_time, keygen_time * 1000.0 / num_signers, num_signers / keygen_time);

    // Phase 2: Tree Construction
    log_it(L_INFO, "   🌳 Phase 2: Merkle tree construction...");
    double tree_timer = get_time_ms();

    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;

    // Initialize hasher with test seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to initialize HVC hasher");
        goto cleanup;
    }

    // Allocate leaf nodes for tree using DAP SDK standards
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

    // Get tree statistics
    uint32_t tree_height;
    size_t tree_leaf_count, tree_memory_usage;
    chipmunk_tree_get_stats(&tree, &tree_height, &tree_leaf_count, &tree_memory_usage);

    char tree_memory_str[64];
    format_memory_size(tree_memory_usage, tree_memory_str, sizeof(tree_memory_str));

    double tree_time = get_time_ms() - tree_timer;
    log_it(L_NOTICE, "   ✅ Tree construction: %.3f seconds - Height: %u - Memory: %s",
           tree_time, tree_height, tree_memory_str);

    // Phase 3: Individual Signature Creation
    log_it(L_INFO, "   ✍️ Phase 3: Individual signature creation...");
    double signing_timer = get_time_ms();

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
        if (progress_interval > 0 && (i + 1) % progress_interval == 0) {
            double elapsed = get_time_ms() - signing_timer;
            double rate = (i + 1) / elapsed;
            double eta = (num_signers - i - 1) / rate;

            log_it(L_INFO, "   📊 Signing progress: %zu/%zu (%.1f%%) - Rate: %.1f sigs/sec - ETA: %.1f sec",
                   i + 1, num_signers, (float)(i + 1) * 100.0 / num_signers, rate, eta);
            signing_timer = get_time_ms();
        }
    }

    double signing_time = get_time_ms() - signing_timer;
    log_it(L_NOTICE, "   ✅ Individual signing: %.3f seconds (%.3f ms per signature, %.1f sigs/sec)",
           signing_time, signing_time * 1000.0 / num_signers, num_signers / signing_time);

    // Phase 4: Signature Aggregation
    log_it(L_INFO, "   🔗 Phase 4: Signature aggregation...");
    double aggregation_timer = get_time_ms();

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

    double aggregation_time = get_time_ms() - aggregation_timer;
    log_it(L_NOTICE, "   ✅ Signature aggregation: %.3f seconds", aggregation_time);

    // Calculate signature and key sizes for distribution/storage analysis
    size_t multi_sig_size = sizeof(chipmunk_multi_signature_t);
    size_t single_pubkey_size = sizeof(chipmunk_public_key_t);
    size_t total_pubkeys_size = num_signers * single_pubkey_size;
    size_t total_distributable_size = multi_sig_size + total_pubkeys_size;

    char multi_sig_str[64], pubkeys_str[64], total_dist_str[64];
    format_memory_size(multi_sig_size, multi_sig_str, sizeof(multi_sig_str));
    format_memory_size(total_pubkeys_size, pubkeys_str, sizeof(pubkeys_str));
    format_memory_size(total_distributable_size, total_dist_str, sizeof(total_dist_str));

    log_it(L_INFO, "   📦 Multi-signature size: %s", multi_sig_str);
    log_it(L_INFO, "   🔑 Total public keys size: %s (%zu keys × %zu bytes)",
           pubkeys_str, num_signers, single_pubkey_size);
    log_it(L_INFO, "   📋 Total distributable payload: %s", total_dist_str);

    // Phase 5: Verification
    log_it(L_INFO, "   🔍 Phase 5: Multi-signature verification...");
    double verification_timer = get_time_ms();

    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);

    double verification_time = get_time_ms() - verification_timer;

    if (ret != 1) {
        log_it(L_ERROR, "ERROR: Multi-signature verification failed, result: %d", ret);
        DAP_DEL_MULTY(individual_sigs);
        goto cleanup;
    }

    log_it(L_NOTICE, "   ✅ Verification: %.3f seconds", verification_time);

    // Phase 6: Test with wrong message (should fail)
    const char wrong_message[] = "Wrong message for verification test";
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)wrong_message, strlen(wrong_message));

    if (ret > 0) {
        log_it(L_ERROR, "ERROR: Wrong message verification should have failed!");
        DAP_DEL_MULTY(individual_sigs);
        goto cleanup;
    }

    log_it(L_INFO, "   ✅ Wrong message verification correctly failed");

    // Total performance summary
    double total_time = get_time_ms() - total_timer;

    // Calculate throughput metrics
    double keygen_rate = num_signers / keygen_time;
    double signing_rate = num_signers / signing_time;
    double total_rate = num_signers / total_time;

    log_it(L_NOTICE, " ");  // Use space instead of empty string
    log_it(L_NOTICE, "🎯 PERFORMANCE SUMMARY for %zu participants:", num_signers);
    log_it(L_NOTICE, "   ⏱️ Total time: %.3f seconds (%.3f ms per participant)",
           total_time, total_time * 1000.0 / num_signers);
    log_it(L_NOTICE, "   🔑 Key generation: %.3f s (%.1f keys/sec)", keygen_time, keygen_rate);
    log_it(L_NOTICE, "   🌳 Tree construction: %.3f s", tree_time);
    log_it(L_NOTICE, "   ✍️ Individual signing: %.3f s (%.1f sigs/sec)", signing_time, signing_rate);
    log_it(L_NOTICE, "   🔗 Aggregation: %.3f s", aggregation_time);
    log_it(L_NOTICE, "   🔍 Verification: %.3f s", verification_time);
    log_it(L_NOTICE, "   📊 Overall throughput: %.1f participants/sec", total_rate);
    log_it(L_NOTICE, "   💾 Memory usage: %s", memory_str);
    log_it(L_NOTICE, "   📦 Multi-signature size: %s", multi_sig_str);
    log_it(L_NOTICE, "   🔑 Public keys total: %s (%zu participants)", pubkeys_str, num_signers);
    log_it(L_NOTICE, "   📋 Distributable payload: %s (signature + all pubkeys)", total_dist_str);
    log_it(L_NOTICE, " ");  // Use space instead of empty string

    // Cleanup
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    DAP_DEL_MULTY(individual_sigs);
    chipmunk_multi_signature_free(&multi_sig);
    chipmunk_tree_free(&tree);

    goto cleanup_keys;

cleanup:
    chipmunk_tree_free(&tree);

cleanup_keys:
    DAP_DEL_MULTY(private_keys);
    DAP_DEL_MULTY(public_keys);
    DAP_DEL_MULTY(hots_public_keys);
    DAP_DEL_MULTY(hots_secret_keys);

    return (ret == 1) ? 0 : ret;
}

/**
 * @brief Progressive scale testing from small to large
 */
static int test_progressive_scale(void) {
    log_it(L_NOTICE, "🚀 PROGRESSIVE SCALE TESTING");
    log_it(L_NOTICE, " ");  // Use space instead of empty string

    // Test scales: optimized for unit testing, max 1000 (blockchain shard size)
    size_t test_scales[] = {
        3,      // Baseline small test
        5,      // Small group
        10,     // Medium group
        16,     // Previous static limit (legacy compatibility)
        32,     // Small organization
        64,     // Medium organization
        128,    // Large organization
        256,    // Very large organization
        512,    // Blockchain validator set
        1000    // Max blockchain shard size (unit test limit)
    };

    size_t num_tests = sizeof(test_scales) / sizeof(test_scales[0]);

    for (size_t i = 0; i < num_tests; i++) {
        size_t scale = test_scales[i];

        log_it(L_NOTICE, "📈 Testing scale %zu/%zu: %zu participants",
               i + 1, num_tests, scale);

        int result = test_large_scale_performance(scale);

        if (result != 0) {
            log_it(L_ERROR, "❌ FAILED at scale %zu participants", scale);
            return result;
        }

        log_it(L_NOTICE, "✅ SUCCESS at scale %zu participants", scale);
        log_it(L_NOTICE, " ");  // Use space instead of empty string

        // Short pause between larger tests for system stability
        if (scale >= 512) {
            log_it(L_INFO, "⏸️ Pausing 1 second for system stability...");
            sleep(1);
        }
    }

    log_it(L_NOTICE, "🎉 ALL PROGRESSIVE SCALE TESTS COMPLETED SUCCESSFULLY!");
    return 0;
}

/**
 * @brief Entry point for large-scale testing
 */
int main(int argc, char *argv[]) {
    // Initialize logging with clean format for unit tests
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);  // Clean output without timestamps/modules

    // Initialize Chipmunk module
    dap_enc_chipmunk_init();

    log_it(L_NOTICE, "🚀 CHIPMUNK LARGE-SCALE MULTI-SIGNATURE TESTING");
    log_it(L_NOTICE, "Unit test range: Up to 1000 participants (blockchain shard size)");
    log_it(L_NOTICE, " ");  // Use space instead of empty string

    int result = 0;

    if (argc > 1) {
        // Single scale test from command line argument
        size_t scale = strtoul(argv[1], NULL, 10);

        if (scale == 0 || scale > CHIPMUNK_TREE_MAX_PARTICIPANTS) {
            log_it(L_ERROR, "Invalid scale: %zu (must be 1-%d)", scale, CHIPMUNK_TREE_MAX_PARTICIPANTS);
            return -1;
        }

        log_it(L_NOTICE, "🎯 Single scale test: %zu participants", scale);
        result = test_large_scale_performance(scale);

    } else {
        // Progressive testing from small to large
        result = test_progressive_scale();
    }

    if (result == 0) {
        log_it(L_NOTICE, "🎉 ALL LARGE-SCALE TESTS COMPLETED SUCCESSFULLY!");
    } else {
        log_it(L_ERROR, "❌ LARGE-SCALE TESTS FAILED! Error code: %d", result);
    }

    return result;
}
