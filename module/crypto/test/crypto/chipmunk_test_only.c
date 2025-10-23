#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_time.h"  // Use core timing functions
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_tree.h"

#define LOG_TAG "chipmunk_test_only"

// Debug control flag
static bool s_debug_more = false;

// Use dap_time.h functions instead of custom timer_t
static inline double get_time_ms(void) {
    return dap_nanotime_now() / 1000000.0;  // Convert nanoseconds to milliseconds
}

/**
 * @brief Test multi-signature aggregation with 3 signers
 */
static int test_multi_signature_aggregation(void)
{
    double start_time = get_time_ms();
    
    debug_if(s_debug_more, L_INFO, "=== Multi-Signature Aggregation Test ===");
    
    const size_t num_signers = 3;
    const char test_message[] = "Multi-party contract agreement";
    const size_t message_len = strlen(test_message);
    
    // Создаем ключи для всех участников
    chipmunk_private_key_t private_keys[num_signers];
    chipmunk_public_key_t public_keys[num_signers];
    chipmunk_hots_pk_t hots_public_keys[num_signers];
    chipmunk_hots_sk_t hots_secret_keys[num_signers];
    
    debug_if(s_debug_more, L_INFO, "Generating keys for %zu signers...", num_signers);
    
    for (size_t i = 0; i < num_signers; i++) {
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate keypair for signer %zu", i);
            return -1;
        }
        
        // Получаем HOTS ключи из Chipmunk ключей
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        // Генерируем HOTS ключи
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_setup(&hots_params) != 0) {
            log_it(L_ERROR, "ERROR: Failed to setup HOTS params for signer %zu", i);
            return -1;
        }
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate HOTS keys for signer %zu", i);
            return -1;
        }
        
        debug_if(s_debug_more, L_INFO, "Generated keypair for signer %zu", i);
    }
    
    double keygen_time = get_time_ms() - start_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Key generation time: %.3f seconds (%.3f ms per signer)", 
             keygen_time, keygen_time * 1000.0 / num_signers);
    
    // Создаем ОДНО Merkle дерево для всех участников
    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;
    
    // Инициализируем hasher с тестовым seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to initialize HVC hasher");
        return -2;
    }
    
    // Создаем массив листов для дерева
    chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    memset(leaf_nodes, 0, sizeof(leaf_nodes));
    
    // Добавляем публичные ключи всех участников в дерево
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to convert HOTS pk to HVC poly for signer %zu", i);
            return -3;
        }
    }
    
    // Создаем дерево со всеми участниками
    ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, num_signers, &hasher);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to create shared tree");
        return -4;
    }
    
    debug_if(s_debug_more, L_INFO, "Created shared tree with %zu signers", num_signers);
    
    // Создаем индивидуальные подписи
    chipmunk_individual_sig_t individual_sigs[num_signers];
    
    debug_if(s_debug_more, L_INFO, "Creating individual signatures...");
    
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_create_individual_signature(
            (uint8_t*)test_message, message_len,
            &hots_secret_keys[i], &hots_public_keys[i],
            &tree, i,  // leaf_index = i (позиция в общем дереве)
            &individual_sigs[i]
        );
        
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to create individual signature for signer %zu", i);
            return -5;
        }
        
        debug_if(s_debug_more, L_INFO, "Created individual signature for signer %zu", i);
    }
    
    // Агрегируем подписи
    chipmunk_multi_signature_t multi_sig;
    
    debug_if(s_debug_more, L_INFO, "Aggregating signatures...");
    
    ret = chipmunk_aggregate_signatures_with_tree(
        individual_sigs, num_signers,
        (uint8_t*)test_message, message_len,
        &tree, &multi_sig
    );
    
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to aggregate signatures, error: %d", ret);
        return -6;
    }
    
    double aggregation_time = get_time_ms() - start_time - keygen_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Aggregation time: %.3f seconds", aggregation_time);
    debug_if(s_debug_more, L_INFO, "Successfully aggregated %zu signatures", num_signers);
    
    // Проверяем агрегированную подпись
    debug_if(s_debug_more, L_INFO, "Verifying aggregated signature...");
    
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);
    
    double verification_time = get_time_ms() - start_time - keygen_time - aggregation_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Verification time: %.3f seconds", verification_time);
    
    if (ret != 1) {
        log_it(L_ERROR, "ERROR: Multi-signature verification failed, result: %d", ret);
        return -7;
    }
    
    debug_if(s_debug_more, L_INFO, "Multi-signature verification PASSED!");
    
    // Тест с неправильным сообщением (должен провалиться)
    const char wrong_message[] = "Wrong message";
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)wrong_message, strlen(wrong_message));
    
    if (ret > 0) {
        log_it(L_ERROR, "ERROR: Multi-signature verification with wrong message should have failed");
        return -8;
    }
    
    debug_if(s_debug_more, L_INFO, "Wrong message verification correctly failed");
    
    // Cleanup
    chipmunk_tree_clear(&tree);
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    chipmunk_multi_signature_free(&multi_sig);
    
    double total_time = get_time_ms() - start_time;
    
    // Always show performance summary for 3-signer test
    log_it(L_NOTICE, "✅ 3-signer multi-signature test PASSED");
    log_it(L_NOTICE, "   ⏱️ Total time: %.3f seconds", total_time);
    log_it(L_NOTICE, "   📊 Keygen: %.3f ms/signer", keygen_time * 1000.0 / num_signers);
    log_it(L_NOTICE, "   📊 Aggregation: %.3f ms", aggregation_time * 1000.0);
    log_it(L_NOTICE, "   📊 Verification: %.3f ms", verification_time * 1000.0);
    
    debug_if(s_debug_more, L_INFO, "Multi-signature aggregation test COMPLETED successfully");
    return 0;
}

