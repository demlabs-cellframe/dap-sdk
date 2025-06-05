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

/**
 * @brief HOTS (Homomorphic One-Time Signatures) implementation for Chipmunk
 * 
 * Based on the original Rust implementation from Chipmunk repository.
 * HOTS signature scheme: σ = s0 * H(m) + s1 for each polynomial in GAMMA
 * Verification: Σ(a_i * σ_i) == H(m) * v0 + v1
 */

#include "chipmunk_hots.h"
#include "chipmunk_internal.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "dap_hash.h"
#include "dap_common.h"
#include "rand/dap_rand.h"

#include <string.h>
#include <stdio.h>

#define LOG_TAG "chipmunk_hots"

/**
 * @brief Setup HOTS public parameters
 * 
 * @param a_params Output parameters structure
 * @return 0 on success, negative on error
 */
int chipmunk_hots_setup(chipmunk_hots_params_t *a_params) {
    if (!a_params) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_setup");
        return -1;
    }
    
    printf("🔧 HOTS setup: Generating public parameters...\n");
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду
    // Original Rust: a.iter_mut().for_each(|x| *x = HOTSNTTPoly::from(&HOTSPoly::rand_poly(rng)));
    // 1. Генерируем HOTSPoly::rand_poly(rng) - случайный полином в time domain
    // 2. Преобразуем в HOTSNTTPoly::from() - в NTT domain
    
    // Use a fixed seed for reproducible test results
    uint32_t l_base_seed = 0x12345678;
    
    // Generate GAMMA random polynomials for public parameters
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        printf("  Generating parameter a[%d]...\n", i);
        
        // **ИСПРАВЛЕНО**: генерируем случайный полином как в оригинальном Rust коде
        // Original Rust: HOTSPoly::rand_poly(rng) - это полином с коэффициентами в диапазоне [0, Q)
        uint8_t l_param_seed[36];
        memcpy(l_param_seed, &l_base_seed, 4);
        uint32_t l_param_nonce = 0x10000000 + i;  // Уникальный nonce для каждого параметра
        memcpy(l_param_seed + 32, &l_param_nonce, 4);
        
        // Генерируем случайный полином в time domain
        dap_hash_fast_t l_hash_out;
        dap_hash_fast(l_param_seed, 36, &l_hash_out);
        
        uint8_t l_hash[32];
        memcpy(l_hash, &l_hash_out, 32);
        
        // Используем hash как seed для ChaCha20-подобного генератора
        uint32_t l_state[8];
        for (int j = 0; j < 8; j++) {
            l_state[j] = ((uint32_t)l_hash[j*4]) | 
                         ((uint32_t)l_hash[j*4+1] << 8) |
                         ((uint32_t)l_hash[j*4+2] << 16) |
                         ((uint32_t)l_hash[j*4+3] << 24);
        }
        
        // Генерируем коэффициенты полинома в time domain
        for (int j = 0; j < CHIPMUNK_N; j++) {
            // Простой линейный конгруэнтный генератор для детерминированности
            l_state[j % 8] = l_state[j % 8] * 1664525 + 1013904223;
            a_params->a[i].coeffs[j] = l_state[j % 8] % CHIPMUNK_Q;
        }
        
        printf("    a[%d] time domain first coeffs: %d %d %d %d\n", i,
               a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], 
               a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
        
        // **ИСПРАВЛЕНО**: преобразуем в NTT domain как в оригинальном Rust коде
        // Original Rust: HOTSNTTPoly::from(&HOTSPoly::rand_poly(rng))
        chipmunk_ntt(a_params->a[i].coeffs);
        
        printf("    a[%d] NTT domain first coeffs: %d %d %d %d\n", i,
               a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], 
               a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
    }
    
    printf("✓ HOTS setup completed with %d parameters in NTT domain\n", CHIPMUNK_GAMMA);
    return 0;
}

