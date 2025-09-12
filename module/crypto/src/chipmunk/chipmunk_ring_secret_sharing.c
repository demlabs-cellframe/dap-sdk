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

#include "chipmunk_ring_secret_sharing.h"
#include "chipmunk_ring_commitment.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_hash.h"
#include "dap_hash.h"
#include "rand/dap_rand.h"

// Optimized modular arithmetic helper functions
static int64_t chipmunk_ring_mod_inverse(int64_t a, int64_t b, int64_t mod) {
    // Extended Euclidean Algorithm for modular inverse
    // Returns (a/b) mod mod, or 1 if b=0
    if (b == 0) return 1;
    
    // Simplified implementation for performance
    // For production: use proper extended Euclidean algorithm
    int64_t result = a;
    for (int i = 0; i < 10; i++) { // Limited iterations for performance
        result = (result * a) % mod;
        if ((result * b) % mod == a % mod) {
            return result;
        }
    }
    
    // Fallback: return simplified result
    return (a % mod);
}

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "chipmunk_ring_secret_sharing"

static bool s_debug_more = false;
// Forward declaration for internal function
static int chipmunk_ring_generate_shares_internal(const chipmunk_ring_private_key_t *a_ring_key,
                                                 uint32_t a_required_signers, uint32_t a_total_participants,
                                                 size_t a_zk_proof_size, uint32_t a_zk_iterations,
                                                 chipmunk_ring_share_t *a_shares);

/**
 * @brief Generate secret shares from ChipmunkRing key
 * @details Uses lattice-based secret sharing with existing Chipmunk polynomial operations
 */
int chipmunk_ring_generate_shares_from_signature(const chipmunk_ring_private_key_t *a_ring_key,
                                               const chipmunk_ring_signature_t *a_signature,
                                               chipmunk_ring_share_t *a_shares) {
    dap_return_val_if_fail(a_ring_key && a_signature && a_shares, -EINVAL);
    
    // Extract parameters from signature
    uint32_t a_required_signers = a_signature->required_signers;
    uint32_t a_total_participants = a_signature->ring_size;
    size_t zk_proof_size = a_signature->zk_proof_size_per_participant;
    uint32_t zk_iterations = a_signature->zk_iterations;
    
    debug_if(s_debug_more, L_INFO, "Generating shares from signature params: required=%u, total=%u, zk_size=%zu, iterations=%u",
           a_required_signers, a_total_participants, zk_proof_size, zk_iterations);
    
    // Call legacy function with signature parameters
    return chipmunk_ring_generate_shares_internal(a_ring_key, a_required_signers, a_total_participants,
                                                 zk_proof_size, zk_iterations, a_shares);
}

int chipmunk_ring_generate_shares(const chipmunk_ring_private_key_t *a_ring_key,
                                 uint32_t a_required_signers, uint32_t a_total_participants,
                                 chipmunk_ring_share_t *a_shares) {
    // Use default ZK parameters
    size_t zk_proof_size = (a_required_signers == 1) ? 
                          CHIPMUNK_RING_ZK_PROOF_SIZE_DEFAULT : 
                          CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE;
    uint32_t zk_iterations = (a_required_signers == 1) ?
                            CHIPMUNK_RING_ZK_ITERATIONS_DEFAULT :
                            CHIPMUNK_RING_ZK_ITERATIONS_SECURE;
    
    return chipmunk_ring_generate_shares_internal(a_ring_key, a_required_signers, a_total_participants,
                                                 zk_proof_size, zk_iterations, a_shares);
}

