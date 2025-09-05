/*
 * Authors:
 * [Developer Name] <email@demlabs.net>
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

#include "dap_enc_chipmunk_ring.h"

#include <errno.h>

#include "dap_common.h"
#include "dap_crypto_common.h"
#include "dap_enc_key.h"
#include "chipmunk/chipmunk.h"

#define LOG_TAG "dap_enc_chipmunk_ring"

/**
 * @brief Initialize Chipmunk_Ring module
 */
int dap_enc_chipmunk_ring_init(void) {
    // Initialize Chipmunk (underlying signature scheme)
    if (chipmunk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk for Chipmunk_Ring");
        return -1;
    }

    log_it(L_INFO, "Chipmunk_Ring initialized successfully");
    return 0;
}

/**
 * @brief Generate new Chipmunk_Ring keypair
 */
int dap_enc_chipmunk_ring_key_new(struct dap_enc_key *a_key) {
    dap_return_val_if_fail(a_key, -EINVAL);

    // Use standard Chipmunk key generation
    return chipmunk_keypair(a_key->pub_key_data, a_key->pub_key_data_size,
                           a_key->priv_key_data, a_key->priv_key_data_size);
}

/**
 * @brief Generate keypair from seed
 */
int dap_enc_chipmunk_ring_key_new_generate(struct dap_enc_key *a_key, const void *a_seed,
                                 size_t a_seed_size, size_t a_key_size) {
    dap_return_val_if_fail(a_key, -EINVAL);
    dap_return_val_if_fail(a_seed, -EINVAL);
    dap_return_val_if_fail(a_seed_size == 32, -EINVAL);

    // Use deterministic Chipmunk key generation
    return chipmunk_keypair_from_seed(a_seed,
                                     a_key->pub_key_data, a_key->pub_key_data_size,
                                     a_key->priv_key_data, a_key->priv_key_data_size);
}

/**
 * @brief Delete Chipmunk_Ring key
 */
void dap_enc_chipmunk_ring_key_delete(struct dap_enc_key *a_key) {
    if (a_key) {
        // Use standard cleanup for sensitive data
        memset(a_key->priv_key_data, 0, a_key->priv_key_data_size);
        memset(a_key->pub_key_data, 0, a_key->pub_key_data_size);
    }
}

/**
 * @brief Get signature size for given ring size
 */
size_t dap_enc_chipmunk_ring_get_signature_size(size_t a_ring_size) {
    if (a_ring_size > CHIPMUNK_RING_MAX_RING_SIZE) {
        return 0;
    }

    return sizeof(uint32_t) + // ring_size
           sizeof(uint32_t) + // signer_index
           32 +              // linkability_tag
           32 +              // challenge
           a_ring_size * (32 + 32) + // commitments (value + randomness)
           a_ring_size * 32 +  // responses
           CHIPMUNK_SIGNATURE_SIZE; // chipmunk_signature
}