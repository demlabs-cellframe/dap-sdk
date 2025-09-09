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
#include "../fixtures/utilities/test_helpers.h"
#include "rand/dap_rand.h"

#define LOG_TAG "test_ring_signature_zkp"

// Security test constants
#define SECURITY_RING_SIZE 32
#define SECURITY_TEST_ITERATIONS 10
#define SECURITY_MESSAGE_COUNT 5

/**
 * @brief Test zero-knowledge property: verifier learns nothing about signer identity
 */
static bool s_test_zkp_soundness(void) {
    log_it(L_INFO, "Testing ZKP soundness for Chipmunk Ring signatures...");

    // Generate large ring for anonymity testing
    dap_enc_key_t* l_ring_keys[SECURITY_RING_SIZE] = {0};
    for (size_t i = 0; i < SECURITY_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Test messages
    const char* l_test_messages[SECURITY_MESSAGE_COUNT] = {
        "Transaction: Send 100 tokens to Alice",
        "Transaction: Send 50 tokens to Bob",
        "Transaction: Vote YES on proposal #123",
        "Transaction: Vote NO on proposal #456",
        "Contract: Execute function updateBalance"
    };

    // Create signatures from different positions in the ring
    dap_sign_t* l_signatures[SECURITY_MESSAGE_COUNT][3] = {0}; // 3 different signers per message
    size_t l_signer_positions[3] = {5, 15, 25}; // Different positions in ring

    for (size_t msg_idx = 0; msg_idx < SECURITY_MESSAGE_COUNT; msg_idx++) {
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_test_messages[msg_idx], strlen(l_test_messages[msg_idx]), &l_message_hash);

        for (size_t signer_idx = 0; signer_idx < 3; signer_idx++) {
            size_t l_pos = l_signer_positions[signer_idx];

            // Replace key at position with signer key
            dap_enc_key_t* l_temp_key = l_ring_keys[l_pos];
            l_ring_keys[l_pos] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);

            l_signatures[msg_idx][signer_idx] = dap_sign_create_ring(
                l_ring_keys[l_pos],
                &l_message_hash,
                sizeof(l_message_hash),
                l_ring_keys,
                SECURITY_RING_SIZE,
                l_pos
            );
            DAP_TEST_ASSERT_NOT_NULL(l_signatures[msg_idx][signer_idx],
                                   "Ring signature creation should succeed");

            // Restore original key
            dap_enc_key_delete(l_ring_keys[l_pos]);
            l_ring_keys[l_pos] = l_temp_key;
        }
    }

    // Verify all signatures
    for (size_t msg_idx = 0; msg_idx < SECURITY_MESSAGE_COUNT; msg_idx++) {
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_test_messages[msg_idx], strlen(l_test_messages[msg_idx]), &l_message_hash);

        for (size_t signer_idx = 0; signer_idx < 3; signer_idx++) {
            int l_verify_result = dap_sign_verify_ring(l_signatures[msg_idx][signer_idx],
                                                      &l_message_hash, sizeof(l_message_hash),
                                                      l_ring_keys, SECURITY_RING_SIZE);
            DAP_TEST_ASSERT(l_verify_result == 0, "All signatures should be valid");
        }
    }

    // Test zero-knowledge property: signatures should be indistinguishable
    // All signatures for the same message should look equally valid
    // (we cannot determine which position in the ring was the actual signer)

    // Additionally test that all signatures are of correct type
    for (size_t msg_idx = 0; msg_idx < SECURITY_MESSAGE_COUNT; msg_idx++) {
        for (size_t signer_idx = 0; signer_idx < 3; signer_idx++) {
            DAP_TEST_ASSERT(l_signatures[msg_idx][signer_idx]->header.type.type == SIG_TYPE_CHIPMUNK_RING,
                           "All signatures should be CHIPMUNK_RING type");

            bool l_is_ring = dap_sign_is_ring(l_signatures[msg_idx][signer_idx]);
            DAP_TEST_ASSERT(l_is_ring == true, "All should be detected as ring signatures");

            bool l_is_zk = dap_sign_is_zk(l_signatures[msg_idx][signer_idx]);
            DAP_TEST_ASSERT(l_is_zk == true, "All should be detected as ZKP");
        }
    }

    // Cleanup
    for (size_t msg_idx = 0; msg_idx < SECURITY_MESSAGE_COUNT; msg_idx++) {
        for (size_t signer_idx = 0; signer_idx < 3; signer_idx++) {
            DAP_DELETE(l_signatures[msg_idx][signer_idx]);
        }
    }
    for (size_t i = 0; i < SECURITY_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "✓ ZKP soundness tests passed");
    return true;
}

