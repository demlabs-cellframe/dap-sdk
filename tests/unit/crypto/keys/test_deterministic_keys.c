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
#include <dap_sign.h>
#include <dap_hash.h>
#include "test_helpers.h"

#define LOG_TAG "test_deterministic_keys"

// Test constants
#define TEST_MESSAGE "Test message for deterministic keys"

/**
 * @brief Test deterministic key generation for regular Chipmunk
 */
static bool s_test_chipmunk_deterministic_keys(void) {
    log_it(L_INFO, "Testing Chipmunk deterministic key generation...");

    // Test seed
    uint8_t l_test_seed[32];
    for (int i = 0; i < 32; i++) {
        l_test_seed[i] = (uint8_t)(i + 1);  // 0x01, 0x02, ..., 0x20
    }

    // Generate two key pairs with the same seed
    dap_enc_key_t* l_key1 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_test_seed, sizeof(l_test_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key1, "First deterministic key generation should succeed");

    dap_enc_key_t* l_key2 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_test_seed, sizeof(l_test_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key2, "Second deterministic key generation should succeed");

    // Keys should be identical
    DAP_TEST_ASSERT(memcmp(l_key1->pub_key_data, l_key2->pub_key_data, l_key1->pub_key_data_size) == 0,
                   "Public keys from same seed should be identical");

    DAP_TEST_ASSERT(memcmp(l_key1->priv_key_data, l_key2->priv_key_data, l_key1->priv_key_data_size) == 0,
                   "Private keys from same seed should be identical");

    // Test signing with both keys
    dap_hash_fast_t l_message_hash;
    dap_hash_fast((const void*)TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    dap_sign_t* l_sig1 = dap_sign_create(l_key1, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_sig1, "First signature creation should succeed");

    dap_sign_t* l_sig2 = dap_sign_create(l_key2, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_sig2, "Second signature creation should succeed");

    // Both signatures should verify
    int l_verify1 = dap_sign_verify(l_sig1, &l_message_hash, sizeof(l_message_hash));
    int l_verify2 = dap_sign_verify(l_sig2, &l_message_hash, sizeof(l_message_hash));

    DAP_TEST_ASSERT(l_verify1 == 0, "First signature should verify");
    DAP_TEST_ASSERT(l_verify2 == 0, "Second signature should verify");

    // Test with different seed
    uint8_t l_different_seed[32];
    for (int i = 0; i < 32; i++) {
        l_different_seed[i] = (uint8_t)(i + 100);  // Different seed
    }

    dap_enc_key_t* l_key3 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_different_seed, sizeof(l_different_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key3, "Third key generation should succeed");

    // Keys should be different
    DAP_TEST_ASSERT(memcmp(l_key1->pub_key_data, l_key3->pub_key_data, l_key1->pub_key_data_size) != 0,
                   "Different seeds should produce different keys");

    // Cleanup
    DAP_DELETE(l_sig1);
    DAP_DELETE(l_sig2);
    dap_enc_key_delete(l_key1);
    dap_enc_key_delete(l_key2);
    dap_enc_key_delete(l_key3);

    log_it(L_INFO, "✓ Chipmunk deterministic key tests passed");
    return true;
}

/**
 * @brief Test deterministic key generation for Chipmunk Ring
 */
static bool s_test_chipmunk_ring_deterministic_keys(void) {
    log_it(L_INFO, "Testing Chipmunk Ring deterministic key generation...");

    // Test seed
    uint8_t l_test_seed[32];
    for (int i = 0; i < 32; i++) {
        l_test_seed[i] = (uint8_t)(i + 1);
    }

    // Generate two key pairs with the same seed
    dap_enc_key_t* l_key1 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, l_test_seed, sizeof(l_test_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key1, "First Ring deterministic key generation should succeed");

    dap_enc_key_t* l_key2 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, l_test_seed, sizeof(l_test_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key2, "Second Ring deterministic key generation should succeed");

    // Keys should be identical
    DAP_TEST_ASSERT(memcmp(l_key1->pub_key_data, l_key2->pub_key_data, l_key1->pub_key_data_size) == 0,
                   "Ring public keys from same seed should be identical");

    DAP_TEST_ASSERT(memcmp(l_key1->priv_key_data, l_key2->priv_key_data, l_key1->priv_key_data_size) == 0,
                   "Ring private keys from same seed should be identical");

    // Test ring signature creation
    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    l_ring_keys[0] = l_key1;
    l_ring_keys[1] = l_key2;
    l_ring_keys[2] = l_key1;
    l_ring_keys[3] = l_key2;

    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    dap_sign_t* l_ring_sig = dap_sign_create_ring(
        l_key1, &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size, 0
    );
    DAP_TEST_ASSERT_NOT_NULL(l_ring_sig, "Ring signature creation should succeed");

    // Verify ring signature
    int l_verify_result = dap_sign_verify(l_ring_sig, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_verify_result == 0, "Ring signature should verify");

    // Test with different seed
    uint8_t l_different_seed[32];
    for (int i = 0; i < 32; i++) {
        l_different_seed[i] = (uint8_t)(i + 200);
    }

    dap_enc_key_t* l_key4 = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, l_different_seed, sizeof(l_different_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_key4, "Fourth key generation should succeed");

    // Keys should be different
    DAP_TEST_ASSERT(memcmp(l_key1->pub_key_data, l_key4->pub_key_data, l_key1->pub_key_data_size) != 0,
                   "Ring keys from different seeds should be different");

    // Cleanup
    DAP_DELETE(l_ring_sig);
    dap_enc_key_delete(l_key1);
    dap_enc_key_delete(l_key2);
    dap_enc_key_delete(l_key4);

    log_it(L_INFO, "✓ Chipmunk Ring deterministic key tests passed");
    return true;
}

/**
 * @brief Test cross-compatibility between regular and ring keys
 */
static bool s_test_key_compatibility(void) {
    log_it(L_INFO, "Testing key compatibility between Chipmunk and Chipmunk Ring...");

    // Generate keys of both types with same seed
    uint8_t l_seed[32];
    for (int i = 0; i < 32; i++) {
        l_seed[i] = (uint8_t)(i + 50);
    }

    dap_enc_key_t* l_regular_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_seed, sizeof(l_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_regular_key, "Regular Chipmunk key generation should succeed");

    dap_enc_key_t* l_ring_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, l_seed, sizeof(l_seed), 0);
    DAP_TEST_ASSERT_NOT_NULL(l_ring_key, "Ring Chipmunk key generation should succeed");

    // Keys may be same or different (both use Chipmunk algorithm but different contexts)
    // The important thing is that both are generated successfully
    size_t l_min_size = l_regular_key->pub_key_data_size < l_ring_key->pub_key_data_size ?
                         l_regular_key->pub_key_data_size : l_ring_key->pub_key_data_size;
    bool l_keys_different = (memcmp(l_regular_key->pub_key_data, l_ring_key->pub_key_data, l_min_size) != 0);
    log_it(L_INFO, "Regular vs Ring keys: %s", l_keys_different ? "Different" : "Same");
    // Note: Both outcomes are valid since both use Chipmunk algorithm

    // Test signing with each type
    dap_hash_fast_t l_message_hash = {0};
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    // Regular signature
    dap_sign_t* l_regular_sig = dap_sign_create(l_regular_key, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT_NOT_NULL(l_regular_sig, "Regular signature creation should succeed");

    int l_regular_verify = dap_sign_verify(l_regular_sig, &l_message_hash, sizeof(l_message_hash));
    DAP_TEST_ASSERT(l_regular_verify == 0, "Regular signature should verify");

    // Ring signature
    dap_enc_key_t* l_ring_keys[2] = {l_ring_key, l_ring_key};
    dap_sign_t* l_ring_sig = dap_sign_create_ring(
        l_ring_key, &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, 2, 0
    );
    DAP_TEST_ASSERT_NOT_NULL(l_ring_sig, "Ring signature creation should succeed");

    int l_ring_verify = dap_sign_verify_ring(l_ring_sig, &l_message_hash, sizeof(l_message_hash),
                                            l_ring_keys, 2);
    DAP_TEST_ASSERT(l_ring_verify == 0, "Ring signature should verify");

    // Signatures should be of different types
    if (memcmp(&l_regular_sig->header, &l_ring_sig->header, sizeof(dap_sign_hdr_t)) == 0) {
        log_it(L_ERROR, "Regular and Ring signatures should have different headers");
        return false;
    }

    // Cleanup
    DAP_DELETE(l_regular_sig);
    DAP_DELETE(l_ring_sig);
    dap_enc_key_delete(l_regular_key);
    dap_enc_key_delete(l_ring_key);

    log_it(L_INFO, "✓ Key compatibility tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Deterministic Keys Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting deterministic keys unit tests...");

    // Initialize logging for tests
    dap_test_logging_init();

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_chipmunk_deterministic_keys();
    l_all_passed &= s_test_chipmunk_ring_deterministic_keys();
    l_all_passed &= s_test_key_compatibility();

    // Cleanup
    dap_test_logging_restore();

    log_it(L_NOTICE, "Deterministic keys unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL deterministic keys unit tests PASSED!");
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some deterministic keys unit tests FAILED!");
        return -1;
    }
}
