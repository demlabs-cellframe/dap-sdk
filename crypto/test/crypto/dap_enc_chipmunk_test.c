#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_key.h"
#include "chipmunk/chipmunk.h"

#define LOG_TAG "dap_enc_chipmunk_test"
#define TEST_DATA "This is test data for Chipmunk algorithm verification"
#define INVALID_TEST_DATA "This is invalid test data"

// Custom assert implementation
#define dap_assert(condition, message) \
    do { \
        if (!(condition)) { \
            log_it(L_ERROR, "Assertion failed: %s", message); \
            return -1; \
        } \
    } while (0)

/**
 * @brief Test for Chipmunk key creation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_new_test(void)
{
    // Initialize cryptography module
    dap_enc_chipmunk_init();
    
    // Create a new key
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    
    // Verify the key was created correctly
    dap_assert(l_key != NULL, "Key successfully created");
    dap_assert(l_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, "Key type is correct");
    dap_assert(l_key->priv_key_data != NULL, "Private key is not NULL");
    dap_assert(l_key->pub_key_data != NULL, "Public key is not NULL");
    
    
    // Cleanup
    dap_enc_key_delete(l_key);
    
    return 0;
}

/**
 * @brief Test for Chipmunk key pair generation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_generate_test(void)
{
    // Generate key from seed data
    uint8_t seed[32];
    memcpy(seed, "ChipmunkTestSeed1234567890123456", 32);
    
    dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, seed, sizeof(seed), 0);
    
    // Verify the key was created correctly
    dap_assert(l_key != NULL, "Key successfully generated from seed");
    dap_assert(l_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, "Key type is correct");
    dap_assert(l_key->priv_key_data != NULL, "Private key is not NULL");
    dap_assert(l_key->pub_key_data != NULL, "Public key is not NULL");
    
    // Cleanup
    dap_enc_key_delete(l_key);
    
    return 0;
}

/**
 * @brief Test for Chipmunk signature creation and verification
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_sign_verify_test(void)
{
    // Create a key for signing
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    dap_assert(l_key != NULL, "Key successfully created");
    
    // Calculate signature size
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    dap_assert(l_sign_size > 0, "Signature size is positive");
    dap_assert(l_sign_size == CHIPMUNK_SIGNATURE_SIZE, "Signature size matches expected value");
    
    // Allocate memory for signature
    uint8_t *l_sign = DAP_NEW_Z_SIZE(uint8_t, l_sign_size);
    dap_assert(l_sign != NULL, "Signature buffer allocated");
    
    // Create signature
    int l_sign_res = l_key->sign_get(l_key, (uint8_t*)TEST_DATA, strlen(TEST_DATA), l_sign, l_sign_size);
    dap_assert(l_sign_res > 0, "Signature created successfully");
    
    // Verify signature with the same key
    int l_verify_res = l_key->sign_verify(l_key, (uint8_t*)TEST_DATA, strlen(TEST_DATA), l_sign, l_sign_res);
    dap_assert(l_verify_res == 0, "Signature verified successfully");
    
    // Try to verify with wrong data
    l_verify_res = l_key->sign_verify(l_key, (uint8_t*)INVALID_TEST_DATA, strlen(INVALID_TEST_DATA), l_sign, l_sign_res);
    dap_assert(l_verify_res != 0, "Signature verification failed with wrong data as expected");
    
    // Generate a different key and try to verify the signature
    dap_enc_key_t *l_key2 = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    dap_assert(l_key2 != NULL, "Second key successfully created");
    
    // The signature should not verify with a different key
    l_verify_res = l_key2->sign_verify(l_key2, (uint8_t*)TEST_DATA, strlen(TEST_DATA), l_sign, l_sign_res);
    dap_assert(l_verify_res != 0, "Signature verification fails with different key as expected");
    
    // Clean up
    DAP_DELETE(l_sign);
    dap_enc_key_delete(l_key);
    dap_enc_key_delete(l_key2);
    
    return 0;
}

/**
 * @brief Test for Chipmunk signature size calculation
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_calc_signature_size_test(void)
{
    size_t l_sign_size = dap_enc_chipmunk_calc_signature_size();
    dap_assert(l_sign_size > 0, "Signature size is positive");
    dap_assert(l_sign_size == CHIPMUNK_SIGNATURE_SIZE, "Signature size matches expected value");
    return 0;
}

/**
 * @brief Test for Chipmunk key deletion
 * 
 * @return int Test result (0 - success)
 */
static int dap_enc_chipmunk_key_delete_test(void)
{
    dap_enc_key_t *l_key = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    dap_assert(l_key != NULL, "Key successfully created");
    
    // Test deletion
    dap_enc_key_delete(l_key);
    
    // If we got here without crashes, the test passed
    return 0;
}

/**
 * @brief Run all tests for Chipmunk module
 * 
 * @return int Result of all tests execution (0 - success)
 */
int dap_enc_chipmunk_tests_run(void)
{
    int l_ret = 0;

    // Test key creation
    log_it(L_INFO, "Testing Chipmunk key creation...");
    l_ret += dap_enc_chipmunk_key_new_test();
    log_it(L_INFO, "Key creation test %s", l_ret == 0 ? "PASSED" : "FAILED");

    // Test key pair generation
    log_it(L_INFO, "Testing Chipmunk key pair generation...");
    l_ret += dap_enc_chipmunk_key_generate_test();
    log_it(L_INFO, "Key pair generation test %s", l_ret == 0 ? "PASSED" : "FAILED");

    // Test signature generation and verification
    log_it(L_INFO, "Testing Chipmunk signature...");
    l_ret += dap_enc_chipmunk_sign_verify_test();
    log_it(L_INFO, "Signature test %s", l_ret == 0 ? "PASSED" : "FAILED");

    // Test signature size calculation
    log_it(L_INFO, "Testing Chipmunk signature size calculation...");
    l_ret += dap_enc_chipmunk_calc_signature_size_test();
    log_it(L_INFO, "Signature size calculation test %s", l_ret == 0 ? "PASSED" : "FAILED");

    // Test key deletion
    log_it(L_INFO, "Testing Chipmunk key deletion...");
    l_ret += dap_enc_chipmunk_key_delete_test();
    log_it(L_INFO, "Key deletion test %s", l_ret == 0 ? "PASSED" : "FAILED");

    return l_ret;
} 
