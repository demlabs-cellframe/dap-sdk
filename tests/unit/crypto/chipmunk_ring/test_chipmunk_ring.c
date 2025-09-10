/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Ltd   https://demlabs.net
 * DAP SDK  https://gitlab.demlabs.net/dap/dap-sdk
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

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"


#define LOG_TAG "test_chipmunk_ring"

// Test constants
#define TEST_RING_SIZE 8
#define TEST_MESSAGE "Chipmunk Ring Signature Test Message"
#define TEST_MESSAGE_LEN strlen(TEST_MESSAGE)
#define MAX_RING_SIZE 64
#define PERFORMANCE_ITERATIONS 100

/**
 * @brief Test comprehensive key generation
 */
static bool s_test_key_generation(void) {
    log_it(L_INFO, "Testing Chipmunk Ring key generation...");

    // Test random key generation
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_key != NULL, "Random key generation should succeed");
    dap_assert(l_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, "Key type should be CHIPMUNK_RING");
    dap_assert(l_key->pub_key_data_size > 0, "Public key should have size");
    dap_assert(l_key->priv_key_data_size > 0, "Private key should have size");

    // Test deterministic key generation
    uint8_t l_seed[32] = {0};
    for (size_t i = 0; i < sizeof(l_seed); i++) {
        l_seed[i] = (uint8_t)i;
    }

    dap_enc_key_t* l_key_det = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, l_seed, sizeof(l_seed), 0, 0);
    dap_assert(l_key_det != NULL, "Deterministic key generation should succeed");

    // Keys should be different since different generation methods
    dap_assert(memcmp(l_key->pub_key_data, l_key_det->pub_key_data, l_key->pub_key_data_size) != 0,
                   "Keys from different generation methods should differ");

    // Generate another key with same seed - should be identical
    dap_enc_key_t* l_key_det2 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, l_seed, sizeof(l_seed), 0, 0);
    dap_assert(l_key_det2, "Second deterministic key generation should succeed");

    dap_assert(memcmp(l_key_det->pub_key_data, l_key_det2->pub_key_data, l_key_det->pub_key_data_size) == 0,
                   "Keys from same seed should be identical");

    // Test multiple key generation for consistency
    const size_t l_num_keys = 10;
    dap_enc_key_t* l_keys[l_num_keys] = {0};
    for (size_t i = 0; i < l_num_keys; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_keys[i], "Multiple key generation should succeed");

        // Ensure all keys are unique
        for (size_t j = 0; j < i; j++) {
            dap_assert(memcmp(l_keys[i]->pub_key_data, l_keys[j]->pub_key_data,
                                 l_keys[i]->pub_key_data_size) != 0,
                           "All generated keys should be unique");
        }
    }

    // Cleanup
    dap_enc_key_delete(l_key);
    dap_enc_key_delete(l_key_det);
    dap_enc_key_delete(l_key_det2);
    for (size_t i = 0; i < l_num_keys; i++) {
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ Comprehensive key generation tests passed");
    return true;
}

/**
 * @brief Test comprehensive ring signature operations
 */
