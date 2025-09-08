#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_sdk.h"
#include "module/crypto/src/chipmunk/chipmunk.h"

int main() {
    printf("=== Testing Chipmunk keypair generation ===\n");
    
    // Initialize DAP
    if (dap_sdk_init_with_app_name("Test", 0xFFFFFFFF) != 0) {
        printf("Failed to init DAP SDK\n");
        return 1;
    }
    
    // Allocate buffers
    size_t pub_size = CHIPMUNK_PUBLIC_KEY_SIZE;
    size_t priv_size = CHIPMUNK_PRIVATE_KEY_SIZE;
    
    printf("CHIPMUNK_PUBLIC_KEY_SIZE = %d\n", CHIPMUNK_PUBLIC_KEY_SIZE);
    printf("CHIPMUNK_PRIVATE_KEY_SIZE = %d\n", CHIPMUNK_PRIVATE_KEY_SIZE);
    printf("CHIPMUNK_N = %d\n", CHIPMUNK_N);
    
    uint8_t *pub_key = calloc(1, pub_size);
    uint8_t *priv_key = calloc(1, priv_size);
    
    if (!pub_key || !priv_key) {
        printf("Failed to allocate memory\n");
        return 1;
    }
    
    printf("Calling chipmunk_keypair...\n");
    int result = chipmunk_keypair(pub_key, pub_size, priv_key, priv_size);
    printf("chipmunk_keypair returned: %d\n", result);
    
    if (result == 0) {
        printf("Public key first 32 bytes: ");
        for (int i = 0; i < 32 && i < pub_size; i++) {
            printf("%02x ", pub_key[i]);
        }
        printf("\n");
        
        printf("Public key size check: %zu bytes\n", pub_size);
        printf("Private key size check: %zu bytes\n", priv_size);
    }
    
    free(pub_key);
    free(priv_key);
    dap_sdk_deinit();
    
    return result;
}
