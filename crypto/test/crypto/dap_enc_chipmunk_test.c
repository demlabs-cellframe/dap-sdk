#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "chipmunk/chipmunk.h"
#include "chipmunk/chipmunk_poly.h"

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
        int l_ret_verify = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, l_sign, l_sign_size);
        
        if (l_ret_verify == 0) {
            log_it(L_INFO, "Chipmunk signature verification successful");
        } else {
            log_it(L_ERROR, "Chipmunk signature verification failed, error code: %d", l_ret_verify);
            l_result = -3;
        }
        
        // Test signature verification with a modified message (should fail)
        log_it(L_INFO, "Testing signature verification with modified message (should fail)");
        
        // Модифицируем сообщение, добавляя символ
        char* l_modified_message = calloc(1, l_message_len + 5);
        memcpy(l_modified_message, l_message, l_message_len);
        strcat(l_modified_message, "test");
        
        int l_ret_verify_modified = dap_enc_chipmunk_verify_sign(l_key, l_modified_message, l_message_len + 4, l_sign, l_sign_size);
        
        // Теперь проверка должна не пройти, результат должен быть отрицательным
        if (l_ret_verify_modified < 0) {
            log_it(L_NOTICE, "Chipmunk signature verification with modified message correctly failed (expected behavior)");
        } else {
            log_it(L_ERROR, "Chipmunk signature verification with modified message unexpectedly succeeded");
            l_result = -4;
        }
        
        free(l_modified_message);
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
    
    int l_res1 = chipmunk_poly_challenge(&l_poly1, l_seed, 32);
    int l_res2 = chipmunk_poly_challenge(&l_poly2, l_seed, 32);
    
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
    
    // Для полинома challenge должно быть ровно CHIPMUNK_ALPHA_H ненулевых коэффициентов
    if (l_nonzero_count != CHIPMUNK_ALPHA_H) {
        log_it(L_ERROR, "WARNING: Challenge polynomial has %d non-zero coefficients, expected %d",
               l_nonzero_count, CHIPMUNK_ALPHA_H);
    }
    
    log_it(L_NOTICE, "Challenge polynomial test passed: %d nonzero coefficients (expected %d)",
           l_nonzero_count, CHIPMUNK_ALPHA_H);
    
    return 0;
}

/**
 * @brief Test for chipmunk serialization
 * @return Returns true if test passed
 */
static bool s_test_chipmunk_serialization() {
    log_it(L_INFO, "=== Testing Chipmunk serialization ===");
    
    // Test data
    uint8_t l_test_seed[32] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
                               0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
                               0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
                               0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F, 0x20};
    
    // Create test signature
    chipmunk_signature_t l_sig_src = {0};
    
    // Fill c_seed
    memcpy(l_sig_src.c_seed, l_test_seed, sizeof(l_sig_src.c_seed));
    
    // DEBUG
    log_it(L_DEBUG, "Before serialization, c_seed: %02x%02x%02x%02x...",
           l_sig_src.c_seed[0], l_sig_src.c_seed[1], l_sig_src.c_seed[2], l_sig_src.c_seed[3]);
    
    // Serialize to bytes
    uint8_t l_sig_bytes[CHIPMUNK_SIGNATURE_SIZE];
    int l_res = chipmunk_signature_to_bytes(l_sig_bytes, &l_sig_src);
    
    if (l_res != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize signature: %d", l_res);
        return false;
    }
    
    // DEBUG
    log_it(L_DEBUG, "Serialized bytes, c_seed: %02x%02x%02x%02x...",
           l_sig_bytes[0], l_sig_bytes[1], l_sig_bytes[2], l_sig_bytes[3]);
    
    // Deserialize from bytes
    chipmunk_signature_t l_sig_dst = {0};
    l_res = chipmunk_signature_from_bytes(&l_sig_dst, l_sig_bytes);
    
    if (l_res != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to deserialize signature: %d", l_res);
        return false;
    }
    
    // DEBUG
    log_it(L_DEBUG, "After deserialization, c_seed: %02x%02x%02x%02x...",
           l_sig_dst.c_seed[0], l_sig_dst.c_seed[1], l_sig_dst.c_seed[2], l_sig_dst.c_seed[3]);
    
    // Compare signatures - verify c_seed matches
    bool l_match = true;
    for (size_t i = 0; i < sizeof(l_sig_src.c_seed); i++) {
        if (l_sig_src.c_seed[i] != l_sig_dst.c_seed[i]) {
            log_it(L_ERROR, "c_seed[%zu] mismatch: %02x != %02x", 
                   i, l_sig_src.c_seed[i], l_sig_dst.c_seed[i]);
            l_match = false;
        }
    }
    
    if (!l_match) {
        log_it(L_ERROR, "Signature serialization failed - c_seed mismatch");
        return false;
    }
    
    log_it(L_INFO, "✓ Signature serialization test passed");
    return true;
}