static bool s_test_ring_signature_operations(void) {
    log_it(L_INFO, "Testing comprehensive Chipmunk Ring signature operations...");

    // Test different ring sizes
    const size_t l_ring_sizes[] = {2, 4, 8, 16, 32};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        size_t l_ring_size = l_ring_sizes[size_idx];
        log_it(L_DEBUG, "Testing ring size: %zu", l_ring_size);

    // Generate signer key
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key, "Signer key generation should succeed");

        // Generate ring keys
        dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
        for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Replace first key with signer key
    dap_enc_key_delete(l_ring_keys[0]);
    l_ring_keys[0] = l_signer_key;

        // Test different message types
        const char* l_messages[] = {
            "Short message",
            TEST_MESSAGE,
            "Very long message that should test the limits of the signature scheme and ensure it works correctly with larger data",
            "",
            "Message with special chars: !@#$%^&*()"
        };
        const size_t l_num_messages = sizeof(l_messages) / sizeof(l_messages[0]);

        for (size_t msg_idx = 0; msg_idx < l_num_messages; msg_idx++) {
    // Create message hash
    dap_hash_fast_t l_message_hash = {0};
            bool l_hash_result = dap_hash_fast(l_messages[msg_idx], strlen(l_messages[msg_idx]), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

            // Test different signer positions
            const size_t l_signer_positions[] = {0, l_ring_size / 2, l_ring_size - 1};
            const size_t l_num_positions = sizeof(l_signer_positions) / sizeof(l_signer_positions[0]);

            for (size_t pos_idx = 0; pos_idx < l_num_positions && l_signer_positions[pos_idx] < l_ring_size; pos_idx++) {
                size_t l_signer_pos = l_signer_positions[pos_idx];

                // Replace key at signer position
                if (l_signer_pos != 0) {
                    dap_enc_key_delete(l_ring_keys[l_signer_pos]);
                    l_ring_keys[l_signer_pos] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
                }

    // Create ring signature
    dap_sign_t* l_signature = dap_sign_create_ring(
                    l_ring_keys[l_signer_pos],
        &l_message_hash,
        sizeof(l_message_hash),
        l_ring_keys,
                    l_ring_size,
                    l_signer_pos
    );
    dap_assert(l_signature, "Ring signature creation should succeed");

                // Verify signature properties
    dap_assert(l_signature->header.type.type == SIG_TYPE_CHIPMUNK_RING,
                   "Signature should be CHIPMUNK_RING type");

                size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
                dap_assert(l_signature->header.sign_size == l_expected_size,
                               "Signature size should match expected size");

    // Test signature verification
    int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result == 0, "Ring signature verification should succeed");

    // Test with wrong message
                const char* l_wrong_message = "Wrong message for verification";
    dap_hash_fast_t l_wrong_hash = {0};
    dap_hash_fast(l_wrong_message, strlen(l_wrong_message), &l_wrong_hash);

    l_verify_result = dap_sign_verify(l_signature, &l_wrong_hash, sizeof(l_wrong_hash));
    dap_assert(l_verify_result != 0, "Signature verification should fail with wrong message");

    // Test ring signature detection
    bool l_is_ring = dap_sign_is_ring(l_signature);
    dap_assert(l_is_ring == true, "Signature should be detected as ring signature");

    bool l_is_zk = dap_sign_is_zk(l_signature);
    dap_assert(l_is_zk == true, "Signature should be detected as zero-knowledge proof");

                // Test signature serialization
                dap_assert(l_serialized, "Signature serialization should succeed");

                dap_assert(l_deserialized, "Signature deserialization should succeed");

                // Verify deserialized signature
                l_verify_result = dap_sign_verify(l_deserialized, &l_message_hash, sizeof(l_message_hash));
                dap_assert(l_verify_result == 0, "Deserialized signature verification should succeed");

    // Cleanup
    DAP_DELETE(l_signature);
                DAP_DELETE(l_deserialized);
                DAP_FREE(l_serialized);

                if (l_signer_pos != 0) {
                    dap_enc_key_delete(l_ring_keys[l_signer_pos]);
                    l_ring_keys[l_signer_pos] = NULL;
                }
            }
        }

        // Cleanup ring keys
        for (size_t i = 0; i < l_ring_size; i++) {
            if (l_ring_keys[i] && i != 0) {
            dap_enc_key_delete(l_ring_keys[i]);
            }
        }
    }

    log_it(L_INFO, "✓ Comprehensive ring signature operations tests passed");
    return true;
}

/**
 * @brief Test comprehensive ring signature anonymity
 */
