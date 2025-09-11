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
    dap_enc_key_t* key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    assert(key != NULL);
    assert(key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING);

    log_it(L_INFO, "Key generated successfully: pub_key_data=%p, priv_key_data=%p",
           key->pub_key_data, key->priv_key_data);

    // Verify that pointers are valid
    assert(key->pub_key_data != NULL);
    assert(key->priv_key_data != NULL);

    // Free the key
    dap_enc_key_delete(key);
    log_it(L_INFO, "Key freed successfully");
}

static void test_minimal_ring_signature() {
    log_it(L_INFO, "Testing minimal ring signature...");

    // Generate keys
    dap_enc_key_t* signer_key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    assert(signer_key != NULL);

    dap_enc_key_t* ring_keys[2];
    ring_keys[0] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    ring_keys[1] = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);
    assert(ring_keys[0] != NULL);
    assert(ring_keys[1] != NULL);

    log_it(L_INFO, "Keys generated successfully");

    // Create a simple message
    const char* message = "test";
    size_t message_len = strlen(message);

    // Debug key state before signing
    log_it(L_INFO, "Signer key: pub_key_data=%p, priv_key_data=%p", signer_key->pub_key_data, signer_key->priv_key_data);
    log_it(L_INFO, "Ring key 0: pub_key_data=%p, priv_key_data=%p", ring_keys[0]->pub_key_data, ring_keys[0]->priv_key_data);
    log_it(L_INFO, "Ring key 1: pub_key_data=%p, priv_key_data=%p", ring_keys[1]->pub_key_data, ring_keys[1]->priv_key_data);

    // Create signature
    log_it(L_INFO, "Creating signature...");
    dap_sign_t* signature = dap_sign_create_ring(signer_key, (uint8_t*)message, message_len, ring_keys, 2, 1);
    assert(signature != NULL);

    log_it(L_INFO, "Signature created successfully");

    // Free everything
    DAP_DELETE(signature);

    dap_enc_key_delete(signer_key);
    dap_enc_key_delete(ring_keys[0]);
    dap_enc_key_delete(ring_keys[1]);

    log_it(L_INFO, "All memory freed successfully");
}

int main(int argc, char* argv[]) {
    log_it(L_INFO, "Starting minimal Chipmunk Ring test...");

    // Initialize DAP SDK
    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    // Test minimal key generation
    test_minimal_key_generation();

    // Test minimal ring signature
    test_minimal_ring_signature();

    // Cleanup
    dap_test_sdk_cleanup();

    log_it(L_INFO, "Minimal test completed successfully");
    return 0;
}
