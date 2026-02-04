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
 * @file dap_secp256k1_group.h
 * @brief Group operations for secp256k1 elliptic curve
 * @details Operations on the elliptic curve E: y² = x³ + 7 (mod p)
 *
 * Points are represented in Jacobian projective coordinates:
 *   (X, Y, Z) represents affine point (X/Z², Y/Z³)
 *   Z=0 represents the point at infinity
 *
 * This allows addition and doubling without field inversions.
 */

#pragma once

#include "dap_secp256k1_field.h"
#include "dap_secp256k1_scalar.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Point Representations
// =============================================================================

/**
 * @brief Point in Jacobian coordinates (X, Y, Z)
 * @note Affine point is (X/Z², Y/Z³), infinity has Z=0
 */
typedef struct {
    dap_secp256k1_field_t x;
    dap_secp256k1_field_t y;
    dap_secp256k1_field_t z;
    bool infinity;  // True if point at infinity
} dap_secp256k1_gej_t;

/**
 * @brief Point in affine coordinates (x, y)
 * @note Only used for input/output, Jacobian used internally
 */
typedef struct {
    dap_secp256k1_field_t x;
    dap_secp256k1_field_t y;
    bool infinity;
} dap_secp256k1_ge_t;

/**
 * @brief Point with precomputed z^-1 (for batch inversion)
 */
typedef struct {
    dap_secp256k1_field_t x;
    dap_secp256k1_field_t y;
    dap_secp256k1_field_t zinv;  // 1/z for conversion from Jacobian
} dap_secp256k1_gei_t;

// =============================================================================
// Generator Point
// =============================================================================

/**
 * @brief Generator point G for secp256k1
 * @note G.x = 0x79BE667EF9DCBBAC55A06295CE870B07029BFCDB2DCE28D959F2815B16F81798
 *       G.y = 0x483ADA7726A3C4655DA4FBFC0E1108A8FD17B448A68554199C47D08FFB10D4B8
 */
extern const dap_secp256k1_ge_t DAP_SECP256K1_GENERATOR;

// =============================================================================
// Basic Operations (Affine)
// =============================================================================

/**
 * @brief Set point to infinity (identity)
 */
void dap_secp256k1_ge_set_infinity(dap_secp256k1_ge_t *r);

/**
 * @brief Set affine point from coordinates
 * @return true if point is on curve
 */
bool dap_secp256k1_ge_set_xy(dap_secp256k1_ge_t *r, const dap_secp256k1_field_t *x, const dap_secp256k1_field_t *y);

/**
 * @brief Set affine point from x coordinate (compute y)
 * @param odd If true, use odd y; if false, use even y
 * @return true if x is valid (quadratic residue)
 */
bool dap_secp256k1_ge_set_xo(dap_secp256k1_ge_t *r, const dap_secp256k1_field_t *x, bool odd);

/**
 * @brief Check if point is on curve
 */
bool dap_secp256k1_ge_is_valid(const dap_secp256k1_ge_t *a);

/**
 * @brief Check if point is infinity
 */
bool dap_secp256k1_ge_is_infinity(const dap_secp256k1_ge_t *a);

/**
 * @brief Negate point: r = -a
 */
void dap_secp256k1_ge_neg(dap_secp256k1_ge_t *r, const dap_secp256k1_ge_t *a);

// =============================================================================
// Basic Operations (Jacobian)
// =============================================================================

/**
 * @brief Set Jacobian point to infinity
 */
void dap_secp256k1_gej_set_infinity(dap_secp256k1_gej_t *r);

/**
 * @brief Set Jacobian point from affine point
 */
void dap_secp256k1_gej_set_ge(dap_secp256k1_gej_t *r, const dap_secp256k1_ge_t *a);

/**
 * @brief Convert Jacobian to affine (requires field inversion)
 */
void dap_secp256k1_ge_set_gej(dap_secp256k1_ge_t *r, const dap_secp256k1_gej_t *a);

/**
 * @brief Check if Jacobian point is infinity
 */
bool dap_secp256k1_gej_is_infinity(const dap_secp256k1_gej_t *a);

/**
 * @brief Negate Jacobian point
 */
void dap_secp256k1_gej_neg(dap_secp256k1_gej_t *r, const dap_secp256k1_gej_t *a);

/**
 * @brief Compare Jacobian point with affine point
 */
bool dap_secp256k1_gej_eq_ge(const dap_secp256k1_gej_t *a, const dap_secp256k1_ge_t *b);

// =============================================================================
// Point Addition/Doubling
// =============================================================================

/**
 * @brief Point doubling: r = 2*a
 * @note Uses complete formula valid for all inputs
 */
void dap_secp256k1_gej_double(dap_secp256k1_gej_t *r, const dap_secp256k1_gej_t *a);

/**
 * @brief Point addition: r = a + b (Jacobian + Affine)
 * @note Faster than Jacobian + Jacobian
 */
void dap_secp256k1_gej_add_ge(dap_secp256k1_gej_t *r, const dap_secp256k1_gej_t *a, const dap_secp256k1_ge_t *b);

/**
 * @brief Point addition: r = a + b (both Jacobian)
 */
void dap_secp256k1_gej_add(dap_secp256k1_gej_t *r, const dap_secp256k1_gej_t *a, const dap_secp256k1_gej_t *b);

// =============================================================================
// Scalar Multiplication
// =============================================================================

/**
 * @brief Scalar multiplication: r = n * G (generator)
 * @note Uses precomputed tables for speed
 */
void dap_secp256k1_ecmult_gen(dap_secp256k1_gej_t *r, const dap_secp256k1_scalar_t *n);

/**
 * @brief Scalar multiplication: r = na * A + ng * G
 * @note General form for verification: r = s^-1 * (e*G + r*P)
 */
void dap_secp256k1_ecmult(
    dap_secp256k1_gej_t *r,
    const dap_secp256k1_gej_t *a,
    const dap_secp256k1_scalar_t *na,
    const dap_secp256k1_scalar_t *ng
);

/**
 * @brief Constant-time scalar multiplication: r = n * P
 * @note Side-channel resistant, for operations with secret scalars
 */
void dap_secp256k1_ecmult_const(
    dap_secp256k1_gej_t *r,
    const dap_secp256k1_ge_t *a,
    const dap_secp256k1_scalar_t *n
);

// =============================================================================
// Serialization
// =============================================================================

/**
 * @brief Serialize affine point (compressed or uncompressed)
 * @param compressed If true, output 33 bytes; if false, 65 bytes
 * @param output Output buffer
 * @param outputlen In: buffer size, Out: written size
 * @return true on success
 */
bool dap_secp256k1_ge_serialize(
    const dap_secp256k1_ge_t *a,
    bool compressed,
    uint8_t *output,
    size_t *outputlen
);

/**
 * @brief Parse serialized point (auto-detect format)
 * @param input 33 or 65 bytes
 * @param inputlen Input length
 * @return true if valid point
 */
bool dap_secp256k1_ge_parse(
    dap_secp256k1_ge_t *r,
    const uint8_t *input,
    size_t inputlen
);

// =============================================================================
// Precomputation Tables (for fast ecmult)
// =============================================================================

/**
 * @brief Initialize precomputation tables for generator
 * @note Call once at startup
 */
void dap_secp256k1_ecmult_gen_init(void);

/**
 * @brief Free precomputation tables
 */
void dap_secp256k1_ecmult_gen_deinit(void);

#ifdef __cplusplus
}
#endif