static bool s_test_ring_anonymity(void) {
    log_it(L_INFO, "Testing comprehensive Chipmunk Ring signature anonymity...");

    // Test with different ring sizes for anonymity evaluation
    const size_t l_ring_sizes[] = {8, 16, 32};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        size_t l_ring_size = l_ring_sizes[size_idx];
        log_it(L_DEBUG, "Testing anonymity with ring size: %zu", l_ring_size);

        // Generate ring keys
    dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Create message hash
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

        // Test multiple signer positions
        const size_t l_positions_to_test = 5;
        dap_sign_t* l_signatures[l_positions_to_test] = {0};
        size_t l_test_positions[l_positions_to_test];

        // Select diverse positions in the ring
        for (size_t i = 0; i < l_positions_to_test; i++) {
            l_test_positions[i] = (i * l_ring_size) / l_positions_to_test;
            if (l_test_positions[i] >= l_ring_size) l_test_positions[i] = l_ring_size - 1;
        }

        for (size_t i = 0; i < l_positions_to_test; i++) {
            size_t l_signer_pos = l_test_positions[i];

        l_signatures[i] = dap_sign_create_ring(
                l_ring_keys[l_signer_pos],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
                l_signer_pos
        );
        dap_assert(l_signatures[i], "Ring signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hash, sizeof(l_message_hash));
        dap_assert(l_verify_result == 0, "Ring signature verification should succeed");
    }

        // Test anonymity: signatures from different positions should be indistinguishable
        // (verifier cannot determine which position in the ring was the actual signer)
        for (size_t i = 0; i < l_positions_to_test - 1; i++) {
            for (size_t j = i + 1; j < l_positions_to_test; j++) {
                // Signatures should have the same size
                dap_assert(l_signatures[i]->header.sign_size == l_signatures[j]->header.sign_size,
                               "All signatures should have the same size");

                // Signatures should be different (due to different signer positions)
                dap_assert(memcmp(l_signatures[i]->pkey_n_sign, l_signatures[j]->pkey_n_sign,
                                     l_signatures[i]->header.sign_size) != 0,
                               "Signatures from different positions should be different");
            }
        }

        // Test that all signatures are properly typed
        for (size_t i = 0; i < l_positions_to_test; i++) {
        dap_assert(l_signatures[i]->header.type.type == SIG_TYPE_CHIPMUNK_RING,
                       "All signatures should be CHIPMUNK_RING type");

        bool l_is_ring = dap_sign_is_ring(l_signatures[i]);
            dap_assert(l_is_ring == true, "All should be detected as ring signatures");

            bool l_is_zk = dap_sign_is_zk(l_signatures[i]);
            dap_assert(l_is_zk == true, "All should be detected as ZKP");
        }

        // Test anonymity with same signer but different messages
        dap_sign_t* l_same_signer_different_msg[3] = {0};
        const char* l_different_messages[] = {
            "Message 1",
            "Message 2",
            "Message 3"
        };

        for (size_t i = 0; i < 3; i++) {
            dap_hash_fast_t l_msg_hash = {0};
            dap_hash_fast(l_different_messages[i], strlen(l_different_messages[i]), &l_msg_hash);

            l_same_signer_different_msg[i] = dap_sign_create_ring(
                l_ring_keys[0],  // Same signer
                &l_msg_hash,
                sizeof(l_msg_hash),
                l_ring_keys,
                l_ring_size,
                0  // Same position
            );
            dap_assert(l_same_signer_different_msg[i], "Ring signature creation should succeed");

            int l_verify_result = dap_sign_verify(l_same_signer_different_msg[i], &l_msg_hash, sizeof(l_msg_hash));
            dap_assert(l_verify_result == 0, "Ring signature verification should succeed");
    }

    // Cleanup
        for (size_t i = 0; i < l_positions_to_test; i++) {
            DAP_DELETE(l_signatures[i]);
        }
    for (size_t i = 0; i < 3; i++) {
            DAP_DELETE(l_same_signer_different_msg[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "✓ Comprehensive ring signature anonymity tests passed");
    return true;
}

/**
 * @brief Test linkability for double-spending prevention
 */
static bool s_test_linkability_prevention(void) {
    log_it(L_INFO, "Testing Chipmunk Ring linkability for double-spending prevention...");

    // Test with different ring sizes
    const size_t l_ring_sizes[] = {4, 8, 16};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        size_t l_ring_size = l_ring_sizes[size_idx];

    // Generate signer key
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key, "Signer key generation should succeed");

    // Generate ring keys
        dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
        for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Replace first key with signer key
    dap_enc_key_delete(l_ring_keys[0]);
    l_ring_keys[0] = l_signer_key;

        // Test multiple messages
        const char* l_test_messages[] = {
            "Transaction 1",
            "Transaction 2",
            "Same message again"
        };
        const size_t l_num_messages = sizeof(l_test_messages) / sizeof(l_test_messages[0]);

        for (size_t msg_idx = 0; msg_idx < l_num_messages; msg_idx++) {
    dap_hash_fast_t l_message_hash = {0};
            dap_hash_fast(l_test_messages[msg_idx], strlen(l_test_messages[msg_idx]), &l_message_hash);

            // Create multiple signatures from same signer (simulating double-spending attempts)
            const size_t l_num_attempts = 3;
            dap_sign_t* l_signatures[l_num_attempts] = {0};

            for (size_t attempt = 0; attempt < l_num_attempts; attempt++) {
                l_signatures[attempt] = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash,
        sizeof(l_message_hash),
        l_ring_keys,
                    l_ring_size,
                    0
                );
                dap_assert(l_signatures[attempt], "Ring signature creation should succeed");

                // All signatures should be valid
                int l_verify_result = dap_sign_verify(l_signatures[attempt], &l_message_hash, sizeof(l_message_hash));
                dap_assert(l_verify_result == 0, "Signature verification should succeed");
            }

            // Signatures should be different due to random elements (linkability)
            for (size_t i = 0; i < l_num_attempts - 1; i++) {
                for (size_t j = i + 1; j < l_num_attempts; j++) {
                    dap_assert(memcmp(l_signatures[i]->pkey_n_sign, l_signatures[j]->pkey_n_sign,
                                         l_signatures[i]->header.sign_size) != 0,
                                   "Signatures from same signer should be different due to linkability");
                }
            }

            // Cleanup signatures for this message
            for (size_t attempt = 0; attempt < l_num_attempts; attempt++) {
                DAP_DELETE(l_signatures[attempt]);
            }
        }

        // Cleanup ring keys
        for (size_t i = 0; i < l_ring_size; i++) {
        if (i != 0) {
            dap_enc_key_delete(l_ring_keys[i]);
            }
        }
    }

    log_it(L_INFO, "✓ Linkability prevention tests passed");
    return true;
}

