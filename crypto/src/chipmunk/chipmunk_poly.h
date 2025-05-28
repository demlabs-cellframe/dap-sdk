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

#include <stdint.h>
#include <stdlib.h>
#include "chipmunk.h"

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
 * @brief Create challenge polynomial
 * 
 * @param a_poly Output polynomial to be filled with challenge coefficients
 * @param a_seed 32-byte seed for deterministic generation
 * @return int CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_challenge(chipmunk_poly_t *a_poly, const uint8_t a_seed[32]);

/**
 * @brief Check polynomial norm
 * 
 * @param a_poly Polynomial to check
 * @param a_bound Maximum absolute value that coefficients can have
 * @return Returns 0 if all coefficients are within the bound, 1 otherwise
 */
int chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound);

/**
 * @brief Decompose a polynomial into high and low parts
 * 
 * @param a_out Output polynomial with high bits (w1)
 * @param a_in Input polynomial
 */
void chipmunk_poly_highbits(chipmunk_poly_t *a_out, const chipmunk_poly_t *a_in);

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

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_POLY_H_ */ 