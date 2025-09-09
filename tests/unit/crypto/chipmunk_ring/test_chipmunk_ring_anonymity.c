#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"
#include <dap_math_mod.h>

#define LOG_TAG "test_chipmunk_ring_anonymity"

// Test constants
#define TEST_RING_SIZE 8
#define TEST_MESSAGE "Chipmunk Ring Signature Anonymity Test"
#define POSITIONS_TO_TEST 3

/**
 * @brief Test ring anonymity - signatures from different positions should be different
 */
static bool s_test_ring_anonymity(void) {
    log_it(L_INFO, "Testing Chipmunk Ring anonymity properties...");

    // Generate signer key
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");

    // Generate ring keys
    dap_enc_key_t* l_ring_keys[TEST_RING_SIZE];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test different signer positions
    dap_sign_t* l_signatures[POSITIONS_TO_TEST];
    memset(l_signatures, 0, sizeof(l_signatures));
    size_t l_positions[POSITIONS_TO_TEST] = {0, 2, TEST_RING_SIZE - 1};

    for (size_t i = 0; i < POSITIONS_TO_TEST; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_signer_key,
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, TEST_RING_SIZE,
            l_positions[i]
        );
        dap_assert(l_signatures[i] != NULL, "Ring signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, TEST_RING_SIZE);
        dap_assert(l_verify_result == 0, "Ring signature verification should succeed");
    }

    // Signatures should have the same size
    for (size_t i = 1; i < POSITIONS_TO_TEST; i++) {
        dap_assert(l_signatures[0]->header.sign_size == l_signatures[i]->header.sign_size,
                       "All signatures should have the same size");
    }

    // Signatures should be different (due to different signer positions)
    for (size_t i = 0; i < POSITIONS_TO_TEST - 1; i++) {
        for (size_t j = i + 1; j < POSITIONS_TO_TEST; j++) {
            dap_assert(memcmp(l_signatures[i]->pkey_n_sign, l_signatures[j]->pkey_n_sign,
                              l_signatures[i]->header.sign_size) != 0,
                           "Signatures from different positions should be different");
        }
    }

    // Test that all signatures are properly typed
    for (size_t i = 0; i < POSITIONS_TO_TEST; i++) {
        dap_assert(l_signatures[i]->header.type.type == SIG_TYPE_CHIPMUNK_RING,
                       "All signatures should be CHIPMUNK_RING type");

        bool l_is_ring = dap_sign_is_ring(l_signatures[i]);
        dap_assert(l_is_ring == true, "All should be detected as ring signatures");

        bool l_is_zk = dap_sign_is_zk(l_signatures[i]);
        dap_assert(l_is_zk == true, "All should be detected as ZKP");
    }

    // Cleanup
    for (size_t i = 0; i < POSITIONS_TO_TEST; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    dap_enc_key_delete(l_signer_key);
    for (size_t i = 0; i < TEST_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Ring anonymity test passed");
    return true;
}

/**
 * @brief Test linkability prevention - signatures from same signer should be different
 */
static bool s_test_linkability_prevention(void) {
    log_it(L_INFO, "Testing Chipmunk Ring linkability prevention...");

    // Generate signer key
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(l_signer_key != NULL, "Signer key generation should succeed");

    // Generate ring keys
    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Create multiple signatures from same signer
    const size_t l_num_attempts = 5;
    dap_sign_t* l_signatures[l_num_attempts];
    memset(l_signatures, 0, sizeof(l_signatures));

    for (size_t i = 0; i < l_num_attempts; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_signer_key,
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, l_ring_size,
            0  // Same position
        );
        dap_assert(l_signatures[i] != NULL, "Ring signature creation should succeed");

        // All signatures should be valid
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, TEST_RING_SIZE);
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

    // Cleanup
    for (size_t i = 0; i < l_num_attempts; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    dap_enc_key_delete(l_signer_key);
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Linkability prevention test passed");
    return true;
}

/**
 * @brief Test cryptographic strength
 */
static bool s_test_cryptographic_strength(void) {
    log_it(L_INFO, "Testing Chipmunk Ring cryptographic strength...");

    // Generate keys
    const size_t l_ring_size = 4;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Create multiple signatures
    const size_t l_num_signatures = 10;
    dap_sign_t* l_signatures[l_num_signatures];
    memset(l_signatures, 0, sizeof(l_signatures));

    for (size_t i = 0; i < l_num_signatures; i++) {
        l_signatures[i] = dap_sign_create_ring(
            l_ring_keys[0],  // Same signer
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, l_ring_size,
            0
        );
        dap_assert(l_signatures[i] != NULL, "Signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify(l_signatures[i], &l_message_hash, sizeof(l_message_hash));
        dap_assert(l_verify_result == 0, "Signature verification should succeed");
    }

    // All signatures should be unique
    size_t l_unique_signatures = 0;
    bool l_is_unique[l_num_signatures];
    memset(l_is_unique, false, sizeof(l_is_unique));

    for (size_t i = 0; i < l_num_signatures; i++) {
        bool l_is_unique_sig = true;
        for (size_t j = 0; j < l_num_signatures; j++) {
            if (i != j && !l_is_unique[j] &&
                memcmp(l_signatures[i]->pkey_n_sign, l_signatures[j]->pkey_n_sign,
                       l_signatures[i]->header.sign_size) == 0) {
                l_is_unique_sig = false;
                break;
            }
        }
        if (l_is_unique_sig) {
            l_is_unique[i] = true;
            l_unique_signatures++;
        }
    }

    dap_assert(l_unique_signatures == l_num_signatures,
                   "All signatures should be cryptographically unique");

    // Check entropy (signatures should not have too many zero bytes)
    for (size_t i = 0; i < l_num_signatures; i++) {
        size_t l_zero_bytes = 0;
        for (size_t j = 0; j < l_signatures[i]->header.sign_size; j++) {
            if (l_signatures[i]->pkey_n_sign[j] == 0) {
                l_zero_bytes++;
            }
        }
        double l_zero_ratio = (double)l_zero_bytes / l_signatures[i]->header.sign_size;
        dap_assert(l_zero_ratio < 0.1, "Signatures should have good entropy (not too many zeros)");
    }

    // Cleanup
    for (size_t i = 0; i < l_num_signatures; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Cryptographic strength test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting Chipmunk Ring anonymity tests...");

    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }

    if (dap_math_mod_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP math module");
        return -1;
    }

    bool l_all_passed = true;
    l_all_passed &= s_test_ring_anonymity();
    l_all_passed &= s_test_linkability_prevention();
    l_all_passed &= s_test_cryptographic_strength();

    log_it(L_NOTICE, "Chipmunk Ring anonymity tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All anonymity tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some anonymity tests FAILED");
        return -1;
    }
}
