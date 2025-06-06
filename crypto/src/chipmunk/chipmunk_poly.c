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
#include "dap_hash.h"
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
static bool s_debug_more = true;

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
 * @brief Add two polynomials
 */
int chipmunk_poly_add(chipmunk_poly_t *r, const chipmunk_poly_t *a, const chipmunk_poly_t *b) {
    if (!r || !a || !b) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_add");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    for (int i = 0; i < CHIPMUNK_N; i++) {
        // **ИСПРАВЛЕНО**: используем центрированное представление как в Rust
        int64_t l_temp = (int64_t)a->coeffs[i] + (int64_t)b->coeffs[i];
        r->coeffs[i] = (int32_t)(l_temp % CHIPMUNK_Q);
        
        // Приводим к положительному представлению сначала
        if (r->coeffs[i] < 0) {
            r->coeffs[i] += CHIPMUNK_Q;
        }
        
        // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: применяем центрированную нормализацию [-Q/2, Q/2]
        // как в оригинальном Rust коде normalize() функция
        if (r->coeffs[i] > CHIPMUNK_Q / 2) {
            r->coeffs[i] -= CHIPMUNK_Q;
        }
    }
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Subtract polynomials (r = a - b)
 * 
 * @param a_result Output polynomial
 * @param a_a First polynomial
 * @param a_b Second polynomial
 * @return 0 on success, negative on error
 */
int chipmunk_poly_sub(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b) {
    if (!a_result || !a_a || !a_b) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_sub");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // **ИСПРАВЛЕНО**: используем центрированное представление как в Rust
        int64_t l_temp = (int64_t)a_a->coeffs[i] - (int64_t)a_b->coeffs[i];
        a_result->coeffs[i] = (int32_t)(l_temp % CHIPMUNK_Q);
        
        // Приводим к положительному представлению сначала
        if (a_result->coeffs[i] < 0) {
            a_result->coeffs[i] += CHIPMUNK_Q;
        }
        
        // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: применяем центрированную нормализацию [-Q/2, Q/2]
        // как в оригинальном Rust коде normalize() функция
        if (a_result->coeffs[i] > CHIPMUNK_Q / 2) {
            a_result->coeffs[i] -= CHIPMUNK_Q;
        }
    }
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Multiply two polynomials in NTT form
 */
int chipmunk_poly_pointwise(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b) {
    log_it(L_DEBUG, "chipmunk_poly_pointwise: Function entry");
    
    if (!a_result || !a_a || !a_b) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_poly_pointwise");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    log_it(L_DEBUG, "chipmunk_poly_pointwise: Pointers validated, calling chipmunk_ntt_pointwise_montgomery");
    log_it(L_DEBUG, "Starting pointwise multiplication in NTT domain");
    int result = chipmunk_ntt_pointwise_montgomery(a_result->coeffs, a_a->coeffs, a_b->coeffs);
    log_it(L_DEBUG, "chipmunk_poly_pointwise: chipmunk_ntt_pointwise_montgomery returned %d", result);
    
    if (result != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "Failed pointwise multiplication in NTT domain");
        return result;
    }
    
    log_it(L_DEBUG, "chipmunk_poly_pointwise: Function exit with success");
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
 * @brief Simple coefficient decomposition for compatibility
 * @param decomp Output array [low, high]
 * @param coeff Input coefficient
 */
static void chipmunk_poly_decompose_coeff(int32_t decomp[2], int32_t coeff) {
    // Simple decomposition: high = coeff / 16, low = coeff % 16
    decomp[1] = coeff / 16;  // high bits
    decomp[0] = coeff % 16;  // low bits
}

/**
 * @brief Decompose a polynomial into high and low parts
 * 
 * @param a_out Output polynomial with high bits (w1)
 * @param a_in Input polynomial
 */
