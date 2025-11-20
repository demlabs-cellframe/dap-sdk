/**
 * @file test_transport_api.c
 * @brief Transport Layer API Test Suite
 * @details Tests transport type set/get API and transport registration:
 *          - dap_client_set_transport_type()
 *          - dap_client_get_transport_type()
 *          - Transport registration verification
 *          - Transport enumeration
 *          - Transport string parsing
 * 
 * @note This test suite focuses on API functionality without server-client integration
 * @date 2025-11-01
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// DAP SDK headers
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_events.h"
#include "dap_client.h"
#include "dap_stream.h"
#include "dap_net_transport.h"

// Test framework headers
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_test_helpers.h"
#include "dap_client_test_fixtures.h"

#define LOG_TAG "test_transport_api"

// =======================================================================================
// TEST CASES
// =======================================================================================

/**
 * @brief Test 1: Initialize transport system
 */
static void test_01_init_transport_system(void) {
    TEST_INFO("Test 1: Initializing transport system");
    
    // Initialize event system
    int l_ret = dap_events_init(1, 60000);
    TEST_ASSERT(l_ret == 0, "Events initialization should succeed");
    
    dap_events_start();
    
    // Initialize stream system
    l_ret = dap_stream_init(NULL);
    TEST_ASSERT(l_ret == 0, "Stream initialization should succeed");
    
    // Give system time to stabilize
    dap_test_sleep_ms(200);
    
    TEST_SUCCESS("Test 1 passed: Transport system initialized");
}

/**
 * @brief Test 2: Verify transport registration and string parsing
 */
static void test_02_transport_registration(void) {
    TEST_INFO("Test 2: Verifying transport registration and string parsing");
    
    // Test string to enum conversion
    TEST_ASSERT(dap_net_transport_type_from_str("http") == DAP_NET_TRANSPORT_HTTP, 
                "http string should parse to HTTP enum");
    TEST_ASSERT(dap_net_transport_type_from_str("udp") == DAP_NET_TRANSPORT_UDP_BASIC, 
                "udp string should parse to UDP_BASIC enum");
    TEST_ASSERT(dap_net_transport_type_from_str("websocket") == DAP_NET_TRANSPORT_WEBSOCKET, 
                "websocket string should parse to WEBSOCKET enum");
    TEST_ASSERT(dap_net_transport_type_from_str("tls") == DAP_NET_TRANSPORT_TLS_DIRECT, 
                "tls string should parse to TLS_DIRECT enum");
    TEST_ASSERT(dap_net_transport_type_from_str("unknown") == DAP_NET_TRANSPORT_HTTP, 
                "unknown string should default to HTTP");
    
    // Test enum to string conversion
    TEST_ASSERT(strcmp(dap_net_transport_type_to_str(DAP_NET_TRANSPORT_HTTP), "HTTP") == 0,
                "HTTP enum should convert to HTTP string");
    TEST_ASSERT(strcmp(dap_net_transport_type_to_str(DAP_NET_TRANSPORT_UDP_BASIC), "UDP_BASIC") == 0,
                "UDP_BASIC enum should convert to UDP_BASIC string");
    TEST_ASSERT(strcmp(dap_net_transport_type_to_str(DAP_NET_TRANSPORT_WEBSOCKET), "WEBSOCKET") == 0,
                "WEBSOCKET enum should convert to WEBSOCKET string");
    
    // Check HTTP transport registration
    dap_net_transport_t *l_http = dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(l_http, "HTTP transport should be registered");
    TEST_ASSERT(l_http->type == DAP_NET_TRANSPORT_HTTP, "HTTP transport type should match");
    
    // Check WebSocket transport registration
    dap_net_transport_t *l_ws = dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_ws, "WebSocket transport should be registered");
    TEST_ASSERT(l_ws->type == DAP_NET_TRANSPORT_WEBSOCKET, "WebSocket transport type should match");
    
    // Check UDP transport registration
    dap_net_transport_t *l_udp = dap_net_transport_find(DAP_NET_TRANSPORT_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_udp, "UDP transport should be registered");
    TEST_ASSERT(l_udp->type == DAP_NET_TRANSPORT_UDP_BASIC, "UDP transport type should match");
    
    TEST_SUCCESS("Test 2 passed: All transports are registered and string parsing works");
}

/**
 * @brief Test 3: Client transport type API
 */
