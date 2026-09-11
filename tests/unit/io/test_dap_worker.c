/**
 * @file test_dap_worker.c
 * @brief Unit tests for DAP worker module
 * @details Tests worker creation, socket management, and async operations with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <pthread.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_worker.h"
#include "dap_context.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_worker"

// Mock adjacent DAP SDK modules to isolate dap_worker
DAP_MOCK(dap_events_init);
DAP_MOCK(dap_events_deinit);
DAP_MOCK(dap_events_worker_get);
DAP_MOCK(dap_context_init);
DAP_MOCK(dap_context_deinit);
DAP_MOCK(dap_context_new);
DAP_MOCK(dap_events_socket_init);
DAP_MOCK(dap_events_socket_deinit);
DAP_MOCK(dap_events_socket_create);
DAP_MOCK(dap_events_socket_delete_unsafe);

// Test data
static bool s_worker_callback_called = false;

/**
 * @brief Test callback for worker operations
 */
static void s_test_worker_callback(void *a_arg)
{
    UNUSED(a_arg);
    s_worker_callback_called = true;
    log_it(L_DEBUG, "Worker callback executed");
}

/**
 * @brief Test: Initialize and deinitialize worker system
 */
static void s_test_worker_init_deinit(void)
{
    log_it(L_INFO, "Testing worker init/deinit");
    
    // Note: Worker init is typically called as part of events system
    // Testing it directly may require mocking context creation
    
    // Mock context module (worker depends on it)
    DAP_MOCK_SET_RETURN(dap_context_init, 0);
    
    size_t l_conn_timeout = 60;
    int l_ret = dap_worker_init(l_conn_timeout);
    dap_assert(l_ret == 0, "Worker initialization");
    
    dap_worker_deinit();
    dap_pass_msg("Worker deinitialization");
    
    // Reset mocks
    DAP_MOCK_RESET(dap_context_init);
}

/**
 * @brief Test: Get current worker
 */
static void s_test_worker_get_current(void)
{
    log_it(L_INFO, "Testing get current worker");
    
    /* confcall W59-R7.1: DAP_MOCK(func) on a function that has no
     * DAP_MOCK_WRAPPER_CUSTOM in this file is never actually linker-wrapped
     * by this mock framework (dap_mock_autowrap only auto-generates
     * wrappers for DAP_MOCK_DECLARE/_CUSTOM usages that also define a
     * DAP_MOCK_WRAPPER_CUSTOM body; the short DAP_MOCK() form used here
     * produces an unused warning template on Linux) — every "mocked" call
     * below always ran the real dap_events_init/dap_worker_get_current.
     * Exercise the real reactor instead of pretending it is mocked. */
    int l_ret = dap_events_init(2, 60);
    if (l_ret == 0) {
        dap_assert(dap_events_start() == 0, "Events started");
        // Get current worker (NULL outside a worker thread — this is the
        // test/main thread, so NULL is the correct, expected result)
        dap_worker_t *l_current = dap_worker_get_current();
        log_it(L_DEBUG, "Current worker: %p", l_current);
        dap_assert(l_current == NULL, "Current worker is NULL outside a worker thread");
        
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
    }
}

/**
 * @brief Test: Execute callback on worker
 */
static void s_test_worker_exec_callback(void)
{
    log_it(L_INFO, "Testing worker callback execution");
    
    int l_ret = dap_events_init(2, 60);
    if (l_ret == 0) {
        dap_assert(dap_events_start() == 0, "Events started");
        // Get a REAL worker (the mocked events_worker_get above never ran)
        dap_worker_t *l_worker = dap_events_worker_get(0);
        dap_assert(l_worker != NULL, "Get worker 0");
        
        if (l_worker) {
            // Execute callback on worker and actually wait for it to run —
            // dap_worker_exec_callback_on posts to the worker's queue; the
            // worker thread is alive now, so the post is real and should
            // be observable instead of "queued and never verified".
            s_worker_callback_called = false;
            int l_post_rc = dap_worker_exec_callback_on(l_worker, s_test_worker_callback, NULL);
            dap_assert(l_post_rc == 0, "Callback post accepted");
            for (int i = 0; i < 200 && !s_worker_callback_called; i++)
                usleep(10000);
            dap_assert(s_worker_callback_called, "Callback executed on worker thread");
        }
        
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
    }
}

/**
 * @brief Test: Add events socket to worker
 */