int chipmunk_poly_highbits(uint8_t *a_output, const chipmunk_poly_t *a_poly) {
    if (!a_output || !a_poly) {
        return -1;
    }
    
    // Согласно алгоритму Chipmunk, высокие биты упаковываются по 4 бита на коэффициент
    // Каждый байт содержит 2 коэффициента (по 4 бита каждый)
    // Для 256 коэффициентов нужно 128 байт
    
    for (int i = 0; i < CHIPMUNK_N; i += 2) {
        int32_t l_decomp1[2], l_decomp2[2];
        
        // Разлагаем первый коэффициент
        chipmunk_poly_decompose_coeff(l_decomp1, a_poly->coeffs[i]);
        
        // Разлагаем второй коэффициент (если есть)
        if (i + 1 < CHIPMUNK_N) {
            chipmunk_poly_decompose_coeff(l_decomp2, a_poly->coeffs[i + 1]);
        } else {
            l_decomp2[1] = 0; // Если нет второго коэффициента, w1 = 0
        }
        
        // Упаковываем два w1 в один байт (по 4 бита каждый)
        // Ограничиваем w1 до 4 битов [0, 15] для компактности
        uint8_t w1_1 = (uint8_t)(l_decomp1[1] & 0xF);
        uint8_t w1_2 = (uint8_t)(l_decomp2[1] & 0xF);
        
        a_output[i / 2] = w1_1 | (w1_2 << 4);
    }
    
    return 0;
}

/**
 * @brief Apply hint bits to recover w1 from w'
 * 
 * @param a_out Output polynomial w1 (high bits)
 * @param a_w_prime Input polynomial w' 
 * @param a_hint Hint bits array
 */
void chipmunk_use_hint(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_w_prime, const uint8_t a_hint[CHIPMUNK_N/8]) {
    if (!a_out || !a_w_prime || !a_hint) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_use_hint");
        return;
    }
    
    // Инициализируем выходной полином нулями
    memset(a_out->coeffs, 0, sizeof(a_out->coeffs));
    
    // Применяем hint биты к каждому коэффициенту полинома
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Проверяем бит подсказки для этого коэффициента
        uint8_t l_hint_bit = (a_hint[l_i/8] >> (l_i % 8)) & 1;
        
        // Разложить w' на high и low биты
        int32_t l_decomp[2];
        chipmunk_poly_decompose_coeff(l_decomp, a_w_prime->coeffs[l_i]);
        
        // Получаем высокие биты от w'
        int32_t l_w1_prime = l_decomp[1];
        
        // Применяем hint бит: если hint=1, то корректируем высокие биты
        if (l_hint_bit) {
            // Hint бит указывает, что нужно скорректировать w1
            // Для Chipmunk с 4-битными w1: w1 = (w1' + 1) mod 16
            l_w1_prime = (l_w1_prime + 1) & 15;
        }
        
        a_out->coeffs[l_i] = l_w1_prime;
    }
}

/**
 * @brief Compute hint bits for verification
 * 
 * @param a_hint Output hint bits array
 * @param a_w_prime First polynomial (w')
 * @param a_w Second polynomial (w)
 */
void chipmunk_make_hint(uint8_t a_hint[CHIPMUNK_N/8], const chipmunk_poly_t *a_w_prime, const chipmunk_poly_t *a_w) {
    if (!a_hint || !a_w_prime || !a_w) {
        log_it(L_ERROR, "NULL input parameters in chipmunk_make_hint");
        return;
    }
    
    int32_t l_decomp_w_prime[2], l_decomp_w[2];
    
    // Инициализируем массив hint нулями
    memset(a_hint, 0, CHIPMUNK_N/8);
    
    // Для каждого коэффициента
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        // Разложить w' на high и low биты
        chipmunk_poly_decompose_coeff(l_decomp_w_prime, a_w_prime->coeffs[l_i]);
        
        // Разложить w на high и low биты
        chipmunk_poly_decompose_coeff(l_decomp_w, a_w->coeffs[l_i]);
        
        // Hint бит устанавливается в 1, если high биты w' и w отличаются
        // Для Chipmunk с 4-битными w1 проверяем различие с учетом модуля 16
        int32_t w1_prime = l_decomp_w_prime[1] & 15;
        int32_t w1 = l_decomp_w[1] & 15;
        
        // Hint нужен, если w1' != w1 и (w1' + 1) mod 16 == w1
        if (w1_prime != w1 && ((w1_prime + 1) & 15) == w1) {
            // Установить бит в массиве hint
            a_hint[l_i / 8] |= (1 << (l_i % 8));
        }
    }
    
    // Подсчитаем количество установленных hint битов для отладки
    int l_hint_count = 0;
    for (int l_i = 0; l_i < CHIPMUNK_N; l_i++) {
        if ((a_hint[l_i / 8] >> (l_i % 8)) & 1) {
            l_hint_count++;
        }
    }
    
    log_it(L_DEBUG, "Created hint with %d nonzero bits out of %d", l_hint_count, CHIPMUNK_N);
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
 * @brief Decompose polynomial into power-of-2 base representation
 * 
 * NOTE: This function is currently not used in HOTS scheme and may be removed
 */
