#include <dap_test.h>
#include <dap_common.h>
#include <dap_sign.h>

#define LOG_TAG "test_sign_api"

// Declare our test function
void dap_sign_test_run(void);

int main(void) {
    // Initialize logging
    dap_log_level_set(L_INFO);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_set_format(DAP_LOG_FORMAT_NO_PREFIX);
    
    log_it(L_INFO, "=== Testing Universal Signature API ===");
    
    // Run our universal signature API tests
    dap_sign_test_run();
    
    log_it(L_INFO, "=== Universal Signature API Tests Completed ===");
    
    return 0;
} 