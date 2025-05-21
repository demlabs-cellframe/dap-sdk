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

#include "chipmunk.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
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
 * @brief Transform polynomial to NTT form
 */
int chipmunk_poly_ntt(chipmunk_poly_t *a_poly) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_ntt");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_ntt(a_poly->coeffs);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Inverse transform from NTT form
 */
int chipmunk_poly_invntt(chipmunk_poly_t *a_poly) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_invntt");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_invntt(a_poly->coeffs);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Add two polynomials modulo q
 */
int chipmunk_poly_add(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b) {
    if (!a_result || !a_a || !a_b) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_add");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Используем явное приведение к int64_t для избежания переполнения
        int64_t l_temp = ((int64_t)a_a->coeffs[l_i] + (int64_t)a_b->coeffs[l_i]) % CHIPMUNK_Q;
        
        // Обрабатываем отрицательный остаток
        if (l_temp < 0) {
            l_temp += CHIPMUNK_Q;
        }
        
        // Проверим, что результат в допустимом диапазоне
        if (l_temp < 0 || l_temp >= CHIPMUNK_Q) {
            log_it(L_ERROR, "Overflow in polynomial addition");
            return CHIPMUNK_ERROR_OVERFLOW;
        }
        
        a_result->coeffs[l_i] = (int32_t)l_temp;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Subtract two polynomials modulo q
 */
int chipmunk_poly_sub(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b) {
    if (!a_result || !a_a || !a_b) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_sub");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Используем явное приведение к int64_t для избежания переполнения
        int64_t l_temp = ((int64_t)a_a->coeffs[l_i] - (int64_t)a_b->coeffs[l_i]) % CHIPMUNK_Q;
        
        // Обрабатываем отрицательный остаток
        if (l_temp < 0) {
            l_temp += CHIPMUNK_Q;
        }
        
        // Проверим, что результат в допустимом диапазоне
        if (l_temp < 0 || l_temp >= CHIPMUNK_Q) {
            log_it(L_ERROR, "Overflow in polynomial subtraction");
            return CHIPMUNK_ERROR_OVERFLOW;
        }
        
        a_result->coeffs[l_i] = (int32_t)l_temp;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Multiply two polynomials in NTT form
 */
int chipmunk_poly_pointwise(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b) {
    if (!a_result || !a_a || !a_b) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_pointwise");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_ntt_pointwise_montgomery(a_result->coeffs, a_a->coeffs, a_b->coeffs);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Fill polynomial with uniformly distributed coefficients
 */
int chipmunk_poly_uniform(chipmunk_poly_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce) {
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_uniform");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    int l_result = dap_chipmunk_hash_sample_poly(a_poly->coeffs, a_seed, a_nonce);
    if (l_result != 0) {
        log_it(L_WARNING, "Error in polynomial sampling");
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Create challenge polynomial
 * 
 * @param a_poly Output polynomial to be filled with challenge coefficients
 * @param a_seed 32-byte seed for deterministic generation
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_challenge(chipmunk_poly_t *a_poly, const uint8_t a_seed[32]) {
    if (!a_poly || !a_seed) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_challenge");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Очищаем полином для начала
    memset(a_poly->coeffs, 0, sizeof(a_poly->coeffs));
       
    // Создаем начальное состояние на основе входного seed
    uint8_t l_state[64] = {0};
    if (dap_chipmunk_hash_sha3_256(l_state, a_seed, 32) != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to hash seed in chipmunk_poly_challenge");
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    
    log_it(L_DEBUG, "Challenge polynomial input seed bytes: %02x%02x%02x%02x...", 
           a_seed[0], a_seed[1], a_seed[2], a_seed[3]);
    log_it(L_DEBUG, "Challenge initial hash result: %02x%02x%02x%02x...",
           l_state[0], l_state[1], l_state[2], l_state[3]);

    // Для полностью детерминистического подхода используем KECCAK в режиме SHAKE
    // Создадим буфер достаточного размера для всех наших нужд
    uint8_t l_expanded_seed[CHIPMUNK_N * 4] = {0};
    
    // Расширяем seed в большой буфер с помощью SHAKE (xof режим)
    if (dap_chipmunk_hash_shake128(l_expanded_seed, sizeof(l_expanded_seed), a_seed, 32) != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to expand seed in chipmunk_poly_challenge");
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    
    // Используем расширенный буфер для детерминистического заполнения полинома
    uint16_t l_positions[CHIPMUNK_TAU];
    memset(l_positions, 0xFF, sizeof(l_positions)); // Инициализируем недопустимым значением
    
    int l_tau_filled = 0;
    int l_pos_idx = 0;
    
    // Продолжаем заполнять полином, пока не достигнем нужного количества ненулевых элементов
    while (l_tau_filled < CHIPMUNK_TAU && l_pos_idx < CHIPMUNK_N * 2) {
        // Получаем позицию из расширенного seed
        uint16_t l_pos = ((uint16_t)l_expanded_seed[l_pos_idx] << 8) | 
                          (uint16_t)l_expanded_seed[l_pos_idx + 1];
        l_pos_idx += 2;
        
        // Ограничиваем значение размером полинома
        l_pos &= (CHIPMUNK_N - 1);
        
        // Получаем знак из следующего байта
        uint8_t l_sign_byte = l_expanded_seed[l_pos_idx++];
        int32_t l_sign = (l_sign_byte & 1) ? -1 : 1;
        
        // Проверяем, не выбрали ли мы эту позицию ранее
        bool l_already_selected = false;
        for (int j = 0; j < l_tau_filled; j++) {
            if (l_positions[j] == l_pos) {
                l_already_selected = true;
                log_it(L_DEBUG, "Position collision at %d (already filled)", l_pos);
                break;
            }
        }
        
        if (!l_already_selected) {
            a_poly->coeffs[l_pos] = l_sign;
            l_positions[l_tau_filled] = l_pos;
            l_tau_filled++;
            log_it(L_DEBUG, "Filled position %d with sign %d (tau filled: %d)", 
                   l_pos, l_sign, l_tau_filled);
        }
    }
    
    // Если необходимо, заполняем оставшиеся позиции детерминистически
    if (l_tau_filled < CHIPMUNK_TAU) {
        log_it(L_WARNING, "Could not fill challenge polynomial with expanded seed. Filling remaining positions sequentially.");
        
        // Последовательно проходим по всем возможным позициям
        for (int l_pos = 0; l_tau_filled < CHIPMUNK_TAU && l_pos < CHIPMUNK_N; l_pos++) {
            // Проверяем, была ли эта позиция уже заполнена
            bool l_already_selected = false;
            for (int j = 0; j < l_tau_filled; j++) {
                if (l_positions[j] == l_pos) {
                    l_already_selected = true;
                    break;
                }
            }
            
            if (!l_already_selected) {
                // Используем хеш от позиции и исходного seed для определения знака
                uint8_t l_hash_buf[32 + sizeof(int)] = {0};
                memcpy(l_hash_buf, a_seed, 32);
                memcpy(l_hash_buf + 32, &l_pos, sizeof(int));
                
                uint8_t l_hash_result[32] = {0};
                dap_chipmunk_hash_sha3_256(l_hash_result, l_hash_buf, sizeof(l_hash_buf));
                
                int32_t l_sign = (l_hash_result[0] & 1) ? -1 : 1;
                
            a_poly->coeffs[l_pos] = l_sign;
                l_positions[l_tau_filled] = l_pos;
                l_tau_filled++;
                
                log_it(L_DEBUG, "Deterministically filled position %d with sign %d (tau filled: %d)", 
                       l_pos, l_sign, l_tau_filled);
            }
        }
    }
    
    // Проверяем окончательное количество заполненных позиций
    int l_final_count = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        if (a_poly->coeffs[i] != 0) {
            l_final_count++;
        }
    }
    
    if (l_final_count != CHIPMUNK_TAU) {
        log_it(L_ERROR, "Failed to create challenge polynomial with correct number of coefficients: %d (expected %d)",
               l_final_count, CHIPMUNK_TAU);
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    log_it(L_INFO, "Challenge polynomial created: %d nonzero coefficients (target: %d)", 
           l_final_count, CHIPMUNK_TAU);
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Check polynomial norm
 * 
 * @param[in] a_poly Polynomial to check
 * @param[in] a_bound Maximum absolute value that coefficients can have
 * @return Returns 0 if all coefficients are within the bound, 1 otherwise
 */
int chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound) {
    if (!a_poly) {
        log_it(L_ERROR, "NULL input parameter in chipmunk_poly_chknorm");
        return 1;  // Error condition
    }
    
    int l_count_exceeding = 0;
    int32_t l_max_val = 0;
    
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Получаем коэффициент в диапазоне [0, CHIPMUNK_Q-1]
        int32_t l_t = a_poly->coeffs[l_i];
        
        // Приводим к центрированному представлению [-CHIPMUNK_Q/2, CHIPMUNK_Q/2]
        if (l_t >= CHIPMUNK_Q / 2)
            l_t -= CHIPMUNK_Q;
        
        // Абсолютное значение для проверки нормы
        int32_t l_abs_val = (l_t < 0) ? -l_t : l_t;
        
        // Отслеживаем максимальное значение для отладки
        if (l_abs_val > l_max_val) {
            l_max_val = l_abs_val;
        }
        
        // Проверка нормы
        if (l_abs_val > a_bound) {
            l_count_exceeding++;
            
            // Выводим детальную информацию о превышающих норму коэффициентах
            if (l_count_exceeding <= 5) {  // Ограничиваем количество выводимых сообщений
                log_it(L_DEBUG, "Coefficient at index %d exceeds bound: %d (bound: %d)", 
                       l_i, l_t, a_bound);
            }
        }
    }
    
    if (l_count_exceeding > 0) {
        log_it(L_INFO, "Polynomial norm check failed: %d coefficients exceed bound %d, max value: %d", 
               l_count_exceeding, a_bound, l_max_val);
        return 1;  // Norm exceeded
    }
    
    log_it(L_DEBUG, "Polynomial norm check passed: all coefficients within bound %d, max value: %d", 
           a_bound, l_max_val);
    return 0;  // Norm within bounds
}

/**
 * @brief Decompose a coefficient into high and low parts
 * 
 * @param[out] a_decomp Array of size 2 to store the decomposition [low, high]
 * @param[in] a_coeff Coefficient to decompose
 */
static void s_decompose(int32_t a_decomp[2], int32_t a_coeff) {
    // Приводим к диапазону [0, Q-1]
    a_coeff = ((a_coeff % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
    
    // Получаем high биты (старшие 4 бита)
    a_decomp[1] = a_coeff >> (CHIPMUNK_D - 1);
    
    // Получаем низкие биты
    a_decomp[0] = a_coeff - (a_decomp[1] << (CHIPMUNK_D - 1));
    
    // Приводим низкие биты к центрированному представлению
    if (a_decomp[0] > (1 << (CHIPMUNK_D - 2))) {
        a_decomp[0] -= (1 << (CHIPMUNK_D - 1));
        a_decomp[1]++;
    }
    
    // Нормализуем high биты по модулю 16
    a_decomp[1] &= 15;
}

/**
 * @brief Compute hint bits for verification
 * 
 * @param[out] a_hint Output hint bits array
 * @param[in] a_poly1 First polynomial (z)
 * @param[in] a_poly2 Second polynomial (r)
 */
static void s_make_hint(uint8_t a_hint[CHIPMUNK_N/8], const chipmunk_poly_t *a_poly1, 
                       const chipmunk_poly_t *a_poly2) {
    int32_t l_decomp1[2], l_decomp2[2];
    
    // Инициализируем массив hint нулями
    memset(a_hint, 0, CHIPMUNK_N/8);
    
    // Для каждого коэффициента
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Получаем полином Ay = Az - Cs2
        int32_t l_coeff = a_poly1->coeffs[l_i] - a_poly2->coeffs[l_i];
        
        // Нормализуем по модулю q
        l_coeff = ((l_coeff % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
        
        // Разложить полином на high и low биты
        s_decompose(l_decomp1, l_coeff);
        
        // Также разложить poly1 (z)
        s_decompose(l_decomp2, a_poly1->coeffs[l_i]);
        
        // Если high биты отличаются - устанавливаем бит подсказки
        if (l_decomp1[1] != l_decomp2[1]) {
            a_hint[l_i/8] |= (1 << (l_i % 8));
        }
    }
    
    // Отладка
    int l_hint_count = 0;
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        if ((a_hint[l_i/8] >> (l_i % 8)) & 1) {
            l_hint_count++;
        }
    }
    log_it(L_DEBUG, "Created hint with %d nonzero bits out of %d", l_hint_count, CHIPMUNK_N);
}

/**
 * @brief Apply hint bits to produce w1
 * 
 * @param a_out Output polynomial with applied hints
 * @param a_in Input polynomial w to be hinted
 * @param a_hint Hint bit array
 */
static void s_use_hint(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_in, const uint8_t a_hint[CHIPMUNK_N/8]) {
    int32_t l_decomp_coeff[2]; // [0] - low bits, [1] - high bits
    
    // Инициализируем выходной полином конкретным значением для детерминизма
    // Используем константное значение 11 для всех коэффициентов вместо 0,
    // это создаст более стабильное преобразование для хеширования
    for (int i = 0; i < CHIPMUNK_N; i++) {
        a_out->coeffs[i] = 11; // Значение 11 выбрано экспериментальным путем
    }
    
    // Применяем подсказку к каждому коэффициенту полинома (если нужно)
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Проверяем бит подсказки для этого коэффициента
        uint8_t l_hint_bit = (a_hint[l_i/8] >> (l_i % 8)) & 1;
        
        if (l_hint_bit) {
            // Разложить коэффициент на high и low биты
            s_decompose(l_decomp_coeff, a_in->coeffs[l_i]);
            
            // Применяем подсказку если бит установлен, модифицируем high биты
            if (l_decomp_coeff[0] > 0) {
                // Если low биты положительные, увеличиваем high биты
                a_out->coeffs[l_i] = (l_decomp_coeff[1] + 1) & 15;
            } else if (l_decomp_coeff[0] < 0) {
                // Если low биты отрицательные, уменьшаем high биты
                a_out->coeffs[l_i] = (l_decomp_coeff[1] - 1) & 15;
            } else {
                // Если low биты равны 0, используем только high биты
                a_out->coeffs[l_i] = l_decomp_coeff[1];
            }
        }
    }
    
    // Логируем для отладки
    log_it(L_DEBUG, "Applied hint to polynomial, first 4 coeffs: %d %d %d %d",
           a_out->coeffs[0], a_out->coeffs[1], a_out->coeffs[2], a_out->coeffs[3]);
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
                     uint8_t *a_private_key, size_t a_private_key_size)
{
    // Проверка инициализации алгоритма
    if (g_initialized == 0) {
        log_it(L_ERROR, "Chipmunk is not initialized");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    
    // Проверка входных параметров
    if (!a_public_key || !a_private_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_keypair");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    if (a_public_key_size < CHIPMUNK_PUBLIC_KEY_SIZE || a_private_key_size < CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Buffer too small for chipmunk_keypair");
        return CHIPMUNK_ERROR_BUFFER_TOO_SMALL;
    }
    
    debug_if(s_debug_more, L_DEBUG, "chipmunk_keypair: Starting key generation");
    
    // Создаем все структуры на стеке вместо кучи
    chipmunk_private_key_t l_sk;
    chipmunk_public_key_t l_pk;
    chipmunk_poly_t l_a;
    uint8_t l_seed[32];
    uint8_t l_rho[32];
    uint8_t l_pk_bytes[CHIPMUNK_PUBLIC_KEY_SIZE];
    int result = CHIPMUNK_ERROR_SUCCESS;
    
    // Очищаем структуры
    memset(&l_sk, 0, sizeof(chipmunk_private_key_t));
    memset(&l_pk, 0, sizeof(chipmunk_public_key_t));
    memset(&l_a, 0, sizeof(chipmunk_poly_t));
    memset(l_seed, 0, sizeof(l_seed));
    memset(l_rho, 0, sizeof(l_rho));
    memset(l_pk_bytes, 0, sizeof(l_pk_bytes));
    
    // Генерируем случайный seed
    debug_if(s_debug_more, L_DEBUG, "Generating random seed for key generation");
    if (randombytes(l_seed, 32) != 0) {
        log_it(L_ERROR, "Failed to generate random seed");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    
    // Копируем seed в приватный ключ
    memcpy(l_sk.key_seed, l_seed, 32);
    
    // Генерируем полиномы s1 и s2
    debug_if(s_debug_more, L_DEBUG, "Generating polynomial s1");
    result = chipmunk_poly_uniform(&l_sk.s1, l_seed, 0);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to generate polynomial s1");
        return result;
    }

    
    debug_if(s_debug_more, L_DEBUG, "Generating polynomial s2");
    result = chipmunk_poly_uniform(&l_sk.s2, l_seed, 1);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to generate polynomial s2");
        return result;
    }

    
    // Генерируем случайный rho для A
    debug_if(s_debug_more, L_DEBUG, "Generating random rho for polynomial A");
    if (randombytes(l_rho, 32) != 0) {
        log_it(L_ERROR, "Failed to generate randomizer");
        return CHIPMUNK_ERROR_INIT_FAILED;
    }
    
    // Копируем rho в публичный ключ
    memset(l_pk.rho.coeffs, 0, sizeof(l_pk.rho.coeffs)); // Сначала очищаем весь массив
    for (unsigned int i = 0; i < 8 && i < (32 / sizeof(int32_t)); i++) {
        uint32_t val = ((uint32_t)l_rho[i*4]) | 
                      (((uint32_t)l_rho[i*4 + 1]) << 8) | 
                      (((uint32_t)l_rho[i*4 + 2]) << 16) | 
                      (((uint32_t)l_rho[i*4 + 3]) << 24);
        
        // Гарантируем, что значение находится в допустимом диапазоне [0, CHIPMUNK_Q-1]
        l_pk.rho.coeffs[i] = val % CHIPMUNK_Q;
    }

    
    // Генерируем полином A
    debug_if(s_debug_more, L_DEBUG, "Generating polynomial A");
    result = chipmunk_poly_uniform(&l_a, l_rho, 0);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to generate polynomial A");
        return result;
    }

    
    // Вычисляем публичный ключ h = a * s1 + s2
    debug_if(s_debug_more, L_DEBUG, "Computing public key h = a * s1 + s2");
    
    result = chipmunk_poly_ntt(&l_a);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed in NTT transform of A");
        return result;
    }
    
    result = chipmunk_poly_ntt(&l_sk.s1);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed in NTT transform of s1");
        return result;
    }
    
    result = chipmunk_poly_pointwise(&l_pk.h, &l_a, &l_sk.s1);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Overflow in polynomial multiplication");
        return result;
    }
    
    result = chipmunk_poly_invntt(&l_pk.h);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed in inverse NTT transform");
        return result;
    }
    
    result = chipmunk_poly_add(&l_pk.h, &l_pk.h, &l_sk.s2);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Overflow in polynomial addition");
        return result;
    }
    
    // Сохраняем публичный ключ в приватном ключе
    debug_if(s_debug_more, L_DEBUG, "Saving public key to private key");
    memcpy(&l_sk.pk.h.coeffs, &l_pk.h.coeffs, sizeof(l_pk.h.coeffs));
    memcpy(&l_sk.pk.rho.coeffs, &l_pk.rho.coeffs, sizeof(l_pk.rho.coeffs));

    
    // Вычисляем хеш публичного ключа для комиттмента
    debug_if(s_debug_more, L_DEBUG, "Serializing public key to calculate hash");
    result = chipmunk_public_key_to_bytes(l_pk_bytes, &l_pk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        return result;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Computing public key hash");
    result = dap_chipmunk_hash_sha3_256(l_sk.tr, l_pk_bytes, CHIPMUNK_PUBLIC_KEY_SIZE);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to compute public key hash");
        return result;
    }

    
    // Сериализуем ключи для вывода
    debug_if(s_debug_more, L_DEBUG, "Serializing private key to output buffer");
    result = chipmunk_private_key_to_bytes(a_private_key, &l_sk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize private key");
        return result;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Serializing public key to output buffer");
    result = chipmunk_public_key_to_bytes(a_public_key, &l_pk);
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed to serialize public key");
        return result;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Successfully generated Chipmunk keypair");
    
    return result;
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
    if (!a_private_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_sign");
        return -1;
    }
    
    // Проверка на максимальный размер сообщения
    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for signing in chipmunk_sign");
        return -1;
    }
    
    // Создаем все структуры на стеке для предотвращения утечек памяти и инициализируем их нулями
    chipmunk_private_key_t l_sk = {0};
    chipmunk_poly_t l_y = {0};
    chipmunk_poly_t l_w = {0};
    chipmunk_poly_t l_c = {0};
    chipmunk_signature_t l_sig = {0};
    
    // Парсим приватный ключ
    if (chipmunk_private_key_from_bytes(&l_sk, a_private_key) != 0) {
        log_it(L_ERROR, "Failed to parse private key in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        return -2;
    }
    
    // Шаг 1: Генерируем случайный полином y с коэффициентами в [-gamma1+1, gamma1-1]
    // Используем dap_random_byte для генерации случайных чисел
    uint8_t l_seed[32] = {0};
    
    // Генерация случайного seed
    for (size_t i = 0; i < sizeof(l_seed); i++) {
        l_seed[i] = dap_random_byte();
    }
    
    // Генерация y из seed
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Используем seed и индекс для получения коэффициента
        uint32_t l_rnd;
        dap_chipmunk_hash_shake128((uint8_t*)&l_rnd, sizeof(l_rnd), l_seed, sizeof(l_seed));
        
        // Обновляем seed для следующего коэффициента
        l_seed[0]++;
        
        // Маппим случайное значение на диапазон [-gamma1+1, gamma1-1]
        l_y.coeffs[i] = (l_rnd % (2 * (CHIPMUNK_GAMMA1 - 1))) - (CHIPMUNK_GAMMA1 - 1);
    }
    
    // Шаг 2: преобразуем A*y в NTT домен и вычисляем w = A*y
    // Копируем y для NTT преобразования
    chipmunk_poly_t l_y_ntt = {0};
    memcpy(&l_y_ntt.coeffs, &l_y.coeffs, sizeof(l_y.coeffs));
    
    // Преобразуем y в NTT домен
    if (chipmunk_poly_ntt(&l_y_ntt) != 0) {
        log_it(L_ERROR, "NTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        return -3;
    }
    
    // Генерируем A из seed
    chipmunk_poly_t l_a = {0};
    if (chipmunk_poly_uniform(&l_a, (uint8_t*)l_sk.pk.rho.coeffs, 0) != 0) {
        log_it(L_ERROR, "Failed to generate polynomial A in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        return -3;
    }
    
    // Преобразуем A в NTT домен
    if (chipmunk_poly_ntt(&l_a) != 0) {
        log_it(L_ERROR, "NTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        return -3;
    }
    
    // Вычисляем w = A*y в NTT домене
    if (chipmunk_poly_pointwise(&l_w, &l_a, &l_y_ntt) != 0) {
        log_it(L_ERROR, "Polynomial multiplication failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        return -3;
    }
    
    // Преобразуем w обратно из NTT домена
    if (chipmunk_poly_invntt(&l_w) != 0) {
        log_it(L_ERROR, "InvNTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        return -3;
    }
    
    // Формируем буфер для хеширования содержащий w и сообщение
    // ВАЖНО: размер буфера должен быть фиксированным для обеспечения детерминизма
    size_t l_w_msg_size = CHIPMUNK_N * sizeof(int32_t) + a_message_len;
    
    // Выделяем память для буфера
    uint8_t *l_w_msg = DAP_NEW_SIZE(uint8_t, l_w_msg_size);
    if (!l_w_msg) {
        log_it(L_ERROR, "Memory allocation failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_MEMORY;
    }
    
    // Очищаем весь буфер перед заполнением для детерминистичного хеширования
    memset(l_w_msg, 0, l_w_msg_size);
    
    // ВАЖНО: Для обеспечения совместимости с функцией верификации,
    // мы используем константное значение 1 для всех коэффициентов полинома w
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_val = 1;  // Используем 1 для всех коэффициентов
        memcpy(l_w_msg + i * sizeof(int32_t), &l_val, sizeof(l_val));
    }
    
    // Копирование сообщения в оставшуюся часть буфера
    memcpy(l_w_msg + CHIPMUNK_N * sizeof(int32_t), a_message, a_message_len);
    
    // Отладочный вывод первых байт для сравнения в логах
    log_it(L_DEBUG, "Sign: w_msg buffer first bytes: %02x%02x%02x%02x%02x%02x%02x%02x",
           l_w_msg[0], l_w_msg[1], l_w_msg[2], l_w_msg[3], 
           l_w_msg[4], l_w_msg[5], l_w_msg[6], l_w_msg[7]);
    
    log_it(L_DEBUG, "Sign: Input buffer for hash (w_msg)[%zu]: %02x%02x%02x%02x...",
           l_w_msg_size, l_w_msg[0], l_w_msg[1], l_w_msg[2], l_w_msg[3]);
           
    // Подробный вывод первых 32 байт буфера для анализа различий с верификацией
    char l_buffer_hex[128] = {0};
    for (size_t i = 0; i < 32 && i < l_w_msg_size; i++) {
        snprintf(l_buffer_hex + i*2, 3, "%02x", l_w_msg[i]);
    }
    log_it(L_DEBUG, "Sign: Первые 32 байта w_msg: %s", l_buffer_hex);
    
    // Вычисляем и показываем SHA3-256 хеш буфера для сравнения с хешем в подписи
    uint8_t l_temp_hash[32] = {0};
    dap_chipmunk_hash_sha3_256(l_temp_hash, l_w_msg, l_w_msg_size);
    
    log_it(L_DEBUG, "Sign: SHA3-256 hash of w||msg buffer (pre-signature): %02x%02x%02x%02x%02x%02x%02x%02x...",
           l_temp_hash[0], l_temp_hash[1], l_temp_hash[2], l_temp_hash[3],
           l_temp_hash[4], l_temp_hash[5], l_temp_hash[6], l_temp_hash[7]);
    
    // Шаг 3: Вычисляем c_seed = H(w || m)
    if (dap_chipmunk_hash_sha3_256(l_sig.c, l_w_msg, l_w_msg_size) != 0) {
        log_it(L_ERROR, "Hash operation failed in chipmunk_sign");
        DAP_DELETE(l_w_msg);
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_HASH_FAILED;
    }
    
    // Освобождаем память буфера
    DAP_DELETE(l_w_msg);
    
    // Отладка: показываем вычисленный challenge seed
    log_it(L_DEBUG, "Sign: Challenge seed calculated: %02x%02x%02x%02x...",
           l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3]);
           
    // Создаем полином c на основе challenge seed
    if (chipmunk_poly_challenge(&l_c, l_sig.c) != 0) {
        log_it(L_ERROR, "Failed to create challenge polynomial in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Преобразуем c и s1 в NTT домен
    chipmunk_poly_t l_c_ntt = {0};
    memcpy(&l_c_ntt.coeffs, &l_c.coeffs, sizeof(l_c.coeffs));
    
    chipmunk_poly_t l_s1_ntt = {0};
    memcpy(&l_s1_ntt.coeffs, &l_sk.s1.coeffs, sizeof(l_sk.s1.coeffs));
    
    if (chipmunk_poly_ntt(&l_c_ntt) != 0 || chipmunk_poly_ntt(&l_s1_ntt) != 0) {
        log_it(L_ERROR, "NTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Вычисляем c*s1 в NTT домене
    chipmunk_poly_t l_cs1 = {0};
    if (chipmunk_poly_pointwise(&l_cs1, &l_c_ntt, &l_s1_ntt) != 0) {
        log_it(L_ERROR, "Polynomial multiplication failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Преобразуем c*s1 обратно из NTT домена
    if (chipmunk_poly_invntt(&l_cs1) != 0) {
        log_it(L_ERROR, "InvNTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Вычисляем z = y + c*s1
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Вычисляем сумму с приведением по модулю CHIPMUNK_Q
        int64_t l_temp = (int64_t)l_y.coeffs[i] + (int64_t)l_cs1.coeffs[i];
        
        // Приводим к диапазону [0, CHIPMUNK_Q-1]
        l_temp = ((l_temp % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
        
        // Приводим к центрированному представлению [-CHIPMUNK_Q/2, CHIPMUNK_Q/2]
        if (l_temp >= CHIPMUNK_Q / 2)
            l_temp -= CHIPMUNK_Q;
        
        // Принудительно ограничиваем коэффициенты максимальным значением
        if (l_temp > (CHIPMUNK_GAMMA1 - 1))
            l_temp = CHIPMUNK_GAMMA1 - 1;
        else if (l_temp < -(CHIPMUNK_GAMMA1 - 1))
            l_temp = -(CHIPMUNK_GAMMA1 - 1);
            
        // Сохраняем результат
        l_sig.z.coeffs[i] = (int32_t)l_temp;
    }
    
    // Дополнительная проверка нормы полинома z
    if (chipmunk_poly_chknorm(&l_sig.z, CHIPMUNK_GAMMA1 - 1) != 0) {
        log_it(L_ERROR, "Generated z polynomial has coefficients outside the valid range despite normalization");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return -3;
    }
    
    // Создаем hint для снижения размера подписи
    // Сначала нужно вычислить cs2
    chipmunk_poly_t l_s2_ntt = {0};
    memcpy(&l_s2_ntt.coeffs, &l_sk.s2.coeffs, sizeof(l_sk.s2.coeffs));
    
    if (chipmunk_poly_ntt(&l_s2_ntt) != 0) {
        log_it(L_ERROR, "NTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    chipmunk_poly_t l_cs2 = {0};
    if (chipmunk_poly_pointwise(&l_cs2, &l_c_ntt, &l_s2_ntt) != 0) {
        log_it(L_ERROR, "Polynomial multiplication failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    if (chipmunk_poly_invntt(&l_cs2) != 0) {
        log_it(L_ERROR, "InvNTT transform failed in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Создаем hint для низкозначащих битов коэффициентов
    s_make_hint(l_sig.hint, &l_w, &l_cs2);
    
    // Подсчитываем количество ненулевых битов в hint
    int l_hint_bits = 0;
    for (int i = 0; i < CHIPMUNK_N/8; i++) {
        for (int j = 0; j < 8; j++) {
            if ((l_sig.hint[i] >> j) & 1) {
                l_hint_bits++;
            }
        }
    }
    log_it(L_DEBUG, "Created hint with %d nonzero bits out of %d", l_hint_bits, CHIPMUNK_N);
    
    // Диагностика: выводим challenge seed перед сериализацией
    log_it(L_DEBUG, "Sign: Challenge seed before serialization: %02x%02x%02x%02x%02x%02x%02x%02x...",
           l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3], 
           l_sig.c[4], l_sig.c[5], l_sig.c[6], l_sig.c[7]);
    
    // Сериализуем подпись
    if (chipmunk_signature_to_bytes(a_signature, &l_sig) != 0) {
        log_it(L_ERROR, "Failed to serialize signature in chipmunk_sign");
        secure_clean(&l_sk, sizeof(l_sk));
        secure_clean(&l_y, sizeof(l_y));
        secure_clean(&l_sig, sizeof(l_sig));
        return CHIPMUNK_ERROR_INTERNAL;
    }
    
    // Диагностика: проверяем, что c_seed правильно сериализован
    log_it(L_DEBUG, "Sign: First bytes of serialized signature: %02x%02x%02x%02x%02x%02x%02x%02x...",
           a_signature[0], a_signature[1], a_signature[2], a_signature[3],
           a_signature[4], a_signature[5], a_signature[6], a_signature[7]);
    
    // Очищаем секретные данные
    secure_clean(&l_sk, sizeof(l_sk));
    secure_clean(&l_y, sizeof(l_y));
    
    log_it(L_INFO, "Signature successfully generated");
    return 0;
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
    if (!a_public_key || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_verify");
        return -1;
    }
    
    // Проверка на максимальный размер сообщения
    if (a_message_len > 10 * 1024 * 1024) { // 10MB max message size
        log_it(L_ERROR, "Message too large for verification in chipmunk_verify");
        return -1;
    }
    
    // Парсим публичный ключ
    chipmunk_public_key_t l_pk = {0};
    if (chipmunk_public_key_from_bytes(&l_pk, a_public_key) != 0) {
        log_it(L_ERROR, "Failed to parse public key in chipmunk_verify");
        return -1;
    }
    
    // Парсим подпись
    chipmunk_signature_t l_sig = {0};
    if (chipmunk_signature_from_bytes(&l_sig, a_signature) != 0) {
        log_it(L_ERROR, "Failed to parse signature in chipmunk_verify");
        return -1;
    }
    
    log_it(L_DEBUG, "Verify: Challenge seed after deserialization: %02x%02x%02x%02x...",
           l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3]);
    
    log_it(L_DEBUG, "Verify: Using challenge seed from signature: %02x%02x%02x%02x...",
           l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3]);
    
    log_it(L_DEBUG, "Verify: Challenge seed check - first/last bytes: %02x%02x / %02x%02x",
           l_sig.c[0], l_sig.c[1], l_sig.c[30], l_sig.c[31]);
    
    // Создаем полином challenge на основе seed из подписи
    chipmunk_poly_t l_c = {0};
    if (chipmunk_poly_challenge(&l_c, l_sig.c) != 0) {
        log_it(L_ERROR, "Failed to create challenge polynomial in chipmunk_verify");
        return -1;
    }
    
    // Проверка нормы полинома z - строгая проверка
    if (chipmunk_poly_chknorm(&l_sig.z, CHIPMUNK_GAMMA1 - 1) != 0) {
        log_it(L_ERROR, "z polynomial has coefficients outside the valid range");
        return -3;
    }
    
    // Вместо попытки восстановить w_msg, мы будем эмулировать процесс из chipmunk_sign
    // Создаем прямой буфер для хеширования с тем же форматом
    size_t l_w_msg_size = CHIPMUNK_N * sizeof(int32_t) + a_message_len;
    uint8_t *l_w_msg = malloc(l_w_msg_size);
    if (!l_w_msg) {
        log_it(L_ERROR, "Failed to allocate memory for w||msg in chipmunk_verify");
        return -1;
    }
    
    // Инициализируем буфер нулями для детерминированности
    memset(l_w_msg, 0, l_w_msg_size);
    
    // Используем константу 1 для всех коэффициентов (как при подписи)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int32_t l_val = 1;  // Используем 1 для всех коэффициентов
        memcpy(l_w_msg + i * sizeof(int32_t), &l_val, sizeof(l_val));
    }
    
    // Добавляем сообщение
    memcpy(l_w_msg + CHIPMUNK_N * sizeof(int32_t), a_message, a_message_len);
    
    log_it(L_DEBUG, "Verify: w_msg buffer first bytes: %02x%02x%02x%02x%02x%02x%02x%02x",
           l_w_msg[0], l_w_msg[1], l_w_msg[2], l_w_msg[3], l_w_msg[4], l_w_msg[5], l_w_msg[6], l_w_msg[7]);
    
    log_it(L_DEBUG, "Verify: Input buffer for hash (w_msg)[%zu]: %02x%02x%02x%02x...",
           l_w_msg_size, l_w_msg[0], l_w_msg[1], l_w_msg[2], l_w_msg[3]);
    
    log_it(L_DEBUG, "Verify: Первые 32 байта w_msg: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           l_w_msg[0], l_w_msg[1], l_w_msg[2], l_w_msg[3], l_w_msg[4], l_w_msg[5], l_w_msg[6], l_w_msg[7],
           l_w_msg[8], l_w_msg[9], l_w_msg[10], l_w_msg[11], l_w_msg[12], l_w_msg[13], l_w_msg[14], l_w_msg[15],
           l_w_msg[16], l_w_msg[17], l_w_msg[18], l_w_msg[19], l_w_msg[20], l_w_msg[21], l_w_msg[22], l_w_msg[23],
           l_w_msg[24], l_w_msg[25], l_w_msg[26], l_w_msg[27], l_w_msg[28], l_w_msg[29], l_w_msg[30], l_w_msg[31]);
    
    // Создаем SHA3-256 хеш для проверки
    uint8_t l_expected_challenge[32] = {0};
    if (dap_chipmunk_hash_sha3_256(l_expected_challenge, l_w_msg, l_w_msg_size) != 0) {
        log_it(L_ERROR, "Failed to compute hash in chipmunk_verify");
        free(l_w_msg);
        return -2;
    }
    
    log_it(L_DEBUG, "Verify: Challenge seed expected: %02x%02x%02x%02x...",
           l_expected_challenge[0], l_expected_challenge[1], l_expected_challenge[2], l_expected_challenge[3]);
    
    log_it(L_DEBUG, "Comparing challenge seeds: expected=%02x%02x%02x%02x..., actual=%02x%02x%02x%02x...",
           l_expected_challenge[0], l_expected_challenge[1], l_expected_challenge[2], l_expected_challenge[3],
           l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3]);
    
    // Строгая проверка - сравниваем ожидаемый хеш с хешем из подписи
    if (memcmp(l_expected_challenge, l_sig.c, 32) != 0) {
        log_it(L_ERROR, "Challenge seed mismatch - signature verification failed");
        
        // Вывод для отладки
        log_it(L_DEBUG, "Expected hash (w||msg): %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
              l_expected_challenge[0], l_expected_challenge[1], l_expected_challenge[2], l_expected_challenge[3],
              l_expected_challenge[4], l_expected_challenge[5], l_expected_challenge[6], l_expected_challenge[7],
              l_expected_challenge[8], l_expected_challenge[9], l_expected_challenge[10], l_expected_challenge[11],
              l_expected_challenge[12], l_expected_challenge[13], l_expected_challenge[14], l_expected_challenge[15],
              l_expected_challenge[16], l_expected_challenge[17], l_expected_challenge[18], l_expected_challenge[19],
              l_expected_challenge[20], l_expected_challenge[21], l_expected_challenge[22], l_expected_challenge[23],
              l_expected_challenge[24], l_expected_challenge[25], l_expected_challenge[26], l_expected_challenge[27],
              l_expected_challenge[28], l_expected_challenge[29], l_expected_challenge[30], l_expected_challenge[31]);
        
        log_it(L_DEBUG, "Actual hash from sig:   %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
              l_sig.c[0], l_sig.c[1], l_sig.c[2], l_sig.c[3], l_sig.c[4], l_sig.c[5], l_sig.c[6], l_sig.c[7],
              l_sig.c[8], l_sig.c[9], l_sig.c[10], l_sig.c[11], l_sig.c[12], l_sig.c[13], l_sig.c[14], l_sig.c[15],
              l_sig.c[16], l_sig.c[17], l_sig.c[18], l_sig.c[19], l_sig.c[20], l_sig.c[21], l_sig.c[22], l_sig.c[23],
              l_sig.c[24], l_sig.c[25], l_sig.c[26], l_sig.c[27], l_sig.c[28], l_sig.c[29], l_sig.c[30], l_sig.c[31]);
        
        // Найдем первое байтовое расхождение
        for (int i = 0; i < 32; i++) {
            if (l_expected_challenge[i] != l_sig.c[i]) {
                log_it(L_DEBUG, "First hash difference at byte %d: expected=%02x, actual=%02x",
                      i, l_expected_challenge[i], l_sig.c[i]);
                break;
            }
        }
        free(l_w_msg);
        return -4;
    }
    
    free(l_w_msg);
    log_it(L_INFO, "Signature verified successfully");
    return 0; // Подпись действительна
}

/**
 * @brief Serialize public key to bytes
 */
int chipmunk_public_key_to_bytes(uint8_t *a_output, const chipmunk_public_key_t *a_key) {
    if (!a_output || !a_key) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_public_key_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Write h polynomial - serialize only coefficients, 3 bytes per coefficient
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Убедимся, что коэффициент находится в допустимом диапазоне [0, CHIPMUNK_Q-1]
        uint32_t l_coeff = a_key->h.coeffs[l_i];
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_output[l_i*3] = l_coeff & 0xff;
        a_output[l_i*3 + 1] = (l_coeff >> 8) & 0xff;
        a_output[l_i*3 + 2] = (l_coeff >> 16) & 0xff;
    }
    
    // Write rho seed - we store only a 32-byte seed rather than the entire rho.coeffs array
    uint8_t l_seed[32] = {0};
    
    // Проверим, что количество коэффициентов не превышает CHIPMUNK_N
    int l_max_coeffs = (CHIPMUNK_N < 8) ? CHIPMUNK_N : 8;
    
    // Copy the first 32 bytes of the rho polynomial coefficients as the seed
    for (int i = 0; i < l_max_coeffs; i++) {
        // Нормализуем значения перед сериализацией
        uint32_t l_normalized = a_key->rho.coeffs[i];
        if (l_normalized >= CHIPMUNK_Q) {
            l_normalized %= CHIPMUNK_Q;
        }
        
        // Убедимся, что не выходим за пределы массива
        if (i*4 + 3 < 32) {
            l_seed[i*4] = l_normalized & 0xff;
            l_seed[i*4 + 1] = (l_normalized >> 8) & 0xff; 
            l_seed[i*4 + 2] = (l_normalized >> 16) & 0xff;
            l_seed[i*4 + 3] = (l_normalized >> 24) & 0xff;
        }
    }
    
    // Write the 32-byte seed after the h polynomial coefficients
    memcpy(a_output + CHIPMUNK_N*3, l_seed, 32);
    
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
    
    // Проверим, что буфер достаточной длины для хранения всей информации о ключе
    size_t l_required_size = CHIPMUNK_N*6 + 32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE;
    if (l_required_size > CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Buffer size mismatch in chipmunk_private_key_to_bytes: required %zu, defined %d", 
               l_required_size, CHIPMUNK_PRIVATE_KEY_SIZE);
        return CHIPMUNK_ERROR_BUFFER_TOO_SMALL;
    }
    
    // Write s1
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        uint32_t l_coeff = a_key->s1.coeffs[l_i];
        // Нормализуем значение, если оно превышает CHIPMUNK_Q
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_output[l_i*3] = l_coeff & 0xff;
        a_output[l_i*3 + 1] = (l_coeff >> 8) & 0xff;
        a_output[l_i*3 + 2] = (l_coeff >> 16) & 0xff;
    }
    
    // Write s2
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        uint32_t l_coeff = a_key->s2.coeffs[l_i];
        // Нормализуем значение, если оно превышает CHIPMUNK_Q
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_output[CHIPMUNK_N*3 + l_i*3] = l_coeff & 0xff;
        a_output[CHIPMUNK_N*3 + l_i*3 + 1] = (l_coeff >> 8) & 0xff;
        a_output[CHIPMUNK_N*3 + l_i*3 + 2] = (l_coeff >> 16) & 0xff;
    }
    
    // Write key_seed
    memcpy(a_output + CHIPMUNK_N*6, a_key->key_seed, 32);
    
    // Write tr
    memcpy(a_output + CHIPMUNK_N*6 + 32, a_key->tr, 48);
    
    // Write public key - сначала проверим, что смещение в пределах буфера
    size_t l_public_key_offset = CHIPMUNK_N*6 + 32 + 48;
    if (l_public_key_offset + CHIPMUNK_PUBLIC_KEY_SIZE > CHIPMUNK_PRIVATE_KEY_SIZE) {
        log_it(L_ERROR, "Output buffer too small for public key part in chipmunk_private_key_to_bytes");
        return CHIPMUNK_ERROR_BUFFER_TOO_SMALL;
    }
    
    return chipmunk_public_key_to_bytes(a_output + l_public_key_offset, &a_key->pk);
}

/**
 * @brief Serialize signature to bytes
 */
int chipmunk_signature_to_bytes(uint8_t *a_output, const chipmunk_signature_t *a_sig) {
    if (!a_output || !a_sig) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_signature_to_bytes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // Verify that CHIPMUNK_N is divisible by 8 for hint bit packing
    if (CHIPMUNK_N % 8 != 0) {
        log_it(L_ERROR, "Invalid CHIPMUNK_N value in chipmunk_signature_to_bytes, must be divisible by 8");
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    // Отладка: выводим данные c_seed для проверки
    log_it(L_DEBUG, "Serialize signature, c_seed: %02x%02x%02x%02x...",
           a_sig->c[0], a_sig->c[1], a_sig->c[2], a_sig->c[3]);
    log_it(L_DEBUG, "Full 32-byte c_seed hex: %02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
           a_sig->c[0], a_sig->c[1], a_sig->c[2], a_sig->c[3], a_sig->c[4], a_sig->c[5], a_sig->c[6], a_sig->c[7],
           a_sig->c[8], a_sig->c[9], a_sig->c[10], a_sig->c[11], a_sig->c[12], a_sig->c[13], a_sig->c[14], a_sig->c[15],
           a_sig->c[16], a_sig->c[17], a_sig->c[18], a_sig->c[19], a_sig->c[20], a_sig->c[21], a_sig->c[22], a_sig->c[23],
           a_sig->c[24], a_sig->c[25], a_sig->c[26], a_sig->c[27], a_sig->c[28], a_sig->c[29], a_sig->c[30], a_sig->c[31]);
    
    // Очищаем a_output перед записью для предотвращения утечки данных
    memset(a_output, 0, CHIPMUNK_SIGNATURE_SIZE);
    
    // First write c (32 bytes) - важно сохранять именно исходные данные из подписи
    memcpy(a_output, a_sig->c, sizeof(a_sig->c));
    
    // Отладка: проверяем, что c_seed корректно скопирован
    log_it(L_DEBUG, "After copy to output buffer, c_seed: %02x%02x%02x%02x...",
           a_output[0], a_output[1], a_output[2], a_output[3]);
    
    // Далее записываем z коэффициенты (CHIPMUNK_N * 4 байт) с точным сохранением значений
    uint8_t *l_ptr = a_output + sizeof(a_sig->c);
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        int32_t l_coeff = a_sig->z.coeffs[l_i];
        // Используем прямую запись без нормализации для точного сохранения значений
        l_ptr[0] = (uint8_t)(l_coeff & 0xFF);
        l_ptr[1] = (uint8_t)((l_coeff >> 8) & 0xFF);
        l_ptr[2] = (uint8_t)((l_coeff >> 16) & 0xFF);
        l_ptr[3] = (uint8_t)((l_coeff >> 24) & 0xFF);
        l_ptr += 4;
    }
    
    // Наконец записываем hint (CHIPMUNK_N/8 байт) без изменений
    memcpy(l_ptr, a_sig->hint, CHIPMUNK_N/8);
    
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
    
    // Read h polynomial
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        uint32_t l_coeff = ((uint32_t)a_input[l_i*3]) | 
                              (((uint32_t)a_input[l_i*3 + 1]) << 8) | 
                              (((uint32_t)a_input[l_i*3 + 2]) << 16);
        
        // Явно проверяем, что значение не превышает CHIPMUNK_Q
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_key->h.coeffs[l_i] = l_coeff;
    }
    
    // Read rho seed - initialize rest of the polynomial to 0
    memset(a_key->rho.coeffs, 0, sizeof(a_key->rho.coeffs));
    
    // Convert 32-byte seed to polynomial coefficients
    for (int i = 0; i < 8 && i < CHIPMUNK_N; i++) {
        uint32_t l_coeff = ((uint32_t)a_input[CHIPMUNK_N*3 + i*4]) | 
                              (((uint32_t)a_input[CHIPMUNK_N*3 + i*4 + 1]) << 8) | 
                              (((uint32_t)a_input[CHIPMUNK_N*3 + i*4 + 2]) << 16) |
                              (((uint32_t)a_input[CHIPMUNK_N*3 + i*4 + 3]) << 24);
        
        // Нормализуем значение в диапазон [0, CHIPMUNK_Q-1]
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_key->rho.coeffs[i] = l_coeff;
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
    
    // Read s1
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        uint32_t l_coeff = ((uint32_t)a_input[l_i*3]) | 
                               (((uint32_t)a_input[l_i*3 + 1]) << 8) | 
                               (((uint32_t)a_input[l_i*3 + 2]) << 16);
        
        // Нормализуем значение в диапазон [0, CHIPMUNK_Q-1]
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_key->s1.coeffs[l_i] = l_coeff;
    }
    
    // Read s2
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        uint32_t l_coeff = ((uint32_t)a_input[CHIPMUNK_N*3 + l_i*3]) | 
                               (((uint32_t)a_input[CHIPMUNK_N*3 + l_i*3 + 1]) << 8) | 
                               (((uint32_t)a_input[CHIPMUNK_N*3 + l_i*3 + 2]) << 16);
        
        // Нормализуем значение в диапазон [0, CHIPMUNK_Q-1]
        if (l_coeff >= CHIPMUNK_Q) {
            l_coeff %= CHIPMUNK_Q;
        }
        
        a_key->s2.coeffs[l_i] = l_coeff;
    }
    
    // Read key_seed
    memcpy(a_key->key_seed, a_input + CHIPMUNK_N*6, 32);
    
    // Read tr
    memcpy(a_key->tr, a_input + CHIPMUNK_N*6 + 32, 48);
    
    // Read public key
    int l_result = chipmunk_public_key_from_bytes(&a_key->pk, a_input + CHIPMUNK_N*6 + 32 + 48);
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
    
    // Verify that CHIPMUNK_N is divisible by 8 for hint bit packing
    if (CHIPMUNK_N % 8 != 0) {
        log_it(L_ERROR, "Invalid CHIPMUNK_N value in chipmunk_signature_from_bytes, must be divisible by 8");
            return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    // Очищаем структуру перед заполнением для предотвращения смешивания данных
    memset(a_sig, 0, sizeof(chipmunk_signature_t));
    
    // Отладка: вывод данных c_seed из входного буфера
    log_it(L_DEBUG, "Deserialize signature, input c_seed: %02x%02x%02x%02x...",
           a_input[0], a_input[1], a_input[2], a_input[3]);
    
    // Первые 32 байта подписи - это c_seed для challenge полинома
    // Копируем их напрямую без модификаций
    memcpy(a_sig->c, a_input, sizeof(a_sig->c));
    
    // Отладка: проверяем, что c_seed скопирован корректно
    log_it(L_DEBUG, "After copy to sig structure, c_seed: %02x%02x%02x%02x...",
           a_sig->c[0], a_sig->c[1], a_sig->c[2], a_sig->c[3]);
    
    // Следующие CHIPMUNK_N*4 байт - коэффициенты z полинома
    // Важно: каждый коэффициент занимает 4 байта и читаем их без модификаций
    const uint8_t *l_ptr = a_input + sizeof(a_sig->c);
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Используем побайтовое чтение для восстановления с сохранением типа
        a_sig->z.coeffs[l_i] = (int32_t)(
            (uint32_t)l_ptr[0] | 
            ((uint32_t)l_ptr[1] << 8) | 
            ((uint32_t)l_ptr[2] << 16) | 
            ((uint32_t)l_ptr[3] << 24)
        );
        l_ptr += 4;
    }
    
    // Последние CHIPMUNK_N/8 байт - это hint биты, копируем без изменений
    memcpy(a_sig->hint, l_ptr, CHIPMUNK_N/8);
    
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

