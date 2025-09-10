#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"
#include <sys/time.h>

#define LOG_TAG "test_chipmunk_ring_performance"

// Test constants
#define TEST_MESSAGE "ChipmunkRing Performance Benchmark - Post-Quantum Ring Signature"
#define PERFORMANCE_RING_SIZES {2, 4, 8, 16, 32, 64}
#define PERFORMANCE_ITERATIONS 50

// Performance measurement utilities
static double get_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
}

// Structure to store performance results
typedef struct {
    size_t ring_size;
    size_t pub_key_size;
    size_t priv_key_size;
    size_t signature_size;
    double avg_signing_time;
    double avg_verification_time;
} performance_result_t;

// Global results storage
static performance_result_t g_performance_results[10];
static size_t g_results_count = 0;

/**
 * @brief Comprehensive performance benchmark with detailed metrics
 */
static bool s_test_performance_detailed(void) {
    log_it(L_INFO, "=== CHIPMUNKRING PERFORMANCE BENCHMARK ===");
    log_it(L_INFO, "Generating detailed metrics for scientific paper...");

    // Define ring sizes to test
    const size_t l_ring_sizes[] = PERFORMANCE_RING_SIZES;
    const size_t l_num_sizes = sizeof(l_ring_sizes) / sizeof(l_ring_sizes[0]);

    // Array to store results
    performance_result_t l_results[l_num_sizes];

    // Hash the test message
    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    for (size_t size_idx = 0; size_idx < l_num_sizes; size_idx++) {
        const size_t l_ring_size = l_ring_sizes[size_idx];

        // Generate keys for this ring size
        dap_enc_key_t* l_ring_keys[l_ring_size];
        memset(l_ring_keys, 0, sizeof(l_ring_keys));
        
        double l_key_generation_start = get_time_ms();
        for (size_t i = 0; i < l_ring_size; i++) {
            l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
            dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
        }
        double l_key_generation_time = get_time_ms() - l_key_generation_start;

        // Get key sizes (all keys should be the same size)
        size_t l_pub_key_size = l_ring_keys[0]->pub_key_data_size;
        size_t l_priv_key_size = l_ring_keys[0]->priv_key_data_size;

        // Measure signature creation performance
        double l_total_signing_time = 0.0;
        double l_total_verification_time = 0.0;
        size_t l_signature_size = 0;

        for (size_t iter = 0; iter < PERFORMANCE_ITERATIONS; iter++) {
            // Measure signing time
            double l_sign_start = get_time_ms();
            dap_sign_t* l_signature = dap_sign_create_ring(
                l_ring_keys[iter % l_ring_size],  // Anonymous signer
                &l_message_hash, sizeof(l_message_hash),
                l_ring_keys, l_ring_size
            );
            double l_sign_end = get_time_ms();
            
            dap_assert(l_signature != NULL, "Signature creation should succeed");
            l_signature_size = l_signature->header.sign_size;
            l_total_signing_time += (l_sign_end - l_sign_start);

            // Measure verification time
            double l_verify_start = get_time_ms();
            int l_verify_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                                      l_ring_keys, l_ring_size);
            double l_verify_end = get_time_ms();
            
            dap_assert(l_verify_result == 0, "Signature verification should succeed");
            l_total_verification_time += (l_verify_end - l_verify_start);

            DAP_DELETE(l_signature);
        }

        // Calculate averages
        double l_avg_signing_time = l_total_signing_time / PERFORMANCE_ITERATIONS;
        double l_avg_verification_time = l_total_verification_time / PERFORMANCE_ITERATIONS;

        // Store results in global array
        g_performance_results[g_results_count].ring_size = l_ring_size;
        g_performance_results[g_results_count].pub_key_size = l_pub_key_size;
        g_performance_results[g_results_count].priv_key_size = l_priv_key_size;
        g_performance_results[g_results_count].signature_size = l_signature_size;
        g_performance_results[g_results_count].avg_signing_time = l_avg_signing_time;
        g_performance_results[g_results_count].avg_verification_time = l_avg_verification_time;
        g_results_count++;

        log_it(L_DEBUG, "Completed ring size %zu: sign=%.1fms, verify=%.1fms, sig_size=%.1fKB",
               l_ring_size, l_avg_signing_time, l_avg_verification_time, l_signature_size / 1024.0);

        // Cleanup
        for (size_t i = 0; i < l_ring_size; i++) {
            dap_enc_key_delete(l_ring_keys[i]);
        }
    }

    return true;
}

/**
 * @brief Print final performance summary table
 */
static void s_print_final_performance_table(void) {
    log_it(L_INFO, " ");
    log_it(L_INFO, "╔════════════════════════════════════════════════════════════════╗");
    log_it(L_INFO, "║                 CHIPMUNKRING PERFORMANCE REPORT                ║");
    log_it(L_INFO, "╠════════════════════════════════════════════════════════════════╣");
    log_it(L_INFO, "║ Ring │ Pub Key │ Priv Key │ Signature │  Signing  │ Verif.     ║");
    log_it(L_INFO, "║ Size │  Size   │   Size   │   Size    │   Time    │  Time      ║");
    log_it(L_INFO, "╠══════╪═════════╪══════════╪═══════════╪═══════════╪════════════╣");

    for (size_t i = 0; i < g_results_count; i++) {
        log_it(L_INFO, "║ %4zu │ %5.1fKB │ %6.1fKB │ %7.1fKB │ %7.3fms │   %6.3fms ║",
               g_performance_results[i].ring_size,
               g_performance_results[i].pub_key_size / 1024.0,
               g_performance_results[i].priv_key_size / 1024.0,
               g_performance_results[i].signature_size / 1024.0,
               g_performance_results[i].avg_signing_time,
               g_performance_results[i].avg_verification_time);
    }

    log_it(L_INFO, "╚══════╧═════════╧══════════╧═══════════╧═══════════╧════════════╝");
    log_it(L_INFO, " ");
    log_it(L_INFO, "PERFORMANCE SUMMARY:");
    log_it(L_INFO, "- Iterations per ring size: %d", PERFORMANCE_ITERATIONS);
    log_it(L_INFO, "- Message size: %zu bytes", strlen(TEST_MESSAGE));
    //log_it(L_INFO, "- Security level: 112-bit post-quantum");
    log_it(L_INFO, "- Algorithm: Chipmunk signature ringed with Fiat-Shamir");
    log_it(L_INFO, " ");
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
            l_ring_keys, l_ring_size
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
    l_all_passed &= s_test_performance_detailed();
    l_all_passed &= s_test_size_scaling();

    // Print final performance table after all tests
    s_print_final_performance_table();

    log_it(L_NOTICE, "Chipmunk Ring performance tests completed");

    if (l_all_passed) {
        log_it(L_NOTICE, "All performance tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some performance tests FAILED");
        return -1;
    }
}
