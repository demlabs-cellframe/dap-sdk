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
 * @file dap_keccak_x4_avx512.c
 * @brief AVX-512 optimized 4-way Keccak-p[1600]×4 permutation
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
// AVX-512VL Primitives for Keccak×4 (4-way parallel permutation)
//
// Uses 256-bit vectors (__m256i) holding 4 Keccak lanes, but leverages
// AVX-512VL instructions:
//   - vpternlogq: Chi step a ^ (~b & c) in 1 instruction (was 2 on AVX2)
//   - vprolvq:    native 64-bit rotate in 1 instruction (was 3 on AVX2)
// Saves ~75 instructions per round × 24 rounds = ~1800 instructions per permute.
// ============================================================================

// ============================================================================
// AVX-512 Shared SIMD Primitives (512-bit)
// Requires AVX-512F + AVX-512BW for full 8/16-bit element support.
// ============================================================================

#include <immintrin.h>

typedef __m512i VEC_T;
#define VEC_BITS     512
#define VEC_LANES_8  64
#define VEC_LANES_16 32
#define VEC_LANES_32 16
#define VEC_LANES_64 8

// === Load / Store (type-agnostic) ==========================================

#define VEC_LOAD(p)       _mm512_loadu_si512((const void *)(p))
#define VEC_STORE(p, v)   _mm512_storeu_si512((void *)(p), (v))

// === Bitwise (type-agnostic) ================================================

#define VEC_XOR(a, b)     _mm512_xor_si512(a, b)
#define VEC_AND(a, b)     _mm512_and_si512(a, b)
#define VEC_OR(a, b)      _mm512_or_si512(a, b)
#define VEC_ANDNOT(a, b)  _mm512_andnot_si512(a, b)

// === Ternary logic (single-instruction boolean combinations) ================

#define VEC_XOR3(a, b, c)     _mm512_ternarylogic_epi64(a, b, c, 0x96)
#define VEC_CHI(a, b, c)      _mm512_ternarylogic_epi64(a, b, c, 0xD2)

// === Zero ===================================================================

#define VEC_ZERO()        _mm512_setzero_si512()

// === 8-bit element ops (requires AVX-512BW) =================================

#define VEC_SET1_8(x)              _mm512_set1_epi8(x)
#define VEC_ADD8(a, b)             _mm512_add_epi8(a, b)
#define VEC_SUB8(a, b)             _mm512_sub_epi8(a, b)
#define VEC_CMPEQ_8_MASK(a, b)     _mm512_cmpeq_epi8_mask(a, b)
#define VEC_MOVEMASK_8_TYPE        __mmask64

// === 16-bit element ops (requires AVX-512BW) ================================

#define VEC_SET1_16(x)      _mm512_set1_epi16(x)
#define VEC_ADD16(a, b)     _mm512_add_epi16(a, b)
#define VEC_SUB16(a, b)     _mm512_sub_epi16(a, b)
#define VEC_MULLO16(a, b)   _mm512_mullo_epi16(a, b)
#define VEC_MULHI16(a, b)   _mm512_mulhi_epi16(a, b)
#define VEC_SRAI16(a, n)    _mm512_srai_epi16(a, n)
#define VEC_SLLI16(a, n)    _mm512_slli_epi16(a, n)
#define VEC_SRLI16(a, n)    _mm512_srli_epi16(a, n)

// === 32-bit element ops =====================================================

#define VEC_SET1_32(x)      _mm512_set1_epi32((int)(x))
#define VEC_ADD32(a, b)     _mm512_add_epi32(a, b)
#define VEC_SUB32(a, b)     _mm512_sub_epi32(a, b)
#define VEC_MULLO32(a, b)   _mm512_mullo_epi32(a, b)
#define VEC_SLLI32(a, n)    _mm512_slli_epi32(a, n)
#define VEC_SRLI32(a, n)    _mm512_srli_epi32(a, n)
#define VEC_SRAI32(a, n)    _mm512_srai_epi32(a, n)
#define VEC_CMPEQ_32(a, b) _mm512_maskz_set1_epi32(_mm512_cmpeq_epi32_mask(a, b), -1)
#define VEC_CMPGT_32(a, b) _mm512_maskz_set1_epi32(_mm512_cmpgt_epi32_mask(a, b), -1)
#define VEC_BLENDV_32(mask, t, f) \
    _mm512_mask_blend_epi32(_mm512_movepi32_mask(mask), f, t)
