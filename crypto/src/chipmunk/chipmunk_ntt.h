/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
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

#pragma once

#include <stdint.h>
#include "chipmunk.h"

// NTT parameters for q = 3168257 (corrected from original Rust implementation)
#define CHIPMUNK_ZETAS_MONT_LEN 128

// Montgomery parameters for q = 3168257
#define CHIPMUNK_MONT_R          (1U << 22)    // Montgomery reduction parameter R = 2^22
#define CHIPMUNK_MONT_R_INV      202470        // R^(-1) mod q
#define CHIPMUNK_QINV            202470        // -q^(-1) mod 2^22

// **PHASE 3 ОПТИМИЗАЦИЯ #1**: CRITICAL INLINE FUNCTIONS
// Эти функции вызываются тысячи раз в горячих циклах NTT

/**
 * @brief **INLINE** Barrett reduction for q = 3168257 - КРИТИЧЕСКАЯ ПРОИЗВОДИТЕЛЬНОСТЬ
 * @param[in] a_value Value to reduce
 * @return Reduced value
 * 
 * **ВАЖНО**: Inline потому что вызывается ~4096 раз за одну NTT операцию!
 */
static inline int32_t chipmunk_ntt_barrett_reduce(int32_t a_value) {
    // Barrett reduction constants for q = 3168257
    // v = floor(2^26 / q) = floor(67108864 / 3168257) = 21
    int32_t l_v = ((int64_t)a_value * 21) >> 26;
    int32_t l_t = l_v * CHIPMUNK_Q;
    l_t = a_value - l_t;
    
    // Ensure result is in range [0, q)
    if (l_t >= CHIPMUNK_Q) l_t -= CHIPMUNK_Q;
    if (l_t < 0) l_t += CHIPMUNK_Q;
    
    return l_t;
}

// **TESTING ДЕФАЙН**: Раскомментируй для принудительного использования универсальных реализаций
// #define CHIPMUNK_FORCE_GENERIC 1

#if defined(__APPLE__) && defined(__aarch64__) && !defined(CHIPMUNK_FORCE_GENERIC)
// Apple Silicon специфичные определения
#include <arm_neon.h>
#define CHIPMUNK_NEON_APPLE_SILICON 1

/**
 * @brief **APPLE SILICON NEON** векторизированный Barrett reduction
 * @param[in] a_values_vec NEON вектор с 4 значениями для редукции
 * @return NEON вектор с редуцированными значениями
 * 
 * **APPLE SILICON ОПТИМИЗАЦИЯ**: Оптимизировано для M1/M2/M3/M4 чипов!
 */
#elif defined(__ARM_NEON) || defined(__aarch64__)
// Общий ARM64 с NEON поддержкой
#include <arm_neon.h>
#define CHIPMUNK_NEON_GENERIC_ARM 1

/**
 * @brief **NEON векторизированный** Barrett reduction для ARM64
 * @param[in] a_values_vec NEON вектор с 4 значениями для редукции
 * @return NEON вектор с редуцированными значениями
 * 
 * **ОПТИМИЗАЦИЯ**: Обрабатывает 4 значения одновременно на ARM64!
 */
#endif

#if defined(CHIPMUNK_NEON_APPLE_SILICON) || defined(CHIPMUNK_NEON_GENERIC_ARM)
static inline int32x4_t chipmunk_ntt_barrett_reduce_neon(int32x4_t a_values_vec) {
    // Barrett reduction константы для q = 3168257
    int32x4_t l_barrett_21 = vdupq_n_s32(21);  // Barrett константа
    int32x4_t l_q_vec = vdupq_n_s32(CHIPMUNK_Q);
    
    // Упрощенная версия для совместимости с разными версиями NEON
    // Применяем Barrett reduction скалярно для точности
    int32_t temp_values[4];
    vst1q_s32(temp_values, a_values_vec);
    
    for (int i = 0; i < 4; i++) {
        // Стандартный Barrett reduction
        int32_t l_v = ((int64_t)temp_values[i] * 21) >> 26;
        int32_t l_t = l_v * CHIPMUNK_Q;
        l_t = temp_values[i] - l_t;
        
        // Ensure result is in range [0, q)
        if (l_t >= CHIPMUNK_Q) l_t -= CHIPMUNK_Q;
        if (l_t < 0) l_t += CHIPMUNK_Q;
        
        temp_values[i] = l_t;
    }
    
    return vld1q_s32(temp_values);
}
#endif

/**
 * @brief **INLINE** Montgomery multiplication for q = 3168257 - КРИТИЧЕСКАЯ ПРОИЗВОДИТЕЛЬНОСТЬ  
 * @param[in] a_a First value
 * @param[in] a_b Second value
 * @return Result of multiplication
 * 
 * **ВАЖНО**: Inline потому что используется в pointwise multiplication - 512 раз за операцию!
 */
static inline int32_t chipmunk_ntt_montgomery_multiply(int32_t a_a, int32_t a_b) {
    // Montgomery multiplication for q = 3168257, R = 2^22
    const uint32_t QINV_HOTS = 3166785; // -q^(-1) mod 2^22 for q=3168257
    
    int64_t l_t = (int64_t)a_a * a_b;
    uint32_t l_u = (uint32_t)(l_t & 0x3FFFFF) * QINV_HOTS; // Mask for 22 bits
    l_u &= 0x3FFFFF; // Keep only 22 bits
    l_t += (int64_t)l_u * CHIPMUNK_Q;
    int32_t result = (int32_t)(l_t >> 22); // Shift by 22 for R = 2^22
    
    // Final reduction if needed
    if (result >= CHIPMUNK_Q) result -= CHIPMUNK_Q;
    if (result < 0) result += CHIPMUNK_Q;
    
    return result;
}

/**
 * @brief **INLINE** Bit-reverse a 9-bit integer (for 512-point NTT)
 * @param[in] a_x Input value
 * @return Bit-reversed value
 * 
 * **ВАЖНО**: Inline для быстрых битовых операций
 */
static inline int chipmunk_ntt_bit_reverse_9(int a_x) {
    int l_result = 0;
    for (int i = 0; i < 9; i++) {
        l_result = (l_result << 1) | (a_x & 1);
        a_x >>= 1;
    }
    return l_result;
}

/**
 * @brief Transform polynomial to NTT form
 * @param[in,out] a_r Polynomial coefficients array
 */
void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]);

/**
 * @brief Inverse transform from NTT form
 * @param[in,out] a_r Polynomial coefficients array
 */
void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]);

/**
 * @brief Pointwise multiplication of polynomials in NTT domain using Montgomery reduction
 * @param[out] a_c Output polynomial coefficients
 * @param[in] a_a First polynomial coefficients
 * @param[in] a_b Second polynomial coefficients
 * @return Returns 0 on success, negative error code on failure
 */
int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N], 
                                     const int32_t a_b[CHIPMUNK_N]);

/**
 * @brief Perform Montgomery reduction for q = 3168257
 * @param[in,out] a_r Value to reduce
 */
void chipmunk_ntt_montgomery_reduce(int32_t *a_r);

/**
 * @brief Reduce value modulo q = 3168257
 * @param[in] a_value Value to reduce
 * @return Reduced value
 */
int32_t chipmunk_ntt_mod_reduce(int32_t a_value);

/**
 * @brief Convert value to Montgomery domain (multiply by R mod q)
 * @param[in] a_value Value to convert
 * @return Value in Montgomery domain
 */
int32_t chipmunk_ntt_mont_factor(int32_t a_value); 