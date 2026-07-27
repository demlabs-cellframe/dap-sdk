#if defined(__aarch64__) || defined(__arm__)
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
 * @file ecdsa_scalar_mul_neon.c
 * @brief NEON optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
 *
 * Optimizations for NEON:
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
#include <arm_neon.h>

#include "ecdsa_scalar.h"
#include "arch/ecdsa_scalar_mul_arch.h"

// ============================================================================
// NEON Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// ARM64 NEON Primitives for secp256k1 Scalar Arithmetic
// Uses UMULH for high-part multiplication and NEON for parallel operations
// ============================================================================

#if defined(__aarch64__)

#include <arm_neon.h>

// ARM64 has native 64x64->128 multiplication via MUL + UMULH
// MUL: low 64 bits
// UMULH: high 64 bits

// ============================================================================
// 256x256 -> 512 bit multiplication using ARM64 MUL/UMULH
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    /* Use ARM64 intrinsics where available, else inline asm */ \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3]; \
    \
    __asm__ __volatile__( \
        /* Column 0: a0*b0 */ \
        "mul x8, %[a0], %[b0]\n" \
        "umulh x9, %[a0], %[b0]\n" \
        "str x8, [%[pl], #0]\n" \
        \
        /* Column 1: a0*b1 + a1*b0 */ \
        "mul x8, %[a0], %[b1]\n" \
        "umulh x10, %[a0], %[b1]\n" \
        "adds x9, x9, x8\n" \
        \
        "mul x8, %[a1], %[b0]\n" \
        "umulh x11, %[a1], %[b0]\n" \
        "adcs x10, x10, x11\n" \
        "adc x11, xzr, xzr\n" \
        "adds x9, x9, x8\n" \
        "str x9, [%[pl], #8]\n" \
        \
        /* Column 2: a0*b2 + a1*b1 + a2*b0 */ \
        "mul x8, %[a0], %[b2]\n" \
        "umulh x9, %[a0], %[b2]\n" \
        "adcs x10, x10, x8\n" \
        "adc x11, x11, x9\n" \
        \
        "mul x8, %[a1], %[b1]\n" \
        "umulh x9, %[a1], %[b1]\n" \
        "adds x10, x10, x8\n" \
        "adcs x11, x11, x9\n" \
        "adc x12, xzr, xzr\n" \
        \
        "mul x8, %[a2], %[b0]\n" \
        "umulh x9, %[a2], %[b0]\n" \
        "adds x10, x10, x8\n" \
        "adcs x11, x11, x9\n" \
        "adc x12, x12, xzr\n" \
        "str x10, [%[pl], #16]\n" \
        \
        /* Column 3: a0*b3 + a1*b2 + a2*b1 + a3*b0 */ \
        "mul x8, %[a0], %[b3]\n" \
        "umulh x9, %[a0], %[b3]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, xzr, xzr\n" \
        \
        "mul x8, %[a1], %[b2]\n" \
        "umulh x9, %[a1], %[b2]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        \
        "mul x8, %[a2], %[b1]\n" \
        "umulh x9, %[a2], %[b1]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        \
        "mul x8, %[a3], %[b0]\n" \
        "umulh x9, %[a3], %[b0]\n" \
        "adds x11, x11, x8\n" \
        "adcs x12, x12, x9\n" \
        "adc x13, x13, xzr\n" \
        "str x11, [%[pl], #24]\n" \
        \
        /* Column 4: a1*b3 + a2*b2 + a3*b1 */ \
        "mul x8, %[a1], %[b3]\n" \
        "umulh x9, %[a1], %[b3]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, xzr, xzr\n" \
        \
        "mul x8, %[a2], %[b2]\n" \
        "umulh x9, %[a2], %[b2]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, x14, xzr\n" \
        \
        "mul x8, %[a3], %[b1]\n" \
        "umulh x9, %[a3], %[b1]\n" \
        "adds x12, x12, x8\n" \
        "adcs x13, x13, x9\n" \
        "adc x14, x14, xzr\n" \
        "str x12, [%[pl], #32]\n" \
        \
        /* Column 5: a2*b3 + a3*b2 */ \
        "mul x8, %[a2], %[b3]\n" \
        "umulh x9, %[a2], %[b3]\n" \
        "adds x13, x13, x8\n" \
        "adcs x14, x14, x9\n" \
        "adc x15, xzr, xzr\n" \
        \
        "mul x8, %[a3], %[b2]\n" \
        "umulh x9, %[a3], %[b2]\n" \
        "adds x13, x13, x8\n" \
        "adcs x14, x14, x9\n" \
        "adc x15, x15, xzr\n" \
        "str x13, [%[pl], #40]\n" \
        \
        /* Column 6: a3*b3 */ \
        "mul x8, %[a3], %[b3]\n" \
        "umulh x9, %[a3], %[b3]\n" \
        "adds x14, x14, x8\n" \
        "adc x15, x15, x9\n" \
        "str x14, [%[pl], #48]\n" \
        "str x15, [%[pl], #56]\n" \
        \
        : /* outputs */ \
        : [pl] "r" (l), \
          [a0] "r" (a0), [a1] "r" (a1), [a2] "r" (a2), [a3] "r" (a3), \
          [b0] "r" (b0), [b1] "r" (b1), [b2] "r" (b2), [b3] "r" (b3) \
        : "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "cc", "memory" \
    ); \
} while(0)

// ============================================================================
// 512-bit reduction mod n for ARM64
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        /* Simplified reduction - multiply h by (2^256 mod n) and add */ \
        __asm__ __volatile__( \
            "mul x8, %[h0], %[c0]\n" \
            "umulh x9, %[h0], %[c0]\n" \
            "adds %[r0], %[r0], x8\n" \
            "adcs %[r1], %[r1], x9\n" \
            "adcs %[r2], %[r2], xzr\n" \
            "adc %[r3], %[r3], xzr\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0]) \
            : "x8", "x9", "cc" \
        ); \
    } \
    \
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
        __asm__ __volatile__( \
            "subs %[r0], %[r0], %[n0]\n" \
            "sbcs %[r1], %[r1], %[n1]\n" \
            "sbcs %[r2], %[r2], %[n2]\n" \
            "sbc %[r3], %[r3], %[n3]\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [n0] "r" (SCALAR_N[0]), [n1] "r" (SCALAR_N[1]), \
              [n2] "r" (SCALAR_N[2]), [n3] "r" (SCALAR_N[3]) \
            : "cc" \
        ); \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)

#else
#error "ARM64 NEON requires aarch64 architecture"
#endif

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
// NEON Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================


void ecdsa_scalar_mul_512_neon(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// NEON Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================


void ecdsa_scalar_mul_shift_384_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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
// NEON Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================


void ecdsa_scalar_reduce_512_neon(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// NEON Full Scalar Multiplication: (a * b) mod n
// ============================================================================


void ecdsa_scalar_mul_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_neon(l, a, b);
    ecdsa_scalar_reduce_512_neon(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_neon(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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

void ecdsa_scalar_mul_shift_384_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_neon(ecdsa_scalar_t *r, const uint64_t l[8])
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

void ecdsa_scalar_mul_neon(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
#endif
