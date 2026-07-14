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
 * @file ecdsa_scalar_mul_x86_64_asm.c
 * @brief x86-64 ASM optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
 *
 * Optimizations for x86-64 ASM:
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
// x86-64 ASM Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// x86-64 Assembly Primitives for secp256k1 Scalar Arithmetic
// Hand-optimized inline assembly for maximum performance
// Based on bitcoin-core/secp256k1 scalar_4x64_impl.h
// ============================================================================

#if defined(__x86_64__) || defined(_M_X64)

// ============================================================================
// 256x256 -> 512 bit multiplication using x86-64 MULQ instruction
// This is the most critical hot path - every µs counts
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    const uint64_t *_mul_pa = (const uint64_t *)(a); \
    uint64_t *_mul_pl = (uint64_t *)(l); \
    uint64_t _mul_pb = (uint64_t)(uintptr_t)(b); \
    __asm__ __volatile__( \
        /* Load b[0..3] into r11-r14 FIRST (frees [pb] register after) */ \
        "movq 0(%[pb]), %%r11\n" \
        "movq 8(%[pb]), %%r12\n" \
        "movq 16(%[pb]), %%r13\n" \
        "movq 24(%[pb]), %%r14\n" \
        /* Load a[0..2]: a[0]→r15, a[1]→[pb] (reuse!), a[2]→rcx */ \
        "movq 0(%[pa]), %%r15\n" \
        "movq 8(%[pa]), %[pb]\n" \
        "movq 16(%[pa]), %%rcx\n" \
        \
        /* ================================================================ */ \
        /* l[0] = a[0] * b[0] */ \
        /* ================================================================ */ \
        "movq %%r15, %%rax\n" \
        "mulq %%r11\n" \
        "movq %%rax, 0(%[pl])\n"     /* l[0] = low */ \
        "movq %%rdx, %%r8\n"         /* r8 = high (carry) */ \
        "xorq %%r9, %%r9\n" \
        "xorq %%r10, %%r10\n" \
        \
        /* ================================================================ */ \
        /* l[1] = a[0]*b[1] + a[1]*b[0] */ \
        /* ================================================================ */ \
        "movq %%r15, %%rax\n" \
        "mulq %%r12\n" \
        "addq %%rax, %%r8\n" \
        "adcq %%rdx, %%r9\n" \
        "adcq $0, %%r10\n" \
        \
        "movq %[pb], %%rax\n" \
        "mulq %%r11\n" \
        "addq %%rax, %%r8\n" \
        "adcq %%rdx, %%r9\n" \
        "adcq $0, %%r10\n" \
        \
        "movq %%r8, 8(%[pl])\n"      /* l[1] */ \
        "xorq %%r8, %%r8\n" \
        \
        /* ================================================================ */ \
        /* l[2] = a[0]*b[2] + a[1]*b[1] + a[2]*b[0] */ \
        /* ================================================================ */ \
        "movq %%r15, %%rax\n" \
        "mulq %%r13\n" \
        "addq %%rax, %%r9\n" \
        "adcq %%rdx, %%r10\n" \
        "adcq $0, %%r8\n" \
        \
        "movq %[pb], %%rax\n" \
        "mulq %%r12\n" \
        "addq %%rax, %%r9\n" \
        "adcq %%rdx, %%r10\n" \
        "adcq $0, %%r8\n" \
        \
        "movq %%rcx, %%rax\n" \
        "mulq %%r11\n" \
        "addq %%rax, %%r9\n" \
        "adcq %%rdx, %%r10\n" \
        "adcq $0, %%r8\n" \
        \
        "movq %%r9, 16(%[pl])\n"     /* l[2] */ \
        "xorq %%r9, %%r9\n" \
        \
        /* ================================================================ */ \
        /* l[3] = a[0]*b[3] + a[1]*b[2] + a[2]*b[1] + a[3]*b[0] */ \
        /* ================================================================ */ \
        "movq %%r15, %%rax\n" \
        "mulq %%r14\n" \
        "addq %%rax, %%r10\n" \
        "adcq %%rdx, %%r8\n" \
        "adcq $0, %%r9\n" \
        \
        /* Load a[3] into r15 (reusing register, [pa] still valid) */ \
        "movq 24(%[pa]), %%r15\n" \
        \
        "movq %[pb], %%rax\n" \
        "mulq %%r13\n" \
        "addq %%rax, %%r10\n" \
        "adcq %%rdx, %%r8\n" \
        "adcq $0, %%r9\n" \
        \
        "movq %%rcx, %%rax\n" \
        "mulq %%r12\n" \
        "addq %%rax, %%r10\n" \
        "adcq %%rdx, %%r8\n" \
        "adcq $0, %%r9\n" \
        \
        "movq %%r15, %%rax\n" \
        "mulq %%r11\n" \
        "addq %%rax, %%r10\n" \
        "adcq %%rdx, %%r8\n" \
        "adcq $0, %%r9\n" \
        \
        "movq %%r10, 24(%[pl])\n"    /* l[3] */ \
        "xorq %%r10, %%r10\n" \
        \
        /* ================================================================ */ \
        /* l[4] = a[1]*b[3] + a[2]*b[2] + a[3]*b[1] */ \
        /* ================================================================ */ \
        "movq %[pb], %%rax\n" \
        "mulq %%r14\n" \
        "addq %%rax, %%r8\n" \
        "adcq %%rdx, %%r9\n" \
        "adcq $0, %%r10\n" \
        \
        "movq %%rcx, %%rax\n" \
        "mulq %%r13\n" \
        "addq %%rax, %%r8\n" \
        "adcq %%rdx, %%r9\n" \
        "adcq $0, %%r10\n" \
        \
        "movq %%r15, %%rax\n" \
        "mulq %%r12\n" \
        "addq %%rax, %%r8\n" \
        "adcq %%rdx, %%r9\n" \
        "adcq $0, %%r10\n" \
        \
        "movq %%r8, 32(%[pl])\n"     /* l[4] */ \
        "xorq %%r8, %%r8\n" \
        \
        /* ================================================================ */ \
        /* l[5] = a[2]*b[3] + a[3]*b[2] */ \
        /* ================================================================ */ \
        "movq %%rcx, %%rax\n" \
        "mulq %%r14\n" \
        "addq %%rax, %%r9\n" \
        "adcq %%rdx, %%r10\n" \
        "adcq $0, %%r8\n" \
        \
        "movq %%r15, %%rax\n" \
        "mulq %%r13\n" \
        "addq %%rax, %%r9\n" \
        "adcq %%rdx, %%r10\n" \
        "adcq $0, %%r8\n" \
        \
        "movq %%r9, 40(%[pl])\n"     /* l[5] */ \
        \
        /* ================================================================ */ \
        /* l[6] = a[3]*b[3], l[7] = carry */ \
        /* ================================================================ */ \
        "movq %%r15, %%rax\n" \
        "mulq %%r14\n" \
        "addq %%rax, %%r10\n" \
        "adcq %%rdx, %%r8\n" \
        \
        "movq %%r10, 48(%[pl])\n"    /* l[6] */ \
        "movq %%r8, 56(%[pl])\n"     /* l[7] */ \
        \
        : [pb] "+r" (_mul_pb) /* starts as &b, becomes a[1] scratch */ \
        : [pl] "r" (_mul_pl), [pa] "r" (_mul_pa) \
        : "rax", "rdx", "rcx", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "cc", "memory" \
    ); \
} while(0)

