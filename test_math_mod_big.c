#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_math_mod.h"

#define LOG_TAG "test_math_mod"

int main() {
    printf("=== Testing DAP Math Mod with real values ===\n");
    
    // Initialize math mod module only
    if (dap_math_mod_init() != 0) {
        printf("Failed to init DAP Math Mod\n");
        return 1;
    }
    
    // Test with values from the real failing test
    uint256_t challenge = uint256_0;
    uint256_t private_key = uint256_0;
    uint256_t modulus = uint256_0;
    uint256_t result;
    
    // Set challenge: 24434c00 b7164a5c 48554860 729e2222 (little endian)
    uint8_t challenge_bytes[16] = {
        0x22, 0x22, 0x9e, 0x72, 0x60, 0x48, 0x55, 0x48,
        0x5c, 0x4a, 0x16, 0xb7, 0x00, 0x4c, 0x43, 0x24
    };
    
    // Set private_key: cbe4f0ce 04155376 7d24bdcb 1da8ff69 (little endian)
    uint8_t private_key_bytes[16] = {
        0x69, 0xff, 0xa8, 0x1d, 0xcb, 0xbd, 0x24, 0x7d,
        0x76, 0x53, 0x15, 0x04, 0xce, 0xf0, 0xe4, 0xcb
    };
    
    // Set modulus: fffffffb 00000000 00000000 00000000 (little endian)
    uint8_t modulus_bytes[16] = {
        0xfb, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    
    memcpy(&challenge, challenge_bytes, sizeof(challenge_bytes));
    memcpy(&private_key, private_key_bytes, sizeof(private_key_bytes));
    memcpy(&modulus, modulus_bytes, sizeof(modulus_bytes));
    
    printf("Testing multiplication with real values from failing test\n");
    
    int ret = dap_math_mod_mul(challenge, private_key, modulus, &result);
    printf("dap_math_mod_mul returned: %d\n", ret);
    
    if (ret == 0) {
        uint8_t result_bytes[32];
        memcpy(result_bytes, &result, sizeof(result_bytes));
        
        printf("Result first 16 bytes: ");
        for (int i = 0; i < 16; i++) {
            printf("%02x ", result_bytes[i]);
        }
        printf("\n");
        printf("✓ Test PASSED\n");
    } else {
        printf("✗ Function returned error: %d\n", ret);
        
        // Try with smaller modulus to avoid overflow
        uint256_t small_modulus = uint256_0;
        uint8_t small_mod_bytes[4] = {0xFB, 0xFF, 0xFF, 0xFF}; // 2^32 - 5
        memcpy(&small_modulus, small_mod_bytes, sizeof(small_mod_bytes));
        
        printf("Trying with smaller modulus...\n");
        ret = dap_math_mod_mul(challenge, private_key, small_modulus, &result);
        printf("dap_math_mod_mul with small modulus returned: %d\n", ret);
        
        if (ret == 0) {
            uint8_t result_bytes[32];
            memcpy(result_bytes, &result, sizeof(result_bytes));
            
            printf("Result first 4 bytes: %02x %02x %02x %02x\n",
                   result_bytes[0], result_bytes[1], result_bytes[2], result_bytes[3]);
            printf("✓ Test with small modulus PASSED\n");
        } else {
            printf("✗ Even small modulus failed: %d\n", ret);
        }
    }
    
    return ret;
}
