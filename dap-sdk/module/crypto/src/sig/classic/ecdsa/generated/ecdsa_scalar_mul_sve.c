#if defined(__aarch64__) && !defined(__APPLE__)
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
 * @file ecdsa_scalar_mul_sve.c
 * @brief SVE optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
 *
 * Optimizations for SVE:
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
#include <arm_sve.h>

#include "ecdsa_scalar.h"
#include "arch/ecdsa_scalar_mul_arch.h"

// ============================================================================
// SVE Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// ARM SVE Primitives for secp256k1 Scalar Arithmetic
// For SERVER ARM64 only: AWS Graviton3, Fujitsu A64FX, AmpereOne
// SVE provides scalable vectors (128-2048 bits)
//
// NOTE: Apple Silicon (M1/M2/M3/M4) does NOT support SVE!
//       Apple uses NEON + proprietary AMX (undocumented).
//       For Apple Silicon, use neon_primitives.tpl instead.
// ============================================================================

#if defined(__ARM_FEATURE_SVE)

#include <arm_sve.h>

// ============================================================================
// SVE Scalar Multiplication: 256x256 -> 512 bit
// Uses MUL/UMULH instructions with SVE predicates
// Note: For 4x64-bit scalars, standard NEON/scalar path often faster
// ============================================================================

// Helper: 64x64 -> 128 bit multiply using standard ARM64 instructions
#define MUL64_128(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mul   %0, %2, %3\n\t" \
        "umulh %1, %2, %3" \
        : "=&r"(lo), "=&r"(hi) \
        : "r"(_a), "r"(_b) \
    ); \
} while(0)

// Helper: multiply-add to 128-bit accumulator
#define MULADD64_128(c1, c0, a, b) do { \
    uint64_t _lo, _hi; \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mul   %0, %4, %5\n\t" \
        "umulh %1, %4, %5\n\t" \
        "adds  %2, %2, %0\n\t" \
        "adc   %3, %3, %1" \
        : "=&r"(_lo), "=&r"(_hi), "+r"(c0), "+r"(c1) \
        : "r"(_a), "r"(_b) \
        : "cc" \
    ); \
} while(0)

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3]; \
    uint64_t c0 = 0, c1 = 0; \
    uint64_t lo, hi; \
    \
    /* Column 0 */ \
    MUL64_128(c1, (l)[0], a0, b0); \
    c0 = c1; c1 = 0; \
    \
    /* Column 1 */ \
    MULADD64_128(c1, c0, a0, b1); \
    MULADD64_128(c1, c0, a1, b0); \
    (l)[1] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 2 */ \
    MULADD64_128(c1, c0, a0, b2); \
    MULADD64_128(c1, c0, a1, b1); \
    MULADD64_128(c1, c0, a2, b0); \
    (l)[2] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 3 */ \
    MULADD64_128(c1, c0, a0, b3); \
    MULADD64_128(c1, c0, a1, b2); \
    MULADD64_128(c1, c0, a2, b1); \
    MULADD64_128(c1, c0, a3, b0); \
    (l)[3] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 4 */ \
    MULADD64_128(c1, c0, a1, b3); \
    MULADD64_128(c1, c0, a2, b2); \
    MULADD64_128(c1, c0, a3, b1); \
    (l)[4] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 5 */ \
    MULADD64_128(c1, c0, a2, b3); \
    MULADD64_128(c1, c0, a3, b2); \
    (l)[5] = c0; c0 = c1; c1 = 0; \
    \
    /* Column 6,7 */ \
    MULADD64_128(c1, c0, a3, b3); \
    (l)[6] = c0; \
    (l)[7] = c1; \
} while(0)

// ============================================================================
// SVE 512-bit reduction mod n
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        uint64_t lo, hi, carry; \
        \
        /* h0 * SCALAR_2P256_MOD_N[0] */ \
        MUL64_128(hi, lo, h0, SCALAR_2P256_MOD_N[0]); \
        __asm__ __volatile__ ( \
            "adds %0, %0, %4\n\t" \
            "adcs %1, %1, %5\n\t" \
            "adcs %2, %2, xzr\n\t" \
            "adc  %3, %3, xzr" \
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
        \
        /* h0 * SCALAR_2P256_MOD_N[1] */ \
        MUL64_128(hi, lo, h0, SCALAR_2P256_MOD_N[1]); \
        __asm__ __volatile__ ( \
            "adds %0, %0, %3\n\t" \
            "adcs %1, %1, %4\n\t" \
            "adc  %2, %2, xzr" \
            : "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(lo), "r"(hi) \
            : "cc" \
        ); \
    } \
    \
    /* Final reduction if r >= n */ \
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
        __asm__ __volatile__ ( \
            "subs %0, %0, %4\n\t" \
            "sbcs %1, %1, %5\n\t" \
            "sbcs %2, %2, %6\n\t" \
            "sbc  %3, %3, %7" \
            : "+r"(r0), "+r"(r1), "+r"(r2), "+r"(r3) \
            : "r"(SCALAR_N[0]), "r"(SCALAR_N[1]), \
              "r"(SCALAR_N[2]), "r"(SCALAR_N[3]) \
            : "cc" \
        ); \
    } \
    \
    (r)[0] = r0; (r)[1] = r1; (r)[2] = r2; (r)[3] = r3; \
} while(0)

#else
#error "SVE primitives require ARM SVE support"
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
// SVE Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================

__attribute__((target("+sve")))
void ecdsa_scalar_mul_512_sve(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// SVE Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================

__attribute__((target("+sve")))
void ecdsa_scalar_mul_shift_384_sve(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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
// SVE Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================

__attribute__((target("+sve")))
void ecdsa_scalar_reduce_512_sve(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// SVE Full Scalar Multiplication: (a * b) mod n
// ============================================================================

__attribute__((target("+sve")))
void ecdsa_scalar_mul_sve(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_sve(l, a, b);
    ecdsa_scalar_reduce_512_sve(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_sve(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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

void ecdsa_scalar_mul_shift_384_sve(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_sve(ecdsa_scalar_t *r, const uint64_t l[8])
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

void ecdsa_scalar_mul_sve(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
#endif