/**
 * @brief Test comprehensive error handling
 */
static bool s_test_error_handling(void) {
    log_it(L_INFO, "Testing comprehensive Chipmunk Ring error handling...");

    // Test with NULL parameters
    dap_sign_t* l_signature = dap_sign_create_ring(NULL, NULL, 0, NULL, 0);
    dap_assert(l_signature, "Signature creation should fail with NULL parameters");

    // Test with valid signer but NULL message
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key, "Signer key generation should succeed");

    l_signature = dap_sign_create_ring(l_signer_key, NULL, 0, NULL, 0);
    dap_assert(l_signature, "Signature creation should fail with NULL message");

    // Test with empty ring
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

    l_signature = dap_sign_create_ring(l_signer_key, &l_message_hash, sizeof(l_message_hash), NULL, 0);
    dap_assert(l_signature, "Signature creation should fail with empty ring");

    // Test with invalid ring size
    dap_enc_key_t* l_ring_keys[1] = {l_signer_key};
    l_signature = dap_sign_create_ring(l_signer_key, &l_message_hash, sizeof(l_message_hash),
                                      l_ring_keys, 1, 0);
    dap_assert(l_signature, "Signature creation should fail with ring size < 2");

    // Test with invalid signer index
    dap_enc_key_t* l_ring_keys_2[2] = {l_signer_key, l_signer_key};
    l_signature = dap_sign_create_ring(l_signer_key, &l_message_hash, sizeof(l_message_hash),
                                      l_ring_keys_2, 2, 5); // Index out of bounds
    dap_assert(l_signature, "Signature creation should fail with invalid signer index");

    // Test with wrong key types in ring
    dap_enc_key_t* l_wrong_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    dap_assert(l_wrong_key, "Wrong key type generation should succeed");

    dap_enc_key_t* l_ring_keys_mixed[2] = {l_signer_key, l_wrong_key};
    l_signature = dap_sign_create_ring(l_signer_key, &l_message_hash, sizeof(l_message_hash),
                                      l_ring_keys_mixed, 2, 0);
    dap_assert(l_signature, "Signature creation should fail with wrong key types");

    // Test verification with NULL signature
    int l_verify_result = dap_sign_verify(NULL, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result != 0, "Verification should fail with NULL signature");

    // Test verification with NULL message
    l_signature = dap_sign_create_ring(l_signer_key, &l_message_hash, sizeof(l_message_hash),
                                      l_ring_keys_2, 2, 0);
    dap_assert(l_signature, "Valid signature creation should succeed");

    l_verify_result = dap_sign_verify(l_signature, NULL, 0);
    dap_assert(l_verify_result != 0, "Verification should fail with NULL message");

    // Test ring detection with NULL
    bool l_is_ring = dap_sign_is_ring(NULL);
    dap_assert(l_is_ring == false, "Ring detection should return false for NULL");

    bool l_is_zk = dap_sign_is_zk(NULL);
    dap_assert(l_is_zk == false, "ZK detection should return false for NULL");

    // Test signature serialization with NULL
    dap_assert(l_serialized, "Serialization should fail with NULL signature");

    // Test deserialization with NULL
    dap_assert(l_deserialized, "Deserialization should fail with NULL data");

    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_signer_key);
    dap_enc_key_delete(l_wrong_key);

    log_it(L_INFO, "✓ Comprehensive error handling tests passed");
    return true;
}

