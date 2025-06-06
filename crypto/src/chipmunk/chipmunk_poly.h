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

#pragma once
#ifndef _CHIPMUNK_POLY_H_
#define _CHIPMUNK_POLY_H_

#include "chipmunk.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Transform polynomial to NTT form
 * 
 * @param a_poly Polynomial to transform
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_ntt(chipmunk_poly_t *a_poly);

/**
 * @brief Inverse transform from NTT form
 * 
 * @param a_poly Polynomial to transform
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_invntt(chipmunk_poly_t *a_poly);

/**
 * @brief Add two polynomials modulo q
 * 
 * @param a_result Result polynomial
 * @param a_a First polynomial
 * @param a_b Second polynomial
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_add(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Subtract two polynomials modulo q
 * 
 * @param a_result Result polynomial
 * @param a_a First polynomial
 * @param a_b Second polynomial
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_sub(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Multiply two polynomials in NTT form
 * 
 * @param a_result Result polynomial
 * @param a_a First polynomial
 * @param a_b Second polynomial
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_pointwise(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Fill polynomial with uniformly distributed coefficients
 * 
 * @param a_poly Polynomial to fill
 * @param a_seed 32-byte seed for deterministic generation
 * @param a_nonce Nonce value to use with seed
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_uniform(chipmunk_poly_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);

/**
 * @brief Decompose polynomial into power-of-2 base representation
 * 
 * @param r1 Output polynomial for higher bits  
 * @param r0 Output polynomial for lower bits
 * @param a Input polynomial to decompose
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_decompose(chipmunk_poly_t *r1, chipmunk_poly_t *r0, const chipmunk_poly_t *a);

/**
 * @brief Generate challenge polynomial from hash
 * 
 * @param c Output challenge polynomial
 * @param hash Input hash bytes
 * @param hash_len Length of hash
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_challenge(chipmunk_poly_t *c, const uint8_t *hash, size_t hash_len);

/**
 * @brief Check polynomial norm
 * 
 * @param a_poly Polynomial to check
 * @param a_bound Maximum absolute value that coefficients can have
 * @return Returns 0 if all coefficients are within the bound, 1 otherwise
 */
int chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound);

/**
 * @brief Extract and pack high bits from polynomial
 * 
 * @param a_output Output buffer for packed high bits (128 bytes)
 * @param a_poly Input polynomial
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_highbits(uint8_t *a_output, const chipmunk_poly_t *a_poly);

/**
 * @brief Apply hint bits to produce w1
 * 
 * @param a_out Output polynomial with applied hints
 * @param a_in Input polynomial w to be hinted
 * @param a_hint Hint bit array
 */
void chipmunk_use_hint(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_in, const uint8_t a_hint[CHIPMUNK_N/8]);

/**
 * @brief Compute hint bits for verification
 * 
 * @param a_hint Output hint bits array
 * @param a_poly1 First polynomial (z)
 * @param a_poly2 Second polynomial (r)
 */
void chipmunk_make_hint(uint8_t a_hint[CHIPMUNK_N/8], const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Create polynomial from hash of message
 * 
 * @param a_poly Output polynomial
 * @param a_message Message to hash
 * @param a_message_len Message length
 * @return 0 on success, negative on error
 */
int chipmunk_poly_from_hash(chipmunk_poly_t *a_poly, const uint8_t *a_message, size_t a_message_len);

/**
 * @brief Multiply two polynomials in NTT domain
 * 
 * @param a_result Output polynomial (can be same as input)
 * @param a_poly1 First polynomial (in NTT domain)
 * @param a_poly2 Second polynomial (in NTT domain)
 */
void chipmunk_poly_mul_ntt(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Add two polynomials in NTT domain
 * 
 * @param a_result Output polynomial (can be same as input)
 * @param a_poly1 First polynomial (in NTT domain)
 * @param a_poly2 Second polynomial (in NTT domain)
 */
void chipmunk_poly_add_ntt(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Check if two polynomials are equal
 * 
 * @param a_poly1 First polynomial
 * @param a_poly2 Second polynomial
 * @return true if equal, false otherwise
 */
bool chipmunk_poly_equal(const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Generate random polynomial in time domain
 * @param a_poly Output polynomial
 * @param a_seed Seed for generation
 * @param a_seed_len Seed length
 * @param a_modulus Modulus for coefficients
 * @return 0 on success, negative on error
 */
int dap_random_poly_time_domain(chipmunk_poly_t *a_poly, const uint8_t *a_seed, size_t a_seed_len, int a_modulus);

/**
 * @brief Generate uniform polynomial with coefficients in range [-bound, bound]
 */
int chipmunk_poly_uniform_mod_p(chipmunk_poly_t *a_poly, const uint8_t a_seed[36], int32_t a_bound);

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_POLY_H_ */ 