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

#include "dap_enc_chipmunk_ring_test.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_chipmunk_ring.h"
#include "dap_enc_key.h"
#include "chipmunk.h"

#define LOG_TAG "chipmunk_ring_test"

/**
 * @brief Test Chipmunk_Ring basic functionality
 */
int dap_enc_chipmunk_ring_tests_run(const int a_times) {
    log_it(L_INFO, "Starting Chipmunk_Ring tests...");

    // Initialize Chipmunk_Ring
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk_Ring");
        return -1;
    }

    // Test basic key generation
    struct dap_enc_key key1 = {0};
    key1.pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    key1.priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    key1.pub_key_data = DAP_CALLOC(1, CHIPMUNK_PUBLIC_KEY_SIZE);
    key1.priv_key_data = DAP_CALLOC(1, CHIPMUNK_PRIVATE_KEY_SIZE);

    if (!key1.pub_key_data || !key1.priv_key_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return -ENOMEM;
    }

    if (dap_enc_chipmunk_ring_key_new(&key1) != 0) {
        log_it(L_ERROR, "Failed to generate Chipmunk_Ring keypair");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        return -1;
    }

    log_it(L_INFO, "✓ Key generation test passed");

    // Test deterministic key generation
    struct dap_enc_key key2 = {0};
    key2.pub_key_data_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    key2.priv_key_data_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    key2.pub_key_data = calloc(1, CHIPMUNK_PUBLIC_KEY_SIZE);
    key2.priv_key_data = calloc(1, CHIPMUNK_PRIVATE_KEY_SIZE);

    if (!key2.pub_key_data || !key2.priv_key_data) {
        log_it(L_ERROR, "Failed to allocate key2 memory");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        return -1;
    }

    uint8_t seed[32] = {0};
    for (int i = 0; i < 32; i++) seed[i] = i;

    if (dap_enc_chipmunk_ring_key_new_generate(&key2, seed, 32, 0) != 0) {
        log_it(L_ERROR, "Failed to generate deterministic keypair");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    log_it(L_INFO, "✓ Deterministic key generation test passed");

    // Test signature size calculation
    size_t sig_size_64 = dap_enc_chipmunk_ring_get_signature_size(64);
    size_t sig_size_128 = dap_enc_chipmunk_ring_get_signature_size(128);

    if (sig_size_64 == 0 || sig_size_128 == 0) {
        log_it(L_ERROR, "Invalid signature size calculation");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    if (sig_size_128 <= sig_size_64) {
        log_it(L_ERROR, "Signature size should increase with ring size");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    log_it(L_INFO, "✓ Signature size calculation test passed (64: %zu, 128: %zu)",
           sig_size_64, sig_size_128);

    // TODO: Test serialization/deserialization when ring signature API is implemented
    /*
    // Test serialization/deserialization
    chipmunk_ring_signature_t sig = {0};
    sig.ring_size = 64;
    sig.signer_index = 0;

    // Fill with test data
    memset(sig.linkability_tag, 0xAA, 32);
    memset(sig.challenge, 0xBB, 32);
    for (int i = 0; i < 64; i++) {
        memset(sig.responses[i], (uint8_t)i, 32);
    }
    memset(sig.chipmunk_signature, 0xCC, CHIPMUNK_SIGNATURE_SIZE);

    // Serialize
    uint8_t *serialized = calloc(1, sig_size_64);
    if (!serialized) {
        log_it(L_ERROR, "Failed to allocate serialization buffer");
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    if (chipmunk_ring_signature_to_bytes(&sig, serialized, sig_size_64) != 0) {
        log_it(L_ERROR, "Failed to serialize signature");
        DAP_FREE(serialized);
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    // Deserialize
    chipmunk_ring_signature_t sig_restored = {0};
    if (chipmunk_ring_signature_from_bytes(&sig_restored, serialized, sig_size_64) != 0) {
        log_it(L_ERROR, "Failed to deserialize signature");
        DAP_FREE(serialized);
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    // Verify deserialization
    if (sig_restored.ring_size != sig.ring_size ||
        sig_restored.signer_index != sig.signer_index ||
        memcmp(sig_restored.linkability_tag, sig.linkability_tag, 32) != 0 ||
        memcmp(sig_restored.challenge, sig.challenge, 32) != 0 ||
        memcmp(sig_restored.chipmunk_signature, sig.chipmunk_signature, CHIPMUNK_SIGNATURE_SIZE) != 0) {
        log_it(L_ERROR, "Deserialization verification failed");
        DAP_FREE(serialized);
        DAP_FREE(key1.pub_key_data);
        DAP_FREE(key1.priv_key_data);
        DAP_FREE(key2.pub_key_data);
        DAP_FREE(key2.priv_key_data);
        return -1;
    }

    log_it(L_INFO, "✓ Serialization/deserialization test passed");

    // Cleanup
    DAP_FREE(serialized);
    DAP_FREE(key1.pub_key_data);
    DAP_FREE(key1.priv_key_data);
    DAP_FREE(key2.pub_key_data);
    DAP_FREE(key2.priv_key_data);
    */

    log_it(L_INFO, "Chipmunk_Ring basic tests completed successfully!");
    return 0;
}
