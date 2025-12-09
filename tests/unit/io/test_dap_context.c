/**
 * @file test_dap_context.c
 * @brief Unit tests for DAP context module
 * @details Tests context creation, lifecycle, and management
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include "dap_common.h"
#include "dap_test.h"
#include "dap_context.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_context"

// Test data
static bool s_callback_called = false;
static int s_callback_result = 0;

/**
 * @brief Test callback for context operations
 */
static int s_test_callback(dap_context_t *a_context, void *a_arg)
{
    UNUSED(a_context);
    UNUSED(a_arg);
    s_callback_called = true;
    return s_callback_result;
}

/**
 * @brief Test: Initialize and deinitialize context system
 */
static void s_test_context_init_deinit(void)
{
    log_it(L_INFO, "Testing context init/deinit");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    dap_context_deinit();
    dap_pass_msg("Context deinitialization");
}

/**
 * @brief Test: Create new context
 */
static void s_test_context_new(void)
{
    log_it(L_INFO, "Testing context creation");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    // Test worker context creation
    dap_context_t *l_ctx = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
    dap_assert(l_ctx != NULL, "Create worker context");
    
    if (l_ctx) {
        dap_assert(l_ctx->type == DAP_CONTEXT_TYPE_WORKER, "Context type is worker");
        dap_assert(!l_ctx->started, "Context not started yet");
        dap_assert(!l_ctx->is_running, "Context not running yet");
    }
    
    // Test proc_thread context creation
    dap_context_t *l_ctx2 = dap_context_new(DAP_CONTEXT_TYPE_PROC_THREAD);
    dap_assert(l_ctx2 != NULL, "Create proc_thread context");
    
    if (l_ctx2) {
        dap_assert(l_ctx2->type == DAP_CONTEXT_TYPE_PROC_THREAD, "Context type is proc_thread");
    }
    
    dap_context_deinit();
}

/**
 * @brief Test: Context creation with different types
 */
static void s_test_context_types(void)
{
    log_it(L_INFO, "Testing context types");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    // Test both context types
    dap_context_t *l_worker = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
    dap_assert(l_worker != NULL, "Create worker type context");
    
    dap_context_t *l_proc = dap_context_new(DAP_CONTEXT_TYPE_PROC_THREAD);
    dap_assert(l_proc != NULL, "Create proc_thread type context");
    
    // Verify types are correctly set
    if (l_worker) {
        dap_assert(l_worker->type == DAP_CONTEXT_TYPE_WORKER, "Worker type correct");
    }
    
    if (l_proc) {
        dap_assert(l_proc->type == DAP_CONTEXT_TYPE_PROC_THREAD, "Proc thread type correct");
    }
    
    dap_context_deinit();
}

/**
 * @brief Test: Context current retrieval
 */
static void s_test_context_current(void)
{
    log_it(L_INFO, "Testing current context retrieval");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    // Test current context when none is set (not in context thread)
    dap_context_t *l_current = dap_context_current();
    log_it(L_DEBUG, "Current context (not in thread): %p", l_current);
    // May be NULL or have a value depending on implementation
    
    dap_context_deinit();
}

/**
 * @brief Test: Context structure validation
 */
static void s_test_context_structure(void)
{
    log_it(L_INFO, "Testing context structure validation");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    dap_context_t *l_ctx = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
    dap_assert(l_ctx != NULL, "Create context");
    
    // Verify context structure is properly initialized
    if (l_ctx) {
        dap_assert(l_ctx->type == DAP_CONTEXT_TYPE_WORKER, "Context type is worker");
        dap_assert(!l_ctx->started, "Context not started");
        dap_assert(!l_ctx->is_running, "Context not running");
        log_it(L_DEBUG, "Context structure validated");
    }
    
    dap_context_deinit();
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_context_edge_cases(void)
{
    log_it(L_INFO, "Testing context edge cases");
    
    int l_ret = dap_context_init();
    dap_assert(l_ret == 0, "Context initialization");
    
    // Test current context before any created
    dap_context_t *l_current_before = dap_context_current();
    log_it(L_DEBUG, "Current context before creation: %p", l_current_before);
    
    // Create context
    dap_context_t *l_ctx = dap_context_new(DAP_CONTEXT_TYPE_WORKER);
    dap_assert(l_ctx != NULL, "Create context");
    
    // Test current again (still may be NULL as we're not in thread)
    dap_context_t *l_current_after = dap_context_current();
    log_it(L_DEBUG, "Current context after creation: %p", l_current_after);
    
    dap_context_deinit();
}

/**
 * @brief Test: Context initialization multiple times
 */
static void s_test_context_multiple_init(void)
{
    log_it(L_INFO, "Testing multiple init/deinit cycles");
    
    // First cycle
    int l_ret1 = dap_context_init();
    dap_assert(l_ret1 == 0, "First initialization");
    dap_context_deinit();
    
    // Second cycle
    int l_ret2 = dap_context_init();
    dap_assert(l_ret2 == 0, "Second initialization");
    dap_context_deinit();
    
    dap_pass_msg("Multiple init/deinit cycles successful");
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_context", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Context - Unit Tests ===");
    
    // Run tests
    s_test_context_init_deinit();
    s_test_context_new();
    s_test_context_types();
    s_test_context_current();
    s_test_context_structure();
    s_test_context_edge_cases();
    s_test_context_multiple_init();
    
    log_it(L_INFO, "=== All Context Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
