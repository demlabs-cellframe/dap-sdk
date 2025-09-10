/*
 * Enhanced Quantum-Resistant Commitments for ChipmunkRing
 * 
 * This header defines the implementation of multi-layer quantum-resistant
 * commitment schemes for small ring anonymity protection.
 * 
 * Security: 100+ bit quantum resistance for rings of any size
 * Performance: <2× overhead vs standard commitments
 * Dependencies: Pure post-quantum cryptographic assumptions
 */

#pragma once

#include "dap_common.h"
#include "chipmunk_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

// Enhanced commitment parameters
#define ENHANCED_RING_LWE_N 1024
#define ENHANCED_RING_LWE_Q 12289
#define ENHANCED_NTRU_N 509
#define ENHANCED_NTRU_Q 2048
#define ENHANCED_CODE_N 1024
#define ENHANCED_CODE_K 512
#define ENHANCED_ERROR_WEIGHT 64

// Quantum security levels
typedef enum quantum_security_level {
    QUANTUM_SECURITY_STANDARD = 0,    // Current ChipmunkRing (~3-6 bits for small rings)
    QUANTUM_SECURITY_ENHANCED = 1,    // Multi-layer commitments (~100+ bits)
    QUANTUM_SECURITY_MAXIMUM = 2      // Future: additional layers (~200+ bits)
} quantum_security_level_t;

// Enhanced Ring-LWE commitment structure
typedef struct enhanced_ring_lwe_commitment {
    uint16_t polynomial_coeffs[ENHANCED_RING_LWE_N];
    uint32_t modulus;
    uint8_t security_parameter;
} enhanced_ring_lwe_commitment_t;

// NTRU commitment structure  
typedef struct ntru_commitment {
    uint16_t ntru_coeffs[ENHANCED_NTRU_N];
    uint16_t modulus;
    uint8_t sparsity_parameter;
} ntru_commitment_t;

// Code-based commitment structure
typedef struct code_commitment {
    uint8_t syndrome[64];           // 512-bit syndrome
    uint16_t code_length;
    uint16_t code_dimension;
    uint8_t error_weight;
} code_commitment_t;

// Multi-layer quantum-resistant commitment
typedef struct quantum_resistant_commitment {
    enhanced_ring_lwe_commitment_t ring_lwe_layer;
    ntru_commitment_t ntru_layer;
    uint8_t hash_layer[64];         // SHAKE256 commitment
    code_commitment_t code_layer;
    uint8_t binding_proof[128];     // Proof that all layers commit to same value
    quantum_security_level_t security_level;
} quantum_resistant_commitment_t;

// Enhanced ChipmunkRing signature with quantum-resistant commitments
typedef struct chipmunk_ring_enhanced_signature {
    // Standard ChipmunkRing components (for backward compatibility)
    uint32_t ring_size;
    uint32_t signer_index;
    uint8_t linkability_tag[32];
    uint8_t challenge[32];
    
    // Enhanced quantum-resistant commitments
    quantum_resistant_commitment_t *enhanced_commitments;
    
    // Standard responses (reuse existing Schnorr-like responses)
    chipmunk_ring_response_t *responses;
    
    // Underlying Chipmunk signature (unchanged)
    uint8_t chipmunk_signature[CHIPMUNK_SIGNATURE_SIZE];
    
    // Metadata
    quantum_security_level_t security_level;
    uint32_t quantum_resistance_years;
} chipmunk_ring_enhanced_signature_t;

// API Functions

/**
 * @brief Initialize enhanced quantum-resistant commitment module
 */
int chipmunk_ring_enhanced_init(void);

/**
 * @brief Create quantum-resistant commitment
 * @param commitment Output commitment structure
 * @param public_key Public key to commit to
 * @param secret Secret value being committed
 * @param randomness Commitment randomness
 * @param security_level Desired quantum security level
 * @return 0 on success, negative on error
 */
int create_quantum_resistant_commitment(
    quantum_resistant_commitment_t *commitment,
    const chipmunk_ring_public_key_t *public_key,
    const uint8_t secret[32],
    const uint8_t randomness[32],
    quantum_security_level_t security_level
);

