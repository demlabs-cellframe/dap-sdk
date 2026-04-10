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

{{TARGET_ATTR}} __attribute__((noinline))
void dap_hash_keccak_permute_{{ARCH_LOWER}}(dap_hash_keccak_state_t *state)
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
