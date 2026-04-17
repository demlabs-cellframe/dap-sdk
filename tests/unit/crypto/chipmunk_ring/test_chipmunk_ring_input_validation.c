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
#include <dap_hash_compat.h>
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
 * @brief Test that verification validates key type for every ring key
 */
static bool s_test_verify_rejects_invalid_key_type(void) {
    log_it(L_INFO, "Testing verification key type validation...");

    const size_t l_ring_size = 3;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));

    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    dap_hash_fast_t l_message_hash;
    dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);

    dap_sign_t* l_signature = dap_sign_create_ring(
        l_ring_keys[0],
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size, 1
    );
    dap_assert(l_signature != NULL, "Valid signature creation should succeed");

    // Wrong key type should be rejected during verification
    dap_enc_key_type_t l_original_type = l_ring_keys[1]->type;
    l_ring_keys[1]->type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    int l_result = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                        l_ring_keys, l_ring_size);
    dap_assert(l_result != 0, "Verification should fail with invalid ring key type");

    l_ring_keys[1]->type = l_original_type;

    // Restore all-valid ring keys for cleanup
    int l_recheck = dap_sign_verify_ring(l_signature, &l_message_hash, sizeof(l_message_hash),
                                         l_ring_keys, l_ring_size);
    dap_assert(l_recheck == 0, "Verification should succeed with all valid keys");

    DAP_DELETE(l_signature);
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Key type validation test passed");
    return true;
}

/**
 * @brief Test size and boundary limits in ring signing/verification APIs
 */
