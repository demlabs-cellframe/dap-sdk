/*
 * ChipmunkRing Input Validation Tests
 * Comprehensive testing of enhanced input parameter validation
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include <dap_hash.h>
#include "chipmunk_ring_errors.h"

#define LOG_TAG "test_chipmunk_ring_input_validation"

// Test constants
#define TEST_MESSAGE "ChipmunkRing Input Validation Test"
#define LARGE_MESSAGE_SIZE (2 * 1024 * 1024) // 2MB - should exceed limit

/**
 * @brief Test input validation in ring signature creation
 */
static bool s_test_signature_creation_validation(void) {
    log_it(L_INFO, "Testing signature creation input validation...");
    
    // Generate valid ring keys for positive tests
    const size_t ring_size = 4;
    dap_enc_key_t* ring_keys[ring_size];
    memset(ring_keys, 0, sizeof(ring_keys));
    
    for (size_t i = 0; i < ring_size; i++) {
        ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(ring_keys[i] != NULL, "Ring key generation should succeed");
    }
    
    dap_hash_fast_t message_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &message_hash);
    
    // Test 1: NULL signer key
    dap_sign_t* signature = dap_sign_create_ring(NULL, &message_hash, sizeof(message_hash),
                                                ring_keys, ring_size, 1);
    dap_assert(signature == NULL, "Should fail with NULL signer key");
    
    // Test 2: NULL ring keys
    signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                    NULL, ring_size, 1);
    dap_assert(signature == NULL, "Should fail with NULL ring keys");
    
    // Test 3: Invalid ring size (too small)
    signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                    ring_keys, 1, 1);
    dap_assert(signature == NULL, "Should fail with ring size 1");
    
    // Test 4: Invalid threshold (0)
    signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                    ring_keys, ring_size, 0);
    dap_assert(signature == NULL, "Should fail with threshold 0");
    
    // Test 5: Invalid threshold (greater than ring size)
    signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                    ring_keys, ring_size, ring_size + 1);
    dap_assert(signature == NULL, "Should fail with threshold > ring_size");
    
    // Test 6: Valid signature creation (should succeed)
    signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                    ring_keys, ring_size, 2);
    dap_assert(signature != NULL, "Valid signature creation should succeed");
    
    if (signature) {
        DAP_DELETE(signature);
    }
    
    // Cleanup
    for (size_t i = 0; i < ring_size; i++) {
        dap_enc_key_delete(ring_keys[i]);
    }
    
    log_it(L_INFO, "Signature creation validation test passed");
    return true;
}

/**
 * @brief Test input validation in ring signature verification
 */
static bool s_test_signature_verification_validation(void) {
    log_it(L_INFO, "Testing signature verification input validation...");
    
    // Create a valid signature for testing
    const size_t ring_size = 3;
    dap_enc_key_t* ring_keys[ring_size];
    memset(ring_keys, 0, sizeof(ring_keys));
    
    for (size_t i = 0; i < ring_size; i++) {
        ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(ring_keys[i] != NULL, "Ring key generation should succeed");
    }
    
    dap_hash_fast_t message_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &message_hash);
    
    dap_sign_t* valid_signature = dap_sign_create_ring(ring_keys[0], &message_hash, sizeof(message_hash),
                                                      ring_keys, ring_size, 1);
    dap_assert(valid_signature != NULL, "Valid signature creation should succeed");
    
    // Test 1: NULL signature
    int result = dap_sign_verify_ring(NULL, &message_hash, sizeof(message_hash),
                                     ring_keys, ring_size);
    dap_assert(result != 0, "Should fail with NULL signature");
    
    // Test 2: NULL message with non-zero size
    result = dap_sign_verify_ring(valid_signature, NULL, 10, ring_keys, ring_size);
    dap_assert(result != 0, "Should fail with NULL message but non-zero size");
    
    // Test 3: Valid verification (should succeed)
    result = dap_sign_verify_ring(valid_signature, &message_hash, sizeof(message_hash),
                                 ring_keys, ring_size);
    dap_assert(result == 0, "Valid signature verification should succeed");
    
    // Test 4: Empty message verification (should work)
    dap_sign_t* empty_msg_signature = dap_sign_create_ring(ring_keys[0], NULL, 0,
                                                          ring_keys, ring_size, 1);
    if (empty_msg_signature) {
        result = dap_sign_verify_ring(empty_msg_signature, NULL, 0, ring_keys, ring_size);
        dap_assert(result == 0, "Empty message verification should succeed");
        DAP_DELETE(empty_msg_signature);
    }
    
    // Cleanup
    DAP_DELETE(valid_signature);
    for (size_t i = 0; i < ring_size; i++) {
        dap_enc_key_delete(ring_keys[i]);
    }
    
    log_it(L_INFO, "Signature verification validation test passed");
    return true;
}

/**
 * @brief Test input validation with boundary conditions
 */
