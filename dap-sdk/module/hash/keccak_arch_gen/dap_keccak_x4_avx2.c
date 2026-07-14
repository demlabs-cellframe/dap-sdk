#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
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
 * @file dap_keccak_x4_avx2.c
 * @brief AVX2 optimized 4-way Keccak-p[1600]×4 permutation
 * @details Auto-generated from x4 template.
 *
 * Each LANE_T vector holds the same Keccak lane from LANE_WIDTH instances.
 * The permutation processes LANE_WIDTH instances per pass (4/LANE_WIDTH passes total).
 * All Keccak steps (Theta, Rho, Pi, Chi, Iota) are fully vectorized across instances.
 *
 * 
 *
 * @generated
 */

#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#include "dap_hash_keccak_x4.h"

// ============================================================================
// Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// AVX2 Primitives for Keccak×4 (4-way parallel permutation)
//
// Each LANE_T (__m256i) holds the same Keccak lane from 4 independent states.
// LANE_WIDTH = 4: one AVX2 pass covers all 4 instances.
// ============================================================================

// ============================================================================
// AVX2 Shared SIMD Primitives (256-bit)
// Provides unified macro names for all modules using AVX2 arch optimizations.
// ============================================================================

#include <immintrin.h>

typedef __m256i VEC_T;
#define VEC_BITS     256
#define VEC_LANES_8  32
#define VEC_LANES_16 16
#define VEC_LANES_32 8
#define VEC_LANES_64 4

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm256_loadu_si256((const __m256i *)(p))
#define VEC_STORE(p, v)   _mm256_storeu_si256((__m256i *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm256_xor_si256(a, b)
#define VEC_AND(a, b)     _mm256_and_si256(a, b)
#define VEC_OR(a, b)      _mm256_or_si256(a, b)
#define VEC_ANDNOT(a, b)  _mm256_andnot_si256(a, b)

// === Zero / Blend ===========================================================

#define VEC_ZERO()        _mm256_setzero_si256()

// === 8-bit element ops ======================================================

#define VEC_SET1_8(x)      _mm256_set1_epi8(x)
#define VEC_CMPEQ_8(a, b)  _mm256_cmpeq_epi8(a, b)
#define VEC_ADD8(a, b)     _mm256_add_epi8(a, b)
#define VEC_SUB8(a, b)     _mm256_sub_epi8(a, b)
#define VEC_MOVEMASK_8(v)  _mm256_movemask_epi8(v)

// === 16-bit element ops =====================================================

#define VEC_SET1_16(x)      _mm256_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm256_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm256_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm256_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm256_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm256_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm256_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm256_srli_epi16(a, n)

// === 16-bit advanced ops ====================================================

#define VEC_MULHRS16(a, b)      _mm256_mulhrs_epi16(a, b)
#define VEC_BLEND16(a, b, imm)  _mm256_blend_epi16(a, b, imm)
#define VEC_SHUFFLELO16(a, imm) _mm256_shufflelo_epi16(a, imm)
#define VEC_SHUFFLEHI16(a, imm) _mm256_shufflehi_epi16(a, imm)
#define VEC_SETR_16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15) \
    _mm256_setr_epi16(e0,e1,e2,e3,e4,e5,e6,e7,e8,e9,e10,e11,e12,e13,e14,e15)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)          _mm256_set1_epi32((int)(x))
#define VEC_ADD32(a, b)         _mm256_add_epi32(a, b)
#define VEC_SUB32(a, b)         _mm256_sub_epi32(a, b)
#define VEC_MULLO32(a, b)       _mm256_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)        _mm256_slli_epi32(a, n)
#define VEC_SRLI32(a, n)        _mm256_srli_epi32(a, n)
#define VEC_SRAI32(a, n)        _mm256_srai_epi32(a, n)
#define VEC_SET_32(h,g,f,e,d,c,b,a)  _mm256_set_epi32(h,g,f,e,d,c,b,a)
#define VEC_CMPEQ_32(a, b)         _mm256_cmpeq_epi32(a, b)
#define VEC_CMPGT_32(a, b)         _mm256_cmpgt_epi32(a, b)
#define VEC_BLENDV_32(mask, t, f)  _mm256_blendv_epi8(f, t, mask)
#define VEC_ANY_TRUE_32(v)         (_mm256_movemask_epi8(v) != 0)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)          _mm256_set1_epi64x(x)
#define VEC_ADD64(a, b)         _mm256_add_epi64(a, b)
#define VEC_SET_64(d, c, b, a)  _mm256_set_epi64x(d, c, b, a)

