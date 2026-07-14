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

void chipmunk_poly_sub_ntt(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Check if two polynomials are equal
 * 
 * @param a_poly1 First polynomial
 * @param a_poly2 Second polynomial
 * @return true if equal, false otherwise
 */
bool chipmunk_poly_equal(const chipmunk_poly_t *a_poly1, const chipmunk_poly_t *a_poly2);

/**
 * @brief Generate uniform polynomial with coefficients in range [-bound, bound]
 */
int chipmunk_poly_uniform_mod_p(chipmunk_poly_t *a_poly, const uint8_t a_seed[36], int32_t a_bound);

/**
 * @brief Safe modular reduction of a signed 64-bit value into [0, CHIPMUNK_Q).
 *
 * Centralised replacement for the three private copies of s_mod_q that lived in
 * chipmunk_lrs.c, chipmunk_snark.c, and chipmunk_range_proof.c.
 *
 * @param a_val  Signed 64-bit value to reduce.
 * @return       Value in [0, CHIPMUNK_Q).
 */
static inline int32_t chipmunk_mod_q(int64_t a_val)
{
    int64_t l_r = a_val % (int64_t)CHIPMUNK_Q;
    if (l_r < 0)
        l_r += CHIPMUNK_Q;
    return (int32_t)l_r;
}

/**
 * @brief Rejection-sample a single uint32_t into [0, a_range).
 *
 * Consumes 4 bytes of XOF output and returns an unbiased sample in
 * [0, a_range).  If the 4-byte value falls in the rejection zone the
 * function returns -1 and the caller must call again with fresh bytes.
 *
 * Bias is eliminated because the rejection threshold is
 *   threshold = (2^32 / range) * range
 * so all accepted values map uniformly.
 *
 * @param a_raw       4 raw bytes from XOF (consumed unconditionally).
 * @param a_range     Desired range (must be > 0 and <= 2^32).
 * @return            Sample in [0, a_range), or -1 on rejection.
 */
static inline int32_t chipmunk_sample_reject4(const uint8_t a_raw[4], uint32_t a_range)
{
    uint32_t l_val;
    memcpy(&l_val, a_raw, 4);
    /* threshold = (2^32 / range) * range  — largest multiple of range <= 2^32 */
    uint64_t l_thresh = ((uint64_t)0x100000000ULL / a_range) * a_range;
    if (l_val >= (uint32_t)l_thresh)
        return -1;
    return (int32_t)(l_val % a_range);
}

/**
 * @brief Rejection-sample a single uint8_t into [0, a_range).
 *
 * Same as chipmunk_sample_reject4 but for 8-bit inputs.  Useful for
 * small ranges (e.g. 3) where wasting bytes is irrelevant.
 *
 * @param a_raw    1 raw byte from XOF.
 * @param a_range  Desired range (must be > 0 and <= 256).
 * @return         Sample in [0, a_range), or -1 on rejection.
 */
static inline int32_t chipmunk_sample_reject1(uint8_t a_raw, uint32_t a_range)
{
    uint32_t l_thresh = (256U / a_range) * a_range;
    if (a_raw >= (uint8_t)l_thresh)
        return -1;
    return (int32_t)(a_raw % a_range);
}

#ifdef __cplusplus
}
#endif

#endif /* _CHIPMUNK_POLY_H_ */ 