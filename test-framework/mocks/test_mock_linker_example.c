/**
 * @file test_mock_linker_example.c
 * @brief Example of using linker wrapping for function mocking
 * @details Demonstrates how to use --wrap linker option with DAP mock framework
 * 
 * Build with:
 * gcc test_mock_linker_example.c -Wl,--wrap=dap_common_init -Wl,--wrap=dap_config_get_item_str
 * 
 * @date 2025-10-26
 * @copyright (c) 2025 Cellframe Network
 */

#include <assert.h>
#include <string.h>
#include "dap_mock_framework.h"
#include "dap_mock_linker_wrapper.h"

#define LOG_TAG "test_mock_example"

// ============================================================================
// Mock Declarations
// ============================================================================

DAP_MOCK_DECLARE(dap_common_init);
DAP_MOCK_DECLARE(dap_config_get_item_str);

// ============================================================================
// Linker Wrappers - These replace real functions
// ============================================================================

/**
 * Wrapper for dap_common_init
 * Linker will redirect all calls to dap_common_init() to this function
 */
DAP_MOCK_WRAPPER_INT(dap_common_init,
    (const char *a_app_name, const char *a_log_file),
    (a_app_name, a_log_file))

/**
 * Wrapper for dap_config_get_item_str
 * Returns mock value or forwards to real function
 */
DAP_MOCK_WRAPPER_PTR(dap_config_get_item_str,
    (void *a_config, const char *a_section, const char *a_key),
    (a_config, a_section, a_key))

// ============================================================================
// Tests
// ============================================================================

/**
 * Test that mock is called instead of real function
 */
void test_mock_called_instead_of_real(void) {
    log_it(L_INFO, "TEST: Mock intercepts real function call");
    
    dap_mock_framework_init();
    
    // Register and enable mock
    g_mock_dap_common_init = dap_mock_register("dap_common_init");
    dap_mock_set_enabled(g_mock_dap_common_init, true);
    DAP_MOCK_SET_RETURN(dap_common_init, 0);
    
    // Call function - linker redirects to __wrap_dap_common_init
    int l_ret = dap_common_init("test_app", NULL);
    
    // Verify mock was called
    assert(dap_mock_get_call_count(g_mock_dap_common_init) == 1);
    assert(l_ret == 0);
    
    dap_mock_framework_deinit();
    
    log_it(L_INFO, "✅ PASS: Mock intercepted call");
}

/**
 * Test that real function is called when mock disabled
 */
void test_real_function_called_when_mock_disabled(void) {
    log_it(L_INFO, "TEST: Real function called when mock disabled");
    
    dap_mock_framework_init();
    
    // Register but don't enable mock
    g_mock_dap_common_init = dap_mock_register("dap_common_init");
    dap_mock_set_enabled(g_mock_dap_common_init, false);
    
    // Call function - should forward to __real_dap_common_init
    int l_ret = dap_common_init("test_app", NULL);
    
    // Mock should not have recorded the call
    assert(dap_mock_get_call_count(g_mock_dap_common_init) == 0);
    
    dap_mock_framework_deinit();
    
    log_it(L_INFO, "✅ PASS: Real function was called");
}

/**
 * Test mock return values
 */
void test_mock_return_values(void) {
    log_it(L_INFO, "TEST: Mock return values");
    
    dap_mock_framework_init();
    
    g_mock_dap_common_init = dap_mock_register("dap_common_init");
    dap_mock_set_enabled(g_mock_dap_common_init, true);
    
    // Test different return values
    DAP_MOCK_SET_RETURN(dap_common_init, 42);
    assert(dap_common_init("test", NULL) == 42);
    
    DAP_MOCK_SET_RETURN(dap_common_init, -1);
    assert(dap_common_init("test", NULL) == -1);
    
    DAP_MOCK_SET_RETURN(dap_common_init, 0);
    assert(dap_common_init("test", NULL) == 0);
    
    dap_mock_framework_deinit();
    
    log_it(L_INFO, "✅ PASS: Return values controlled by mock");
}

/**
 * Test pointer return values
 */
void test_mock_pointer_return(void) {
    log_it(L_INFO, "TEST: Mock pointer return");
    
    dap_mock_framework_init();
    
    g_mock_dap_config_get_item_str = dap_mock_register("dap_config_get_item_str");
    dap_mock_set_enabled(g_mock_dap_config_get_item_str, true);
    
    // Mock returns our test string
    const char *l_test_value = "mock_value_123";
    DAP_MOCK_SET_RETURN_PTR(dap_config_get_item_str, l_test_value);
    
    const char *l_result = (const char*)dap_config_get_item_str(NULL, "section", "key");
    
    assert(l_result == l_test_value);
    assert(strcmp(l_result, "mock_value_123") == 0);
    
    dap_mock_framework_deinit();
    
    log_it(L_INFO, "✅ PASS: Pointer return value works");
}

/**
 * Test argument verification
 */
void test_argument_verification(void) {
    log_it(L_INFO, "TEST: Argument verification");
    
    dap_mock_framework_init();
    
    g_mock_dap_common_init = dap_mock_register("dap_common_init");
    dap_mock_set_enabled(g_mock_dap_common_init, true);
    DAP_MOCK_SET_RETURN(dap_common_init, 0);
    
    const char *l_app_name = "my_test_app";
    dap_common_init(l_app_name, NULL);
    
    // Verify first argument
    dap_mock_call_record_t *l_last_call = dap_mock_get_last_call(g_mock_dap_common_init);
    assert(l_last_call != NULL);
    assert(l_last_call->args[0] == (void*)l_app_name);
    
    dap_mock_framework_deinit();
    
    log_it(L_INFO, "✅ PASS: Arguments recorded correctly");
}

// ============================================================================
// Main
// ============================================================================

int main(void) {
    log_it(L_INFO, "===========================================");
    log_it(L_INFO, "LINKER WRAPPING MOCK EXAMPLE");
    log_it(L_INFO, "===========================================\n");
    
    test_mock_called_instead_of_real();
    test_real_function_called_when_mock_disabled();
    test_mock_return_values();
    test_mock_pointer_return();
    test_argument_verification();
    
    log_it(L_INFO, "\n===========================================");
    log_it(L_INFO, "ALL TESTS PASSED ✅");
    log_it(L_INFO, "===========================================");
    
    return 0;
}

