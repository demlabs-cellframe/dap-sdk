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

#include "dap_common.h"
#include "dap_enc_key.h"
#include "chipmunk.h"
#include "chipmunk_hypertree.h"
#include "dap_enc_chipmunk_ring.h"

/**
 * @brief Chipmunk_Ring ring signature parameters
 */
// Note: ZK proof parameters moved to dap_enc_chipmunk_ring_params.h
// Note: CHIPMUNK_RING_SIGNATURE_SIZE is now defined in dap_enc_chipmunk_ring_params.h
// with parametric form: CHIPMUNK_RING_SIGNATURE_SIZE(chipmunk_n, chipmunk_gamma)


/*
 * CR-D15.C (Round-3): a ring-signature keypair is now a Chipmunk hypertree
 * keypair, not a single-shot HOTS keypair.  The ring still uses *one*
 * underlying signature per chipmunk_ring_sign() invocation (over the
 * Fiat-Shamir challenge), but a single hypertree keypair can back up to
 * CHIPMUNK_HT_MAX_SIGNATURES distinct ring signatures before the CR-D3
 * one-time-key exhaustion guard trips.  Without this migration every
 * chipmunk_ring_sign() call exhausted the keypair in one shot, which is
 * obviously useless for any real deployment.
 *
 * Canonical byte layout matches chipmunk_hypertree.h:
 *   - chipmunk_ring_public_key_t::data  = CHIPMUNK_HT_PUBLIC_KEY_SIZE
 *       (rho_seed[32] || hasher_seed[32] || root_poly_N*4 bytes)
 *   - chipmunk_ring_private_key_t::data = CHIPMUNK_HT_PRIVATE_KEY_SIZE
 *       (leaf_index_LE || key_seed[32] || tr[32] || pk_bytes)
 *
 * The runtime chipmunk_ht_private_key_t (which owns the materialised Merkle
 * tree and a thread-safety mutex) is rebuilt on demand from these bytes by
 * chipmunk_ring_sign() and re-serialised back so that the leaf_index
 * advances monotonically in the caller's storage.
 */

#define CHIPMUNK_RING_PUBLIC_KEY_SIZE   CHIPMUNK_HT_PUBLIC_KEY_SIZE
#define CHIPMUNK_RING_PRIVATE_KEY_SIZE  CHIPMUNK_HT_PRIVATE_KEY_SIZE
#define CHIPMUNK_RING_CHALLENGE_SIG_SIZE CHIPMUNK_HT_SIGNATURE_SIZE

/**
 * @brief Chipmunk_Ring public key structure
 */
typedef struct chipmunk_ring_public_key {
    uint8_t data[CHIPMUNK_RING_PUBLIC_KEY_SIZE];  ///< Serialised hypertree public key
} chipmunk_ring_public_key_t;

/**
 * @brief Chipmunk_Ring private key structure
 */
typedef struct chipmunk_ring_private_key {
    uint8_t data[CHIPMUNK_RING_PRIVATE_KEY_SIZE];  ///< Serialised hypertree private key (incl. leaf_index counter)
} chipmunk_ring_private_key_t;

/**
 * @brief Ring container for public keys
 */
typedef struct chipmunk_ring_container {
    uint32_t size;                                      ///< Number of keys in ring
    chipmunk_ring_public_key_t *public_keys;             ///< Array of public keys
    uint8_t *ring_hash;                                 ///< Hash of all public keys (dynamic size)
    size_t ring_hash_size;                              ///< Size of ring hash (from hash algorithm)
} chipmunk_ring_container_t;

/**
 * @brief Acorn verification structure 
 */
typedef struct chipmunk_ring_acorn {
    // Acorn proof data (uses signature parameters for size)
    uint8_t *acorn_proof;            ///< Acorn verification proof (dynamic size from signature)
    size_t acorn_proof_size;         ///< Size of Acorn proof (from signature->zk_proof_size_per_participant)
    
    // Commitment randomness
    uint8_t *randomness;             ///< Randomness used in commitment (dynamic size)
    size_t randomness_size;          ///< Size of randomness in bytes
    
    /* CR-D8 fix (Round-3): the original design used this field as a
     * per-acorn "linkability tag" computed as SHA3(CHIPMUNK_RING_DOMAIN_
     * ACORN_LINKABILITY || pk_i).  Because the input depended only on the
     * ring slot's public key, the value was publicly recomputable and
     * therefore *revealed the ring slot* in every signature — the exact
     * opposite of unlinkability.  It was also never consumed by the
     * verifier.  The field is retained for wire-compatibility within the
     * pre-production feature/chipmunk-ring branch; chipmunk_ring_acorn.c
     * now zero-fills it so no information is leaked.  A real sigma-
     * protocol-bound tag is tracked under CR-11. */
    uint8_t *linkability_tag;                          ///< Reserved zero slot (CR-D8 mitigation)
    size_t linkability_tag_size;                       ///< Size of linkability tag (payload is all-zero)

 } chipmunk_ring_acorn_t;

