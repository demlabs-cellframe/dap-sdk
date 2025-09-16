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

// ================ CHIPMUNK CORE CONSTANTS ================

// Cryptographic constants (replace magic numbers)
#define CHIPMUNK_RING_RHO_SEED_SIZE 32             // Size of rho_seed in bytes
#define CHIPMUNK_RING_KEY_SEED_SIZE 32             // Size of key_seed in bytes  
#define CHIPMUNK_RING_TR_SIZE 48                   // Size of tr (public key commitment) in bytes
#define CHIPMUNK_RING_COEFF_SIZE 4                 // Size of polynomial coefficient in bytes (int32_t)
#define CHIPMUNK_RING_POLY_COUNT_PUBLIC 2          // Number of polynomials in public key (v0, v1)
#define CHIPMUNK_RING_MODULUS_BYTES 4              // Size of modulus in bytes (32-bit)

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


// ================ CODE-BASED LAYER PARAMETERS ================

// Code-based layer parameters (~80,000 logical qubits required)
#define CHIPMUNK_RING_CODE_N_DEFAULT 3072                        // Code length strengthened (+1024 from Hash)
#define CHIPMUNK_RING_CODE_K_DEFAULT 1536                        // Code dimension (proportional increase)
#define CHIPMUNK_RING_CODE_T_DEFAULT 192                         // Error weight (proportional increase)
#define CHIPMUNK_RING_CODE_SYNDROME_BITS_DEFAULT 1536            // Syndrome size strengthened (+1024 bits)
#define CHIPMUNK_RING_CODE_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_CODE_SYNDROME_BITS_DEFAULT / 8)  // 192 bytes (+128)

// ================ BINDING PROOF PARAMETERS ================

// Optimized Merkle Tree binding proof parameters  
#define CHIPMUNK_RING_BINDING_PROOF_BIT_SIZE_DEFAULT 256         // 256-bit Merkle root (sufficient for binding)
#define CHIPMUNK_RING_BINDING_PROOF_SIZE_DEFAULT (CHIPMUNK_RING_BINDING_PROOF_BIT_SIZE_DEFAULT / 8)

// ================ COMPUTED COMMITMENT LAYER SIZES ================

// Ring-LWE commitment layer size (computed from parameters)
#define CHIPMUNK_RING_RING_LWE_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_RING_LWE_N_DEFAULT * CHIPMUNK_RING_RING_LWE_BYTES_PER_COEFF_DEFAULT)

// NTRU commitment layer size (computed from parameters)
#define CHIPMUNK_RING_NTRU_COMMITMENT_SIZE_DEFAULT (CHIPMUNK_RING_NTRU_N_DEFAULT * CHIPMUNK_RING_NTRU_BYTES_PER_COEFF_DEFAULT)

// ================ QUANTUM-RESISTANT LAYER CONSTANTS ================

// Number of quantum-resistant layers (replace magic number 4)
#define CHIPMUNK_RING_QR_LAYER_COUNT 4             // ring_lwe, ntru, code, binding

// Header field counts (replace magic numbers)
#define CHIPMUNK_RING_HEADER_PARAMS_COUNT 3        // chipmunk_n, chipmunk_gamma, randomness_size
#define CHIPMUNK_RING_ZK_PARAMS_COUNT 3            // zk_proof_size, zk_iterations, coordination_round

// ================ ACORN VERIFICATION PARAMETERS ================

// Acorn Verification scheme constants
#define CHIPMUNK_RING_ACORN_VERIFICATION_VERSION 1     // Version of Acorn scheme
#define CHIPMUNK_RING_ACORN_PROOF_SIZE 96              // Standard Acorn proof size (96 bytes)
#define CHIPMUNK_RING_ACORN_MIN_ENTROPY_RATIO 0.1      // Minimum entropy ratio for valid proofs
#define CHIPMUNK_RING_ACORN_PROOF_PREFIX "ACORN_"      // Prefix for domain separation

// ================ COMPUTED PARAMETERS ================

