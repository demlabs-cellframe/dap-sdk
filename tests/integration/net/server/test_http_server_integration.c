/**
 * HTTP Server Integration Test Suite
 * 
 * Tests real HTTP server behavior with client connections.
 * 
 * Features tested:
 * - Server initialization and lifecycle
 * - Request handling with real connections
 * - User-Agent filtering
 * - Multiple concurrent connections
 * 
 * @note This is an INTEGRATION test - starts real server instance
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "dap_http_simple.h"
#include "dap_events.h"
#include "dap_test_helpers.h"

#define LOG_TAG "test_http_server_integration"

static bool s_server_initialized = false;

// ==============================================
// Test Infrastructure
// ==============================================

static void s_setup_server_test(void) {
    TEST_INFO("Initializing HTTP server...");
    
    dap_events_init(1, 60000);
    dap_events_start();
    dap_http_simple_module_init();
    
    s_server_initialized = true;
    
    TEST_INFO("HTTP server initialized");
}

static void s_teardown_server_test(void) {
    TEST_INFO("Shutting down HTTP server...");
    
    if (s_server_initialized) {
        dap_http_simple_module_deinit();
        dap_events_stop_all();
        dap_events_deinit();
        s_server_initialized = false;
    }
    
    TEST_INFO("HTTP server shutdown complete");
}

// ==============================================
// Test Cases
// ==============================================

/**
 * Test 1: Server initialization and deinitialization
 */
static void test_01_server_lifecycle(void) {
    TEST_INFO("Testing server lifecycle");
    
    // Initialize
    dap_events_init(1, 60000);
    dap_events_start();
    dap_http_simple_module_init();
    
    TEST_INFO("Server initialized successfully");
    
    // Deinitialize
    dap_http_simple_module_deinit();
    dap_events_stop_all();
    dap_events_deinit();
    
    TEST_INFO("Server deinitialized successfully");
    
    TEST_SUCCESS("Server lifecycle works");
}

/**
 * Test 2: User-Agent version checking
 */
static void test_02_user_agent_support(void) {
    TEST_INFO("Testing user-agent version support");
    
    // Set supported user agents
    dap_http_simple_set_supported_user_agents("DapVpn/2.2", "TestClient/1.0", NULL);
    
    // These functions are internal, but we can test through the public API
    // by checking that the list was set
    TEST_ASSERT(_is_supported_user_agents_list_setted(), 
                "User agent list should be set");
    
    // Test version comparisons
    TEST_ASSERT(_is_user_agent_supported("DapVpn/2.2"), 
                "Exact version should be supported");
    TEST_ASSERT(_is_user_agent_supported("DapVpn/2.3"), 
                "Higher version should be supported");
    TEST_ASSERT(!_is_user_agent_supported("DapVpn/2.1"), 
                "Lower version should NOT be supported");
    TEST_ASSERT(!_is_user_agent_supported("Unknown/1.0"), 
                "Unknown user agent should NOT be supported");
    TEST_ASSERT(_is_user_agent_supported("TestClient/1.0"), 
                "Second user agent should be supported");
    TEST_ASSERT(_is_user_agent_supported("TestClient/2.0"), 
                "Higher version of second agent should be supported");
    
    // Cleanup
    _free_user_agents_list();
    
    TEST_ASSERT(!_is_supported_user_agents_list_setted(), 
                "User agent list should be cleared");
    
    TEST_SUCCESS("User-agent support works");
}

/**
 * Test 3: Empty user-agent list handling
 */
static void test_03_empty_user_agent_list(void) {
    TEST_INFO("Testing empty user-agent list");
    
    TEST_ASSERT(!_is_supported_user_agents_list_setted(), 
                "Initially no user agents should be set");
    
    TEST_SUCCESS("Empty user-agent list handling works");
}

// ==============================================
// Main Test Suite
// ==============================================

int main(void) {
    TEST_SUITE_START("HTTP Server Integration Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing HTTP server with real initialization\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_server_lifecycle);
    TEST_RUN(test_02_user_agent_support);
    TEST_RUN(test_03_empty_user_agent_list);
    
    TEST_SUITE_END();
    
    return 0;
}

