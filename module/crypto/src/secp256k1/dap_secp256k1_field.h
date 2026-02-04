/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2026
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

/**
 * @file dap_secp256k1_field.h
 * @brief Field arithmetic for secp256k1 (mod p)
 * @details Operations in the finite field F_p where:
 *   p = 2^256 - 2^32 - 2^9 - 2^8 - 2^7 - 2^6 - 2^4 - 1
 *     = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F
 *
 * Uses 5x52-bit limb representation for 64-bit platforms:
 *   n[0] + n[1]*2^52 + n[2]*2^104 + n[3]*2^156 + n[4]*2^208
 *
 * Or 10x26-bit limb representation for 32-bit platforms:
 *   n[0] + n[1]*2^26 + ... + n[9]*2^234
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Field Element Representation
// =============================================================================

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
    // 64-bit: use 5x52-bit limbs
    #define DAP_SECP256K1_FIELD_52BIT 1
    #define DAP_SECP256K1_FIELD_LIMBS 5
    typedef uint64_t dap_secp256k1_field_limb_t;
#else
    // 32-bit: use 10x26-bit limbs
    #define DAP_SECP256K1_FIELD_26BIT 1
    #define DAP_SECP256K1_FIELD_LIMBS 10
    typedef uint32_t dap_secp256k1_field_limb_t;
#endif

/**
 * @brief Field element structure
 * @note Magnitude indicates how far from normalized the value can be.
 *       normalized=true means value is in [0, p)
 */
typedef struct {
    dap_secp256k1_field_limb_t n[DAP_SECP256K1_FIELD_LIMBS];
#ifdef DAP_SECP256K1_DEBUG
    int magnitude;
    bool normalized;
#endif
} dap_secp256k1_field_t;

// =============================================================================
// Field Constants
// =============================================================================

/**
 * @brief Field prime p = 2^256 - 2^32 - 977
 */
extern const dap_secp256k1_field_t DAP_SECP256K1_FIELD_P;

/**
 * @brief Field element 0
 */
extern const dap_secp256k1_field_t DAP_SECP256K1_FIELD_ZERO;

/**
 * @brief Field element 1
 */
extern const dap_secp256k1_field_t DAP_SECP256K1_FIELD_ONE;

// =============================================================================
// Basic Operations
// =============================================================================

/**
 * @brief Set field element to zero
 */
void dap_secp256k1_field_clear(dap_secp256k1_field_t *r);

/**
 * @brief Set field element to an integer
 */
void dap_secp256k1_field_set_int(dap_secp256k1_field_t *r, int a);

/**
 * @brief Set field element from 32 bytes (big-endian)
 * @return true if value is in valid range [0, p)
 */
bool dap_secp256k1_field_set_b32(dap_secp256k1_field_t *r, const uint8_t *a);

/**
 * @brief Get 32-byte big-endian representation
 * @note Field element must be normalized
 */
void dap_secp256k1_field_get_b32(uint8_t *r, const dap_secp256k1_field_t *a);

/**
 * @brief Copy field element
 */
void dap_secp256k1_field_copy(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a);

/**
 * @brief Check if field element is zero
 */
bool dap_secp256k1_field_is_zero(const dap_secp256k1_field_t *a);

/**
 * @brief Check if field element is odd
 */
bool dap_secp256k1_field_is_odd(const dap_secp256k1_field_t *a);

/**
 * @brief Compare field elements
 * @return true if equal
 */
bool dap_secp256k1_field_equal(const dap_secp256k1_field_t *a, const dap_secp256k1_field_t *b);

// =============================================================================
// Arithmetic Operations
// =============================================================================

/**
 * @brief Normalize field element to [0, p)
 */
void dap_secp256k1_field_normalize(dap_secp256k1_field_t *r);

/**
 * @brief Weak normalize (reduce to < 2*p)
 */
void dap_secp256k1_field_normalize_weak(dap_secp256k1_field_t *r);

/**
 * @brief Negate: r = -a mod p
 */
void dap_secp256k1_field_negate(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a, int m);

/**
 * @brief Add: r = a + b mod p
 */
void dap_secp256k1_field_add(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a, const dap_secp256k1_field_t *b);

/**
 * @brief Multiply: r = a * b mod p
 */
void dap_secp256k1_field_mul(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a, const dap_secp256k1_field_t *b);

/**
 * @brief Square: r = a^2 mod p
 */
void dap_secp256k1_field_sqr(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a);

/**
 * @brief Multiply by small integer: r = a * n mod p
 */
void dap_secp256k1_field_mul_int(dap_secp256k1_field_t *r, int n);

/**
 * @brief Add in place: r += a
 */
void dap_secp256k1_field_add_in_place(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a);

// =============================================================================
// Advanced Operations
// =============================================================================

/**
 * @brief Invert: r = 1/a mod p
 * @note Uses Fermat's little theorem: a^(-1) = a^(p-2) mod p
 */
void dap_secp256k1_field_inv(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a);

/**
 * @brief Square root: r = sqrt(a) mod p, if exists
 * @return true if square root exists
 */
bool dap_secp256k1_field_sqrt(dap_secp256k1_field_t *r, const dap_secp256k1_field_t *a);

/**
 * @brief Check if a is a quadratic residue mod p
 */
bool dap_secp256k1_field_is_quad(const dap_secp256k1_field_t *a);

#ifdef __cplusplus
}
#endif