/**
 * @brief Test multi-signature aggregation with 5 signers
 */
static int test_large_multi_signature_aggregation(void)
{
    double start_time = get_time_ms();
    
    debug_if(s_debug_more, L_INFO, "=== Large Multi-Signature Aggregation Test (5 signers) ===");
    
    const size_t num_signers = 5;
    const char test_message[] = "Large consortium blockchain transaction";
    const size_t message_len = strlen(test_message);
    
    // Создаем ключи для всех участников
    chipmunk_private_key_t private_keys[num_signers];
    chipmunk_public_key_t public_keys[num_signers];
    chipmunk_hots_pk_t hots_public_keys[num_signers];
    chipmunk_hots_sk_t hots_secret_keys[num_signers];
    
    debug_if(s_debug_more, L_INFO, "Generating keys for %zu signers...", num_signers);
    
    for (size_t i = 0; i < num_signers; i++) {
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate keypair for signer %zu", i);
            return -1;
        }
        
        // Получаем HOTS ключи из Chipmunk ключей
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        // Генерируем HOTS ключи
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_setup(&hots_params) != 0) {
            log_it(L_ERROR, "ERROR: Failed to setup HOTS params for signer %zu", i);
            return -1;
        }
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
            log_it(L_ERROR, "ERROR: Failed to generate HOTS keys for signer %zu", i);
            return -1;
        }
        
        debug_if(s_debug_more, L_INFO, "Generated keypair for signer %zu", i);
    }
    
    double keygen_time = get_time_ms() - start_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Key generation time: %.3f seconds (%.3f ms per signer)", 
             keygen_time, keygen_time * 1000.0 / num_signers);
    
    // Создаем ОДНО Merkle дерево для всех участников
    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;
    
    // Инициализируем hasher с тестовым seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to initialize HVC hasher");
        return -2;
    }
    
    // Создаем массив листов для дерева
    chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
    memset(leaf_nodes, 0, sizeof(leaf_nodes));
    
    // Добавляем публичные ключи всех участников в дерево
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to convert HOTS pk to HVC poly for signer %zu", i);
            return -3;
        }
    }
    
    // Создаем дерево со всеми участниками
    ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, num_signers, &hasher);
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to create shared tree");
        return -4;
    }
    
    debug_if(s_debug_more, L_INFO, "Created shared tree with %zu signers", num_signers);
    
    // Создаем индивидуальные подписи
    chipmunk_individual_sig_t individual_sigs[num_signers];
    
    debug_if(s_debug_more, L_INFO, "Creating individual signatures...");
    
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_create_individual_signature(
            (uint8_t*)test_message, message_len,
            &hots_secret_keys[i], &hots_public_keys[i],
            &tree, i,  // leaf_index = i (позиция в общем дереве)
            &individual_sigs[i]
        );
        
        if (ret != 0) {
            log_it(L_ERROR, "ERROR: Failed to create individual signature for signer %zu", i);
            return -5;
        }
        
        debug_if(s_debug_more, L_INFO, "Created individual signature for signer %zu", i);
    }
    
    // Агрегируем подписи
    chipmunk_multi_signature_t multi_sig;
    
    debug_if(s_debug_more, L_INFO, "Aggregating signatures...");
    
    ret = chipmunk_aggregate_signatures_with_tree(
        individual_sigs, num_signers,
        (uint8_t*)test_message, message_len,
        &tree, &multi_sig
    );
    
    if (ret != 0) {
        log_it(L_ERROR, "ERROR: Failed to aggregate signatures, result: %d", ret);
        return -6;
    }
    
    double aggregation_time = get_time_ms() - start_time - keygen_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Aggregation time: %.3f seconds", aggregation_time);
    debug_if(s_debug_more, L_INFO, "Successfully aggregated %zu signatures", num_signers);
    
    // Проверяем агрегированную подпись
    debug_if(s_debug_more, L_INFO, "Verifying aggregated signature...");
    
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);
    
    double verification_time = get_time_ms() - start_time - keygen_time - aggregation_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Verification time: %.3f seconds", verification_time);
    
    if (ret != 1) {
        log_it(L_ERROR, "ERROR: Multi-signature verification failed, result: %d", ret);
        return -7;
    }
    
    debug_if(s_debug_more, L_INFO, "Large multi-signature verification PASSED!");
    
    // Тест с неправильным сообщением (должен провалиться)
    const char wrong_message[] = "Wrong message";
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)wrong_message, strlen(wrong_message));
    
    if (ret == 0 || ret < 0) {
        debug_if(s_debug_more, L_INFO, "Wrong message verification correctly failed");
    } else {
        log_it(L_ERROR, "ERROR: Wrong message verification should have failed!");
        return -8;
    }
    
    // Cleanup
    chipmunk_tree_clear(&tree);
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    chipmunk_multi_signature_free(&multi_sig);
    
    double total_time = get_time_ms() - start_time;
    
    // Always show performance summary for 5-signer test
    log_it(L_NOTICE, "✅ 5-signer multi-signature test PASSED");
    log_it(L_NOTICE, "   ⏱️ Total time: %.3f seconds", total_time);
    log_it(L_NOTICE, "   📊 Keygen: %.3f ms/signer", keygen_time * 1000.0 / num_signers);
    log_it(L_NOTICE, "   📊 Aggregation: %.3f ms", aggregation_time * 1000.0);
    log_it(L_NOTICE, "   📊 Verification: %.3f ms", verification_time * 1000.0);
    
    debug_if(s_debug_more, L_INFO, "Large multi-signature aggregation test COMPLETED successfully\n");
    
    return 0;
}

