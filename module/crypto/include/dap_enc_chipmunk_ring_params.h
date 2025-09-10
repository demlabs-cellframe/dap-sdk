/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

/**
 * @brief Chipmunk_Ring quantum-resistant commitment parameters
 *
 * Security Analysis:
 * - Ring-LWE attack complexity: 2^(0.292×n) operations
 * - Required logical qubits: ~4n×log₂(q) for quantum sieve
 * - For 100+ year security: need 2^200+ operations = n≥685
 * - Conservative choice: n=1024 → 2^300 operations, ~90,000 logical qubits
 */

// Chipmunk base parameters (must match chipmunk.h definitions)
#define CHIPMUNK_RING_CHIPMUNK_N_DEFAULT 256       // Chipmunk security parameter N
#define CHIPMUNK_RING_CHIPMUNK_GAMMA_DEFAULT 4     // Chipmunk gamma parameter

// ================ RING-LWE LAYER PARAMETERS ================

// Enhanced Ring-LWE layer parameters (~90,000 logical qubits required)
#define CHIPMUNK_RING_RING_LWE_N_DEFAULT 1024                    // Ring dimension for ~300-bit classical security
#define CHIPMUNK_RING_RING_LWE_Q_DEFAULT 40961                   // Prime modulus (2^15 + 2^13 + 1)
#define CHIPMUNK_RING_RING_LWE_SIGMA_NUMERATOR_DEFAULT 32        // Error distribution σ = 32/√(2π) ≈ 12.7
#define CHIPMUNK_RING_RING_LWE_BYTES_PER_COEFF_DEFAULT 2         // Conservative: 2 bytes per coefficient
#define CHIPMUNK_RING_RING_LWE_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_RING_LWE_N_DEFAULT * CHIPMUNK_RING_RING_LWE_BYTES_PER_COEFF_DEFAULT)

// ================ NTRU LAYER PARAMETERS ================

// NTRU layer parameters (~70,000 logical qubits required)
#define CHIPMUNK_RING_NTRU_N_DEFAULT 1024                        // NTRU dimension for ~250-bit classical security
#define CHIPMUNK_RING_NTRU_Q_DEFAULT 65537                       // Prime modulus (2^16 + 1)
#define CHIPMUNK_RING_NTRU_BYTES_PER_COEFF_DEFAULT 2             // Conservative: 2 bytes per coefficient
#define CHIPMUNK_RING_NTRU_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_NTRU_N_DEFAULT * CHIPMUNK_RING_NTRU_BYTES_PER_COEFF_DEFAULT)

// ================ POST-QUANTUM HASH LAYER PARAMETERS ================

// Post-quantum hash layer parameters
#define CHIPMUNK_RING_HASH_BIT_SIZE_DEFAULT 1024                 // 1024-bit output for 512-bit Grover resistance
#define CHIPMUNK_RING_HASH_BYTE_SIZE_DEFAULT (CHIPMUNK_RING_HASH_BIT_SIZE_DEFAULT / 8)
#define CHIPMUNK_RING_HASH_DOMAIN_SEP_DEFAULT "CHIPMUNK_RING_PQ_HASH_COMMIT_1024"

// ================ CODE-BASED LAYER PARAMETERS ================

// Code-based layer parameters (~60,000 logical qubits required)
#define CHIPMUNK_RING_CODE_N_DEFAULT 2048                        // Code length for ~200-bit classical security
#define CHIPMUNK_RING_CODE_K_DEFAULT 1024                        // Code dimension
#define CHIPMUNK_RING_CODE_T_DEFAULT 128                         // Error weight
#define CHIPMUNK_RING_CODE_SYNDROME_BITS_DEFAULT 512             // Syndrome size in bits
#define CHIPMUNK_RING_CODE_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_CODE_SYNDROME_BITS_DEFAULT / 8)

// ================ BINDING PROOF PARAMETERS ================

// Binding proof parameters
#define CHIPMUNK_RING_BINDING_PROOF_BIT_SIZE_DEFAULT 1024        // 1024-bit binding proof
#define CHIPMUNK_RING_BINDING_PROOF_SIZE_DEFAULT (CHIPMUNK_RING_BINDING_PROOF_BIT_SIZE_DEFAULT / 8)

// ================ COMPUTED PARAMETERS ================

// Randomness and configurable sizes
#define CHIPMUNK_RING_RANDOMNESS_SIZE_DEFAULT 32                 // 256-bit randomness for commitment (default)
#define CHIPMUNK_RING_CHALLENGE_SIZE 32                          // 256-bit challenge size
#define CHIPMUNK_RING_LINKABILITY_TAG_SIZE 32                    // 256-bit linkability tag
#define CHIPMUNK_RING_RESPONSE_SIZE 32                           // 256-bit response size