static void s_test_worker_add_events_socket(void)
{
    log_it(L_INFO, "Testing add events socket to worker");
    
    int l_ret = dap_events_init(2, 60);
    if (l_ret == 0) {
        /* dap_worker_add_events_socket_auto() → dap_events_worker_get_auto()
         * dereferences s_workers[i]->context, which start() populates. */
        dap_assert(dap_events_start() == 0, "Events started");
        // Initialize events socket system
        dap_events_socket_init();
        
        // Create an events socket
        dap_events_socket_callbacks_t l_callbacks = {0};
        dap_events_socket_t *l_es = dap_events_socket_create(
            DESCRIPTOR_TYPE_QUEUE, &l_callbacks);
        
        if (l_es) {
            // Get auto worker
            dap_worker_t *l_worker = dap_worker_add_events_socket_auto(l_es);
            log_it(L_DEBUG, "Auto worker assigned: %p", l_worker);
            dap_assert(l_worker != NULL, "Auto worker assigned after start");
            
            // Cleanup
            dap_events_socket_delete_unsafe(l_es, false);
        }
        
        dap_events_socket_deinit();
        dap_events_stop_all();
        dap_events_wait();
        dap_events_deinit();
    }
}

/**
 * @brief Test: Worker context callbacks
 */
static void s_test_worker_context_callbacks(void)
{
    log_it(L_INFO, "Testing worker context callbacks");
    
    /* confcall W59-R7.1: dap_worker_context_callback_started/_stopped are
     * internal reactor hooks — both assert() a non-NULL dap_worker_t arg
     * (dap_worker_context_callback_stopped also dereferences
     * a_context->event_exit, which only dap_context_run's thread sets up).
     * They are documented as callback_arg for dap_context_run, never meant
     * to be called ad hoc with NULL — the previous version of this test did
     * exactly that and only "passed" because the mocked-out real dap_events
     * calls made every earlier assignment in this test a no-op too.
     * Exercise the real pairing: init → start (which drives these hooks on
     * the worker thread) → stop → wait → deinit. */
    int l_ret = dap_events_init(1, 60);
    dap_assert(l_ret == 0, "Events initialization");
    dap_assert(dap_events_start() == 0, "Events started (drives context callbacks)");
    dap_worker_t *l_worker = dap_events_worker_get(0);
    dap_assert(l_worker != NULL, "Worker present after start");
    dap_events_stop_all();
    dap_events_wait();
    dap_events_deinit();
}

/**
 * @brief Test: Worker esocket polling check
 */
static void s_test_worker_check_esocket_polled(void)
{
    log_it(L_INFO, "Testing worker esocket polling check");
    
    // Test check function (may return false if not in worker context)
        // bool l_polled = dap_worker_check_esocket_polled_now(l_worker, l_es); // Function not exported
        //     log_it(L_DEBUG, "Esocket polled now: %d", l_is_polled);
        //     // Not failing - just checking the function works
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_worker_edge_cases(void)
{
    log_it(L_INFO, "Testing worker edge cases");
    
    // Test with NULL worker
    dap_worker_exec_callback_on(NULL, s_test_worker_callback, NULL);
    dap_pass_msg("NULL worker callback handled gracefully");
    
    // Test with NULL callback
    dap_worker_exec_callback_on(NULL, NULL, NULL);
    dap_pass_msg("NULL callback handled gracefully");
    
    // Test add events socket with NULL
    dap_worker_t *l_worker = dap_worker_add_events_socket_auto(NULL);
    dap_assert(l_worker == NULL, "NULL events socket returns NULL");
}

/**
 * @brief Test: Worker initialization with different parameters
 */
static void s_test_worker_init_params(void)
{
    log_it(L_INFO, "Testing worker initialization with different parameters");
    
    // Mock system calls
    
    // Test with zero timeout
    int l_ret = dap_worker_init(0);
    log_it(L_DEBUG, "Worker init with timeout=0 returned: %d", l_ret);
    dap_worker_deinit();
    
    // Test with large timeout
    l_ret = dap_worker_init(3600);
    log_it(L_DEBUG, "Worker init with timeout=3600 returned: %d", l_ret);
    dap_worker_deinit();
    
    // Reset mocks
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_worker", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Worker - Unit Tests ===");
    
    // Run tests
    s_test_worker_init_deinit();
    s_test_worker_get_current();
    s_test_worker_exec_callback();
    s_test_worker_add_events_socket();
    s_test_worker_context_callbacks();
    s_test_worker_check_esocket_polled();
    s_test_worker_edge_cases();
    s_test_worker_init_params();
    
    log_it(L_INFO, "=== All Worker Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}