#define VEC_ANY_TRUE_32(v) (_mm512_test_epi32_mask(v, v) != 0)

// === 64-bit element ops =====================================================

#define VEC_SET1_64(x)      _mm512_set1_epi64(x)
#define VEC_ADD64(a, b)     _mm512_add_epi64(a, b)
#define VEC_ROL64(a, n)     _mm512_rol_epi64(a, n)
#define VEC_ROLV64(a, v)    _mm512_rolv_epi64(a, v)

// === Mask load / store (5-lane for Keccak plane layout etc.) ================

#define VEC_MASKZ_LOAD_64(mask, p)     _mm512_maskz_loadu_epi64(mask, p)
#define VEC_MASK_STORE_64(p, mask, v)  _mm512_mask_storeu_epi64(p, mask, v)
#define VEC_MASK_BLEND_64(mask, a, b)  _mm512_mask_blend_epi64(mask, a, b)

// === Permutation ============================================================

#define VEC_PERMUTEXVAR_64(idx, v)          _mm512_permutexvar_epi64(idx, v)
#define VEC_PERMUTEX2VAR_64(a, idx, b)      _mm512_permutex2var_epi64(a, idx, b)
#define VEC_UNPACKLO_64(a, b)               _mm512_unpacklo_epi64(a, b)
#define VEC_UNPACKHI_64(a, b)               _mm512_unpackhi_epi64(a, b)

// === Half-width (256-bit) operations ========================================

typedef __m256i HVEC_T;
#define HVEC_BITS    256
#define HVEC_LANES_16 16

#define HVEC_LOAD(p)        _mm256_loadu_si256((const __m256i *)(p))
#define HVEC_STORE(p, v)    _mm256_storeu_si256((__m256i *)(p), (v))

#define HVEC_SET1_16(x)     _mm256_set1_epi16(x)
#define HVEC_ADD16(a, b)    _mm256_add_epi16(a, b)
#define HVEC_SUB16(a, b)    _mm256_sub_epi16(a, b)
#define HVEC_MULLO16(a, b)  _mm256_mullo_epi16(a, b)
#define HVEC_MULHI16(a, b)  _mm256_mulhi_epi16(a, b)
#define HVEC_SRAI16(a, n)   _mm256_srai_epi16(a, n)

// === Lane extract / compose =================================================

#define VEC_LO_HALF(v)            _mm512_castsi512_si256(v)
#define VEC_HI_HALF(v)            _mm512_extracti64x4_epi64(v, 1)
#define VEC_FROM_HALVES(lo, hi)   _mm512_inserti64x4(_mm512_castsi256_si512(lo), (hi), 1)

#define LANE_T      __m256i
#define LANE_WIDTH  4

#define LANE_LOAD(p)        _mm256_load_si256((const __m256i *)(p))
#define LANE_STORE(p, v)    _mm256_store_si256((__m256i *)(p), (v))

#define LANE_XOR(a, b)      _mm256_xor_si256((a), (b))
#define LANE_ANDN(a, b)     _mm256_andnot_si256((a), (b))
#define LANE_OR(a, b)       _mm256_or_si256((a), (b))
#define LANE_SET1_64(x)     _mm256_set1_epi64x((long long)(x))

// Chi fused: a ^ (~b & c) via vpternlogq (truth table 0xD2)
#define LANE_CHI(a, b, c)   _mm256_ternarylogic_epi64((a), (b), (c), 0xD2)

// Native 64-bit rotate via AVX-512VL vprolvq
static inline __m256i lane_rol64(__m256i x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    return _mm256_rolv_epi64(x, _mm256_set1_epi64x(n));
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
// AVX-512 Keccak-p[1600]×4 Permutation
// ============================================================================

__attribute__((target("avx512f,avx512bw,avx512vl")))
void dap_keccak_x4_permute_avx512(dap_keccak_x4_state_t *a_state)
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