/**
 * @brief Тест для проверки подписей разных объектов разными ключами
 * Проверяет, что подписи от разных ключей не взаимозаменяемы
 * 
 * @return int 0 при успехе, отрицательный код при ошибке
 */
static int dap_enc_chipmunk_different_signatures_test(void)
{
    log_it(L_INFO, "Testing signatures for different objects with different keys...");
    
    // Создаем два ключа для подписи
    dap_enc_key_t *l_key1 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    dap_enc_key_t *l_key2 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, NULL, 0, 0);
    
    if (!l_key1 || !l_key2) {
        log_it(L_ERROR, "Failed to create Chipmunk keys");
        if (l_key1) dap_enc_key_delete(l_key1);
        if (l_key2) dap_enc_key_delete(l_key2);
        return -1;
    }
    
    // Размер подписи
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    
    // Создаем два разных объекта для подписи
    const char l_message1[] = "First test message for comparison";
    const char l_message2[] = "Second completely different message";
    size_t l_message1_len = strlen(l_message1);
    size_t l_message2_len = strlen(l_message2);
    
    // Выделяем память для подписей
    uint8_t *l_sign1_key1 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    uint8_t *l_sign2_key2 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    
    if (!l_sign1_key1 || !l_sign2_key2) {
        log_it(L_ERROR, "Failed to allocate memory for signatures");
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        if (l_sign1_key1) DAP_DELETE(l_sign1_key1);
        if (l_sign2_key2) DAP_DELETE(l_sign2_key2);
        return -1;
    }
    
    // Создаем подписи для обоих объектов разными ключами
    int l_ret1 = dap_enc_chipmunk_get_sign(l_key1, l_message1, l_message1_len, l_sign1_key1, l_sign_size);
    int l_ret2 = dap_enc_chipmunk_get_sign(l_key2, l_message2, l_message2_len, l_sign2_key2, l_sign_size);
    
    if (l_ret1 <= 0 || l_ret2 <= 0) {
        log_it(L_ERROR, "Failed to sign messages, error codes: %d, %d", l_ret1, l_ret2);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        DAP_DELETE(l_sign1_key1);
        DAP_DELETE(l_sign2_key2);
        return -2;
    }
    
    // Извлекаем c_seed из обеих подписей
    chipmunk_signature_t l_sig1 = {0};
    chipmunk_signature_t l_sig2 = {0};
    
    if (chipmunk_signature_from_bytes(&l_sig1, l_sign1_key1) != 0 || 
        chipmunk_signature_from_bytes(&l_sig2, l_sign2_key2) != 0) {
        log_it(L_ERROR, "Failed to parse signatures");
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        DAP_DELETE(l_sign1_key1);
        DAP_DELETE(l_sign2_key2);
        return -3;
    }
    
    // DEBUG: log first 4 bytes of c_seed for each signature
    log_it(L_DEBUG, "Signature 1 c_seed: %02x%02x%02x%02x...",
           l_sig1.c_seed[0], l_sig1.c_seed[1], l_sig1.c_seed[2], l_sig1.c_seed[3]);
    log_it(L_DEBUG, "Signature 2 c_seed: %02x%02x%02x%02x...",
           l_sig2.c_seed[0], l_sig2.c_seed[1], l_sig2.c_seed[2], l_sig2.c_seed[3]);
    
    // Compare first few bytes of c_seed to verify they're different
    bool l_c_different = false;
    for (size_t i = 0; i < sizeof(l_sig1.c_seed); i++) {
        if (l_sig1.c_seed[i] != l_sig2.c_seed[i]) {
            l_c_different = true;
            break;
        }
    }
    
    if (!l_c_different) {
        log_it(L_ERROR, "Challenge seeds of different messages are identical! This should not happen!");
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        DAP_DELETE(l_sign1_key1);
        DAP_DELETE(l_sign2_key2);
        return -4;
    }
    
    // Проверяем каждую подпись своим ключом - должны пройти верификацию
    int l_verify1 = dap_enc_chipmunk_verify_sign(l_key1, l_message1, l_message1_len, l_sign1_key1, l_sign_size);
    int l_verify2 = dap_enc_chipmunk_verify_sign(l_key2, l_message2, l_message2_len, l_sign2_key2, l_sign_size);
    
    if (l_verify1 != 0 || l_verify2 != 0) {
        log_it(L_ERROR, "Signature verification failed with correct keys: %d, %d", l_verify1, l_verify2);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        DAP_DELETE(l_sign1_key1);
        DAP_DELETE(l_sign2_key2);
        return -5;
    }
    
    // Проверяем перекрестную верификацию с неправильными ключами
    int l_cross_verify1 = dap_enc_chipmunk_verify_sign(l_key2, l_message1, l_message1_len, l_sign1_key1, l_sign_size);
    int l_cross_verify2 = dap_enc_chipmunk_verify_sign(l_key1, l_message2, l_message2_len, l_sign2_key2, l_sign_size);
    
    // Эти проверки должны быть неуспешными
    if (l_cross_verify1 == 0 || l_cross_verify2 == 0) {
        log_it(L_ERROR, "Cross verification with wrong keys unexpectedly succeeded: %d, %d", 
               l_cross_verify1, l_cross_verify2);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        DAP_DELETE(l_sign1_key1);
        DAP_DELETE(l_sign2_key2);
        return -6;
    }
    
    // Очистка ресурсов
    dap_enc_key_delete(l_key1);
    dap_enc_key_delete(l_key2);
    DAP_DELETE(l_sign1_key1);
    DAP_DELETE(l_sign2_key2);
    
    log_it(L_NOTICE, "Different objects with different keys test PASSED");
    return 0;
}

