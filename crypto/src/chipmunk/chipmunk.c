/*
 * Authors:
 * Dmitriy A. Gearasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_enc_base64.h"
#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "chipmunk_hots.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "rand/dap_rand.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <stddef.h>  // для offsetof

#define LOG_TAG "chipmunk"

// Определение MIN для использования в функциях работы с массивами
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

#define CHIPMUNK_ETA 2  // Error distribution parameter η

// Флаг для расширенного логирования
static bool s_debug_more = true;

static volatile int g_initialized = 0;

// Forward declarations
static void secure_clean(volatile void* data, size_t size);

/**
 * @brief Initialize Chipmunk module
 * @return Returns 0 on success
 */
int chipmunk_init(void) {
    if (g_initialized) {
        return CHIPMUNK_ERROR_SUCCESS;
    }

    // Инициализация криптографических примитивов
    if (dap_chipmunk_hash_init() != 0) {
        log_it(L_ERROR, "Failed to initialize chipmunk hash functions");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }

    g_initialized = 1;
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Generate a Chipmunk keypair
 * 
 * @param[out] a_public_key Public key buffer
 * @param[in] a_public_key_size Public key buffer size
 * @param[out] a_private_key Private key buffer
 * @param[in] a_private_key_size Private key buffer size
 * @return int CHIPMUNK_ERROR_SUCCESS if successful, error code otherwise
 */
int chipmunk_keypair(uint8_t *a_public_key, size_t a_public_key_size,
                    uint8_t *a_private_key, size_t a_private_key_size) {
    debug_if(s_debug_more, L_DEBUG, "chipmunk_keypair: Starting HOTS key generation");
    
    // ДОБАВЛЯЕМ ДИАГНОСТИКУ РАЗМЕРОВ СТРУКТУР
    printf("=== CHIPMUNK STRUCTURE SIZES ===\n");
    printf("sizeof(chipmunk_poly_t) = %zu (expected %d)\n", 
           sizeof(chipmunk_poly_t), CHIPMUNK_N * 4);
    printf("sizeof(chipmunk_public_key_t) = %zu (expected %d)\n", 
           sizeof(chipmunk_public_key_t), CHIPMUNK_PUBLIC_KEY_SIZE);
    printf("sizeof(chipmunk_private_key_t) = %zu (expected %d)\n", 
           sizeof(chipmunk_private_key_t), CHIPMUNK_PRIVATE_KEY_SIZE);
    printf("offsetof(chipmunk_private_key_t, tr) = %zu\n", 
           offsetof(chipmunk_private_key_t, tr));
    printf("offsetof(chipmunk_private_key_t, pk) = %zu\n", 
           offsetof(chipmunk_private_key_t, pk));
    printf("=================================\n");
    fflush(stdout);
    
    log_it(L_DEBUG, "=== STRUCTURE SIZE DIAGNOSTICS ===");
    
    // Проверка параметров
    if (!a_public_key || !a_private_key) {
        log_it(L_ERROR, "NULL key buffers in chipmunk_keypair");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    if (a_public_key_size != CHIPMUNK_PUBLIC_KEY_SIZE ||
        a_private_key_size != CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Invalid key buffer sizes in chipmunk_keypair: pub %zu (expected %d), priv %zu (expected %d)",
               a_public_key_size, CHIPMUNK_PUBLIC_KEY_SIZE,
               a_private_key_size, CHIPMUNK_PRIVATE_KEY_SIZE);
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    
    // Генерируем основной seed для ключей
    uint8_t l_key_seed[32];
    if (randombytes(l_key_seed, 32) != 0) {
        log_it(L_ERROR, "Failed to generate random key seed");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    
    // Генерируем rho для публичных параметров
    uint8_t l_rho_seed[32];
    if (randombytes(l_rho_seed, 32) != 0) {
        log_it(L_ERROR, "Failed to generate rho seed");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    
    // Генерируем HOTS параметры из rho_seed (как при подписи/верификации!)
    chipmunk_hots_params_t l_hots_params = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params.a[i].coeffs, l_rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate polynomial A[%d]", i);
            return CHIPMUNK_ERROR_HASH_FAILED;
        }
        // Преобразуем в NTT домен
        chipmunk_poly_ntt(&l_hots_params.a[i]);
    }
    
    // Генерируем полноценные HOTS ключи
    chipmunk_hots_pk_t l_hots_pk = {0};
    chipmunk_hots_sk_t l_hots_sk = {0};
    
    if (chipmunk_hots_keygen(l_key_seed, 0, &l_hots_params, &l_hots_pk, &l_hots_sk) != 0) {
        log_it(L_ERROR, "Failed to generate HOTS keys");
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Создаем структуры для новых ключей
    chipmunk_private_key_t l_sk = {0};
    chipmunk_public_key_t l_pk = {0};
    
    // Копируем key_seed в приватный ключ
    memcpy(l_sk.key_seed, l_key_seed, 32);
    
    // Копируем rho_seed в публичный ключ
    memcpy(l_pk.rho_seed, l_rho_seed, 32);
    
    // Копируем v0 и v1 из HOTS ключей
    memcpy(&l_pk.v0, &l_hots_pk.v0, sizeof(chipmunk_poly_t));
    memcpy(&l_pk.v1, &l_hots_pk.v1, sizeof(chipmunk_poly_t));
    
    // Сохраняем публичный ключ в приватном ключе
    memcpy(&l_sk.pk, &l_pk, sizeof(l_pk));
    
    // Вычисляем хеш публичного ключа для коммитмента
    uint8_t l_pk_bytes[CHIPMUNK_PUBLIC_KEY_SIZE];
    int result = chipmunk_public_key_to_bytes(l_pk_bytes, &l_pk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        return result;
    }
    
    // Используем SHA3-384 для tr (48 байт)
    result = dap_chipmunk_hash_sha3_384(l_sk.tr, l_pk_bytes, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to compute public key hash");
        return result;
    }
    
    // Сериализуем ключи для вывода
    result = chipmunk_private_key_to_bytes(a_private_key, &l_sk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize private key");
        return result;
    }
    
    result = chipmunk_public_key_to_bytes(a_public_key, &l_pk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        return result;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Successfully generated Chipmunk HOTS keypair");
    
    // Очищаем секретные данные
    secure_clean(&l_hots_sk, sizeof(l_hots_sk));
    secure_clean(&l_sk, sizeof(l_sk));
    secure_clean(l_key_seed, sizeof(l_key_seed));
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Sign a message
 * 
 * @param[in] a_private_key Private key buffer
 * @param[in] a_message Message to sign
 * @param[in] a_message_len Message length
 * @param[out] a_signature Output signature buffer
 * @return Returns 0 on success, negative on error
 */
int chipmunk_sign(const uint8_t *a_private_key, const uint8_t *a_message, 
                  size_t a_message_len, uint8_t *a_signature) {
    log_it(L_INFO, "Starting Chipmunk HOTS signature generation");
    
    if (!a_private_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_sign");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Проверка на максимальный размер сообщения
    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for signing");
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    
    // Парсим приватный ключ
    chipmunk_private_key_t l_sk = {0};
    if (chipmunk_private_key_from_bytes(&l_sk, a_private_key) != 0) {
        log_it(L_ERROR, "Failed to parse private key");
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    // Генерируем HOTS параметры из rho_seed
    chipmunk_hots_params_t l_hots_params = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params.a[i].coeffs, l_sk.pk.rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate polynomial A[%d]", i);
        secure_clean(&l_sk, sizeof(l_sk));
            return CHIPMUNK_ERROR_HASH_FAILED;
        }
        // Преобразуем в NTT домен
        chipmunk_poly_ntt(&l_hots_params.a[i]);
    }
    
    // Восстанавливаем HOTS секретный ключ из key_seed
    chipmunk_hots_sk_t l_hots_sk = {0};
    
    // Используем тот же метод что и в chipmunk_hots_keygen!
    // Initialize the RNG with seed and counter (как в Rust)
    uint8_t l_derived_seed[32];
    uint8_t l_counter_bytes[4];
    uint32_t a_counter = 0; // такой же counter как при генерации ключей
    l_counter_bytes[0] = (a_counter >> 24) & 0xFF;
    l_counter_bytes[1] = (a_counter >> 16) & 0xFF;
    l_counter_bytes[2] = (a_counter >> 8) & 0xFF;
    l_counter_bytes[3] = a_counter & 0xFF;
    
    // Concatenate seed and counter
    uint8_t l_seed_and_counter[36];
    memcpy(l_seed_and_counter, l_sk.key_seed, 32);
    memcpy(l_seed_and_counter + 32, l_counter_bytes, 4);
    
    // Hash to get derived seed (точно как в chipmunk_hots_keygen)
    dap_hash_fast_t l_hash_out;
    dap_hash_fast(l_seed_and_counter, 36, &l_hash_out);
    memcpy(l_derived_seed, &l_hash_out, 32);
    
    // Генерируем секретные ключи точно как в chipmunk_hots_keygen
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        // Generate s0[i] in time domain, then convert to NTT
        uint8_t l_s0_seed[36];
        memcpy(l_s0_seed, l_derived_seed, 32);
        uint32_t l_s0_nonce = a_counter + i;
        memcpy(l_s0_seed + 32, &l_s0_nonce, 4);
        
        chipmunk_poly_uniform_mod_p(&l_hots_sk.s0[i], l_s0_seed, CHIPMUNK_PHI);
        // Преобразуем s0[i] в NTT домен для хранения
        chipmunk_ntt(l_hots_sk.s0[i].coeffs);
        
        // Generate s1[i] in time domain, then convert to NTT  
        uint8_t l_s1_seed[36];
        memcpy(l_s1_seed, l_derived_seed, 32);
        uint32_t l_s1_nonce = a_counter + CHIPMUNK_GAMMA + i;
        memcpy(l_s1_seed + 32, &l_s1_nonce, 4);
        
        chipmunk_poly_uniform_mod_p(&l_hots_sk.s1[i], l_s1_seed, CHIPMUNK_PHI_ALPHA_H);
        // Преобразуем s1[i] в NTT домен для хранения
        chipmunk_ntt(l_hots_sk.s1[i].coeffs);
    }
    
    // Генерируем HOTS подпись
    chipmunk_hots_signature_t l_hots_sig = {0};
    int l_result = chipmunk_hots_sign(&l_hots_sk, a_message, a_message_len, &l_hots_sig);
    
    if (l_result != 0) {
        log_it(L_ERROR, "HOTS signature failed with error %d", l_result);
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_hots_sk, sizeof(l_hots_sk));
        return l_result;
    }
    
    // Конвертируем HOTS подпись в формат Chipmunk
    chipmunk_signature_t l_sig = {0};
    
    // Копируем все GAMMA полиномов sigma
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        memcpy(&l_sig.sigma[i], &l_hots_sig.sigma[i], sizeof(chipmunk_poly_t));
    }
    
    // Сериализуем подпись
    if (chipmunk_signature_to_bytes(a_signature, &l_sig) != 0) {
        log_it(L_ERROR, "Failed to serialize signature");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_hots_sk, sizeof(l_hots_sk));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Очищаем секретные данные
    secure_clean(&l_sk, sizeof(l_sk));
    secure_clean(&l_hots_sk, sizeof(l_hots_sk));
    secure_clean(&l_hots_sig, sizeof(l_hots_sig));
    
    log_it(L_INFO, "HOTS signature successfully generated");
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Verify a signature
 * 
 * @param[in] a_public_key Public key buffer
 * @param[in] a_message Message that was signed
 * @param[in] a_message_len Message length
 * @param[in] a_signature Signature to verify
 * @return Returns 0 if signature is valid, negative on error
 */
int chipmunk_verify(const uint8_t *a_public_key, const uint8_t *a_message, 
                    size_t a_message_len, const uint8_t *a_signature) {
    log_it(L_INFO, "Starting HOTS signature verification");
    
    if (!a_public_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_verify");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Проверка на максимальный размер сообщения
    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for verification");
        return CHIPMUNK_ERROR_INVALID_SIZE;
    }
    
    // Парсим публичный ключ
    chipmunk_public_key_t l_pk = {0};
    if (chipmunk_public_key_from_bytes(&l_pk, a_public_key) != 0) {
        log_it(L_ERROR, "Failed to parse public key");
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    // Парсим подпись
    chipmunk_signature_t l_sig = {0};
    if (chipmunk_signature_from_bytes(&l_sig, a_signature) != 0) {
        log_it(L_ERROR, "Failed to parse signature");
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    // Генерируем HOTS параметры из rho_seed
    chipmunk_hots_params_t l_hots_params = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        if (dap_chipmunk_hash_sample_matrix(l_hots_params.a[i].coeffs, l_pk.rho_seed, i) != 0) {
            log_it(L_ERROR, "Failed to generate polynomial A[%d]", i);
            return CHIPMUNK_ERROR_HASH_FAILED;
        }
        // Преобразуем в NTT домен
        chipmunk_poly_ntt(&l_hots_params.a[i]);
    }
    
    // Создаем HOTS публичный ключ
    chipmunk_hots_pk_t l_hots_pk = {0};
    memcpy(&l_hots_pk.v0, &l_pk.v0, sizeof(chipmunk_poly_t));
    memcpy(&l_hots_pk.v1, &l_pk.v1, sizeof(chipmunk_poly_t));
    
    // Создаем HOTS подпись
    chipmunk_hots_signature_t l_hots_sig = {0};
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        memcpy(&l_hots_sig.sigma[i], &l_sig.sigma[i], sizeof(chipmunk_poly_t));
    }
    
    // Используем HOTS функцию верификации
    int l_result = chipmunk_hots_verify(&l_hots_pk, a_message, a_message_len, 
                                        &l_hots_sig, &l_hots_params);
    
    if (l_result != 0) {  // Стандартное C соглашение: 0 для успеха, отрицательное для ошибки
        log_it(L_ERROR, "HOTS signature verification failed: %d", l_result);
        return CHIPMUNK_ERROR_VERIFY_FAILED;
    }
    
    log_it(L_INFO, "HOTS signature verified successfully");
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Serialize public key to bytes
 */
int chipmunk_public_key_to_bytes(uint8_t *a_output, const chipmunk_public_key_t *a_key) {
    if (!a_output || !a_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_public_key_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    size_t l_expected_size = 32 + (CHIPMUNK_N * 4 * 2); // rho_seed + v0 + v1
    
    printf("=== chipmunk_public_key_to_bytes DEBUG ===\n");
    printf("Expected size: %zu (should be %d)\n", l_expected_size, CHIPMUNK_PUBLIC_KEY_SIZE);
    printf("Output buffer: %p\n", a_output);
    printf("CHIPMUNK_N = %d\n", CHIPMUNK_N);
    
    // Write rho_seed (32 bytes)
    printf("Writing rho_seed at offset %zu\n", l_offset);
    memcpy(a_output + l_offset, a_key->rho_seed, 32);
    l_offset += 32;
    
    // Write v0 polynomial (CHIPMUNK_N * 4 bytes)
    printf("Writing v0 polynomial at offset %zu (size %d)\n", l_offset, CHIPMUNK_N * 4);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_coeff = a_key->v0.coeffs[i];
        a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
        a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
        a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
        a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
        l_offset += 4;
    }
    
    // Write v1 polynomial (CHIPMUNK_N * 4 bytes)
    printf("Writing v1 polynomial at offset %zu (size %d)\n", l_offset, CHIPMUNK_N * 4);
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_coeff = a_key->v1.coeffs[i];
        a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
        a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
        a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
        a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
        l_offset += 4;
    }
    
    printf("Total bytes written: %zu\n", l_offset);
    printf("===========================================\n");
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Serialize private key to bytes
 */
int chipmunk_private_key_to_bytes(uint8_t *a_output, const chipmunk_private_key_t *a_key) {
    if (!a_output || !a_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_private_key_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    size_t l_total_size = 32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE;
    
    printf("=== chipmunk_private_key_to_bytes DEBUG ===\n");
    printf("Expected total size: %zu\n", l_total_size);
    printf("Output buffer pointer: %p\n", a_output);
    
    // Write key_seed (32 bytes)
    printf("Writing key_seed at offset %zu\n", l_offset);
    memcpy(a_output + l_offset, a_key->key_seed, 32);
    l_offset += 32;
    
    // Write tr (48 bytes)
    printf("Writing tr at offset %zu\n", l_offset);
    memcpy(a_output + l_offset, a_key->tr, 48);
    l_offset += 48;
    
    // Write public key
    printf("Writing public key at offset %zu\n", l_offset);
    printf("Calling chipmunk_public_key_to_bytes with buffer at %p\n", a_output + l_offset);
    int result = chipmunk_public_key_to_bytes(a_output + l_offset, &a_key->pk);
    
    printf("chipmunk_public_key_to_bytes returned %d\n", result);
    printf("===========================================\n");
    
    return result;
}

/**
 * @brief Serialize signature to bytes
 */
int chipmunk_signature_to_bytes(uint8_t *a_output, const chipmunk_signature_t *a_sig) {
    if (!a_output || !a_sig) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_signature_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    
    // Write all GAMMA sigma polynomials
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            int32_t l_coeff = a_sig->sigma[i].coeffs[j];
            a_output[l_offset] = (uint8_t)(l_coeff & 0xFF);
            a_output[l_offset + 1] = (uint8_t)((l_coeff >> 8) & 0xFF);
            a_output[l_offset + 2] = (uint8_t)((l_coeff >> 16) & 0xFF);
            a_output[l_offset + 3] = (uint8_t)((l_coeff >> 24) & 0xFF);
            l_offset += 4;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize public key from bytes
 */
int chipmunk_public_key_from_bytes(chipmunk_public_key_t *a_key, const uint8_t *a_input) {
    if (!a_key || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_public_key_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Read rho_seed (32 bytes)
    memcpy(a_key->rho_seed, a_input, 32);
    
    // Read v0 polynomial (CHIPMUNK_N * 4 bytes)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // ИСПРАВЛЕНО: читаем как знаковое число для корректной обработки отрицательных коэффициентов
        uint32_t l_raw = ((uint32_t)a_input[32 + i*4]) | 
                        (((uint32_t)a_input[32 + i*4 + 1]) << 8) | 
                        (((uint32_t)a_input[32 + i*4 + 2]) << 16) |
                        (((uint32_t)a_input[32 + i*4 + 3]) << 24);
        
        // Интерпретируем как знаковое число и приводим к диапазону [0, Q-1]
        int32_t l_signed = (int32_t)l_raw;
        a_key->v0.coeffs[i] = ((l_signed % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
    }
    
    // Read v1 polynomial (CHIPMUNK_N * 4 bytes)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // ИСПРАВЛЕНО: читаем как знаковое число для корректной обработки отрицательных коэффициентов
        uint32_t l_raw = ((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4]) | 
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 1]) << 8) | 
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 2]) << 16) |
                        (((uint32_t)a_input[32 + CHIPMUNK_N*4 + i*4 + 3]) << 24);
        
        // Интерпретируем как знаковое число и приводим к диапазону [0, Q-1]
        int32_t l_signed = (int32_t)l_raw;
        a_key->v1.coeffs[i] = ((l_signed % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize private key from bytes
 */
int chipmunk_private_key_from_bytes(chipmunk_private_key_t *a_key, const uint8_t *a_input) {
    if (!a_key || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_private_key_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    
    // Read key_seed (32 bytes)
    memcpy(a_key->key_seed, a_input + l_offset, 32);
    l_offset += 32;
    
    // Read tr (48 bytes)
    memcpy(a_key->tr, a_input + l_offset, 48);
    l_offset += 48;
    
    // Read public key
    int l_result = chipmunk_public_key_from_bytes(&a_key->pk, a_input + l_offset);
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to deserialize public key part in private key");
        return l_result;
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Deserialize signature from bytes
 */
int chipmunk_signature_from_bytes(chipmunk_signature_t *a_sig, const uint8_t *a_input) {
    if (!a_sig || !a_input) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_signature_from_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    size_t l_offset = 0;
    
    // Clear structure before filling
    memset(a_sig, 0, sizeof(chipmunk_signature_t));
    
    // Read all GAMMA sigma polynomials
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        for (int j = 0; j < CHIPMUNK_N; j++) {
            a_sig->sigma[i].coeffs[j] = (int32_t)(
                (uint32_t)a_input[l_offset] | 
                ((uint32_t)a_input[l_offset + 1] << 8) | 
                ((uint32_t)a_input[l_offset + 2] << 16) | 
                ((uint32_t)a_input[l_offset + 3] << 24)
            );
            l_offset += 4;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
} 

// Implementation of utility functions
static void secure_clean(volatile void* data, size_t size) {
    volatile uint8_t *p = (volatile uint8_t*)data;
    while (size--) {
        *p++ = 0;
    }
} 

// Удалена неиспользуемая функция copy_to_hash_buffer