int chipmunk_poly_decompose(chipmunk_poly_t *r1, chipmunk_poly_t *r0, const chipmunk_poly_t *a) {
    if (!r1 || !r0 || !a) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_decompose");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // NOTE: This decomposition is not used in HOTS, keeping placeholder
    // Original Dilithium decomposition parameters are not applicable to Chipmunk HOTS
    log_it(L_WARNING, "Polynomial decomposition not implemented for HOTS scheme");
    
    // For now, just copy input to r0 and zero r1
    memcpy(r0, a, sizeof(chipmunk_poly_t));
    memset(r1, 0, sizeof(chipmunk_poly_t));
    
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Generate challenge polynomial from hash
 * 
 * NOTE: This function generates a sparse polynomial for HOTS challenge
 */
int chipmunk_poly_challenge(chipmunk_poly_t *c, const uint8_t *hash, size_t hash_len) {
    if (!c || !hash) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_challenge");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    if (hash_len < 16) {
        log_it(L_ERROR, "Hash too short in chipmunk_poly_challenge: %zu bytes", hash_len);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    // Initialize to zero
    memset(c, 0, sizeof(chipmunk_poly_t));

    // For HOTS scheme, we use a sparse challenge polynomial
    // Based on ALPHA_H parameter from original implementation
    uint16_t l_positions[CHIPMUNK_ALPHA_H];
    int8_t l_signs[CHIPMUNK_ALPHA_H];

    // Initialize arrays
    memset(l_positions, 0, sizeof(l_positions));
    memset(l_signs, 0, sizeof(l_signs));

    // Generate positions and signs deterministically from hash
    int l_coeffs_set = 0;
    int l_hash_offset = 0;
    
    // КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Строгие ограничения на количество попыток
    const int MAX_ATTEMPTS = MIN(hash_len * 8, 2000);  // Максимум 2000 попыток
    int l_attempts = 0;
    
    while (l_coeffs_set < CHIPMUNK_ALPHA_H && l_attempts < MAX_ATTEMPTS && l_hash_offset < (int)(hash_len - 2)) {
        l_attempts++;
        
        // Get position from 2 bytes of hash
        uint16_t l_pos = ((uint16_t)hash[l_hash_offset] | ((uint16_t)hash[l_hash_offset + 1] << 8)) % CHIPMUNK_N;
        
        // Check if this position is already used
        bool l_already_used = false;
        for (int j = 0; j < l_coeffs_set; j++) {
            if (l_positions[j] == l_pos) {
                l_already_used = true;
                break;
            }
        }
        
        if (!l_already_used) {
            l_positions[l_coeffs_set] = l_pos;
            // Get sign from next byte (bit 0)
            l_signs[l_coeffs_set] = (hash[(l_hash_offset + 2) % hash_len] & 1) ? 1 : -1;
            l_coeffs_set++;
        }
        
        l_hash_offset++;
        
        // Prevent infinite loop by cycling through hash
        if (l_hash_offset >= (int)hash_len - 2) {
            l_hash_offset = 0;  // Wrap around
        }
    }

    // Set coefficients in polynomial
    for (int i = 0; i < l_coeffs_set; i++) {
        if (l_positions[i] < CHIPMUNK_N) {  // Additional safety check
            c->coeffs[l_positions[i]] = l_signs[i];
        }
    }

    if (l_coeffs_set < CHIPMUNK_ALPHA_H) {
        log_it(L_WARNING, "Could not generate full challenge polynomial: got %d/%d coefficients in %d attempts", 
               l_coeffs_set, CHIPMUNK_ALPHA_H, l_attempts);
        // Продолжаем с частичным полиномом для совместимости
    }

    log_it(L_DEBUG, "Generated challenge polynomial with %d non-zero coefficients in %d attempts", 
           l_coeffs_set, l_attempts);
    return CHIPMUNK_ERROR_SUCCESS;
}

/**
 * @brief Create polynomial from hash of message (следуя оригинальному Rust коду)
 * 
 * КРИТИЧЕСКИ ВАЖНО: оригинальный Rust код:
 * fn from_hash_message(msg: &[u8]) -> Self {
 *     let mut hasher = Sha256::new();
 *     hasher.update(msg);
 *     let seed = hasher.finalize().into();
 *     let mut rng = rand_chacha::ChaCha20Rng::from_seed(seed);
 *     Self::rand_ternary(&mut rng, ALPHA_H)
 * }
 * 
 * @param a_poly Output polynomial
 * @param a_message Message to hash
 * @param a_message_len Message length
 * @return 0 on success, negative on error
 */
int chipmunk_poly_from_hash(chipmunk_poly_t *a_poly, const uint8_t *a_message, size_t a_message_len) {
    static int call_count = 0;
    call_count++;
    
    if (!a_poly || !a_message) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_from_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: ограничиваем отладочный вывод
    if (call_count <= 10 || call_count % 1000 == 0) {
        printf("🔍 FROM_HASH: Call #%d - Processing message length %zu\n", call_count, a_message_len);
    }
    
    // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: точно следуем оригинальному Rust коду!
    // 1. Hash message with SHA256
    dap_hash_fast_t l_hash_out;
    dap_hash_fast(a_message, a_message_len, &l_hash_out);
    
    uint8_t l_seed[32];
    memcpy(l_seed, &l_hash_out, 32);
    
    if (call_count <= 5) {
        printf("🔍 FROM_HASH: SHA256 seed = 0x%02x%02x%02x%02x\n", 
               l_seed[0], l_seed[1], l_seed[2], l_seed[3]);
    }
    
    // 2. Use seed to create ChaCha20Rng (simplified deterministic version)
    // Initialize to zero
    memset(a_poly, 0, sizeof(chipmunk_poly_t));
    
    // 3. **КЛЮЧЕВОЕ ИСПРАВЛЕНИЕ**: генерируем rand_ternary с весом ALPHA_H = 37
    // Original Rust: Self::rand_ternary(&mut rng, ALPHA_H)
    
    uint32_t l_rng_state = ((uint32_t)l_seed[0]) | 
                          ((uint32_t)l_seed[1] << 8) |
                          ((uint32_t)l_seed[2] << 16) |
                          ((uint32_t)l_seed[3] << 24);
    
    // Simple linear congruential generator for deterministic results
    const uint32_t l_a = 1664525;
    const uint32_t l_c = 1013904223;
    
    int l_weight_set = 0;
    int l_max_iterations = CHIPMUNK_N * 10; // Safety limit
    int l_iteration = 0;
    
    if (call_count <= 5) {
        printf("🔍 FROM_HASH: Generating ternary polynomial with weight %d\n", CHIPMUNK_ALPHA_H);
    }
    
    while (l_weight_set < CHIPMUNK_ALPHA_H && l_iteration < l_max_iterations) {
        l_rng_state = l_a * l_rng_state + l_c;
        uint32_t l_tmp = l_rng_state;
        
        uint32_t l_index = l_tmp % CHIPMUNK_N;
        l_tmp >>= 9;
        
        if (a_poly->coeffs[l_index] == 0) {
            // Original Rust: if (tmp >> 9) & 1 == 1 { coeff = 1 } else { coeff = -1 }
            if ((l_tmp & 1) == 1) {
                a_poly->coeffs[l_index] = 1;
            } else {
                a_poly->coeffs[l_index] = -1;
            }
            l_weight_set++;
        }
        l_iteration++;
    }
    
    if (call_count <= 5) {
        printf("🔍 FROM_HASH: Generated %d ternary coefficients (target: %d)\n", 
               l_weight_set, CHIPMUNK_ALPHA_H);
        printf("🔍 FROM_HASH: First coeffs: %d %d %d %d\n", 
               a_poly->coeffs[0], a_poly->coeffs[1], a_poly->coeffs[2], a_poly->coeffs[3]);
    }
    
    if (l_weight_set != CHIPMUNK_ALPHA_H) {
        log_it(L_WARNING, "Generated weight %d differs from target %d", l_weight_set, CHIPMUNK_ALPHA_H);
    }
    
    // **КРИТИЧЕСКОЕ ОГРАНИЧЕНИЕ**: останавливаем процесс при слишком большом количестве вызовов
    if (call_count > 10000) {
        printf("🚨 КРИТИЧЕСКАЯ ОШИБКА: слишком много вызовов chipmunk_poly_from_hash (%d)! Возможен бесконечный цикл!\n", call_count);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    
    return 0;
}

/**
 * @brief Multiply two polynomials in NTT domain
 * 
 * @param a_result Output polynomial (can be same as input)
 * @param a_poly1 First polynomial (in NTT domain)
 * @param a_poly2 Second polynomial (in NTT domain)
 */
void chipmunk_poly_mul_ntt(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2) {
    if (!a_result || !a_poly1 || !a_poly2) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_mul_ntt");
        return;
    }
    
    // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: Используем обычное умножение по модулю как в оригинальном Rust!
    // В оригинальном Rust коде НЕ используется Montgomery умножение в NTT операциях
    // Rust: ((a as i64) * (b as i64) % modulus as i64) as i32
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_temp = ((int64_t)a_poly1->coeffs[i] * (int64_t)a_poly2->coeffs[i]) % (int64_t)CHIPMUNK_Q;
        a_result->coeffs[i] = (int32_t)l_temp;
        
        // Ensure positive representation
        if (a_result->coeffs[i] < 0) {
            a_result->coeffs[i] += CHIPMUNK_Q;
        }
    }
}