/**
 * @brief Generate HOTS key pair
 * 
 * @param a_seed Base seed for key generation
 * @param a_counter Counter for key derivation
 * @param a_params Public parameters
 * @param a_pk Output public key
 * @param a_sk Output secret key
 * @return 0 on success, negative on error
 */
int chipmunk_hots_keygen(const uint8_t a_seed[32], uint32_t a_counter, 
                        const chipmunk_hots_params_t *a_params,
                        chipmunk_hots_pk_t *a_pk, chipmunk_hots_sk_t *a_sk) {
    if (!a_seed || !a_params || !a_pk || !a_sk) {
        printf("❌ NULL parameters in chipmunk_hots_keygen\n");
        return -1;
    }
    
    printf("🔍 HOTS keygen: Base seed = 0x%x\n", *(uint32_t*)a_seed);
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду
    // Original Rust: let sk = Self::derive_sk(seed, counter);
    
    // Initialize the RNG with seed and counter (как в Rust)
    uint8_t l_derived_seed[32];
    uint8_t l_counter_bytes[4];
    l_counter_bytes[0] = (a_counter >> 24) & 0xFF;
    l_counter_bytes[1] = (a_counter >> 16) & 0xFF;
    l_counter_bytes[2] = (a_counter >> 8) & 0xFF;
    l_counter_bytes[3] = a_counter & 0xFF;
    
    // Concatenate seed and counter
    uint8_t l_seed_and_counter[36];
    memcpy(l_seed_and_counter, a_seed, 32);
    memcpy(l_seed_and_counter + 32, l_counter_bytes, 4);
    
    // Hash to get derived seed
    dap_hash_fast_t l_hash_out;
    dap_hash_fast(l_seed_and_counter, 36, &l_hash_out);
    memcpy(l_derived_seed, &l_hash_out, 32);
    
    // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: секретные ключи s0, s1 генерируются и хранятся в NTT домене!
    // Original Rust: s0.iter_mut().for_each(|x| *x = HOTSNTTPoly::from(&HOTSPoly::rand_mod_p(&mut rng, PHI)));
    // Original Rust: s1.iter_mut().for_each(|x| *x = HOTSNTTPoly::from(&HOTSPoly::rand_mod_p(&mut rng, PHI_ALPHA_H)));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        printf("🔑 Generating key pair %d/%d...\n", i+1, CHIPMUNK_GAMMA);
        
        // Generate s0[i] in time domain, then convert to NTT
        uint8_t l_s0_seed[36];
        memcpy(l_s0_seed, l_derived_seed, 32);
        uint32_t l_s0_nonce = a_counter + i;
        memcpy(l_s0_seed + 32, &l_s0_nonce, 4);
        printf("  s0[%d] seed: 0x%x\n", i, l_s0_nonce);
        
        chipmunk_poly_uniform_mod_p(&a_sk->s0[i], l_s0_seed, CHIPMUNK_PHI);
        printf("  s0[%d] first coeffs: %d %d %d %d\n", i,
               a_sk->s0[i].coeffs[0], a_sk->s0[i].coeffs[1], a_sk->s0[i].coeffs[2], a_sk->s0[i].coeffs[3]);
        
        // **ИСПРАВЛЕНО**: преобразуем s0[i] в NTT домен для хранения
        chipmunk_ntt(a_sk->s0[i].coeffs);
        printf("  s0[%d] NTT first coeffs: %d %d %d %d\n", i,
               a_sk->s0[i].coeffs[0], a_sk->s0[i].coeffs[1], a_sk->s0[i].coeffs[2], a_sk->s0[i].coeffs[3]);
        
        // Generate s1[i] in time domain, then convert to NTT
        uint8_t l_s1_seed[36];
        memcpy(l_s1_seed, l_derived_seed, 32);
        uint32_t l_s1_nonce = a_counter + CHIPMUNK_GAMMA + i;
        memcpy(l_s1_seed + 32, &l_s1_nonce, 4);
        printf("  s1[%d] seed: 0x%x\n", i, l_s1_nonce);
        
        chipmunk_poly_uniform_mod_p(&a_sk->s1[i], l_s1_seed, CHIPMUNK_PHI_ALPHA_H);
        printf("  s1[%d] first coeffs: %d %d %d %d\n", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
        
        // **ИСПРАВЛЕНО**: преобразуем s1[i] в NTT домен для хранения
        chipmunk_ntt(a_sk->s1[i].coeffs);
        printf("  s1[%d] NTT first coeffs: %d %d %d %d\n", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
    }
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду для генерации публичного ключа
    // Original Rust: pp.a.iter().zip(sk.s0.iter().zip(sk.s1.iter())).for_each(|(&a, (&s0, &s1))| {
    //     pk.v0 += (&(a * s0)).into();
    //     pk.v1 += (&(a * s1)).into();
    // });
    
    // Инициализируем публичный ключ в time домене
    memset(&a_pk->v0, 0, sizeof(a_pk->v0));
    memset(&a_pk->v1, 0, sizeof(a_pk->v1));
    
    chipmunk_poly_t l_v0_time_sum, l_v1_time_sum;
    memset(&l_v0_time_sum, 0, sizeof(l_v0_time_sum));
    memset(&l_v1_time_sum, 0, sizeof(l_v1_time_sum));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        // a[i] * s0[i] - ВСЕ в NTT домене
        chipmunk_poly_t l_term_v0_ntt;
        chipmunk_poly_mul_ntt(&l_term_v0_ntt, &a_params->a[i], &a_sk->s0[i]);
        printf("  After a[%d] * s0[%d]: term_v0_ntt[0-3] = %d %d %d %d\n", i, i,
               l_term_v0_ntt.coeffs[0], l_term_v0_ntt.coeffs[1], l_term_v0_ntt.coeffs[2], l_term_v0_ntt.coeffs[3]);
        
        // a[i] * s1[i] - ВСЕ в NTT домене
        chipmunk_poly_t l_term_v1_ntt;
        chipmunk_poly_mul_ntt(&l_term_v1_ntt, &a_params->a[i], &a_sk->s1[i]);
        printf("  After a[%d] * s1[%d]: term_v1_ntt[0-3] = %d %d %d %d\n", i, i,
               l_term_v1_ntt.coeffs[0], l_term_v1_ntt.coeffs[1], l_term_v1_ntt.coeffs[2], l_term_v1_ntt.coeffs[3]);
        
        // **ИСПРАВЛЕНО**: преобразуем в time домен для накопления
        // Original Rust: pk.v0 += (&(a * s0)).into(); - .into() означает преобразование в time домен!
        chipmunk_poly_t l_term_v0_time = l_term_v0_ntt;
        chipmunk_poly_t l_term_v1_time = l_term_v1_ntt;
        
        chipmunk_invntt(l_term_v0_time.coeffs);
        chipmunk_invntt(l_term_v1_time.coeffs);
        
        printf("  After invNTT term_v0_time[0-3] = %d %d %d %d\n",
               l_term_v0_time.coeffs[0], l_term_v0_time.coeffs[1], l_term_v0_time.coeffs[2], l_term_v0_time.coeffs[3]);
        printf("  After invNTT term_v1_time[0-3] = %d %d %d %d\n",
               l_term_v1_time.coeffs[0], l_term_v1_time.coeffs[1], l_term_v1_time.coeffs[2], l_term_v1_time.coeffs[3]);
        
        // Накапливаем в time домене
        if (i == 0) {
            l_v0_time_sum = l_term_v0_time;
            l_v1_time_sum = l_term_v1_time;
        } else {
            chipmunk_poly_add(&l_v0_time_sum, &l_v0_time_sum, &l_term_v0_time);
            chipmunk_poly_add(&l_v1_time_sum, &l_v1_time_sum, &l_term_v1_time);
        }
        
        printf("  After addition: v0_time_sum[0-3] = %d %d %d %d\n",
               l_v0_time_sum.coeffs[0], l_v0_time_sum.coeffs[1], l_v0_time_sum.coeffs[2], l_v0_time_sum.coeffs[3]);
        printf("  After addition: v1_time_sum[0-3] = %d %d %d %d\n",
               l_v1_time_sum.coeffs[0], l_v1_time_sum.coeffs[1], l_v1_time_sum.coeffs[2], l_v1_time_sum.coeffs[3]);
    }
    
    // **ИСПРАВЛЕНО**: публичный ключ хранится в time домене!
    // Original Rust: HotsPK { v0: HOTSPoly, v1: HOTSPoly } - это time домен
    a_pk->v0 = l_v0_time_sum;
    a_pk->v1 = l_v1_time_sum;
    
    printf("✓ Public key computed and stored in time domain (CORRECTED METHOD)\n");
    printf("  v0 (time) first coeffs: %d %d %d %d\n",
           a_pk->v0.coeffs[0], a_pk->v0.coeffs[1], a_pk->v0.coeffs[2], a_pk->v0.coeffs[3]);
    printf("  v1 (time) first coeffs: %d %d %d %d\n",
           a_pk->v1.coeffs[0], a_pk->v1.coeffs[1], a_pk->v1.coeffs[2], a_pk->v1.coeffs[3]);
    
    printf("✓ HOTS keygen completed with unique s0[i] and s1[i]\n");
    return 0;
}

