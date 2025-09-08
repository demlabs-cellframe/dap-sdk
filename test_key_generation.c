#include <stdio.h>
#include <stdlib.h>
#include "dap_common.h"
#include "dap_enc_key.h"

#define LOG_TAG "test_key_generation"

int main() {
    printf("=== Testing Chipmunk Ring Key Generation ===\n");
    
    // Initialize DAP
    if (dap_sdk_init_with_app_name("Test", 0xFFFFFFFF) != 0) {
        printf("Failed to init DAP SDK\n");
        return 1;
    }
    
    // Generate a key
    printf("Generating key...\n");
    dap_enc_key_t* key = dap_enc_key_new_generate(DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING, NULL, 0, NULL, 0, 0);
    
    if (!key) {
        printf("✗ Key generation failed\n");
        return 1;
    }
    
    printf("Key generated successfully\n");
    printf("Key type: %d (expected: %d)\n", key->type, DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING);
    printf("Public key size: %zu\n", key->pub_key_data_size);
    printf("Private key size: %zu\n", key->priv_key_data_size);
    printf("Public key data: %p\n", key->pub_key_data);
    printf("Private key data: %p\n", key->priv_key_data);
    
    if (key->type == DAP_ENC_KEY_TYPE_SIG_CHIPMUNK_RING) {
        printf("✓ Key type is correct\n");
    } else {
        printf("✗ Key type is incorrect\n");
    }
    
    if (key->pub_key_data && key->priv_key_data) {
        printf("✓ Key data allocated\n");
    } else {
        printf("✗ Key data not allocated\n");
    }
    
    // Clean up
    dap_enc_key_delete(key);
    dap_sdk_deinit();
    
    return 0;
}
