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
    CHIPMUNK_ERROR_INVALID_SIZE = -10, ///< Invalid size
    CHIPMUNK_ERROR_RETRY = -11         ///< Retry operation with new randomness
};

// =================SHARED PARAMETERS FROM ORIGINAL param.rs===============
#define CHIPMUNK_N           512             ///< Ring dimension (polynomial degree)
#define CHIPMUNK_SEC_PARAM   112             ///< Security parameter
#define CHIPMUNK_ALPHA       16              ///< Non-zero entries in randomizer
#define CHIPMUNK_HEIGHT      5               ///< Height of the tree
#define CHIPMUNK_ZETA        29              ///< Base of decomposition: coefficients in [-zeta, zeta]
#define CHIPMUNK_TWO_ZETA_PLUS_ONE 59        ///< Arity: 2 * zeta + 1

// =================HOTS PARAMETERS FROM ORIGINAL param.rs=================
#define CHIPMUNK_Q           3168257         ///< HOTS modulus (corrected from original)
#define CHIPMUNK_ONE_OVER_N  3162069         ///< 1/N mod q
#define CHIPMUNK_Q_OVER_TWO  1584128         ///< (q-1)/2
#define CHIPMUNK_WIDTH       4               ///< Number of ring elements during decomposition
#define CHIPMUNK_SAMPLE_THRESHOLD 4292988235U ///< Largest multiple of q < 2^32
#define CHIPMUNK_GAMMA       6               ///< Number of polynomials in decomposed poly
#define CHIPMUNK_ALPHA_H     37              ///< Hamming weight of hash of message
#define CHIPMUNK_PHI         13              ///< Infinity norm bound for s0 (CRITICAL: must match Rust PHI = 13)
#define CHIPMUNK_PHI_SAMPLE_THRESHOLD 4294967274U ///< Largest multiple of (2*phi+1) < 2^32
#define CHIPMUNK_PHI_ALPHA_H 481             ///< Norm bound of s_1 = phi * alpha_H
#define CHIPMUNK_PHI_ALPHA_H_SAMPLE_THRESHOLD 4294966518U ///< Largest multiple of (2*PHI_ALPHA_H+1) < 2^32

// =================HVC PARAMETERS FROM ORIGINAL param.rs==================
#define CHIPMUNK_HVC_Q       202753          ///< HVC modulus for small ring
#define CHIPMUNK_HVC_ONE_OVER_N 202357       ///< 1/N mod q for HVC
#define CHIPMUNK_HVC_Q_OVER_TWO 101376       ///< (q-1)/2 for HVC
#define CHIPMUNK_HVC_SAMPLE_THRESHOLD 4294916799U ///< Largest multiple of HVC_q < 2^32
#define CHIPMUNK_HVC_WIDTH   3               ///< Number of ring elements during HVC decomposition

// =================ENCODING PARAMETERS FROM ORIGINAL param.rs==================
#define CHIPMUNK_ENCODING_NORM_BOUND 425     ///< Norm bound for alphas and a_star

// Key and signature sizes (updated for correct parameters)
#define CHIPMUNK_PUBLIC_KEY_SIZE  (32 + CHIPMUNK_N*4*2) // rho_seed + v0 + v1
#define CHIPMUNK_PRIVATE_KEY_SIZE (32 + 48 + CHIPMUNK_PUBLIC_KEY_SIZE) // key_seed + tr + public_key
#define CHIPMUNK_SIGNATURE_SIZE   (CHIPMUNK_N*4*CHIPMUNK_GAMMA) // sigma[GAMMA]

/**
 * @brief Polynomial structure used in Chipmunk operations
 * @details Represents a polynomial with CHIPMUNK_N integer coefficients
 */
typedef struct chipmunk_poly {
    int32_t coeffs[CHIPMUNK_N];  ///< Array of polynomial coefficients
} chipmunk_poly_t;

/**
 * @brief Public key structure for Chipmunk HOTS algorithm
 */
typedef struct chipmunk_public_key {
    uint8_t rho_seed[32];   ///< Seed for generating matrix A parameters
    chipmunk_poly_t v0;     ///< Public key polynomial v0 = Σ(a[i] * s0[i])
    chipmunk_poly_t v1;     ///< Public key polynomial v1 = Σ(a[i] * s1[i])
} chipmunk_public_key_t;

/**
 * @brief Private key structure for Chipmunk HOTS algorithm
 */
typedef struct chipmunk_private_key {
    uint8_t key_seed[32];    ///< Master seed for generating s0[i] and s1[i]
    uint8_t tr[48];          ///< Public key commitment (SHA3-384 hash)
    chipmunk_public_key_t pk; ///< Embedded public key
} chipmunk_private_key_t;

/**
 * @brief Signature structure for Chipmunk HOTS algorithm
 */
typedef struct chipmunk_signature {
    chipmunk_poly_t sigma[CHIPMUNK_GAMMA]; ///< HOTS signature polynomials
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