/**
 * @brief Verify quantum-resistant commitment
 * @param commitment Commitment to verify
 * @param public_key Public key used for commitment
 * @param secret Secret value to verify against
 * @param randomness Randomness used in commitment
 * @return 0 if valid, negative if invalid
 */
int verify_quantum_resistant_commitment(
    const quantum_resistant_commitment_t *commitment,
    const chipmunk_ring_public_key_t *public_key,
    const uint8_t secret[32],
    const uint8_t randomness[32]
);

/**
 * @brief Create enhanced ChipmunkRing signature with quantum-resistant commitments
 * @param private_key Signer's private key
 * @param message Message to sign
 * @param message_size Message size in bytes
 * @param ring Ring of public keys
 * @param signer_index Index of signer in ring
 * @param security_level Desired quantum security level
 * @param signature Output enhanced signature
 * @return 0 on success, negative on error
 */
int chipmunk_ring_enhanced_sign(
    const chipmunk_ring_private_key_t *private_key,
    const void *message, size_t message_size,
    const chipmunk_ring_container_t *ring,
    uint32_t signer_index,
    quantum_security_level_t security_level,
    chipmunk_ring_enhanced_signature_t *signature
);

/**
 * @brief Verify enhanced ChipmunkRing signature
 * @param signature Enhanced signature to verify
 * @param message Original message
 * @param message_size Message size in bytes  
 * @param ring Ring of public keys
 * @return 0 if valid, negative if invalid
 */
int chipmunk_ring_enhanced_verify(
    const chipmunk_ring_enhanced_signature_t *signature,
    const void *message, size_t message_size,
    const chipmunk_ring_container_t *ring
);

/**
 * @brief Get signature size for enhanced scheme
 * @param ring_size Number of participants
 * @param security_level Quantum security level
 * @return Required signature buffer size
 */
size_t chipmunk_ring_enhanced_get_signature_size(
    size_t ring_size,
    quantum_security_level_t security_level
);

/**
 * @brief Estimate quantum resistance duration
 * @param ring_size Ring size
 * @param security_level Security level used
 * @return Estimated years of quantum resistance
 */
uint32_t estimate_quantum_resistance_years(
    size_t ring_size,
    quantum_security_level_t security_level
);

/**
 * @brief Calculate optimal ring size for target quantum resistance
 * @param target_years Desired years of quantum resistance
 * @param security_level Available security level
 * @return Recommended minimum ring size
 */
size_t calculate_quantum_resistant_ring_size(
    uint32_t target_years,
    quantum_security_level_t security_level
);

/**
 * @brief Free enhanced signature resources
 * @param signature Signature to free
 */
void chipmunk_ring_enhanced_signature_free(chipmunk_ring_enhanced_signature_t *signature);

// Utility functions for quantum threat assessment

/**
 * @brief Assess current quantum threat level based on public information
 * @return Current estimated quantum threat level
 */
quantum_security_level_t assess_current_quantum_threat(void);

/**
 * @brief Check if quantum-resistant commitments are recommended
 * @param ring_size Target ring size
 * @param anonymity_duration_years Required anonymity duration
 * @return true if enhanced commitments recommended
 */
bool is_quantum_enhancement_recommended(
    size_t ring_size,
    uint32_t anonymity_duration_years
);

// Performance measurement utilities

typedef struct enhanced_commitment_performance {
    double creation_time_ms;
    double verification_time_ms;
    size_t memory_usage_bytes;
    size_t commitment_size_bytes;
} enhanced_commitment_performance_t;

/**
 * @brief Measure performance of enhanced commitments
 * @param ring_size Ring size to test
 * @param security_level Security level to test
 * @param iterations Number of test iterations
 * @param performance Output performance metrics
 * @return 0 on success
 */
int measure_enhanced_commitment_performance(
    size_t ring_size,
    quantum_security_level_t security_level,
    size_t iterations,
    enhanced_commitment_performance_t *performance
);

#ifdef __cplusplus
}
#endif
