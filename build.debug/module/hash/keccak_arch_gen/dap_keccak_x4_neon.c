#if defined(__aarch64__) || defined(__arm__)
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
 * @file dap_keccak_x4_neon.c
 * @brief NEON optimized 4-way Keccak-p[1600]×4 permutation
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
#include <arm_neon.h>

#include "dap_hash_keccak_x4.h"

// ============================================================================
// Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// NEON Primitives for Keccak×4 (2-way parallel permutation, 2 passes for x4)
//
// Each LANE_T (uint64x2_t) holds the same Keccak lane from 2 instances.
// LANE_WIDTH = 2: two passes needed to cover all 4 instances.
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

#define LANE_T      uint64x2_t
#define LANE_WIDTH  2

#define LANE_LOAD(p)        VEC_LOAD_U64(p)
#define LANE_STORE(p, v)    VEC_STORE_U64(p, v)

#define LANE_XOR(a, b)      VEC_XOR_U64(a, b)
#define LANE_ANDN(a, b)     vbicq_u64((b), (a))    /* ~a & b */
#define LANE_OR(a, b)       vorrq_u64((a), (b))
#define LANE_SET1_64(x)     VEC_SET1_U64(x)

/* Variable-shift rotate via vshlq_u64 (works with runtime n) */
static inline uint64x2_t lane_rol64(uint64x2_t x, int n)
{
    if (__builtin_constant_p(n) && n == 0) return x;
    int64x2_t vn = vdupq_n_s64(n);
    int64x2_t vneg = vdupq_n_s64(n - 64);
    return vorrq_u64(vshlq_u64(x, vn), vshlq_u64(x, vneg));
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
// NEON Keccak-p[1600]×4 Permutation
// ============================================================================


void dap_keccak_x4_permute_neon(dap_keccak_x4_state_t *a_state)
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