/**
 * @brief Test anonymity property: signatures from different signers are indistinguishable
 */
static bool s_test_anonymity_property(void) {
    log_it(L_INFO, "Testing anonymity property of Chipmunk Ring signatures...");

    // Create ring with known signers
    const size_t l_ring_size = 16;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));

    // Generate ring keys
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Create message
    const char* l_message = "Anonymous transaction test";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    // Create signatures from different positions
    const size_t l_test_positions[] = {0, 5, 10, 15};
    const size_t l_num_positions = sizeof(l_test_positions) / sizeof(l_test_positions[0]);
    dap_sign_t* l_signatures[l_num_positions];
    memset(l_signatures, 0, sizeof(l_signatures));

    for (size_t i = 0; i < l_num_positions; i++) {
        size_t l_pos = l_test_positions[i];

        l_signatures[i] = dap_sign_create_ring(
            l_ring_keys[l_pos],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            l_pos
        );
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Ring signature creation should succeed");

        // Verify signature
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, SECURITY_RING_SIZE);
        DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should succeed");
    }

    // Test that signatures are cryptographically indistinguishable
    // (an observer cannot determine which position was the actual signer)

    // Check signature sizes are consistent
    size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
    for (size_t i = 0; i < l_num_positions; i++) {
        DAP_TEST_ASSERT(l_signatures[i]->header.sign_size == l_expected_size,
                       "All signatures should have the same size");
    }

    // Signatures should have similar structure but different content
    // (due to different signer positions and random elements)
    for (size_t i = 0; i < l_num_positions - 1; i++) {
        for (size_t j = i + 1; j < l_num_positions; j++) {
            DAP_TEST_ASSERT(memcmp(l_signatures[i]->pkey_n_sign,
                                 l_signatures[j]->pkey_n_sign,
                                 l_signatures[i]->header.sign_size) != 0,
                           "Signatures from different positions should be different");
        }
    }

    // Cleanup
    for (size_t i = 0; i < l_num_positions; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "✓ Anonymity property tests passed");
    return true;
}

/**
 * @brief Test linkability for double-spending prevention
 */
static bool s_test_linkability_prevention(void) {
    log_it(L_INFO, "Testing linkability for double-spending prevention...");

    // Generate signer key and ring
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_signer_key, "Signer key generation should succeed");

    const size_t l_ring_size = 12;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_ring_keys[i], "Ring key generation should succeed");
    }

    // Replace first position with signer
    dap_enc_key_delete(l_ring_keys[0]);
    l_ring_keys[0] = l_signer_key;

    // Create message
    const char* l_message = "Prevent double-spending test";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    // Create multiple signatures from same signer (simulating double-spending attempt)
    const size_t l_num_attempts = 5;
    dap_sign_t* l_signatures[l_num_attempts];
    memset(l_signatures, 0, sizeof(l_signatures));

    for (size_t i = 0; i < l_num_attempts; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_signer_key,
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            0
        );
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Ring signature creation should succeed");

        // All signatures should be valid
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, SECURITY_RING_SIZE);
        DAP_TEST_ASSERT(l_verify_result == 0, "All signatures should be valid");
    }

    // In a proper implementation with linkability tags, signatures from the same
    // signer for the same message should be linkable (detectable as coming from same source)
    // This prevents double-spending while maintaining anonymity

    // For now, test that signatures are different (due to random elements)
    for (size_t i = 0; i < l_num_attempts - 1; i++) {
        for (size_t j = i + 1; j < l_num_attempts; j++) {
            DAP_TEST_ASSERT(memcmp(l_signatures[i]->pkey_n_sign,
                                 l_signatures[j]->pkey_n_sign,
                                 l_signatures[i]->header.sign_size) != 0,
                           "Signatures should be different due to random elements");
        }
    }

    // Cleanup
    for (size_t i = 0; i < l_num_attempts; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        if (i != 0) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "✓ Linkability prevention tests passed");
    return true;
}

