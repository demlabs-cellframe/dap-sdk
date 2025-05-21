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

// NTT parameters
#define CHIPMUNK_ZETAS_MONT_LEN 512

/**
 * @brief Montgomery representation of roots of unity for NTT
 */
extern const int32_t g_zetas_mont[CHIPMUNK_ZETAS_MONT_LEN];

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
 * @brief Perform Montgomery reduction
 * @param[in,out] a_r Value to reduce
 */
void chipmunk_ntt_montgomery_reduce(int32_t *a_r);

/**
 * @brief Reduce value modulo q
 * @param[in] a_value Value to reduce
 * @return Reduced value
 */
int32_t chipmunk_ntt_mod_reduce(int32_t a_value);

/**
 * @brief Perform Barrett reduction
 * @param[in] a_value Value to reduce
 * @return Reduced value
 */
int32_t chipmunk_ntt_barrett_reduce(int32_t a_value);

/**
 * @brief Multiply two values with Montgomery reduction
 * @param[in] a_a First value
 * @param[in] a_b Second value
 * @return Result of multiplication
 */
int32_t chipmunk_ntt_montgomery_multiply(int32_t a_a, int32_t a_b);

/**
 * @brief Multiply value by 2^32 modulo q (Montgomery domain conversion)
 * @param[in] a_value Value to convert
 * @return Value in Montgomery domain
 */
int32_t chipmunk_ntt_mont_factor(int32_t a_value); 