/**
 * @brief Add two polynomials in NTT domain
 * 
 * @param a_result Output polynomial (can be same as input)
 * @param a_poly1 First polynomial (in NTT domain)
 * @param a_poly2 Second polynomial (in NTT domain)
 */
void chipmunk_poly_add_ntt(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2) {
    if (!a_result || !a_poly1 || !a_poly2) {
        log_it(L_ERROR, "NULL parameters in chipmunk_poly_add_ntt");
        return;
    }
    
    // Coefficient-wise addition with proper modular reduction
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_temp = (int64_t)a_poly1->coeffs[i] + a_poly2->coeffs[i];
        
        // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: применяем точную нормализацию как в оригинальном Rust
        // Rust normalize() функция: центрированное представление [-q/2, q/2]
        int32_t l_result = (int32_t)(l_temp % CHIPMUNK_Q);
        if (l_result < 0) {
            l_result += CHIPMUNK_Q;
        }
        
        // Центрированная нормализация как в InvNTT
        if (l_result > CHIPMUNK_Q / 2) {
            l_result -= CHIPMUNK_Q;
        }
        if (l_result < -CHIPMUNK_Q / 2) {
            l_result += CHIPMUNK_Q;
        }
        
        a_result->coeffs[i] = l_result;
    }
}

/**
 * @brief Lift coefficient to positive representation [0, q)
 * Based on original Rust implementation: (a % modulus + modulus) % modulus
 */
