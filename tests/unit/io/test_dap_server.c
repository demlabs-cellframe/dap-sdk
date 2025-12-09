/**
 * @file test_dap_server.c
 * @brief Unit tests for DAP server module
 * @details Tests server creation, configuration, and lifecycle with mocking
 * @date 2025-11-21
 * @copyright (c) 2025 Cellframe Network
 */

#include <sys/socket.h>
#include "dap_common.h"
#include "dap_test.h"
#include "dap_server.h"
#include "dap_events_socket.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_mock.h"

#define LOG_TAG "test_dap_server"

// Mock adjacent DAP SDK modules to isolate dap_server
DAP_MOCK_DECLARE(dap_events_socket_init);
DAP_MOCK_DECLARE(dap_events_socket_deinit);
DAP_MOCK_DECLARE(dap_events_socket_wrap_listener);
DAP_MOCK_DECLARE(dap_worker_add_events_socket_auto);
DAP_MOCK_DECLARE(dap_events_worker_get_auto);

// Test data
static bool s_server_callback_called = false;

/**
 * @brief Test callback for server operations
 */
static void s_test_server_callback(dap_server_t *a_server, void *a_arg)
{
    UNUSED(a_server);
    UNUSED(a_arg);
    s_server_callback_called = true;
}

/**
 * @brief Test: Initialize and deinitialize server system
 */
