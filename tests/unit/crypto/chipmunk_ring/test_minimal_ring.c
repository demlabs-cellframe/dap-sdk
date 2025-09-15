/**
 * @brief Minimal test to isolate heap corruption in Chipmunk Ring
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define LOG_TAG "test_minimal_ring"

#include <test_helpers.h>
#include "dap_enc_chipmunk_ring.h"
#include "dap_sign.h"

static void test_minimal_key_generation() {
    log_it(L_INFO, "Testing minimal key generation...");

    // Generate a simple key
    dap_enc_key_t* l_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    assert(l_key != NULL);
    assert(l_key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING);

    log_it(L_INFO, "Key generated successfully: pub_key_data=%p, priv_key_data=%p",
           l_key->pub_key_data, l_key->priv_key_data);

    // Verify that pointers are valid
    assert(l_key->pub_key_data != NULL);
    assert(l_key->priv_key_data != NULL);

    // Free the key
    dap_enc_key_delete(l_key);
    log_it(L_INFO, "Key freed successfully");
}

static int test_minimal_ring_signature() {
    log_it(L_INFO, "Testing minimal ring signature...");

    // Generate keys - signer must be part of the ring
    dap_enc_key_t* l_signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    if (!l_signer_key) {
        log_it(L_ERROR, "Failed to generate signer key");
        return -1;
    }

    dap_enc_key_t* l_ring_keys[2];
    l_ring_keys[0] = l_signer_key;  // Signer is part of the ring (critical for anonymity)
    l_ring_keys[1] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    if (!l_ring_keys[1]) {
        log_it(L_ERROR, "Failed to generate ring key");
        dap_enc_key_delete(l_signer_key);
        return -1;
    }

    log_it(L_INFO, "Keys generated successfully");

    // Create a simple message
    const char* l_message = "test";
    size_t l_message_len = strlen(l_message);

    // Debug key state before signing
    log_it(L_INFO, "Signer key: pub_key_data=%p, priv_key_data=%p", l_signer_key->pub_key_data, l_signer_key->priv_key_data);
    log_it(L_INFO, "Ring key 0: pub_key_data=%p, priv_key_data=%p", l_ring_keys[0]->pub_key_data, l_ring_keys[0]->priv_key_data);
    log_it(L_INFO, "Ring key 1: pub_key_data=%p, priv_key_data=%p", l_ring_keys[1]->pub_key_data, l_ring_keys[1]->priv_key_data);

    // Create signature
    log_it(L_INFO, "Creating signature...");
    dap_sign_t* l_signature = dap_sign_create_ring(l_signer_key, (uint8_t*)l_message, l_message_len, l_ring_keys, 2, 1);
    
    // FAIL FAST: Return error immediately if signature creation fails
    if (!l_signature) {
        log_it(L_ERROR, "Ring signature creation failed - ChipmunkRing implementation has errors");
        dap_enc_key_delete(l_signer_key);
        dap_enc_key_delete(l_ring_keys[1]);
        return -1;  // Return error code immediately
    }

    log_it(L_INFO, "Signature created successfully");

    // Free everything
    DAP_DELETE(l_signature);

    dap_enc_key_delete(l_signer_key);  // Only delete signer key once (it's ring_keys[0])
    dap_enc_key_delete(l_ring_keys[1]);  // Delete only the additional ring key

    log_it(L_INFO, "All memory freed successfully");
    return 0;  // Success
}

int main(int argc, char* argv[]) {
    log_it(L_INFO, "Starting minimal Chipmunk Ring test...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }
    
    if (dap_enc_chipmunk_ring_init() != 0) {
        log_it(L_ERROR, "Failed to initialize ChipmunkRing");
        dap_test_sdk_cleanup();
        return -1;
    }

    // Test minimal key generation (void function - uses assert internally)
    test_minimal_key_generation();

    // Test minimal ring signature - FAIL FAST if error
    int l_signature_result = test_minimal_ring_signature();
    if (l_signature_result != 0) {
        log_it(L_ERROR, "Ring signature test failed with error %d", l_signature_result);
        dap_test_sdk_cleanup();
        return l_signature_result;  // Propagate error code
    }

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_INFO, "Minimal test completed successfully");
    return 0;
}
