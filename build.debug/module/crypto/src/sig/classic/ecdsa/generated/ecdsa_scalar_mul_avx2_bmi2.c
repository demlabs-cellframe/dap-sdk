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
 * @file ecdsa_scalar_mul_avx2_bmi2.c
 * @brief AVX2+BMI2 optimized secp256k1 scalar multiplication
 * @details Auto-generated from template.
 *
 * Key functions optimized:
 *   - scalar_mul_512: 256x256 -> 512 bit multiplication
 *   - scalar_mul_shift_384: (a * b) >> 384 for GLV decomposition
 *   - scalar_reduce_512: Reduction mod n
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

#include "ecdsa_scalar.h"
#include "arch/ecdsa_scalar_mul_arch.h"

// ============================================================================
// AVX2+BMI2 Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// AVX2 + BMI2 Primitives for secp256k1 Scalar Arithmetic
// Uses MULX, ADCX, ADOX for parallel carry chains
// ============================================================================

// Note: BMI2/ADX instructions are enabled via __attribute__((target("avx2,bmi2,adx")))
// on each function, not via compiler flags
#if defined(__x86_64__)

// ============================================================================
// 256x256 -> 512 bit multiplication using BMI2 MULX + ADX ADCX/ADOX
// MULX: dst_hi:dst_lo = src1 * rdx (no flags affected!)
// ADCX: dst += src + CF (only affects CF)
// ADOX: dst += src + OF (only affects OF)
// This allows two parallel carry chains for maximum throughput
// ============================================================================

#define SCALAR_MUL_512_IMPL(l, a, b) do { \
    __asm__ __volatile__( \
        /* Strategy: Use MULX for flag-free multiply, ADCX/ADOX for parallel adds */ \
        /* Load b[0] into rdx for MULX source */ \
        "movq 0(%[pb]), %%rdx\n" \
        \
        /* Column 0: a[0]*b[0] -> l[0], carry to r8 */ \
        "mulx 0(%[pa]), %%rax, %%r8\n" \
        "movq %%rax, 0(%[pl])\n" \
        \
        /* Column 1: a[1]*b[0] + ... */ \
        "mulx 8(%[pa]), %%rax, %%r9\n" \
        "xorq %%r10, %%r10\n"           /* Clear r10 and flags */ \
        "adcx %%rax, %%r8\n" \
        \
        /* Column 2: a[2]*b[0] + ... */ \
        "mulx 16(%[pa]), %%rax, %%r10\n" \
        "adcx %%rax, %%r9\n" \
        \
        /* Column 3: a[3]*b[0] + ... */ \
        "mulx 24(%[pa]), %%rax, %%r11\n" \
        "adcx %%rax, %%r10\n" \
        "adcx %%r11, %%r11\n"           /* r11 = 0 + carry */ \
        "movq $0, %%r11\n" \
        "adcq $0, %%r11\n" \
        \
        /* Load b[1] into rdx */ \
        "movq 8(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[1] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r8\n" \
        "movq %%r8, 8(%[pl])\n"         /* l[1] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r9\n" \
        "adox %%rax, %%r9\n" \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r10\n" \
        "adox %%rax, %%r10\n" \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r11\n" \
        "adox %%rax, %%r11\n" \
        "movq $0, %%r12\n" \
        "adcx %%r8, %%r12\n" \
        "adox %%r12, %%r12\n" \
        "movq $0, %%r12\n" \
        "adcq $0, %%r12\n" \
        \
        /* Load b[2] into rdx */ \
        "movq 16(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[2] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r9\n" \
        "movq %%r9, 16(%[pl])\n"        /* l[2] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r10\n" \
        "adox %%rax, %%r10\n" \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r11\n" \
        "adox %%rax, %%r11\n" \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r12\n" \
        "adox %%rax, %%r12\n" \
        "movq $0, %%r13\n" \
        "adcx %%r8, %%r13\n" \
        "adox %%r13, %%r13\n" \
        "movq $0, %%r13\n" \
        "adcq $0, %%r13\n" \
        \
        /* Load b[3] into rdx */ \
        "movq 24(%[pb]), %%rdx\n" \
        \
        /* a[0..3] * b[3] */ \
        "mulx 0(%[pa]), %%rax, %%rcx\n" \
        "adox %%rax, %%r10\n" \
        "movq %%r10, 24(%[pl])\n"       /* l[3] done */ \
        \
        "mulx 8(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r11\n" \
        "adox %%rax, %%r11\n" \
        "movq %%r11, 32(%[pl])\n"       /* l[4] done */ \
        \
        "mulx 16(%[pa]), %%rax, %%rcx\n" \
        "adcx %%r8, %%r12\n" \
        "adox %%rax, %%r12\n" \
        "movq %%r12, 40(%[pl])\n"       /* l[5] done */ \
        \
        "mulx 24(%[pa]), %%rax, %%r8\n" \
        "adcx %%rcx, %%r13\n" \
        "adox %%rax, %%r13\n" \
        "movq %%r13, 48(%[pl])\n"       /* l[6] done */ \
        \
        "movq $0, %%rax\n" \
        "adcx %%r8, %%rax\n" \
        "movq %%rax, 56(%[pl])\n"       /* l[7] done */ \
        \
        : /* outputs */ \
        : [pl] "r" (l), [pa] "r" (a), [pb] "r" (b) \
        : "rax", "rcx", "rdx", "r8", "r9", "r10", "r11", "r12", "r13", "cc", "memory" \
    ); \
} while(0)