static bool s_test_boundary_conditions(void) {
    log_it(L_INFO, "Testing boundary conditions validation...");
    
    // Test minimum ring size (2)
    const size_t min_ring_size = 2;
    dap_enc_key_t* min_ring_keys[min_ring_size];
    memset(min_ring_keys, 0, sizeof(min_ring_keys));
    
    for (size_t i = 0; i < min_ring_size; i++) {
        min_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(min_ring_keys[i] != NULL, "Min ring key generation should succeed");
    }
    
    dap_hash_fast_t message_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &message_hash);
    
    // Test minimum ring size with minimum threshold
    dap_sign_t* min_signature = dap_sign_create_ring(min_ring_keys[0], &message_hash, sizeof(message_hash),
                                                    min_ring_keys, min_ring_size, 1);
    dap_assert(min_signature != NULL, "Minimum ring size signature should succeed");
    
    // Test minimum ring size with maximum threshold
    dap_sign_t* max_threshold_signature = dap_sign_create_ring(min_ring_keys[0], &message_hash, sizeof(message_hash),
                                                              min_ring_keys, min_ring_size, min_ring_size);
    dap_assert(max_threshold_signature != NULL, "Maximum threshold for minimum ring should succeed");
    
    // Test verification of boundary signatures
    int result = dap_sign_verify_ring(min_signature, &message_hash, sizeof(message_hash),
                                     min_ring_keys, min_ring_size);
    dap_assert(result == 0, "Minimum signature verification should succeed");
    
    result = dap_sign_verify_ring(max_threshold_signature, &message_hash, sizeof(message_hash),
                                 min_ring_keys, min_ring_size);
    dap_assert(result == 0, "Maximum threshold signature verification should succeed");
    
    // Cleanup
    DAP_DELETE(min_signature);
    DAP_DELETE(max_threshold_signature);
    for (size_t i = 0; i < min_ring_size; i++) {
        dap_enc_key_delete(min_ring_keys[i]);
    }
    
    log_it(L_INFO, "Boundary conditions validation test passed");
    return true;
}

/**
 * @brief Test error handling and recovery
 */
static bool s_test_error_handling_recovery(void) {
    log_it(L_INFO, "Testing error handling and recovery...");
    
    // Test error message generation for different validation failures
    log_it(L_DEBUG, "Testing error message: %s", 
           chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_NULL_PARAM));
    log_it(L_DEBUG, "Testing error message: %s", 
           chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_INVALID_THRESHOLD));
    log_it(L_DEBUG, "Testing error message: %s", 
           chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_RING_TOO_LARGE));
    
    // Test error classification
    dap_assert(chipmunk_ring_error_is_critical(CHIPMUNK_RING_ERROR_MEMORY_ALLOC) == true,
               "Memory allocation should be critical");
    dap_assert(chipmunk_ring_error_is_critical(CHIPMUNK_RING_ERROR_NULL_PARAM) == false,
               "NULL param should not be critical");
    
    // Test logging different error levels
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_NULL_PARAM, __func__, "Test validation error");
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_MEMORY_ALLOC, __func__, "Test critical error");
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_HASH_FAILED, __func__, "Test crypto error");
    
    log_it(L_INFO, "Error handling and recovery test passed");
    return true;
}

/**
 * @brief Test comprehensive input validation coverage
 */
static bool s_test_comprehensive_validation(void) {
    log_it(L_INFO, "Testing comprehensive input validation coverage...");
    
    // Test all major validation categories
    const size_t test_ring_size = 5;
    dap_enc_key_t* test_keys[test_ring_size];
    memset(test_keys, 0, sizeof(test_keys));
    
    for (size_t i = 0; i < test_ring_size; i++) {
        test_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(test_keys[i] != NULL, "Test key generation should succeed");
    }
    
    dap_hash_fast_t message_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &message_hash);
    
    // Test various threshold configurations
    size_t thresholds[] = {1, 2, 3, 4, 5};
    size_t num_thresholds = sizeof(thresholds) / sizeof(thresholds[0]);
    
    for (size_t i = 0; i < num_thresholds; i++) {
        dap_sign_t* signature = dap_sign_create_ring(test_keys[0], &message_hash, sizeof(message_hash),
                                                    test_keys, test_ring_size, thresholds[i]);
        
        if (thresholds[i] <= test_ring_size) {
            dap_assert(signature != NULL, "Valid threshold should succeed");
            
            if (signature) {
                int verify_result = dap_sign_verify_ring(signature, &message_hash, sizeof(message_hash),
                                                        test_keys, test_ring_size);
                dap_assert(verify_result == 0, "Valid signature verification should succeed");
                DAP_DELETE(signature);
            }
        } else {
            dap_assert(signature == NULL, "Invalid threshold should fail");
        }
        
        log_it(L_DEBUG, "Threshold %zu test completed", thresholds[i]);
    }
    
    // Cleanup
    for (size_t i = 0; i < test_ring_size; i++) {
        dap_enc_key_delete(test_keys[i]);
    }
    
    log_it(L_INFO, "Comprehensive validation test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting ChipmunkRing input validation tests...");
    
    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }
    
    bool l_all_passed = true;
    l_all_passed &= s_test_signature_creation_validation();
    l_all_passed &= s_test_signature_verification_validation();
    l_all_passed &= s_test_boundary_conditions();
    l_all_passed &= s_test_error_handling_recovery();
    l_all_passed &= s_test_comprehensive_validation();
    
    log_it(L_NOTICE, "ChipmunkRing input validation tests completed");
    
    if (l_all_passed) {
        log_it(L_NOTICE, "All input validation tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some input validation tests FAILED");
        return -1;
    }
}