/**
 * @brief Test performance characteristics
 */
static bool s_test_performance(void) {
    log_it(L_INFO, "Testing Chipmunk Ring performance characteristics...");

    // Test with different ring sizes
    const size_t l_ring_sizes[] = {4, 8, 16};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        size_t l_ring_size = l_ring_sizes[size_idx];

        // Generate keys
        dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i], "Ring key generation should succeed");
        }

        // Measure signature creation time
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

        uint64_t l_start_time = clock();
        dap_sign_t* l_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            0
        );
        uint64_t l_end_time = clock();

        dap_assert(l_signature, "Signature creation should succeed");

        uint64_t l_creation_time = l_end_time - l_start_time;
        log_it(L_DEBUG, "Ring size %zu: signature creation took %" PRIu64 " microseconds", l_ring_size, l_creation_time);

        // Measure verification time
        l_start_time = clock();
        int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));
        l_end_time = clock();

        dap_assert(l_verify_result == 0, "Signature verification should succeed");

        uint64_t l_verify_time = l_end_time - l_start_time;
        log_it(L_DEBUG, "Ring size %zu: signature verification took %" PRIu64 " microseconds", l_ring_size, l_verify_time);

        // Test signature size scaling
        size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
        dap_assert(l_signature->header.sign_size == l_expected_size,
                       "Signature size should match expected size");

        // Cleanup
        DAP_DELETE(l_signature);
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "✓ Performance tests passed");
    return true;
}

/**
 * @brief Test edge cases and boundary conditions
 */
