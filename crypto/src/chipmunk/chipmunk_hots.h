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

#include "chipmunk.h"
#include "chipmunk_poly.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file chipmunk_hots.h
 * @brief HOTS (Homomorphic One-Time Signatures) implementation for Chipmunk
 * 
 * Based on the original Rust implementation from Chipmunk repository.
 * HOTS signature scheme: σ = s0 * H(m) + s1 for each polynomial in GAMMA
 * Verification: Σ(a_i * σ_i) == H(m) * v0 + v1
 */

/**
 * @brief HOTS public parameters
 */
typedef struct {
    chipmunk_poly_t a[CHIPMUNK_GAMMA];  ///< Random matrix A in NTT domain
} chipmunk_hots_params_t;

/**
 * @brief HOTS public key
 */
typedef struct {
    chipmunk_poly_t v0;  ///< v0 = Σ(a_i * s0_i)
    chipmunk_poly_t v1;  ///< v1 = Σ(a_i * s1_i)
} chipmunk_hots_pk_t;

/**
 * @brief HOTS secret key
 */
typedef struct {
    chipmunk_poly_t s0[CHIPMUNK_GAMMA];  ///< Secret polynomials s0 in NTT domain
    chipmunk_poly_t s1[CHIPMUNK_GAMMA];  ///< Secret polynomials s1 in NTT domain
} chipmunk_hots_sk_t;

/**
 * @brief HOTS signature
 */
typedef struct {
    chipmunk_poly_t sigma[CHIPMUNK_GAMMA];  ///< Signature polynomials σ_i = s0_i * H(m) + s1_i
} chipmunk_hots_signature_t;

/**
 * @brief Setup HOTS public parameters
 * 
 * @param a_params Output parameters structure
 * @return 0 on success, negative on error
 */
int chipmunk_hots_setup(chipmunk_hots_params_t *a_params);

/**
 * @brief Generate HOTS key pair
 * 
 * @param a_seed Seed for key generation (32 bytes)
 * @param a_counter Counter for key derivation
 * @param a_params Public parameters
 * @param a_pk Output public key
 * @param a_sk Output secret key
 * @return 0 on success, negative on error
 */
int chipmunk_hots_keygen(const uint8_t a_seed[32], uint32_t a_counter, 
                        const chipmunk_hots_params_t *a_params,
                        chipmunk_hots_pk_t *a_pk, chipmunk_hots_sk_t *a_sk);

/**
 * @brief Sign message with HOTS
 * 
 * @param a_sk Secret key
 * @param a_message Message to sign
 * @param a_message_len Message length
 * @param a_signature Output signature
 * @return 0 on success, negative on error
 */
int chipmunk_hots_sign(const chipmunk_hots_sk_t *a_sk, const uint8_t *a_message, 
                      size_t a_message_len, chipmunk_hots_signature_t *a_signature);

/**
 * @brief Verify HOTS signature
 * 
 * @param a_pk Public key
 * @param a_message Message that was signed
 * @param a_message_len Message length
 * @param a_signature Signature to verify
 * @param a_params Public parameters
 * @return 0 if valid, negative on error or invalid signature
 */
int chipmunk_hots_verify(const chipmunk_hots_pk_t *a_pk, const uint8_t *a_message,
                        size_t a_message_len, const chipmunk_hots_signature_t *a_signature,
                        const chipmunk_hots_params_t *a_params);

/**
 * @brief Enable/disable debug output for HOTS module
 * 
 * @param a_enable Enable debug output if true
 */
void chipmunk_hots_set_debug(bool a_enable);

#ifdef __cplusplus
}
#endif 