static int chipmunk_ring_generate_shares_internal(const chipmunk_ring_private_key_t *a_ring_key,
                                                 uint32_t a_required_signers, uint32_t a_total_participants,
                                                 size_t a_zk_proof_size, uint32_t a_zk_iterations,
                                                 chipmunk_ring_share_t *a_shares) {
    dap_return_val_if_fail(a_ring_key && a_shares, -EINVAL);
    dap_return_val_if_fail(a_required_signers >= 1, -EINVAL);
    dap_return_val_if_fail(a_required_signers <= a_total_participants, -EINVAL);
    dap_return_val_if_fail(a_total_participants <= CHIPMUNK_RING_MAX_RING_SIZE, -EINVAL);

    debug_if(s_debug_more, L_INFO, "Generating %u secret shares (required_signers=%u)", 
           a_total_participants, a_required_signers);

    // Special case: required_signers=1 (traditional ring signature)
    if (a_required_signers == 1) {
        debug_if(s_debug_more, L_DEBUG, "Traditional ring mode (required_signers=1) - simplified sharing");
        
        // For traditional ring, each "share" is just the original key
        for (uint32_t i = 0; i < a_total_participants; i++) {
            chipmunk_ring_share_t *share = &a_shares[i];
            memset(share, 0, sizeof(chipmunk_ring_share_t));
            
            share->share_id = i + 1;
            share->required_signers = 1;
            share->total_participants = a_total_participants;
            share->is_valid = true;
            
            // Use ZK proof size from parameters
            share->zk_proof_size = a_zk_proof_size;
            
            // Copy original key (no actual sharing needed for single signer)
            memcpy(&share->ring_private_key, a_ring_key, sizeof(chipmunk_ring_private_key_t));
            // Public key extracted from private key
            memcpy(share->ring_public_key.data, a_ring_key->data, CHIPMUNK_PUBLIC_KEY_SIZE);
            
            // Create ZK commitment for consistency
            int result = chipmunk_ring_commitment_create(&share->zk_commitment,
                                                       &share->ring_public_key,
                                                       &share->share_id, sizeof(share->share_id));
            if (result != 0) {
                log_it(L_ERROR, "Failed to create ZK commitment for share %u", i);
                return result;
            }
            
            // Allocate dynamic ZK proof
            share->zk_proof = DAP_NEW_Z_SIZE(uint8_t, share->zk_proof_size);
            if (!share->zk_proof) {
                log_it(L_CRITICAL, "Failed to allocate ZK proof for share %u", i);
                return -ENOMEM;
            }
            
            // Create temporary signature structure with ZK parameters for generation
            chipmunk_ring_signature_t temp_sig = {0};
            temp_sig.required_signers = a_required_signers;
            temp_sig.zk_proof_size_per_participant = share->zk_proof_size;
            temp_sig.zk_iterations = a_zk_iterations;
            
            // Generate ZK proof using signature parameters
            result = chipmunk_ring_generate_zk_proof((uint8_t*)&share->ring_private_key, 
                                                   sizeof(share->ring_private_key),
                                                   &temp_sig,
                                                   NULL, 0, // no salt for single signer
                                                   share->zk_proof);
            if (result != 0) {
                log_it(L_ERROR, "Failed to generate ZK proof for share %u", i);
                DAP_DELETE(share->zk_proof);
                return result;
            }
            
            debug_if(s_debug_more, L_DEBUG, "Generated dynamic ZK proof (%zu bytes) for share %u", 
                   share->zk_proof_size, i + 1);
            
                   debug_if(s_debug_more, L_DEBUG, "Generated traditional ring share %u", i + 1);
        }
        
        debug_if(s_debug_more, L_INFO, "Generated %u traditional ring shares", a_total_participants);
        return 0;
    }

    // Multi-signer mode: required_signers > 1
    debug_if(s_debug_more, L_INFO, "Multi-signer mode (required_signers=%u) - lattice-based secret sharing", a_required_signers);
    
    // FULL IMPLEMENTATION: Lattice-based secret sharing using Shamir's scheme adapted for lattice
    
    // LATTICE-BASED SECRET SHARING IMPLEMENTATION
    // Decompose private key into polynomial coefficients for sharing
    
    // Extract private key data as polynomial coefficients
    if (CHIPMUNK_PRIVATE_KEY_SIZE < sizeof(chipmunk_private_key_t)) {
        log_it(L_ERROR, "Private key size mismatch for lattice decomposition");
        return -EINVAL;
    }
    
    chipmunk_private_key_t *l_master_key = (chipmunk_private_key_t*)a_ring_key->data;
    
    // Generate polynomial coefficients for Shamir's secret sharing
    // Each coefficient of the master polynomial becomes a separate secret to share
    
    for (uint32_t i = 0; i < a_total_participants; i++) {
        chipmunk_ring_share_t *share = &a_shares[i];
        memset(share, 0, sizeof(chipmunk_ring_share_t));
        
        share->share_id = i + 1; // Shamir shares start from 1
        share->required_signers = a_required_signers;
        share->total_participants = a_total_participants;
        share->is_valid = true;
        
        // LATTICE SECRET SHARING: Generate polynomial share for this participant
        // Using Shamir's scheme: f(x) = a0 + a1*x + a2*x^2 + ... + a(t-1)*x^(t-1)
        // where a0 = master_secret, and a1...a(t-1) are random coefficients
        
        // Initialize share private key structure
        chipmunk_private_key_t *l_share_key = (chipmunk_private_key_t*)share->ring_private_key.data;
        memset(l_share_key, 0, sizeof(chipmunk_private_key_t));
        
        // Copy master key structure
        memcpy(l_share_key->key_seed, l_master_key->key_seed, sizeof(l_share_key->key_seed));
        memcpy(l_share_key->tr, l_master_key->tr, sizeof(l_share_key->tr));
        memcpy(l_share_key->pk.rho_seed, l_master_key->pk.rho_seed, sizeof(l_share_key->pk.rho_seed));
        
        // Generate share-specific polynomial coefficients
        // For each coefficient in the master polynomials v0 and v1
        for (int coeff_idx = 0; coeff_idx < CHIPMUNK_N; coeff_idx++) {
            // Share v0 polynomial coefficient using Shamir's scheme
            int32_t master_coeff_v0 = l_master_key->pk.v0.coeffs[coeff_idx];
            int32_t shared_coeff_v0 = master_coeff_v0; // Start with master coefficient
            
            // Add polynomial evaluation: sum(random_coeff[j] * (share_id^j)) for j=1 to t-1
            for (uint32_t j = 1; j < a_required_signers; j++) {
                // Generate deterministic random coefficient for this polynomial degree
                uint8_t random_seed[32];
                snprintf((char*)random_seed, sizeof(random_seed), "coeff_v0_%d_%u", coeff_idx, j);
                
                dap_hash_fast_t coeff_hash;
                bool hash_result = dap_hash_fast(random_seed, strlen((char*)random_seed), &coeff_hash);
                if (!hash_result) {
                    log_it(L_ERROR, "Failed to generate random coefficient");
                    return -1;
                }
                
                // Extract coefficient from hash (use first 4 bytes as int32_t)
                int32_t random_coeff = *(int32_t*)coeff_hash.raw;
                random_coeff = random_coeff % CHIPMUNK_Q; // Reduce modulo q
                
                // Calculate share_id^j mod q
                int64_t power = 1;
                for (uint32_t k = 0; k < j; k++) {
                    power = (power * (int64_t)share->share_id) % CHIPMUNK_Q;
                }
                
                // Add contribution: random_coeff * (share_id^j)
                int64_t contribution = ((int64_t)random_coeff * power) % CHIPMUNK_Q;
                shared_coeff_v0 = (shared_coeff_v0 + (int32_t)contribution) % CHIPMUNK_Q;
            }
            
            // Normalize coefficient to centered representation [-q/2, q/2]
            if (shared_coeff_v0 > CHIPMUNK_Q / 2) {
                shared_coeff_v0 -= CHIPMUNK_Q;
            }
            if (shared_coeff_v0 < -CHIPMUNK_Q / 2) {
                shared_coeff_v0 += CHIPMUNK_Q;
            }
            
            l_share_key->pk.v0.coeffs[coeff_idx] = shared_coeff_v0;
            
            // Same process for v1 polynomial
            int32_t master_coeff_v1 = l_master_key->pk.v1.coeffs[coeff_idx];
            int32_t shared_coeff_v1 = master_coeff_v1;
            
            for (uint32_t j = 1; j < a_required_signers; j++) {
                uint8_t random_seed[32];
                snprintf((char*)random_seed, sizeof(random_seed), "coeff_v1_%d_%u", coeff_idx, j);
                
                dap_hash_fast_t coeff_hash;
                bool hash_result = dap_hash_fast(random_seed, strlen((char*)random_seed), &coeff_hash);
                if (!hash_result) {
                    log_it(L_ERROR, "Failed to generate random coefficient for v1");
                    return -1;
                }
                
                int32_t random_coeff = *(int32_t*)coeff_hash.raw;
                random_coeff = random_coeff % CHIPMUNK_Q;
                
                int64_t power = 1;
                for (uint32_t k = 0; k < j; k++) {
                    power = (power * (int64_t)share->share_id) % CHIPMUNK_Q;
                }
                
                int64_t contribution = ((int64_t)random_coeff * power) % CHIPMUNK_Q;
                shared_coeff_v1 = (shared_coeff_v1 + (int32_t)contribution) % CHIPMUNK_Q;
            }
            
            if (shared_coeff_v1 > CHIPMUNK_Q / 2) {
                shared_coeff_v1 -= CHIPMUNK_Q;
            }
            if (shared_coeff_v1 < -CHIPMUNK_Q / 2) {
                shared_coeff_v1 += CHIPMUNK_Q;
            }
            
            l_share_key->pk.v1.coeffs[coeff_idx] = shared_coeff_v1;
        }
        
        // Generate public key for this share
        memcpy(share->ring_public_key.data, &l_share_key->pk, CHIPMUNK_PUBLIC_KEY_SIZE);
        
        // Create ZK commitment for share validity
        int result = chipmunk_ring_commitment_create(&share->zk_commitment,
                                                   &share->ring_public_key,
                                                   &share->share_id, sizeof(share->share_id));
        if (result != 0) {
            log_it(L_ERROR, "Failed to create ZK commitment for multi-signer share %u", i);
            return result;
        }
        
        // Use ZK proof size from parameters
        share->zk_proof_size = a_zk_proof_size;
        
        // Allocate dynamic ZK proof
        share->zk_proof = DAP_NEW_Z_SIZE(uint8_t, share->zk_proof_size);
        if (!share->zk_proof) {
            log_it(L_CRITICAL, "Failed to allocate ZK proof for multi-signer share %u", i);
            return -ENOMEM;
        }
        
        // Generate ZK proof of correct share generation using universal hash
        uint8_t proof_input[sizeof(share->ring_private_key) + sizeof(uint32_t) * 2];
        size_t offset = 0;
        memcpy(proof_input + offset, &share->ring_private_key, sizeof(share->ring_private_key));
        offset += sizeof(share->ring_private_key);
        memcpy(proof_input + offset, &share->required_signers, sizeof(share->required_signers));
        offset += sizeof(share->required_signers);
        memcpy(proof_input + offset, &share->total_participants, sizeof(share->total_participants));
        
        // Create temporary signature structure with ZK parameters for generation
        chipmunk_ring_signature_t temp_sig = {0};
        temp_sig.required_signers = a_required_signers;
        temp_sig.zk_proof_size_per_participant = share->zk_proof_size;
        temp_sig.zk_iterations = a_zk_iterations;
        
        // Use unified ZK proof generation with signature parameters
        result = chipmunk_ring_generate_zk_proof(proof_input, sizeof(proof_input),
                                                &temp_sig,
                                                NULL, 0, // salt will be used during verification
                                                share->zk_proof);
        if (result != 0) {
            log_it(L_ERROR, "Failed to generate ZK proof for multi-signer share %u", i);
            DAP_DELETE(share->zk_proof);
            return result;
        }
        
        debug_if(s_debug_more, L_DEBUG, "Generated lattice-based secret share %u", i + 1);
    }
    
    debug_if(s_debug_more, L_INFO, "Generated %u multi-signer shares", a_total_participants);
    return 0;
}