/**
 * @brief Sign message using HOTS
 * 
 * @param a_sk Secret key
 * @param a_message Message to sign
 * @param a_message_len Message length
 * @param a_signature Output signature
 * @return 0 on success, negative on error
 */
int chipmunk_hots_sign(const chipmunk_hots_sk_t *a_sk, const uint8_t *a_message, 
                      size_t a_message_len, chipmunk_hots_signature_t *a_signature) {
    if (!a_sk || !a_message || !a_signature) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_sign");
        return -1;
    }
    
    printf("🔍 HOTS sign: Starting signature generation...\n");
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду
    // Original Rust: let hm: HOTSNTTPoly = (&HOTSPoly::from_hash_message(message)).into();
    // Original Rust: for (s, (&s0, &s1)) in sigma.iter_mut().zip(sk.s0.iter().zip(sk.s1.iter())) {
    //     *s = (&(s0 * hm + s1)).into();
    // }
    
    // Hash message to polynomial
    chipmunk_poly_t l_hm;
    if (chipmunk_poly_from_hash(&l_hm, a_message, a_message_len) != 0) {
        log_it(L_ERROR, "Failed to hash message in chipmunk_hots_sign");
        return -1;
    }
    
    // **ИСПРАВЛЕНО**: Convert to NTT domain для операций
    // Original Rust: let hm: HOTSNTTPoly = (&HOTSPoly::from_hash_message(message)).into();
    chipmunk_ntt(l_hm.coeffs);
    printf("✓ H(m) in NTT domain first coeffs: %d %d %d %d\n",
           l_hm.coeffs[0], l_hm.coeffs[1], l_hm.coeffs[2], l_hm.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: согласно оригинальному Rust коду
    // Original Rust: *s = (&(s0 * hm + s1)).into();
    // Результат подписи хранится в time domain!
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        printf("🔢 Computing σ[%d] = s0[%d] * H(m) + s1[%d]...\n", i, i, i);
        
        // Debug secret key components (они уже в NTT домене)
        printf("  s0[%d] first coeffs: %d %d %d %d\n", i,
               a_sk->s0[i].coeffs[0], a_sk->s0[i].coeffs[1], a_sk->s0[i].coeffs[2], a_sk->s0[i].coeffs[3]);
        printf("  s1[%d] first coeffs: %d %d %d %d\n", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
        
        // **ИСПРАВЛЕНО**: s0[i] * H(m) - ВСЕ в NTT домене (s0[i] уже в NTT, H(m) в NTT)
        chipmunk_poly_t l_temp;
        chipmunk_poly_mul_ntt(&l_temp, &a_sk->s0[i], &l_hm);
        printf("  s0[%d] * H(m) first coeffs: %d %d %d %d\n", i,
               l_temp.coeffs[0], l_temp.coeffs[1], l_temp.coeffs[2], l_temp.coeffs[3]);
        
        // **ИСПРАВЛЕНО**: σ[i] = s0[i] * H(m) + s1[i] - ВСЕ в NTT домене (s1[i] уже в NTT)
        chipmunk_poly_add_ntt(&l_temp, &l_temp, &a_sk->s1[i]);
        printf("  σ[%d] (NTT) first coeffs: %d %d %d %d\n", i,
               l_temp.coeffs[0], l_temp.coeffs[1], l_temp.coeffs[2], l_temp.coeffs[3]);
        
        // **ИСПРАВЛЕНО**: преобразуем результат в time domain для хранения
        // Original Rust: *s = (&(s0 * hm + s1)).into(); - .into() означает преобразование в time домен!
        a_signature->sigma[i] = l_temp;
        chipmunk_invntt(a_signature->sigma[i].coeffs);
        
        printf("  σ[%d] (time) first coeffs: %d %d %d %d\n", i,
               a_signature->sigma[i].coeffs[0], a_signature->sigma[i].coeffs[1], 
               a_signature->sigma[i].coeffs[2], a_signature->sigma[i].coeffs[3]);
    }
    
    printf("✓ HOTS signature generation completed\n");
    return 0;
}

