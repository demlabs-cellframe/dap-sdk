#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
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
 * @file ecdsa_field_avx2_bmi2.c
 * @brief AVX2+BMI2 optimized secp256k1 field multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - field_mul: Field multiplication with interleaved reduction
 *   - field_sqr: Field squaring with interleaved reduction
 *
 * Optimizations for AVX2+BMI2:
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
#include <immintrin.h>

#include "ecdsa_field.h"
#include "ecdsa_field_ref.h"
#include "arch/ecdsa_field_arch.h"

// ============================================================================
// AVX2+BMI2 Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// AVX2 + BMI2 Primitives for secp256k1 Field Arithmetic
// Uses MULX for fast multiplication, __uint128_t for accumulation
// ============================================================================

#include <immintrin.h>
#include <x86intrin.h>

// secp256k1 field constants
#define FIELD_M52 0xFFFFFFFFFFFFFULL
#define FIELD_R   0x1000003D10ULL

// ============================================================================
// BMI2 MULX: a * b -> hi:lo (faster than regular MUL on modern CPUs)
// ============================================================================

#if defined(__BMI2__)
#define MULX64(hi, lo, a, b) do { \
    uint64_t _a = (a), _b = (b); \
    __asm__ __volatile__ ( \
        "mulx %3, %0, %1" \
        : "=r"(lo), "=r"(hi) \
        : "d"(_a), "rm"(_b) \
    ); \
} while(0)
#else
#define MULX64(hi, lo, a, b) do { \
    __uint128_t _p = (__uint128_t)(a) * (b); \
    (lo) = (uint64_t)_p; \
    (hi) = (uint64_t)(_p >> 64); \
} while(0)
#endif

// ============================================================================
// Helper: accumulate product into 128-bit accumulator
// ============================================================================
static inline void accum_mul(__uint128_t *acc, uint64_t a, uint64_t b) {
    uint64_t lo, hi;
    MULX64(hi, lo, a, b);
    *acc += ((__uint128_t)hi << 64) | lo;
}

// ============================================================================
// AVX2+BMI2 Field Multiplication with Interleaved Reduction
// Uses MULX for multiplication, __uint128_t for correct accumulation
// ============================================================================

