/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2024
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

#pragma once

#include "dap_common.h"

/**
 * @file chipmunk.h
 * @brief Chipmunk post-quantum signature algorithm
 * @details Chipmunk is a lattice-based digital signature scheme built on top of
 * the module learning with errors (MLWE) problem. It is designed to be secure against quantum
 * attacks and provide efficient signature generation and verification.
 * Based on research paper: https://eprint.iacr.org/2023/1820
 */

// Error codes
#define CHIPMUNK_SIGNATURE_SIZE    (32 + CHIPMUNK_N*4 + CHIPMUNK_N/8)

/**
 * @brief Error codes for Chipmunk operations
 */
enum chipmunk_error_t {
    CHIPMUNK_ERROR_SUCCESS = 0,        ///< Operation completed successfully
    CHIPMUNK_ERROR_NULL_PARAM = -1,    ///< NULL parameter passed to function
    CHIPMUNK_ERROR_BUFFER_TOO_SMALL = -2, ///< Output buffer too small
    CHIPMUNK_ERROR_HASH_FAILED = -3,   ///< Hash operation failed
    CHIPMUNK_ERROR_INIT_FAILED = -4,   ///< Initialization failed
    CHIPMUNK_ERROR_OVERFLOW = -5,      ///< Arithmetic overflow detected
    CHIPMUNK_ERROR_INVALID_PARAM = -6, ///< Invalid parameter value
    CHIPMUNK_ERROR_MEMORY = -7,        ///< Memory allocation failed
    CHIPMUNK_ERROR_INTERNAL = -8,      ///< Internal error
    CHIPMUNK_ERROR_VERIFY_FAILED = -9, ///< Verification failed
    CHIPMUNK_ERROR_INVALID_SIZE = -10  ///< Invalid size
};

// Algorithm parameters
#define CHIPMUNK_N           256             // Полиномиальная степень
#define CHIPMUNK_Q           8380417        // Модуль
#define CHIPMUNK_D           13             // Параметр усечения
#define CHIPMUNK_TAU         39             // Вес полинома challenge
#define CHIPMUNK_GAMMA1      (1 << 17)      // Параметр гамма 1
#define CHIPMUNK_GAMMA2      (CHIPMUNK_Q/2 - 1) // Параметр гамма 2
#define CHIPMUNK_K           4               // Number of polynomials in public key

// Key and signature sizes
#define CHIPMUNK_PRIVATE_KEY_SIZE (CHIPMUNK_N*6 + 32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE)
#define CHIPMUNK_PUBLIC_KEY_SIZE  (CHIPMUNK_N*3 + 32)

/**
 * @brief Polynomial structure used in Chipmunk operations
 * @details Represents a polynomial with CHIPMUNK_N integer coefficients
 */
typedef struct chipmunk_poly {
    int32_t coeffs[CHIPMUNK_N];  ///< Array of polynomial coefficients
} chipmunk_poly_t;

/**
 * @brief Public key structure for Chipmunk algorithm
 */
typedef struct chipmunk_public_key {
    chipmunk_poly_t h;   ///< Public key polynomial h
    chipmunk_poly_t rho; ///< Seed for matrix A generation
} chipmunk_public_key_t;

/**
 * @brief Private key structure for Chipmunk algorithm
 */
typedef struct chipmunk_private_key {
    chipmunk_poly_t s1;      ///< Private key polynomial s1
    chipmunk_poly_t s2;      ///< Private key polynomial s2
    uint8_t key_seed[32];    ///< Seed used for key generation
    uint8_t tr[48];          ///< Public key commitment
    chipmunk_public_key_t pk; ///< Embedded public key
} chipmunk_private_key_t;

/**
 * @brief Signature structure for Chipmunk algorithm
 */
typedef struct chipmunk_signature {
    chipmunk_poly_t z;   ///< Response polynomial z
    uint8_t c[32];       ///< Challenge seed
    uint8_t hint[CHIPMUNK_N/8]; ///< Hint bits for verification
} chipmunk_signature_t;

/**
 * @brief Initialize Chipmunk module
 * 
 * Must be called before any other Chipmunk functions
 * 
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_init(void);

/**
 * @brief Generate a Chipmunk keypair
 * 
 * @param[out] a_public_key Buffer to store public key, must be at least CHIPMUNK_PUBLIC_KEY_SIZE bytes
 * @param[in] a_public_key_size Size of public key buffer
 * @param[out] a_private_key Buffer to store private key, must be at least CHIPMUNK_PRIVATE_KEY_SIZE bytes
 * @param[in] a_private_key_size Size of private key buffer
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_keypair(uint8_t *a_public_key, size_t a_public_key_size,
                    uint8_t *a_private_key, size_t a_private_key_size);

/**
 * @brief Sign a message using Chipmunk algorithm
 * 
 * @param[in] a_private_key Private key buffer generated by chipmunk_keypair
 * @param[in] a_message Message to sign
 * @param[in] a_message_len Length of the message in bytes
 * @param[out] a_signature Output signature buffer, must be at least CHIPMUNK_SIGNATURE_SIZE bytes
 * @return 0 on success, negative error code on failure
 */