/**
 * @brief Test batch verification of multiple multi-signatures
 */
static int test_batch_verification(void)
{
    double start_time = get_time_ms();
    
    debug_if(s_debug_more, L_INFO, "=== Batch Verification Test ===");
    
    const size_t num_batches = 3;
    const size_t signers_per_batch = 3;
    
    // Storage for all multi-signatures and messages
    chipmunk_multi_signature_t multi_sigs[num_batches];
    char batch_messages[num_batches][64];
    
    debug_if(s_debug_more, L_INFO, "Creating %zu multi-signatures with %zu signers each...", num_batches, signers_per_batch);
    
    for (size_t batch = 0; batch < num_batches; batch++) {
        debug_if(s_debug_more, L_INFO, "\\nProcessing batch %zu...", batch);
        
        // Generate unique message for this batch
        snprintf(batch_messages[batch], sizeof(batch_messages[batch]),
                "Batch %zu transaction message", batch);
        
        const size_t message_len = strlen(batch_messages[batch]);
        
        // Generate keys for this batch
        chipmunk_private_key_t private_keys[signers_per_batch];
        chipmunk_public_key_t public_keys[signers_per_batch];
        chipmunk_hots_pk_t hots_public_keys[signers_per_batch];
        chipmunk_hots_sk_t hots_secret_keys[signers_per_batch];
        
        for (size_t i = 0; i < signers_per_batch; i++) {
            int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                       (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
            if (ret != 0) {
                return -1;
            }
            
            hots_public_keys[i].v0 = private_keys[i].pk.v0;
            hots_public_keys[i].v1 = private_keys[i].pk.v1;
            
            chipmunk_hots_params_t hots_params;
            if (chipmunk_hots_setup(&hots_params) != 0) {
                return -1;
            }
            
            uint8_t hots_seed[32];
            memcpy(hots_seed, private_keys[i].key_seed, 32);
            uint32_t counter = (uint32_t)(batch * signers_per_batch + i);
            
            if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                    &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
                return -1;
            }
        }
        
        // Create tree for this batch
        chipmunk_tree_t tree;
        chipmunk_hvc_hasher_t hasher;
        
        uint8_t hasher_seed[32];
        memset(hasher_seed, batch + 1, 32);  // Unique seed per batch
        int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
        if (ret != 0) {
            return -2;
        }
        
        chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT_DEFAULT];
        memset(leaf_nodes, 0, sizeof(leaf_nodes));
        
        for (size_t i = 0; i < signers_per_batch; i++) {
            ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
            if (ret != 0) {
                return -3;
            }
        }
        
        ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, signers_per_batch, &hasher);
        if (ret != 0) {
            return -4;
        }
        
        // Create individual signatures
        chipmunk_individual_sig_t individual_sigs[signers_per_batch];
        
        for (size_t i = 0; i < signers_per_batch; i++) {
            ret = chipmunk_create_individual_signature(
                (uint8_t*)batch_messages[batch], message_len,
                &hots_secret_keys[i], &hots_public_keys[i],
                &tree, i,
                &individual_sigs[i]
            );
            
            if (ret != 0) {
                return -5;
            }
        }
        
        // Aggregate this batch
        ret = chipmunk_aggregate_signatures_with_tree(
            individual_sigs, signers_per_batch,
            (uint8_t*)batch_messages[batch], message_len,
            &tree, &multi_sigs[batch]
        );
        
        if (ret != 0) {
            return -6;
        }
        
        debug_if(s_debug_more, L_INFO, "Batch %zu multi-signature created successfully", batch);
        
        // Cleanup temporary resources
        chipmunk_tree_clear(&tree);
        for (size_t i = 0; i < signers_per_batch; i++) {
            chipmunk_individual_signature_free(&individual_sigs[i]);
        }
    }
    
    double batch_creation_time = get_time_ms() - start_time;
    debug_if(s_debug_more, L_INFO, "⏱️ Batch creation time: %.3f seconds", batch_creation_time);
    
    debug_if(s_debug_more, L_INFO, "\\nVerifying all multi-signatures in batch...");
    
    // Verify each multi-signature
    for (size_t batch = 0; batch < num_batches; batch++) {
        int ret = chipmunk_verify_multi_signature(&multi_sigs[batch], 
                                                  (uint8_t*)batch_messages[batch], 
                                                  strlen(batch_messages[batch]));
        if (ret != 1) {
            log_it(L_ERROR, "ERROR: Batch %zu verification failed", batch);
            return -7;
        }
        debug_if(s_debug_more, L_INFO, "Batch %zu verification: PASSED", batch);
    }
    
    double batch_verification_time = get_time_ms() - start_time - batch_creation_time;
    double total_time = get_time_ms() - start_time;
    
    // Cleanup all multi-signatures
    for (size_t batch = 0; batch < num_batches; batch++) {
        chipmunk_multi_signature_free(&multi_sigs[batch]);
    }
    
    // Always show performance summary for batch test
    log_it(L_NOTICE, "✅ Batch verification test PASSED");
    log_it(L_NOTICE, "   📊 %zu batches × %zu signers = %zu total signatures", 
           num_batches, signers_per_batch, num_batches * signers_per_batch);
    log_it(L_NOTICE, "   ⏱️ Creation time: %.3f seconds (%.3f ms per multi-sig)", 
           batch_creation_time, batch_creation_time * 1000.0 / num_batches);
    log_it(L_NOTICE, "   ⏱️ Verification time: %.3f seconds (%.3f ms per multi-sig)", 
           batch_verification_time, batch_verification_time * 1000.0 / num_batches);
    log_it(L_NOTICE, "   ⏱️ Total time: %.3f seconds", total_time);
    
    debug_if(s_debug_more, L_INFO, "Batch verification test COMPLETED successfully\n");
    
    return 0;
}

