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

#include "chipmunk_ntt.h"
#include <string.h>
#include <inttypes.h>
#include "dap_common.h"

#define LOG_TAG "chipmunk_ntt"

// NTT algorithm constants
#define MONT 4193792
#define QINV 4236238847

// Pre-computed table of values for NTT
const int32_t g_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN] = {
    1479, 2937, 1799, 618, 32, 1855, 2882, 875, 4011, 372,
    /* ... full table omitted for brevity ... */
    3760, 2754, 2106, 2440, 2348, 797, 2568, 2124, 1436,
    2472, 2519, 66, 3911, 1449, 3866, 1183, 3368, 1409
};

/**
 * @brief Barrett reduction implementation
 */
int32_t chipmunk_ntt_barrett_reduce(int32_t a_value) {
    int32_t l_v = ((int64_t)a_value * 5) >> 26;
    int32_t l_t = l_v * CHIPMUNK_Q;
    l_t = a_value - l_t;
    
    return l_t;
}

/**
 * @brief Modulo q reduction
 */
int32_t chipmunk_ntt_mod_reduce(int32_t a_value) {
    int32_t l_t = a_value % CHIPMUNK_Q;
    if (l_t < 0) 
        l_t += CHIPMUNK_Q;
    return l_t;
}

/**
 * @brief Montgomery reduction
 */
void chipmunk_ntt_montgomery_reduce(int32_t *a_r) {
    int64_t l_a = *a_r;
    int32_t l_u = (int32_t)(l_a * QINV);
    l_u &= 0xFFFFFFFF;
    l_u *= CHIPMUNK_Q;
    l_a += l_u;
    *a_r = (int32_t)(l_a >> 32);
}

/**
 * @brief Montgomery multiplication with reduction
 */
int32_t chipmunk_ntt_montgomery_multiply(int32_t a_a, int32_t a_b) {
    int64_t l_t = (int64_t)a_a * a_b;
    int32_t l_u = (int32_t)(l_t * QINV);
    l_u &= 0xFFFFFFFF;
    l_u *= CHIPMUNK_Q;
    l_t += l_u;
    return (int32_t)(l_t >> 32);
}

/**
 * @brief Multiply constant by 2^32 mod q
 */
int32_t chipmunk_ntt_mont_factor(int32_t a_value) {
    return chipmunk_ntt_montgomery_multiply(a_value, MONT);
}

/**
 * @brief NTT butterfly operation
 */
static void s_butterfly(int32_t *a_a, int32_t *a_b, int32_t a_zeta) {
    int32_t l_t = chipmunk_ntt_montgomery_multiply(*a_b, a_zeta);
    *a_b = *a_a - l_t;
    if (*a_b < 0) 
        *a_b += CHIPMUNK_Q;
    *a_a = *a_a + l_t;
    if (*a_a >= CHIPMUNK_Q) 
        *a_a -= CHIPMUNK_Q;
}

/**
 * @brief Inverse NTT butterfly operation
 */
static void s_butterfly_inv(int32_t *a_a, int32_t *a_b, int32_t a_zeta) {
    int32_t l_t = *a_a;
    *a_a = l_t + *a_b;
    if (*a_a >= CHIPMUNK_Q) 
        *a_a -= CHIPMUNK_Q;
    *a_b = l_t - *a_b;
    if (*a_b < 0) 
        *a_b += CHIPMUNK_Q;
    *a_b = chipmunk_ntt_montgomery_multiply(*a_b, a_zeta);
}

/**
 * @brief Transform polynomial to NTT form
 */
void chipmunk_ntt(int32_t a_r[CHIPMUNK_N]) {
    int l_k = 1;
    for (int l_len = 512; l_len > 0; l_len >>= 1) {
        int l_j;
        for (int l_start = 0; l_start < CHIPMUNK_N; l_start = l_j + l_len) {
            int32_t l_zeta = g_zetas_mont[l_k++];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = chipmunk_ntt_montgomery_multiply(a_r[l_j + l_len], l_zeta);
                a_r[l_j + l_len] = a_r[l_j] - l_t;
                if (a_r[l_j + l_len] < 0) 
                    a_r[l_j + l_len] += CHIPMUNK_Q;
                a_r[l_j] = a_r[l_j] + l_t;
                if (a_r[l_j] >= CHIPMUNK_Q) 
                    a_r[l_j] -= CHIPMUNK_Q;
            }
        }
    }
}

/**
 * @brief Inverse transform from NTT form
 */
void chipmunk_invntt(int32_t a_r[CHIPMUNK_N]) {
    int l_k = CHIPMUNK_ZETAS_MONT_LEN - 1;
    for (int l_len = 1; l_len < CHIPMUNK_N; l_len <<= 1) {
        int l_j;
        for (int l_start = 0; l_start < CHIPMUNK_N; l_start = l_j + l_len) {
            int32_t l_zeta = g_zetas_mont[l_k--];
            for (l_j = l_start; l_j < l_start + l_len; l_j++) {
                int32_t l_t = a_r[l_j];
                a_r[l_j] = l_t + a_r[l_j + l_len];
                if (a_r[l_j] >= CHIPMUNK_Q) 
                    a_r[l_j] -= CHIPMUNK_Q;
                a_r[l_j + l_len] = l_t - a_r[l_j + l_len];
                if (a_r[l_j + l_len] < 0) 
                    a_r[l_j + l_len] += CHIPMUNK_Q;
                a_r[l_j + l_len] = chipmunk_ntt_montgomery_multiply(a_r[l_j + l_len], l_zeta);
            }
        }
    }
    
    // Multiply by n^(-1) in the ring
    for (int l_j = 0; l_j < CHIPMUNK_N; l_j++) {
        a_r[l_j] = chipmunk_ntt_montgomery_multiply(a_r[l_j], g_zetas_mont[0]);
    }
}

/**
 * @brief Pointwise multiplication in NTT form
 */
int chipmunk_ntt_pointwise_montgomery(int32_t a_c[CHIPMUNK_N],
                                     const int32_t a_a[CHIPMUNK_N], 
                                     const int32_t a_b[CHIPMUNK_N]) {
    if (!a_c || !a_a || !a_b) {
        log_it(L_ERROR, "NULL pointer in chipmunk_ntt_pointwise_montgomery");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    log_it(L_DEBUG, "Starting pointwise multiplication in NTT domain");
    
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Используем int64_t для безопасного умножения без переполнения
        int64_t l_t = (int64_t)a_a[l_i] * (int64_t)a_b[l_i];
        
        // Если произошло переполнение (значение больше максимального int32)
        if (l_t > INT32_MAX || l_t < INT32_MIN) {
            //log_it(L_DEBUG, "Large product detected at index %d: %lld", l_i, l_t);
            
            // Применяем модульную редукцию - вычисляем остаток по модулю CHIPMUNK_Q
            int64_t l_mod = l_t % CHIPMUNK_Q;
            
            // Обрабатываем отрицательный остаток
            if (l_mod < 0) {
                l_mod += CHIPMUNK_Q;
            }
            
            a_c[l_i] = (int32_t)l_mod;
            //log_it(L_DEBUG, "Normalized result to: %d", a_c[l_i]);
        } else {
            // Для небольших значений используем обычную редукцию Монтгомери
            int32_t l_temp = (int32_t)l_t;
            chipmunk_ntt_montgomery_reduce(&l_temp);
            a_c[l_i] = l_temp;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
} 
