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
 * @file dap_keccak_sve.c
 * @brief SVE optimized Keccak-p[1600] permutation (Lane)
 * @details Auto-generated from lane template using dap_tpl.
 *
 * Lane layout: Traditional 25-lane state with SIMD acceleration for:
 *   - Column parity (Theta)
 *   - Chi non-linear step (ANDN/BIC)
 *
 * Key optimizations for SVE:
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
// SVE Architecture-Specific Primitives
// ============================================================================

// ============================================================================
// ARM SVE SIMD Primitives for Keccak (Lane Layout)
// ============================================================================

// ============================================================================
// ARM SVE Shared SIMD Primitives (scalable vectors, 128-2048 bits)
//
// SVE uses predicated operations. All ops require a governing predicate.
// Unlike NEON/x86, vector length is determined at runtime via svcntb() etc.
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

// === 64-bit (svuint64_t) operations =========================================

#define VEC_LOAD_U64_Z(pg, p)      svld1_u64(pg, (const uint64_t *)(p))
#define VEC_STORE_U64(pg, p, v)    svst1_u64(pg, (uint64_t *)(p), v)
#define VEC_SET1_U64(x)            svdup_u64(x)
#define VEC_XOR_U64_Z(pg, a, b)    sveor_u64_z(pg, a, b)
#define VEC_ADD_U64_Z(pg, a, b)    svadd_u64_z(pg, a, b)
#define VEC_SUB_U64_Z(pg, a, b)    svsub_u64_z(pg, a, b)
#define VEC_SHL_U64_Z(pg, a, n)    svlsl_n_u64_z(pg, a, n)
#define VEC_SHR_U64_Z(pg, a, n)    svlsr_n_u64_z(pg, a, n)

typedef svuint64_t VTYPE;

static inline uint64_t rol64(uint64_t x, unsigned n) {
    return (n == 0) ? x : ((x << n) | (x >> (64 - n)));
}
#define ROL64(x, n) rol64((x), (n))

#define CHI_SCALAR(a, b, c) ((a) ^ (~(b) & (c)))

// ============================================================================
// Theta: Column parity computation with SVE acceleration
// ============================================================================

#define THETA_COMPUTE_PARITY() \
    uint64_t C[5]; \
    svbool_t pg2 = VEC_WHILELT_64(0, 2); \
    \
    svuint64_t c01 = VEC_LOAD_U64_Z(pg2, A + 0); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 5)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 10)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 15)); \
    c01 = VEC_XOR_U64_Z(pg2, c01, VEC_LOAD_U64_Z(pg2, A + 20)); \
    \
    svuint64_t c23 = VEC_LOAD_U64_Z(pg2, A + 2); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 7)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 12)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 17)); \
    c23 = VEC_XOR_U64_Z(pg2, c23, VEC_LOAD_U64_Z(pg2, A + 22)); \
    \
    VEC_STORE_U64(pg2, C + 0, c01); \
    VEC_STORE_U64(pg2, C + 2, c23); \
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
        svbool_t pg2 = VEC_WHILELT_64(0, 2); \
        svuint64_t vD01 = VEC_LOAD_U64_Z(pg2, D); \
        svuint64_t vD23 = VEC_LOAD_U64_Z(pg2, D + 2); \
        for (int y = 0; y < 5; y++) { \
            svuint64_t row01 = VEC_LOAD_U64_Z(pg2, A + y * 5); \
            svuint64_t row23 = VEC_LOAD_U64_Z(pg2, A + y * 5 + 2); \
            row01 = VEC_XOR_U64_Z(pg2, row01, vD01); \
            row23 = VEC_XOR_U64_Z(pg2, row23, vD23); \
            VEC_STORE_U64(pg2, A + y * 5, row01); \
            VEC_STORE_U64(pg2, A + y * 5 + 2, row23); \
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
// SVE Keccak-p[1600] Permutation (Lane)
// ============================================================================

__attribute__((target("+sve")))
void dap_hash_keccak_permute_sve(dap_hash_keccak_state_t *state)
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
