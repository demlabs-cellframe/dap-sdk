#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "chipmunk/chipmunk.h"

#define LOG_TAG "dap_enc_chipmunk_test"
#define TEST_DATA "This is test data for Chipmunk algorithm verification"
#define INVALID_TEST_DATA "This is invalid test data"

// Custom assert implementation
#define dap_assert(condition, message) \
    do { \
        if (!(condition)) { \
            log_it(L_ERROR, "Assertion failed: %s", message); \
            return -1; \
        } \
    } while (0)

/**
 * @brief Test for Chipmunk key creation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_new_test(void)
{
    // Initialize cryptography module
    dap_enc_chipmunk_init();
    
    // Create a new key
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    
    // Verify the key was created correctly
    dap_assert(l_key != NULL, "Key successfully created");
    dap_assert(l_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, "Key type is correct");
    dap_assert(l_key->priv_key_data != NULL, "Private key is not NULL");
    dap_assert(l_key->pub_key_data != NULL, "Public key is not NULL");
    
    
    // Cleanup
    dap_enc_key_delete(l_key);
    
    return 0;
}

/**
 * @brief Test key pair generation for Chipmunk
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_generate_test(void)
{
    // Generate two different keys
    dap_enc_key_t *l_key1 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    if (!l_key1) {
        log_it(L_ERROR, "Failed to generate first Chipmunk key");
        return -1;
    }
    
    dap_enc_key_t *l_key2 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    if (!l_key2) {
        log_it(L_ERROR, "Failed to generate second Chipmunk key");
        dap_enc_key_delete(l_key1);
        return -1;
    }
    
    // Make sure keys public keys differ
    int l_ret = 0;
    if (l_key1->pub_key_data_size != l_key2->pub_key_data_size) {
        log_it(L_ERROR, "Different public key sizes: %"DAP_UINT64_FORMAT_U" vs %"DAP_UINT64_FORMAT_U"",
               l_key1->pub_key_data_size, l_key2->pub_key_data_size);
        l_ret = -2;
    } else {
        // Compare public keys - должны быть разными
        if (memcmp(l_key1->pub_key_data, l_key2->pub_key_data, l_key1->pub_key_data_size) == 0) {
            log_it(L_ERROR, "Both keys have the same public key - this should not happen");
            l_ret = -3;
        }
    }
    
    // Make sure private keys differ
    if (l_key1->priv_key_data_size != l_key2->priv_key_data_size) {
        log_it(L_ERROR, "Different private key sizes: %"DAP_UINT64_FORMAT_U" vs %"DAP_UINT64_FORMAT_U"",
               l_key1->priv_key_data_size, l_key2->priv_key_data_size);
        l_ret = -4;
    } else {
        // Compare private keys - должны быть разными
        if (memcmp(l_key1->priv_key_data, l_key2->priv_key_data, l_key1->priv_key_data_size) == 0) {
            log_it(L_ERROR, "Both keys have the same private key - this should not happen");
            l_ret = -5;
        }
    }
    
    // Удаляем ключи
    dap_enc_key_delete(l_key1);
    dap_enc_key_delete(l_key2);
    
    return l_ret;
}

/**
 * @brief Test for Chipmunk signature creation and verification
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_sign_verify_test(void)
{
    // Для сбора результатов тестов
    int l_result = 0;
    
    // Create a key for signing
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    if (!l_key) {
        log_it(L_ERROR, "Failed to create Chipmunk key");
        return -1;
    }
    
    // Calculate signature size
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    if (l_sign_size <= 0 || l_sign_size != CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Invalid signature size: expected %d, got %zu", CHIPMUNK_SIGNATURE_SIZE, l_sign_size);
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    // Allocate memory for signature
    uint8_t *l_sign = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    if (!l_sign) {
        log_it(L_ERROR, "Failed to allocate memory for signature");
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    // Prepare test message
    const char l_message[] = "Test message for chipmunk signature";
    size_t l_message_len = strlen(l_message);
    
    // Try to sign message
    log_it(L_INFO, "Signing test message with Chipmunk algorithm");
    int l_sign_result = dap_enc_chipmunk_get_sign(l_key, l_message, l_message_len, l_sign, l_sign_size);
    
    // Check if signing was successful
    if (l_sign_result <= 0) {
        log_it(L_ERROR, "Chipmunk sign failed, error code: %d", l_sign_result);
        l_result = -2;
        // Не возвращаем сразу ошибку, чтобы проверить еще некоторые тестовые случаи
    } else {
        log_it(L_DEBUG, "Chipmunk sign succeeded, signature size: %d", l_sign_result);
        
        // Verify signature
        log_it(L_INFO, "Verifying Chipmunk signature");
        int l_verify_result = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, l_sign, l_sign_size);
        
        if (l_verify_result != 0) {
            log_it(L_ERROR, "Chipmunk signature verification failed, error code: %d", l_verify_result);
            l_result = -3;
            // Не выходим сразу, продолжаем тесты
        } else {
            log_it(L_NOTICE, "Chipmunk signature verified successfully");
        }
        
        // Проверка с измененным сообщением (должна не пройти)
        const char l_modified_message[] = "Modified message for chipmunk signature";
        size_t l_modified_message_len = strlen(l_modified_message);
        
        log_it(L_INFO, "Testing signature verification with modified message (should fail)");
        int l_verify_modified = dap_enc_chipmunk_verify_sign(l_key, l_modified_message, l_modified_message_len, l_sign, l_sign_size);
        
        // Измененное сообщение должно не пройти верификацию (результат должен быть отрицательным)
        if (l_verify_modified == 0) {
            log_it(L_ERROR, "Chipmunk signature verification with modified message passed incorrectly!");
            l_result = -4;
        } else {
            log_it(L_NOTICE, "Chipmunk signature verification with modified message correctly failed (expected behavior)");
        }
    }
    
    // Освобождаем ресурсы
    DAP_DELETE(l_sign);
    dap_enc_key_delete(l_key);
    
    return l_result;
}

/**
 * @brief Test for Chipmunk signature size calculation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_size_test(void)
{
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    
    // Check if the returned size matches expected value
    if (l_sign_size != CHIPMUNK_SIGNATURE_SIZE) {
        log_it(L_ERROR, "Incorrect signature size: expected %d, got %zu", 
               CHIPMUNK_SIGNATURE_SIZE, l_sign_size);
        return -1;
    }
    
    log_it(L_NOTICE, "Signature size calculation is correct: %zu bytes", l_sign_size);
    return 0;
}

/**
 * @brief Test for Chipmunk key deletion
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_delete_test(void)
{
    // Create a key
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    if (!l_key) {
        log_it(L_ERROR, "Failed to create Chipmunk key");
        return -1;
    }
    
    // Check if the key was allocated correctly with non-NULL fields
    if (!l_key->priv_key_data || !l_key->pub_key_data) {
        log_it(L_ERROR, "Key data pointers are NULL");
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    // Delete the key
    dap_enc_key_delete(l_key);
    
    // We cannot check if fields are NULL since key is already freed
    // The success is measured by not having segmentation faults
    
    return 0;
}

/**
 * @brief Test specifically for the Chipmunk challenge polynomial generation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_challenge_poly_test(void)
{
    log_it(L_INFO, "Testing Chipmunk challenge polynomial generation...");
    
    // Создаем seed для генерации полинома challenge
    uint8_t l_seed[32];
    for (int i = 0; i < 32; i++) {
        l_seed[i] = (uint8_t)i;  // Детерминированные значения для воспроизводимости
    }
    
    // Создаем два полинома challenge с одинаковым seed - они должны быть идентичны
    chipmunk_poly_t l_poly1 = {0};
    chipmunk_poly_t l_poly2 = {0};
    
    int l_res1 = chipmunk_poly_challenge(&l_poly1, l_seed);
    int l_res2 = chipmunk_poly_challenge(&l_poly2, l_seed);
    
    if (l_res1 != 0 || l_res2 != 0) {
        log_it(L_ERROR, "Failed to generate challenge polynomials: %d, %d", l_res1, l_res2);
        return -1;
    }
    
    // Проверяем, что полиномы идентичны
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_poly1.coeffs[i] != l_poly2.coeffs[i]) {
            log_it(L_ERROR, "Challenge polynomials differ at position %d: %d vs %d", 
                  i, l_poly1.coeffs[i], l_poly2.coeffs[i]);
            return -1;
        }
    }
    
    // Проверяем, сколько ненулевых коэффициентов в полиноме
    int l_nonzero_count = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (l_poly1.coeffs[i] != 0) {
            l_nonzero_count++;
        }
    }
    
    // Для полинома challenge должно быть ровно CHIPMUNK_TAU ненулевых коэффициентов
    if (l_nonzero_count != CHIPMUNK_TAU) {
        log_it(L_ERROR, "Challenge polynomial has %d nonzero coefficients, expected %d", 
              l_nonzero_count, CHIPMUNK_TAU);
        return -2;
    }
    
    log_it(L_NOTICE, "Challenge polynomial test passed: %d nonzero coefficients (expected %d)",
           l_nonzero_count, CHIPMUNK_TAU);
    
    return 0;
}

/**
 * @brief Run all Chipmunk tests.
 * 
 * @return int 0 if all tests pass, non-zero otherwise
 */
