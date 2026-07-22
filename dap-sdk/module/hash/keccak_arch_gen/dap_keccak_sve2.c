#if defined(__aarch64__) && !defined(__APPLE__)
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
 * @file dap_keccak_sve2.c
 * @brief SVE2 optimized Keccak-p[1600] permutation (Plane-Based)
 * @details Auto-generated from plane-based template using dap_tpl.
 *
 * Plane-based layout: 5 SIMD registers hold 5 lanes each (B, G, K, M, S planes)
 * Requires: wide registers (512-bit+), ternarylogic, variable rotation, permutation
 *
 * Key optimizations for SVE2:
 * 
 *
 * Performance target: 
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <string.h>
#include <arm_sve.h>

#include "dap_hash_keccak.h"

// ============================================================================
// SVE2 Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// ARM SVE2 SIMD Primitives for Keccak (Plane Layout)
// ============================================================================

// ============================================================================
// ARM SVE2 Shared SIMD Primitives (scalable vectors, 128-2048 bits)
//
// SVE2 extends SVE with: EOR3 (3-way XOR), BCAX (bit clear and xor),
// MATCH/NMATCH for byte-level search, and more.
// ============================================================================

#include <arm_sve.h>

// === Predicate helpers ======================================================

#define VEC_PTRUE_8()    svptrue_b8()
#define VEC_PTRUE_16()   svptrue_b16()
#define VEC_PTRUE_32()   svptrue_b32()
#define VEC_PTRUE_64()   svptrue_b64()

#define VEC_WHILELT_8(a, b)   svwhilelt_b8(a, b)
#define VEC_WHILELT_64(a, b)  svwhilelt_b64(a, b)

// === 8-bit (svuint8_t) operations ===========================================

#define VEC_LOAD_U8_Z(pg, p)       svld1_u8(pg, (const uint8_t *)(p))
#define VEC_STORE_U8(pg, p, v)     svst1_u8(pg, (uint8_t *)(p), v)
#define VEC_SET1_U8(x)             svdup_n_u8(x)
#define VEC_CMPEQ_U8(pg, a, b)     svcmpeq_u8(pg, a, b)
#define VEC_OR_PRED(a, b)          svorr_b_z(svptrue_b8(), a, b)
#define VEC_PRED_ANY(pg, pred)     svptest_any(pg, pred)
#define VEC_MATCH_U8(pg, a, b)     svmatch_u8(pg, a, b)

// === 64-bit (svuint64_t) operations =========================================

#define VEC_LOAD_U64_Z(pg, p)      svld1_u64(pg, (const uint64_t *)(p))
#define VEC_STORE_U64(pg, p, v)    svst1_u64(pg, (uint64_t *)(p), v)
#define VEC_SET1_U64(x)            svdup_u64(x)
#define VEC_XOR_U64_Z(pg, a, b)    sveor_u64_z(pg, a, b)
#define VEC_ADD_U64_Z(pg, a, b)    svadd_u64_z(pg, a, b)
#define VEC_SUB_U64_Z(pg, a, b)    svsub_u64_z(pg, a, b)
#define VEC_SHL_U64_Z(pg, a, n)    svlsl_n_u64_z(pg, a, n)
#define VEC_SHR_U64_Z(pg, a, n)    svlsr_n_u64_z(pg, a, n)

// === SVE2-specific multi-operand ops ========================================

#define VEC_XOR3_U64(a, b, c)      sveor3_u64(a, b, c)
#define VEC_BCAX_U64(a, b, c)      svbcax_u64(a, c, b)

// === Permutation (table lookup) =============================================

#define VEC_TBL_U64(a, idx)        svtbl_u64(a, idx)
#define VEC_ZIP1_U64(a, b)         svzip1_u64(a, b)
#define VEC_ZIP2_U64(a, b)         svzip2_u64(a, b)
#define VEC_UZP1_U64(a, b)         svuzp1_u64(a, b)
#define VEC_UZP2_U64(a, b)         svuzp2_u64(a, b)
#define VEC_EXT_U64(a, b, n)       svext_u64(a, b, n)

typedef svuint64_t VTYPE;

#define PG5 svwhilelt_b64(0, 5)

#define XOR(a, b)       VEC_XOR_U64_Z(VEC_PTRUE_64(), a, b)
#define XOR3(a, b, c)   VEC_XOR3_U64(a, b, c)
#define XOR5(a,b,c,d,e) XOR3(XOR3(a, b, c), d, e)
#define CHI(a, b, c)    VEC_BCAX_U64(a, b, c)

