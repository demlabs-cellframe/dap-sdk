#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_math_mod.h"

#define LOG_TAG "test_math_mod"

int main() {
    printf("=== Testing DAP Math Mod functions ===\n");
    
    // Initialize math mod module only
    if (dap_math_mod_init() != 0) {
        printf("Failed to init DAP Math Mod\n");
        return 1;
    }
    
    // Test simple multiplication using the same approach as chipmunk_ring.c
    uint256_t a = uint256_0;
    uint256_t b = uint256_0;
    uint256_t modulus = uint256_0;
    uint256_t result;
    
    // Set values using memcpy like in the real code
    uint8_t a_val[4] = {0x01, 0x00, 0x00, 0x00}; // 1
    uint8_t b_val[4] = {0x02, 0x00, 0x00, 0x00}; // 2
    uint8_t mod_val[4] = {0x0A, 0x00, 0x00, 0x00}; // 10
    
    memcpy(&a, a_val, sizeof(a_val));
    memcpy(&b, b_val, sizeof(b_val));
    memcpy(&modulus, mod_val, sizeof(mod_val));
    
    printf("Testing simple multiplication: 1 * 2 mod 10\n");
    
    int ret = dap_math_mod_mul(a, b, modulus, &result);
    printf("dap_math_mod_mul returned: %d\n", ret);
    
    if (ret == 0) {
        uint8_t result_bytes[32];
        memcpy(result_bytes, &result, sizeof(result_bytes));
        
        printf("Result first 4 bytes: %02x %02x %02x %02x\n",
               result_bytes[0], result_bytes[1], result_bytes[2], result_bytes[3]);
        
        // Expected result: 1 * 2 % 10 = 2
        if (result_bytes[0] == 0x02) {
            printf("✓ Test PASSED\n");
        } else {
            printf("✗ Test FAILED - expected 2, got %u\n", result_bytes[0]);
        }
    } else {
        printf("✗ Function returned error: %d\n", ret);
    }
    
    return ret;
}