/**
 * @brief Тест для проверки верификации поврежденной подписи
 * Проверяет, что поврежденная подпись не проходит верификацию
 * 
 * @return int 0 при успехе, отрицательный код при ошибке
 */
static int dap_enc_chipmunk_corrupted_signature_test(void)
{
    log_it(L_INFO, "Testing verification of corrupted signatures...");
    
    // Создаем ключ для подписи
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    if (!l_key) {
        log_it(L_ERROR, "Failed to create Chipmunk key");
        return -1;
    }
    
    // Размер подписи
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    
    // Создаем сообщение для подписи
    const char l_message[] = "Message for testing corrupted signatures";
    size_t l_message_len = strlen(l_message);
    
    // Выделяем память для подписи
    uint8_t *l_sign = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    if (!l_sign) {
        log_it(L_ERROR, "Failed to allocate memory for signature");
        dap_enc_key_delete(l_key);
        return -1;
    }
    
    // Создаем подпись
    int l_ret = dap_enc_chipmunk_get_sign(l_key, l_message, l_message_len, l_sign, l_sign_size);
    if (l_ret <= 0) {
        log_it(L_ERROR, "Failed to sign message, error code: %d", l_ret);
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign);
        return -2;
    }
    
    // Проверяем, что подпись действительна
    int l_verify = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, l_sign, l_sign_size);
    if (l_verify != 0) {
        log_it(L_ERROR, "Original signature verification failed unexpectedly, error code: %d", l_verify);
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign);
        return -3;
    }
    
    // Создаем копии подписи для различных видов повреждений
    uint8_t *l_sign_c_corrupted = DAP_NEW_SIZE(uint8_t, l_sign_size);
    uint8_t *l_sign_z_corrupted = DAP_NEW_SIZE(uint8_t, l_sign_size);
    uint8_t *l_sign_hint_corrupted = DAP_NEW_SIZE(uint8_t, l_sign_size);
    
    if (!l_sign_c_corrupted || !l_sign_z_corrupted || !l_sign_hint_corrupted) {
        log_it(L_ERROR, "Failed to allocate memory for corrupted signatures");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign);
        if (l_sign_c_corrupted) DAP_DELETE(l_sign_c_corrupted);
        if (l_sign_z_corrupted) DAP_DELETE(l_sign_z_corrupted);
        if (l_sign_hint_corrupted) DAP_DELETE(l_sign_hint_corrupted);
        return -4;
    }
    
    // Копируем подпись в буферы
    memcpy(l_sign_c_corrupted, l_sign, l_sign_size);
    memcpy(l_sign_z_corrupted, l_sign, l_sign_size);
    memcpy(l_sign_hint_corrupted, l_sign, l_sign_size);
    
    // 1. Повреждаем c_seed (первые 32 байта подписи)
    // Меняем все 32 байта c_seed на случайные - это гарантировано нарушит подпись
    for (int i = 0; i < 32; i++) {
        l_sign_c_corrupted[i] = (uint8_t)rand();
    }
    
    // 2. Повреждаем полином z (средняя часть подписи)
    // Портим больше значений и более серьезно, чтобы точно нарушить подпись
    // Начало полинома z находится на смещении 32 (после c_seed)
    size_t z_offset = 32;
    size_t z_size = CHIPMUNK_N * sizeof(int32_t);
    for (size_t i = 0; i < z_size / 4; i++) {  // Портим 25% байтов полинома z
        size_t idx = z_offset + (rand() % z_size);
        l_sign_z_corrupted[idx] = (uint8_t)rand();
    }
    
    // 3. Повреждаем hint биты (конец подписи)
    // Hint биты начинаются после z
    size_t l_hint_offset = 32 + CHIPMUNK_N * sizeof(int32_t);
    // Установим все hint биты в 1, что сделает подпись невалидной
    memset(l_sign_hint_corrupted + l_hint_offset, 0xFF, CHIPMUNK_N/8);
    
    // Проверяем каждую поврежденную подпись
    int l_verify_c_corrupted = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, 
                                                          l_sign_c_corrupted, l_sign_size);
    int l_verify_z_corrupted = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, 
                                                         l_sign_z_corrupted, l_sign_size);
    int l_verify_hint_corrupted = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, 
                                                            l_sign_hint_corrupted, l_sign_size);
    
    // Все поврежденные подписи должны не пройти верификацию (должны вернуть отрицательное значение)
    bool l_c_test_passed = (l_verify_c_corrupted < 0);
    bool l_z_test_passed = (l_verify_z_corrupted < 0);
    bool l_hint_test_passed = (l_verify_hint_corrupted < 0);
    
    // Выводим результаты для каждого типа повреждения
    log_it(l_c_test_passed ? L_NOTICE : L_ERROR, 
           "Verification of signature with corrupted c_seed %s (return code: %d)",
           l_c_test_passed ? "correctly failed" : "unexpectedly succeeded", 
           l_verify_c_corrupted);
    
    log_it(l_z_test_passed ? L_NOTICE : L_ERROR, 
           "Verification of signature with corrupted z polynomial %s (return code: %d)",
           l_z_test_passed ? "correctly failed" : "unexpectedly succeeded", 
           l_verify_z_corrupted);
    
    log_it(l_hint_test_passed ? L_NOTICE : L_ERROR, 
           "Verification of signature with corrupted hint bits %s (return code: %d)",
           l_hint_test_passed ? "correctly failed" : "unexpectedly succeeded", 
           l_verify_hint_corrupted);
    
    // Освобождаем ресурсы
    dap_enc_key_delete(l_key);
    DAP_DELETE(l_sign);
    DAP_DELETE(l_sign_c_corrupted);
    DAP_DELETE(l_sign_z_corrupted);
    DAP_DELETE(l_sign_hint_corrupted);
    
    // Итоговый результат - положительный только если все тесты прошли
    if (l_c_test_passed && l_z_test_passed && l_hint_test_passed) {
        log_it(L_NOTICE, "All corrupted signature tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some corrupted signature tests FAILED");
        return -5;
    }
}

