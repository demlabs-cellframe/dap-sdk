#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
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
 * @file dap_keccak_avx512.c
 * @brief AVX-512 optimized Keccak-p[1600] permutation (Plane-Based)
 * @details Auto-generated from plane-based template using dap_tpl.
 *
 * Plane-based layout: 5 SIMD registers hold 5 lanes each (B, G, K, M, S planes)
 * Requires: wide registers (512-bit+), ternarylogic, variable rotation, permutation
 *
 * Key optimizations for AVX-512:
 * 
 *
 * Performance target: 
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <string.h>
#include <immintrin.h>

#include "dap_hash_keccak.h"

// ============================================================================
// AVX-512 Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// AVX-512 SIMD Primitives for Keccak (Plane Layout)
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

#define VTYPE VEC_T

#define XOR(a, b)       VEC_XOR(a, b)
#define XOR3(a, b, c)   VEC_XOR3(a, b, c)
#define XOR5(a,b,c,d,e) XOR3(XOR3(a, b, c), d, e)
#define CHI(a, b, c)    VEC_CHI(a, b, c)

#define LOAD_PLANE(a)   VEC_MASKZ_LOAD_64(0x1F, a)
#define STORE_PLANE(a, v) VEC_MASK_STORE_64(a, 0x1F, v)
#define LOAD_RC(i)      VEC_MASKZ_LOAD_64(0x01, s_round_constants + (i))

#define ROL1(x)         VEC_ROL64(x, 1)

// Permutation indices for Theta step
static const VTYPE s_theta_prev = {4, 0, 1, 2, 3, 5, 6, 7};
static const VTYPE s_theta_next = {1, 2, 3, 4, 0, 5, 6, 7};

#define PERMUTE_THETA_PREV(x) VEC_PERMUTEXVAR_64(s_theta_prev, x)
#define PERMUTE_THETA_NEXT(x) VEC_PERMUTEXVAR_64(s_theta_next, x)

// Rho rotation offsets per plane
static const VTYPE s_rho_B = { 0,  1, 62, 28, 27, 0, 0, 0};
static const VTYPE s_rho_G = {36, 44,  6, 55, 20, 0, 0, 0};
static const VTYPE s_rho_K = { 3, 10, 43, 25, 39, 0, 0, 0};
static const VTYPE s_rho_M = {41, 45, 15, 21,  8, 0, 0, 0};
static const VTYPE s_rho_S = {18,  2, 61, 56, 14, 0, 0, 0};

#define ROLV_B(x) VEC_ROLV64(x, s_rho_B)
#define ROLV_G(x) VEC_ROLV64(x, s_rho_G)
#define ROLV_K(x) VEC_ROLV64(x, s_rho_K)
#define ROLV_M(x) VEC_ROLV64(x, s_rho_M)
#define ROLV_S(x) VEC_ROLV64(x, s_rho_S)

// Pi step 1: within-plane permutation indices
static const VTYPE s_pi1_B = {0, 3, 1, 4, 2, 5, 6, 7};
static const VTYPE s_pi1_G = {1, 4, 2, 0, 3, 5, 6, 7};
static const VTYPE s_pi1_K = {2, 0, 3, 1, 4, 5, 6, 7};
static const VTYPE s_pi1_M = {3, 1, 4, 2, 0, 5, 6, 7};
static const VTYPE s_pi1_S = {4, 2, 0, 3, 1, 5, 6, 7};

#define PERMUTE_PI1_B(x) VEC_PERMUTEXVAR_64(s_pi1_B, x)
#define PERMUTE_PI1_G(x) VEC_PERMUTEXVAR_64(s_pi1_G, x)
#define PERMUTE_PI1_K(x) VEC_PERMUTEXVAR_64(s_pi1_K, x)
#define PERMUTE_PI1_M(x) VEC_PERMUTEXVAR_64(s_pi1_M, x)
#define PERMUTE_PI1_S(x) VEC_PERMUTEXVAR_64(s_pi1_S, x)

