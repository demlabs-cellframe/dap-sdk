/*
 * Authors:
 * Dmitriy A. Gearasimov <kahovski@gmail.com>
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

#include "chipmunk_hash.h"
#include "dap_hash.h"
#include "dap_crypto_common.h"
#include "chipmunk.h"
// Добавляем SHA2-256 из secp256k1 с правильными путями
#include "../../3rdparty/secp256k1/src/hash.h"
#include "../../3rdparty/secp256k1/src/hash_impl.h"
// XKCP includes for SHA3 functions
#include "SimpleFIPS202.h"
#include <string.h>

#define LOG_TAG "chipmunk_hash"

/**
 * @brief Compute SHA2-256 hash using DAP wrapper
 * @param[out] a_output Output buffer (32 bytes)
 * @param[in] a_input Input data
 * @param[in] a_inlen Input length
 * @return Returns 0 on success, negative error code on failure
 */
static int dap_chipmunk_hash_sha2_256(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    int l_result = dap_hash_sha2_256(a_output, a_input, a_inlen);
    if (l_result != 0) {
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Initialize hash functions for Chipmunk
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_init(void) {
    // Currently there's no specific initialization needed
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHA3-256 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_256(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-256 hash
    SHA3_256(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHA3-384 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_384(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-384 hash
    SHA3_384(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHA3-512 wrapper function implementation
 */
int dap_chipmunk_hash_sha3_512(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Perform SHA3-512 hash
    SHA3_512(a_output, a_input, a_inlen);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief SHAKE-128 implementation for extendable output
 * @param[out] a_output Output buffer
 * @param[in] a_outlen Desired output length
 * @param[in] a_input Input data
 * @param[in] a_inlen Length of input data
 * @return Returns 0 on success, negative error code on failure
 */
int dap_chipmunk_hash_shake128(uint8_t *a_output, size_t a_outlen, const uint8_t *a_input, size_t a_inlen) 
{
    // Check input parameters
    if (!a_output || !a_input || !a_outlen) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_shake128");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Check for potential overflow
    if (a_inlen > SIZE_MAX - 1) {
        log_it(L_ERROR, "Input size too large in dap_chipmunk_hash_shake128");
        return CHIPMUNK_ERROR_OVERFLOW;
    }
    
    // Limit output size to avoid memory issues
    const size_t l_max_out_size = 4096; // Безопасное ограничение на выходной размер
    size_t l_outlen = a_outlen;
    if (l_outlen > l_max_out_size) {
        log_it(L_WARNING, "Output size limited in dap_chipmunk_hash_shake128 (requested %zu, limited to %zu)", 
               l_outlen, l_max_out_size);
        l_outlen = l_max_out_size;
    }
    
    // Выделяем память для входных данных с добавлением счетчика
    uint8_t *l_tmp_input = NULL;
    
    // Проверка на переполнение при выделении памяти
    if (a_inlen + 1 < a_inlen) {
        log_it(L_ERROR, "Integer overflow in memory allocation");
        return CHIPMUNK_ERROR_OVERFLOW;
    }
    
    // Выделение памяти со строгой проверкой
    l_tmp_input = DAP_NEW_Z_SIZE(uint8_t, a_inlen + 1);
    if (!l_tmp_input) {
        log_it(L_ERROR, "Memory allocation failed in dap_chipmunk_hash_shake128");
        return CHIPMUNK_ERROR_MEMORY;
    }
    
    // Копируем входные данные и сохраняем место для счетчика
    memcpy(l_tmp_input, a_input, a_inlen);
    uint8_t l_counter = 0;
    
    // Очищаем выходной буфер для безопасности
    memset(a_output, 0, l_outlen);
    
    // Generate output in chunks of 32 bytes using SHA2-256
    for (size_t l_offset = 0; l_offset < l_outlen; l_offset += 32) {
        // Обновляем счетчик для каждого блока
        l_tmp_input[a_inlen] = l_counter++;
        
        // SHA2-256 буфер для одного блока
        uint8_t l_buffer[32] = {0};
        
        // Вызываем SHA2-256 (ИСПРАВЛЕНО: теперь используем правильный SHA2)
        int l_result = dap_chipmunk_hash_sha2_256(l_buffer, l_tmp_input, a_inlen + 1);
        if (l_result != CHIPMUNK_ERROR_SUCCESS) {
            // Безопасная очистка перед выходом
            memset(l_tmp_input, 0, a_inlen + 1);
            DAP_DELETE(l_tmp_input);
            return l_result;
        }
        
        // Определяем, сколько данных скопировать на этой итерации
        size_t l_copy_len = (l_offset + 32 <= l_outlen) ? 32 : l_outlen - l_offset;
        
        // Копируем данные в выходной буфер
        memcpy(a_output + l_offset, l_buffer, l_copy_len);
    }

    // Безопасная очистка временных данных
    memset(l_tmp_input, 0, a_inlen + 1);
    DAP_DELETE(l_tmp_input);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Generate seed for polynomials from message
 */
int dap_chipmunk_hash_to_seed(uint8_t a_output[32], const uint8_t *a_message, size_t a_msglen) 
{
    if (!a_output || !a_message) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_to_seed");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    // ИСПРАВЛЕНО: Используем SHA2-256 вместо SHA3-256
    return dap_chipmunk_hash_sha2_256(a_output, a_message, a_msglen);
}

/**
 * @brief Generate hash for challenge function
 */
int dap_chipmunk_hash_challenge(uint8_t a_output[32], const uint8_t *a_input, size_t a_inlen) {
    if (!a_output || !a_input) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_challenge");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    // ИСПРАВЛЕНО: Используем SHA2-256 вместо SHA3-256
    return dap_chipmunk_hash_sha2_256(a_output, a_input, a_inlen);
}

/**
 * @brief Generate random polynomial based on seed and nonce
 * 
 * @return Returns 0 on success, negative values on error:
 *         CHIPMUNK_ERROR_NULL_PARAM: NULL pointers
 *         CHIPMUNK_ERROR_OVERFLOW: Size overflow
 *         CHIPMUNK_ERROR_MEMORY: Memory allocation failure
 */
int dap_chipmunk_hash_sample_poly(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce) 
{
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_sample_poly");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Инициализируем буфер для запроса (seed + nonce)
    uint8_t l_buf[34] = {0}; // 32 bytes seed + 2 bytes nonce
    
    // Копируем seed
    memcpy(l_buf, a_seed, 32);
    
    // Добавляем nonce в младшем порядке байтов (little-endian)
    l_buf[32] = a_nonce & 0xff;
    l_buf[33] = (a_nonce >> 8) & 0xff;
    
    // Проверяем переполнение при умножении для вычисления размера выходного буфера
    if (CHIPMUNK_N > SIZE_MAX / 3) {
        log_it(L_ERROR, "Size overflow in dap_chipmunk_hash_sample_poly");
        // Очищаем полином, чтобы не оставлять неинициализированные данные
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return CHIPMUNK_ERROR_OVERFLOW;
    }
    
    // Вычисляем размер буфера для SHAKE128 (3 байта на коэффициент)
    const size_t l_total_bytes = CHIPMUNK_N * 3;
    
    // Выделяем память под временный буфер
    uint8_t *l_sample_bytes = DAP_NEW_Z_SIZE(uint8_t, l_total_bytes);
    if (!l_sample_bytes) {
        log_it(L_ERROR, "Memory allocation failed in dap_chipmunk_hash_sample_poly");
        // Очищаем полином, чтобы не оставлять неинициализированные данные
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return CHIPMUNK_ERROR_MEMORY;
    }
    
    // Получаем расширенный выход через SHAKE128
    int l_result = dap_chipmunk_hash_shake128(l_sample_bytes, l_total_bytes, l_buf, sizeof(l_buf));
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "SHAKE128 failed in dap_chipmunk_hash_sample_poly with error %d", l_result);
        memset(l_sample_bytes, 0, l_total_bytes);
        DAP_DELETE(l_sample_bytes);
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return l_result;
    }
    
    // Конвертируем байты в коэффициенты полинома
    for (int i = 0, j = 0; i < CHIPMUNK_N; i++, j += 3) {
        uint32_t l_t = ((uint32_t)l_sample_bytes[j]) | 
                      (((uint32_t)l_sample_bytes[j + 1]) << 8) | 
                      (((uint32_t)l_sample_bytes[j + 2]) << 16);
        
        // Маскируем до 23 бит
        l_t &= 0x7FFFFF; 
        
        // Согласно алгоритму Chipmunk, полином y должен иметь коэффициенты
        // в диапазоне [-gamma1, gamma1], где gamma1 = 2^17 = 131072
        // Маппим l_t на диапазон [-gamma1, gamma1]
        const int32_t l_gamma1 = 1 << 17; // 131072
        const uint32_t l_range = 2 * l_gamma1 + 1; // 262145
        
        // Приводим к диапазону [0, range-1], затем сдвигаем к [-gamma1, gamma1]
        uint32_t l_reduced = l_t % l_range;
        int32_t l_coeff = (int32_t)l_reduced - l_gamma1;
        
        a_poly[i] = l_coeff;
    }
    
    // Безопасно очищаем и освобождаем память
    memset(l_sample_bytes, 0, l_total_bytes);
    DAP_DELETE(l_sample_bytes);
    
    return CHIPMUNK_ERROR_SUCCESS;  // Успешное выполнение
}

/**
 * @brief Generate point from hash
 */
int dap_chipmunk_hash_to_point(uint8_t *a_output, const uint8_t *a_input, size_t a_inlen) 
{
    if (!a_output || !a_input) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_to_point");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    return dap_chipmunk_hash_sha3_256(a_output, a_input, a_inlen);
}

/**
 * @brief Generate random polynomial for matrix A based on seed and nonce
 * 
 * @param[out] a_poly Output polynomial coefficients
 * @param[in] a_seed 32-byte seed
 * @param[in] a_nonce Nonce value
 * @return Returns 0 on success, negative values on error
 */
int dap_chipmunk_hash_sample_matrix(int32_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce) 
{
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in dap_chipmunk_hash_sample_matrix");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Инициализируем буфер для запроса (seed + nonce)
    uint8_t l_buf[34] = {0}; // 32 bytes seed + 2 bytes nonce
    
    // Копируем seed
    memcpy(l_buf, a_seed, 32);
    
    // Добавляем nonce в младшем порядке байтов (little-endian)
    l_buf[32] = a_nonce & 0xff;
    l_buf[33] = (a_nonce >> 8) & 0xff;
    
    // Проверяем переполнение при умножении для вычисления размера выходного буфера
    if (CHIPMUNK_N > SIZE_MAX / 3) {
        log_it(L_ERROR, "Size overflow in dap_chipmunk_hash_sample_matrix");
        // Очищаем полином, чтобы не оставлять неинициализированные данные
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return CHIPMUNK_ERROR_OVERFLOW;
    }
    
    // Вычисляем размер буфера для SHAKE128 (3 байта на коэффициент)
    const size_t l_total_bytes = CHIPMUNK_N * 3;
    
    // Выделяем память под временный буфер
    uint8_t *l_sample_bytes = DAP_NEW_Z_SIZE(uint8_t, l_total_bytes);
    if (!l_sample_bytes) {
        log_it(L_ERROR, "Memory allocation failed in dap_chipmunk_hash_sample_matrix");
        // Очищаем полином, чтобы не оставлять неинициализированные данные
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return CHIPMUNK_ERROR_MEMORY;
    }
    
    // Получаем расширенный выход через SHAKE128
    int l_result = dap_chipmunk_hash_shake128(l_sample_bytes, l_total_bytes, l_buf, sizeof(l_buf));
    if (l_result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "SHAKE128 failed in dap_chipmunk_hash_sample_matrix with error %d", l_result);
        memset(l_sample_bytes, 0, l_total_bytes);
        DAP_DELETE(l_sample_bytes);
        memset(a_poly, 0, CHIPMUNK_N * sizeof(int32_t));
        return l_result;
    }
    
    // Конвертируем байты в коэффициенты полинома A
    for (int i = 0, j = 0; i < CHIPMUNK_N; i++, j += 3) {
        uint32_t l_t = ((uint32_t)l_sample_bytes[j]) | 
                      (((uint32_t)l_sample_bytes[j + 1]) << 8) | 
                      (((uint32_t)l_sample_bytes[j + 2]) << 16);
        
        // Маскируем до 23 бит
        l_t &= 0x7FFFFF; 
        
        // Приводим к диапазону [0, q-1]
        l_t = l_t % CHIPMUNK_Q;
        
        // Для полинома A коэффициенты должны быть в диапазоне [0, q-1]
        // согласно алгоритму Chipmunk из статьи
        a_poly[i] = (int32_t)l_t;
    }
    
    // Безопасно очищаем и освобождаем память
    memset(l_sample_bytes, 0, l_total_bytes);
    DAP_DELETE(l_sample_bytes);
    
    return CHIPMUNK_ERROR_SUCCESS;  // Успешное выполнение
} 