#define LOAD_PLANE(a)   VEC_LOAD_U64_Z(PG5, a)
#define STORE_PLANE(a, v) VEC_STORE_U64(PG5, a, v)
#define LOAD_RC(i)      VEC_LOAD_U64_Z(svwhilelt_b64(0, 1), s_round_constants + (i))

#define ROL1(x)         svorr_u64_z(VEC_PTRUE_64(), VEC_SHL_U64_Z(VEC_PTRUE_64(), x, 1), VEC_SHR_U64_Z(VEC_PTRUE_64(), x, 63))

// Theta permutation indices
static const uint64_t s_theta_prev_idx[8] = {4, 0, 1, 2, 3, 5, 6, 7};
static const uint64_t s_theta_next_idx[8] = {1, 2, 3, 4, 0, 5, 6, 7};

#define PERMUTE_THETA_PREV(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_theta_prev_idx))
#define PERMUTE_THETA_NEXT(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_theta_next_idx))

// Rho rotation offsets per plane
static const uint64_t s_rho_B_off[8] = { 0,  1, 62, 28, 27, 0, 0, 0};
static const uint64_t s_rho_G_off[8] = {36, 44,  6, 55, 20, 0, 0, 0};
static const uint64_t s_rho_K_off[8] = { 3, 10, 43, 25, 39, 0, 0, 0};
static const uint64_t s_rho_M_off[8] = {41, 45, 15, 21,  8, 0, 0, 0};
static const uint64_t s_rho_S_off[8] = {18,  2, 61, 56, 14, 0, 0, 0};

static inline VTYPE rolv_sve2(VTYPE x, const uint64_t *offsets) {
    svuint64_t off = VEC_LOAD_U64_Z(VEC_PTRUE_64(), offsets);
    svuint64_t off_neg = VEC_SUB_U64_Z(VEC_PTRUE_64(), VEC_SET1_U64(64), off);
    return svorr_u64_z(VEC_PTRUE_64(),
        svlsl_u64_z(VEC_PTRUE_64(), x, off),
        svlsr_u64_z(VEC_PTRUE_64(), x, off_neg));
}

#define ROLV_B(x) rolv_sve2(x, s_rho_B_off)
#define ROLV_G(x) rolv_sve2(x, s_rho_G_off)
#define ROLV_K(x) rolv_sve2(x, s_rho_K_off)
#define ROLV_M(x) rolv_sve2(x, s_rho_M_off)
#define ROLV_S(x) rolv_sve2(x, s_rho_S_off)

// Pi step 1: within-plane permutation indices
static const uint64_t s_pi1_B_idx[8] = {0, 3, 1, 4, 2, 5, 6, 7};
static const uint64_t s_pi1_G_idx[8] = {1, 4, 2, 0, 3, 5, 6, 7};
static const uint64_t s_pi1_K_idx[8] = {2, 0, 3, 1, 4, 5, 6, 7};
static const uint64_t s_pi1_M_idx[8] = {3, 1, 4, 2, 0, 5, 6, 7};
static const uint64_t s_pi1_S_idx[8] = {4, 2, 0, 3, 1, 5, 6, 7};

#define PERMUTE_PI1_B(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_B_idx))
#define PERMUTE_PI1_G(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_G_idx))
#define PERMUTE_PI1_K(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_K_idx))
#define PERMUTE_PI1_M(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_M_idx))
#define PERMUTE_PI1_S(x) VEC_TBL_U64(x, VEC_LOAD_U64_Z(VEC_PTRUE_64(), s_pi1_S_idx))

// Pi step 2: cross-plane permutation (complex, using zip/unzip)
#define PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4) \
do { \
    t0 = VEC_ZIP1_U64(B, G); \
    t1 = VEC_ZIP1_U64(K, M); \
    t2 = VEC_ZIP2_U64(B, G); \
    t3 = VEC_ZIP2_U64(K, M); \
    \
    VTYPE newB = VEC_UZP1_U64(t0, t1); \
    VTYPE newG = VEC_UZP1_U64(t2, t3); \
    VTYPE newK = VEC_UZP2_U64(t0, t1); \
    VTYPE newM = VEC_UZP2_U64(t2, t3); \
    \
    t4 = VEC_EXT_U64(S, S, 4); \
    \
    B = newB; G = newG; K = newK; M = newM; \
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
// SVE2 Keccak-p[1600] Permutation (Plane-Based)
// ============================================================================

__attribute__((target("+sve2"))) __attribute__((noinline))
void dap_hash_keccak_permute_sve2(dap_hash_keccak_state_t *state)
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
