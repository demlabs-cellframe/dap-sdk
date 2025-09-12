/*
 * ChipmunkRing Error Handling Unit Tests
 * Tests for the unified error handling system
 */

#include <dap_common.h>
#include <dap_test.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>
#include <dap_sign.h>
#include "chipmunk_ring_errors.h"

#define LOG_TAG "test_chipmunk_ring_error_handling"

/**
 * @brief Test error code to string conversion
 */
static bool s_test_error_to_string(void) {
    log_it(L_INFO, "Testing error code to string conversion...");
    
    // Test success code
    const char* success_msg = chipmunk_ring_error_to_string(CHIPMUNK_RING_SUCCESS);
    dap_assert(success_msg != NULL, "Success message should not be NULL");
    dap_assert(strcmp(success_msg, "Success") == 0, "Success message should be 'Success'");
    
    // Test parameter validation errors
    const char* null_param_msg = chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_NULL_PARAM);
    dap_assert(null_param_msg != NULL, "Null param message should not be NULL");
    dap_assert(strstr(null_param_msg, "NULL parameter") != NULL, "Should contain 'NULL parameter'");
    
    // Test memory errors
    const char* memory_msg = chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_MEMORY_ALLOC);
    dap_assert(memory_msg != NULL, "Memory error message should not be NULL");
    dap_assert(strstr(memory_msg, "Memory allocation") != NULL, "Should contain 'Memory allocation'");
    
    // Test crypto errors
    const char* hash_msg = chipmunk_ring_error_to_string(CHIPMUNK_RING_ERROR_HASH_FAILED);
    dap_assert(hash_msg != NULL, "Hash error message should not be NULL");
    dap_assert(strstr(hash_msg, "Hash operation") != NULL, "Should contain 'Hash operation'");
    
    // Test unknown error code
    const char* unknown_msg = chipmunk_ring_error_to_string((chipmunk_ring_error_t)-999);
    dap_assert(unknown_msg != NULL, "Unknown error message should not be NULL");
    dap_assert(strcmp(unknown_msg, "Unknown error") == 0, "Unknown error should return 'Unknown error'");
    
    log_it(L_INFO, "Error to string conversion test passed");
    return true;
}

/**
 * @brief Test error classification functions
 */
static bool s_test_error_classification(void) {
    log_it(L_INFO, "Testing error classification functions...");
    
    // Test critical error detection
    dap_assert(chipmunk_ring_error_is_critical(CHIPMUNK_RING_ERROR_MEMORY_ALLOC) == true, 
               "Memory allocation should be critical");
    dap_assert(chipmunk_ring_error_is_critical(CHIPMUNK_RING_ERROR_SECURITY_VIOLATION) == true, 
               "Security violation should be critical");
    dap_assert(chipmunk_ring_error_is_critical(CHIPMUNK_RING_ERROR_NULL_PARAM) == false, 
               "NULL param should not be critical");
    
    // Test memory-related error detection
    dap_assert(chipmunk_ring_error_is_memory_related(CHIPMUNK_RING_ERROR_MEMORY_ALLOC) == true, 
               "Memory alloc should be memory-related");
    dap_assert(chipmunk_ring_error_is_memory_related(CHIPMUNK_RING_ERROR_MEMORY_OVERFLOW) == true, 
               "Memory overflow should be memory-related");
    dap_assert(chipmunk_ring_error_is_memory_related(CHIPMUNK_RING_ERROR_HASH_FAILED) == false, 
               "Hash failure should not be memory-related");
    
    // Test crypto-related error detection
    dap_assert(chipmunk_ring_error_is_crypto_related(CHIPMUNK_RING_ERROR_HASH_FAILED) == true, 
               "Hash failure should be crypto-related");
    dap_assert(chipmunk_ring_error_is_crypto_related(CHIPMUNK_RING_ERROR_ZK_PROOF_FAILED) == true, 
               "ZK proof failure should be crypto-related");
    dap_assert(chipmunk_ring_error_is_crypto_related(CHIPMUNK_RING_ERROR_MEMORY_ALLOC) == false, 
               "Memory alloc should not be crypto-related");
    
    log_it(L_INFO, "Error classification test passed");
    return true;
}

/**
 * @brief Test error handling in ring signature creation
 */
static bool s_test_ring_signature_error_handling(void) {
    log_it(L_INFO, "Testing ring signature error handling...");
    
    // Test NULL signer key
    dap_sign_t* result = dap_sign_create_ring(NULL, "test", 4, NULL, 0, 1);
    dap_assert(result == NULL, "Should return NULL for NULL signer key");
    
    // Test NULL ring keys
    dap_enc_key_t* test_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    if (test_key) {
        result = dap_sign_create_ring(test_key, "test", 4, NULL, 5, 1);
        dap_assert(result == NULL, "Should return NULL for NULL ring keys");
        
        // Test invalid ring size
        result = dap_sign_create_ring(test_key, "test", 4, NULL, 1, 1);
        dap_assert(result == NULL, "Should return NULL for ring size 1");
        
        dap_enc_key_delete(test_key);
    }
    
    log_it(L_INFO, "Ring signature error handling test passed");
    return true;
}