// NOTE: chipmunk_ring_pq_params_t defined in dap_enc_chipmunk_ring.h

// NOTE: Response structure removed - Acorn Verification handles all ZKP needs

/**
 * @brief Chipmunk_Ring signature structure (now supports threshold)
 * @details Unified structure for both traditional ring (t=1) and threshold ring (t>1)
 */
typedef struct chipmunk_ring_signature {
    uint32_t ring_size;                                ///< Number of participants in ring
    uint32_t required_signers;                         ///< Required signers (1 = single, >1 = multi-signer)
    
    // ZK Components (needed for threshold coordination and ZK schemes)
    uint8_t *challenge;                                ///< Acorn challenge (dynamic size)
    size_t challenge_size;                             ///< Size of challenge (from hash algorithm)
    chipmunk_ring_acorn_t *acorn_proofs;               ///< Acorn verification proofs
    
    // Core signature
    uint8_t *signature;                                ///< Core signature data (dynamic size)
    size_t signature_size;                             ///< Size of signature data
    
    // Ring public keys storage (scalability optimization)
    bool use_embedded_keys;                            ///< True = keys in signature, False = external storage
    chipmunk_ring_public_key_t *ring_public_keys;      ///< Embedded public keys (NULL if external)
    uint8_t *ring_hash;                                ///< Hash of all ring public keys (dynamic size, serves both purposes)
    size_t ring_hash_size;                             ///< Size of ring hash (from hash algorithm)
    
    // Multi-signer extensions (only used when required_signers > 1)
    uint8_t *threshold_zk_proofs;                      ///< ZK proofs from participating signers (dynamic)
    /* CR-D9 fix (Round-3): explicit 64-bit width instead of size_t to keep
     * the on-wire representation identical on 32-bit and 64-bit hosts.
     * Schema entry in chipmunk_ring_serialize_schema.c now uses
     * sizeof(uint64_t) to match. */
    uint64_t zk_proofs_size;                           ///< Total size of all ZK proofs data
    uint32_t *zk_proof_lengths;                        ///< Length of each individual ZK proof (dynamic array)
    uint32_t participating_count;                      ///< Actual number of participants who signed
    
    // NOTE: Participating signer identification is handled through ring_hash and ZK proofs
    
    // Coordination state (for threshold schemes)
    bool is_coordinated;                               ///< True if threshold coordination completed
    uint32_t coordination_round;                       ///< Current coordination round (0=commit, 1=reveal, 2=aggregate)
    uint32_t zk_proof_size_per_participant;            ///< Configurable ZK proof size (default: 64)
    uint32_t zk_iterations;                            ///< Number of SHAKE-256 iterations for ZK proofs
    
    /* CR-D8 note (Round-3): this field is populated with H(ring_hash ||
     * message || challenge) by chipmunk_ring_sign.  Because it depends on
     * the message it DOES NOT provide linkability in the cryptographic
     * sense: two signatures by the same signer over different messages
     * have different tags, so repeat-signer detection is impossible.  It
     * is effectively a per-signature session digest, useful only as an
     * anti-replay nonce at the transport layer.  A proper linkability
     * tag bound to the signer's secret via a sigma protocol is tracked
     * under CR-11 (master-plan phase).  Do not treat this field as
     * evidence of key reuse; doing so will produce false negatives. */
    uint8_t *linkability_tag;                          ///< Per-signature session digest (NOT a cryptographic linkability tag)
    size_t linkability_tag_size;                       ///< Size of session digest
} chipmunk_ring_signature_t;

/**
 * @brief Initialize Chipmunk_Ring module
 * @return 0 on success, negative on error
 */
int chipmunk_ring_init(void);

/**
 * @brief Generate Chipmunk_Ring keypair (same as Chipmunk)
 * @param key Output key structure
 * @return 0 on success, negative on error
 */
int chipmunk_ring_key_new(struct dap_enc_key *a_key);

/**
 * @brief Generate keypair from seed
 * @param key Output key structure
 * @param seed Seed for deterministic generation
 * @param seed_size Seed size
 * @param key_size Key size (unused, kept for compatibility)
 * @return 0 on success, negative on error
 */
int chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size);

/**
 * @brief Create ring from public keys
 * @param public_keys Array of public keys
 * @param num_keys Number of keys in ring
 * @param ring Output ring structure
 * @return 0 on success, negative on error
 */
int chipmunk_ring_container_create(const chipmunk_ring_public_key_t *a_public_keys,
                           size_t a_num_keys, chipmunk_ring_container_t *a_ring);

/**
 * @brief Free ring container resources
 * @param ring Ring container to free
 */
void chipmunk_ring_container_free(chipmunk_ring_container_t *a_ring);

