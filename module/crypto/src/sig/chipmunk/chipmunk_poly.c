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
#include "dap_hash_shake256.h"
#include "dap_common.h"
#include "dap_crypto_common.h"
#include "dap_rand.h"

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

    /*
     * CR-D14 remediation: the previous implementation derived its entropy
     * pool by XOR'ing the input hash with the byte index
     * (extended_hash[i] = hash[i % hash_len] ^ (i + 1)) — a reversible
     * transformation that provides no new entropy, only permutes the hash
     * and introduces a data-dependent exit when MAX_ATTEMPTS was hit
     * (producing fewer-than-ALPHA_H coefficients without signalling an
     * error to the caller). Both issues are fixed here:
     *
     *   state = SHAKE256("CHIPMUNK/poly_challenge/v1" || hash)
     *   pull 2 bytes → pos in [0, 2^16)
     *   reject if pos >= (2^16 - 2^16 % N)
     *   pos %= N
     *   pull 1 byte, sign = (byte & 1) ? +1 : -1
     *   if coeffs[pos] == 0 assign sign, weight++
     *
     * The loop runs until exactly ALPHA_H non-zero coefficients are
     * placed. On inputs that cannot produce enough distinct positions
     * (N too small for ALPHA_H) we return CHIPMUNK_ERROR_INTERNAL
     * instead of silently emitting a truncated challenge.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/poly_challenge/v1";

    memset(c, 0, sizeof(*c));

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));

    const size_t l_in_len = sizeof(k_domain) + hash_len;
    uint8_t *l_in = (uint8_t *)malloc(l_in_len);
    if (!l_in) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    memcpy(l_in, k_domain, sizeof(k_domain));
    memcpy(l_in + sizeof(k_domain), hash, hash_len);
    dap_hash_shake256_absorb(l_state, l_in, l_in_len);
    free(l_in);

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    const uint32_t l_range16 = 1u << 16;
    const uint32_t l_mul_n = (l_range16 / (uint32_t)CHIPMUNK_N) * (uint32_t)CHIPMUNK_N;

    int l_weight_set = 0;
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    while (l_weight_set < CHIPMUNK_ALPHA_H) {
        if (l_sq_pos + 3 > DAP_SHAKE256_RATE) {
            if (l_blocks_squeezed++ >= k_max_blocks) {
                log_it(L_ERROR, "chipmunk_poly_challenge: SHAKE squeeze budget exhausted");
                return CHIPMUNK_ERROR_INTERNAL;
            }
            dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
            l_sq_pos = 0;
        }

        uint32_t l_pos16 = (uint32_t)l_squeeze[l_sq_pos]
                         | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8);
        uint8_t  l_sign_byte = l_squeeze[l_sq_pos + 2];
        l_sq_pos += 3;

        if (l_pos16 >= l_mul_n) {
            continue; // reject-sample to avoid modulo bias
        }
        uint32_t l_pos = l_pos16 % (uint32_t)CHIPMUNK_N;

        if (c->coeffs[l_pos] == 0) {
            c->coeffs[l_pos] = (l_sign_byte & 1u) ? 1 : -1;
            l_weight_set++;
        }
    }

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
    debug_if(s_debug_more, L_INFO, "chipmunk_poly_from_hash: message=%p, len=%zu", a_message, a_message_len);

    if (!a_poly) {
        log_it(L_ERROR, "NULL poly parameter in chipmunk_poly_from_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Allow empty message (len=0) but require non-NULL pointer for non-empty message
    if (a_message_len > 0 && !a_message) {
        log_it(L_ERROR, "NULL message with non-zero length in chipmunk_poly_from_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    /*
     * CR-D5 remediation: replace the 32-bit LCG (coeff a=1664525, c=1013904223)
     * seeded from only 4 bytes of SHA3-256 with a SHAKE256 extendable-output
     * function fed the full message and a domain separator. The ALPHA_H-of-N
     * ternary polynomial is now sampled via unbiased rejection sampling:
     *
     *   state = SHAKE256(DOMAIN_TAG || message)
     *   repeat:
     *     pull 3 bytes → pos in [0, 2^24)
     *     reject if pos >= (2^24 - 2^24 % N)   (avoids modulo bias)
     *     pos %= N
     *     if coeffs[pos] == 0:
     *         pull 1 byte, sign = (byte & 1) ? +1 : -1
     *         coeffs[pos] = sign
     *         weight++
     *
     * No 32-bit cycle, no seed truncation, no unbalanced +1/-1 distribution.
     */

    static const uint8_t k_domain[] = "CHIPMUNK/poly_from_hash/v1";

    memset(a_poly, 0, sizeof(*a_poly));

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));

    // Absorb domain tag and message as one contiguous SHAKE256 input.
    // A single absorb call is sufficient because the rate (136 bytes) is
    // already large enough to swallow the domain tag and the message in
    // one shot.
    const size_t l_in_len = sizeof(k_domain) + a_message_len;
    uint8_t *l_in = (uint8_t *)malloc(l_in_len);
    if (!l_in) {
        log_it(L_ERROR, "chipmunk_poly_from_hash: allocation failed (len=%zu)", l_in_len);
        return CHIPMUNK_ERROR_MEMORY;
    }
    memcpy(l_in, k_domain, sizeof(k_domain));
    if (a_message_len > 0) {
        memcpy(l_in + sizeof(k_domain), a_message, a_message_len);
    }
    dap_hash_shake256_absorb(l_state, l_in, l_in_len);
    free(l_in);

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    // Reject-sample positions in [0, N). 3 bytes → [0, 2^24).
    const uint32_t l_range24 = 1u << 24;
    const uint32_t l_mul_n = (l_range24 / (uint32_t)CHIPMUNK_N) * (uint32_t)CHIPMUNK_N;

    int l_weight_set = 0;
    // CHIPMUNK_ALPHA_H is small (≈37). Even adversarial inputs need only a
    // few hundred bytes of SHAKE output in practice; we cap at 1<<20 rate
    // blocks to fail fast on impossible parameters rather than hang.
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    while (l_weight_set < CHIPMUNK_ALPHA_H) {
        // Refill the SHAKE buffer when we've exhausted it or when we'd
        // need to straddle the end (we consume 4 bytes per trial: 3 for
        // position, 1 for sign).
        if (l_sq_pos + 4 > DAP_SHAKE256_RATE) {
            if (l_blocks_squeezed++ >= k_max_blocks) {
                log_it(L_ERROR, "chipmunk_poly_from_hash: SHAKE squeeze budget exhausted");
                return CHIPMUNK_ERROR_INTERNAL;
            }
            dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
            l_sq_pos = 0;
        }

        uint32_t l_pos24 = (uint32_t)l_squeeze[l_sq_pos]
                         | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8)
                         | ((uint32_t)l_squeeze[l_sq_pos + 2] << 16);
        uint8_t  l_sign_byte = l_squeeze[l_sq_pos + 3];
        l_sq_pos += 4;

        if (l_pos24 >= l_mul_n) {
            continue; // reject to avoid modulo bias
        }
        uint32_t l_pos = l_pos24 % (uint32_t)CHIPMUNK_N;

        if (a_poly->coeffs[l_pos] == 0) {
            a_poly->coeffs[l_pos] = (l_sign_byte & 1u) ? 1 : -1;
            l_weight_set++;
        }
    }

    return CHIPMUNK_ERROR_SUCCESS;
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

    // CR-D16 fix (Round-3): in NTT domain the canonical representation is
    // [0, q), not the centred [-q/2, q/2) form the legacy code produced.
    // The previous double-normalisation (first non-negative, then centred)
    // produced sporadic sign flips that only survived final equality checks
    // by accident, because chipmunk_poly_equal re-lifts both operands via
    // (x % q + q) % q.  Keep add_ntt strictly in [0, q) so intermediate
    // NTT-domain results stay semantically consistent with
    // chipmunk_ntt_pointwise_montgomery / chipmunk_poly_mul_ntt output and
    // with chipmunk_poly_equal's lift convention.
    for (int i = 0; i < CHIPMUNK_N; i++) {
        int64_t l_sum = (int64_t)a_poly1->coeffs[i] + (int64_t)a_poly2->coeffs[i];
        int32_t l_result = (int32_t)(l_sum % CHIPMUNK_Q);
        if (l_result < 0) {
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
    dap_hash_sha2_256(l_derived_seed, a_seed, a_seed_len);
    
    memset(a_poly, 0, sizeof(*a_poly));
    
    // Generate coefficients using deterministic approach
    for (int i = 0; i < CHIPMUNK_N; i++) {
        // Create unique input for each coefficient
        uint8_t l_input[36];  // 32 bytes seed + 4 bytes index
        memcpy(l_input, l_derived_seed, 32);
        memcpy(l_input + 32, &i, sizeof(i));
        
        // Hash to get random value
        uint8_t l_hash[32];
        dap_hash_sha2_256(l_hash, l_input, sizeof(l_input));
        
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
    if (a_bound <= 0) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    /*
     * CR-D5 remediation: replace the 8×32-bit LCG (per-lane state fed from
     * 36 seed bytes, stepped with a=1664525, c=1013904223) by SHAKE256 over
     * a domain-separated seed. Coefficients in [-bound, bound] are drawn
     * via unbiased rejection sampling on 2*bound+1 values:
     *
     *   state = SHAKE256("CHIPMUNK/uniform_mod_p/v1" || seed)
     *   for each i in [0, N):
     *     pull 3 bytes → r in [0, 2^24)
     *     reject if r >= (2^24 - 2^24 % range)
     *     r %= range; coeff = (int32)r - bound
     *
     * No deterministic short period, no bias (the range is small for the
     * HOTS y-polynomial, so the rejection rate is negligible).
     */

    static const uint8_t k_domain[] = "CHIPMUNK/uniform_mod_p/v1";

    memset(a_poly, 0, sizeof(*a_poly));

    uint8_t l_abs_in[sizeof(k_domain) + 36];
    memcpy(l_abs_in, k_domain, sizeof(k_domain));
    memcpy(l_abs_in + sizeof(k_domain), a_seed, 36);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake256_absorb(l_state, l_abs_in, sizeof(l_abs_in));

    uint8_t l_squeeze[DAP_SHAKE256_RATE];
    size_t l_sq_pos = DAP_SHAKE256_RATE;

    const uint32_t l_range24 = 1u << 24;
    const uint32_t l_range = (uint32_t)(2 * a_bound + 1);
    if (l_range == 0 || l_range > l_range24) {
        log_it(L_ERROR, "chipmunk_poly_uniform_mod_p: bound %d out of supported range", a_bound);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    const uint32_t l_mul = (l_range24 / l_range) * l_range;

    // Bound on SHAKE squeeze blocks for defence-in-depth; realistic worst
    // case for the Chipmunk bounds (≤ a few thousand) is well below 2^20.
    const size_t k_max_blocks = 1u << 20;
    size_t l_blocks_squeezed = 0;

    for (int i = 0; i < CHIPMUNK_N; i++) {
        uint32_t l_val;
        for (;;) {
            if (l_sq_pos + 3 > DAP_SHAKE256_RATE) {
                if (l_blocks_squeezed++ >= k_max_blocks) {
                    log_it(L_ERROR, "chipmunk_poly_uniform_mod_p: SHAKE squeeze budget exhausted");
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake256_squeezeblocks(l_squeeze, 1, l_state);
                l_sq_pos = 0;
            }
            l_val = (uint32_t)l_squeeze[l_sq_pos]
                  | ((uint32_t)l_squeeze[l_sq_pos + 1] << 8)
                  | ((uint32_t)l_squeeze[l_sq_pos + 2] << 16);
            l_sq_pos += 3;
            if (l_val < l_mul) {
                break;
            }
        }
        a_poly->coeffs[i] = (int32_t)(l_val % l_range) - a_bound;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}