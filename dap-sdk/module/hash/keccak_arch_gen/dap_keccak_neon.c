#if defined(__aarch64__) || defined(__arm__)
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
 * @file dap_keccak_neon.c
 * @brief NEON optimized Keccak-p[1600] permutation (Lane)
 * @details Auto-generated from lane template using dap_tpl.
 *
 * Lane layout: Traditional 25-lane state with SIMD acceleration for:
 *   - Column parity (Theta)
 *   - Chi non-linear step (ANDN/BIC)
 *
 * Key optimizations for NEON:
 * 
 *
 * Performance target: 
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <string.h>
#include <arm_neon.h>

#include "dap_hash_keccak.h"

// ============================================================================
// NEON Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// ARM NEON SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

// ============================================================================
// ARM NEON Shared SIMD Primitives (128-bit)
//
// NEON uses typed vectors (int16x8_t, uint32x4_t, etc.) unlike x86's generic
// __m128i. This library provides operations for each element width. Modules
// should typedef VEC_T to the appropriate type for their use case.
// ============================================================================

#include <arm_neon.h>

#define VEC_BITS     128
#define VEC_LANES_8  16
#define VEC_LANES_16 8
#define VEC_LANES_32 4
#define VEC_LANES_64 2

// === 8-bit (uint8x16_t) operations =========================================

#define VEC_LOAD_U8(p)        vld1q_u8((const uint8_t *)(p))
#define VEC_STORE_U8(p, v)    vst1q_u8((uint8_t *)(p), (v))
#define VEC_SET1_U8(x)        vmovq_n_u8(x)
#define VEC_CMPEQ_U8(a, b)    vceqq_u8(a, b)
#define VEC_OR_U8(a, b)       vorrq_u8(a, b)
#define VEC_AND_U8(a, b)      vandq_u8(a, b)
#define VEC_XOR_U8(a, b)      veorq_u8(a, b)

static inline uint16_t neon_movemask_u8(uint8x16_t a_vec) {
    uint8x16_t l_shifted = vshrq_n_u8(a_vec, 7);
    static const uint8_t l_weights[16] = {
        1, 2, 4, 8, 16, 32, 64, 128,
        1, 2, 4, 8, 16, 32, 64, 128
    };
    uint8x16_t l_weighted = vmulq_u8(l_shifted, vld1q_u8(l_weights));
#ifdef __aarch64__
    uint8_t l_lo = vaddv_u8(vget_low_u8(l_weighted));
    uint8_t l_hi = vaddv_u8(vget_high_u8(l_weighted));
#else
    uint8x8_t lo8 = vget_low_u8(l_weighted);
    uint8x8_t hi8 = vget_high_u8(l_weighted);
    lo8 = vpadd_u8(lo8, lo8); lo8 = vpadd_u8(lo8, lo8); lo8 = vpadd_u8(lo8, lo8);
    hi8 = vpadd_u8(hi8, hi8); hi8 = vpadd_u8(hi8, hi8); hi8 = vpadd_u8(hi8, hi8);
    uint8_t l_lo = vget_lane_u8(lo8, 0);
    uint8_t l_hi = vget_lane_u8(hi8, 0);
#endif
    return (uint16_t)l_lo | ((uint16_t)l_hi << 8);
}
#define VEC_MOVEMASK_U8(v)    neon_movemask_u8(v)

// === 16-bit signed (int16x8_t) operations ===================================

#define VEC_LOAD_S16(p)        vld1q_s16((const int16_t *)(p))
#define VEC_STORE_S16(p, v)    vst1q_s16((int16_t *)(p), (v))
#define VEC_SET1_16(x)         vdupq_n_s16(x)
#define VEC_ADD16(a, b)        vaddq_s16(a, b)
#define VEC_SUB16(a, b)        vsubq_s16(a, b)

static inline int16x8_t neon_mullo_s16(int16x8_t a, int16x8_t b) {
    return vmulq_s16(a, b);
}
static inline int16x8_t neon_mulhi_s16(int16x8_t a, int16x8_t b) {
    int16x4_t a_lo = vget_low_s16(a),  a_hi = vget_high_s16(a);
    int16x4_t b_lo = vget_low_s16(b),  b_hi = vget_high_s16(b);
    int32x4_t p_lo = vmull_s16(a_lo, b_lo);
    int32x4_t p_hi = vmull_s16(a_hi, b_hi);
    return vcombine_s16(vshrn_n_s32(p_lo, 16), vshrn_n_s32(p_hi, 16));
}

#define VEC_MULLO16(a, b)      neon_mullo_s16(a, b)
#define VEC_MULHI16(a, b)      neon_mulhi_s16(a, b)
#define VEC_SRAI16(a, n)       vshrq_n_s16(a, n)
#define VEC_SLLI16(a, n)       vshlq_n_s16(a, n)

// === 16-bit advanced ops ====================================================

#define VEC_MULHRS16(a, b)     vqrdmulhq_s16(a, b)
#define VEC_AND_S16(a, b)      vandq_s16(a, b)

// === 32-bit unsigned (uint32x4_t) operations ================================

#define VEC_LOAD_U32(p)        vld1q_u32((const uint32_t *)(p))
#define VEC_STORE_U32(p, v)    vst1q_u32((uint32_t *)(p), (v))
#define VEC_SET1_U32(x)        vdupq_n_u32(x)
#define VEC_ADD_U32(a, b)      vaddq_u32(a, b)
#define VEC_SUB_U32(a, b)      vsubq_u32(a, b)
#define VEC_XOR_U32(a, b)      veorq_u32(a, b)
#define VEC_OR_U32(a, b)       vorrq_u32(a, b)
#define VEC_SHL_U32(a, n)      vshlq_n_u32(a, n)
#define VEC_SHR_U32(a, n)      vshrq_n_u32(a, n)