// Randomness and configurable sizes
#define CHIPMUNK_RING_RANDOMNESS_SIZE_DEFAULT 32                 // 256-bit randomness for commitment (default)
#define CHIPMUNK_RING_CHALLENGE_SIZE 32                          // 256-bit challenge size
#define CHIPMUNK_RING_LINKABILITY_TAG_SIZE 32                    // 256-bit linkability tag
#define CHIPMUNK_RING_RESPONSE_SIZE_DEFAULT 64                   // Default response size (matches ZK proof default)
#define CHIPMUNK_RING_RESPONSE_SIZE_MIN 32                       // Minimum response size
#define CHIPMUNK_RING_RESPONSE_SIZE_MAX 128                      // Maximum response size

// Hash sizes for different components
#define CHIPMUNK_RING_HASH_SIZE 32                               // Standard hash size (SHA3-256)
#define CHIPMUNK_RING_KEY_HASH_SIZE 32                           // Public key hash size
#define CHIPMUNK_RING_RING_HASH_SIZE 32                          // Ring hash size

// ================ ZK PROOF PARAMETERS ================

// Zero-Knowledge proof sizes (configurable for different security levels)
#define CHIPMUNK_RING_ZK_PROOF_SIZE_DEFAULT 64   ///< Default ZK proof size (SHA3-512)
#define CHIPMUNK_RING_ZK_PROOF_SIZE_MIN     32   ///< Minimum ZK proof size (SHA3-256)
#define CHIPMUNK_RING_ZK_PROOF_SIZE_MAX     128  ///< Maximum ZK proof size (SHAKE-128 extended)
#define CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE 96  ///< Enterprise-grade ZK proof size

// ZK proof serialization format: [uint32_t length][proof_data]
#define CHIPMUNK_RING_ZK_PROOF_LENGTH_PREFIX_SIZE 4  ///< Size of length prefix (uint32_t)

// ZK proof iterations for enhanced security
#define CHIPMUNK_RING_ZK_ITERATIONS_DEFAULT     100   ///< Default iterations (fast)
#define CHIPMUNK_RING_ZK_ITERATIONS_SECURE      1000  ///< Secure iterations (medium)
#define CHIPMUNK_RING_ZK_ITERATIONS_ENTERPRISE  10000 ///< Enterprise iterations (slow but secure)
#define CHIPMUNK_RING_ZK_ITERATIONS_MAX         50000 ///< Maximum allowed iterations

// Security levels for different modes
#define CHIPMUNK_RING_SECURITY_LEVEL_SINGLE     256   ///< Security level for single signer mode
#define CHIPMUNK_RING_SECURITY_LEVEL_MULTI      512   ///< Security level for multi-signer mode
#define CHIPMUNK_RING_SECURITY_LEVEL_ENTERPRISE 1024  ///< Security level for enterprise mode

// Hash algorithm preferences
#define CHIPMUNK_RING_HASH_ALGORITHM_UNIVERSAL  DAP_HASH_TYPE_SHAKE256  ///< Universal hash for all ZK proofs

// Domain separators for different contexts
#define CHIPMUNK_RING_DOMAIN_ZK_PROOF           "ChipmunkRing-ZK-Proof"
#define CHIPMUNK_RING_DOMAIN_ENTERPRISE_ZK      "ChipmunkRing-Enterprise-ZK"
#define CHIPMUNK_RING_DOMAIN_SIGNATURE_ZK       "ChipmunkRing-Signature-ZK"
#define CHIPMUNK_RING_DOMAIN_COORDINATION       "ChipmunkRing-Coordination"

// Acorn domain separators (must match between creation and verification)
#define CHIPMUNK_RING_DOMAIN_ACORN_RANDOMNESS   "ACORN_RANDOMNESS_V1"
#define CHIPMUNK_RING_DOMAIN_ACORN_COMMITMENT   "ACORN_COMMITMENT_V1"
#define CHIPMUNK_RING_DOMAIN_ACORN_LINKABILITY  "ACORN_LINKABILITY_V1"

// Scalability thresholds
#define CHIPMUNK_RING_SMALL_RING_THRESHOLD      16    ///< Threshold for embedded vs external keys
#define CHIPMUNK_RING_LARGE_RING_THRESHOLD      64    ///< Threshold for performance optimization
#define CHIPMUNK_RING_MASSIVE_RING_THRESHOLD    256   ///< Threshold for special handling