// === Half-width (128-bit) operations ========================================

typedef __m128i HVEC_T;
#define HVEC_BITS    128
#define HVEC_LANES_8  16
#define HVEC_LANES_16 8
#define HVEC_LANES_32 4
#define HVEC_LANES_64 2

#define HVEC_LOAD(p)       _mm_loadu_si128((const __m128i *)(p))
#define HVEC_STORE(p, v)   _mm_storeu_si128((__m128i *)(p), (v))

#define HVEC_XOR(a, b)     _mm_xor_si128(a, b)
#define HVEC_AND(a, b)     _mm_and_si128(a, b)
#define HVEC_OR(a, b)      _mm_or_si128(a, b)
#define HVEC_ANDNOT(a, b)  _mm_andnot_si128(a, b)

#define HVEC_SET1_16(x)     _mm_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm_srai_epi16(a, n)

#define HVEC_SET1_32(x)     _mm_set1_epi32((int)(x))
#define HVEC_ADD32(a, b)    _mm_add_epi32(a, b)
#define HVEC_SUB32(a, b)    _mm_sub_epi32(a, b)
#define HVEC_SLLI32(a, n)   _mm_slli_epi32(a, n)
#define HVEC_SRLI32(a, n)   _mm_srli_epi32(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm256_castsi256_si128(v)
#define VEC_HI_HALF(v)            _mm256_extracti128_si256(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm256_setr_m128i(lo, hi)

#define LANE_T      VEC_T
#define LANE_WIDTH  4

#define LANE_LOAD(p)        VEC_LOAD(p)
#define LANE_STORE(p, v)    VEC_STORE(p, v)

#define LANE_XOR(a, b)      VEC_XOR(a, b)
#define LANE_ANDN(a, b)     VEC_ANDNOT(a, b)    /* ~a & b */
#define LANE_OR(a, b)       VEC_OR(a, b)
#define LANE_SET1_64(x)     VEC_SET1_64((long long)(x))

/* AVX2 has no native 64-bit rotate; emulate with shift+shift+OR */
static inline __m256i lane_rol64(__m256i x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    return _mm256_or_si256(_mm256_slli_epi64(x, n),
                           _mm256_srli_epi64(x, 64 - n));
}
#define LANE_ROL64(x, n) lane_rol64((x), (n))

// ============================================================================
// Constants
// ============================================================================

static const uint64_t s_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int s_rho[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
};

static const int s_pi[25] = {
     0, 10, 20,  5, 15,
    16,  1, 11, 21,  6,
     7, 17,  2, 12, 22,
    23,  8, 18,  3, 13,
    14, 24,  9, 19,  4
};

#define X4_PASSES  (4 / LANE_WIDTH)

// ============================================================================
// AVX2 Keccak-p[1600]×4 Permutation
// ============================================================================