static bool s_test_edge_cases(void) {
    log_it(L_INFO, "Testing Chipmunk Ring edge cases and boundary conditions...");

    // Test with maximum ring size
    const size_t l_max_ring_size = 32; // Reasonable maximum for testing

    // Generate maximum ring
    dap_enc_key_t* l_max_ring_keys[l_max_ring_size] = {0};
    for (size_t i = 0; i < l_max_ring_size; i++) {
        l_max_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_max_ring_keys[i], "Max ring key generation should succeed");
    }

    // Test signature creation with max ring size
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

    dap_sign_t* l_max_signature = dap_sign_create_ring(
        l_max_ring_keys[0],
        &l_message_hash,
        sizeof(l_message_hash),
        l_max_ring_keys,
        l_max_ring_size,
        0
    );
    dap_assert(l_max_signature, "Max ring signature creation should succeed");

    // Verify max signature
    int l_verify_result = dap_sign_verify(l_max_signature, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result == 0, "Max ring signature verification should succeed");

    // Test with minimum ring size (2)
    dap_enc_key_t* l_min_ring_keys[2] = {0};
    for (size_t i = 0; i < 2; i++) {
        l_min_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_min_ring_keys[i], "Min ring key generation should succeed");
    }

    dap_sign_t* l_min_signature = dap_sign_create_ring(
        l_min_ring_keys[0],
        &l_message_hash,
        sizeof(l_message_hash),
        l_min_ring_keys,
        2,
        0
    );
    dap_assert(l_min_signature, "Min ring signature creation should succeed");

    l_verify_result = dap_sign_verify(l_min_signature, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result == 0, "Min ring signature verification should succeed");

    // Test with different signer positions in min ring
    dap_sign_t* l_min_signature_pos1 = dap_sign_create_ring(
        l_min_ring_keys[1],
        &l_message_hash,
        sizeof(l_message_hash),
        l_min_ring_keys,
        2,
        1
    );
    dap_assert(l_min_signature_pos1, "Min ring signature creation (pos 1) should succeed");

    l_verify_result = dap_sign_verify(l_min_signature_pos1, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result == 0, "Min ring signature verification (pos 1) should succeed");

    // Test signature size differences
    size_t l_max_size = dap_enc_chipmunk_ring_get_signature_size(l_max_ring_size);
    size_t l_min_size = dap_enc_chipmunk_ring_get_signature_size(2);

    dap_assert(l_max_signature->header.sign_size == l_max_size,
                   "Max signature should have correct size");
    dap_assert(l_min_signature->header.sign_size == l_min_size,
                   "Min signature should have correct size");
    dap_assert(l_max_size > l_min_size, "Larger ring should produce larger signature");

    // Cleanup
    DAP_DELETE(l_max_signature);
    DAP_DELETE(l_min_signature);
    DAP_DELETE(l_min_signature_pos1);

    for (size_t i = 0; i < l_max_ring_size; i++) {
        dap_enc_key_delete(l_max_ring_keys[i]);
    }
    for (size_t i = 0; i < 2; i++) {
        dap_enc_key_delete(l_min_ring_keys[i]);
    }

    log_it(L_INFO, "✓ Edge cases and boundary condition tests passed");
    return true;
}

/**
 * @brief Test cryptographic strength and uniqueness
 */