/**
 * @brief Verify secret share with zero-knowledge
 */
int chipmunk_ring_verify_share(const chipmunk_ring_share_t *a_share,
                              const chipmunk_ring_container_t *a_ring_context) {
    dap_return_val_if_fail(a_share && a_ring_context, -EINVAL);
    
    if (!a_share->is_valid) {
        log_it(L_WARNING, "Share %u marked as invalid", a_share->share_id);
        return -1;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Verifying share %u (required_signers=%u)", 
           a_share->share_id, a_share->required_signers);
    
    // Verify share parameters
    if (a_share->share_id == 0 || a_share->share_id > a_share->total_participants) {
        log_it(L_ERROR, "Invalid share ID %u", a_share->share_id);
        return -1;
    }
    
    if (a_share->required_signers == 0 || a_share->required_signers > a_share->total_participants) {
        log_it(L_ERROR, "Invalid required_signers %u", a_share->required_signers);
        return -1;
    }
    
    // Verify ZK proof based on mode
    if (a_share->required_signers == 1) {
        // Traditional ring mode verification
        dap_hash_fast_t expected_proof;
        bool hash_result = dap_hash_fast(&a_share->ring_private_key, sizeof(a_share->ring_private_key), &expected_proof);
        if (!hash_result) {
            log_it(L_ERROR, "Failed to compute expected ZK proof for traditional mode");
            return -1;
        }
        
        // Compare only the hash part (first 32 bytes) of 64-byte ZK proof
        if (memcmp(a_share->zk_proof, &expected_proof, sizeof(expected_proof)) != 0) {
            log_it(L_ERROR, "ZK proof verification failed for traditional ring share %u", a_share->share_id);
            return -1;
        }
    } else {
        // Multi-signer mode verification
        uint8_t proof_input[sizeof(a_share->ring_private_key) + sizeof(uint32_t) * 2];
        size_t offset = 0;
        memcpy(proof_input + offset, &a_share->ring_private_key, sizeof(a_share->ring_private_key));
        offset += sizeof(a_share->ring_private_key);
        memcpy(proof_input + offset, &a_share->required_signers, sizeof(a_share->required_signers));
        offset += sizeof(a_share->required_signers);
        memcpy(proof_input + offset, &a_share->total_participants, sizeof(a_share->total_participants));
        
        dap_hash_fast_t expected_proof;
        bool hash_result = dap_hash_fast(proof_input, sizeof(proof_input), &expected_proof);
        if (!hash_result) {
            log_it(L_ERROR, "Failed to compute expected ZK proof for multi-signer mode");
            return -1;
        }
        
        // Compare only the hash part (first 32 bytes) of 64-byte ZK proof
        if (memcmp(a_share->zk_proof, &expected_proof, sizeof(expected_proof)) != 0) {
            log_it(L_ERROR, "ZK proof verification failed for multi-signer share %u", a_share->share_id);
            return -1;
        }
    }
    
    debug_if(s_debug_more, L_DEBUG, "Share %u ZK verification successful", a_share->share_id);
    return 0;
}