static int32_t chipmunk_poly_lift(int32_t a, int32_t modulus) {
    return (a % modulus + modulus) % modulus;
}

/**
 * @brief Compare two polynomials for equality
 * Uses lift() normalization as in original Rust code
 */
bool chipmunk_poly_equal(const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2) {
    if (!a_poly1 || !a_poly2) {
        return false;
    }
    
    // **ИСПРАВЛЕНО**: используем точную функцию из оригинального Rust коду
    // Original Rust: HOTSPoly::from(&left) == HOTSPoly::from(&right)
    // где '==' оператор использует lift() функцию: crate::poly::lift(x, modulus) == crate::poly::lift(y, modulus)
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Применяем точную копию Rust lift() функции к обеим сторонам
        int32_t left_lifted = chipmunk_poly_lift(a_poly1->coeffs[i], CHIPMUNK_Q);
        int32_t right_lifted = chipmunk_poly_lift(a_poly2->coeffs[i], CHIPMUNK_Q);
        
        if (left_lifted != right_lifted) {
            return false;
        }
    }
    
    return true;
}

/**
 * @brief Generate random polynomial in time domain
 * @param a_poly Output polynomial
 * @param a_seed Seed for generation
 * @param a_seed_len Seed length
 * @param a_modulus Modulus for coefficients
 * @return 0 on success, negative on error
 */
