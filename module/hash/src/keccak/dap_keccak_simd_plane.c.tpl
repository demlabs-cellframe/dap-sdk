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
 * @file dap_keccak_{{ARCH_LOWER}}.c
 * @brief {{ARCH_NAME}} optimized Keccak-p[1600] permutation (Plane-Based)
 * @details Auto-generated from plane-based template using dap_tpl.
 *
 * Plane-based layout: 5 SIMD registers hold 5 lanes each (B, G, K, M, S planes)
 * Requires: wide registers (512-bit+), ternarylogic, variable rotation, permutation
 *
 * Key optimizations for {{ARCH_NAME}}:
 * {{OPTIMIZATION_NOTES}}
 *
 * Performance target: {{PERF_TARGET}}
 *
 * @date 2026
 * @generated
 */

#include <stdint.h>
#include <string.h>
{{ARCH_INCLUDES}}

#include "dap_hash_keccak.h"

// ============================================================================
// {{ARCH_NAME}} Architecture-Specific Primitives
// ============================================================================

{{#include PRIMITIVES_FILE}}

// ============================================================================
// Round Constants (aligned for SIMD)
// ============================================================================

static const uint64_t s_round_constants[24] {{ALIGNMENT_ATTR}} = {
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
// {{ARCH_NAME}} Keccak-p[1600] Permutation (Plane-Based)
// ============================================================================

{{TARGET_ATTR}}
void dap_hash_keccak_permute_{{ARCH_LOWER}}(dap_hash_keccak_state_t *state)
{
    uint64_t *A = state->lanes;
    
    // Load state into 5 plane registers (B, G, K, M, S)
    VTYPE B = LOAD_PLANE(A + 0);
    VTYPE G = LOAD_PLANE(A + 5);
    VTYPE K = LOAD_PLANE(A + 10);
    VTYPE M = LOAD_PLANE(A + 15);
    VTYPE S = LOAD_PLANE(A + 20);
    
    // Temporary registers
    VTYPE t0, t1, t2, t3, t4;
    
    // ========================================================================
    // Single Round Macro - All 5 steps fused for maximum register reuse
    // ========================================================================
    
#define KECCAK_ROUND(i) \
do { \
    /* Theta: column parity XOR */ \
    t0 = XOR5(B, G, K, M, S); \
    t1 = PERMUTE_THETA_PREV(t0); \
    t0 = PERMUTE_THETA_NEXT(t0); \
    t0 = ROL1(t0); \
    B = XOR3(B, t0, t1); G = XOR3(G, t0, t1); \
    K = XOR3(K, t0, t1); M = XOR3(M, t0, t1); S = XOR3(S, t0, t1); \
    \
    /* Rho: variable rotation per lane */ \
    B = ROLV_B(B); G = ROLV_G(G); K = ROLV_K(K); M = ROLV_M(M); S = ROLV_S(S); \
    \
    /* Pi step 1: within-plane permutation */ \
    t0 = PERMUTE_PI1_B(B); t1 = PERMUTE_PI1_G(G); t2 = PERMUTE_PI1_K(K); \
    t3 = PERMUTE_PI1_M(M); t4 = PERMUTE_PI1_S(S); \
    \
    /* Chi: non-linear mixing (single instruction per plane!) */ \
    B = CHI(t0, t1, t2); G = CHI(t1, t2, t3); K = CHI(t2, t3, t4); \
    M = CHI(t3, t4, t0); S = CHI(t4, t0, t1); \
    \
    /* Iota: round constant XOR */ \
    B = XOR(B, LOAD_RC(i)); \
    \
    /* Pi step 2: cross-plane permutation */ \
    PI2_PERMUTE(B, G, K, M, S, t0, t1, t2, t3, t4); \
} while(0)
    
    // ========================================================================
    // 24 Rounds (Fully Unrolled for Maximum ILP)
    // ========================================================================
    
    KECCAK_ROUND(0);  KECCAK_ROUND(1);  KECCAK_ROUND(2);  KECCAK_ROUND(3);
    KECCAK_ROUND(4);  KECCAK_ROUND(5);  KECCAK_ROUND(6);  KECCAK_ROUND(7);
    KECCAK_ROUND(8);  KECCAK_ROUND(9);  KECCAK_ROUND(10); KECCAK_ROUND(11);
    KECCAK_ROUND(12); KECCAK_ROUND(13); KECCAK_ROUND(14); KECCAK_ROUND(15);
    KECCAK_ROUND(16); KECCAK_ROUND(17); KECCAK_ROUND(18); KECCAK_ROUND(19);
    KECCAK_ROUND(20); KECCAK_ROUND(21); KECCAK_ROUND(22); KECCAK_ROUND(23);
    
#undef KECCAK_ROUND
    
    // Store planes back to state
    STORE_PLANE(A + 0, B);
    STORE_PLANE(A + 5, G);
    STORE_PLANE(A + 10, K);
    STORE_PLANE(A + 15, M);
    STORE_PLANE(A + 20, S);
}