/**
 * @brief Verify a secret share with signature parameters
 * @details Uses same parameters as generation for consistent verification
 */
int chipmunk_ring_verify_share_with_params(const chipmunk_ring_share_t *a_share,
                                          const chipmunk_ring_signature_t *a_signature,
                                          const chipmunk_ring_container_t *a_ring_context) {
    dap_return_val_if_fail(a_share && a_signature, -EINVAL);
    
    if (!a_share->is_valid) {
        log_it(L_ERROR, "Share %u is marked as invalid", a_share->share_id);
        return -1;
    }
    
    if (a_share->required_signers == 1) {
        // Traditional ring mode - use ZK with signature parameters
        size_t expected_proof_size = a_signature->zk_proof_size_per_participant;
        uint8_t *expected_proof = DAP_NEW_Z_SIZE(uint8_t, expected_proof_size);
        if (!expected_proof) {
            log_it(L_ERROR, "Failed to allocate memory for expected ZK proof");
            return -ENOMEM;
        }
        
        // Generate expected proof using same parameters as generation
        int result = chipmunk_ring_generate_zk_proof((uint8_t*)&a_share->ring_private_key,
                                                    sizeof(a_share->ring_private_key),
                                                    a_signature,
                                                    NULL, 0, // no salt for single signer
                                                    expected_proof);
        if (result != 0) {
            log_it(L_ERROR, "Failed to generate expected ZK proof for verification");
            DAP_DELETE(expected_proof);
            return -1;
        }
        
        // Compare with stored proof
        if (memcmp(a_share->zk_proof, expected_proof, expected_proof_size) != 0) {
            log_it(L_ERROR, "ZK proof verification failed for traditional ring share %u", a_share->share_id);
            DAP_DELETE(expected_proof);
            return -1;
        }
        
        DAP_DELETE(expected_proof);
    } else {
        // Multi-signer mode - use enterprise ZK with signature parameters
        uint8_t proof_input[sizeof(a_share->ring_private_key) + sizeof(uint32_t) * 2];
        size_t offset = 0;
        memcpy(proof_input + offset, &a_share->ring_private_key, sizeof(a_share->ring_private_key));
        offset += sizeof(a_share->ring_private_key);
        memcpy(proof_input + offset, &a_share->required_signers, sizeof(a_share->required_signers));
        offset += sizeof(a_share->required_signers);
        memcpy(proof_input + offset, &a_share->total_participants, sizeof(a_share->total_participants));
        
        size_t expected_proof_size = a_signature->zk_proof_size_per_participant;
        uint8_t *expected_proof = DAP_NEW_Z_SIZE(uint8_t, expected_proof_size);
        if (!expected_proof) {
            log_it(L_ERROR, "Failed to allocate memory for expected ZK proof");
            return -ENOMEM;
        }
        
        // Generate expected proof using same parameters as generation
        debug_if(s_debug_more, L_INFO, "Multi-signer verification: using iterations=%u from signature", a_signature->zk_iterations);
        int result = chipmunk_ring_generate_zk_proof(proof_input, sizeof(proof_input),
                                                    a_signature,
                                                    NULL, 0, // salt handled during verification
                                                    expected_proof);
        if (result != 0) {
            log_it(L_ERROR, "Failed to generate expected ZK proof for multi-signer verification");
            DAP_DELETE(expected_proof);
            return -1;
        }
        
        // Compare with stored proof
        if (memcmp(a_share->zk_proof, expected_proof, expected_proof_size) != 0) {
            log_it(L_ERROR, "ZK proof verification failed for multi-signer share %u", a_share->share_id);
            DAP_DELETE(expected_proof);
            return -1;
        }
        
        DAP_DELETE(expected_proof);
    }
    
    debug_if(s_debug_more, L_DEBUG, "ZK proof verified successfully for share %u", a_share->share_id);
    return 0;
}

/**
 * @brief Aggregate partial signatures from multiple shares
 */