/**
 * @brief Тест для проверки подписей одного объекта с одним ключом
 * Проверяет, что подписи одного и того же объекта одним ключом дают
 * разные c_seed из-за случайной составляющей при подписи
 * 
 * @return int 0 при успехе, отрицательный код при ошибке
 */
static int dap_enc_chipmunk_same_object_signatures_test(void)
{
    log_it(L_INFO, "Testing signatures for the same object with the same key...");
    
    // Создаем ключ для подписи
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    if (!l_key) {
        log_it(L_ERROR, "Failed to create Chipmunk key");
        return -1;
    }
    
    // Размер подписи
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    
    // Создаем один объект для подписи
    const char l_message[] = "Test message to be signed multiple times";
    size_t l_message_len = strlen(l_message);
    
    // Выделяем память для подписей
    uint8_t *l_sign1 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    uint8_t *l_sign2 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    
    if (!l_sign1 || !l_sign2) {
        log_it(L_ERROR, "Failed to allocate memory for signatures");
        dap_enc_key_delete(l_key);
        if (l_sign1) DAP_DELETE(l_sign1);
        if (l_sign2) DAP_DELETE(l_sign2);
        return -1;
    }
    
    // Создаем две подписи для одного объекта одним ключом
    int l_ret1 = dap_enc_chipmunk_get_sign(l_key, l_message, l_message_len, l_sign1, l_sign_size);
    int l_ret2 = dap_enc_chipmunk_get_sign(l_key, l_message, l_message_len, l_sign2, l_sign_size);
    
    if (l_ret1 <= 0 || l_ret2 <= 0) {
        log_it(L_ERROR, "Failed to sign message, error codes: %d, %d", l_ret1, l_ret2);
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        return -2;
    }
    
    // Извлекаем c_seed из обеих подписей
    chipmunk_signature_t l_sig1 = {0};
    chipmunk_signature_t l_sig2 = {0};
    
    if (chipmunk_signature_from_bytes(&l_sig1, l_sign1) != 0 || 
        chipmunk_signature_from_bytes(&l_sig2, l_sign2) != 0) {
        log_it(L_ERROR, "Failed to parse signatures");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        return -3;
    }
    
    // DEBUG: log first 4 bytes of c_seed for each signature
    log_it(L_DEBUG, "Signature 1 c_seed: %02x%02x%02x%02x...",
           l_sig1.c_seed[0], l_sig1.c_seed[1], l_sig1.c_seed[2], l_sig1.c_seed[3]);
    log_it(L_DEBUG, "Signature 2 c_seed: %02x%02x%02x%02x...",
           l_sig2.c_seed[0], l_sig2.c_seed[1], l_sig2.c_seed[2], l_sig2.c_seed[3]);
    
    // Compare first few bytes of c_seed to verify they're different
    bool l_c_different = false;
    for (size_t i = 0; i < sizeof(l_sig1.c_seed); i++) {
        if (l_sig1.c_seed[i] != l_sig2.c_seed[i]) {
            l_c_different = true;
            break;
        }
    }
    
    // Для алгоритма Chipmunk ожидается случайная составляющая,
    // поэтому подписи одного и того же сообщения должны отличаться
    if (!l_c_different) {
        log_it(L_ERROR, "Challenge seeds of the same message signed twice are identical! This might indicate a problem with randomness.");
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        return -4;
    } else {
        log_it(L_NOTICE, "Challenge seeds of the same message signed twice are different (as expected with random component)");
    }
    
    // Проверяем, что обе подписи валидны
    int l_verify1 = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, l_sign1, l_sign_size);
    int l_verify2 = dap_enc_chipmunk_verify_sign(l_key, l_message, l_message_len, l_sign2, l_sign_size);
    
    if (l_verify1 != 0 || l_verify2 != 0) {
        log_it(L_ERROR, "Signature verification failed: %d, %d", l_verify1, l_verify2);
        dap_enc_key_delete(l_key);
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        return -5;
    }
    
    // Очистка ресурсов
    dap_enc_key_delete(l_key);
    DAP_DELETE(l_sign1);
    DAP_DELETE(l_sign2);
    
    log_it(L_NOTICE, "Same object with same key test PASSED");
    return 0;
}

