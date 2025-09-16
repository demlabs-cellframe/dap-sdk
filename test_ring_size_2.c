#include <stdio.h>
#include <dap_common.h>
#include <dap_sign.h>
#include <dap_enc_key.h>
#include <dap_enc_chipmunk_ring.h>

int main() {
    dap_log_level_set(L_DEBUG);
    
    // Test with ring_size=2 (same as performance test)
    struct dap_enc_key* keys[2];
    for(int i = 0; i < 2; i++) {
        keys[i] = dap_enc_key_new(DAP_ENC_KEY_TYPE_CHIPMUNK_RING);
        if(!keys[i]) {
            printf("Failed to create key %d\n", i);
            return -1;
        }
        dap_enc_key_generate(keys[i], NULL, 0, NULL, 0, 0);
    }
    
    char message[] = "test";
    dap_sign_t* signature = dap_sign_create_ring(keys[0], message, sizeof(message), keys, 2, 1);
    
    if(signature) {
        printf("SUCCESS: ring_size=2 works!\n");
        DAP_DELETE(signature);
    } else {
        printf("FAILED: ring_size=2 does not work!\n");
    }
    
    for(int i = 0; i < 2; i++) {
        dap_enc_key_delete(keys[i]);
    }
    return 0;
}
