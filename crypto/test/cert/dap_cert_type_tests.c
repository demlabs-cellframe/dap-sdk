#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "dap_test.h"
#include "dap_enc_key.h"
#include "dap_cert.h"
#include "dap_cert_file.h"

#define PRIVATE_CERT_FILE_PATH "private_cert.tmp"
#define PUBLIC_CERT_FILE_PATH "public_cert.tmp"

/**
 * @brief Test certificate type detection for private certificates
 */
static void test_cert_type_private(dap_enc_key_type_t a_key_type)
{
    dap_cert_t *l_cert = dap_cert_generate_mem("test_private_cert", a_key_type);
    dap_assert_PIF(l_cert, "Failed to create private certificate");
    
    // Check that newly generated certificate is private
    dap_cert_type_t l_type = dap_cert_get_type(l_cert);
    dap_assert_PIF(l_type == DAP_CERT_TYPE_PRIVATE, 
                   "Certificate type should be PRIVATE, got %s", 
                   dap_cert_type_to_str(l_type));
    
    // Check helper functions
    dap_assert_PIF(dap_cert_is_private(l_cert), 
                   "dap_cert_is_private() should return true for private certificate");
    dap_assert_PIF(!dap_cert_is_public(l_cert), 
                   "dap_cert_is_public() should return false for private certificate");
    
    // Verify certificate can sign data
    const char *test_data = "Test data for signing";
    dap_sign_t *l_sign = dap_cert_sign(l_cert, test_data, strlen(test_data));
    dap_assert_PIF(l_sign, "Private certificate should be able to sign data");
    
    DAP_DELETE(l_sign);
    dap_cert_delete(l_cert);
    
    dap_pass_msg("Private certificate type detection passed for %s", 
                 dap_enc_get_type_name(a_key_type));
}

/**
 * @brief Test certificate type detection for public certificates
 */
static void test_cert_type_public(dap_enc_key_type_t a_key_type)
{
    // Create private certificate first
    dap_cert_t *l_cert_private = dap_cert_generate_mem("test_cert_for_public", a_key_type);
    dap_assert_PIF(l_cert_private, "Failed to create private certificate");
    
    // Save to memory
    uint32_t l_cert_size = 0;
    uint8_t *l_cert_buf = dap_cert_mem_save(l_cert_private, &l_cert_size);
    dap_assert_PIF(l_cert_buf, "Failed to serialize certificate");
    
    // Load certificate back
    dap_cert_t *l_cert_loaded = dap_cert_mem_load(l_cert_buf, l_cert_size);
    dap_assert_PIF(l_cert_loaded, "Failed to load certificate from memory");
    
    // Create public-only version by removing private key data
    dap_cert_t *l_cert_public = dap_cert_new("test_public_cert");
    l_cert_public->enc_key = dap_enc_key_new(l_cert_loaded->enc_key->type);
    
    // Copy only public key data
    l_cert_public->enc_key->pub_key_data_size = l_cert_loaded->enc_key->pub_key_data_size;
    if (l_cert_loaded->enc_key->pub_key_data_size > 0) {
        l_cert_public->enc_key->pub_key_data = DAP_NEW_SIZE(void, l_cert_loaded->enc_key->pub_key_data_size);
        memcpy(l_cert_public->enc_key->pub_key_data, 
               l_cert_loaded->enc_key->pub_key_data, 
               l_cert_loaded->enc_key->pub_key_data_size);
    }
    
    // Ensure no private key data
    l_cert_public->enc_key->priv_key_data = NULL;
    l_cert_public->enc_key->priv_key_data_size = 0;
    
    // Check that certificate is detected as public
    dap_cert_type_t l_type = dap_cert_get_type(l_cert_public);
    dap_assert_PIF(l_type == DAP_CERT_TYPE_PUBLIC, 
                   "Certificate type should be PUBLIC, got %s", 
                   dap_cert_type_to_str(l_type));
    
    // Check helper functions
    dap_assert_PIF(dap_cert_is_public(l_cert_public), 
                   "dap_cert_is_public() should return true for public certificate");
    dap_assert_PIF(!dap_cert_is_private(l_cert_public), 
                   "dap_cert_is_private() should return false for public certificate");
    
    // Verify that public certificate CANNOT sign data
    const char *test_data = "Test data that should not be signable";
    dap_sign_t *l_sign = dap_cert_sign(l_cert_public, test_data, strlen(test_data));
    dap_assert_PIF(!l_sign, "Public certificate should NOT be able to sign data");
    
    DAP_DELETE(l_cert_buf);
    dap_cert_delete(l_cert_private);
    dap_cert_delete(l_cert_loaded);
    dap_cert_delete(l_cert_public);
    
    dap_pass_msg("Public certificate type detection passed for %s", 
                 dap_enc_get_type_name(a_key_type));
}

/**
 * @brief Test signing operations with certificate type validation
 */