static void s_test_server_init_deinit(void)
{
    log_it(L_INFO, "Testing server init/deinit");
    
    // Mock events socket (server depends on it)
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    dap_server_deinit();
    dap_pass_msg("Server deinitialization");
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Create new server
 */
static void s_test_server_new(void)
{
    log_it(L_INFO, "Testing server creation");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    // Prepare callbacks
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};
    
    // Test creating server
    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");
    
    if (l_server) {
        dap_server_delete(l_server);
        dap_pass_msg("Server deleted");
    }
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Server enabled status
 */
static void s_test_server_enabled(void)
{
    log_it(L_INFO, "Testing server enabled status");
    
    // Before init - should return false
    bool l_enabled_before = dap_server_enabled();
    dap_assert(!l_enabled_before, "Server not enabled before init");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    // After init - should return true
    bool l_enabled_after = dap_server_enabled();
    dap_assert(l_enabled_after, "Server enabled after init");
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Default server set/get
 */
static void s_test_server_default(void)
{
    log_it(L_INFO, "Testing default server");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    // Get default server (should be NULL initially)
    dap_server_t *l_default_before = dap_server_get_default();
    log_it(L_DEBUG, "Default server before set: %p", l_default_before);
    
    // Create and set default server
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};
    
    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");
    
    if (l_server) {
        dap_server_set_default(l_server);
        
        // Get default server (should be our server)
        dap_server_t *l_default_after = dap_server_get_default();
        dap_assert(l_default_after == l_server, "Default server matches");
        
        dap_server_delete(l_server);
    }
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Add listen address to server
 */
static void s_test_server_listen_addr_add(void)
{
    log_it(L_INFO, "Testing listen address addition");
    
    // Mock events socket and worker modules
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    // Create mock events socket for listener
    dap_events_socket_t l_mock_listener = {0};
    l_mock_listener.type = DESCRIPTOR_TYPE_SOCKET_LISTENING;
    DAP_MOCK_SET_RETURN(dap_events_socket_wrap_listener, &l_mock_listener);
    
    // Create mock worker
    dap_worker_t l_mock_worker = {0};
    DAP_MOCK_SET_RETURN(dap_worker_add_events_socket_auto, &l_mock_worker);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};
    
    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");
    
    if (l_server) {
        // Test adding TCP listen address
        dap_events_socket_callbacks_t l_listen_callbacks = {0};
        int l_add_ret = dap_server_listen_addr_add(l_server, "127.0.0.1", 8080,
                                                     DESCRIPTOR_TYPE_SOCKET_LISTENING,
                                                     &l_listen_callbacks);
        log_it(L_DEBUG, "Listen addr add returned: %d", l_add_ret);
        
        dap_server_delete(l_server);
    }
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
    DAP_MOCK_RESET(dap_events_socket_wrap_listener);
    DAP_MOCK_RESET(dap_worker_add_events_socket_auto);
}

/**
 * @brief Test: Set server callbacks
 */
static void s_test_server_callbacks_set(void)
{
    log_it(L_INFO, "Testing server callbacks setting");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    dap_events_socket_callbacks_t l_server_callbacks = {0};
    dap_events_socket_callbacks_t l_client_callbacks = {0};
    
    dap_server_t *l_server = dap_server_new("test_section",
                                             &l_server_callbacks,
                                             &l_client_callbacks);
    dap_assert(l_server != NULL, "Create server");
    
    if (l_server) {
        // Create new callbacks
        dap_events_socket_callbacks_t l_new_server_cb = {0};
        dap_events_socket_callbacks_t l_new_client_cb = {0};
        
        // Set new callbacks
        int l_set_ret = dap_server_callbacks_set(l_server, &l_new_server_cb, &l_new_client_cb);
        log_it(L_DEBUG, "Callbacks set returned: %d", l_set_ret);
        dap_assert(l_set_ret == 0, "Callbacks set successfully");
        
        dap_server_delete(l_server);
    }
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Edge cases and NULL checks
 */
static void s_test_server_edge_cases(void)
{
    log_it(L_INFO, "Testing server edge cases");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    // Test creating server with NULL section
    dap_events_socket_callbacks_t l_callbacks = {0};
    dap_server_t *l_server_null = dap_server_new(NULL, &l_callbacks, &l_callbacks);
    log_it(L_DEBUG, "Server with NULL section: %p", l_server_null);
    
    if (l_server_null) {
        dap_server_delete(l_server_null);
    }
    
    // Test setting NULL as default
    dap_server_set_default(NULL);
    dap_pass_msg("Set NULL default handled gracefully");
    
    // Test deleting NULL server
    dap_server_delete(NULL);
    dap_pass_msg("Delete NULL server handled gracefully");
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Test: Multiple servers
 */
static void s_test_multiple_servers(void)
{
    log_it(L_INFO, "Testing multiple servers");
    
    // Mock events socket
    DAP_MOCK_SET_RETURN(dap_events_socket_init, 0);
    
    int l_ret = dap_server_init();
    dap_assert(l_ret == 0, "Server initialization");
    
    // Create multiple servers
    dap_events_socket_callbacks_t l_callbacks = {0};
    
    dap_server_t *l_server1 = dap_server_new("test1", &l_callbacks, &l_callbacks);
    dap_server_t *l_server2 = dap_server_new("test2", &l_callbacks, &l_callbacks);
    dap_server_t *l_server3 = dap_server_new("test3", &l_callbacks, &l_callbacks);
    
    dap_assert(l_server1 != NULL, "Create server 1");
    dap_assert(l_server2 != NULL, "Create server 2");
    dap_assert(l_server3 != NULL, "Create server 3");
    
    // Cleanup
    if (l_server1) dap_server_delete(l_server1);
    if (l_server2) dap_server_delete(l_server2);
    if (l_server3) dap_server_delete(l_server3);
    
    dap_server_deinit();
    
    // Reset mocks
    DAP_MOCK_RESET(dap_events_socket_init);
}

/**
 * @brief Main test function
 */
int main(int argc, char **argv)
{
    UNUSED(argc);
    UNUSED(argv);
    
    // Initialize DAP SDK
    int l_ret = dap_common_init("test_dap_server", NULL);
    if (l_ret != 0) {
        printf("Failed to initialize DAP SDK\n");
        return 1;
    }
    
    // Initialize mock framework
    dap_mock_init();
    
    log_it(L_INFO, "=== DAP Server - Unit Tests ===");
    
    // Run tests
    s_test_server_init_deinit();
    s_test_server_new();
    s_test_server_enabled();
    s_test_server_default();
    s_test_server_listen_addr_add();
    s_test_server_callbacks_set();
    s_test_server_edge_cases();
    s_test_multiple_servers();
    
    log_it(L_INFO, "=== All Server Tests PASSED! ===");
    
    // Cleanup
    dap_mock_deinit();
    dap_common_deinit();
    
    return 0;
}