#define FIELD_MUL_IMPL(r, a, b) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    uint64_t b0 = (b)[0], b1 = (b)[1], b2 = (b)[2], b3 = (b)[3], b4 = (b)[4]; \
    const uint64_t M = FIELD_M52, R = FIELD_R; \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    \
    /* [d 0 0 0] = [p3 0 0 0] */ \
    d = (__uint128_t)a0 * b3; \
    d += (__uint128_t)a1 * b2; \
    d += (__uint128_t)a2 * b1; \
    d += (__uint128_t)a3 * b0; \
    \
    /* [c 0 0 0 0 d 0 0 0] = [p8 0 0 0 0 p3 0 0 0] */ \
    c = (__uint128_t)a4 * b4; \
    \
    /* Reduce p8: d += (p8 mod 2^64) * R */ \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    /* [(c<<12) 0 0 0 0 0 d 0 0 0] */ \
    t3 = (uint64_t)d & M; d >>= 52; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] */ \
    \
    /* Compute p4 = a0*b4 + a1*b3 + a2*b2 + a3*b1 + a4*b0 */ \
    d += (__uint128_t)a0 * b4; \
    d += (__uint128_t)a1 * b3; \
    d += (__uint128_t)a2 * b2; \
    d += (__uint128_t)a3 * b1; \
    d += (__uint128_t)a4 * b0; \
    /* Add remaining high bits: (c<<12) * R */ \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    /* [d t3 0 0 0] */ \
    t4 = (uint64_t)d & M; d >>= 52; \
    /* [d t4 t3 0 0 0] */ \
    tx = t4 >> 48; t4 &= (M >> 4); \
    /* [d t4+(tx<<48) t3 0 0 0] */ \
    \
    /* [d t4+(tx<<48) t3 0 0 c] = start computing p0 */ \
    c = (__uint128_t)a0 * b0; \
    \
    /* Compute p5 = a1*b4 + a2*b3 + a3*b2 + a4*b1 */ \
    d += (__uint128_t)a1 * b4; \
    d += (__uint128_t)a2 * b3; \
    d += (__uint128_t)a3 * b2; \
    d += (__uint128_t)a4 * b1; \
    /* [d t4+(tx<<48) t3 0 0 c] */ \
    u0 = (uint64_t)d & M; d >>= 52; \
    /* [d u0 t4+(tx<<48) t3 0 0 c] */ \
    u0 = (u0 << 4) | tx; \
    /* [d 0 t4+(u0<<48) t3 0 0 c] */ \
    /* Reduce: c += u0 * (R >> 4) */ \
    c += (__uint128_t)u0 * (R >> 4); \
    /* [d 0 t4 t3 0 0 c] */ \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 t4 t3 0 c r0] */ \
    \
    /* Compute p1 = a0*b1 + a1*b0 */ \
    c += (__uint128_t)a0 * b1; \
    c += (__uint128_t)a1 * b0; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 0 p5 p4 p3 0 p1 p0] */ \
    \
    /* Compute p6 = a2*b4 + a3*b3 + a4*b2 */ \
    d += (__uint128_t)a2 * b4; \
    d += (__uint128_t)a3 * b3; \
    d += (__uint128_t)a4 * b2; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 p6 p5 p4 p3 0 p1 p0] */ \
    /* Reduce p6: c += (d mod M) * R */ \
    c += (__uint128_t)((uint64_t)d & M) * R; d >>= 52; \
    /* [d 0 0 t4 t3 0 c r0] */ \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 0 t4 t3 c r1 r0] */ \
    \
    /* Compute p2 = a0*b2 + a1*b1 + a2*b0 */ \
    c += (__uint128_t)a0 * b2; \
    c += (__uint128_t)a1 * b1; \
    c += (__uint128_t)a2 * b0; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 0 p6 p5 p4 p3 p2 p1 p0] */ \
    \
    /* Compute p7 = a3*b4 + a4*b3 */ \
    d += (__uint128_t)a3 * b4; \
    d += (__uint128_t)a4 * b3; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 p7 p6 p5 p4 p3 p2 p1 p0] */ \
    /* Reduce p7: c += (d mod 2^64) * R */ \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    /* [(d<<12) 0 0 0 t4 t3 c r1 r0] */ \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    /* [(d<<12) 0 0 0 t4 t3+c r2 r1 r0] */ \
    \
    /* Final: add remaining high bits and t3 */ \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    /* [t4 c r2 r1 r0] */ \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    /* [t4+c r3 r2 r1 r0] */ \
    (r)[4] = (uint64_t)c + t4; \
    /* [r4 r3 r2 r1 r0] */ \
} while(0)

// ============================================================================
// AVX2+BMI2 Field Squaring with Interleaved Reduction
// ============================================================================