static bool s_test_size_boundaries(void) {
    log_it(L_INFO, "Testing size and boundary limits...");

    const size_t l_small_ring_size = 2;
    dap_enc_key_t* l_ring_keys[l_small_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));
    for (size_t i = 0; i < l_small_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    uint8_t l_message[32] = {0};

    // Create a valid baseline signature for repeated checks
    dap_sign_t* l_signature = dap_sign_create_ring(
        l_ring_keys[0],
        l_message, sizeof(l_message),
        l_ring_keys, l_small_ring_size, 1
    );
    dap_assert(l_signature != NULL, "Baseline signature should be created");

    // TC-02: ring_size above CHIPMUNK_RING_MAX_RING_SIZE must fail in signing path
    size_t l_oversize_ring = CHIPMUNK_RING_MAX_RING_SIZE + 1;
    dap_enc_key_t **l_oversize_keys = DAP_NEW_Z_COUNT(dap_enc_key_t*, l_oversize_ring);
    dap_assert(l_oversize_keys != NULL, "Oversize key array allocation should succeed");
    for (size_t i = 0; i < l_oversize_ring; i++) {
        l_oversize_keys[i] = l_ring_keys[0];
    }

    dap_sign_t* l_signature_over = dap_sign_create_ring(
        l_ring_keys[0],
        l_message, sizeof(l_message),
        l_oversize_keys, l_oversize_ring, 1
    );
    dap_assert(l_signature_over == NULL, "Signing with oversized ring must fail");

    // TC-02: oversized ring_size must fail in verification path
    int l_verify_over_size = dap_sign_verify_ring(
        l_signature,
        l_message, sizeof(l_message),
        l_oversize_keys, l_oversize_ring
    );
    dap_assert(l_verify_over_size != 0, "Verification with oversized ring_size must fail");
    DAP_DELETE(l_oversize_keys);

    // TC-05: message_size above CHIPMUNK_RING_MAX_MESSAGE_SIZE must fail safely
    size_t l_oversize_message_size = CHIPMUNK_RING_MAX_MESSAGE_SIZE + 1;
    uint8_t *l_oversize_message = DAP_NEW_SIZE(uint8_t, l_oversize_message_size);
    dap_assert(l_oversize_message != NULL, "Oversize message allocation should succeed");
    memset(l_oversize_message, 0xAA, l_oversize_message_size);

    dap_sign_t* l_signature_over_message = dap_sign_create_ring(
        l_ring_keys[0],
        l_oversize_message, l_oversize_message_size,
        l_ring_keys, l_small_ring_size, 1
    );
    dap_assert(l_signature_over_message == NULL, "Signing with oversize message must fail");
    DAP_DELETE(l_oversize_message);

    int l_recheck = dap_sign_verify_ring(
        l_signature,
        l_message, sizeof(l_message),
        l_ring_keys, l_small_ring_size
    );
    dap_assert(l_recheck == 0, "Baseline signature must still verify after negative checks");

    DAP_DELETE(l_signature);
    for (size_t i = 0; i < l_small_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Size and boundary limits test passed");
    return true;
}

/**
 * @brief Test negative verification paths consistency for side-channel resistance
 */
static bool s_test_verify_error_paths_consistency(void) {
    log_it(L_INFO, "Testing verify error-path consistency...");

    const size_t l_ring_size = 2;
    dap_enc_key_t* l_ring_keys[l_ring_size];
    memset(l_ring_keys, 0, sizeof(l_ring_keys));

    for (size_t i = 0; i < l_ring_size; i++) {
        l_ring_keys[i] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
        dap_assert(l_ring_keys[i] != NULL, "Ring key generation should succeed");
    }

    dap_hash_fast_t l_message_hash = {0};
    bool l_hash_result = dap_hash_fast(TEST_MESSAGE, strlen(TEST_MESSAGE), &l_message_hash);
    dap_assert(l_hash_result == true, "Message hashing should succeed");

    dap_sign_t* l_signature = dap_sign_create_ring(
        l_ring_keys[0],
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size, 1
    );
    dap_assert(l_signature != NULL, "Baseline signature creation should succeed");

    // TC-07: invalid verify inputs should fail safely without dereferencing sensitive state
    int l_invalid_values[5];
    size_t l_invalid_count = 0;

    l_invalid_values[l_invalid_count++] = dap_sign_verify_ring(
        NULL,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size
    );
    l_invalid_values[l_invalid_count++] = dap_sign_verify_ring(
        l_signature,
        NULL, 10,
        l_ring_keys, l_ring_size
    );
    l_invalid_values[l_invalid_count++] = dap_sign_verify_ring(
        l_signature,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, 0
    );
    l_invalid_values[l_invalid_count++] = dap_sign_verify_ring(
        l_signature,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, 1
    );
    l_invalid_values[l_invalid_count++] = dap_sign_verify_ring(
        l_signature,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, CHIPMUNK_RING_MAX_RING_SIZE + 1
    );

    for (size_t i = 0; i < l_invalid_count; i++) {
        dap_assert(l_invalid_values[i] != 0, "Invalid verify path must return failure code");
    }

    // TC-07 additional case: invalid key type should fail and still preserve behavioral failure mode
    dap_enc_key_type_t l_original_type = l_ring_keys[1]->type;
    l_ring_keys[1]->type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
    int l_invalid_key_type = dap_sign_verify_ring(
        l_signature, &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size
    );
    l_ring_keys[1]->type = l_original_type;
    dap_assert(l_invalid_key_type != 0, "Verify should fail for invalid key type");

    dap_assert(dap_sign_verify_ring(
        l_signature,
        &l_message_hash, sizeof(l_message_hash),
        l_ring_keys, l_ring_size
    ) == 0, "Baseline verification should succeed after invalid-path checks");

    DAP_DELETE(l_signature);
    for (size_t i = 0; i < l_ring_size; i++) {
        dap_enc_key_delete(l_ring_keys[i]);
    }

    log_it(L_INFO, "Verify error-path consistency test passed");
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
    l_all_passed &= s_test_verify_rejects_invalid_key_type();
    l_all_passed &= s_test_size_boundaries();
    l_all_passed &= s_test_verify_error_paths_consistency();
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