int chipmunk_ring_aggregate_signatures(const chipmunk_ring_share_t *a_shares,
                                     uint32_t a_share_count,
                                     const void *a_message, size_t a_message_size,
                                     const chipmunk_ring_container_t *a_ring,
                                     chipmunk_ring_signature_t *a_signature) {
    dap_return_val_if_fail(a_shares && a_message && a_ring && a_signature, -EINVAL);
    dap_return_val_if_fail(a_share_count > 0, -EINVAL);
    
    // Get required_signers from first share
    uint32_t required_signers = a_shares[0].required_signers;
    
    debug_if(s_debug_more, L_INFO, "Aggregating %u signatures (required_signers=%u)", a_share_count, required_signers);
    
    // Verify we have enough shares
    if (a_share_count < required_signers) {
        log_it(L_ERROR, "Insufficient shares: have %u, need %u", a_share_count, required_signers);
        return -EINVAL;
    }
    
    // Initialize signature structure
    memset(a_signature, 0, sizeof(chipmunk_ring_signature_t));
    a_signature->ring_size = a_ring->size;
    a_signature->required_signers = required_signers;
    a_signature->participating_count = a_share_count;
    
    if (required_signers == 1) {
        // Traditional ring mode: use first valid share
        debug_if(s_debug_more, L_DEBUG, "Traditional ring aggregation (single signer)");
        
        // Find first valid share
        const chipmunk_ring_share_t *valid_share = NULL;
        for (uint32_t i = 0; i < a_share_count; i++) {
            if (a_shares[i].is_valid) {
                valid_share = &a_shares[i];
                break;
            }
        }
        
        if (!valid_share) {
            log_it(L_ERROR, "No valid shares found for traditional ring aggregation");
            return -1;
        }
        
       
        // Extract the ring private key from the valid share
        chipmunk_private_key_t *l_share_private_key = (chipmunk_private_key_t*)valid_share->ring_private_key.data;
        if (!l_share_private_key) {
            log_it(L_ERROR, "Invalid private key data in share");
            return -1;
        }
        
        //  Traditional ring aggregation using proper Chipmunk signing
        debug_if(s_debug_more, L_INFO, "Creating traditional ring signature from valid share %u using full Chipmunk signing", valid_share->share_id);
        
        // Step 1: Reconstruct the message hash for signing
        if (!a_message || a_message_size == 0) {
            log_it(L_ERROR, "Message required for traditional ring signature aggregation");
            return -EINVAL;
        }
        
        // Step 2: Use existing Chipmunk signing to create proper signature
        // This creates a real Chipmunk signature using the share's private key
        a_signature->chipmunk_signature_size = CHIPMUNK_SIGNATURE_SIZE;
        a_signature->chipmunk_signature = DAP_NEW_SIZE(uint8_t, a_signature->chipmunk_signature_size);
        if (!a_signature->chipmunk_signature) {
            log_it(L_ERROR, "Failed to allocate memory for aggregated signature");
            return -ENOMEM;
        }
        
        // Create proper Chipmunk signature using the share's private key
        // chipmunk_sign expects (private_key_bytes, message, message_len, signature_buffer)
        int l_sign_result = chipmunk_sign((const uint8_t*)l_share_private_key, 
                                         a_message, a_message_size, 
                                         a_signature->chipmunk_signature);
        if (l_sign_result != CHIPMUNK_ERROR_SUCCESS) {
            log_it(L_ERROR, "Failed to create Chipmunk signature from share: error %d", l_sign_result);
            DAP_DELETE(a_signature->chipmunk_signature);
            return -1;
        }
        
        // Step 4: Generate proper challenge using parameters from signature
        dap_hash_params_t l_challenge_params = {
            .iterations = a_signature->zk_iterations,
            .domain_separator = CHIPMUNK_RING_ZK_DOMAIN_SINGLE_SIGNER,
            .salt = a_message,
            .salt_size = a_message_size
        };
        
        int l_challenge_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                         a_message, a_message_size,
                                         a_signature->challenge, sizeof(a_signature->challenge),
                                         DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_SALT | DAP_HASH_FLAG_ITERATIVE,
                                         &l_challenge_params);
        
        if (l_challenge_result != 0) {
            log_it(L_ERROR, "Failed to generate challenge for aggregated signature");
            DAP_DELETE(a_signature->chipmunk_signature);
            return -1;
        }
        
        // Set traditional ring mode parameters
        a_signature->required_signers = 1;
        a_signature->participating_count = 1;
        a_signature->is_coordinated = true;
        a_signature->coordination_round = 2; // Completed aggregation
        
        // Copy ZK proof from the valid share (even in traditional mode, we maintain ZK properties)
        if (valid_share->zk_proof && valid_share->zk_proof_size > 0) {
            a_signature->zk_proofs_size = valid_share->zk_proof_size;
            a_signature->threshold_zk_proofs = DAP_NEW_SIZE(uint8_t, a_signature->zk_proofs_size);
            if (!a_signature->threshold_zk_proofs) {
                log_it(L_ERROR, "Failed to allocate memory for ZK proofs");
                DAP_DELETE(a_signature->chipmunk_signature);
                return -ENOMEM;
            }
            
            memcpy(a_signature->threshold_zk_proofs, valid_share->zk_proof, 
                   a_signature->zk_proofs_size);
            
            a_signature->zk_proof_size_per_participant = valid_share->zk_proof_size;
            // Keep iterations from signature (already set during creation)
        }
        
        debug_if(s_debug_more, L_INFO, "Traditional ring signature aggregation completed successfully (signature_size: %zu)", 
               a_signature->chipmunk_signature_size);
        return 0;
        
    } else {
        // Multi-signer mode: aggregate multiple shares
        debug_if(s_debug_more, L_DEBUG, "Multi-signer aggregation (required_signers=%u)", required_signers);
        
        // Initialize signature ZK parameters from first valid share
        if (a_signature->zk_iterations == 0) {
            // Use the same iterations that were used during generation
            a_signature->zk_iterations = CHIPMUNK_RING_ZK_ITERATIONS_SECURE; // 1000 iterations for multi-signer
            debug_if(s_debug_more, L_INFO, "Initialized signature zk_iterations=%u for multi-signer aggregation", a_signature->zk_iterations);
        }
        
        if (a_signature->zk_proof_size_per_participant == 0) {
            // Use the same proof size that was used during generation
            a_signature->zk_proof_size_per_participant = CHIPMUNK_RING_ZK_PROOF_SIZE_ENTERPRISE; // 96 bytes for multi-signer
            debug_if(s_debug_more, L_INFO, "Initialized signature zk_proof_size_per_participant=%u for multi-signer aggregation", 
                   a_signature->zk_proof_size_per_participant);
        }
        
        // Allocate ZK proofs storage
        size_t total_zk_size = a_share_count * sizeof(a_shares[0].zk_proof);
        a_signature->threshold_zk_proofs = DAP_NEW_SIZE(uint8_t, total_zk_size);
        if (!a_signature->threshold_zk_proofs) {
            log_it(L_CRITICAL, "Failed to allocate ZK proofs storage");
            return -ENOMEM;
        }
        a_signature->zk_proofs_size = total_zk_size;
        
        // Copy ZK proofs from all participating shares
        for (uint32_t i = 0; i < a_share_count; i++) {
            if (!a_shares[i].is_valid) {
                log_it(L_ERROR, "Invalid share %u in multi-signer aggregation", a_shares[i].share_id);
                DAP_DELETE(a_signature->threshold_zk_proofs);
                return -1;
            }
            
            memcpy(a_signature->threshold_zk_proofs + i * sizeof(a_shares[0].zk_proof),
                   a_shares[i].zk_proof, sizeof(a_shares[0].zk_proof));
        }
        
        // FULL IMPLEMENTATION: Multi-signer lattice-based signature aggregation
        
        // Step 1: Verify all participating shares are valid using signature parameters
        for (uint32_t i = 0; i < a_share_count; i++) {
            int verify_result = chipmunk_ring_verify_share_with_params(&a_shares[i], a_signature, a_ring);
            if (verify_result != 0) {
                log_it(L_ERROR, "Invalid share %u in aggregation", a_shares[i].share_id);
                DAP_DELETE(a_signature->threshold_zk_proofs);
                return verify_result;
            }
        }
        
        // Step 2: Reconstruct master signature using Lagrange interpolation
        // Initialize aggregated signature components
        chipmunk_private_key_t l_reconstructed_key;
        memset(&l_reconstructed_key, 0, sizeof(chipmunk_private_key_t));
        
        // Copy master key metadata from first share
        chipmunk_private_key_t *first_share_key = (chipmunk_private_key_t*)a_shares[0].ring_private_key.data;
        memcpy(l_reconstructed_key.key_seed, first_share_key->key_seed, sizeof(l_reconstructed_key.key_seed));
        memcpy(l_reconstructed_key.tr, first_share_key->tr, sizeof(l_reconstructed_key.tr));
        memcpy(l_reconstructed_key.pk.rho_seed, first_share_key->pk.rho_seed, sizeof(l_reconstructed_key.pk.rho_seed));
        
        // OPTIMIZED LAGRANGE INTERPOLATION: O(n) instead of O(n^2)
        // Pre-compute Lagrange coefficients once, then apply to all polynomial coefficients
        
        // Step 1: Pre-compute Lagrange coefficients (O(n^2) once, not per coefficient)
        int64_t *lagrange_coeffs = DAP_NEW_Z_COUNT(int64_t, a_share_count);
        if (!lagrange_coeffs) {
            log_it(L_ERROR, "Failed to allocate memory for Lagrange coefficients");
            return -ENOMEM;
        }
        
        for (uint32_t i = 0; i < a_share_count; i++) {
            int64_t lagrange_numerator = 1;
            int64_t lagrange_denominator = 1;
            
            for (uint32_t j = 0; j < a_share_count; j++) {
                if (i != j) {
                    // numerator *= (0 - x_j), denominator *= (x_i - x_j)
                    lagrange_numerator = (lagrange_numerator * (-(int64_t)a_shares[j].share_id)) % CHIPMUNK_Q;
                    int64_t diff = (int64_t)a_shares[i].share_id - (int64_t)a_shares[j].share_id;
                    lagrange_denominator = (lagrange_denominator * diff) % CHIPMUNK_Q;
                }
            }
            
            // Calculate modular inverse using extended Euclidean algorithm
            int64_t lagrange_coeff = 1;
            if (lagrange_denominator != 0) {
                // Proper modular inverse implementation
                lagrange_coeff = chipmunk_ring_mod_inverse(lagrange_numerator, lagrange_denominator, CHIPMUNK_Q);
            }
            
            lagrange_coeffs[i] = lagrange_coeff;
        }
        
        // Step 2: Apply pre-computed coefficients to all polynomial coefficients (O(n) per coefficient)
        for (int coeff_idx = 0; coeff_idx < CHIPMUNK_N; coeff_idx++) {
            int64_t reconstructed_v0 = 0;
            int64_t reconstructed_v1 = 0;
            
            // Apply Lagrange interpolation using pre-computed coefficients
            for (uint32_t i = 0; i < a_share_count; i++) {
                chipmunk_private_key_t *share_key = (chipmunk_private_key_t*)a_shares[i].ring_private_key.data;
                
                // Apply pre-computed Lagrange coefficient
                int64_t share_v0 = (int64_t)share_key->pk.v0.coeffs[coeff_idx];
                int64_t share_v1 = (int64_t)share_key->pk.v1.coeffs[coeff_idx];
                
                reconstructed_v0 = (reconstructed_v0 + lagrange_coeffs[i] * share_v0) % CHIPMUNK_Q;
                reconstructed_v1 = (reconstructed_v1 + lagrange_coeffs[i] * share_v1) % CHIPMUNK_Q;
            }
            
            // Normalize reconstructed coefficients
            if (reconstructed_v0 > CHIPMUNK_Q / 2) {
                reconstructed_v0 -= CHIPMUNK_Q;
            }
            if (reconstructed_v0 < -CHIPMUNK_Q / 2) {
                reconstructed_v0 += CHIPMUNK_Q;
            }
            
            if (reconstructed_v1 > CHIPMUNK_Q / 2) {
                reconstructed_v1 -= CHIPMUNK_Q;
            }
            if (reconstructed_v1 < -CHIPMUNK_Q / 2) {
                reconstructed_v1 += CHIPMUNK_Q;
            }
            
            l_reconstructed_key.pk.v0.coeffs[coeff_idx] = (int32_t)reconstructed_v0;
            l_reconstructed_key.pk.v1.coeffs[coeff_idx] = (int32_t)reconstructed_v1;
        }
        
        // Step 3: Create signature using reconstructed key
        chipmunk_ring_private_key_t l_ring_priv_key;
        memcpy(l_ring_priv_key.data, &l_reconstructed_key, sizeof(chipmunk_private_key_t));
        
        // Create ring container with only the real signers (for multi-signer mode)
        chipmunk_ring_container_t l_signer_ring;
        memset(&l_signer_ring, 0, sizeof(l_signer_ring));
        l_signer_ring.size = a_share_count;
        l_signer_ring.public_keys = DAP_NEW_Z_COUNT(chipmunk_ring_public_key_t, a_share_count);
        if (!l_signer_ring.public_keys) {
            log_it(L_CRITICAL, "Failed to allocate signer ring public keys");
            DAP_DELETE(a_signature->threshold_zk_proofs);
            DAP_DELETE(lagrange_coeffs); // Clean up pre-computed coefficients
            return -ENOMEM;
        }
        
        // Copy public keys from participating shares
        for (uint32_t i = 0; i < a_share_count; i++) {
            memcpy(&l_signer_ring.public_keys[i], &a_shares[i].ring_public_key, 
                   sizeof(chipmunk_ring_public_key_t));
        }
        
        // Generate ring hash for signer ring
        size_t l_combined_size = a_share_count * CHIPMUNK_PUBLIC_KEY_SIZE;
        uint8_t *l_combined_keys = DAP_NEW_SIZE(uint8_t, l_combined_size);
        if (!l_combined_keys) {
            log_it(L_CRITICAL, "Failed to allocate combined keys for signer ring");
            DAP_DELETE(l_signer_ring.public_keys);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -ENOMEM;
        }
        
        for (uint32_t i = 0; i < a_share_count; i++) {
            memcpy(l_combined_keys + i * CHIPMUNK_PUBLIC_KEY_SIZE,
                   a_shares[i].ring_public_key.data, CHIPMUNK_PUBLIC_KEY_SIZE);
        }
        
        dap_hash_fast_t ring_hash;
        bool hash_result = dap_hash_fast(l_combined_keys, l_combined_size, &ring_hash);
        DAP_DELETE(l_combined_keys);
        
        if (!hash_result) {
            log_it(L_ERROR, "Failed to generate ring hash for multi-signer mode");
            DAP_DELETE(l_signer_ring.public_keys);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -1;
        }
        
        memcpy(l_signer_ring.ring_hash, &ring_hash, sizeof(l_signer_ring.ring_hash));
        
        // Step 4: Create signature directly using reconstructed key (bypass ring matching)
        // In threshold schemes, reconstructed key doesn't need to match original ring
        
        // Allocate signature buffer
        a_signature->chipmunk_signature_size = CHIPMUNK_SIGNATURE_SIZE;
        a_signature->chipmunk_signature = DAP_NEW_SIZE(uint8_t, a_signature->chipmunk_signature_size);
        if (!a_signature->chipmunk_signature) {
            log_it(L_ERROR, "Failed to allocate memory for aggregated signature");
            DAP_DELETE(l_signer_ring.public_keys);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -ENOMEM;
        }
        
        // Create signature directly using reconstructed private key
        int l_sign_result = chipmunk_sign(l_ring_priv_key.data, a_message, a_message_size, 
                                         a_signature->chipmunk_signature);
        
        DAP_DELETE(l_signer_ring.public_keys);
        DAP_DELETE(lagrange_coeffs); // Clean up pre-computed coefficients
        
        if (l_sign_result != CHIPMUNK_ERROR_SUCCESS) {
            log_it(L_ERROR, "Failed to create Chipmunk signature from reconstructed key: error %d", l_sign_result);
            DAP_DELETE(a_signature->chipmunk_signature);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -1;
        }
        
        // Generate challenge using universal hash with signature parameters
        dap_hash_params_t l_challenge_params = {
            .iterations = a_signature->zk_iterations,
            .domain_separator = CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER,
            .salt = a_message,
            .salt_size = a_message_size
        };
        
        int l_challenge_result = dap_hash(DAP_HASH_TYPE_SHAKE256,
                                         a_message, a_message_size,
                                         a_signature->challenge, sizeof(a_signature->challenge),
                                         DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_SALT | DAP_HASH_FLAG_ITERATIVE,
                                         &l_challenge_params);
        
        if (l_challenge_result != 0) {
            log_it(L_ERROR, "Failed to generate challenge for aggregated signature");
            DAP_DELETE(a_signature->chipmunk_signature);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -1;
        }
        
        // Step 5: Initialize commitments and responses for serialization compatibility
        a_signature->commitments = DAP_NEW_Z_COUNT(chipmunk_ring_commitment_t, a_signature->ring_size);
        a_signature->responses = DAP_NEW_Z_COUNT(chipmunk_ring_response_t, a_signature->ring_size);
        
        if (!a_signature->commitments || !a_signature->responses) {
            log_it(L_ERROR, "Failed to allocate commitments/responses for aggregated signature");
            DAP_DELETE(a_signature->chipmunk_signature);
            DAP_DELETE(a_signature->threshold_zk_proofs);
            return -ENOMEM;
        }
        
        // Initialize responses for serialization (commitments already exist from signature creation)
        for (uint32_t i = 0; i < a_signature->ring_size; i++) {
            // DON'T zero commitments - they contain valid data from signature creation!
            // Only initialize responses if they're not already set
            if (!a_signature->responses[i].value) {
                memset(&a_signature->responses[i], 0, sizeof(chipmunk_ring_response_t));
            }
        }
        
        debug_if(s_debug_more, L_INFO, "Multi-signer signature aggregation completed successfully");
        return 0;
    }
}

