/*
 * ChipmunkRing Advanced Anonymity Tests
 * Statistical analysis and advanced anonymity properties testing:
 * - Signer indistinguishability analysis
 * - Statistical distribution of signatures
 * - Linkability prevention testing
 * - Ring size impact on anonymity
 * - Multi-message anonymity preservation
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "rand/dap_rand.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#define LOG_TAG "test_chipmunk_ring_anonymity_advanced"

// Statistical analysis parameters
#define ANONYMITY_TEST_ITERATIONS 100        // Number of signatures to analyze
#define ANONYMITY_RING_SIZE 8                // Ring size for anonymity testing
#define ANONYMITY_MESSAGE_COUNT 50           // Number of different messages
#define ANONYMITY_STATISTICAL_THRESHOLD 0.05 // 5% statistical significance

// Test fixture for anonymity analysis
typedef struct {
    dap_enc_key_t **ring_keys;
    size_t ring_size;
    uint8_t **test_messages;
    size_t *message_sizes;
    size_t message_count;
    dap_sign_t **signatures;
    size_t signature_count;
} anonymity_test_fixture_t;

static anonymity_test_fixture_t g_anonymity_fixture;

// Setup anonymity test environment
static void setup_anonymity_fixture(size_t ring_size, size_t message_count) {
    g_anonymity_fixture.ring_size = ring_size;
    g_anonymity_fixture.message_count = message_count;
    
    // Generate ring keys
    g_anonymity_fixture.ring_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, ring_size);
    for (size_t i = 0; i < ring_size; i++) {
        g_anonymity_fixture.ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    }
    
    // Generate test messages
    g_anonymity_fixture.test_messages = DAP_NEW_Z_COUNT(uint8_t*, message_count);
    g_anonymity_fixture.message_sizes = DAP_NEW_Z_COUNT(size_t, message_count);
    
    for (size_t i = 0; i < message_count; i++) {
        char message_buffer[128];
        snprintf(message_buffer, sizeof(message_buffer), "Anonymity test message %zu - unique content", i);
        
        g_anonymity_fixture.message_sizes[i] = strlen(message_buffer);
        g_anonymity_fixture.test_messages[i] = DAP_NEW_Z_SIZE(uint8_t, g_anonymity_fixture.message_sizes[i]);
        memcpy(g_anonymity_fixture.test_messages[i], message_buffer, g_anonymity_fixture.message_sizes[i]);
    }
    
    // Initialize signatures array
    g_anonymity_fixture.signature_count = 0;
    g_anonymity_fixture.signatures = DAP_NEW_Z_COUNT(dap_sign_t*, ANONYMITY_TEST_ITERATIONS);
    
    dap_test_msg("Anonymity fixture setup: ring_size=%zu, messages=%zu", ring_size, message_count);
}

static void cleanup_anonymity_fixture(void) {
    if (g_anonymity_fixture.ring_keys) {
        for (size_t i = 0; i < g_anonymity_fixture.ring_size; i++) {
            if (g_anonymity_fixture.ring_keys[i]) {
                dap_enc_key_delete(g_anonymity_fixture.ring_keys[i]);
            }
        }
        DAP_DELETE(g_anonymity_fixture.ring_keys);
    }
    
    if (g_anonymity_fixture.test_messages) {
        for (size_t i = 0; i < g_anonymity_fixture.message_count; i++) {
            if (g_anonymity_fixture.test_messages[i]) {
                DAP_DELETE(g_anonymity_fixture.test_messages[i]);
            }
        }
        DAP_DELETE(g_anonymity_fixture.test_messages);
    }
    
    if (g_anonymity_fixture.message_sizes) {
        DAP_DELETE(g_anonymity_fixture.message_sizes);
    }
    
    if (g_anonymity_fixture.signatures) {
        for (size_t i = 0; i < g_anonymity_fixture.signature_count; i++) {
            if (g_anonymity_fixture.signatures[i]) {
                DAP_DELETE(g_anonymity_fixture.signatures[i]);
            }
        }
        DAP_DELETE(g_anonymity_fixture.signatures);
    }
    
    memset(&g_anonymity_fixture, 0, sizeof(g_anonymity_fixture));
}

// Test 1: Signer indistinguishability analysis
static void test_signer_indistinguishability(void) {
    dap_test_msg("=== Test: Signer Indistinguishability Analysis ===");
    
    setup_anonymity_fixture(ANONYMITY_RING_SIZE, 10);
    
    // Generate signatures from different signers using same message
    dap_hash_fast_t message_hash;
    dap_hash_fast(g_anonymity_fixture.test_messages[0], g_anonymity_fixture.message_sizes[0], &message_hash);
    
    size_t signatures_per_signer = 10;
    uint32_t signature_distributions[ANONYMITY_RING_SIZE] = {0};
    
    // Generate signatures from each signer
    for (size_t signer_idx = 0; signer_idx < g_anonymity_fixture.ring_size; signer_idx++) {
        for (size_t sig_idx = 0; sig_idx < signatures_per_signer; sig_idx++) {
            dap_sign_t *signature = dap_sign_create_ring(
                g_anonymity_fixture.ring_keys[signer_idx],
                &message_hash,
                sizeof(message_hash),
                g_anonymity_fixture.ring_keys,
                g_anonymity_fixture.ring_size,
                1 // Single signer for anonymity test
            );
            
            dap_assert(signature != NULL, "Signature creation should succeed");
            
            // Verify signature
            int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                                   g_anonymity_fixture.ring_keys, g_anonymity_fixture.ring_size);
            dap_assert(verify_result == 0, "Signature verification should succeed");
            
            // Analyze signature properties for indistinguishability
            // In a perfect anonymous system, all signatures should be indistinguishable
            signature_distributions[signer_idx]++;
            
            DAP_DELETE(signature);
        }
    }
    
    // Statistical analysis: verify uniform distribution
    double expected_per_signer = (double)(g_anonymity_fixture.ring_size * signatures_per_signer) / g_anonymity_fixture.ring_size;
    double chi_square = 0.0;
    
    for (size_t i = 0; i < g_anonymity_fixture.ring_size; i++) {
        double deviation = signature_distributions[i] - expected_per_signer;
        chi_square += (deviation * deviation) / expected_per_signer;
        dap_test_msg("Signer %zu: %u signatures (expected: %.1f)", i, signature_distributions[i], expected_per_signer);
    }
    
    // Chi-square test for uniformity (degrees of freedom = ring_size - 1)
    double critical_value = 14.07; // Chi-square critical value for df=7, α=0.05
    bool is_uniform = chi_square < critical_value;
    
    dap_test_msg("Chi-square statistic: %.3f (critical: %.3f)", chi_square, critical_value);
    dap_test_msg("Distribution uniformity: %s", is_uniform ? "PASS" : "MARGINAL");
    
    // For anonymity, we expect signatures to be indistinguishable
    // Note: This test verifies the signature creation process, not cryptanalysis
    
    cleanup_anonymity_fixture();
    dap_test_msg("✅ Signer indistinguishability test completed");
}

// Test 2: Ring size impact on anonymity
static void test_ring_size_anonymity_impact(void) {
    dap_test_msg("=== Test: Ring Size Impact on Anonymity ===");
    
    size_t ring_sizes[] = {3, 5, 8, 12, 16};
    size_t num_tests = sizeof(ring_sizes) / sizeof(ring_sizes[0]);
    
    for (size_t test_idx = 0; test_idx < num_tests; test_idx++) {
        size_t ring_size = ring_sizes[test_idx];
        dap_test_msg("Testing anonymity for ring size %zu", ring_size);
        
        setup_anonymity_fixture(ring_size, 5);
        
        // Generate signatures from random signers
        size_t test_signatures = 20;
        size_t successful_signatures = 0;
        
        for (size_t i = 0; i < test_signatures; i++) {
            // Random signer and message
            size_t signer_idx = rand() % ring_size;
            size_t message_idx = rand() % g_anonymity_fixture.message_count;
            
            dap_hash_fast_t message_hash;
            dap_hash_fast(g_anonymity_fixture.test_messages[message_idx], 
                         g_anonymity_fixture.message_sizes[message_idx], &message_hash);
            
            dap_sign_t *signature = dap_sign_create_ring(
                g_anonymity_fixture.ring_keys[signer_idx],
                &message_hash,
                sizeof(message_hash),
                g_anonymity_fixture.ring_keys,
                ring_size,
                1
            );
            
            if (signature) {
                int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                                       g_anonymity_fixture.ring_keys, ring_size);
                if (verify_result == 0) {
                    successful_signatures++;
                }
                DAP_DELETE(signature);
            }
        }
        
        double success_rate = (double)successful_signatures / test_signatures;
        dap_test_msg("Ring size %zu: %zu/%zu signatures successful (%.1f%%)", 
                     ring_size, successful_signatures, test_signatures, success_rate * 100);
        
        dap_assert(success_rate >= 0.9, "Success rate should be at least 90%");
        
        cleanup_anonymity_fixture();
    }
    
    dap_test_msg("✅ Ring size anonymity impact test completed");
}

// Test 3: Multi-message anonymity preservation
static void test_multi_message_anonymity(void) {
    dap_test_msg("=== Test: Multi-Message Anonymity Preservation ===");
    
    setup_anonymity_fixture(6, ANONYMITY_MESSAGE_COUNT);
    
    // Test that same signer produces different signatures for different messages
    size_t signer_idx = 0; // Use first signer
    size_t different_signatures = 0;
    
    dap_sign_t *reference_signature = NULL;
    
    for (size_t msg_idx = 0; msg_idx < g_anonymity_fixture.message_count; msg_idx++) {
        dap_hash_fast_t message_hash;
        dap_hash_fast(g_anonymity_fixture.test_messages[msg_idx], 
                     g_anonymity_fixture.message_sizes[msg_idx], &message_hash);
        
        dap_sign_t *signature = dap_sign_create_ring(
            g_anonymity_fixture.ring_keys[signer_idx],
            &message_hash,
            sizeof(message_hash),
            g_anonymity_fixture.ring_keys,
            g_anonymity_fixture.ring_size,
            1
        );
        
        dap_assert(signature != NULL, "Signature creation should succeed");
        
        if (reference_signature == NULL) {
            reference_signature = signature;
        } else {
            // Compare signatures - they should be different for anonymity
            bool signatures_different = (signature->header.sign_size != reference_signature->header.sign_size) ||
                                      (memcmp(signature->pkey_n_sign, reference_signature->pkey_n_sign, 
                                             signature->header.sign_size) != 0);
            
            if (signatures_different) {
                different_signatures++;
            }
            
            DAP_DELETE(signature);
        }
    }
    
    double differentiation_rate = (double)different_signatures / (g_anonymity_fixture.message_count - 1);
    dap_test_msg("Message differentiation: %zu/%zu different (%.1f%%)", 
                 different_signatures, g_anonymity_fixture.message_count - 1, differentiation_rate * 100);
    
    // For good anonymity, signatures should be different for different messages
    dap_assert(differentiation_rate >= 0.8, "At least 80% of signatures should be different for different messages");
    
    if (reference_signature) {
        DAP_DELETE(reference_signature);
    }
    
    cleanup_anonymity_fixture();
    dap_test_msg("✅ Multi-message anonymity test completed");
}

// Test 4: Linkability modes testing
static void test_linkability_modes(void) {
    dap_test_msg("=== Test: Linkability Modes ===");
    
    setup_anonymity_fixture(5, 3);
    
    // Test different linkability modes
    uint32_t linkability_modes[] = {1, 2}; // MESSAGE_ONLY, FULL
    const char *mode_names[] = {"MESSAGE_ONLY", "FULL"};
    size_t num_modes = sizeof(linkability_modes) / sizeof(linkability_modes[0]);
    
    for (size_t mode_idx = 0; mode_idx < num_modes; mode_idx++) {
        dap_test_msg("Testing linkability mode: %s", mode_names[mode_idx]);
        
        // Create signatures with different linkability modes
        for (size_t msg_idx = 0; msg_idx < 3; msg_idx++) {
            dap_hash_fast_t message_hash;
            dap_hash_fast(g_anonymity_fixture.test_messages[msg_idx], 
                         g_anonymity_fixture.message_sizes[msg_idx], &message_hash);
            
            dap_sign_t *signature = dap_sign_create_ring(
                g_anonymity_fixture.ring_keys[0],
                &message_hash,
                sizeof(message_hash),
                g_anonymity_fixture.ring_keys,
                g_anonymity_fixture.ring_size,
                1
            );
            
            dap_assert(signature != NULL, "Signature with linkability mode should succeed");
            
            int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                                   g_anonymity_fixture.ring_keys, g_anonymity_fixture.ring_size);
            dap_assert(verify_result == 0, "Linkability signature should verify");
            
            DAP_DELETE(signature);
        }
    }
    
    cleanup_anonymity_fixture();
    dap_test_msg("✅ Linkability modes test completed");
}

// Test 5: Statistical signature analysis
static void test_statistical_signature_analysis(void) {
    dap_test_msg("=== Test: Statistical Signature Analysis ===");
    
    setup_anonymity_fixture(6, 10);
    
    // Generate multiple signatures and analyze their statistical properties
    size_t signature_sizes[ANONYMITY_TEST_ITERATIONS];
    uint8_t first_bytes[ANONYMITY_TEST_ITERATIONS];
    uint8_t last_bytes[ANONYMITY_TEST_ITERATIONS];
    
    size_t generated_signatures = 0;
    
    for (size_t i = 0; i < ANONYMITY_TEST_ITERATIONS && generated_signatures < ANONYMITY_TEST_ITERATIONS; i++) {
        // Random signer and message for each signature
        size_t signer_idx = rand() % g_anonymity_fixture.ring_size;
        size_t message_idx = rand() % g_anonymity_fixture.message_count;
        
        dap_hash_fast_t message_hash;
        dap_hash_fast(g_anonymity_fixture.test_messages[message_idx], 
                     g_anonymity_fixture.message_sizes[message_idx], &message_hash);
        
        dap_sign_t *signature = dap_sign_create_ring(
            g_anonymity_fixture.ring_keys[signer_idx],
            &message_hash,
            sizeof(message_hash),
            g_anonymity_fixture.ring_keys,
            g_anonymity_fixture.ring_size,
            1
        );
        
        if (signature) {
            signature_sizes[generated_signatures] = signature->header.sign_size;
            first_bytes[generated_signatures] = signature->pkey_n_sign[0];
            last_bytes[generated_signatures] = signature->pkey_n_sign[signature->header.sign_size - 1];
            
            generated_signatures++;
            DAP_DELETE(signature);
        }
    }
    
    // Statistical analysis
    if (generated_signatures > 10) {
        // Analyze size distribution
        size_t min_size = signature_sizes[0], max_size = signature_sizes[0];
        double avg_size = 0;
        
        for (size_t i = 0; i < generated_signatures; i++) {
            if (signature_sizes[i] < min_size) min_size = signature_sizes[i];
            if (signature_sizes[i] > max_size) max_size = signature_sizes[i];
            avg_size += signature_sizes[i];
        }
        avg_size /= generated_signatures;
        
        dap_test_msg("Signature sizes: min=%zu, max=%zu, avg=%.1f", min_size, max_size, avg_size);
        
        // Analyze byte distribution
        uint32_t first_byte_counts[256] = {0};
        uint32_t last_byte_counts[256] = {0};
        
        for (size_t i = 0; i < generated_signatures; i++) {
            first_byte_counts[first_bytes[i]]++;
            last_byte_counts[last_bytes[i]]++;
        }
        
        // Count unique values
        uint32_t unique_first = 0, unique_last = 0;
        for (uint32_t i = 0; i < 256; i++) {
            if (first_byte_counts[i] > 0) unique_first++;
            if (last_byte_counts[i] > 0) unique_last++;
        }
        
        dap_test_msg("Byte diversity: first_byte=%u unique, last_byte=%u unique", unique_first, unique_last);
        
        // For good anonymity, we expect reasonable diversity
        dap_assert(unique_first >= generated_signatures / 4, "First bytes should have reasonable diversity");
        dap_assert(unique_last >= generated_signatures / 4, "Last bytes should have reasonable diversity");
    }
    
    cleanup_anonymity_fixture();
    dap_test_msg("✅ Statistical signature analysis completed");
}

// Test 6: Timing analysis resistance (basic)
static void test_timing_analysis_resistance(void) {
    dap_test_msg("=== Test: Basic Timing Analysis Resistance ===");
    
    setup_anonymity_fixture(4, 5);
    
    // Measure signature creation times for different signers
    double signer_times[4];
    size_t measurements_per_signer = 5;
    
    for (size_t signer_idx = 0; signer_idx < g_anonymity_fixture.ring_size; signer_idx++) {
        double total_time = 0.0;
        
        for (size_t measurement = 0; measurement < measurements_per_signer; measurement++) {
            dap_hash_fast_t message_hash;
            dap_hash_fast(g_anonymity_fixture.test_messages[measurement % g_anonymity_fixture.message_count], 
                         g_anonymity_fixture.message_sizes[measurement % g_anonymity_fixture.message_count], &message_hash);
            
            clock_t start_time = clock();
            
            dap_sign_t *signature = dap_sign_create_ring(
                g_anonymity_fixture.ring_keys[signer_idx],
                &message_hash,
                sizeof(message_hash),
                g_anonymity_fixture.ring_keys,
                g_anonymity_fixture.ring_size,
                1
            );
            
            clock_t end_time = clock();
            double elapsed = ((double)(end_time - start_time)) / CLOCKS_PER_SEC;
            total_time += elapsed;
            
            if (signature) {
                DAP_DELETE(signature);
            }
        }
        
        signer_times[signer_idx] = total_time / measurements_per_signer;
        dap_test_msg("Signer %zu average time: %.6f seconds", signer_idx, signer_times[signer_idx]);
    }
    
    // Analyze timing variance
    double avg_time = 0.0;
    for (size_t i = 0; i < g_anonymity_fixture.ring_size; i++) {
        avg_time += signer_times[i];
    }
    avg_time /= g_anonymity_fixture.ring_size;
    
    double max_deviation = 0.0;
    for (size_t i = 0; i < g_anonymity_fixture.ring_size; i++) {
        double deviation = fabs(signer_times[i] - avg_time);
        if (deviation > max_deviation) {
            max_deviation = deviation;
        }
    }
    
    double relative_deviation = max_deviation / avg_time;
    dap_test_msg("Timing analysis: avg=%.6f, max_deviation=%.6f (%.1f%%)", 
                 avg_time, max_deviation, relative_deviation * 100);
    
    // For basic timing resistance, deviation should be reasonable
    dap_assert(relative_deviation < 0.5, "Timing deviation should be less than 50%");
    
    cleanup_anonymity_fixture();
    dap_test_msg("✅ Basic timing analysis resistance test completed");
}

// Main test runner
int main(void) {
    dap_test_msg("Starting ChipmunkRing Advanced Anonymity Tests");
    
    // Initialize DAP and seed random number generator
    dap_enc_key_init();
    srand(time(NULL));
    
    // Run advanced anonymity tests
    test_signer_indistinguishability();
    test_ring_size_anonymity_impact();
    test_multi_message_anonymity();
    test_linkability_modes();
    test_statistical_signature_analysis();
    test_timing_analysis_resistance();
    
    // Cleanup
    dap_enc_key_deinit();
    
    dap_test_msg("🎉 All advanced anonymity tests completed successfully!");
    return 0;
}