int dap_enc_chipmunk_tests_run(void)
{
    // Initialize the module
    dap_enc_chipmunk_init();
    
    int l_ret = 0; // Успешный результат, if all tests pass
    
    // Test key creation
    log_it(L_INFO, "Testing Chipmunk key creation...");
    l_ret += dap_enc_chipmunk_key_new_test();
    log_it(L_INFO, "Key creation test %s", l_ret == 0 ? "PASSED" : "FAILED");
    
    // Test key pair generation
    log_it(L_INFO, "Testing Chipmunk key pair generation...");
    int l_res = dap_enc_chipmunk_key_generate_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_INFO, "Key pair generation test FAILED");
    } else {
        log_it(L_INFO, "Key pair generation test PASSED");
    }
    
    // Test challenge polynomial generation specifically
    log_it(L_INFO, "Testing Chipmunk challenge polynomial generation...");
    l_res = dap_enc_chipmunk_challenge_poly_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_ERROR, "Challenge polynomial test FAILED! This is a critical issue.");
    } else {
        log_it(L_INFO, "Challenge polynomial test PASSED");
    }
    
    // Test signature generation and verification
    log_it(L_INFO, "Testing Chipmunk signature...");
    l_res = dap_enc_chipmunk_sign_verify_test();
    if (l_res != 0) {
        // Считаем проблему с верификацией подписи критической ошибкой
        l_ret += 1;
        log_it(L_ERROR, "Signature test FAILED! Critical issue with challenge polynomial detected");
    } else {
        log_it(L_INFO, "Signature test PASSED");
    }
    
    // Test signature size calculation
    log_it(L_INFO, "Testing Chipmunk signature size calculation...");
    l_res = dap_enc_chipmunk_size_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_INFO, "Signature size calculation test FAILED");
    } else {
        log_it(L_INFO, "Signature size calculation test PASSED");
    }
    
    // Test key deletion
    log_it(L_INFO, "Testing Chipmunk key deletion...");
    l_res = dap_enc_chipmunk_key_delete_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_INFO, "Key deletion test FAILED");
    } else {
        log_it(L_INFO, "Key deletion test PASSED");
    }
    
    // Return 0 if all tests passed, non-zero otherwise
    if (l_ret != 0) {
        log_it(L_ERROR, "Some Chipmunk tests FAILED! Error code: %d", l_ret);
    } else {
        log_it(L_NOTICE, "All Chipmunk tests PASSED!");
    }
    
    return l_ret;
} 