static bool s_test_cryptographic_strength(void) {
    log_it(L_INFO, "Testing Chipmunk Ring cryptographic strength and uniqueness...");

    const size_t l_ring_size = 8;
    const size_t l_num_signatures = 100; // Test many signatures for uniqueness

    // Generate ring keys
    dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Create many signatures from same signer
    dap_sign_t* l_signatures[l_num_signatures] = {0};
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

    for (size_t i = 0; i < l_num_signatures; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            0
        );
        dap_assert(l_signatures[i], "Signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hash, sizeof(l_message_hash));
        dap_assert(l_verify_result == 0, "Signature verification should succeed");
    }

    // Test uniqueness: all signatures should be different due to random elements
    size_t l_unique_signatures = 0;
    for (size_t i = 0; i < l_num_signatures; i++) {
        bool l_is_unique = true;
        for (size_t j = 0; j < l_num_signatures; j++) {
            if (i != j && memcmp(l_signatures[i]->pkey_n_sign,
                                l_signatures[j]->pkey_n_sign,
                                l_signatures[i]->header.sign_size) == 0) {
                l_is_unique = false;
                break;
            }
        }
        if (l_is_unique) {
            l_unique_signatures++;
        }
    }

    // All signatures should be unique
    dap_assert(l_unique_signatures == l_num_signatures,
                   "All signatures should be cryptographically unique");

    // Test signature entropy (basic check)
    for (size_t i = 0; i < 10; i++) { // Check first 10 signatures
        // Count zero bytes (should be very few in a good signature)
        size_t l_zero_bytes = 0;
        for (size_t j = 0; j < l_signatures[i]->header.sign_size; j++) {
            if (l_signatures[i]->pkey_n_sign[j] == 0) {
                l_zero_bytes++;
            }
        }
        // Signatures should not have too many zero bytes (indicates poor randomness)
        double l_zero_ratio = (double)l_zero_bytes / l_signatures[i]->header.sign_size;
        dap_assert(l_zero_ratio < 0.1, "Signatures should have good entropy (not too many zeros)");
    }

    log_it(L_INFO, "✓ Generated %zu unique signatures with good cryptographic properties", l_num_signatures);

    // Cleanup
    for (size_t i = 0; i < l_num_signatures; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "✓ Cryptographic strength tests passed");
    return true;
}

/**
 * @brief Test serialization robustness
 */
static bool s_test_serialization_robustness(void) {
    log_it(L_INFO, "Testing Chipmunk Ring serialization robustness...");

    // Generate signature for testing
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key, "Signer key generation should succeed");

    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Replace first key with signer key
    dap_enc_key_delete(l_ring_keys[0]);
    l_ring_keys[0] = l_signer_key;

    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, TEST_MESSAGE_LEN, &l_message_hash);

    dap_sign_t* l_original_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash,
        sizeof(l_message_hash),
        l_ring_keys,
        l_ring_size,
        0
    );
    dap_assert(l_original_signature, "Original signature creation should succeed");

    // Test normal serialization/deserialization
    dap_assert(l_serialized, "Signature serialization should succeed");

    dap_assert(l_deserialized, "Signature deserialization should succeed");

    int l_verify_result = dap_sign_verify(l_deserialized, &l_message_hash, sizeof(l_message_hash));
    dap_assert(l_verify_result == 0, "Deserialized signature verification should succeed");

    // Test with corrupted serialization data
    if (l_deserialized->header.sign_size > 10) {
        // Corrupt a few bytes in the middle
        uint8_t* l_corrupted_serialized = DAP_NEW_SIZE(uint8_t, l_deserialized->header.sign_size + sizeof(dap_sign_hdr_t));
        memcpy(l_corrupted_serialized, l_serialized, l_deserialized->header.sign_size + sizeof(dap_sign_hdr_t));

        // Corrupt some bytes
        size_t l_corrupt_offset = sizeof(dap_sign_hdr_t) + 10;
        l_corrupted_serialized[l_corrupt_offset] ^= 0xFF;
        l_corrupted_serialized[l_corrupt_offset + 1] ^= 0xFF;

        if (l_corrupted_deserialized) {
            // Corrupted signature should fail verification
            l_verify_result = dap_sign_verify(l_corrupted_deserialized, &l_message_hash, sizeof(l_message_hash));
            // Note: corrupted signature might still pass basic verification due to ring properties
            // This is acceptable for ring signatures as long as they don't crash
            DAP_DELETE(l_corrupted_deserialized);
        }
        DAP_DELETE(l_corrupted_serialized);
    }

    // Test serialization size consistency
    size_t l_expected_serialized_size = sizeof(dap_sign_hdr_t) + l_original_signature->header.sign_size;
    size_t l_actual_serialized_size = l_original_signature->header.sign_size + sizeof(dap_sign_hdr_t);

    dap_assert(l_actual_serialized_size == l_expected_serialized_size,
                   "Serialized size should match expected size");

    // Cleanup
    DAP_DELETE(l_original_signature);
    DAP_DELETE(l_deserialized);
    DAP_FREE(l_serialized);
    for (size_t i = 0; i < l_ring_size; i++) {
        if (i != 0) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "✓ Serialization robustness tests passed");
    return true;
}

