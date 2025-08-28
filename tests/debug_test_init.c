#include "test_helpers.h"
#include "dap_common.h"
#include <stdio.h>

int main() {
    printf("=== Debug Test: SDK Initialization ===\n");
    
    printf("1. Before dap_test_sdk_init...\n");
    fflush(stdout);
    
    int ret = dap_test_sdk_init();
    
    printf("2. dap_test_sdk_init returned: %d\n", ret);
    fflush(stdout);
    
    if (ret != 0) {
        printf("FAILED: dap_test_sdk_init returned error\n");
        fflush(stdout);
        return -1;
    }
    
    printf("3. SUCCESS: SDK initialized\n");
    printf("4. Before cleanup...\n");
    fflush(stdout);
    
    dap_test_sdk_cleanup();
    
    printf("5. SUCCESS: SDK cleaned up\n");
    fflush(stdout);
    
    return 0;
}
