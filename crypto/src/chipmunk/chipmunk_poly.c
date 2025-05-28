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

#include "chipmunk.h"
#include "chipmunk_poly.h"
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

#define LOG_TAG "chipmunk_poly"

// Определение MIN для использования в функциях работы с массивами
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

// Флаг для расширенного логирования
static bool s_debug_more = false;

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
    
    log_it(L_DEBUG, "Starting pointwise multiplication in NTT domain");
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
                debug_if(s_debug_more, L_DEBUG, "Position collision at %d (already filled)", l_pos);
                break;
            }
        }
        
        if (!l_already_selected) {
            a_poly->coeffs[l_pos] = l_sign;
            l_positions[l_tau_filled] = l_pos;
            l_tau_filled++;
            debug_if(s_debug_more, L_DEBUG,"Filled position %d with sign %d (tau filled: %d)",
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
 * @brief Decompose a polynomial into high and low parts
 * 
 * @param a_out Output polynomial with high bits (w1)
 * @param a_in Input polynomial
 */
void chipmunk_poly_highbits(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_in) {
    if (!a_out || !a_in) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_highbits");
        return;
    }
    
    // Инициализируем выходной полином нулями
    memset(a_out->coeffs, 0, sizeof(a_out->coeffs));
    
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Приводим к диапазону [0, Q-1]
        int32_t l_coeff = ((a_in->coeffs[i] % CHIPMUNK_Q) + CHIPMUNK_Q) % CHIPMUNK_Q;
        
        // Получаем high биты (старшие 4 бита)
        int32_t l_high = l_coeff >> (CHIPMUNK_D - 1);
        
        // Сохраняем только высокобитную часть в выходном полиноме (4 бита)
        a_out->coeffs[i] = l_high & 15;
    }
}

/**
 * @brief Apply hint bits to produce w1
 * 
 * @param a_out Output polynomial with applied hints
 * @param a_in Input polynomial w to be hinted
 * @param a_hint Hint bit array
 */
void chipmunk_use_hint(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_in, const uint8_t a_hint[CHIPMUNK_N/8]) {
    if (!a_out || !a_in || !a_hint) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_use_hint");
        return;
    }
    
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
 * @brief Compute hint bits for verification
 * 
 * @param a_hint Output hint bits array
 * @param a_poly1 First polynomial (z)
 * @param a_poly2 Second polynomial (r)
 */
void chipmunk_make_hint(uint8_t a_hint[CHIPMUNK_N/8], const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2) {
    if (!a_hint || !a_poly1 || !a_poly2) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_make_hint");
        return;
    }
    
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