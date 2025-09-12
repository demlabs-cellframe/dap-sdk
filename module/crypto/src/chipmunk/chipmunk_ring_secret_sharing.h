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
#include "chipmunk.h"
#include "chipmunk_poly.h"
#include "chipmunk_hots.h"
#include "chipmunk_ring.h"
#include "dap_enc_chipmunk_ring_params.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file chipmunk_ring_secret_sharing.h
 * @brief Lattice-based Secret Sharing for ChipmunkRing (unified architecture)
 * 
 * UNIFIED ARCHITECTURE:
 * - ChipmunkRing with required_signers=1: Single signer anonymity (existing behavior)
 * - ChipmunkRing with required_signers>1: Multiple signers required (new functionality)
 * - All functionality integrated within ChipmunkRing module
 * - Backward compatibility: existing API works with default required_signers=1
 * 
 * Key features:
 * - Lattice-based secret sharing using existing Chipmunk polynomial structures
 * - Zero-knowledge verification integrated with existing commitment system
 * - Multi-signer support where t participants create valid signature
 * - Maximum reuse of existing ChipmunkRing infrastructure
 * - Seamless integration: no separate modules needed
 */

/**
 * @brief Secret share integrated with ChipmunkRing
 * @details Extends existing ChipmunkRing structures for multi-signer functionality
 */
typedef struct chipmunk_ring_share {
    uint8_t share_id;                           ///< Share identifier (1 to n)
    
    // Reuse existing ChipmunkRing structures
    chipmunk_ring_private_key_t ring_private_key; ///< Private key share (uses existing structure)
    chipmunk_ring_public_key_t ring_public_key;  ///< Public key share (uses existing structure)
    
    // Multi-signer extensions
    uint32_t required_signers;                   ///< Required signers count
    uint32_t total_participants;                 ///< Total participants n
    
    // Zero-knowledge components (integrated Acorn verification)
    chipmunk_ring_acorn_t acorn_proof;           ///< Acorn verification proof handles all ZK needs
    
    // Metadata
    bool is_valid;                               ///< Share validity flag
} chipmunk_ring_share_t;

/**
 * @brief Generate secret shares from ChipmunkRing key with signature parameters
 * @details Uses ZK parameters from signature structure for adaptive configuration
 * 
 * @param a_ring_key Input ChipmunkRing private key
 * @param a_signature Signature containing ZK parameters (zk_proof_size_per_participant, zk_iterations)
 * @param a_shares Output array of secret shares
 * @return 0 on success, negative on error
 */
int chipmunk_ring_generate_shares_from_signature(const chipmunk_ring_private_key_t *a_ring_key,
                                               const chipmunk_ring_signature_t *a_signature,
                                               chipmunk_ring_share_t *a_shares);

/**
 * @brief Generate secret shares from ChipmunkRing key (legacy interface)
 * @details Extends existing key into (t,n) secret shares with ZK properties
 * 
 * @param a_ring_key Input ChipmunkRing private key
 * @param a_required_signers Required signers (1 = traditional ring behavior)
 * @param a_total_participants Total participants n
 * @param a_shares Output array of secret shares
 * @return 0 on success, negative on error
 */
int chipmunk_ring_generate_shares(const chipmunk_ring_private_key_t *a_ring_key,
                                 uint32_t a_required_signers, uint32_t a_total_participants,
                                 chipmunk_ring_share_t *a_shares);

/**
 * @brief Verify secret share with zero-knowledge
 * @details Verifies share validity without revealing secret information
 * 
 * @param a_share Share to verify
 * @param a_ring_context Ring context for verification parameters
 * @return 0 if valid, negative on error
 */
int chipmunk_ring_verify_share(const chipmunk_ring_share_t *a_share,
                              const chipmunk_ring_container_t *a_ring_context);

/**
 * @brief Verify a secret share with signature parameters
 * @details Validates share using same parameters as generation
 * 
 * @param a_share Share to verify
 * @param a_signature Signature containing ZK parameters
 * @param a_ring_context Ring context for verification
 * @return 0 if valid, negative on error
 */
int chipmunk_ring_verify_share_with_params(const chipmunk_ring_share_t *a_share,
                                          const chipmunk_ring_signature_t *a_signature,
                                          const chipmunk_ring_container_t *a_ring_context);

/**
 * @brief Aggregate partial signatures from multiple shares
 * @details Combines t partial signatures into final ring signature
 * 
 * @param a_shares Array of shares from participating signers
 * @param a_share_count Number of participating shares
 * @param a_message Original message
 * @param a_message_size Message size
 * @param a_ring Ring context
 * @param a_signature Output aggregated signature
 * @return 0 on success, negative on error
 */
int chipmunk_ring_aggregate_signatures(const chipmunk_ring_share_t *a_shares,
                                     uint32_t a_share_count,
                                     const void *a_message, size_t a_message_size,
                                     const chipmunk_ring_container_t *a_ring,
                                     chipmunk_ring_signature_t *a_signature);

/**
 * @brief Generate ZK proof using signature parameters
 * @details Universal function that uses all parameters from signature structure
 * 
 * @param a_input Input data for proof
 * @param a_input_size Size of input data
 * @param a_signature Signature containing all ZK parameters (size, iterations, domain, etc.)
 * @param a_salt Optional salt for enhanced security
 * @param a_salt_size Size of salt
 * @param a_output Output buffer for ZK proof
 * @return 0 on success, negative on error
 */
int chipmunk_ring_generate_zk_proof(const uint8_t *a_input, size_t a_input_size,
                                   const chipmunk_ring_signature_t *a_signature,
                                   const uint8_t *a_salt, size_t a_salt_size,
                                   uint8_t *a_output);

/**
 * @brief Free secret share resources
 * @param a_share Share to free
 */
void chipmunk_ring_share_free(chipmunk_ring_share_t *a_share);

#ifdef __cplusplus
}
#endif
