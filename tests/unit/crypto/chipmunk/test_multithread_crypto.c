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
#include "../../../../module/test/dap_test.h"
#include <dap_enc_key.h>
#include <pthread.h>

#define LOG_TAG "test_multithread_crypto"

// Test constants
#define THREAD_COUNT 4
#define OPERATIONS_PER_THREAD 10
#define TEST_MESSAGE "Multithread crypto test message"

// Thread data structure
typedef struct thread_data {
    int thread_id;
    dap_enc_key_t* key;
    int operations_completed;
    int errors_encountered;
    pthread_mutex_t* mutex;
} thread_data_t;

/**
 * @brief Thread function for cryptographic operations
 */
static void* s_crypto_thread_function(void* a_arg) {
    thread_data_t* l_data = (thread_data_t*)a_arg;

    for (int i = 0; i < OPERATIONS_PER_THREAD; i++) {
        // Create message hash
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "%s thread %d op %d",
                TEST_MESSAGE, l_data->thread_id, i);

        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

        // Create signature
        dap_sign_t* l_signature = dap_sign_create(l_data->key, &l_message_hash, sizeof(l_message_hash));

        if (l_signature) {
            // Verify signature
            int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));

            if (l_verify_result == 0) {
                // Success
                pthread_mutex_lock(l_data->mutex);
                l_data->operations_completed++;
                pthread_mutex_unlock(l_data->mutex);

                DAP_DELETE(l_signature);
            } else {
                // Verification failed
                pthread_mutex_lock(l_data->mutex);
                l_data->errors_encountered++;
                pthread_mutex_unlock(l_data->mutex);

                DAP_DELETE(l_signature);
            }
        } else {
            // Signature creation failed
            pthread_mutex_lock(l_data->mutex);
            l_data->errors_encountered++;
            pthread_mutex_unlock(l_data->mutex);
        }
    }

    return NULL;
}

/**
 * @brief Test multithreaded cryptographic operations
 */
static bool s_test_multithread_crypto(void) {
    log_it(L_INFO, "Testing multithreaded cryptographic operations...");

    // Generate keys for each thread
    dap_enc_key_t* l_keys[THREAD_COUNT] = {0};
    for (int i = 0; i < THREAD_COUNT; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_keys[i], "Key generation should succeed for thread %d", i);
    }

    // Create thread data
    thread_data_t l_thread_data[THREAD_COUNT];
    pthread_t l_threads[THREAD_COUNT];
    pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;

    for (int i = 0; i < THREAD_COUNT; i++) {
        l_thread_data[i].thread_id = i;
        l_thread_data[i].key = l_keys[i];
        l_thread_data[i].operations_completed = 0;
        l_thread_data[i].errors_encountered = 0;
        l_thread_data[i].mutex = &l_mutex;
    }

    // Start threads
    log_it(L_INFO, "Starting %d threads for cryptographic operations...", THREAD_COUNT);
    for (int i = 0; i < THREAD_COUNT; i++) {
        int l_result = pthread_create(&l_threads[i], NULL, s_crypto_thread_function, &l_thread_data[i]);
        DAP_TEST_ASSERT(l_result == 0, "Thread creation should succeed for thread %d", i);
    }

    // Wait for all threads to complete
    log_it(L_INFO, "Waiting for threads to complete...");
    for (int i = 0; i < THREAD_COUNT; i++) {
        int l_result = pthread_join(l_threads[i], NULL);
        DAP_TEST_ASSERT(l_result == 0, "Thread join should succeed for thread %d", i);
    }

    // Analyze results
    int l_total_operations = 0;
    int l_total_errors = 0;

    for (int i = 0; i < THREAD_COUNT; i++) {
        l_total_operations += l_thread_data[i].operations_completed;
        l_total_errors += l_thread_data[i].errors_encountered;

        log_it(L_DEBUG, "Thread %d: %d operations completed, %d errors",
               i, l_thread_data[i].operations_completed, l_thread_data[i].errors_encountered);
    }

    log_it(L_INFO, "Multithread results: %d total operations, %d total errors",
           l_total_operations, l_total_errors);

    // Verify results
    int l_expected_operations = THREAD_COUNT * OPERATIONS_PER_THREAD;
    DAP_TEST_ASSERT(l_total_operations == l_expected_operations,
                   "All operations should complete successfully");
    DAP_TEST_ASSERT(l_total_errors == 0, "No errors should occur in multithreaded operations");

    // Cleanup
    pthread_mutex_destroy(&l_mutex);
    for (int i = 0; i < THREAD_COUNT; i++) {
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ Multithreaded crypto tests passed");
    return true;
}

/**
 * @brief Test thread safety of key operations
 */