static void test_cert_signing_validation(dap_enc_key_type_t a_key_type)
{
    // Create two private certificates
    dap_cert_t *l_cert_to_sign = dap_cert_generate_mem("cert_to_be_signed", a_key_type);
    dap_cert_t *l_cert_signer_private = dap_cert_generate_mem("private_signer", a_key_type);
    
    dap_assert_PIF(l_cert_to_sign && l_cert_signer_private, 
                   "Failed to create certificates for signing test");
    
    // Test 1: Private certificate can sign another certificate
    int l_result = dap_cert_add_cert_sign(l_cert_to_sign, l_cert_signer_private);
    dap_assert_PIF(l_result == 0, 
                   "Private certificate should be able to sign another certificate");
    
    // Test 2: Create public-only version of signer
    dap_cert_t *l_cert_signer_public = dap_cert_new("public_signer");
    l_cert_signer_public->enc_key = dap_enc_key_new(l_cert_signer_private->enc_key->type);
    
    // Copy only public key
    l_cert_signer_public->enc_key->pub_key_data_size = l_cert_signer_private->enc_key->pub_key_data_size;
    if (l_cert_signer_private->enc_key->pub_key_data_size > 0) {
        l_cert_signer_public->enc_key->pub_key_data = DAP_NEW_SIZE(void, 
                                                                    l_cert_signer_private->enc_key->pub_key_data_size);
        memcpy(l_cert_signer_public->enc_key->pub_key_data, 
               l_cert_signer_private->enc_key->pub_key_data, 
               l_cert_signer_private->enc_key->pub_key_data_size);
    }
    l_cert_signer_public->enc_key->priv_key_data = NULL;
    l_cert_signer_public->enc_key->priv_key_data_size = 0;
    
    // Test 3: Public certificate should NOT be able to sign
    dap_cert_t *l_cert_to_sign2 = dap_cert_generate_mem("another_cert_to_sign", a_key_type);
    l_result = dap_cert_add_cert_sign(l_cert_to_sign2, l_cert_signer_public);
    dap_assert_PIF(l_result != 0, 
                   "Public certificate should NOT be able to sign another certificate, got result: %d", 
                   l_result);
    
    dap_cert_delete(l_cert_to_sign);
    dap_cert_delete(l_cert_to_sign2);
    dap_cert_delete(l_cert_signer_private);
    dap_cert_delete(l_cert_signer_public);
    
    dap_pass_msg("Certificate signing validation passed for %s", 
                 dap_enc_get_type_name(a_key_type));
}

/**
 * @brief Test certificate type string conversion
 */
static void test_cert_type_to_string()
{
    const char *l_private_str = dap_cert_type_to_str(DAP_CERT_TYPE_PRIVATE);
    dap_assert_PIF(strcmp(l_private_str, "private") == 0, 
                   "DAP_CERT_TYPE_PRIVATE should convert to 'private'");
    
    const char *l_public_str = dap_cert_type_to_str(DAP_CERT_TYPE_PUBLIC);
    dap_assert_PIF(strcmp(l_public_str, "public") == 0, 
                   "DAP_CERT_TYPE_PUBLIC should convert to 'public'");
    
    const char *l_invalid_str = dap_cert_type_to_str(DAP_CERT_TYPE_INVALID);
    dap_assert_PIF(strcmp(l_invalid_str, "invalid") == 0, 
                   "DAP_CERT_TYPE_INVALID should convert to 'invalid'");
    
    dap_pass_msg("Certificate type to string conversion passed");
}

/**
 * @brief Test NULL certificate handling
 */
static void test_cert_type_null_handling()
{
    dap_cert_type_t l_type = dap_cert_get_type(NULL);
    dap_assert_PIF(l_type == DAP_CERT_TYPE_INVALID, 
                   "NULL certificate should return DAP_CERT_TYPE_INVALID");
    
    dap_assert_PIF(!dap_cert_is_private(NULL), 
                   "dap_cert_is_private(NULL) should return false");
    
    dap_assert_PIF(!dap_cert_is_public(NULL), 
                   "dap_cert_is_public(NULL) should return false");
    
    dap_pass_msg("NULL certificate handling passed");
}

void init_test_case()
{
    dap_enc_key_init();
}

void cleanup_test_case()
{
    dap_enc_key_deinit();
}

void dap_cert_type_tests_run(void)
{
    dap_print_module_name("dap_cert_type");
    init_test_case();
    
    // Test NULL handling
    test_cert_type_null_handling();
    
    // Test type to string conversion
    test_cert_type_to_string();
    
    // Test with different key types
    dap_enc_key_type_t l_key_types[] = {
        DAP_ENC_KEY_TYPE_SIG_DILITHIUM,
        DAP_ENC_KEY_TYPE_SIG_FALCON,
        DAP_ENC_KEY_TYPE_SIG_SPHINCSPLUS,
        DAP_ENC_KEY_TYPE_SIG_BLISS,
        DAP_ENC_KEY_TYPE_SIG_PICNIC
    };
    
    size_t l_key_types_count = sizeof(l_key_types) / sizeof(l_key_types[0]);
    
    for (size_t i = 0; i < l_key_types_count; i++) {
        test_cert_type_private(l_key_types[i]);
        test_cert_type_public(l_key_types[i]);
        test_cert_signing_validation(l_key_types[i]);
    }
    
    cleanup_test_case();
}

