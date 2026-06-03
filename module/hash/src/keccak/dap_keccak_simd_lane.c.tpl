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
 * @brief {{ARCH_NAME}} optimized Keccak-p[1600] permutation (Lane)
 * @details Auto-generated from lane template using dap_tpl.
 *
 * Lane layout: Traditional 25-lane state with SIMD acceleration for:
 *   - Column parity (Theta)
 *   - Chi non-linear step (ANDN/BIC)
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
// {{ARCH_NAME}} Keccak-p[1600] Permutation (Lane)
// ============================================================================

{{TARGET_ATTR}}
void dap_hash_keccak_permute_{{ARCH_LOWER}}(dap_hash_keccak_state_t *state)
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