#define FIELD_SQR_IMPL(r, a) do { \
    uint64_t a0 = (a)[0], a1 = (a)[1], a2 = (a)[2], a3 = (a)[3], a4 = (a)[4]; \
    const uint64_t M = FIELD_M52, R = FIELD_R; \
    __uint128_t c, d; \
    uint64_t t3, t4, tx, u0; \
    \
    /* p3 = 2*a0*a3 + 2*a1*a2 */ \
    d = (__uint128_t)(a0*2) * a3; \
    d += (__uint128_t)(a1*2) * a2; \
    /* [d 0 0 0] = [p3 0 0 0] */ \
    \
    /* p8 = a4*a4 */ \
    c = (__uint128_t)a4 * a4; \
    /* [c 0 0 0 0 d 0 0 0] = [p8 0 0 0 0 p3 0 0 0] */ \
    \
    /* Reduce p8 */ \
    d += (__uint128_t)((uint64_t)c) * R; c >>= 64; \
    /* [(c<<12) 0 0 0 0 0 d 0 0 0] */ \
    t3 = (uint64_t)d & M; d >>= 52; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] */ \
    \
    /* p4 = 2*a0*a4 + 2*a1*a3 + a2*a2 */ \
    a4 *= 2; \
    d += (__uint128_t)a0 * a4; \
    d += (__uint128_t)(a1*2) * a3; \
    d += (__uint128_t)a2 * a2; \
    /* [(c<<12) 0 0 0 0 d t3 0 0 0] = [p8 0 0 0 p4 p3 0 0 0] */ \
    d += (__uint128_t)((uint64_t)c) * (R << 12); \
    /* [d t3 0 0 0] = [p8 0 0 0 p4 p3 0 0 0] */ \
    t4 = (uint64_t)d & M; d >>= 52; \
    /* [d t4 t3 0 0 0] */ \
    tx = t4 >> 48; t4 &= (M >> 4); \
    /* [d t4+(tx<<48) t3 0 0 0] */ \
    \
    /* p0 = a0*a0 */ \
    c = (__uint128_t)a0 * a0; \
    /* [d t4+(tx<<48) t3 0 0 c] = [p8 0 0 0 p4 p3 0 0 p0] */ \
    \
    /* p5 = 2*a1*a4 + 2*a2*a3 */ \
    d += (__uint128_t)a1 * a4; \
    d += (__uint128_t)(a2*2) * a3; \
    /* [d t4+(tx<<48) t3 0 0 c] = [p8 0 0 p5 p4 p3 0 0 p0] */ \
    u0 = (uint64_t)d & M; d >>= 52; \
    /* [d u0 t4+(tx<<48) t3 0 0 c] */ \
    u0 = (u0 << 4) | tx; \
    /* [d 0 t4+(u0<<48) t3 0 0 c] */ \
    c += (__uint128_t)u0 * (R >> 4); \
    /* [d 0 t4 t3 0 0 c] */ \
    (r)[0] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 t4 t3 0 c r0] */ \
    \
    /* p1 = 2*a0*a1 */ \
    a0 *= 2; \
    c += (__uint128_t)a0 * a1; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 0 p5 p4 p3 0 p1 p0] */ \
    \
    /* p6 = 2*a2*a4 + a3*a3 */ \
    d += (__uint128_t)a2 * a4; \
    d += (__uint128_t)a3 * a3; \
    /* [d 0 t4 t3 0 c r0] = [p8 0 p6 p5 p4 p3 0 p1 p0] */ \
    c += (__uint128_t)((uint64_t)d & M) * R; d >>= 52; \
    /* [d 0 0 t4 t3 0 c r0] */ \
    (r)[1] = (uint64_t)c & M; c >>= 52; \
    /* [d 0 0 t4 t3 c r1 r0] */ \
    \
    /* p2 = 2*a0*a2 + a1*a1 */ \
    c += (__uint128_t)a0 * a2; \
    c += (__uint128_t)a1 * a1; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 0 p6 p5 p4 p3 p2 p1 p0] */ \
    \
    /* p7 = 2*a3*a4 */ \
    d += (__uint128_t)a3 * a4; \
    /* [d 0 0 t4 t3 c r1 r0] = [p8 p7 p6 p5 p4 p3 p2 p1 p0] */ \
    c += (__uint128_t)((uint64_t)d) * R; d >>= 64; \
    /* [(d<<12) 0 0 0 t4 t3 c r1 r0] */ \
    (r)[2] = (uint64_t)c & M; c >>= 52; \
    /* [(d<<12) 0 0 0 t4 t3+c r2 r1 r0] */ \
    \
    c += (__uint128_t)((uint64_t)d) * (R << 12); \
    c += t3; \
    /* [t4 c r2 r1 r0] */ \
    (r)[3] = (uint64_t)c & M; c >>= 52; \
    /* [t4+c r3 r2 r1 r0] */ \
    (r)[4] = (uint64_t)c + t4; \
    /* [r4 r3 r2 r1 r0] */ \
} while(0)

// ============================================================================
// Platform-specific implementation selection
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || (defined(__SIZEOF_POINTER__) && __SIZEOF_POINTER__ == 8)
// ============================================================================
// 64-bit: 5x52-bit limb representation
// ============================================================================

// ============================================================================
// AVX2+BMI2 Field Multiplication: a * b mod p
// Using interleaved multiplication and reduction (bitcoin-core style)
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_field_mul_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b)
{
    FIELD_MUL_IMPL(r->n, a->n, b->n);
}

// ============================================================================
// AVX2+BMI2 Field Squaring: a^2 mod p
// Optimized for squaring - symmetric products computed once
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_field_sqr_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a)
{
    FIELD_SQR_IMPL(r->n, a->n);
}

#else
// ============================================================================
// 32-bit: 10x26-bit limb representation
// For 32-bit platforms, we delegate to the reference implementation
// as the arch-specific optimizations are designed for 64-bit limbs.
// ============================================================================

void ecdsa_field_mul_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a, const ecdsa_field_t *b)
{
    ecdsa_field_mul_ref(r, a, b);
}

void ecdsa_field_sqr_avx2_bmi2(ecdsa_field_t *r, const ecdsa_field_t *a)
{
    ecdsa_field_sqr_ref(r, a);
}

#endif // 64-bit vs 32-bit
#endif