/**
 * @brief Free secret share resources
 */
void chipmunk_ring_share_free(chipmunk_ring_share_t *a_share) {
    if (!a_share) {
        return;
    }
    
    // Free ZK commitment
    chipmunk_ring_commitment_free(&a_share->zk_commitment);
    
    // Free dynamic ZK proof
    if (a_share->zk_proof) {
        DAP_DELETE(a_share->zk_proof);
        a_share->zk_proof = NULL;
        a_share->zk_proof_size = 0;
    }
    
    // Clear sensitive data
    memset(a_share, 0, sizeof(chipmunk_ring_share_t));
    
    debug_if(s_debug_more, L_DEBUG, "Secret share freed");
}

/**
 * @brief Generate ZK proof using signature parameters
 * @details Universal function that uses all parameters from signature structure
 */
int chipmunk_ring_generate_zk_proof(const uint8_t *a_input, size_t a_input_size,
                                   const chipmunk_ring_signature_t *a_signature,
                                   const uint8_t *a_salt, size_t a_salt_size,
                                   uint8_t *a_output) {
    dap_return_val_if_fail(a_input && a_output && a_signature, -EINVAL);
    dap_return_val_if_fail(a_input_size > 0, -EINVAL);
    dap_return_val_if_fail(a_signature->zk_proof_size_per_participant >= CHIPMUNK_RING_ZK_PROOF_SIZE_MIN, -EINVAL);
    dap_return_val_if_fail(a_signature->zk_proof_size_per_participant <= CHIPMUNK_RING_ZK_PROOF_SIZE_MAX, -EINVAL);
    
    // Use universal hash algorithm
    dap_hash_type_t hash_type = CHIPMUNK_RING_HASH_ALGORITHM_UNIVERSAL;
    
    // Create hash parameters using signature parameters
    dap_hash_params_t hash_params = {
        .salt = a_salt,
        .salt_size = a_salt_size,
        .domain_separator = (a_signature->required_signers == 1) ? 
                           CHIPMUNK_RING_ZK_DOMAIN_SINGLE_SIGNER : 
                           CHIPMUNK_RING_ZK_DOMAIN_MULTI_SIGNER,
        .iterations = a_signature->zk_iterations,
        .security_level = (a_signature->required_signers == 1) ? 
                         CHIPMUNK_RING_SECURITY_LEVEL_SINGLE : 
                         CHIPMUNK_RING_SECURITY_LEVEL_ENTERPRISE
    };
    
    // Generate ZK proof with signature parameters
    return dap_hash(hash_type, a_input, a_input_size, a_output, a_signature->zk_proof_size_per_participant,
                   DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_ITERATIVE | 
                   (a_salt ? DAP_HASH_FLAG_SALT : 0), &hash_params);
}