int chipmunk_sign(const uint8_t *a_private_key, const uint8_t *a_message, 
                 size_t a_message_len, uint8_t *a_signature);

/**
 * @brief Verify a Chipmunk signature
 * 
 * @param[in] a_public_key Public key buffer generated by chipmunk_keypair
 * @param[in] a_message The original message that was signed
 * @param[in] a_message_len Length of the message in bytes
 * @param[in] a_signature Signature to verify, must be CHIPMUNK_SIGNATURE_SIZE bytes
 * @return 0 if the signature is valid, negative error code otherwise
 */
int chipmunk_verify(const uint8_t *a_public_key, const uint8_t *a_message, 
                   size_t a_message_len, const uint8_t *a_signature);

/**
 * @brief Transform polynomial to NTT domain
 * 
 * @param a_poly Polynomial to transform
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_ntt(chipmunk_poly_t *a_poly);

/**
 * @brief Transform polynomial from NTT domain back to normal form
 * 
 * @param a_poly Polynomial to transform
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_invntt(chipmunk_poly_t *a_poly);

/**
 * @brief Add two polynomials coefficient-wise modulo q
 * 
 * @param a_result Output polynomial (a + b)
 * @param a_a First input polynomial
 * @param a_b Second input polynomial
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_add(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Subtract two polynomials coefficient-wise modulo q
 * 
 * @param a_result Output polynomial (a - b)
 * @param a_a First input polynomial
 * @param a_b Second input polynomial to subtract
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_sub(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Multiply two polynomials in NTT domain
 * 
 * Both input polynomials must be in NTT form
 * 
 * @param a_result Output polynomial (a * b)
 * @param a_a First input polynomial in NTT form
 * @param a_b Second input polynomial in NTT form
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_pointwise(chipmunk_poly_t *a_result, const chipmunk_poly_t *a_a, const chipmunk_poly_t *a_b);

/**
 * @brief Fill polynomial with uniformly random coefficients
 * 
 * @param a_poly Output polynomial
 * @param a_seed 32-byte seed for randomness
 * @param a_nonce Nonce value for domain separation
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_uniform(chipmunk_poly_t *a_poly, const uint8_t a_seed[32], uint16_t a_nonce);

/**
 * @brief Create challenge polynomial with CHIPMUNK_TAU nonzero coefficients
 * 
 * This function deterministically generates a sparse polynomial with exactly CHIPMUNK_TAU
 * nonzero coefficients (±1) from the given seed.
 * 
 * @param a_poly Output challenge polynomial
 * @param a_seed 32-byte seed for deterministic generation
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_poly_challenge(chipmunk_poly_t *a_poly, const uint8_t a_seed[32]);

/**
 * @brief Check if polynomial coefficients are within specified bounds
 * 
 * @param a_poly Polynomial to check
 * @param a_bound Maximum absolute value for coefficients
 * @return 0 if norm is within bounds, 1 if norm exceeds bounds
 */
int chipmunk_poly_chknorm(const chipmunk_poly_t *a_poly, int32_t a_bound);

/**
 * @brief Serialize public key to bytes
 * 
 * @param a_output Output buffer, must be at least CHIPMUNK_PUBLIC_KEY_SIZE bytes
 * @param a_key Public key structure to serialize
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_public_key_to_bytes(uint8_t *a_output, const chipmunk_public_key_t *a_key);

/**
 * @brief Deserialize public key from bytes
 * 
 * @param a_key Output public key structure
 * @param a_input Input buffer containing serialized public key (CHIPMUNK_PUBLIC_KEY_SIZE bytes)
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_public_key_from_bytes(chipmunk_public_key_t *a_key, const uint8_t *a_input);

/**
 * @brief Serialize private key to bytes
 * 
 * @param a_output Output buffer, must be at least CHIPMUNK_PRIVATE_KEY_SIZE bytes
 * @param a_key Private key structure to serialize
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_private_key_to_bytes(uint8_t *a_output, const chipmunk_private_key_t *a_key);

/**
 * @brief Deserialize private key from bytes
 * 
 * @param a_key Output private key structure
 * @param a_input Input buffer containing serialized private key (CHIPMUNK_PRIVATE_KEY_SIZE bytes)
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_private_key_from_bytes(chipmunk_private_key_t *a_key, const uint8_t *a_input);

/**
 * @brief Serialize signature to bytes
 * 
 * @param a_output Output buffer, must be at least CHIPMUNK_SIGNATURE_SIZE bytes
 * @param a_sig Signature structure to serialize
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_signature_to_bytes(uint8_t *a_output, const chipmunk_signature_t *a_sig);

/**
 * @brief Deserialize signature from bytes
 * 
 * @param a_sig Output signature structure
 * @param a_input Input buffer containing serialized signature (CHIPMUNK_SIGNATURE_SIZE bytes)
 * @return CHIPMUNK_ERROR_SUCCESS on success, error code otherwise
 */
int chipmunk_signature_from_bytes(chipmunk_signature_t *a_sig, const uint8_t *a_input); 