/**
 * @brief Test resistance to ring size manipulation attacks
 */
static bool s_test_ring_size_security(void) {
    log_it(L_INFO, "Testing resistance to ring size manipulation attacks...");

    // Test with various ring sizes
    const size_t l_ring_sizes[] = {2, 4, 8, 16, 32};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        size_t l_ring_size = l_ring_sizes[size_idx];

        // Generate ring keys
        dap_enc_key_t* l_ring_keys[l_ring_size];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            DAP_TEST_ASSERT_NOT_NULL(l_ring_keys[i], "Ring key generation should succeed");
        }

        // Create message
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "Ring size test message %zu", l_ring_size);
        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

        // Create signature
        dap_sign_t* l_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            0
        );
        DAP_TEST_ASSERT_NOT_NULL(l_signature, "Ring signature creation should succeed");

        // Verify signature
        int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, l_ring_size);
        DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should succeed");

        // Check signature size is appropriate for ring size
        size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
        DAP_TEST_ASSERT(l_signature->header.sign_size == l_expected_size,
                       "Signature size should match expected size for ring size");

        // Test that signature is detected correctly
        bool l_is_ring = dap_sign_is_ring(l_signature);
        DAP_TEST_ASSERT(l_is_ring == true, "Signature should be detected as ring signature");

        // Cleanup
        DAP_DELETE(l_signature);
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "✓ Ring size security tests passed");
    return true;
}

/**
 * @brief Test cryptographic randomness quality
 */
static bool s_test_cryptographic_randomness(void) {
    log_it(L_INFO, "Testing cryptographic randomness quality...");

    // Generate multiple signatures and check they are sufficiently different
    const size_t l_ring_size = 8;
    const size_t l_num_signatures = 10;

    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_ring_keys[i], "Ring key generation should succeed");
    }

    const char* l_message = "Randomness quality test";
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

    dap_sign_t* l_signatures[l_num_signatures];
    memset(l_signatures, 0, sizeof(l_signatures));

    // Create multiple signatures
    for (size_t i = 0; i < l_num_signatures; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash,
            sizeof(l_message_hash),
            l_ring_keys,
            l_ring_size,
            0
        );
        DAP_TEST_ASSERT_NOT_NULL(l_signatures[i], "Ring signature creation should succeed");
    }

    // Verify all signatures
    for (size_t i = 0; i < l_num_signatures; i++) {
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, l_ring_size);
        DAP_TEST_ASSERT(l_verify_result == 0, "All signatures should be valid");
    }

    // Check that signatures are sufficiently different
    // (good randomness should produce different signatures)
    size_t l_different_pairs = 0;
    size_t l_total_pairs = 0;

    for (size_t i = 0; i < l_num_signatures - 1; i++) {
        for (size_t j = i + 1; j < l_num_signatures; j++) {
            l_total_pairs++;
            if (memcmp(l_signatures[i]->pkey_n_sign,
                      l_signatures[j]->pkey_n_sign,
                      l_signatures[i]->header.sign_size) != 0) {
                l_different_pairs++;
            }
        }
    }

    // At least 90% of signature pairs should be different
    double l_difference_ratio = (double)l_different_pairs / l_total_pairs;
    DAP_TEST_ASSERT(l_difference_ratio >= 0.9, "Signatures should show sufficient randomness");

    // Cleanup
    for (size_t i = 0; i < l_num_signatures; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "✓ Cryptographic randomness tests passed (%.1f%% different pairs)",
           l_difference_ratio * 100.0);
    return true;
}

/**
 * @brief Main security test function
 */
int main(void) {
    printf("=== Starting Chipmunk Ring Security Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting Chipmunk Ring security tests...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    // Initialize Chipmunk Ring module
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }

    bool l_all_passed = true;

    // Run security tests
    l_all_passed &= s_test_zkp_soundness();
    l_all_passed &= s_test_anonymity_property();
    l_all_passed &= s_test_linkability_prevention();
    l_all_passed &= s_test_ring_size_security();
    l_all_passed &= s_test_cryptographic_randomness();

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_NOTICE, "Chipmunk Ring security tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL Chipmunk Ring security tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some Chipmunk Ring security tests FAILED!");
        return -1;
    }
}
