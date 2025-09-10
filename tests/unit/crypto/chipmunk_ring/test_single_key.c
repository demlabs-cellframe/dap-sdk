/**
 * @brief Single key test to isolate memory corruption
 */

#define LOG_TAG "test_single_key"

#include "../../../../fixtures/utilities/test_helpers.h"
#include "../../../../module/crypto/src/dap_enc_chipmunk_ring.h"
#include "../../../../module/crypto/src/dap_sign.h"

static bool test_single_key_generation() {
    log_it(L_INFO, "Testing single Chipmunk_Ring key generation...");

    // Generate a single key
    dap_enc_key_t* key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 256);

    if (!key) {
        log_it(L_ERROR, "Failed to generate key");
        return false;
    }

    if (!key->pub_key_data) {
        log_it(L_ERROR, "Public key data is NULL");
        dap_enc_key_delete(key);
        return false;
    }

    if (!key->priv_key_data) {
        log_it(L_ERROR, "Private key data is NULL");
        dap_enc_key_delete(key);
        return false;
    }

    log_it(L_INFO, "Key generated successfully: pub=%p, priv=%p", key->pub_key_data, key->priv_key_data);

    // Test signing with single key in ring
    const char* message = "test message";
    size_t message_len = strlen(message);

    // Create ring with just one key
    dap_enc_key_t* ring_keys[1] = {key};

    dap_sign_t* signature = dap_sign_create_ring(key, (uint8_t*)message, message_len, ring_keys, 1);

    if (signature) {
        log_it(L_INFO, "Single-key ring signature created successfully");
        DAP_DELETE(signature);
    } else {
        log_it(L_ERROR, "Failed to create single-key ring signature");
    }

    dap_enc_key_delete(key);
    log_it(L_INFO, "Single key test completed");
    return signature != NULL;
}

int main(int argc, char** argv) {
    UNUSED(argc);
    UNUSED(argv);

    if (dap_test_sdk_init() != 0) {
        log_it(L_ERROR, "Failed to initialize DAP SDK");
        return -1;
    }

    bool result = test_single_key_generation();

    dap_test_sdk_cleanup();

    return result ? 0 : -1;
}

