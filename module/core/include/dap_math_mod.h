/*
 * Authors:
 * Dmitriy A. Gearasimov <ceo@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DAP SDK https://gitlab.demlabs.net/dap/dap-sdk
 * Copyright  (c) 2025
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
#include <stdbool.h>
#include "dap_common.h"
#include "dap_math_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize modular arithmetic module
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_init(void);

/**
 * @brief Modular addition: (a + b) mod modulus
 * @param a First operand
 * @param b Second operand
 * @param modulus Modulus
 * @param result Result of modular addition
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_add(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result);

/**
 * @brief Modular subtraction: (a - b) mod modulus
 * @param a First operand
 * @param b Second operand
 * @param modulus Modulus
 * @param result Result of modular subtraction
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_sub(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result);

/**
 * @brief Modular multiplication: (a * b) mod modulus
 * @param a First operand
 * @param b Second operand
 * @param modulus Modulus
 * @param result Result of modular multiplication
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_mul(uint256_t a, uint256_t b, uint256_t modulus, uint256_t *result);

/**
 * @brief Modular exponentiation: (base^exponent) mod modulus
 * @param base Base
 * @param exponent Exponent
 * @param modulus Modulus
 * @param result Result of modular exponentiation
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_pow(uint256_t base, uint256_t exponent, uint256_t modulus, uint256_t *result);

/**
 * @brief Modular inverse: find x such that (a * x) mod modulus = 1
 * @param a Value to find inverse for
 * @param modulus Modulus
 * @param result Modular inverse
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_inverse(uint256_t a, uint256_t modulus, uint256_t *result);

/**
 * @brief Check if two values are congruent modulo modulus
 * @param a First value
 * @param b Second value
 * @param modulus Modulus
 * @return true if a ≡ b (mod modulus)
 */
bool dap_math_mod_congruent(uint256_t a, uint256_t b, uint256_t modulus);

/**
 * @brief Barrett reduction for fast modular reduction
 * @param a Value to reduce
 * @param modulus Modulus
 * @param mu Precomputed value for Barrett reduction (mu = 2^(2*k) / modulus)
 * @param result Reduced value
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_barrett_reduce(uint256_t a, uint256_t modulus, uint256_t mu, uint256_t *result);

/**
 * @brief Precompute Barrett reduction parameter mu
 * @param modulus Modulus
 * @param mu Output parameter for Barrett reduction
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_barrett_mu(uint256_t modulus, uint256_t *mu);

/**
 * @brief Montgomery reduction for fast modular multiplication
 * @param a Value to reduce
 * @param modulus Modulus
 * @param r2 Precomputed R^2 value
 * @param n_prime Precomputed n' value
 * @param result Reduced value
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_montgomery_reduce(uint256_t a, uint256_t modulus, uint256_t r2, uint256_t n_prime, uint256_t *result);

/**
 * @brief Precompute Montgomery parameters R^2 and n'
 * @param modulus Modulus
 * @param r2 Output R^2 value
 * @param n_prime Output n' value
 * @return 0 on success, negative error code on failure
 */
int dap_math_mod_montgomery_params(uint256_t modulus, uint256_t *r2, uint256_t *n_prime);

#ifdef __cplusplus
}
#endif