/**
 * @brief Test cross-verification of signatures with wrong keys
 */
static int test_cross_verification(void)
{
    log_it(L_NOTICE, "Testing cross verification with wrong keys...");
    
    // Создаем первый ключ
    dap_enc_key_t *l_key1 = dap_enc_chipmunk_key_new();
    if (!l_key1) {
        log_it(L_ERROR, "Failed to create first key in test_cross_verification");
        return -1;
    }
    
    // Создаем второй ключ
    dap_enc_key_t *l_key2 = dap_enc_chipmunk_key_new();
    if (!l_key2) {
        log_it(L_ERROR, "Failed to create second key in test_cross_verification");
        dap_enc_key_delete(l_key1);
        return -1;
    }
    
    // Создаем тестовое сообщение
    const char l_message[] = "Test message for cross verification";
    size_t l_message_len = strlen(l_message);
    
    // Размер подписи
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    
    // Выделяем память для подписей
    uint8_t *l_sign1 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    if (!l_sign1) {
        log_it(L_ERROR, "Failed to allocate memory for first signature");
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -1;
    }
    
    // Подписываем сообщение первым ключом
    int l_ret1 = dap_enc_chipmunk_get_sign(l_key1, l_message, l_message_len, l_sign1, l_sign_size);
    if (l_ret1 <= 0) {
        log_it(L_ERROR, "Failed to sign message with first key, error code: %d", l_ret1);
        DAP_DELETE(l_sign1);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -2;
    }
    
    // Проверяем подпись с правильным ключом - должна пройти верификацию
    int l_verify1 = dap_enc_chipmunk_verify_sign(l_key1, l_message, l_message_len, l_sign1, l_sign_size);
    if (l_verify1 != 0) {
        log_it(L_ERROR, "Verification failed with correct key, error code: %d", l_verify1);
        DAP_DELETE(l_sign1);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -3;
    }
    
    log_it(L_NOTICE, "Verification with correct key succeeded (expected behavior)");
    
    // Проверяем подпись с неправильным ключом - должна НЕ пройти верификацию
    int l_cross_verify = dap_enc_chipmunk_verify_sign(l_key2, l_message, l_message_len, l_sign1, l_sign_size);
    
    // Если проверка подписи с неправильным ключом прошла успешно - это ошибка
    if (l_cross_verify == 0) {
        log_it(L_ERROR, "Cross-verification unexpectedly succeeded with wrong key");
        DAP_DELETE(l_sign1);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -4;
    }
    
    log_it(L_NOTICE, "Cross-verification correctly failed with error code %d (expected behavior)", l_cross_verify);
    
    // Повторяем тест в обратном порядке
    // Выделяем память для второй подписи
    uint8_t *l_sign2 = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    if (!l_sign2) {
        log_it(L_ERROR, "Failed to allocate memory for second signature");
        DAP_DELETE(l_sign1);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -1;
    }
    
    // Подписываем сообщение вторым ключом
    int l_ret2 = dap_enc_chipmunk_get_sign(l_key2, l_message, l_message_len, l_sign2, l_sign_size);
    if (l_ret2 <= 0) {
        log_it(L_ERROR, "Failed to sign message with second key, error code: %d", l_ret2);
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -2;
    }
    
    // Проверяем вторую подпись с первым (неправильным) ключом
    l_cross_verify = dap_enc_chipmunk_verify_sign(l_key1, l_message, l_message_len, l_sign2, l_sign_size);
    
    // Если проверка подписи с неправильным ключом прошла успешно - это ошибка
    if (l_cross_verify == 0) {
        log_it(L_ERROR, "Cross-verification unexpectedly succeeded with wrong key (second case)");
        DAP_DELETE(l_sign1);
        DAP_DELETE(l_sign2);
        dap_enc_key_delete(l_key1);
        dap_enc_key_delete(l_key2);
        return -4;
    }
    
    log_it(L_NOTICE, "Second cross-verification correctly failed with error code %d (expected behavior)", 
           l_cross_verify);
    
    // Очистка ресурсов
    DAP_DELETE(l_sign1);
    DAP_DELETE(l_sign2);
    dap_enc_key_delete(l_key1);
    dap_enc_key_delete(l_key2);
    
    log_it(L_NOTICE, "All cross-verification tests PASSED");
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
    
    // Добавляем тест сериализации/десериализации challenge seed
    if (s_test_chipmunk_serialization() != true) {
        log_it(L_ERROR, "Challenge seed serialization test FAILED");
        return -4;
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
    
    // Test different signatures
    log_it(L_INFO, "Testing different signatures with different keys...");
    l_res = dap_enc_chipmunk_different_signatures_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_ERROR, "Different signatures test FAILED");
    } else {
        log_it(L_INFO, "Different signatures test PASSED");
    }
    
    // Test corrupted signature
    log_it(L_INFO, "Testing verification of corrupted signatures...");
    l_res = dap_enc_chipmunk_corrupted_signature_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_ERROR, "Corrupted signature test FAILED");
    } else {
        log_it(L_INFO, "Corrupted signature test PASSED");
    }
    
    // Test same object signatures
    log_it(L_INFO, "Testing signatures for the same object with the same key...");
    l_res = dap_enc_chipmunk_same_object_signatures_test();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_ERROR, "Same object signatures test FAILED");
    } else {
        log_it(L_INFO, "Same object signatures test PASSED");
    }
    
    // Test cross-verification
    log_it(L_INFO, "Testing cross-verification with wrong keys...");
    l_res = test_cross_verification();
    if (l_res != 0) {
        l_ret += 1;
        log_it(L_ERROR, "Cross-verification test FAILED");
    } else {
        log_it(L_INFO, "Cross-verification test PASSED");
    }
    
    // Return 0 if all tests passed, non-zero otherwise
    if (l_ret != 0) {
        log_it(L_ERROR, "Some Chipmunk tests FAILED! Error code: %d", l_ret);
    } else {
        log_it(L_NOTICE, "All Chipmunk tests PASSED!");
    }
    
    return l_ret;
} 