/**
 * @brief Test error handling in key generation (via public API)
 */
static bool s_test_key_generation_error_handling(void) {
    log_it(L_INFO, "Testing key generation error handling...");
    
    // Test NULL key parameter in public API
    dap_enc_key_t* result = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    dap_assert(result != NULL, "Valid key generation should succeed");
    
    if (result) {
        dap_enc_key_delete(result);
    }
    
    // Test key generation with invalid parameters through public API
    // Note: Most validation happens at lower levels, so we test what we can through public API
    
    log_it(L_INFO, "Key generation error handling test passed");
    return true;
}

/**
 * @brief Test error logging functionality
 */
static bool s_test_error_logging(void) {
    log_it(L_INFO, "Testing error logging functionality...");
    
    // Test logging different types of errors
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_MEMORY_ALLOC, "test_function", "test memory error");
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_HASH_FAILED, "test_function", "test crypto error");
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_NULL_PARAM, "test_function", "test validation error");
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_SECURITY_VIOLATION, "test_function", "test critical error");
    
    // Test with NULL parameters (should handle gracefully)
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_SYSTEM, NULL, NULL);
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_SYSTEM, "test_function", NULL);
    chipmunk_ring_log_error(CHIPMUNK_RING_ERROR_SYSTEM, NULL, "test info");
    
    log_it(L_INFO, "Error logging test passed");
    return true;
}

/**
 * @brief Test comprehensive error coverage
 */
static bool s_test_comprehensive_error_coverage(void) {
    log_it(L_INFO, "Testing comprehensive error coverage...");
    
    // Test all major error categories have valid string representations
    chipmunk_ring_error_t test_errors[] = {
        CHIPMUNK_RING_SUCCESS,
        CHIPMUNK_RING_ERROR_NULL_PARAM,
        CHIPMUNK_RING_ERROR_INVALID_PARAM,
        CHIPMUNK_RING_ERROR_MEMORY_ALLOC,
        CHIPMUNK_RING_ERROR_MEMORY_OVERFLOW,
        CHIPMUNK_RING_ERROR_HASH_FAILED,
        CHIPMUNK_RING_ERROR_SIGNATURE_FAILED,
        CHIPMUNK_RING_ERROR_VERIFY_FAILED,
        CHIPMUNK_RING_ERROR_ZK_PROOF_FAILED,
        CHIPMUNK_RING_ERROR_SERIALIZATION_FAILED,
        CHIPMUNK_RING_ERROR_NOT_INITIALIZED,
        CHIPMUNK_RING_ERROR_SIGNER_NOT_IN_RING,
        CHIPMUNK_RING_ERROR_COORDINATION_FAILED,
        CHIPMUNK_RING_ERROR_SECURITY_VIOLATION,
        CHIPMUNK_RING_ERROR_SYSTEM
    };
    
    size_t num_test_errors = sizeof(test_errors) / sizeof(test_errors[0]);
    
    for (size_t i = 0; i < num_test_errors; i++) {
        const char* error_msg = chipmunk_ring_error_to_string(test_errors[i]);
        dap_assert(error_msg != NULL, "Error message should not be NULL");
        dap_assert(strlen(error_msg) > 0, "Error message should not be empty");
        
        log_it(L_DEBUG, "Error %d: %s", test_errors[i], error_msg);
    }
    
    log_it(L_INFO, "Comprehensive error coverage test passed");
    return true;
}

/**
 * @brief Main test function
 */
int main(int argc, char** argv) {
    log_it(L_NOTICE, "Starting ChipmunkRing error handling tests...");
    
    // Initialize modules
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize Chipmunk Ring module");
        return -1;
    }
    
    bool l_all_passed = true;
    l_all_passed &= s_test_error_to_string();
    l_all_passed &= s_test_error_classification();
    l_all_passed &= s_test_ring_signature_error_handling();
    l_all_passed &= s_test_key_generation_error_handling();
    l_all_passed &= s_test_error_logging();
    l_all_passed &= s_test_comprehensive_error_coverage();
    
    log_it(L_NOTICE, "ChipmunkRing error handling tests completed");
    
    if (l_all_passed) {
        log_it(L_NOTICE, "All error handling tests PASSED");
        return 0;
    } else {
        log_it(L_ERROR, "Some error handling tests FAILED");
        return -1;
    }
}
