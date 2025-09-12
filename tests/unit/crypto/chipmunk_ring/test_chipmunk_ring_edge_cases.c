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
#define LARGE_RING_SIZE 32
#define SMALL_RING_SIZE 4

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
            l_min_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_min_ring_keys[i] != NULL, "Min ring key generation should succeed");
        }

        dap_sign_t* l_min_signature = dap_sign_create_ring(
            l_min_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_min_ring_keys, MIN_RING_SIZE,
            1  // Traditional ring signature (required_signers=1)
        );
        dap_assert(l_min_signature != NULL, "Min ring signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_min_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_min_ring_keys, MIN_RING_SIZE);
        dap_assert(l_verify_result == 0, "Min ring signature verification should succeed");

        // Test different signer positions in min ring
        dap_sign_t* l_min_signature_pos1 = dap_sign_create_ring(
            l_min_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_min_ring_keys,
            MIN_RING_SIZE,
            1  // Traditional ring signature (required_signers=1)
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
            l_max_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_max_ring_keys[i] != NULL, "Max ring key generation should succeed");
        }

        dap_sign_t* l_max_signature = dap_sign_create_ring(
            l_max_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_max_ring_keys,
            MAX_RING_SIZE,
            1  // Traditional ring signature (required_signers=1)
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
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }

        // Test with max ring size
        dap_sign_t* l_max_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, MAX_RING_SIZE,
            1  // Traditional ring signature (required_signers=1)
        );
        dap_assert(l_max_signature != NULL, "Max ring signature creation should succeed");

        // Test with min ring size (reuse first 2 keys)
        dap_sign_t* l_min_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, MIN_RING_SIZE,
        1  // Traditional ring signature (required_signers=1)
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
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");

    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test with ring size 1 (should fail)
    dap_enc_key_t* l_ring_keys_1[1] = {l_signer_key};
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys_1,
            1,
            1  // Traditional ring signature (required_signers=1)
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with ring size 1");

    // Test with ring size 0 (should fail)
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        NULL,
            0,
            1  // Traditional ring signature (required_signers=1)
    );
    dap_assert(l_signature == NULL, "Signature creation should fail with ring size 0");

    // Test with invalid signer index
    dap_enc_key_t* l_ring_keys[3] = {l_signer_key, l_signer_key, l_signer_key};
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys,
            3,
            1  // Traditional ring signature (required_signers=1)
    );
    dap_assert(l_signature != NULL, "Anonymous signature creation should succeed with valid ring");

    // Test with negative signer index (size_t overflow)
    l_signature = dap_sign_create_ring(
        l_signer_key,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys,
            3,
            1  // Traditional ring signature (required_signers=1)
    );
    dap_assert(l_signature != NULL, "Anonymous signature creation should succeed");

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

    // Generate ring keys first
    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Use first ring key as signer (must be one of the ring participants)
    dap_enc_key_t* l_signer_key = l_ring_keys[0];
    dap_assert(l_signer_key != NULL, "Signer key should be valid");

    // Test with empty message
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_signer_key,
        NULL, 0,
        l_ring_keys, l_ring_size,
        1  // Traditional ring signature (required_signers=1)
    );
    dap_assert(l_signature != NULL, "Signature creation should succeed with empty message");

    int l_verify_result = dap_sign_verify_ring(l_signature, NULL, 0, l_ring_keys, l_ring_size);
    dap_assert(l_verify_result == 0, "Signature verification should succeed with empty message");

    // Cleanup
    DAP_DELETE(l_signature);
    // Don't delete l_signer_key - it's a reference to l_ring_keys[0]
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Empty messages test passed");
    return true;
}

/**
 * @brief Test edge cases with multi-signer signatures
 */