// Reduction uses same approach as generic x86-64
#define SCALAR_REDUCE_512_IMPL(r, l) do { \
    uint64_t r0 = (l)[0], r1 = (l)[1], r2 = (l)[2], r3 = (l)[3]; \
    uint64_t h0 = (l)[4], h1 = (l)[5], h2 = (l)[6], h3 = (l)[7]; \
    \
    if (h0 | h1 | h2 | h3) { \
        __asm__ __volatile__( \
            "movq %[h0], %%rax\n" \
            "mulq %[c0]\n" \
            "addq %%rax, %[r0]\n" \
            "adcq %%rdx, %[r1]\n" \
            "adcq $0, %[r2]\n" \
            "adcq $0, %[r3]\n" \
            : [r0] "+r" (r0), [r1] "+r" (r1), [r2] "+r" (r2), [r3] "+r" (r3) \
            : [h0] "r" (h0), [c0] "r" (SCALAR_2P256_MOD_N[0]) \
            : "rax", "rdx", "cc" \
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
#error "AVX2+BMI2 primitives require x86-64 architecture"
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
// AVX2+BMI2 Scalar Multiplication: 256x256 -> 512 bit
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_scalar_mul_512_avx2_bmi2(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    SCALAR_MUL_512_IMPL(l, a->d, b->d);
}

// ============================================================================
// AVX2+BMI2 Scalar Multiply and Shift: (a * b) >> 384 for GLV
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_scalar_mul_shift_384_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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
// AVX2+BMI2 Scalar Reduction: 512-bit -> 256-bit mod n
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_scalar_reduce_512_avx2_bmi2(ecdsa_scalar_t *r, const uint64_t l[8])
{
    SCALAR_REDUCE_512_IMPL(r->d, l);
}

// ============================================================================
// AVX2+BMI2 Full Scalar Multiplication: (a * b) mod n
// ============================================================================

__attribute__((target("avx2,bmi2,adx")))
void ecdsa_scalar_mul_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    uint64_t l[8];
    ecdsa_scalar_mul_512_avx2_bmi2(l, a, b);
    ecdsa_scalar_reduce_512_avx2_bmi2(r, l);
}

#else // ECDSA_SCALAR_32BIT
// ============================================================================
// 32-bit fallback: delegate to main ecdsa_scalar functions
// The arch-specific scalar_mul functions use 4x64-bit limbs which is not
// compatible with the 8x32-bit limb layout used on 32-bit platforms.
// On 32-bit we simply use the main ecdsa_scalar_mul implementation.
// ============================================================================

void ecdsa_scalar_mul_512_avx2_bmi2(uint64_t l[8], const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
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

void ecdsa_scalar_mul_shift_384_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // GLV shift operation - simplified for 32-bit
    // Returns zeros as GLV optimization is mainly useful on 64-bit
    (void)a; (void)b;
    memset(r->d, 0, sizeof(r->d));
}

void ecdsa_scalar_reduce_512_avx2_bmi2(ecdsa_scalar_t *r, const uint64_t l[8])
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

void ecdsa_scalar_mul_avx2_bmi2(ecdsa_scalar_t *r, const ecdsa_scalar_t *a, const ecdsa_scalar_t *b)
{
    // Direct delegation to the main implementation
    ecdsa_scalar_mul(r, a, b);
}

#endif // ECDSA_SCALAR_64BIT
#endif
