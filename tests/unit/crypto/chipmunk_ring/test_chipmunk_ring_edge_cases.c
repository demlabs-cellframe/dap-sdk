#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"

#define LOG_TAG "test_chipmunk_ring_edge_cases"

// Test constants
#define MAX_RING_SIZE 64
#define MIN_RING_SIZE 2
#define TEST_MESSAGE "Chipmunk Ring Signature Edge Cases Test"

/**
 * @brief Test minimum and maximum ring sizes
 */
static bool s_test_ring_size_limits(void) {
    log_it(L_INFO, "Testing Chipmunk Ring size limits...");

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test minimum ring size (2)
    {
        log_it(L_DEBUG, "Testing minimum ring size (2)");
        dap_enc_key_t* l_min_ring_keys[MIN_RING_SIZE];
        memset(l_min_ring_keys, 0, sizeof(l_min_ring_keys));
        for (size_t i = 0; i < MIN_RING_SIZE; i++) {
            l_min_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
            dap_assert(l_min_ring_keys[i] != NULL, "Min ring key generation should succeed");
        }

        dap_sign_t* l_min_signature = dap_sign_create_ring(
            l_min_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_min_ring_keys, MIN_RING_SIZE
        );
        dap_assert(l_min_signature != NULL, "Min ring signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_min_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_min_ring_keys, MIN_RING_SIZE);
        dap_assert(l_verify_result == 0, "Min ring signature verification should succeed");

        // Test different signer positions in min ring
        dap_sign_t* l_min_signature_pos1 = dap_sign_create_ring(
            l_min_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_min_ring_keys, MIN_RING_SIZE,
            1
        );
        dap_assert(l_min_signature_pos1 != NULL, "Min ring signature creation (pos 1) should succeed");

        l_verify_result = dap_sign_verify_ring(l_min_signature_pos1, &l_message_hash, sizeof(l_message_hash),
                                              l_min_ring_keys, MIN_RING_SIZE);
        dap_assert(l_verify_result == 0, "Min ring signature verification (pos 1) should succeed");

        // Cleanup
        DAP_DELETE(l_min_signature);
        DAP_DELETE(l_min_signature_pos1);
        for (size_t i = 0; i < MIN_RING_SIZE; i++) {
            dap_enc_key_delete(l_min_ring_keys[i]);
        }
    }

    // Test maximum ring size
    {
        log_it(L_DEBUG, "Testing maximum ring size (%d)", MAX_RING_SIZE);
        dap_enc_key_t* l_max_ring_keys[MAX_RING_SIZE];
        memset(l_max_ring_keys, 0, sizeof(l_max_ring_keys));
        for (size_t i = 0; i < MAX_RING_SIZE; i++) {
            l_max_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
            dap_assert(l_max_ring_keys[i] != NULL, "Max ring key generation should succeed");
        }

        dap_sign_t* l_max_signature = dap_sign_create_ring(
            l_max_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_max_ring_keys, MAX_RING_SIZE,
            0
        );
        dap_assert(l_max_signature != NULL, "Max ring signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_max_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_max_ring_keys, MAX_RING_SIZE);
        dap_assert(l_verify_result == 0, "Max ring signature verification should succeed");

        // Cleanup
        DAP_DELETE(l_max_signature);
        for (size_t i = 0; i < MAX_RING_SIZE; i++) {
            dap_enc_key_delete(l_max_ring_keys[i]);
        }
    }

    // Test signature size differences
    {
        log_it(L_DEBUG, "Testing signature size differences");
        // Create signatures with different sizes
        dap_enc_key_t* l_ring_keys[MAX_RING_SIZE];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        for (size_t i = 0; i < MAX_RING_SIZE; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }

        // Test with max ring size
        dap_sign_t* l_max_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, MAX_RING_SIZE,
            0
        );
        dap_assert(l_max_signature != NULL, "Max ring signature creation should succeed");

        // Test with min ring size (reuse first 2 keys)
        dap_sign_t* l_min_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, MIN_RING_SIZE,
            0
        );
        dap_assert(l_min_signature != NULL, "Min ring signature creation should succeed");

        // Check sizes
        size_t l_max_size = dap_enc_chipmunk_ring_get_signature_size(MAX_RING_SIZE);
        size_t l_min_size = dap_enc_chipmunk_ring_get_signature_size(MIN_RING_SIZE);

        dap_assert(l_max_signature->header.sign_size == l_max_size,
                       "Max signature should have correct size");
        dap_assert(l_min_signature->header.sign_size == l_min_size,
                       "Min signature should have correct size");
        dap_assert(l_max_size > l_min_size, "Larger ring should produce larger signature");

        // Cleanup
        DAP_DELETE(l_max_signature);
        DAP_DELETE(l_min_signature);
        for (size_t i = 0; i < MAX_RING_SIZE; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "Ring size limits test passed");
    return true;
}

/**
 * @brief Test edge cases with invalid inputs
 */
static bool s_test_invalid_inputs(void) {
    log_it(L_INFO, "Testing Chipmunk Ring invalid inputs...");

    // Test ring size too small
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");

    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test with ring size 1 (should fail)
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        &l_signer_key, 1,
        0
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with ring size 1");

    // Test with ring size 0 (should fail)
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        NULL, 0,
        0
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with ring size 0");

    // Test with invalid signer index
    dap_enc_key_t* l_ring_keys[3] = {l_signer_key, l_signer_key, l_signer_key};
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, 3,
        10  // Invalid index
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with invalid signer index");

    // Test with negative signer index (size_t overflow)
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, 3,
        (size_t)-1  // Invalid index
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with negative signer index");

    // Cleanup
    dap_enc_key_delete(l_signer_key);

    log_it(L_INFO, "Invalid inputs test passed");
    return true;
}

/**
 * @brief Test with empty/null messages
 */
static bool s_test_empty_messages(void) {
    log_it(L_INFO, "Testing Chipmunk Ring with empty/null messages...");

    // Generate keys
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");

    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Test with empty message
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_signer_key,
        NULL, 0,
        l_ring_keys, l_ring_size,
        0
    );
    dap_assert(l_signature != NULL, "Signature creation should succeed with empty message");

    int l_verify_result = dap_sign_verify_ring(l_signature, NULL, 0, l_ring_keys, l_ring_size);
    dap_assert(l_verify_result == 0, "Signature verification should succeed with empty message");

    // Cleanup
    DAP_DELETE(l_signature);
    dap_enc_key_delete(l_signer_key);
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Empty messages test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting Chipmunk Ring edge cases tests...");

    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }


    bool l_all_passed = true;
    l_all_passed &= s_test_ring_size_limits();
    l_all_passed &= s_test_invalid_inputs();
    l_all_passed &= s_test_empty_messages();

    log_it(L_NOTICE, "Chipmunk Ring edge cases tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All edge cases tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some edge cases tests FAILED");
        return -1;
    }
}