int main(void)
{
    // Initialize logging with clean format for unit tests
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);  // Clean output without timestamps/modules
    
    // Initialize Chipmunk module
    dap_enc_chipmunk_init();
    
    // Allow enabling debug output via environment variable
    char *debug_env = getenv("CHIPMUNK_DEBUG");
    if (debug_env && (strcmp(debug_env, "1") == 0 || strcmp(debug_env, "true") == 0)) {
        s_debug_more = true;
        log_it(L_INFO, "🔧 Debug output enabled");
    }

    log_it(L_NOTICE, "🚀 CHIPMUNK STANDALONE TESTS");
    log_it(L_NOTICE, "Multi-signature aggregation & batch verification");
    log_it(L_NOTICE, " ");
    
    double overall_time;
    
    // Test 1: 3-signer multi-signature
    int test1_result = test_multi_signature_aggregation();
    if (test1_result != 0) {
        goto cleanup;
    }
    
    // Test 2: 5-signer multi-signature
    int test2_result = test_large_multi_signature_aggregation();
    if (test2_result != 0) {
        goto cleanup;
    }
    
    // Test 3: Batch verification
    int test3_result = test_batch_verification();
    if (test3_result != 0) {
        goto cleanup;
    }

cleanup:
    overall_time = get_time_ms();
    
    if (test1_result == 0 && test2_result == 0 && test3_result == 0) {
        log_it(L_NOTICE, " ");
        log_it(L_NOTICE, "🎉 ALL TESTS PASSED SUCCESSFULLY!");
        log_it(L_NOTICE, "✅ 3-signer multi-signature: PASSED");
        log_it(L_NOTICE, "✅ 5-signer multi-signature: PASSED");
        log_it(L_NOTICE, "✅ Batch verification: PASSED");
        log_it(L_NOTICE, " ");
        log_it(L_NOTICE, "⏱️ Overall test time: %.3f seconds", overall_time);
        log_it(L_NOTICE, " ");
        log_it(L_NOTICE, "🏆 Chipmunk multi-signature scheme is fully functional!");
    } else {
        log_it(L_ERROR, " ");
        log_it(L_ERROR, "❌ Tests FAILED with code: %d", test1_result != 0 ? test1_result : test2_result != 0 ? test2_result : test3_result);
        log_it(L_ERROR, "⏱️ Test time before failure: %.3f seconds", overall_time);
    }
    
    return test1_result != 0 ? test1_result : test2_result != 0 ? test2_result : test3_result;
} 