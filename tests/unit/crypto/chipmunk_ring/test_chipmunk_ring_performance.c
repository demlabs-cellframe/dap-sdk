#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"

#define LOG_TAG "test_chipmunk_ring_performance"

// Test constants
#define TEST_MESSAGE "Chipmunk Ring Signature Performance Test"
#define PERFORMANCE_RING_SIZES {2, 4, 8, 16, 32}
#define PERFORMANCE_ITERATIONS 10

/**
 * @brief Test signature creation and verification performance
 */
static bool s_test_performance(void) {
    log_it(L_INFO, "Testing Chipmunk Ring performance...");

    // Define ring sizes to test
    const size_t l_ring_sizes[] = PERFORMANCE_RING_SIZES;
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        const size_t l_ring_size = l_ring_sizes[size_idx];
        log_it(L_DEBUG, "Testing ring size %zu", l_ring_size);

        // Generate keys for this ring size
        dap_enc_key_t* l_ring_keys[l_ring_size];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }

        // Measure signature creation time
        uint64_t l_total_creation_time = 0;
        for (size_t iter = 0; iter < PERFORMANCE_ITERATIONS; iter++) {
            uint64_t l_start_time = clock();
            dap_sign_t* l_signature = dap_sign_create_ring(
                l_ring_keys[0],
                &l_message_hash, sizeof(l_message_hash),
                l_ring_keys, l_ring_size,
                0
            );
            uint64_t l_end_time = clock();

            dap_assert(l_signature != NULL, "Signature creation should succeed");

            l_total_creation_time += (l_end_time - l_start_time);

            // Measure verification time
            uint64_t l_start_verify = clock();
            int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                                      l_ring_keys, l_ring_size);
            uint64_t l_end_verify = clock();

            dap_assert(l_verify_result == 0, "Signature verification should succeed");

            uint64_t l_verify_time = l_end_verify - l_start_verify;

            // Log individual iteration performance
            log_it(L_DEBUG, "Ring size %zu, iteration %zu: creation=%" PRIu64 "us, verify=%" PRIu64 "us",
                   l_ring_size, iter, (l_end_time - l_start_time), l_verify_time);

            DAP_DELETE(l_signature);
        }

        uint64_t l_avg_creation_time = l_total_creation_time / PERFORMANCE_ITERATIONS;

        // Test signature size scaling
        size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
        dap_sign_t* l_test_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, l_ring_size,
            0
        );
        dap_assert(l_test_signature != NULL, "Test signature creation should succeed");
        dap_assert(l_test_signature->header.sign_size == l_expected_size,
                       "Signature size should match expected size");

        log_it(L_INFO, "Ring size %zu: avg creation time %" PRIu64 " microseconds, signature size %zu bytes",
               l_ring_size, l_avg_creation_time, l_expected_size);

        // Cleanup
        DAP_DELETE(l_test_signature);
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "Performance test passed");
    return true;
}

/**
 * @brief Test signature size scaling
 */
static bool s_test_size_scaling(void) {
    log_it(L_INFO, "Testing Chipmunk Ring signature size scaling...");

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    // Test different ring sizes
    const size_t l_ring_sizes[] = {2, 4, 8, 16, 32, 64};
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    size_t l_prev_size = 0;

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        const size_t l_ring_size = l_ring_sizes[size_idx];

        // Generate keys
        dap_enc_key_t* l_ring_keys[l_ring_size];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }

        // Create signature
        dap_sign_t* l_signature = dap_sign_create_ring(
            l_ring_keys[0],
            &l_message_hash, sizeof(l_message_hash),
            l_ring_keys, l_ring_size,
            0
        );
        dap_assert(l_signature != NULL, "Signature creation should succeed");

        // Check size scaling
        size_t l_expected_size = dap_enc_chipmunk_ring_get_signature_size(l_ring_size);
        dap_assert(l_signature->header.sign_size == l_expected_size,
                       "Signature size should match expected size");

        if (l_prev_size > 0) {
            dap_assert(l_signature->header.sign_size > l_prev_size,
                           "Larger ring should produce larger signature");
        }

        log_it(L_DEBUG, "Ring size %zu: signature size %u bytes",
               l_ring_size, l_signature->header.sign_size);

        l_prev_size = l_signature->header.sign_size;

        // Cleanup
        DAP_DELETE(l_signature);
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    log_it(L_INFO, "Size scaling test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting Chipmunk Ring performance tests...");

    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }

    bool l_all_passed = true;
    l_all_passed &= s_test_performance();
    l_all_passed &= s_test_size_scaling();

    log_it(L_NOTICE, "Chipmunk Ring performance tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All performance tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some performance tests FAILED");
        return -1;
    }
}