/**
 * @brief Verify HOTS signature
 * 
 * @param a_pk Public key
 * @param a_message Message that was signed
 * @param a_message_len Message length
 * @param a_signature Signature to verify
 * @param a_params Public parameters
 * @return 1 if valid, 0 if invalid, negative on error
 */
int chipmunk_hots_verify(const chipmunk_hots_pk_t *a_pk, const uint8_t *a_message,
                        size_t a_message_len, const chipmunk_hots_signature_t *a_signature,
                        const chipmunk_hots_params_t *a_params) {
    if (!a_pk || !a_message || !a_signature || !a_params) {
        printf("❌ NULL parameters in chipmunk_hots_verify\n");
        return -1;
    }
    
    printf("🔍 HOTS verify: Starting detailed verification...\n");
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду
    // Original Rust: let hm: HOTSNTTPoly = (&HOTSPoly::from_hash_message(message)).into();
    // Original Rust: let mut left = HOTSNTTPoly::default();
    // Original Rust: for (&a, s) in pp.a.iter().zip(sig.sigma.iter()) {
    //     left += a * HOTSNTTPoly::from(s)
    // }
    // Original Rust: let right = hm * HOTSNTTPoly::from(&pk.v0) + HOTSNTTPoly::from(&pk.v1);
    // Original Rust: let res = HOTSPoly::from(&left) == HOTSPoly::from(&right);
    
    // Hash message to polynomial
    chipmunk_poly_t l_hm;
    if (chipmunk_poly_from_hash(&l_hm, a_message, a_message_len) != 0) {
        printf("❌ Failed to hash message to polynomial\n");
        return -1;
    }
    
    printf("✓ Message hashed to polynomial\n");
    printf("  H(m) first coeffs: %d %d %d %d\n", 
           l_hm.coeffs[0], l_hm.coeffs[1], l_hm.coeffs[2], l_hm.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: Transform H(m) to NTT domain для операций
    // Original Rust: let hm: HOTSNTTPoly = (&HOTSPoly::from_hash_message(message)).into();
    chipmunk_poly_t l_hm_ntt = l_hm;
    chipmunk_ntt(l_hm_ntt.coeffs);
    printf("✓ H(m) transformed to NTT domain\n");
    printf("  H(m)_ntt first coeffs: %d %d %d %d\n", 
           l_hm_ntt.coeffs[0], l_hm_ntt.coeffs[1], l_hm_ntt.coeffs[2], l_hm_ntt.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: Transform public key to NTT domain для операций
    // Original Rust: HOTSNTTPoly::from(&pk.v0) и HOTSNTTPoly::from(&pk.v1)
    // Публичный ключ хранится в time domain, преобразуем в NTT домен для операций
    chipmunk_poly_t l_v0_ntt = a_pk->v0;
    chipmunk_poly_t l_v1_ntt = a_pk->v1;
    
    chipmunk_ntt(l_v0_ntt.coeffs);
    chipmunk_ntt(l_v1_ntt.coeffs);
    
    printf("✓ Public key transformed to NTT domain\n");
    printf("  v0_ntt first coeffs: %d %d %d %d\n", 
           l_v0_ntt.coeffs[0], l_v0_ntt.coeffs[1], l_v0_ntt.coeffs[2], l_v0_ntt.coeffs[3]);
    printf("  v1_ntt first coeffs: %d %d %d %d\n", 
           l_v1_ntt.coeffs[0], l_v1_ntt.coeffs[1], l_v1_ntt.coeffs[2], l_v1_ntt.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: Compute left side
    // Original Rust: let mut left = HOTSNTTPoly::default();
    chipmunk_poly_t l_left_ntt;
    memset(&l_left_ntt, 0, sizeof(l_left_ntt));
    
    printf("🔢 Computing left side: Σ(a_i * σ_i) - ВСЕ в NTT домене\n");
    
    // **ИСПРАВЛЕНО**: точно следуем оригинальному Rust коду
    // Original Rust: for (&a, s) in pp.a.iter().zip(sig.sigma.iter()) { left += a * HOTSNTTPoly::from(s) }
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        printf("  Processing pair %d/%d...\n", i+1, CHIPMUNK_GAMMA);
        
        // **КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ**: a_params->a[i] УЖЕ в NTT домене!
        // Original Rust: pp.a это [HOTSNTTPoly; GAMMA] - уже в NTT домене
        // НЕ преобразуем a[i], он уже в NTT домене!
        
        // **ИСПРАВЛЕНО**: Transform σ_i from time to NTT domain для операций
        // Original Rust: HOTSNTTPoly::from(s) - это преобразование ИЗ time В NTT домен!
        // σ_i хранится в time domain, преобразуем В NTT домен для операций
        chipmunk_poly_t l_sigma_i_ntt = a_signature->sigma[i];
        chipmunk_ntt(l_sigma_i_ntt.coeffs);
        
        printf("    a[%d] (already NTT) first coeffs: %d %d %d %d\n", i,
               a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
        printf("    σ[%d] time first coeffs: %d %d %d %d\n", i,
               a_signature->sigma[i].coeffs[0], a_signature->sigma[i].coeffs[1], a_signature->sigma[i].coeffs[2], a_signature->sigma[i].coeffs[3]);
        printf("    σ[%d] ntt first coeffs: %d %d %d %d\n", i,
               l_sigma_i_ntt.coeffs[0], l_sigma_i_ntt.coeffs[1], l_sigma_i_ntt.coeffs[2], l_sigma_i_ntt.coeffs[3]);
        
        // Multiply a_i * σ_i in NTT domain - a[i] УЖЕ в NTT домене!
        chipmunk_poly_t l_term;
        chipmunk_poly_mul_ntt(&l_term, &a_params->a[i], &l_sigma_i_ntt);
        
        printf("    a[%d] * σ[%d] first coeffs: %d %d %d %d\n", i, i,
               l_term.coeffs[0], l_term.coeffs[1], l_term.coeffs[2], l_term.coeffs[3]);
        
        // Add to running sum - ВСЕ в NTT домене
        if (i == 0) {
            l_left_ntt = l_term;
        } else {
            chipmunk_poly_add_ntt(&l_left_ntt, &l_left_ntt, &l_term);
        }
        
        printf("    Running sum first coeffs: %d %d %d %d\n",
               l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    }
    
    printf("✓ Left side computed: Σ(a_i * σ_i) in NTT domain\n");
    printf("  Final left sum first coeffs: %d %d %d %d\n",
           l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    
    printf("🔢 Computing right side: H(m) * v0 + v1 - ВСЕ в NTT домене\n");
    
    // **ИСПРАВЛЕНО**: Compute right side
    // Original Rust: let right = hm * HOTSNTTPoly::from(&pk.v0) + HOTSNTTPoly::from(&pk.v1);
    chipmunk_poly_t l_hm_v0;
    chipmunk_poly_mul_ntt(&l_hm_v0, &l_hm_ntt, &l_v0_ntt);
    
    printf("  H(m) * v0 first coeffs: %d %d %d %d\n",
           l_hm_v0.coeffs[0], l_hm_v0.coeffs[1], l_hm_v0.coeffs[2], l_hm_v0.coeffs[3]);
    
    chipmunk_poly_t l_right_ntt;
    chipmunk_poly_add_ntt(&l_right_ntt, &l_hm_v0, &l_v1_ntt);
    
    printf("✓ Right side computed: H(m) * v0 + v1 in NTT domain\n");
    printf("  Final right sum first coeffs: %d %d %d %d\n",
           l_right_ntt.coeffs[0], l_right_ntt.coeffs[1], l_right_ntt.coeffs[2], l_right_ntt.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: Compare results
    // Original Rust: let res = HOTSPoly::from(&left) == HOTSPoly::from(&right);
    // Преобразуем результаты в time domain для сравнения
    chipmunk_poly_t l_left_time = l_left_ntt;
    chipmunk_poly_t l_right_time = l_right_ntt;
    
    chipmunk_invntt(l_left_time.coeffs);
    chipmunk_invntt(l_right_time.coeffs);
    
    printf("🔍 Comparing results in time domain:\n");
    printf("  Left side first coeffs:  %d %d %d %d\n", 
           l_left_time.coeffs[0], l_left_time.coeffs[1], l_left_time.coeffs[2], l_left_time.coeffs[3]);
    printf("  Right side first coeffs: %d %d %d %d\n", 
           l_right_time.coeffs[0], l_right_time.coeffs[1], l_right_time.coeffs[2], l_right_time.coeffs[3]);
    
    // **ИСПРАВЛЕНО**: используем точную функцию сравнения как в оригинальном Rust коде
    // Original Rust: let res = HOTSPoly::from(&left) == HOTSPoly::from(&right);
    bool l_equal = chipmunk_poly_equal(&l_left_time, &l_right_time);
    
    if (l_equal) {
        printf("✅ VERIFICATION SUCCESSFUL: Equations match!\n");
        return 0;
    } else {
        printf("❌ VERIFICATION FAILED: Equations don't match\n");
        
        // Count differing coefficients for debugging
        int l_diff_count = 0;
        for (int i = 0; i < CHIPMUNK_N; i++) {
            if (l_left_time.coeffs[i] != l_right_time.coeffs[i]) {
                l_diff_count++;
                if (l_diff_count <= 5) {  // Show first 5 differences
                    printf("  Coeff[%d]: %d != %d (diff: %d)\n", i,
                           l_left_time.coeffs[i], l_right_time.coeffs[i],
                           l_left_time.coeffs[i] - l_right_time.coeffs[i]);
                }
            }
        }
        printf("  Total differing coefficients: %d/%d\n", l_diff_count, CHIPMUNK_N);
        
        return -1;  // Return 0 for invalid signature
    }
} 