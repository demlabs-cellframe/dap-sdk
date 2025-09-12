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

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "dap_common.h"
#include "chipmunk_ring.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file chipmunk_ring_commitment.h
 * @brief Quantum-resistant commitment system for ChipmunkRing signatures
 * 
 * This module provides commitment functionality for ChipmunkRing signatures,
 * including deterministic and random commitment generation modes for anonymity control.
 */

/**
 * @brief Create quantum-resistant commitment for ZKP (always deterministic for anonymity)
 * @param a_commitment Output commitment structure
 * @param a_public_key Public key for commitment
 * @param a_message Message for deterministic seed
 * @param a_message_size Size of message
 * @return 0 on success, negative on error
 */
int chipmunk_ring_acorn_create(chipmunk_ring_acorn_t *a_acorn,
                               const chipmunk_ring_public_key_t *a_public_key,
                               const void *a_message, size_t a_message_size);

/**
 * @brief Free memory allocated for Acorn verification dynamic arrays
 * @param a_acorn Acorn verification to free
 */
void chipmunk_ring_acorn_free(chipmunk_ring_acorn_t *a_acorn);

/**
 * @brief Create Ring-LWE commitment layer (~90,000 qubits required for quantum attack)
 * @param a_output Output buffer for commitment
 * @param a_output_size Size of output buffer
 * @param a_public_key Public key
 * @param randomness Randomness for commitment
 * @return 0 on success, negative on error
 */
int chipmunk_ring_commitment_create_ring_lwe_layer(uint8_t *a_output, size_t a_output_size,
                                                 const chipmunk_ring_public_key_t *a_public_key,
                                                 const uint8_t *randomness);

/**
 * @brief Create NTRU commitment layer (~70,000 qubits required for quantum attack)
 * @param a_output Output buffer for commitment
 * @param a_output_size Size of output buffer
 * @param a_public_key Public key
 * @param randomness Randomness for commitment
 * @return 0 on success, negative on error
 */
int chipmunk_ring_commitment_create_ntru_layer(uint8_t *a_output, size_t a_output_size,
                                             const chipmunk_ring_public_key_t *a_public_key,
                                             const uint8_t *randomness);

/**
 * @brief Create code-based commitment layer (~80,000 qubits required for quantum attack)
 * @param a_output Output buffer for commitment
 * @param a_output_size Size of output buffer
 * @param a_public_key Public key
 * @param randomness Randomness for commitment
 * @return 0 on success, negative on error
 */
int chipmunk_ring_commitment_create_code_layer(uint8_t *a_output, size_t a_output_size,
                                             const chipmunk_ring_public_key_t *a_public_key,
                                             const uint8_t *randomness);

/**
 * @brief Create binding proof for multi-layer commitment (100+ year security)
 * @param a_output Output buffer for binding proof
 * @param a_output_size Size of output buffer
 * @param a_public_key Public key
 * @param randomness Randomness for commitment
 * @param ring_lwe_layer Ring-LWE layer data
 * @param ring_lwe_size Ring-LWE layer size
 * @param ntru_layer NTRU layer data
 * @param ntru_size NTRU layer size
 * @param code_layer Code layer data
 * @param code_size Code layer size
 * @return 0 on success, negative on error
 */
int chipmunk_ring_commitment_create_binding_proof(uint8_t *a_output, size_t a_output_size,
                                                const chipmunk_ring_public_key_t *a_public_key,
                                                const uint8_t *randomness,
                                                const uint8_t *ring_lwe_layer, size_t ring_lwe_size,
                                                const uint8_t *ntru_layer, size_t ntru_size,
                                                const uint8_t *code_layer, size_t code_size);

#ifdef __cplusplus
}
#endif