/**
 * @brief Create ChipmunkRing signature with scalability support
 * @param private_key Signer's private key
 * @param message Message to sign
 * @param message_size Message size
 * @param ring Ring containing public keys
 * @param a_required_signers Required signers (1 = traditional ring, >1 = multi-signer)
 * @param a_use_embedded_keys True = embed keys in signature, False = use external storage
 * @param signature Output signature
 * @return 0 on success, negative on error
 */
/*
 * CR-D15.C: a_private_key is *not* const anymore.  The hypertree sk carries
 * a monotonic leaf_index; chipmunk_ring_sign() bumps it on every call and
 * writes the updated serialised form back into a_private_key->data so that
 * subsequent calls consume the next leaf.  Passing the same buffer to two
 * concurrent signers from different threads is forbidden — callers must
 * serialise access externally (dap_enc_chipmunk_ring_sign() already does
 * this by only exposing one signer at a time).
 */
int chipmunk_ring_sign(chipmunk_ring_private_key_t *a_private_key,
                     const void *a_message, size_t a_message_size,
                     const chipmunk_ring_container_t *a_ring,
                     uint32_t a_required_signers,
                     bool a_use_embedded_keys,
                     chipmunk_ring_signature_t *a_signature);


/**
 * @brief Verify ChipmunkRing signature with scalability support
 * @param message Signed message
 * @param message_size Message size
 * @param signature Signature to verify
 * @param ring Ring containing public keys (NULL if using embedded keys)
 * @return 0 if valid, negative on error or invalid signature
 */
int chipmunk_ring_verify(const void *a_message, size_t a_message_size,
                       const chipmunk_ring_signature_t *a_signature,
                       const chipmunk_ring_container_t *a_ring);

/**
 * @brief Verify ChipmunkRing signature with external key resolution
 * @details For large rings where keys are stored externally (blockchain/database)
 * 
 * @param message Signed message
 * @param message_size Message size
 * @param signature Signature to verify (contains key hashes)
 * @param key_resolver Function to resolve public key from hash
 * @param resolver_context Context for key resolver function
 * @return 0 if valid, negative on error
 */
typedef int (*chipmunk_ring_key_resolver_t)(const uint8_t *key_hash, 
                                           chipmunk_ring_public_key_t *public_key, 
                                           void *context);

int chipmunk_ring_verify_external(const void *a_message, size_t a_message_size,
                                 const chipmunk_ring_signature_t *a_signature,
                                 chipmunk_ring_key_resolver_t a_key_resolver,
                                 void *a_resolver_context);

/**
 * @brief Get signature size for given ring parameters
 * @param a_ring_size Number of participants
 * @param a_required_signers Required signers (1=single, >1=multi-signer)
 * @param a_use_embedded_keys True if keys embedded in signature
 * @return Required signature buffer size
 */
size_t chipmunk_ring_get_signature_size(size_t a_ring_size, uint32_t a_required_signers, bool a_use_embedded_keys);

/**
 * @brief Delete Chipmunk_Ring key
 * @param key Key to delete
 */
void chipmunk_ring_key_delete(struct dap_enc_key *a_key);


/**
 * @brief Create response for ZKP
 * @param response Output response
 * @param commitment Commitment used
 * @param challenge Challenge value
 * @param private_key Private key (NULL for dummy participants)
 * @return 0 on success, negative on error
 */
// NOTE: Response creation removed - Acorn Verification handles all ZKP coordination

/**
 * @brief Free signature resources
 * @param signature Signature to free
 */
void chipmunk_ring_signature_free(chipmunk_ring_signature_t *a_signature);

/**
 * @brief Serialize signature to bytes
 * @param sig Signature structure
 * @param output Output buffer
 * @param output_size Output buffer size
 * @return 0 on success, negative on error
 */
int chipmunk_ring_signature_to_bytes(const chipmunk_ring_signature_t *a_sig,
                                   uint8_t *a_output, size_t a_output_size);

/**
 * @brief Deserialize signature from bytes
 * @param sig Output signature structure
 * @param input Input buffer
 * @param input_size Input buffer size
 * @return 0 on success, negative on error
 */
int chipmunk_ring_signature_from_bytes(chipmunk_ring_signature_t *a_sig,
                                     const uint8_t *a_input, size_t a_input_size);

/**
 * @brief Get current post-quantum parameters
 */
int chipmunk_ring_get_params(chipmunk_ring_pq_params_t *params);

/**
 * @brief Get current post-quantum parameters (direct return)
 */
const chipmunk_ring_pq_params_t* chipmunk_ring_get_current_params(void);

/**
 * @brief Set post-quantum parameters
 */
int chipmunk_ring_set_params(const chipmunk_ring_pq_params_t *params);


/**
 * @brief Reset parameters to defaults
 */
int chipmunk_ring_reset_params(void);
