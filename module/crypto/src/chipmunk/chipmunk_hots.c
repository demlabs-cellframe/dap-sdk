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

// Детальное логирование для Chipmunk HOTS модуля
static bool s_debug_more = true;


/**
 * @brief Enable/disable debug output for HOTS module
 */
void chipmunk_hots_set_debug(bool a_enable) {
    s_debug_more = a_enable;
}

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
    
    debug_if(s_debug_more, L_INFO, "🔧 HOTS setup: Generating public parameters...");
    
    // Use a fixed seed for reproducible test results
    uint32_t l_base_seed = 0x12345678;
    
    // Generate GAMMA random polynomials for public parameters
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        debug_if(s_debug_more, L_INFO, "  Generating parameter a[%d]...", i);
        
        // Generate random polynomial in time domain
        uint8_t l_param_seed[36];
        memcpy(l_param_seed, &l_base_seed, 4);
        uint32_t l_param_nonce = 0x10000000 + i;  // Unique nonce for each parameter
        memcpy(l_param_seed + 32, &l_param_nonce, 4);
        
        // Generate random polynomial in time domain
        dap_hash_fast_t l_hash_out;
        dap_hash_fast(l_param_seed, 36, &l_hash_out);
        
        uint8_t l_hash[32];
        memcpy(l_hash, &l_hash_out, 32);
        
        // Use hash as seed for ChaCha20-like generator
        uint32_t l_state[8];
        for (int j = 0; j < 8; j++) {
            l_state[j] = ((uint32_t)l_hash[j*4]) | 
                         ((uint32_t)l_hash[j*4+1] << 8) |
                         ((uint32_t)l_hash[j*4+2] << 16) |
                         ((uint32_t)l_hash[j*4+3] << 24);
        }
        
        // Generate polynomial coefficients in time domain
        for (int j = 0; j < CHIPMUNK_N; j++) {
            // Simple linear congruential generator for determinism
            l_state[j % 8] = l_state[j % 8] * 1664525 + 1013904223;
            a_params->a[i].coeffs[j] = l_state[j % 8] % CHIPMUNK_Q;
        }
        
        debug_if(s_debug_more, L_INFO, "    a[%d] time domain first coeffs: %d %d %d %d", i,
                 a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], 
                 a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
        
        // Convert to NTT domain as in original Rust code
        chipmunk_ntt(a_params->a[i].coeffs);
        
        debug_if(s_debug_more, L_INFO, "    a[%d] NTT domain first coeffs: %d %d %d %d", i,
                 a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], 
                 a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
    }
    
    debug_if(s_debug_more, L_INFO, "✓ HOTS setup completed with %d parameters in NTT domain", CHIPMUNK_GAMMA);
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
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_keygen");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "🔍 HOTS keygen: Starting key generation");
    
    // Initialize the RNG with seed and counter (as in Rust)
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
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        debug_if(s_debug_more, L_DEBUG, "🔑 Generating key pair %d/%d...", i+1, CHIPMUNK_GAMMA);
        
        // Generate s0[i] in time domain, then convert to NTT
        uint8_t l_s0_seed[36];
        memcpy(l_s0_seed, l_derived_seed, 32);
        uint32_t l_s0_nonce = a_counter + i;
        memcpy(l_s0_seed + 32, &l_s0_nonce, 4);
        
        chipmunk_poly_uniform_mod_p(&a_sk->s0[i], l_s0_seed, CHIPMUNK_PHI);
        debug_if(s_debug_more, L_DEBUG, "  s0[%d] first coeffs: %d %d %d %d", i,
               a_sk->s0[i].coeffs[0], a_sk->s0[i].coeffs[1], a_sk->s0[i].coeffs[2], a_sk->s0[i].coeffs[3]);
        
        // Convert s0[i] to NTT domain for storage
        chipmunk_ntt(a_sk->s0[i].coeffs);
        
        // Generate s1[i] in time domain, then convert to NTT
        uint8_t l_s1_seed[36];
        memcpy(l_s1_seed, l_derived_seed, 32);
        uint32_t l_s1_nonce = a_counter + CHIPMUNK_GAMMA + i;
        memcpy(l_s1_seed + 32, &l_s1_nonce, 4);
        
        chipmunk_poly_uniform_mod_p(&a_sk->s1[i], l_s1_seed, CHIPMUNK_PHI_ALPHA_H);
        debug_if(s_debug_more, L_DEBUG, "  s1[%d] first coeffs: %d %d %d %d", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
        
        // Convert s1[i] to NTT domain for storage
        chipmunk_ntt(a_sk->s1[i].coeffs);
        
        debug_if(s_debug_more, L_DEBUG, "  s1[%d] NTT first coeffs: %d %d %d %d", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
    }
    
    // Initialize public key in time domain
    memset(&a_pk->v0, 0, sizeof(a_pk->v0));
    memset(&a_pk->v1, 0, sizeof(a_pk->v1));
    
    chipmunk_poly_t l_v0_time_sum, l_v1_time_sum;
    memset(&l_v0_time_sum, 0, sizeof(l_v0_time_sum));
    memset(&l_v1_time_sum, 0, sizeof(l_v1_time_sum));
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        // a[i] * s0[i] - ALL in NTT domain
        chipmunk_poly_t l_term_v0_ntt;
        chipmunk_poly_mul_ntt(&l_term_v0_ntt, &a_params->a[i], &a_sk->s0[i]);
        debug_if(s_debug_more, L_DEBUG, "  After a[%d] * s0[%d]: term_v0_ntt[0-3] = %d %d %d %d", i, i,
               l_term_v0_ntt.coeffs[0], l_term_v0_ntt.coeffs[1], l_term_v0_ntt.coeffs[2], l_term_v0_ntt.coeffs[3]);
        
        // a[i] * s1[i] - ALL in NTT domain
        chipmunk_poly_t l_term_v1_ntt;
        chipmunk_poly_mul_ntt(&l_term_v1_ntt, &a_params->a[i], &a_sk->s1[i]);
        debug_if(s_debug_more, L_DEBUG, "  After a[%d] * s1[%d]: term_v1_ntt[0-3] = %d %d %d %d", i, i,
               l_term_v1_ntt.coeffs[0], l_term_v1_ntt.coeffs[1], l_term_v1_ntt.coeffs[2], l_term_v1_ntt.coeffs[3]);
        
        // Convert to time domain for accumulation
        // Original Rust: pk.v0 += (&(a * s0)).into(); - .into() means converting to time domain!
        chipmunk_poly_t l_term_v0_time = l_term_v0_ntt;
        chipmunk_poly_t l_term_v1_time = l_term_v1_ntt;
        
        chipmunk_invntt(l_term_v0_time.coeffs);
        chipmunk_invntt(l_term_v1_time.coeffs);
        
        debug_if(s_debug_more, L_DEBUG, "  After invNTT term_v0_time[0-3] = %d %d %d %d",
               l_term_v0_time.coeffs[0], l_term_v0_time.coeffs[1], l_term_v0_time.coeffs[2], l_term_v0_time.coeffs[3]);
        debug_if(s_debug_more, L_DEBUG, "  After invNTT term_v1_time[0-3] = %d %d %d %d",
               l_term_v1_time.coeffs[0], l_term_v1_time.coeffs[1], l_term_v1_time.coeffs[2], l_term_v1_time.coeffs[3]);
        
        // Accumulate in time domain
        if (i == 0) {
            l_v0_time_sum = l_term_v0_time;
            l_v1_time_sum = l_term_v1_time;
        } else {
            chipmunk_poly_add(&l_v0_time_sum, &l_v0_time_sum, &l_term_v0_time);
            chipmunk_poly_add(&l_v1_time_sum, &l_v1_time_sum, &l_term_v1_time);
        }
        
        debug_if(s_debug_more, L_DEBUG, "  After addition: v0_time_sum[0-3] = %d %d %d %d",
               l_v0_time_sum.coeffs[0], l_v0_time_sum.coeffs[1], l_v0_time_sum.coeffs[2], l_v0_time_sum.coeffs[3]);
        debug_if(s_debug_more, L_DEBUG, "  After addition: v1_time_sum[0-3] = %d %d %d %d",
               l_v1_time_sum.coeffs[0], l_v1_time_sum.coeffs[1], l_v1_time_sum.coeffs[2], l_v1_time_sum.coeffs[3]);
    }
    
    // Initialize public key in time domain
    // Original Rust: HotsPK { v0: HOTSPoly, v1: HOTSPoly } - this is time domain
    a_pk->v0 = l_v0_time_sum;
    a_pk->v1 = l_v1_time_sum;
    
    debug_if(s_debug_more, L_DEBUG, "✓ Public key computed and stored in time domain (CORRECTED METHOD)");
    debug_if(s_debug_more, L_DEBUG, "  v0 (time) first coeffs: %d %d %d %d",
           a_pk->v0.coeffs[0], a_pk->v0.coeffs[1], a_pk->v0.coeffs[2], a_pk->v0.coeffs[3]);
    debug_if(s_debug_more, L_DEBUG, "  v1 (time) first coeffs: %d %d %d %d",
           a_pk->v1.coeffs[0], a_pk->v1.coeffs[1], a_pk->v1.coeffs[2], a_pk->v1.coeffs[3]);
    
    debug_if(s_debug_more, L_DEBUG, "✓ HOTS keygen completed with unique s0[i] and s1[i]");
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
    
    debug_if(s_debug_more, L_DEBUG, "🔍 HOTS sign: Starting signature generation...");
    
    // Hash message to polynomial
    chipmunk_poly_t l_hm;
    if (chipmunk_poly_from_hash(&l_hm, a_message, a_message_len) != 0) {
        log_it(L_ERROR, "Failed to hash message in chipmunk_hots_sign");
        return -1;
    }
    
    // Convert to NTT domain for operations
    chipmunk_ntt(l_hm.coeffs);
    debug_if(s_debug_more, L_DEBUG, "✓ H(m) in NTT domain first coeffs: %d %d %d %d",
           l_hm.coeffs[0], l_hm.coeffs[1], l_hm.coeffs[2], l_hm.coeffs[3]);
    
    // Result of signature is stored in time domain!
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        debug_if(s_debug_more, L_DEBUG, "🔢 Computing σ[%d] = s0[%d] * H(m) + s1[%d]...", i, i, i);
        
        // Debug secret key components (they are already in NTT domain)
        debug_if(s_debug_more, L_DEBUG, "  s0[%d] first coeffs: %d %d %d %d", i,
               a_sk->s0[i].coeffs[0], a_sk->s0[i].coeffs[1], a_sk->s0[i].coeffs[2], a_sk->s0[i].coeffs[3]);
        debug_if(s_debug_more, L_DEBUG, "  s1[%d] first coeffs: %d %d %d %d", i,
               a_sk->s1[i].coeffs[0], a_sk->s1[i].coeffs[1], a_sk->s1[i].coeffs[2], a_sk->s1[i].coeffs[3]);
        
        // s0[i] * H(m) - ALL in NTT domain (s0[i] is already in NTT, H(m) in NTT)
        chipmunk_poly_t l_temp;
        chipmunk_poly_mul_ntt(&l_temp, &a_sk->s0[i], &l_hm);
        debug_if(s_debug_more, L_DEBUG, "  s0[%d] * H(m) first coeffs: %d %d %d %d", i,
               l_temp.coeffs[0], l_temp.coeffs[1], l_temp.coeffs[2], l_temp.coeffs[3]);
        
        // σ[i] = s0[i] * H(m) + s1[i] - ALL in NTT domain (s1[i] is already in NTT)
        chipmunk_poly_add_ntt(&l_temp, &l_temp, &a_sk->s1[i]);
        debug_if(s_debug_more, L_DEBUG, "  σ[%d] (NTT) first coeffs: %d %d %d %d", i,
               l_temp.coeffs[0], l_temp.coeffs[1], l_temp.coeffs[2], l_temp.coeffs[3]);
        
        // Convert result to time domain for storage
        // Original Rust: *s = (&(s0 * hm + s1)).into(); - .into() means converting to time domain!
        a_signature->sigma[i] = l_temp;
        chipmunk_invntt(a_signature->sigma[i].coeffs);
        
        debug_if(s_debug_more, L_DEBUG, "  σ[%d] (time) first coeffs: %d %d %d %d", i,
               a_signature->sigma[i].coeffs[0], a_signature->sigma[i].coeffs[1], 
               a_signature->sigma[i].coeffs[2], a_signature->sigma[i].coeffs[3]);
    }
    
    debug_if(s_debug_more, L_DEBUG, "✓ HOTS signature generation completed");
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
 * @return 0 if valid (standard C convention), negative on error
 */
