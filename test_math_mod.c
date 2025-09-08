#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dap_common.h"
#include "dap_math_mod.h"

// Детальное логирование для отладки
static bool s_debug_more = true;

// Макрос для детального логирования
#define debug_if(condition, level, ...) if (condition) log_it(level, __VA_ARGS__)

#define LOG_TAG "test_math_mod"

int main() {
    printf("=== Testing DAP Math Mod functions ===\n");
    
    // Initialize DAP
    if (dap_sdk_init_with_app_name("Test", 0xFFFFFFFF) != 0) {
        printf("Failed to init DAP SDK\n");
        return 1;
    }
    
    // Initialize math mod module
    if (dap_math_mod_init() != 0) {
        printf("Failed to init DAP Math Mod\n");
        return 1;
    }
    
    // Test simple multiplication
    uint256_t a = {{0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}; // 1
    uint256_t b = {{0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}; // 2
    uint256_t modulus = {{0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}; // 10
    uint256_t result;
    
    debug_if(s_debug_more, L_INFO, "Testing simple multiplication: 1 * 2 mod 10");
    
    int ret = dap_math_mod_mul(a, b, modulus, &result);
    printf("dap_math_mod_mul returned: %d\n", ret);
    
    if (ret == 0) {
        printf("Result: %08x %08x %08x %08x %08x %08x %08x %08x\n",
               result.data[0], result.data[1], result.data[2], result.data[3],
               result.data[4], result.data[5], result.data[6], result.data[7]);
        
        // Expected result: 1 * 2 % 10 = 2
        if (result.data[0] == 0x02) {
            printf("✓ Test PASSED\n");
        } else {
            printf("✗ Test FAILED - expected 2, got %u\n", result.data[0]);
        }
    } else {
        printf("✗ Function returned error: %d\n", ret);
    }
    
    dap_sdk_deinit();
    return ret;
}