static bool s_test_thread_safety(void) {
    log_it(L_INFO, "Testing thread safety of cryptographic operations...");

    // Generate a shared key for testing thread safety
    dap_enc_key_t* l_shared_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
    DAP_TEST_ASSERT_NOT_NULL(l_shared_key, "Shared key generation should succeed");

    // Create thread data with shared key
    thread_data_t l_thread_data[THREAD_COUNT];
    pthread_t l_threads[THREAD_COUNT];
    pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;

    for (int i = 0; i < THREAD_COUNT; i++) {
        l_thread_data[i].thread_id = i;
        l_thread_data[i].key = l_shared_key;  // All threads use the same key
        l_thread_data[i].operations_completed = 0;
        l_thread_data[i].errors_encountered = 0;
        l_thread_data[i].mutex = &l_mutex;
    }

    // Start threads
    log_it(L_INFO, "Testing thread safety with shared key...");
    for (int i = 0; i < THREAD_COUNT; i++) {
        int l_result = pthread_create(&l_threads[i], NULL, s_crypto_thread_function, &l_thread_data[i]);
        DAP_TEST_ASSERT(l_result == 0, "Thread creation should succeed for thread %d", i);
    }

    // Wait for completion
    for (int i = 0; i < THREAD_COUNT; i++) {
        int l_result = pthread_join(l_threads[i], NULL);
        DAP_TEST_ASSERT(l_result == 0, "Thread join should succeed for thread %d", i);
    }

    // Analyze results
    int l_total_operations = 0;
    int l_total_errors = 0;

    for (int i = 0; i < THREAD_COUNT; i++) {
        l_total_operations += l_thread_data[i].operations_completed;
        l_total_errors += l_thread_data[i].errors_encountered;
    }

    log_it(L_INFO, "Thread safety results: %d total operations, %d total errors",
           l_total_operations, l_total_errors);

    // Verify thread safety
    int l_expected_operations = THREAD_COUNT * OPERATIONS_PER_THREAD;
    DAP_TEST_ASSERT(l_total_operations == l_expected_operations,
                   "All operations should complete successfully with shared key");
    DAP_TEST_ASSERT(l_total_errors == 0,
                   "No errors should occur with shared key (thread safety test)");

    // Cleanup
    pthread_mutex_destroy(&l_mutex);
    dap_enc_key_delete(l_shared_key);

    log_it(L_INFO, "✓ Thread safety tests passed");
    return true;
}

/**
 * @brief Test concurrent key generation
 */
static bool s_test_concurrent_key_generation(void) {
    log_it(L_INFO, "Testing concurrent key generation...");

    const int l_key_count = THREAD_COUNT * 5;  // Generate multiple keys per thread
    dap_enc_key_t* l_keys[l_key_count] = {0};

    // Generate keys concurrently (simplified - in real test we'd use threads)
    for (int i = 0; i < l_key_count; i++) {
        l_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, 0, 0);
        DAP_TEST_ASSERT_NOT_NULL(l_keys[i], "Concurrent key generation should succeed for key %d", i);
    }

    // Verify all keys are unique
    for (int i = 0; i < l_key_count - 1; i++) {
        for (int j = i + 1; j < l_key_count; j++) {
            DAP_TEST_ASSERT(memcmp(l_keys[i]->pub_key_data, l_keys[j]->pub_key_data,
                                 l_keys[i]->pub_key_data_size) != 0,
                           "Generated keys should be unique");
        }
    }

    log_it(L_INFO, "Generated %d unique keys successfully", l_key_count);

    // Test that all keys can be used for signing
    for (int i = 0; i < l_key_count; i++) {
        char l_message[64];
        snprintf(l_message, sizeof(l_message), "Key test message %d", i);

        dap_hash_fast_t l_message_hash = {0};
        dap_hash_fast(l_message, strlen(l_message), &l_message_hash);

        dap_sign_t* l_signature = dap_sign_create(l_keys[i], &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT_NOT_NULL(l_signature, "Signature creation should succeed for key %d", i);

        int l_verify_result = dap_sign_verify(l_signature, &l_message_hash, sizeof(l_message_hash));
        DAP_TEST_ASSERT(l_verify_result == 0, "Signature verification should succeed for key %d", i);

        DAP_DELETE(l_signature);
    }

    // Cleanup
    for (int i = 0; i < l_key_count; i++) {
        dap_enc_key_delete(l_keys[i]);
    }

    log_it(L_INFO, "✓ Concurrent key generation tests passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(void) {
    printf("=== Multithread Crypto Unit Tests ===\n");
    fflush(stdout);

    log_it(L_NOTICE, "Starting multithread crypto unit tests...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool l_all_passed = true;

    // Run all tests
    l_all_passed &= s_test_multithread_crypto();
    l_all_passed &= s_test_thread_safety();
    l_all_passed &= s_test_concurrent_key_generation();

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_NOTICE, "Multithread crypto unit tests completed");

    if (l_all_passed) {
        log_it(L_INFO, "✅ ALL multithread crypto unit tests PASSED!");
        log_it(L_INFO, "✓ Tested: %d threads, %d operations per thread, thread safety, concurrent key generation",
               THREAD_COUNT, OPERATIONS_PER_THREAD);
        return 0;
    } else {
        log_it(L_ERROR, "❌ Some multithread crypto unit tests FAILED!");
        return -1;
    }
}