// === 64-bit unsigned (uint64x2_t) operations ================================

#define VEC_LOAD_U64(p)        vld1q_u64((const uint64_t *)(p))
#define VEC_STORE_U64(p, v)    vst1q_u64((uint64_t *)(p), (v))
#define VEC_SET1_U64(x)        vdupq_n_u64(x)
#define VEC_XOR_U64(a, b)      veorq_u64(a, b)
#define VEC_ADD_U64(a, b)      vaddq_u64(a, b)

typedef uint64x2_t VTYPE;

static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

#define XOR_NEON(a, b)  VEC_XOR_U64(a, b)
#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with NEON acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    VTYPE c01 = VEC_LOAD_U64(A + 0); \
    c01 = XOR_NEON(c01, VEC_LOAD_U64(A + 5)); \
    c01 = XOR_NEON(c01, VEC_LOAD_U64(A + 10)); \
    c01 = XOR_NEON(c01, VEC_LOAD_U64(A + 15)); \
    c01 = XOR_NEON(c01, VEC_LOAD_U64(A + 20)); \
    \
    VTYPE c23 = VEC_LOAD_U64(A + 2); \
    c23 = XOR_NEON(c23, VEC_LOAD_U64(A + 7)); \
    c23 = XOR_NEON(c23, VEC_LOAD_U64(A + 12)); \
    c23 = XOR_NEON(c23, VEC_LOAD_U64(A + 17)); \
    c23 = XOR_NEON(c23, VEC_LOAD_U64(A + 22)); \
    \
    VEC_STORE_U64(C + 0, c01); \
    VEC_STORE_U64(C + 2, c23); \
    C[4] = A[4] ^ A[9] ^ A[14] ^ A[19] ^ A[24]; \
    \
    uint64_t D[5]; \
    D[0] = C[4] ^ ROL64(C[1], 1); \
    D[1] = C[0] ^ ROL64(C[2], 1); \
    D[2] = C[1] ^ ROL64(C[3], 1); \
    D[3] = C[2] ^ ROL64(C[4], 1); \
    D[4] = C[3] ^ ROL64(C[0], 1)

#define THETA_APPLY_D() \
    do { \
        VTYPE vD01 = vcombine_u64(vdup_n_u64(D[0]), vdup_n_u64(D[1])); \
        VTYPE vD23 = vcombine_u64(vdup_n_u64(D[2]), vdup_n_u64(D[3])); \
        for (int y = 0; y < 5; y++) { \
            VTYPE row01 = VEC_LOAD_U64(A + y * 5); \
            VTYPE row23 = VEC_LOAD_U64(A + y * 5 + 2); \
            row01 = XOR_NEON(row01, vD01); \
            row23 = XOR_NEON(row23, vD23); \
            VEC_STORE_U64(A + y * 5, row01); \
            VEC_STORE_U64(A + y * 5 + 2, row23); \
            A[y * 5 + 4] ^= D[4]; \
        } \
    } while(0)

// ============================================================================
// Chi: Non-linear mixing
// ============================================================================

#define CHI_ROWS() \
    do { \
        for (int y = 0; y < 5; y++) { \
            int base = y * 5; \
            A[base + 0] = CHI_SCALAR(B[base + 0], B[base + 1], B[base + 2]); \
            A[base + 1] = CHI_SCALAR(B[base + 1], B[base + 2], B[base + 3]); \
            A[base + 2] = CHI_SCALAR(B[base + 2], B[base + 3], B[base + 4]); \
            A[base + 3] = CHI_SCALAR(B[base + 3], B[base + 4], B[base + 0]); \
            A[base + 4] = CHI_SCALAR(B[base + 4], B[base + 0], B[base + 1]); \
        } \
    } while(0)

// ============================================================================
// Round Constants
// ============================================================================

static const uint64_t s_round_constants[24] = {
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

// Rho rotation offsets (linearized 5x5 array)
static const unsigned s_rho[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
};

// Pi permutation: B[pi[i]] = A[i] after rho
// Pi: (x,y) -> (y, (2*x + 3*y) mod 5)
static const unsigned s_pi[25] = {
     0, 10, 20,  5, 15,
    16,  1, 11, 21,  6,
     7, 17,  2, 12, 22,
    23,  8, 18,  3, 13,
    14, 24,  9, 19,  4
};

// ============================================================================
// NEON Keccak-p[1600] Permutation (Lane)
// ============================================================================


void dap_hash_keccak_permute_neon(dap_hash_keccak_state_t *state)
{
    uint64_t *A = state->lanes;
    uint64_t B[25];
    
    for (unsigned round = 0; round < 24; round++) {
        // ====================================================================
        // Theta: Column parity mixing
        // C[x] = A[x,0] ^ A[x,1] ^ A[x,2] ^ A[x,3] ^ A[x,4]
        // D[x] = C[x-1] ^ ROL64(C[x+1], 1)
        // A[x,y] ^= D[x]
        // ====================================================================
        
        THETA_COMPUTE_PARITY();
        THETA_APPLY_D();
        
        // ====================================================================
        // Rho + Pi: Combined rotation and permutation
        // B[y, 2*x + 3*y] = ROL64(A[x,y], rho[x,y])
        // ====================================================================
        
        for (int i = 0; i < 25; i++) {
            B[s_pi[i]] = ROL64(A[i], s_rho[i]);
        }
        
        // ====================================================================
        // Chi: Non-linear mixing
        // A[x,y] = B[x,y] ^ (~B[x+1,y] & B[x+2,y])
        // ====================================================================
        
        CHI_ROWS();
        
        // ====================================================================
        // Iota: Round constant XOR
        // ====================================================================
        
        A[0] ^= s_round_constants[round];
    }
}
#endif
