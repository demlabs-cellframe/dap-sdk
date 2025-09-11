#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"

#define LOG_TAG "test_chipmunk_ring_stress"

// Test constants
#define STRESS_RING_SIZE 16
#define STRESS_NUM_SIGNATURES 50
#define TEST_MESSAGE "Chipmunk Ring Signature Stress Test"

/**
 * @brief Test stress with many signatures
 */
static bool s_test_stress_signatures(void) {
    log_it(L_INFO, "Testing Chipmunk Ring stress with many signatures...");

    // Generate keys
    dap_enc_key_t* l_ring_keys[STRESS_RING_SIZE] = {0};
    for (size_t i = 0; i < STRESS_RING_SIZE; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test stress with many signatures
    dap_sign_t* l_stress_signatures[STRESS_NUM_SIGNATURES] = {0};
    uint64_t l_start_time = clock();

    for (size_t i = 0; i < STRESS_NUM_SIGNATURES; i++) {
        l_stress_signatures[i] = dap_sign_create_ring(
            l_ring_keys[i % STRESS_RING_SIZE],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, STRESS_RING_SIZE,
            1  // Traditional ring signature (required_signers=1)
        );
        dap_assert(l_stress_signatures[i] != NULL, "Stress signature creation should succeed");
    }

    uint64_t l_creation_time = clock() - l_start_time;
    log_it(L_INFO, "Created %d stress signatures in %" PRIu64 " microseconds",
           STRESS_NUM_SIGNATURES, l_creation_time);

    // Verify all stress signatures
    l_start_time = clock();
    size_t l_verified_count = 0;

    for (size_t i = 0; i < STRESS_NUM_SIGNATURES; i++) {
        int l_verify_result = dap_sign_verify_ring(l_stress_signatures[i], &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, STRESS_RING_SIZE);
        if (l_verify_result == 0) {
            l_verified_count++;
        }
    }

    uint64_t l_verify_time = clock() - l_start_time;
    log_it(L_INFO, "Verified %zu/%d stress signatures in %" PRIu64 " microseconds",
           l_verified_count, STRESS_NUM_SIGNATURES, l_verify_time);

    dap_assert(l_verified_count == STRESS_NUM_SIGNATURES,
                   "All stress signatures should verify successfully");

    // Cleanup
    for (size_t i = 0; i < STRESS_NUM_SIGNATURES; i++) {
        DAP_DELETE(l_stress_signatures[i]);
    }
    for (size_t i = 0; i < STRESS_RING_SIZE; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Stress signatures test passed");
    return true;
}

/**
 * @brief Test memory stress with large rings
 */
static bool s_test_memory_stress(void) {
    log_it(L_INFO, "Testing Chipmunk Ring memory stress with large rings...");

    // Test with progressively larger rings
    const size_t l_ring_sizes[] = {8, 16, 32, 48, 64};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        const size_t l_ring_size = l_ring_sizes[size_idx];
        log_it(L_DEBUG, "Testing memory stress with ring size %zu", l_ring_size);

        // Generate keys for this ring size
        dap_enc_key_t* l_ring_keys[l_ring_size];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }

        // Create and verify signature
        dap_sign_t* l_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, l_ring_size,
            1  // Traditional ring signature (required_signers=1)
        );
        dap_assert(l_signature != NULL, "Signature creation should succeed");

        int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                                  l_ring_keys, l_ring_size);
        dap_assert(l_verify_result == 0, "Signature verification should succeed");

        log_it(L_DEBUG, "Ring size %zu: signature size %u bytes",
               l_ring_size, l_signature->header.sign_size);

        // Cleanup
        DAP_DELETE(l_signature);
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "Memory stress test passed");
    return true;
}

/**
 * @brief Test concurrent operations (simulated)
 */
static bool s_test_concurrent_operations(void) {
    log_it(L_INFO, "Testing Chipmunk Ring concurrent operations simulation...");

    // Generate shared ring keys
    const size_t l_ring_size = 8;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    // Hash different messages
    const char* l_messages[] = {
        "Message 1", "Message 2", "Message 3", "Message 4", "Message 5"
    };
    const size_t l_num_messages = sizeof(l_messages) / sizeof(l_messages[0]);

    dap_hash_fast_t l_message_hashes[l_num_messages];
    for (size_t i = 0; i < l_num_messages; i++) {
        bool l_hash_result = dap_hash_fast(l_messages[i], strlen(l_messages[i]), &l_message_hashes[i]);
        dap_assert(l_hash_result == true, "Message hashing should succeed");
    }

    // Simulate concurrent operations
    const size_t l_num_operations = 20;
    dap_sign_t* l_signatures[l_num_operations];
    memset(l_signatures, 0, sizeof(l_signatures));

    for (size_t i = 0; i < l_num_operations; i++) {
        size_t l_signer_idx = i % l_ring_size;
        size_t l_msg_idx = i % l_num_messages;

        l_signatures[i] = dap_sign_create_ring(
            l_ring_keys[l_signer_idx],
            &l_message_hashes[l_msg_idx], sizeof(dap_hash_fast_t),
            l_ring_keys, l_ring_size,
            1  // Traditional ring signature (required_signers=1)
        );
        dap_assert(l_signatures[i] != NULL, "Concurrent signature creation should succeed");

        // Verify each signature
        int l_verify_result = dap_sign_verify_ring(l_signatures[i], &l_message_hashes[l_msg_idx], sizeof(dap_hash_fast_t),
                                                  l_ring_keys, l_ring_size);
        dap_assert(l_verify_result == 0, "Concurrent signature verification should succeed");
    }

    // Verify all signatures are unique
    for (size_t i = 0; i < l_num_operations - 1; i++) {
        for (size_t j = i + 1; j < l_num_operations; j++) {
            // Signatures should be different (different messages or signers)
            bool l_same_signer = ((i % l_ring_size) == (j % l_ring_size));
            bool l_same_message = ((i % l_num_messages) == (j % l_num_messages));

            if (l_same_signer && l_same_message) {
                // Same signer and message should produce different signatures (linkability)
                dap_assert(memcmp(l_signatures[i]->pkey_n_sign, l_signatures[j]->pkey_n_sign,
                                  l_signatures[i]->header.sign_size) != 0,
                               "Same signer/message should produce different signatures");
            }
        }
    }

    // Cleanup
    for (size_t i = 0; i < l_num_operations; i++) {
        DAP_DELETE(l_signatures[i]);
    }
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Concurrent operations test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting Chipmunk Ring stress tests...");

    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }


    bool l_all_passed = true;
    l_all_passed &= s_test_stress_signatures();
    l_all_passed &= s_test_memory_stress();
    l_all_passed &= s_test_concurrent_operations();

    log_it(L_NOTICE, "Chipmunk Ring stress tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All stress tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some stress tests FAILED");
        return -1;
    }
}