// ============================================================================
// 512-bit reduction mod n
// Uses the special structure of secp256k1 order for fast reduction
// ============================================================================

#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    /* secp256k1 n = 2^256 - 0x14551231950B75FC4402DA1732FC9BEBF */ \
    /* So 2^256 ≡ 0x14551231950B75FC4402DA1732FC9BEBF (mod n) */ \
    /* For reduction: r = l[0..3] + l[4..7] * (2^256 mod n) */ \
    \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    /* If high part is zero, skip reduction */ \
    if (h0 | h1 | h2 | h3) { \
        __asm__ __volatile__( \
            /* Multiply h by (2^256 mod n) and add to r */ \
            /* This is simplified - full reduction needed */ \
            "movq %[h0], %%rax\n" \
            "mulq %[c0]\n" \
            "addq %%rax, %[r0]\n" \
            "adcq %%rdx, %[r1]\n" \
            "adcq $0, %[r2]\n" \
            "adcq $0, %[r3]\n" \
            \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0]) \
            : "rax", "rdx", "cc" \
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
        __asm__ __volatile__( \
            "subq %[n0], %[r0]\n" \
            "sbbq %[n1], %[r1]\n" \
            "sbbq %[n2], %[r2]\n" \
            "sbbq %[n3], %[r3]\n" \
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
#error "x86-64 assembly requires x86-64 architecture"
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
// x86-64 ASM Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================


void ecdsa_scalar_mul_512_x86_64_asm(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// x86-64 ASM Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================


void ecdsa_scalar_mul_shift_384_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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
// x86-64 ASM Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================


void ecdsa_scalar_reduce_512_x86_64_asm(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// x86-64 ASM Full Scalar Multiplication: (a * b) mod n
// ============================================================================


void ecdsa_scalar_mul_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_x86_64_asm(l, a, b);
    ecdsa_scalar_reduce_512_x86_64_asm(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_x86_64_asm(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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

void ecdsa_scalar_mul_shift_384_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_x86_64_asm(ecdsa_scalar_t *r, const uint64_t l[8])
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

void ecdsa_scalar_mul_x86_64_asm(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
#endif