static void test_03_client_transport_api(void) {
    TEST_INFO("Test 3: Testing client transport type API");
    
    // Initialize client system
    int l_ret = dap_client_init();
    TEST_ASSERT(l_ret == 0, "Client initialization should succeed");
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT_NOT_NULL(l_client, "Client should be created");
    
    // Wait for client initialization
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(l_client, 1000);
    TEST_ASSERT(l_client_ready, "Client should be properly initialized");
    
    // Test 1: Default transport type should be HTTP
    dap_net_transport_type_t l_default_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_default_type == DAP_NET_TRANSPORT_HTTP, 
                "Default transport type should be HTTP");
    
    // Test 2: Set transport to WebSocket
    dap_client_set_transport_type(l_client, DAP_NET_TRANSPORT_WEBSOCKET);
    dap_net_transport_type_t l_ws_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_ws_type == DAP_NET_TRANSPORT_WEBSOCKET, 
                "Transport type should be WebSocket after set");
    
    // Test 3: Set transport to UDP
    dap_client_set_transport_type(l_client, DAP_NET_TRANSPORT_UDP_RELIABLE);
    dap_net_transport_type_t l_udp_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_udp_type == DAP_NET_TRANSPORT_UDP_RELIABLE, 
                "Transport type should be UDP_RELIABLE after set");
    
    // Test 4: Set transport to TLS
    dap_client_set_transport_type(l_client, DAP_NET_TRANSPORT_TLS_DIRECT);
    dap_net_transport_type_t l_tls_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_tls_type == DAP_NET_TRANSPORT_TLS_DIRECT, 
                "Transport type should be TLS_DIRECT after set");
    
    // Test 5: Direct field access matches getter
    TEST_ASSERT(l_client->transport_type == l_tls_type, 
                "Direct field access should match getter");
    
    // Test 6: Set back to HTTP
    dap_client_set_transport_type(l_client, DAP_NET_TRANSPORT_HTTP);
    l_default_type = dap_client_get_transport_type(l_client);
    TEST_ASSERT(l_default_type == DAP_NET_TRANSPORT_HTTP, 
                "Transport type should be HTTP after reset");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    dap_test_sleep_ms(100);
    
    dap_client_deinit();
    
    TEST_SUCCESS("Test 3 passed: Client transport API works correctly");
}

/**
 * @brief Test 4: Transport enumeration
 */
static void test_04_transport_enumeration(void) {
    TEST_INFO("Test 4: Testing transport enumeration");
    
    // Get all registered transports
    dap_list_t *l_transports = dap_net_transport_list_all();
    TEST_ASSERT_NOT_NULL(l_transports, "Transport list should not be NULL");
    
    // Count transports
    int l_count = 0;
    for (dap_list_t *l_item = l_transports; l_item; l_item = l_item->next) {
        dap_net_transport_t *l_transport = (dap_net_transport_t *)l_item->data;
        TEST_ASSERT_NOT_NULL(l_transport, "Transport in list should not be NULL");
        TEST_INFO("  Found transport: type=%d, name=%s", 
                  l_transport->type, l_transport->name);
        l_count++;
    }
    
    TEST_INFO("Total transports registered: %d", l_count);
    TEST_ASSERT(l_count >= 3, "Should have at least HTTP, WebSocket, UDP registered");
    
    // Free list (not contents)
    dap_list_free(l_transports);
    
    TEST_SUCCESS("Test 4 passed: Transport enumeration works");
}

/**
 * @brief Test 5: Cleanup transport system
 */
static void test_05_cleanup(void) {
    TEST_INFO("Test 5: Cleaning up transport system");
    
    // Cleanup stream system
    dap_stream_deinit();
    
    // Give time for cleanup to propagate
    dap_test_sleep_ms(200);
    
    // Deinit events system
    if (dap_events_workers_init_status()) {
        dap_events_deinit();
    }
    
    TEST_SUCCESS("Test 5 passed: Cleanup complete");
}

// =======================================================================================
// MAIN TEST SUITE
// =======================================================================================

int main(void) {
    // Create minimal config file for tests
    const char *config_content = "[resources]\n"
                                 "ca_folders=[./test_ca]\n"
                                 "[general]\n"
                                 "debug_reactor=true\n"
                                 "[dap_client]\n"
                                 "max_tries=3\n"
                                 "timeout=20\n"
                                 "debug_more=false\n"
                                 "timeout_active_after_connect=15\n";
    FILE *f = fopen("test_transport_api.cfg", "w");
    if (f) {
        fwrite(config_content, 1, strlen(config_content), f);
        fclose(f);
    }
    
    // Initialize common DAP subsystems
    dap_common_init(LOG_TAG, NULL);
    dap_log_level_set(L_DEBUG);
    dap_config_init(".");
    
    // Open config and set as global
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_transport_api");
    if (!g_config) {
        printf("Failed to open config\n");
        return -1;
    }
    
    // Initialize encryption system
    dap_enc_init();
    
    // Setup test certificate environment
    int l_ret = dap_test_setup_certificates(".");
    if (l_ret != 0) {
        printf("Failed to setup test certificates\n");
        return -1;
    }
    
    TEST_SUITE_START("Transport API Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing transport API: set/get, registration, enumeration\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_init_transport_system);
    TEST_RUN(test_02_transport_registration);
    TEST_RUN(test_03_client_transport_api);
    TEST_RUN(test_04_transport_enumeration);
    TEST_RUN(test_05_cleanup);
    
    TEST_SUITE_END();
    
    // Final cleanup
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    
    // Remove temp config file
    remove("test_transport_api.cfg");
    
    return 0;
}

