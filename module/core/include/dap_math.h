/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

This file is part of DAP SDK the open source project

   DAP SDK is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   DAP SDK is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @brief DAP SDK Mathematical Functions
 * @details Provides common mathematical operations for cryptographic and general use
 */

/**
 * @brief Compute modular inverse using Extended Euclidean Algorithm
 * @details Computes (a * b^(-1)) mod mod using mathematically correct approach
 * @param a First operand
 * @param b Second operand (to be inverted)
 * @param mod Modulus
 * @return (a * b^(-1)) mod mod, or 0 if b is not invertible modulo mod
 */
int64_t dap_mod_inverse_u64(int64_t a, int64_t b, int64_t mod);

/**
 * @brief Compute modular exponentiation
 * @details Computes (base^exponent) mod modulus using square-and-multiply
 * @param base Base value
 * @param exponent Exponent value
 * @param modulus Modulus value
 * @return (base^exponent) mod modulus
 */
uint64_t dap_mod_pow_u64(uint64_t base, uint64_t exponent, uint64_t modulus);

/**
 * @brief Compute greatest common divisor
 * @details Uses Euclidean algorithm
 * @param a First number
 * @param b Second number
 * @return GCD(a, b)
 */
uint64_t dap_gcd_u64(uint64_t a, uint64_t b);

/**
 * @brief Compute least common multiple
 * @details Uses GCD-based formula: LCM(a,b) = (a*b)/GCD(a,b)
 * @param a First number
 * @param b Second number
 * @return LCM(a, b)
 */
uint64_t dap_lcm_u64(uint64_t a, uint64_t b);

/**
 * @brief Check if a number is prime (probabilistic)
 * @details Uses Miller-Rabin primality test
 * @param n Number to test
 * @param k Number of rounds (higher = more accurate)
 * @return true if probably prime, false if composite
 */
bool dap_is_prime_u64(uint64_t n, int k);

/**
 * @brief Generate next prime number
 * @details Finds the smallest prime >= n
 * @param n Starting number
 * @return Next prime number >= n
 */
uint64_t dap_next_prime_u64(uint64_t n);

/**
 * @brief Compute modular square root (if exists)
 * @details Uses Tonelli-Shanks algorithm for prime moduli
 * @param a Value to find square root of
 * @param p Prime modulus
 * @return Square root of a modulo p, or 0 if no solution exists
 */
uint64_t dap_mod_sqrt_u64(uint64_t a, uint64_t p);

/**
 * @brief Safe integer addition with overflow detection
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to store result
 * @return true if addition is safe, false if overflow would occur
 */
bool dap_safe_add_u64(uint64_t a, uint64_t b, uint64_t *result);

/**
 * @brief Safe integer multiplication with overflow detection
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to store result
 * @return true if multiplication is safe, false if overflow would occur
 */
bool dap_safe_mul_u64(uint64_t a, uint64_t b, uint64_t *result);

/**
 * @brief Constant-time comparison for cryptographic use
 * @details Compares two memory regions in constant time to prevent timing attacks
 * @param a First memory region
 * @param b Second memory region
 * @param size Number of bytes to compare
 * @return 0 if equal, non-zero if different (timing-safe)
 */
int dap_const_time_memcmp(const void *a, const void *b, size_t size);

/**
 * @brief Constant-time conditional move for cryptographic use
 * @details Conditionally copies data in constant time
 * @param dest Destination buffer
 * @param src Source buffer
 * @param size Number of bytes to copy
 * @param condition Condition (0 = don't copy, non-zero = copy)
 */
void dap_const_time_move(void *dest, const void *src, size_t size, int condition);
