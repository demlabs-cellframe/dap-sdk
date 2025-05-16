#include "dap_enc_chipmunk_test.h"
#include "dap_enc_key.h"
#include "dap_enc_chipmunk.h"
#include "dap_common.h"
#include "dap_test.h"
#include "rand/dap_rand.h"
#include "dap_sign.h"

#define LOG_TAG "dap_crypto_chipmunk_tests"

/**
 * @brief Test for Chipmunk multi-signature
 */
static void test_chipmunk_sign_verify(int a_times)
{
    dap_print_module_name("Chipmunk multi-signature");
    
    size_t l_seed_size = 32;
    uint8_t l_seed[l_seed_size];
    
    for (int i = 0; i < a_times; i++) {
        size_t l_source_size = 10 + random_uint32_t(20);
        uint8_t *l_source = DAP_NEW_SIZE(uint8_t, l_source_size);
        
        randombytes(l_seed, l_seed_size);
        randombytes(l_source, l_source_size);
        
        // Create key
        dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_seed, l_seed_size, 0);
        dap_assert(l_key != NULL, "Chipmunk key generation");
        
        // Calculate required signature buffer size
        size_t l_max_signature_size = dap_enc_chipmunk_calc_signature_size();
        dap_assert(l_max_signature_size > 0, "Chipmunk signature size must be positive");
        
        // Allocate signature buffer
        uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_max_signature_size);
        
        // Sign the message
        int l_sign_result = l_key->sign_get(l_key, l_source, l_source_size, l_sig, l_max_signature_size);
        dap_assert_PIF(l_sign_result == 0, "Chipmunk signature creation");
        
        // Verify the signature
        int l_verify_result = l_key->sign_verify(l_key, l_source, l_source_size, l_sig, l_max_signature_size);
        dap_assert_PIF(l_verify_result == 0, "Chipmunk signature verification");
        
        // Modify the message to check verification correctness
        if (l_source_size > 0) {
            l_source[0] ^= 0x01; // Invert one bit
            l_verify_result = l_key->sign_verify(l_key, l_source, l_source_size, l_sig, l_max_signature_size);
            dap_assert_PIF(l_verify_result != 0, "Check that modified message fails verification");
        }
        
        // Free resources
        DAP_DELETE(l_sig);
        DAP_DELETE(l_source);
        dap_enc_key_delete(l_key);
    }
    
    char l_pass_msg_buf[256];
    snprintf(l_pass_msg_buf, sizeof(l_pass_msg_buf), "Chipmunk signature and verification test performed %d times", a_times);
    dap_pass_msg(l_pass_msg_buf);
}

/**
 * @brief Test for Chipmunk key serialization and deserialization
 */
static void test_chipmunk_key_serialization(void)
{
    dap_print_module_name("Chipmunk key serialization");
    
    size_t l_seed_size = 32;
    uint8_t l_seed[l_seed_size];
    randombytes(l_seed, l_seed_size);
    
    // Create key
    dap_enc_key_t *l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK, NULL, 0, l_seed, l_seed_size, 0);
    dap_assert(l_key != NULL, "Chipmunk key generation");
    
    // Serialize public key
    size_t l_pub_key_data_size = 0;
    uint8_t *l_pub_key_data = dap_enc_key_serialize_pub_key(l_key, &l_pub_key_data_size);
    dap_assert(l_pub_key_data != NULL && l_pub_key_data_size > 0, "Chipmunk public key serialization");
    
    // Serialize private key
    size_t l_priv_key_data_size = 0;
    uint8_t *l_priv_key_data = dap_enc_key_serialize_priv_key(l_key, &l_priv_key_data_size);
    dap_assert(l_priv_key_data != NULL && l_priv_key_data_size > 0, "Chipmunk private key serialization");
    
    // Create new key
    dap_enc_key_t *l_key2 = dap_enc_key_new(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK);
    dap_assert(l_key2 != NULL, "Creating new Chipmunk key");
    
    // Deserialize keys
    int l_result_pub = dap_enc_key_deserialize_pub_key(l_key2, l_pub_key_data, l_pub_key_data_size);
    dap_assert(l_result_pub == 0, "Chipmunk public key deserialization");
    
    int l_result_priv = dap_enc_key_deserialize_priv_key(l_key2, l_priv_key_data, l_priv_key_data_size);
    dap_assert(l_result_priv == 0, "Chipmunk private key deserialization");
    
    // Check that keys match
    dap_assert(l_key->priv_key_data_size == l_key2->priv_key_data_size, "Private key sizes must match");
    dap_assert(l_key->pub_key_data_size == l_key2->pub_key_data_size, "Public key sizes must match");
    
    // Check that signature works with deserialized key
    size_t l_source_size = 32;
    uint8_t *l_source = DAP_NEW_SIZE(uint8_t, l_source_size);
    randombytes(l_source, l_source_size);
    
    size_t l_max_signature_size = dap_enc_chipmunk_calc_signature_size();
    uint8_t *l_sig = DAP_NEW_Z_SIZE(uint8_t, l_max_signature_size);
    
    int l_sign_result = l_key->sign_get(l_key, l_source, l_source_size, l_sig, l_max_signature_size);
    dap_assert_PIF(l_sign_result == 0, "Signature creation with original key");
    
    int l_verify_result = l_key2->sign_verify(l_key2, l_source, l_source_size, l_sig, l_max_signature_size);
    dap_assert_PIF(l_verify_result == 0, "Signature verification with deserialized key");
    
    // Free resources
    DAP_DELETE(l_sig);
    DAP_DELETE(l_source);
    DAP_DELETE(l_pub_key_data);
    DAP_DELETE(l_priv_key_data);
    dap_enc_key_delete(l_key);
    dap_enc_key_delete(l_key2);
    
    dap_pass_msg("Chipmunk key serialization and deserialization test completed successfully");
}

/**
 * @brief Run all tests for the Chipmunk algorithm
 * @param a_times Number of test repetitions
 */
void dap_enc_chipmunk_tests_run(int a_times)
{
    test_chipmunk_sign_verify(a_times);
    test_chipmunk_key_serialization();
} 