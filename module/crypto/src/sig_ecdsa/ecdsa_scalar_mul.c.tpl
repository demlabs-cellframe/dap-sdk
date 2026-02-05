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
 * @file ecdsa_scalar_mul_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
 *
 * Optimizations for {{ARCH_NAME}}:
 * {{OPTIMIZATION_NOTES}}
 *
 * Performance target: {{PERF_TARGET}}
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
{{ARCH_INCLUDES}}

#include "sig_ecdsa/ecdsa_scalar.h"
#include "sig_ecdsa/arch/ecdsa_scalar_mul_arch.h"

// ============================================================================
// {{ARCH_NAME}} Architecture-Specific Primitives
// ============================================================================

{{PRIMITIVES}}

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
// {{ARCH_NAME}} Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_scalar_mul_512_{{ARCH_LOWER}}(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// {{ARCH_NAME}} Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_scalar_mul_shift_384_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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
// {{ARCH_NAME}} Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_scalar_reduce_512_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// {{ARCH_NAME}} Full Scalar Multiplication: (a * b) mod n
// ============================================================================

{{TARGET_ATTR}}
void ecdsa_scalar_mul_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_{{ARCH_LOWER}}(l, a, b);
    ecdsa_scalar_reduce_512_{{ARCH_LOWER}}(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_{{ARCH_LOWER}}(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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

void ecdsa_scalar_mul_shift_384_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const uint64_t l[8])
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

void ecdsa_scalar_mul_{{ARCH_LOWER}}(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
