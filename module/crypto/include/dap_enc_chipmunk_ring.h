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

#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Chipmunk_Ring ring signature parameters
 */
#define CHIPMUNK_RING_MAX_RING_SIZE 1024

/**
 * @brief Initialize Chipmunk_Ring module
 * @return 0 on success, negative on error
 */
int dap_enc_chipmunk_ring_init(void);

/**
 * @brief Generate Chipmunk_Ring keypair (same as Chipmunk)
 * @param a_key Output key structure
 * @return 0 on success, negative on error
 */
int dap_enc_chipmunk_ring_key_new(struct dap_enc_key *a_key);

/**
 * @brief Generate keypair from seed
 * @param a_key Output key structure
 * @param a_seed Seed for deterministic generation
 * @param a_seed_size Seed size
 * @param a_key_size Key size (unused, kept for compatibility)
 * @return 0 on success, negative on error
 */
int dap_enc_chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size);

/**
 * @brief Delete Chipmunk_Ring key
 * @param a_key Key to delete
 */
void dap_enc_chipmunk_ring_key_delete(struct dap_enc_key *a_key);

/**
 * @brief Get signature size for given ring size
 * @param a_ring_size Number of participants
 * @return Required signature buffer size
 */
size_t dap_enc_chipmunk_ring_get_signature_size(size_t a_ring_size);

/* ===== CALLBACK FUNCTIONS ===== */

void dap_enc_chipmunk_ring_key_new_callback(struct dap_enc_key *a_key);
void dap_enc_chipmunk_ring_key_generate_callback(struct dap_enc_key *a_key, const void *a_kex_buf,
                                               size_t a_kex_size, const void *a_seed,
                                               size_t a_seed_size, size_t a_key_size);
void dap_enc_chipmunk_ring_key_delete(struct dap_enc_key *a_key);

int dap_enc_chipmunk_ring_get_sign(struct dap_enc_key *a_key, const void *a_data,
                                  size_t a_data_size, void *a_output, size_t a_output_size);
int dap_enc_chipmunk_ring_verify_sign(struct dap_enc_key *a_key, const void *a_data,
                                     size_t a_data_size, void *a_sign, size_t a_sign_size);

/**
 * @brief Create Chipmunk_Ring signature
 * @param a_priv_key Private key of the signer
 * @param a_data Data to sign
 * @param a_data_size Size of data to sign
 * @param a_ring_pub_keys Array of public keys for the ring
 * @param a_ring_size Number of participants in the ring
 * @param a_signer_index Index of the actual signer in the ring
 * @param a_signature Output buffer for signature
 * @param a_signature_size Size of signature buffer
 * @return 0 on success, negative on error
 */
int dap_enc_chipmunk_ring_sign(const void *a_priv_key,
                              const void *a_data,
                              size_t a_data_size,
                              uint8_t **a_ring_pub_keys,
                              size_t a_ring_size,
                              size_t a_signer_index,
                              uint8_t *a_signature,
                              size_t a_signature_size);

#ifdef __cplusplus
}
#endif