__attribute__((target("avx2")))
void dap_keccak_x4_permute_avx2(dap_keccak_x4_state_t *a_state)
{
    for (int pass = 0; pass < X4_PASSES; pass++) {
        unsigned l_off = pass * LANE_WIDTH;
        LANE_T A[25];

        /* Load: each vector = one Keccak lane from LANE_WIDTH instances */
        for (int i = 0; i < 25; i++)
            A[i] = LANE_LOAD(&a_state->lanes[i * 4 + l_off]);

        for (int round = 0; round < 24; round++) {

            /* ============================================================ */
            /* Theta: C[x] = A[x,0] ^ A[x,1] ^ A[x,2] ^ A[x,3] ^ A[x,4] */
            /*        D[x] = C[x-1] ^ ROL64(C[x+1], 1)                    */
            /*        A[x,y] ^= D[x]                                       */
            /* ============================================================ */

            LANE_T C[5];
            C[0] = LANE_XOR(LANE_XOR(A[0], A[5]),
                    LANE_XOR(A[10], LANE_XOR(A[15], A[20])));
            C[1] = LANE_XOR(LANE_XOR(A[1], A[6]),
                    LANE_XOR(A[11], LANE_XOR(A[16], A[21])));
            C[2] = LANE_XOR(LANE_XOR(A[2], A[7]),
                    LANE_XOR(A[12], LANE_XOR(A[17], A[22])));
            C[3] = LANE_XOR(LANE_XOR(A[3], A[8]),
                    LANE_XOR(A[13], LANE_XOR(A[18], A[23])));
            C[4] = LANE_XOR(LANE_XOR(A[4], A[9]),
                    LANE_XOR(A[14], LANE_XOR(A[19], A[24])));

            LANE_T D[5];
            D[0] = LANE_XOR(C[4], LANE_ROL64(C[1], 1));
            D[1] = LANE_XOR(C[0], LANE_ROL64(C[2], 1));
            D[2] = LANE_XOR(C[1], LANE_ROL64(C[3], 1));
            D[3] = LANE_XOR(C[2], LANE_ROL64(C[4], 1));
            D[4] = LANE_XOR(C[3], LANE_ROL64(C[0], 1));

            for (int i = 0; i < 25; i++)
                A[i] = LANE_XOR(A[i], D[i % 5]);

            /* ============================================================ */
            /* Rho + Pi (combined)                                          */
            /* B[pi[i]] = ROL64(A[i], rho[i])                              */
            /* ============================================================ */

            LANE_T B[25];
            B[s_pi[ 0]] = A[ 0]; /* rho[0] == 0, skip rotate */
            B[s_pi[ 1]] = LANE_ROL64(A[ 1], s_rho[ 1]);
            B[s_pi[ 2]] = LANE_ROL64(A[ 2], s_rho[ 2]);
            B[s_pi[ 3]] = LANE_ROL64(A[ 3], s_rho[ 3]);
            B[s_pi[ 4]] = LANE_ROL64(A[ 4], s_rho[ 4]);
            B[s_pi[ 5]] = LANE_ROL64(A[ 5], s_rho[ 5]);
            B[s_pi[ 6]] = LANE_ROL64(A[ 6], s_rho[ 6]);
            B[s_pi[ 7]] = LANE_ROL64(A[ 7], s_rho[ 7]);
            B[s_pi[ 8]] = LANE_ROL64(A[ 8], s_rho[ 8]);
            B[s_pi[ 9]] = LANE_ROL64(A[ 9], s_rho[ 9]);
            B[s_pi[10]] = LANE_ROL64(A[10], s_rho[10]);
            B[s_pi[11]] = LANE_ROL64(A[11], s_rho[11]);
            B[s_pi[12]] = LANE_ROL64(A[12], s_rho[12]);
            B[s_pi[13]] = LANE_ROL64(A[13], s_rho[13]);
            B[s_pi[14]] = LANE_ROL64(A[14], s_rho[14]);
            B[s_pi[15]] = LANE_ROL64(A[15], s_rho[15]);
            B[s_pi[16]] = LANE_ROL64(A[16], s_rho[16]);
            B[s_pi[17]] = LANE_ROL64(A[17], s_rho[17]);
            B[s_pi[18]] = LANE_ROL64(A[18], s_rho[18]);
            B[s_pi[19]] = LANE_ROL64(A[19], s_rho[19]);
            B[s_pi[20]] = LANE_ROL64(A[20], s_rho[20]);
            B[s_pi[21]] = LANE_ROL64(A[21], s_rho[21]);
            B[s_pi[22]] = LANE_ROL64(A[22], s_rho[22]);
            B[s_pi[23]] = LANE_ROL64(A[23], s_rho[23]);
            B[s_pi[24]] = LANE_ROL64(A[24], s_rho[24]);

            /* ============================================================ */
            /* Chi: A[x,y] = B[x,y] ^ (~B[x+1,y] & B[x+2,y])             */
            /* ============================================================ */

#ifndef LANE_CHI
#define LANE_CHI(a, b, c) LANE_XOR(a, LANE_ANDN(b, c))
#endif
            for (int y = 0; y < 5; y++) {
                int b = y * 5;
                A[b + 0] = LANE_CHI(B[b + 0], B[b + 1], B[b + 2]);
                A[b + 1] = LANE_CHI(B[b + 1], B[b + 2], B[b + 3]);
                A[b + 2] = LANE_CHI(B[b + 2], B[b + 3], B[b + 4]);
                A[b + 3] = LANE_CHI(B[b + 3], B[b + 4], B[b + 0]);
                A[b + 4] = LANE_CHI(B[b + 4], B[b + 0], B[b + 1]);
            }

            /* ============================================================ */
            /* Iota: A[0] ^= RC[round]  (broadcast to all instances)        */
            /* ============================================================ */

            A[0] = LANE_XOR(A[0], LANE_SET1_64(s_rc[round]));
        }

        /* Store back */
        for (int i = 0; i < 25; i++)
            LANE_STORE(&a_state->lanes[i * 4 + l_off], A[i]);
    }
}
#endif