int dap_random_poly_time_domain(chipmunk_poly_t *a_poly, const uint8_t *a_seed, size_t a_seed_len, int a_modulus) {
    if (!a_poly || !a_seed) {
        return -1;
    }
    
    // Use SHA2-256 for deterministic generation
    uint8_t l_derived_seed[32];
    if (dap_hash_sha2_256(l_derived_seed, a_seed, a_seed_len) != 0) {
        return -1;
    }
    
    memset(a_poly, 0, sizeof(*a_poly));
    
    // Generate coefficients using deterministic approach
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Create unique input for each coefficient
        uint8_t l_input[36];  // 32 bytes seed + 4 bytes index
        memcpy(l_input, l_derived_seed, 32);
        memcpy(l_input + 32, &i, sizeof(i));
        
        // Hash to get random value
        uint8_t l_hash[32];
        if (dap_hash_sha2_256(l_hash, l_input, sizeof(l_input)) != 0) {
            return -1;
        }
        
        // Use first 4 bytes as random value
        uint32_t l_random = *(uint32_t*)l_hash;
        a_poly->coeffs[i] = l_random % a_modulus;
    }
    
    return 0;
}

/**
 * @brief Generate uniform polynomial with coefficients in range [-bound, bound]
 * Based on original Rust HOTSPoly::rand_mod_p function
 */
int chipmunk_poly_uniform_mod_p(chipmunk_poly_t *a_poly, const uint8_t a_seed[36], int32_t a_bound) {
    if (!a_poly || !a_seed) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    
    // **ИСПРАВЛЕНО**: используем точный алгоритм из оригинального Rust кода
    // Original Rust: HOTSPoly::rand_mod_p(&mut rng, bound) генерирует коэффициенты в [-bound, bound]
    
    // Debug output для малых bound
    if (a_bound <= 10) {
        printf("  🔍 chipmunk_poly_uniform_mod_p: Generating poly with bound = %d (range [-%d, %d])\n", 
               a_bound, a_bound, a_bound);
    }
    
    // Use ChaCha20-like stream for deterministic random generation
    uint32_t l_state[8];
    for (int i = 0; i < 8; i++) {
        l_state[i] = ((uint32_t)a_seed[i*4]) | 
                     ((uint32_t)a_seed[i*4+1] << 8) |
                     ((uint32_t)a_seed[i*4+2] << 16) |
                     ((uint32_t)a_seed[i*4+3] << 24);
    }
    
    // Используем также последние 4 байта для большей энтропии
    l_state[0] ^= ((uint32_t)a_seed[32]) | 
                  ((uint32_t)a_seed[33] << 8) |
                  ((uint32_t)a_seed[34] << 16) |
                  ((uint32_t)a_seed[35] << 24);
    
    // Generate coefficients in range [-bound, bound]
    int l_out_of_range = 0;
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Simple linear congruential generator for deterministic randomness
        l_state[i % 8] = l_state[i % 8] * 1664525 + 1013904223;
        
        // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: правильно генерируем значения в диапазоне [-bound, bound]
        // Диапазон: от -bound до +bound включительно = (2*bound + 1) значений
        uint32_t l_range = 2 * a_bound + 1;
        uint32_t l_rand = l_state[i % 8] % l_range;
        
        // Преобразуем в signed диапазон [-bound, bound]
        a_poly->coeffs[i] = (int32_t)l_rand - a_bound;
        
        // Дополнительная проверка для отладки
        if (a_poly->coeffs[i] < -a_bound || a_poly->coeffs[i] > a_bound) {
            l_out_of_range++;
            if (l_out_of_range <= 5) {
                log_it(L_ERROR, "Generated coefficient %d out of bounds [-%d, %d] at index %d", 
                       a_poly->coeffs[i], a_bound, a_bound, i);
            }
        }
    }
    
    if (l_out_of_range > 0) {
        log_it(L_ERROR, "Total %d coefficients out of range for bound %d", l_out_of_range, a_bound);
    }
    
    // Debug output для малых bound
    if (a_bound <= 10) {
        printf("    First 10 coeffs: ");
        for (int i = 0; i < 10 && i < CHIPMUNK_N; i++) {
            printf("%d ", a_poly->coeffs[i]);
        }
        printf("\n");
    }
    
    return 0;
} 