// ================ SERIALIZATION FLAGS ================

/**
 * @brief Scalability flags for signature serialization
 */
typedef enum chipmunk_ring_scalability_flags {
    CHIPMUNK_RING_FLAG_NONE = 0x00,
    CHIPMUNK_RING_FLAG_EMBEDDED_KEYS = 0x01,      ///< Bit 0: embedded keys mode
    CHIPMUNK_RING_FLAG_COORDINATED = 0x02,        ///< Bit 1: coordination completed
    CHIPMUNK_RING_FLAG_MULTI_SIGNER = 0x04,       ///< Bit 2: multi-signer mode
    CHIPMUNK_RING_FLAG_ENTERPRISE = 0x08,         ///< Bit 3: enterprise security level
    CHIPMUNK_RING_FLAG_EXTERNAL_KEYS = 0x10,      ///< Bit 4: external key storage
    CHIPMUNK_RING_FLAG_ZK_ENHANCED = 0x20,        ///< Bit 5: enhanced ZK proofs
    CHIPMUNK_RING_FLAG_LINKABILITY_ENABLED = 0x40, ///< Bit 6: linkability tag enabled
    CHIPMUNK_RING_FLAG_FUTURE_2 = 0x80            ///< Bit 7: reserved for future use
} chipmunk_ring_scalability_flags_t;

// Linkability options
#define CHIPMUNK_RING_LINKABILITY_DISABLED     0     ///< No linkability tag (maximum anonymity)
#define CHIPMUNK_RING_LINKABILITY_MESSAGE_ONLY 1     ///< Link only same message (anti-replay)
#define CHIPMUNK_RING_LINKABILITY_FULL         2     ///< Full linkability (anti-double-spend)

// Input buffer size constants for commitment creation
#define CHIPMUNK_RING_RING_LWE_INPUT_EXTRA 16                    // Extra bytes for Ring-LWE parameters (2×8 bytes)
#define CHIPMUNK_RING_NTRU_INPUT_EXTRA 16                        // Extra bytes for NTRU parameters (2×8 bytes)
#define CHIPMUNK_RING_HASH_INPUT_EXTRA 64                        // Extra bytes for hash domain separation
#define CHIPMUNK_RING_CODE_INPUT_EXTRA 24                        // Extra bytes for code parameters (3×8 bytes)

// Maximum ring size
#define CHIPMUNK_RING_MAX_RING_SIZE 1024
#define CHIPMUNK_RING_MAX_MESSAGE_SIZE (1024 * 1024)  // 1MB maximum message size

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

// ================ DOMAIN SEPARATORS FOR ZK PROOFS ================

// Domain separators must match between creation and verification
#define CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER    "CHIPMUNK_RING_ZK_MULTI"
#define CHIPMUNK_RING_ZK_DOMAIN_SINGLE_SIGNER   "CHIPMUNK_RING_ZK_SINGLE"
#define CHIPMUNK_RING_ZK_DOMAIN_THRESHOLD       "CHIPMUNK_RING_ZK_THRESHOLD"
#define CHIPMUNK_RING_ZK_DOMAIN_SECRET_SHARING  "CHIPMUNK_RING_ZK_SECRET_SHARE"
#define CHIPMUNK_RING_ZK_DOMAIN_COMMITMENT      "CHIPMUNK_RING_ZK_COMMITMENT"
#define CHIPMUNK_RING_ZK_DOMAIN_RESPONSE        "CHIPMUNK_RING_ZK_RESPONSE"
#define CHIPMUNK_RING_ZK_DOMAIN_ENTERPRISE      "CHIPMUNK_RING_ZK_ENTERPRISE"

// Context-specific domain separators
#define CHIPMUNK_RING_ZK_DOMAIN_COORDINATION    "CHIPMUNK_RING_ZK_COORD"
#define CHIPMUNK_RING_ZK_DOMAIN_AGGREGATION     "CHIPMUNK_RING_ZK_AGGR"
#define CHIPMUNK_RING_ZK_DOMAIN_VERIFICATION    "CHIPMUNK_RING_ZK_VERIFY"
