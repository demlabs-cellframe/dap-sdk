#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_hots.h"
#include "chipmunk/chipmunk_aggregation.h"
#include "chipmunk/chipmunk_tree.h"

#define LOG_TAG "chipmunk_test_only"

/**
 * @brief Test multi-signature aggregation with 3 signers
 */
static int test_multi_signature_aggregation(void)
{
    printf("=== Multi-Signature Aggregation Test ===\n");
    
    const size_t num_signers = 3;
    const char test_message[] = "Multi-party contract agreement";
    const size_t message_len = strlen(test_message);
    
    // Создаем ключи для всех участников
    chipmunk_private_key_t private_keys[num_signers];
    chipmunk_public_key_t public_keys[num_signers];
    chipmunk_hots_pk_t hots_public_keys[num_signers];
    chipmunk_hots_sk_t hots_secret_keys[num_signers];
    
    printf("Generating keys for %zu signers...\n", num_signers);
    
    for (size_t i = 0; i < num_signers; i++) {
        int ret = chipmunk_keypair((uint8_t*)&public_keys[i], sizeof(chipmunk_public_key_t),
                                   (uint8_t*)&private_keys[i], sizeof(chipmunk_private_key_t));
        if (ret != 0) {
            printf("ERROR: Failed to generate keypair for signer %zu\n", i);
            return -1;
        }
        
        // Получаем HOTS ключи из Chipmunk ключей
        hots_public_keys[i].v0 = private_keys[i].pk.v0;
        hots_public_keys[i].v1 = private_keys[i].pk.v1;
        
        // Генерируем HOTS ключи
        chipmunk_hots_params_t hots_params;
        if (chipmunk_hots_setup(&hots_params) != 0) {
            printf("ERROR: Failed to setup HOTS params for signer %zu\n", i);
            return -1;
        }
        
        uint8_t hots_seed[32];
        memcpy(hots_seed, private_keys[i].key_seed, 32);
        uint32_t counter = (uint32_t)i;
        
        if (chipmunk_hots_keygen(hots_seed, counter, &hots_params, 
                                &hots_public_keys[i], &hots_secret_keys[i]) != 0) {
            printf("ERROR: Failed to generate HOTS keys for signer %zu\n", i);
            return -1;
        }
        
        printf("Generated keypair for signer %zu\n", i);
    }
    
    // Создаем ОДНО Merkle дерево для всех участников
    chipmunk_tree_t tree;
    chipmunk_hvc_hasher_t hasher;
    
    // Инициализируем hasher с тестовым seed
    uint8_t hasher_seed[32] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,
                              17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32};
    int ret = chipmunk_hvc_hasher_init(&hasher, hasher_seed);
    if (ret != 0) {
        printf("ERROR: Failed to initialize HVC hasher\n");
        return -2;
    }
    
    // Создаем массив листов для дерева
    chipmunk_hvc_poly_t leaf_nodes[CHIPMUNK_TREE_LEAF_COUNT];
    memset(leaf_nodes, 0, sizeof(leaf_nodes));
    
    // Добавляем публичные ключи всех участников в дерево
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_hots_pk_to_hvc_poly(&public_keys[i], &leaf_nodes[i]);
        if (ret != 0) {
            printf("ERROR: Failed to convert HOTS pk to HVC poly for signer %zu\n", i);
            return -3;
        }
    }
    
    // Создаем дерево со всеми участниками
    ret = chipmunk_tree_new_with_leaf_nodes(&tree, leaf_nodes, &hasher);
    if (ret != 0) {
        printf("ERROR: Failed to create shared tree\n");
        return -4;
    }
    
    printf("Created shared tree with %zu signers\n", num_signers);
    
    // Создаем индивидуальные подписи
    chipmunk_individual_sig_t individual_sigs[num_signers];
    
    printf("Creating individual signatures...\n");
    
    for (size_t i = 0; i < num_signers; i++) {
        ret = chipmunk_create_individual_signature(
            (uint8_t*)test_message, message_len,
            &hots_secret_keys[i], &hots_public_keys[i],
            &tree, i,  // leaf_index = i (позиция в общем дереве)
            &individual_sigs[i]
        );
        
        if (ret != 0) {
            printf("ERROR: Failed to create individual signature for signer %zu\n", i);
            return -5;
        }
        
        printf("Created individual signature for signer %zu\n", i);
    }
    
    // Агрегируем подписи
    chipmunk_multi_signature_t multi_sig;
    
    printf("Aggregating signatures...\n");
    
    ret = chipmunk_aggregate_signatures_with_tree(
        individual_sigs, num_signers,
        (uint8_t*)test_message, message_len,
        &tree, &multi_sig
    );
    
    if (ret != 0) {
        printf("ERROR: Failed to aggregate signatures, error: %d\n", ret);
        return -6;
    }
    
    printf("Successfully aggregated %zu signatures\n", num_signers);
    
    // Проверяем агрегированную подпись
    printf("Verifying aggregated signature...\n");
    
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)test_message, message_len);
    
    if (ret != 1) {
        printf("ERROR: Multi-signature verification failed, result: %d\n", ret);
        return -7;
    }
    
    printf("Multi-signature verification PASSED!\n");
    
    // Тест с неправильным сообщением (должен провалиться)
    const char wrong_message[] = "Wrong message";
    ret = chipmunk_verify_multi_signature(&multi_sig, (uint8_t*)wrong_message, strlen(wrong_message));
    
    if (ret > 0) {
        printf("ERROR: Multi-signature verification with wrong message should have failed\n");
        return -8;
    }
    
    printf("Wrong message verification correctly failed\n");
    
    // Cleanup
    chipmunk_tree_clear(&tree);
    for (size_t i = 0; i < num_signers; i++) {
        chipmunk_individual_signature_free(&individual_sigs[i]);
    }
    chipmunk_multi_signature_free(&multi_sig);
    
    printf("Multi-signature aggregation test COMPLETED successfully\n");
    return 0;
}

int main(void)
{
    printf("Starting Chipmunk aggregation tests...\n");
    
    // Initialize Chipmunk
    dap_enc_chipmunk_init();
    
    int result = test_multi_signature_aggregation();
    
    if (result == 0) {
        printf("\n✅ All tests PASSED!\n");
    } else {
        printf("\n❌ Tests FAILED with code: %d\n", result);
    }
    
    return result;
} 