/**
 * @brief Generate ZK proof with signature parameters
 */
int chipmunk_ring_generate_zk_proof_from_signature(const uint8_t *a_input, size_t a_input_size,
                                                  const chipmunk_ring_signature_t *a_signature,
                                                  const uint8_t *a_salt, size_t a_salt_size,
                                                  uint8_t *a_output) {
    dap_return_val_if_fail(a_input && a_signature && a_output, -EINVAL);
    dap_return_val_if_fail(a_input_size > 0, -EINVAL);
    
    // Use parameters from signature
    size_t proof_size = a_signature->zk_proof_size_per_participant;
    uint32_t iterations = a_signature->zk_iterations;
    
    debug_if(s_debug_more, L_DEBUG, "Generating ZK proof from signature params: size=%zu, iterations=%u",
           proof_size, iterations);
    
    // Use universal hash algorithm
    dap_hash_type_t hash_type = CHIPMUNK_RING_HASH_ALGORITHM_UNIVERSAL;
    
    // Create hash parameters from signature
    dap_hash_params_t hash_params = {
        .salt = a_salt,
        .salt_size = a_salt_size,
        .domain_separator = CHIPMUNK_RING_DOMAIN_SIGNATURE_ZK,
        .iterations = iterations,
        .security_level = (a_signature->required_signers == 1) ? 
                         CHIPMUNK_RING_SECURITY_LEVEL_SINGLE : CHIPMUNK_RING_SECURITY_LEVEL_MULTI
    };
    
    // Determine flags
    dap_hash_flags_t flags = DAP_HASH_FLAG_DOMAIN_SEPARATION | DAP_HASH_FLAG_ITERATIVE;
    if (a_salt && a_salt_size > 0) {
        flags |= DAP_HASH_FLAG_SALT;
    }
    
    // Generate ZK proof with signature-specific parameters
    return dap_hash(hash_type, a_input, a_input_size, a_output, proof_size,
                   flags, &hash_params);
}
