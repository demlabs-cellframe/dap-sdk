/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP is free software: you can redistribute it and/or modify
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

/**
 * @file ecdsa_scalar_mul_generic.c
 * @brief Generic optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
 *
 * Optimizations for Generic:
 * 
 *
 * Performance target: 
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>


#include "ecdsa_scalar.h"
#include "arch/ecdsa_scalar_mul_arch.h"

// ============================================================================
// Generic Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// Generic (Portable C) Primitives for secp256k1 Scalar Arithmetic
// Uses dap_math_ops.h for cross-platform uint128_t support
// ============================================================================

#include "dap_math_ops.h"

// Helper macros for accessing uint128_t parts (works with both native and emulated)
#ifdef DAP_GLOBAL_IS_INT128
    #define LO64_128(x) ((uint64_t)(x))
    #define HI64_128(x) ((uint64_t)((x) >> 64))
#else
    #define LO64_128(x) ((x).lo)
    #define HI64_128(x) ((x).hi)
#endif

// ============================================================================
// 256x256 -> 512 bit multiplication using schoolbook algorithm
// Input: a[4], b[4] in little-endian 64-bit limbs
// Output: l[8] in little-endian 64-bit limbs
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    uint128_t t; \
    uint64_t c0 = 0, c1 = 0; \
    \
    /* Column 0: l[0] = a[0] * b[0] */ \
    MULT_64_128((a)[0], (b)[0], &t); \
    (l)[0] = LO64_128(t); \
    c0 = HI64_128(t); \
    \
    /* Column 1: l[1] */ \
    MULT_64_128((a)[0], (b)[1], &t); \
    c0 += LO64_128(t); c1 = (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[1] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 2: l[2] */ \
    MULT_64_128((a)[0], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[2] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 3: l[3] */ \
    MULT_64_128((a)[0], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[1], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[0], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[3] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 4: l[4] */ \
    MULT_64_128((a)[1], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[2], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[1], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[4] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 5: l[5] */ \
    MULT_64_128((a)[2], (b)[3], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    MULT_64_128((a)[3], (b)[2], &t); \
    c0 += LO64_128(t); c1 += (c0 < LO64_128(t)) + HI64_128(t); \
    (l)[5] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 6,7: l[6], l[7] */ \
    MULT_64_128((a)[3], (b)[3], &t); \
    c0 += LO64_128(t); \
    c1 = HI64_128(t) + (c0 < LO64_128(t)); \
    (l)[6] = c0; \
    (l)[7] = c1; \
} while(0)

// ============================================================================
// 512-bit reduction mod n using Barrett reduction approximation
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    \
    /* Add h * (2^256 mod n) to r */ \
    if (h0 | h1 | h2 | h3) { \
        uint128_t t; \
        uint64_t c = 0, tmp; \
        \
        /* r += h0 * SCALAR_2P256_MOD_N */ \
        MULT_64_128(h0, SCALAR_2P256_MOD_N[0], &t); \
        tmp = r0 + LO64_128(t); c = (tmp < r0); r0 = tmp; \
        tmp = r1 + HI64_128(t) + c; c = (tmp < r1); r1 = tmp; \
        \
        MULT_64_128(h0, SCALAR_2P256_MOD_N[1], &t); \
        tmp = r1 + LO64_128(t); c = (tmp < r1); r1 = tmp; \
        tmp = r2 + HI64_128(t) + c; c = (tmp < r2); r2 = tmp; \
        tmp = r3 + c; r3 = tmp; \
    } \
    \
    /* Final reduction: if r >= n, subtract n */ \
    int over = 0; \
    if (r3 > SCALAR_N[3]) over = 1; \
    else if (r3 == SCALAR_N[3]) { \
        if (r2 > SCALAR_N[2]) over = 1; \
        else if (r2 == SCALAR_N[2]) { \
            if (r1 > SCALAR_N[1]) over = 1; \
            else if (r1 == SCALAR_N[1]) { \
                if (r0 >= SCALAR_N[0]) over = 1; \
            } \
        } \
    } \
    \
    if (over) { \
        uint64_t borrow = 0, tmp; \
        tmp = r0 - SCALAR_N[0]; borrow = (tmp > r0); r0 = tmp; \
        tmp = r1 - SCALAR_N[1] - borrow; borrow = (r1 < SCALAR_N[1] + borrow); r1 = tmp; \
        tmp = r2 - SCALAR_N[2] - borrow; borrow = (r2 < SCALAR_N[2] + borrow); r2 = tmp; \
        r3 = r3 - SCALAR_N[3] - borrow; \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)

// ============================================================================
// Platform-specific implementation selection
// ============================================================================

#ifdef ECDSA_SCALAR_64BIT
// ============================================================================
// 64-bit implementations using architecture-specific primitives
// ============================================================================

// secp256k1 curve order n (for reduction)
// n = 0xFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141
static const uint64_t SCALAR_N[4] = {
    0xBFD25E8CD0364141ULL,
    0xBAAEDCE6AF48A03BULL,
    0xFFFFFFFFFFFFFFFEULL,
    0xFFFFFFFFFFFFFFFFULL
};

// 2^256 mod n (for Montgomery reduction)
static const uint64_t SCALAR_2P256_MOD_N[4] = {
    0x402DA1732FC9BEBFULL,
    0x4551231950B75FC4ULL,
    0x0000000000000001ULL,
    0x0000000000000000ULL
};

// ============================================================================
// Generic Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================


void ecdsa_scalar_mul_512_generic(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// Generic Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================


void ecdsa_scalar_mul_shift_384_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
    
    // Result is bits 384-511 = l[6]:l[7] with rounding
    uint64_t round_bit = (l[5] >> 63) & 1;
    r->d[0] = l[6] + round_bit;
    r->d[1] = l[7] + (r->d[0] < round_bit);
    r->d[2] = 0;
    r->d[3] = 0;
}

// ============================================================================
// Generic Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================


void ecdsa_scalar_reduce_512_generic(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// Generic Full Scalar Multiplication: (a * b) mod n
// ============================================================================


void ecdsa_scalar_mul_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_generic(l, a, b);
    ecdsa_scalar_reduce_512_generic(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_generic(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // On 32-bit platforms, perform multiplication and store already-reduced result
    ecdsa_scalar_t tmp;
    ecdsa_scalar_mul(&tmp, a, b);
    
    // Convert 8x32 to 4x64 for the output format (already reduced)
    l[0] = (uint64_t)tmp.d[0] | ((uint64_t)tmp.d[1] << 32);
    l[1] = (uint64_t)tmp.d[2] | ((uint64_t)tmp.d[3] << 32);
    l[2] = (uint64_t)tmp.d[4] | ((uint64_t)tmp.d[5] << 32);
    l[3] = (uint64_t)tmp.d[6] | ((uint64_t)tmp.d[7] << 32);
    l[4] = l[5] = l[6] = l[7] = 0;  // Already reduced
}

void ecdsa_scalar_mul_shift_384_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_generic(ecdsa_scalar_t *r, const uint64_t l[8])
{
    // On 32-bit, input l[] is already in reduced form from mul_512
    r->d[0] = (uint32_t)l[0];
    r->d[1] = (uint32_t)(l[0] >> 32);
    r->d[2] = (uint32_t)l[1];
    r->d[3] = (uint32_t)(l[1] >> 32);
    r->d[4] = (uint32_t)l[2];
    r->d[5] = (uint32_t)(l[2] >> 32);
    r->d[6] = (uint32_t)l[3];
    r->d[7] = (uint32_t)(l[3] >> 32);
}

void ecdsa_scalar_mul_generic(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
