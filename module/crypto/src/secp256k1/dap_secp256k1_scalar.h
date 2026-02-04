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
 * @file dap_secp256k1_scalar.h
 * @brief Scalar arithmetic for secp256k1 (mod n)
 * @details Operations in the scalar field Z_n where:
 *   n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
 *
 * Scalars are 256-bit integers used as:
 *   - Private keys
 *   - Signature components (r, s)
 *   - Nonces
 *
 * Uses 4x64-bit or 8x32-bit representation depending on platform.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Scalar Representation
// =============================================================================

#if defined(__LP64__) || defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__)
    // 64-bit: use 4x64-bit limbs
    #define DAP_SECP256K1_SCALAR_64BIT 1
    #define DAP_SECP256K1_SCALAR_LIMBS 4
    typedef uint64_t dap_secp256k1_scalar_limb_t;
#else
    // 32-bit: use 8x32-bit limbs
    #define DAP_SECP256K1_SCALAR_32BIT 1
    #define DAP_SECP256K1_SCALAR_LIMBS 8
    typedef uint32_t dap_secp256k1_scalar_limb_t;
#endif

/**
 * @brief Scalar element structure
 * @note Always in range [0, n)
 */
typedef struct {
    dap_secp256k1_scalar_limb_t d[DAP_SECP256K1_SCALAR_LIMBS];
} dap_secp256k1_scalar_t;

// =============================================================================
// Scalar Constants
// =============================================================================

/**
 * @brief Curve order n
 */
extern const dap_secp256k1_scalar_t DAP_SECP256K1_SCALAR_N;

/**
 * @brief Half curve order (n/2, for low-S normalization)
 */
extern const dap_secp256k1_scalar_t DAP_SECP256K1_SCALAR_N_HALF;

/**
 * @brief Scalar zero
 */
extern const dap_secp256k1_scalar_t DAP_SECP256K1_SCALAR_ZERO;

/**
 * @brief Scalar one
 */
extern const dap_secp256k1_scalar_t DAP_SECP256K1_SCALAR_ONE;

// =============================================================================
// Basic Operations
// =============================================================================

/**
 * @brief Set scalar to zero
 */
void dap_secp256k1_scalar_clear(dap_secp256k1_scalar_t *r);

/**
 * @brief Set scalar from 32-byte big-endian
 * @param overflow If not NULL, set to 1 if value was >= n
 */
void dap_secp256k1_scalar_set_b32(dap_secp256k1_scalar_t *r, const uint8_t *b32, int *overflow);

/**
 * @brief Get 32-byte big-endian representation
 */
void dap_secp256k1_scalar_get_b32(uint8_t *b32, const dap_secp256k1_scalar_t *a);

/**
 * @brief Set scalar from integer
 */
void dap_secp256k1_scalar_set_int(dap_secp256k1_scalar_t *r, unsigned int v);

/**
 * @brief Copy scalar
 */
void dap_secp256k1_scalar_copy(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a);

/**
 * @brief Check if scalar is zero
 */
bool dap_secp256k1_scalar_is_zero(const dap_secp256k1_scalar_t *a);

/**
 * @brief Check if scalar is one
 */
bool dap_secp256k1_scalar_is_one(const dap_secp256k1_scalar_t *a);

/**
 * @brief Check if scalar is high (> n/2, for low-S check)
 */
bool dap_secp256k1_scalar_is_high(const dap_secp256k1_scalar_t *a);

/**
 * @brief Compare scalars
 * @return true if equal
 */
bool dap_secp256k1_scalar_equal(const dap_secp256k1_scalar_t *a, const dap_secp256k1_scalar_t *b);

// =============================================================================
// Arithmetic Operations
// =============================================================================

/**
 * @brief Negate: r = -a mod n
 */
void dap_secp256k1_scalar_negate(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a);

/**
 * @brief Add: r = a + b mod n
 * @return 1 if overflow occurred
 */
int dap_secp256k1_scalar_add(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a, const dap_secp256k1_scalar_t *b);

/**
 * @brief Subtract: r = a - b mod n
 */
void dap_secp256k1_scalar_sub(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a, const dap_secp256k1_scalar_t *b);

/**
 * @brief Multiply: r = a * b mod n
 */
void dap_secp256k1_scalar_mul(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a, const dap_secp256k1_scalar_t *b);

/**
 * @brief Square: r = a^2 mod n
 */
void dap_secp256k1_scalar_sqr(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a);

/**
 * @brief Invert: r = 1/a mod n
 */
void dap_secp256k1_scalar_inv(dap_secp256k1_scalar_t *r, const dap_secp256k1_scalar_t *a);

/**
 * @brief Conditional negate: r = a if flag==0, -a if flag==1
 */
void dap_secp256k1_scalar_cond_negate(dap_secp256k1_scalar_t *r, int flag);

// =============================================================================
// Conversion
// =============================================================================

/**
 * @brief Reduce 512-bit value mod n
 * @param b64 64-byte big-endian value
 */
void dap_secp256k1_scalar_set_b64(dap_secp256k1_scalar_t *r, const uint8_t *b64);

/**
 * @brief Check if 32-byte value is valid secret key (0 < k < n)
 */
bool dap_secp256k1_scalar_check_seckey(const uint8_t *seckey);

#ifdef __cplusplus
}
#endif
