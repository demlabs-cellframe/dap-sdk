#include <stdio.h>
#include "dap_common.h"
#include "dap_enc_chipmunk.h"
#include "dap_enc_chipmunk_test.h"

#define LOG_TAG "chipmunk_test_main"

/**
 * @brief Entry point for Chipmunk unit tests
 * 
 * @return int Exit code (0 - success)
 */
int main(void) {
    // Initialize logging
    dap_log_level_set(L_DEBUG);
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    
    // Initialize Chipmunk module
    dap_enc_chipmunk_init();
    
    log_it(L_NOTICE, "Starting Chipmunk cryptographic module tests");
    
    // Run all Chipmunk tests
    int result = dap_enc_chipmunk_tests_run();
    
    // Report results
    if (result == 0) {
        log_it(L_NOTICE, "All Chipmunk cryptographic tests PASSED");
    } else {
        log_it(L_ERROR, "Some Chipmunk tests FAILED! Error code: %d", result);
    }
    
    return result;
} 