int chipmunk_hots_verify(const chipmunk_hots_pk_t *a_pk, const uint8_t *a_message,
                        size_t a_message_len, const chipmunk_hots_signature_t *a_signature,
                        const chipmunk_hots_params_t *a_params) {
    if (!a_pk || !a_message || !a_signature || !a_params) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_verify");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "🔍 HOTS verify: Starting detailed verification...");
    
    // Hash message to polynomial
    chipmunk_poly_t l_hm;
    if (chipmunk_poly_from_hash(&l_hm, a_message, a_message_len) != 0) {
        log_it(L_ERROR, "Failed to hash message to polynomial");
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "✓ Message hashed to polynomial");
    debug_if(s_debug_more, L_DEBUG, "  H(m) first coeffs: %d %d %d %d", 
           l_hm.coeffs[0], l_hm.coeffs[1], l_hm.coeffs[2], l_hm.coeffs[3]);
    
    // Transform H(m) to NTT domain for operations
    chipmunk_poly_t l_hm_ntt = l_hm;
    chipmunk_ntt(l_hm_ntt.coeffs);
    debug_if(s_debug_more, L_DEBUG, "✓ H(m) transformed to NTT domain");
    debug_if(s_debug_more, L_DEBUG, "  H(m)_ntt first coeffs: %d %d %d %d", 
           l_hm_ntt.coeffs[0], l_hm_ntt.coeffs[1], l_hm_ntt.coeffs[2], l_hm_ntt.coeffs[3]);
    
    // Transform public key to NTT domain for operations
    // Original Rust: HOTSNTTPoly::from(&pk.v0) and HOTSNTTPoly::from(&pk.v1)
    // Public key is stored in time domain, convert to NTT domain for operations
    chipmunk_poly_t l_v0_ntt = a_pk->v0;
    chipmunk_poly_t l_v1_ntt = a_pk->v1;
    
    chipmunk_ntt(l_v0_ntt.coeffs);
    chipmunk_ntt(l_v1_ntt.coeffs);
    
    debug_if(s_debug_more, L_DEBUG, "✓ Public key transformed to NTT domain");
    debug_if(s_debug_more, L_DEBUG, "  v0_ntt first coeffs: %d %d %d %d", 
           l_v0_ntt.coeffs[0], l_v0_ntt.coeffs[1], l_v0_ntt.coeffs[2], l_v0_ntt.coeffs[3]);
    debug_if(s_debug_more, L_DEBUG, "  v1_ntt first coeffs: %d %d %d %d", 
           l_v1_ntt.coeffs[0], l_v1_ntt.coeffs[1], l_v1_ntt.coeffs[2], l_v1_ntt.coeffs[3]);
    
    // Compute left side
    chipmunk_poly_t l_left_ntt;
    memset(&l_left_ntt, 0, sizeof(l_left_ntt));
    
    debug_if(s_debug_more, L_DEBUG, "🔢 Computing left side: Σ(a_i * σ_i) - ALL in NTT domain");
    
    for (int i = 0; i < CHIPMUNK_GAMMA; i++) {
        debug_if(s_debug_more, L_DEBUG, "  Processing pair %d/%d...", i+1, CHIPMUNK_GAMMA);
        
        // Transform σ_i from time to NTT domain for operations
        // Original Rust: HOTSNTTPoly::from(s) - this is conversion FROM time TO NTT domain!
        // σ_i is stored in time domain, convert TO NTT domain for operations
        chipmunk_poly_t l_sigma_i_ntt = a_signature->sigma[i];
        chipmunk_ntt(l_sigma_i_ntt.coeffs);
        
        debug_if(s_debug_more, L_DEBUG, "    a[%d] (already NTT) first coeffs: %d %d %d %d", i,
               a_params->a[i].coeffs[0], a_params->a[i].coeffs[1], a_params->a[i].coeffs[2], a_params->a[i].coeffs[3]);
        debug_if(s_debug_more, L_DEBUG, "    σ[%d] time first coeffs: %d %d %d %d", i,
               a_signature->sigma[i].coeffs[0], a_signature->sigma[i].coeffs[1], a_signature->sigma[i].coeffs[2], a_signature->sigma[i].coeffs[3]);
        debug_if(s_debug_more, L_DEBUG, "    σ[%d] ntt first coeffs: %d %d %d %d", i,
               l_sigma_i_ntt.coeffs[0], l_sigma_i_ntt.coeffs[1], l_sigma_i_ntt.coeffs[2], l_sigma_i_ntt.coeffs[3]);
        
        // Multiply a_i * σ_i in NTT domain - a[i] is already in NTT domain!
        chipmunk_poly_t l_term;
        chipmunk_poly_mul_ntt(&l_term, &a_params->a[i], &l_sigma_i_ntt);
        
        debug_if(s_debug_more, L_DEBUG, "    a[%d] * σ[%d] first coeffs: %d %d %d %d", i, i,
               l_term.coeffs[0], l_term.coeffs[1], l_term.coeffs[2], l_term.coeffs[3]);
        
        // Add to running sum - ALL in NTT domain
        if (i == 0) {
            l_left_ntt = l_term;
        } else {
            chipmunk_poly_add_ntt(&l_left_ntt, &l_left_ntt, &l_term);
        }
        
        debug_if(s_debug_more, L_DEBUG, "    Running sum first coeffs: %d %d %d %d",
               l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    }
    
    debug_if(s_debug_more, L_DEBUG, "✓ Left side computed: Σ(a_i * σ_i) in NTT domain");
    debug_if(s_debug_more, L_DEBUG, "  Final left sum first coeffs: %d %d %d %d",
           l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    
    debug_if(s_debug_more, L_DEBUG, "🔢 Computing right side: H(m) * v0 + v1 - ALL in NTT domain");
    
    // Compute right side
    chipmunk_poly_t l_hm_v0;
    chipmunk_poly_mul_ntt(&l_hm_v0, &l_hm_ntt, &l_v0_ntt);
    
    debug_if(s_debug_more, L_DEBUG, "  H(m) * v0 first coeffs: %d %d %d %d",
           l_hm_v0.coeffs[0], l_hm_v0.coeffs[1], l_hm_v0.coeffs[2], l_hm_v0.coeffs[3]);
    
    chipmunk_poly_t l_right_ntt;
    chipmunk_poly_add_ntt(&l_right_ntt, &l_hm_v0, &l_v1_ntt);
    
    debug_if(s_debug_more, L_DEBUG, "✓ Right side computed: H(m) * v0 + v1 in NTT domain");
    debug_if(s_debug_more, L_DEBUG, "  Final right sum first coeffs: %d %d %d %d",
           l_right_ntt.coeffs[0], l_right_ntt.coeffs[1], l_right_ntt.coeffs[2], l_right_ntt.coeffs[3]);
    
    // Test: First try comparison in NTT domain
    debug_if(s_debug_more, L_DEBUG, "🔍 Testing direct NTT domain comparison:");
    debug_if(s_debug_more, L_DEBUG, "  Left NTT first coeffs:  %d %d %d %d",
           l_left_ntt.coeffs[0], l_left_ntt.coeffs[1], l_left_ntt.coeffs[2], l_left_ntt.coeffs[3]);
    debug_if(s_debug_more, L_DEBUG, "  Right NTT first coeffs: %d %d %d %d",
           l_right_ntt.coeffs[0], l_right_ntt.coeffs[1], l_right_ntt.coeffs[2], l_right_ntt.coeffs[3]);
    
    bool l_ntt_equal = chipmunk_poly_equal(&l_left_ntt, &l_right_ntt);
    if (l_ntt_equal) {
        debug_if(s_debug_more, L_DEBUG, "✅ NTT DOMAIN VERIFICATION SUCCESSFUL!");
        return 0;  // Standard C convention: 0 for success
    }
    
    // Compare results in time domain as backup
    chipmunk_poly_t l_left_time = l_left_ntt;
    chipmunk_poly_t l_right_time = l_right_ntt;
    
    chipmunk_invntt(l_left_time.coeffs);
    chipmunk_invntt(l_right_time.coeffs);
    
    debug_if(s_debug_more, L_DEBUG, "🔍 Comparing results in time domain:");
    debug_if(s_debug_more, L_DEBUG, "  Left side first coeffs:  %d %d %d %d", 
           l_left_time.coeffs[0], l_left_time.coeffs[1], l_left_time.coeffs[2], l_left_time.coeffs[3]);
    debug_if(s_debug_more, L_DEBUG, "  Right side first coeffs: %d %d %d %d", 
           l_right_time.coeffs[0], l_right_time.coeffs[1], l_right_time.coeffs[2], l_right_time.coeffs[3]);
    
    // Use exact comparison function as in original Rust code
    bool l_equal = chipmunk_poly_equal(&l_left_time, &l_right_time);
    
    if (l_equal) {
        debug_if(s_debug_more, L_DEBUG, "✅ TIME DOMAIN VERIFICATION SUCCESSFUL: Equations match!");
        return 0;  // Standard C convention: 0 for success
    } else {
        debug_if(s_debug_more, L_DEBUG, "❌ VERIFICATION FAILED: Equations don't match in both domains");
        
        // Count differing coefficients for debugging
        int l_diff_count = 0;
        for (int i = 0; i < CHIPMUNK_N; i++) {
            if (l_left_time.coeffs[i] != l_right_time.coeffs[i]) {
                l_diff_count++;
                if (l_diff_count <= 5) {  // Show first 5 differences
                    debug_if(s_debug_more, L_DEBUG, "  Coeff[%d]: %d != %d (diff: %d)", i,
                           l_left_time.coeffs[i], l_right_time.coeffs[i],
                           l_left_time.coeffs[i] - l_right_time.coeffs[i]);
                }
            }
        }
        debug_if(s_debug_more, L_DEBUG, "  Total differing coefficients: %d/%d", l_diff_count, CHIPMUNK_N);
        
        return -1;  // Standard C convention: negative for failure/invalid signature
    }
} 