// Pi step 2: cross-plane permutation indices
static const VTYPE s_pi2_S1 = {0, 1, 2, 3, 4, 5, 0+8, 2+8};
static const VTYPE s_pi2_S2 = {0, 1, 2, 3, 4, 5, 1+8, 3+8};
static const VTYPE s_pi2_BG = {0, 1, 0+8, 1+8, 6, 5, 6, 7};
static const VTYPE s_pi2_KM = {2, 3, 2+8, 3+8, 7, 5, 6, 7};
static const VTYPE s_pi2_S3 = {4, 5, 4+8, 5+8, 4, 5, 6, 7};

#define PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4) \
do { \
    t0 = VEC_UNPACKLO_64(B, G); t1 = VEC_UNPACKLO_64(K, M); \
    t0 = VEC_PERMUTEX2VAR_64(t0, s_pi2_S1, S); \
    t2 = VEC_UNPACKHI_64(B, G); t3 = VEC_UNPACKHI_64(K, M); \
    t2 = VEC_PERMUTEX2VAR_64(t2, s_pi2_S2, S); \
    B = VEC_PERMUTEX2VAR_64(t0, s_pi2_BG, t1); \
    G = VEC_PERMUTEX2VAR_64(t2, s_pi2_BG, t3); \
    K = VEC_PERMUTEX2VAR_64(t0, s_pi2_KM, t1); \
    M = VEC_PERMUTEX2VAR_64(t2, s_pi2_KM, t3); \
    t0 = VEC_PERMUTEX2VAR_64(t0, s_pi2_S3, t1); \
    S = VEC_MASK_BLEND_64(0x10, t0, S); \
} while(0)

// ============================================================================
// Round Constants (aligned for SIMD)
// ============================================================================

static const uint64_t s_round_constants[24] __attribute__((aligned(64))) = {
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

// ============================================================================
// AVX-512 Keccak-p[1600] Permutation (Plane-Based)
// ============================================================================

__attribute__((target("avx512f,avx512bw,avx512vl"))) __attribute__((noinline))
void dap_hash_keccak_permute_avx512(dap_hash_keccak_state_t *state)
{
    uint64_t *A = state->lanes;
    
    VTYPE B = LOAD_PLANE(A + 0);
    VTYPE G = LOAD_PLANE(A + 5);
    VTYPE K = LOAD_PLANE(A + 10);
    VTYPE M = LOAD_PLANE(A + 15);
    VTYPE S = LOAD_PLANE(A + 20);
    VTYPE t0, t1, t2, t3, t4;
    
    for (int i = 0; i < 24; i++) {
        t0 = XOR5(B, G, K, M, S);
        t1 = PERMUTE_THETA_PREV(t0);
        t0 = PERMUTE_THETA_NEXT(t0);
        t0 = ROL1(t0);
        B = XOR3(B, t0, t1); G = XOR3(G, t0, t1);
        K = XOR3(K, t0, t1); M = XOR3(M, t0, t1); S = XOR3(S, t0, t1);
        
        B = ROLV_B(B); G = ROLV_G(G); K = ROLV_K(K); M = ROLV_M(M); S = ROLV_S(S);
        
        t0 = PERMUTE_PI1_B(B); t1 = PERMUTE_PI1_G(G); t2 = PERMUTE_PI1_K(K);
        t3 = PERMUTE_PI1_M(M); t4 = PERMUTE_PI1_S(S);
        
        B = CHI(t0, t1, t2); G = CHI(t1, t2, t3); K = CHI(t2, t3, t4);
        M = CHI(t3, t4, t0); S = CHI(t4, t0, t1);
        
        B = XOR(B, LOAD_RC(i));
        PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4);
    }
    
    STORE_PLANE(A + 0, B);
    STORE_PLANE(A + 5, G);
    STORE_PLANE(A + 10, K);
    STORE_PLANE(A + 15, M);
    STORE_PLANE(A + 20, S);
}
#endif