// Input buffer size constants for commitment creation
#define CHIPMUNK_RING_RING_LWE_INPUT_EXTRA 16                    // Extra bytes for Ring-LWE parameters (2×8 bytes)
#define CHIPMUNK_RING_NTRU_INPUT_EXTRA 16                        // Extra bytes for NTRU parameters (2×8 bytes)
#define CHIPMUNK_RING_HASH_INPUT_EXTRA 64                        // Extra bytes for hash domain separation
#define CHIPMUNK_RING_CODE_INPUT_EXTRA 24                        // Extra bytes for code parameters (3×8 bytes)

// Maximum ring size
#define CHIPMUNK_RING_MAX_RING_SIZE 1024

// ================ COMPUTED SECURITY LEVELS ================

// Classical security level calculations (approximate)
#define CHIPMUNK_RING_RING_LWE_CLASSICAL_SECURITY_BITS (CHIPMUNK_RING_RING_LWE_N_DEFAULT * 292 / 1000)  // ~300 bits
#define CHIPMUNK_RING_NTRU_CLASSICAL_SECURITY_BITS (CHIPMUNK_RING_NTRU_N_DEFAULT * 250 / 1000)           // ~250 bits
#define CHIPMUNK_RING_CODE_CLASSICAL_SECURITY_BITS (CHIPMUNK_RING_CODE_N_DEFAULT * 200 / 1000)           // ~400 bits

// Quantum security level calculations (approximate)
#define CHIPMUNK_RING_RING_LWE_QUANTUM_SECURITY_BITS (CHIPMUNK_RING_RING_LWE_N_DEFAULT * 292 / 1000)    // ~300 bits
#define CHIPMUNK_RING_NTRU_QUANTUM_SECURITY_BITS (CHIPMUNK_RING_NTRU_N_DEFAULT * 250 / 1000)             // ~250 bits
#define CHIPMUNK_RING_CODE_QUANTUM_SECURITY_BITS (CHIPMUNK_RING_CODE_N_DEFAULT * 200 / 1000)             // ~400 bits

// ================ COMPUTED LOGICAL QUBITS REQUIRED ================

// Logical qubits needed for quantum attacks (conservative estimates)
#define CHIPMUNK_RING_RING_LWE_QUBITS_REQUIRED (CHIPMUNK_RING_RING_LWE_N_DEFAULT * 4 * 15)  // ~4n×log₂(q) ≈ 61,440
#define CHIPMUNK_RING_NTRU_QUBITS_REQUIRED (CHIPMUNK_RING_NTRU_N_DEFAULT * 4 * 16)          // ~4n×log₂(q) ≈ 65,536
#define CHIPMUNK_RING_CODE_QUBITS_REQUIRED (CHIPMUNK_RING_CODE_N_DEFAULT * 2)               // ~2n ≈ 4,096
#define CHIPMUNK_RING_HASH_QUBITS_REQUIRED (CHIPMUNK_RING_HASH_OUTPUT_SIZE_DEFAULT * 8 / 2) // ~512 qubits (Grover)

// Total logical qubits required for full quantum attack
#define CHIPMUNK_RING_TOTAL_QUBITS_REQUIRED (CHIPMUNK_RING_RING_LWE_QUBITS_REQUIRED + \
                                             CHIPMUNK_RING_NTRU_QUBITS_REQUIRED + \
                                             CHIPMUNK_RING_CODE_QUBITS_REQUIRED + \
                                             CHIPMUNK_RING_HASH_QUBITS_REQUIRED)

// ================ COMPUTED SIZES BASED ON PARAMETERS ================

// Dynamic size calculations based on chipmunk parameters
#define CHIPMUNK_RING_PUBLIC_KEY_SIZE(chipmunk_n)  (32 + (chipmunk_n)*4*2)  // rho_seed + v0 + v1
#define CHIPMUNK_RING_PRIVATE_KEY_SIZE(chipmunk_n) (32 + 48 + CHIPMUNK_RING_PUBLIC_KEY_SIZE(chipmunk_n))  // key_seed + tr + public_key
#define CHIPMUNK_RING_SIGNATURE_SIZE(chipmunk_n, chipmunk_gamma) ((chipmunk_n)*4*(chipmunk_gamma))  // sigma[GAMMA]

// ================ PERFORMANCE CONSTANTS ================

// Expected signature sizes (approximate)
#define CHIPMUNK_RING_SIGNATURE_SIZE_DEFAULT (sizeof(uint32_t) + 128 + CHIPMUNK_RING_MAX_RING_SIZE * 32)  // ~33KB for max ring

// Commitment size calculation
#define CHIPMUNK_RING_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_RANDOMNESS_SIZE + \
                                               CHIPMUNK_RING_RING_LWE_COMMITMENT_SIZE_DEFAULT + \
                                               CHIPMUNK_RING_NTRU_COMMITMENT_SIZE_DEFAULT + \
                                               CHIPMUNK_RING_HASH_OUTPUT_SIZE_DEFAULT + \
                                               CHIPMUNK_RING_CODE_COMMITMENT_SIZE_DEFAULT + \
                                               CHIPMUNK_RING_BINDING_PROOF_SIZE_DEFAULT)