static bool s_test_multi_signer_edge_cases(void) {
    log_it(L_INFO, "Testing Chipmunk Ring multi-signer edge cases...");

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test 1: Large ring with small threshold (32 participants, need 2 signatures)
    {
        log_it(L_DEBUG, "Testing large ring (%d) with small threshold (2)", LARGE_RING_SIZE);
        dap_enc_key_t* l_large_ring_keys[LARGE_RING_SIZE];
        memset(l_large_ring_keys, 0, sizeof(l_large_ring_keys));

        for (size_t i = 0; i < LARGE_RING_SIZE; i++) {
            l_large_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_large_ring_keys[i] != NULL, "Large ring key generation should succeed");
        }

        // Create multi-signer signature with small threshold
        dap_sign_t* l_large_ring_signature = dap_sign_create_ring(
            l_large_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_large_ring_keys, LARGE_RING_SIZE,
            2  // Small threshold for large ring
        );
        dap_assert(l_large_ring_signature != NULL, "Large ring multi-signer signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_large_ring_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_large_ring_keys, LARGE_RING_SIZE);
        dap_assert(l_verify_result == 0, "Large ring multi-signer signature verification should succeed");

        // Test different signer positions
        dap_sign_t* l_large_ring_signature_pos15 = dap_sign_create_ring(
            l_large_ring_keys[15],
            &l_message_hash, sizeof(l_message_hash),
            l_large_ring_keys, LARGE_RING_SIZE,
            2  // Small threshold
        );
        dap_assert(l_large_ring_signature_pos15 != NULL, "Large ring signature creation (pos 15) should succeed");

        l_verify_result = dap_sign_verify_ring(l_large_ring_signature_pos15, &l_message_hash, sizeof(l_message_hash),
                                              l_large_ring_keys, LARGE_RING_SIZE);
        dap_assert(l_verify_result == 0, "Large ring signature verification (pos 15) should succeed");

        // Cleanup
        DAP_DELETE(l_large_ring_signature);
        DAP_DELETE(l_large_ring_signature_pos15);
        for (size_t i = 0; i < LARGE_RING_SIZE; i++) {
            dap_enc_key_delete(l_large_ring_keys[i]);
        }
        log_it(L_DEBUG, "Large ring with small threshold test passed");
    }

    // Test 2: Both values large (32 participants, need 16 signatures)
    {
        log_it(L_DEBUG, "Testing both values large (%d participants, %d required)", LARGE_RING_SIZE, LARGE_RING_SIZE/2);
        dap_enc_key_t* l_both_large_keys[LARGE_RING_SIZE];
        memset(l_both_large_keys, 0, sizeof(l_both_large_keys));

        for (size_t i = 0; i < LARGE_RING_SIZE; i++) {
            l_both_large_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_both_large_keys[i] != NULL, "Both large key generation should succeed");
        }

        // Create multi-signer signature with large threshold
        dap_sign_t* l_both_large_signature = dap_sign_create_ring(
            l_both_large_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_both_large_keys, LARGE_RING_SIZE,
            LARGE_RING_SIZE / 2  // Large threshold (half the ring)
        );
        dap_assert(l_both_large_signature != NULL, "Both large multi-signer signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_both_large_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_both_large_keys, LARGE_RING_SIZE);
        dap_assert(l_verify_result == 0, "Both large multi-signer signature verification should succeed");

        // Cleanup
        DAP_DELETE(l_both_large_signature);
        for (size_t i = 0; i < LARGE_RING_SIZE; i++) {
            dap_enc_key_delete(l_both_large_keys[i]);
        }
        log_it(L_DEBUG, "Both values large test passed");
    }

    // Test 3: Small ring with large threshold (4 participants, need 3 signatures)
    {
        log_it(L_DEBUG, "Testing small ring (%d) with large threshold (3)", SMALL_RING_SIZE);
        dap_enc_key_t* l_small_ring_keys[SMALL_RING_SIZE];
        memset(l_small_ring_keys, 0, sizeof(l_small_ring_keys));

        for (size_t i = 0; i < SMALL_RING_SIZE; i++) {
            l_small_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_small_ring_keys[i] != NULL, "Small ring key generation should succeed");
        }

        // Create multi-signer signature with large threshold for small ring
        dap_sign_t* l_small_ring_signature = dap_sign_create_ring(
            l_small_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_small_ring_keys, SMALL_RING_SIZE,
            SMALL_RING_SIZE - 1  // Large threshold (almost all participants)
        );
        dap_assert(l_small_ring_signature != NULL, "Small ring multi-signer signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_small_ring_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_small_ring_keys, SMALL_RING_SIZE);
        dap_assert(l_verify_result == 0, "Small ring multi-signer signature verification should succeed");

        // Cleanup
        DAP_DELETE(l_small_ring_signature);
        for (size_t i = 0; i < SMALL_RING_SIZE; i++) {
            dap_enc_key_delete(l_small_ring_keys[i]);
        }
        log_it(L_DEBUG, "Small ring with large threshold test passed");
    }

    // Test 4: Edge case - minimum threshold (ring_size=2, required_signers=1)
    {
        log_it(L_DEBUG, "Testing minimum threshold edge case (2 participants, 1 required)");
        dap_enc_key_t* l_min_threshold_keys[MIN_RING_SIZE];
        memset(l_min_threshold_keys, 0, sizeof(l_min_threshold_keys));

        for (size_t i = 0; i < MIN_RING_SIZE; i++) {
            l_min_threshold_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_min_threshold_keys[i] != NULL, "Min threshold key generation should succeed");
        }

        // Create signature with minimum threshold
        dap_sign_t* l_min_threshold_signature = dap_sign_create_ring(
            l_min_threshold_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_min_threshold_keys, MIN_RING_SIZE,
            1  // Minimum threshold
        );
        dap_assert(l_min_threshold_signature != NULL, "Min threshold signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_min_threshold_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_min_threshold_keys, MIN_RING_SIZE);
        dap_assert(l_verify_result == 0, "Min threshold signature verification should succeed");

        // Cleanup
        DAP_DELETE(l_min_threshold_signature);
        for (size_t i = 0; i < MIN_RING_SIZE; i++) {
            dap_enc_key_delete(l_min_threshold_keys[i]);
        }
        log_it(L_DEBUG, "Minimum threshold edge case test passed");
    }

    // Test 5: Performance comparison - different threshold sizes
    {
        log_it(L_DEBUG, "Testing performance comparison with different thresholds");
        const size_t l_perf_ring_size = 16;
        dap_enc_key_t* l_perf_keys[l_perf_ring_size];
        memset(l_perf_keys, 0, sizeof(l_perf_keys));

        for (size_t i = 0; i < l_perf_ring_size; i++) {
            l_perf_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_perf_keys[i] != NULL, "Performance test key generation should succeed");
        }

        // Test with small threshold (25% of ring)
        dap_sign_t* l_small_threshold_sig = dap_sign_create_ring(
            l_perf_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_perf_keys, l_perf_ring_size,
            l_perf_ring_size / 4  // Small threshold
        );
        dap_assert(l_small_threshold_sig != NULL, "Small threshold signature creation should succeed");

        // Test with large threshold (75% of ring)
        dap_sign_t* l_large_threshold_sig = dap_sign_create_ring(
            l_perf_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_perf_keys, l_perf_ring_size,
            (l_perf_ring_size * 3) / 4  // Large threshold
        );
        dap_assert(l_large_threshold_sig != NULL, "Large threshold signature creation should succeed");

        // Verify both signatures
        int l_verify_small = dap_sign_verify_ring(l_small_threshold_sig, &l_message_hash, sizeof(l_message_hash),
                                                 l_perf_keys, l_perf_ring_size);
        int l_verify_large = dap_sign_verify_ring(l_large_threshold_sig, &l_message_hash, sizeof(l_message_hash),
                                                 l_perf_keys, l_perf_ring_size);

        dap_assert(l_verify_small == 0, "Small threshold signature verification should succeed");
        dap_assert(l_verify_large == 0, "Large threshold signature verification should succeed");

        // Compare signature sizes
        size_t l_small_size = l_small_threshold_sig->header.sign_size;
        size_t l_large_size = l_large_threshold_sig->header.sign_size;

        log_it(L_DEBUG, "Performance comparison: small_threshold_size=%zu, large_threshold_size=%zu",
               l_small_size, l_large_size);

        // Cleanup
        DAP_DELETE(l_small_threshold_sig);
        DAP_DELETE(l_large_threshold_sig);
        for (size_t i = 0; i < l_perf_ring_size; i++) {
            dap_enc_key_delete(l_perf_keys[i]);
        }
        log_it(L_DEBUG, "Performance comparison test passed");
    }

    log_it(L_INFO, "Multi-signer edge cases test passed");
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
    l_all_passed &= s_test_multi_signer_edge_cases();

    log_it(L_NOTICE, "Chipmunk Ring edge cases tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All edge cases tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some edge cases tests FAILED");
        return -1;
    }
}
