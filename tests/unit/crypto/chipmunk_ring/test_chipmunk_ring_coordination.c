/*
 * ChipmunkRing Coordination Protocol Tests
 * Basic tests for multi-signer coordination functionality
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

#define LOG_TAG "test_chipmunk_ring_coordination"

// Test basic coordination functionality
static void test_basic_coordination(void) {
    dap_test_msg("=== Test: Basic Coordination ===");
    
    // Generate ring keys (smaller for memory efficiency)
    size_t ring_size = 3;
    dap_enc_key_t **ring_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, ring_size);
    for (size_t i = 0; i < ring_size; i++) {
        ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    }
    
    // Create test message
    const char *test_data = "Coordination test message";
    dap_hash_fast_t message_hash;
    dap_hash_fast(test_data, strlen(test_data), &message_hash);
    
    // Test 2-of-3 coordination
    dap_sign_t *signature = dap_sign_create_ring(
        ring_keys[0],
        &message_hash,
        sizeof(message_hash),
        ring_keys,
        ring_size,
        2 // 2-of-3 threshold
    );
    
    dap_assert(signature != NULL, "Coordination signature creation should succeed");
    
    // Verify signature
    int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                           ring_keys, ring_size);
    dap_assert(verify_result == 0, "Coordination signature verification should succeed");
    
    // Cleanup
    DAP_DELETE(signature);
    for (size_t i = 0; i < ring_size; i++) {
        dap_enc_key_delete(ring_keys[i]);
    }
    DAP_DELETE(ring_keys);
    
    dap_test_msg("✅ Basic coordination test passed");
}

// Test coordination with different thresholds
static void test_coordination_thresholds(void) {
    dap_test_msg("=== Test: Coordination with Different Thresholds ===");
    
    size_t ring_size = 6;
    dap_enc_key_t **ring_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, ring_size);
    for (size_t i = 0; i < ring_size; i++) {
        ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    }
    
    const char *test_data = "Threshold coordination test";
    dap_hash_fast_t message_hash;
    dap_hash_fast(test_data, strlen(test_data), &message_hash);
    
    // Test different thresholds: 2-of-6, 3-of-6, 4-of-6
    size_t thresholds[] = {2, 3, 4};
    size_t num_thresholds = sizeof(thresholds) / sizeof(thresholds[0]);
    
    for (size_t i = 0; i < num_thresholds; i++) {
        size_t threshold = thresholds[i];
        dap_test_msg("Testing %zu-of-%zu coordination", threshold, ring_size);
        
        dap_sign_t *signature = dap_sign_create_ring(
            ring_keys[0],
            &message_hash,
            sizeof(message_hash),
            ring_keys,
            ring_size,
            threshold
        );
        
        dap_assert(signature != NULL, "Threshold coordination should succeed");
        
        int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                               ring_keys, ring_size);
        dap_assert(verify_result == 0, "Threshold signature should verify");
        
        DAP_DELETE(signature);
    }
    
    // Cleanup
    for (size_t i = 0; i < ring_size; i++) {
        dap_enc_key_delete(ring_keys[i]);
    }
    DAP_DELETE(ring_keys);
    
    dap_test_msg("✅ Coordination thresholds test passed");
}

// Test coordination edge cases
static void test_coordination_edge_cases(void) {
    dap_test_msg("=== Test: Coordination Edge Cases ===");
    
    size_t ring_size = 4;
    dap_enc_key_t **ring_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, ring_size);
    for (size_t i = 0; i < ring_size; i++) {
        ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    }
    
    const char *test_data = "Edge case coordination test";
    dap_hash_fast_t message_hash;
    dap_hash_fast(test_data, strlen(test_data), &message_hash);
    
    // Test minimum threshold (1-of-4, should work like traditional ring)
    dap_sign_t *min_signature = dap_sign_create_ring(
        ring_keys[0], &message_hash, sizeof(message_hash),
        ring_keys, ring_size, 1
    );
    
    dap_assert(min_signature != NULL, "Minimum threshold should work");
    
    int verify_min = dap_sign_verify_ring(min_signature, &message_hash, sizeof(message_hash),
                                        ring_keys, ring_size);
    dap_assert(verify_min == 0, "Minimum threshold signature should verify");
    DAP_DELETE(min_signature);
    
    // Test maximum threshold (4-of-4, all signers required)
    dap_sign_t *max_signature = dap_sign_create_ring(
        ring_keys[0], &message_hash, sizeof(message_hash),
        ring_keys, ring_size, ring_size
    );
    
    dap_assert(max_signature != NULL, "Maximum threshold should work");
    
    int verify_max = dap_sign_verify_ring(max_signature, &message_hash, sizeof(message_hash),
                                        ring_keys, ring_size);
    dap_assert(verify_max == 0, "Maximum threshold signature should verify");
    DAP_DELETE(max_signature);
    
    // Test invalid threshold (should fail)
    dap_sign_t *invalid_signature = dap_sign_create_ring(
        ring_keys[0], &message_hash, sizeof(message_hash),
        ring_keys, ring_size, ring_size + 1 // Invalid: more than ring size
    );
    
    dap_assert(invalid_signature == NULL, "Invalid threshold should fail");
    
    // Cleanup
    for (size_t i = 0; i < ring_size; i++) {
        dap_enc_key_delete(ring_keys[i]);
    }
    DAP_DELETE(ring_keys);
    
    dap_test_msg("✅ Coordination edge cases test passed");
}

// Main test runner
int main(void) {
    dap_test_msg("Starting ChipmunkRing Coordination Protocol Tests");
    
    // Initialize DAP
    dap_enc_key_init();
    
    // Run tests
    test_basic_coordination();
    test_coordination_thresholds();
    test_coordination_edge_cases();
    
    // Cleanup
    dap_enc_key_deinit();
    
    dap_test_msg("🎉 All coordination protocol tests passed successfully!");
    return 0;
}