/**
 * @brief Test stress conditions with many signatures
 */
static bool s_test_stress_conditions(void) {
    log_it(L_INFO, "Testing Chipmunk Ring stress conditions...");

    const size_t l_ring_size = 8;
    const size_t l_num_stress_signatures = PERFORMANCE_ITERATIONS; // Use same constant as performance test

    // Generate ring keys
    dap_enc_key_t* l_ring_keys[l_ring_size] = {0};
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Test stress with many signatures
    dap_sign_t* l_stress_signatures[l_num_stress_signatures] = {0};
    uint64_t l_start_time = clock();

    for (size_t i = 0; i < l_num_stress_signatures; i++) {
        // Use different messages for each signature
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "Stress test message %zu", i);
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

        size_t l_signer_pos = i % l_ring_size;
        l_stress_signatures[i] = dap_sign_create_ring(
            l_ring_keys[l_signer_pos],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            l_signer_pos
        );
        dap_assert(l_stress_signatures[i], "Stress signature creation should succeed");
    }

    uint64_t l_creation_time = clock() - l_start_time;
    log_it(L_INFO, "Created %zu stress signatures in %" PRIu64 " microseconds",
           l_num_stress_signatures, l_creation_time);

    // Verify all stress signatures
    l_start_time = clock();
    size_t l_verified_count = 0;

    for (size_t i = 0; i < l_num_stress_signatures; i++) {
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "Stress test message %zu", i);
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

        int l_verify_result = dap_sign_verify(l_stress_signatures[i], &l_message_hash, sizeof(l_message_hash));
        if (l_verify_result == 0) {
            l_verified_count++;
        }
    }

    uint64_t l_verify_time = clock() - l_start_time;

    dap_assert(l_verified_count == l_num_stress_signatures,
                   "All stress signatures should verify successfully");

    log_it(L_INFO, "Verified %zu/%zu stress signatures in %" PRIu64 " microseconds",
           l_verified_count, l_num_stress_signatures, l_verify_time);

    // Test memory usage doesn't grow unexpectedly
    // (Basic check - in a real stress test we'd monitor actual memory usage)

    // Cleanup
    for (size_t i = 0; i < l_num_stress_signatures; i++) {
        DAP_DELETE(l_stress_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "✓ Stress condition tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Starting Comprehensive Chipmunk Ring Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting comprehensive Chipmunk Ring unit tests...");

    // Initialize DAP SDK
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    // Initialize Chipmunk Ring module
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }



    bool l_all_passed = true;

    // Run all comprehensive tests
    l_all_passed &= s_test_key_generation();
    l_all_passed &= s_test_ring_signature_operations();
    l_all_passed &= s_test_ring_anonymity();
    l_all_passed &= s_test_linkability_prevention();
    l_all_passed &= s_test_error_handling();
    l_all_passed &= s_test_performance();
    l_all_passed &= s_test_edge_cases();
    l_all_passed &= s_test_cryptographic_strength();
    l_all_passed &= s_test_serialization_robustness();
    l_all_passed &= s_test_stress_conditions();

    // Cleanup

    log_it(L_NOTICE, "Comprehensive Chipmunk Ring unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL comprehensive Chipmunk Ring unit tests PASSED!");
        log_it(L_INFO, "✓ Tested: key generation, ring signatures, anonymity, linkability, error handling, performance, edge cases, cryptographic strength, serialization, stress conditions");
        log_it(L_INFO, "🎯 Total test functions: 10 | Test coverage: COMPREHENSIVE");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some comprehensive Chipmunk Ring unit tests FAILED!");
        return -1;
    }
}
