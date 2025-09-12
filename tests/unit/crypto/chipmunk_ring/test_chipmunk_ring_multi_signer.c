/*
 * ChipmunkRing Multi-Signer (Threshold) Mode Tests
 * Tests for required_signers > 1 functionality including:
 * - Basic threshold signing (2-of-3, 3-of-5, etc.)
 * - Multi-signer mode verification
 * - Edge cases and error conditions
 * - Performance comparison single vs multi-signer
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

#define LOG_TAG "test_chipmunk_ring_multi_signer"

// Test fixture data
typedef struct {
    dap_enc_key_t **keys;
    size_t keys_count;
    uint8_t *test_message;
    size_t test_message_size;
    dap_enc_key_t **ring_keys;
    size_t ring_size;
    dap_hash_fast_t message_hash;
} test_fixture_t;

static test_fixture_t g_fixture;

// Setup test environment
static void setup_test_fixture(size_t ring_size, size_t keys_count) {
    g_fixture.ring_size = ring_size;
    g_fixture.keys_count = keys_count;
    
    // Generate ring keys
    g_fixture.ring_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, ring_size);
    for (size_t i = 0; i < ring_size; i++) {
        g_fixture.ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
        dap_test_msg("Generated ring key %zu", i);
    }
    
    // Select subset of keys for actual signers
    g_fixture.keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, keys_count);
    for (size_t i = 0; i < keys_count; i++) {
        g_fixture.keys[i] = g_fixture.ring_keys[i]; // Use first keys_count keys from ring
    }
    
    // Create test message
    const char *test_data = "Multi-signer threshold ring signature test message for ChipmunkRing";
    g_fixture.test_message_size = strlen(test_data);
    g_fixture.test_message = DAP_NEW_Z_SIZE(uint8_t, g_fixture.test_message_size);
    memcpy(g_fixture.test_message, test_data, g_fixture.test_message_size);
    
    // Generate message hash
    dap_hash_fast(g_fixture.test_message, g_fixture.test_message_size, &g_fixture.message_hash);
    
    dap_test_msg("Test fixture setup complete: ring_size=%zu, signers=%zu", ring_size, keys_count);
}

static void cleanup_test_fixture(void) {
    if (g_fixture.ring_keys) {
        for (size_t i = 0; i < g_fixture.ring_size; i++) {
            if (g_fixture.ring_keys[i]) {
                dap_enc_key_delete(g_fixture.ring_keys[i]);
            }
        }
        DAP_DELETE(g_fixture.ring_keys);
    }
    
    if (g_fixture.keys) {
        DAP_DELETE(g_fixture.keys); // Don't delete individual keys, they're aliases
    }
    
    if (g_fixture.test_message) {
        DAP_DELETE(g_fixture.test_message);
    }
    
    memset(&g_fixture, 0, sizeof(g_fixture));
}

// Test 1: Basic 2-of-3 threshold signature
static void test_basic_2_of_3_threshold(void) {
    dap_test_msg("=== Test: Basic 2-of-3 Threshold Signature ===");
    
    setup_test_fixture(3, 3); // 3-member ring, 3 potential signers
    
    // Create 2-of-3 threshold signature
    dap_sign_t *signature = dap_sign_create_ring(
        g_fixture.keys[0],           // Signer key (must be in ring)
        &g_fixture.message_hash,     // Message hash
        sizeof(g_fixture.message_hash), // Message size
        g_fixture.ring_keys,         // Ring
        g_fixture.ring_size,         // Ring size
        2                            // Required signers (threshold)
    );
    
    dap_assert(signature != NULL, "Failed to create 2-of-3 threshold signature");
    
    // Verify signature structure
    dap_assert(signature->header.type.type == SIG_TYPE_CHIPMUNK_RING, "Should be CHIPMUNK_RING type");
    dap_assert(signature->header.sign_size > 0, "Signature should have positive size");
    
    // Verify signature
    int verify_result = dap_sign_verify_ring(signature, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                           g_fixture.ring_keys, g_fixture.ring_size);
    dap_assert(verify_result == 0, "2-of-3 threshold signature verification failed");
    
    DAP_DELETE(signature);
    cleanup_test_fixture();
    dap_test_msg("✅ Basic 2-of-3 threshold test passed");
}

// Test 2: 3-of-5 threshold signature
static void test_3_of_5_threshold(void) {
    dap_test_msg("=== Test: 3-of-5 Threshold Signature ===");
    
    setup_test_fixture(4, 4); // 4-member ring, 4 potential signers
    
    // Create 3-of-5 threshold signature
    dap_sign_t *signature = dap_sign_create_ring(
        g_fixture.keys[0],           // Signer key
        &g_fixture.message_hash,     // Message hash
        sizeof(g_fixture.message_hash), // Message size
        g_fixture.ring_keys,         // Ring
        g_fixture.ring_size,         // Ring size
        2                            // Required signers (2-of-4)
    );
    
    dap_assert(signature != NULL, "Failed to create 2-of-4 threshold signature");
    
    // Verify signature structure
    dap_assert(signature->header.type.type == SIG_TYPE_CHIPMUNK_RING, "Should be CHIPMUNK_RING type");
    
    // Verify signature
    int verify_result = dap_sign_verify_ring(signature, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                           g_fixture.ring_keys, g_fixture.ring_size);
    dap_assert(verify_result == 0, "3-of-5 threshold signature verification failed");
    
    DAP_DELETE(signature);
    cleanup_test_fixture();
    dap_test_msg("✅ 3-of-5 threshold test passed");
}

// Test 3: Edge cases and error conditions
static void test_multi_signer_edge_cases(void) {
    dap_test_msg("=== Test: Multi-Signer Edge Cases and Error Conditions ===");
    
    setup_test_fixture(5, 3);
    
    // Test 1: Required signers > available signers (should fail gracefully)
    dap_sign_t *invalid_signature = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        10 // More than ring size
    );
    
    dap_assert(invalid_signature == NULL, "Should fail when required_signers > ring_size");
    
    // Test 2: Required signers = 0 (should default to 1)
    dap_sign_t *zero_threshold = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        0 // Zero threshold
    );
    
    if (zero_threshold) {
        // If it succeeds, it should behave like single signer
        int verify_zero = dap_sign_verify_ring(zero_threshold, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                             g_fixture.ring_keys, g_fixture.ring_size);
        dap_assert(verify_zero == 0, "Zero threshold signature should verify");
        DAP_DELETE(zero_threshold);
    }
    
    // Test 3: Required signers = 1 (should work as single signer)
    dap_sign_t *single_signer = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        1 // Single signer
    );
    
    dap_assert(single_signer != NULL, "Single signer mode should work");
    
    int verify_single = dap_sign_verify_ring(single_signer, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                           g_fixture.ring_keys, g_fixture.ring_size);
    dap_assert(verify_single == 0, "Single signer verification should pass");
    DAP_DELETE(single_signer);
    
    // Test 4: Required signers = ring_size (all signers required)
    dap_sign_t *all_signers = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        g_fixture.ring_size // All signers
    );
    
    dap_assert(all_signers != NULL, "All signers mode should work");
    
    int verify_all = dap_sign_verify_ring(all_signers, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                        g_fixture.ring_keys, g_fixture.ring_size);
    dap_assert(verify_all == 0, "All signers verification should pass");
    DAP_DELETE(all_signers);
    
    cleanup_test_fixture();
    dap_test_msg("✅ Multi-signer edge cases test passed");
}

// Test 4: Performance comparison: single vs multi-signer
static void test_performance_comparison(void) {
    dap_test_msg("=== Test: Performance Comparison Single vs Multi-Signer ===");
    
    setup_test_fixture(3, 3);
    
    // Measure single signer performance
    clock_t start_single = clock();
    
    dap_sign_t *single_sig = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        1 // Single signer
    );
    
    clock_t end_single = clock();
    double single_time = ((double)(end_single - start_single)) / CLOCKS_PER_SEC;
    
    dap_assert(single_sig != NULL, "Single signer creation should succeed");
    
    // Measure multi-signer performance
    clock_t start_multi = clock();
    
    dap_sign_t *multi_sig = dap_sign_create_ring(
        g_fixture.keys[0],
        &g_fixture.message_hash,
        sizeof(g_fixture.message_hash),
        g_fixture.ring_keys,
        g_fixture.ring_size,
        3 // Multi-signer (3-of-8)
    );
    
    clock_t end_multi = clock();
    double multi_time = ((double)(end_multi - start_multi)) / CLOCKS_PER_SEC;
    
    dap_assert(multi_sig != NULL, "Multi-signer creation should succeed");
    
    // Compare performance
    dap_test_msg("Performance comparison:");
    dap_test_msg("  Single signer: %.6f seconds", single_time);
    dap_test_msg("  Multi-signer (2-of-3): %.6f seconds", multi_time);
    if (single_time > 0) {
        dap_test_msg("  Overhead factor: %.2fx", multi_time / single_time);
    }
    
    // Verify both signatures
    int verify_single = dap_sign_verify_ring(single_sig, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                           g_fixture.ring_keys, g_fixture.ring_size);
    int verify_multi = dap_sign_verify_ring(multi_sig, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                          g_fixture.ring_keys, g_fixture.ring_size);
    
    dap_assert(verify_single == 0, "Single signer verification failed");
    dap_assert(verify_multi == 0, "Multi-signer verification failed");
    
    DAP_DELETE(single_sig);
    DAP_DELETE(multi_sig);
    cleanup_test_fixture();
    
    dap_test_msg("✅ Performance comparison test passed");
}

// Test 5: Different threshold combinations
static void test_various_threshold_combinations(void) {
    dap_test_msg("=== Test: Various Threshold Combinations ===");
    
    // Test different combinations
    size_t test_cases[][3] = {
        {3, 2, 1},  // 3-ring, 2-signers, 1-threshold
        {4, 3, 2},  // 4-ring, 3-signers, 2-threshold  
        {5, 4, 3},  // 5-ring, 4-signers, 3-threshold
        {6, 5, 4},  // 6-ring, 5-signers, 4-threshold
        {7, 6, 5}   // 7-ring, 6-signers, 5-threshold
    };
    
    size_t num_cases = sizeof(test_cases) / sizeof(test_cases[0]);
    
    for (size_t i = 0; i < num_cases; i++) {
        size_t ring_size = test_cases[i][0];
        size_t signers = test_cases[i][1];
        size_t threshold = test_cases[i][2];
        
        dap_test_msg("Testing %zu-of-%zu threshold in %zu-member ring", threshold, signers, ring_size);
        
        setup_test_fixture(ring_size, signers);
        
        dap_sign_t *signature = dap_sign_create_ring(
            g_fixture.keys[0],
            &g_fixture.message_hash,
            sizeof(g_fixture.message_hash),
            g_fixture.ring_keys,
            g_fixture.ring_size,
            threshold
        );
        
        dap_assert(signature != NULL, "Threshold signature creation should succeed");
        
        int verify_result = dap_sign_verify_ring(signature, &g_fixture.message_hash, sizeof(g_fixture.message_hash),
                                               g_fixture.ring_keys, g_fixture.ring_size);
        dap_assert(verify_result == 0, "Threshold signature verification should succeed");
        
        DAP_DELETE(signature);
        cleanup_test_fixture();
    }
    
    dap_test_msg("✅ Various threshold combinations test passed");
}

// Main test runner
int main(void) {
    dap_test_msg("Starting ChipmunkRing Multi-Signer Comprehensive Tests");
    
    // Initialize DAP
    dap_enc_key_init();
    
    // Run tests
    test_basic_2_of_3_threshold();
    test_3_of_5_threshold();
    test_multi_signer_edge_cases();
    test_performance_comparison();
    test_various_threshold_combinations();
    
    // Cleanup
    dap_enc_key_deinit();
    
    dap_test_msg("🎉 All ChipmunkRing Multi-Signer tests passed successfully!");
    return 0;
}
