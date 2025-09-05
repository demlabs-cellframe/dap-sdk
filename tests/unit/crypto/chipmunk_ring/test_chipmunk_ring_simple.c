/*
 * DAP SDK - Simple Chipmunk Ring Test
 * Copyright (c) 2025 DeM Labs
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"
#include <dap_math_mod.h>

#define LOG_TAG "test_chipmunk_ring"

// Test constants
#define TEST_RING_SIZE 3
#define TEST_MESSAGE "Test message for Chipmunk Ring signature"
#define TEST_MESSAGE_SIZE (sizeof(TEST_MESSAGE) - 1)

/**
 * @brief Test basic Chipmunk Ring functionality
 */
static bool s_test_basic_functionality(void) {
    log_it(L_INFO, "Testing basic Chipmunk Ring functionality...");

    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring");
        return false;
    }

    if (dap_math_mod_init() != 0) {
        log_it(L_ERROR, "Failed to initialize math mod");
        return false;
    }

    // Generate signer key
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(
        DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");
    dap_assert(l_signer_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING,
               "Key type should be CHIPMUNK_RING");

    // Generate ring keys
    dap_enc_key_t* l_ring_keys[TEST_RING_SIZE];
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(
            DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Create ring signature
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_signer_key, TEST_MESSAGE, TEST_MESSAGE_SIZE,
        l_ring_keys, TEST_RING_SIZE, 0); // signer is at index 0

    dap_assert(l_signature != NULL, "Ring signature creation should succeed");

    // Verify signature
    int l_verify_result = dap_sign_verify(l_signature, TEST_MESSAGE, TEST_MESSAGE_SIZE);
    dap_assert(l_verify_result == 0, "Ring signature verification should succeed");

    // Test anonymity - create signature with different signer
    dap_sign_t* l_signature2 = dap_sign_create_ring(
        l_ring_keys[1], TEST_MESSAGE, TEST_MESSAGE_SIZE,
        l_ring_keys, TEST_RING_SIZE, 1); // signer is at index 1

    dap_assert(l_signature2 != NULL, "Second ring signature creation should succeed");

    // Verify second signature
    l_verify_result = dap_sign_verify(l_signature2, TEST_MESSAGE, TEST_MESSAGE_SIZE);
    dap_assert(l_verify_result == 0, "Second ring signature verification should succeed");

    // Signatures should be different (anonymity)
    dap_assert(memcmp(l_signature->pkey_n_sign, l_signature2->pkey_n_sign,
                      l_signature->header.sign_size) != 0,
               "Signatures from different signers should be different");

    // Cleanup
    DAP_FREE(l_signature);
    DAP_FREE(l_signature2);
    dap_enc_key_delete(l_signer_key);
    DAP_FREE(l_signer_key);

    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
        DAP_FREE(l_ring_keys[i]);
    }

    dap_pass_msg("Basic Chipmunk Ring functionality test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char *argv[]) {
    UNUSED(argc);
    UNUSED(argv);

    // Initialize logging
    dap_test_logging_init();

    log_it(L_NOTICE, "Starting Chipmunk Ring simple unit tests");

    bool l_all_passed = true;

    l_all_passed &= s_test_basic_functionality();

    log_it(L_NOTICE, "Chipmunk Ring simple unit tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All tests PASSED");
        dap_test_logging_restore();
        return 0;
    } else {
        log_it(L_ERROR, "Some tests FAILED");
        dap_test_logging_restore();
        return -1;
    }
}
