/**
 * @file test_trans_api.c
 * @brief Trans Layer API Test Suite
 * @details Tests trans type set/get API and trans registration:
 *          - dap_client_set_trans_type()
 *          - dap_client_get_trans_type()
 *          - Trans registration verification
 *          - Trans enumeration
 *          - Trans string parsing
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
#include "dap_net_trans.h"
#include "dap_net_trans_http_stream.h"
#include "dap_net_trans_udp_stream.h"
#include "dap_net_trans_websocket_stream.h"

// Test framework headers
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_test_helpers.h"
#include "dap_client_test_fixtures.h"

#define LOG_TAG "test_trans_api"

// =======================================================================================
// TEST CASES
// =======================================================================================

/**
 * @brief Test 1: Initialize trans system
 */
static void test_01_init_trans_system(void) {
    TEST_INFO("Test 1: Initializing trans system");
    
    // Initialize event system
    int l_ret = dap_events_init(1, 60000);
    TEST_ASSERT(l_ret == 0, "Events initialization should succeed");
    
    dap_events_start();
    
    // Initialize common DAP (automatically registers transs via module system)
    l_ret = dap_common_init("test_trans_api", NULL);
    TEST_ASSERT(l_ret == 0, "DAP common initialization should succeed");
    
    // Initialize stream system
    l_ret = dap_stream_init(NULL);
    TEST_ASSERT(l_ret == 0, "Stream initialization should succeed");
    
    // Give system time to stabilize
    dap_test_sleep_ms(200);
    
    TEST_SUCCESS("Test 1 passed: Trans system initialized");
}

/**
 * @brief Test 2: Verify trans registration and string parsing
 */
static void test_02_trans_registration(void) {
    TEST_INFO("Test 2: Verifying trans registration and string parsing");
    
    // Test string to enum conversion
    TEST_ASSERT(dap_net_trans_type_from_str("http") == DAP_NET_TRANS_HTTP, 
                "http string should parse to HTTP enum");
    TEST_ASSERT(dap_net_trans_type_from_str("udp") == DAP_NET_TRANS_UDP_BASIC, 
                "udp string should parse to UDP_BASIC enum");
    TEST_ASSERT(dap_net_trans_type_from_str("websocket") == DAP_NET_TRANS_WEBSOCKET, 
                "websocket string should parse to WEBSOCKET enum");
    TEST_ASSERT(dap_net_trans_type_from_str("tls") == DAP_NET_TRANS_TLS_DIRECT, 
                "tls string should parse to TLS_DIRECT enum");
    TEST_ASSERT(dap_net_trans_type_from_str("unknown") == DAP_NET_TRANS_HTTP, 
                "unknown string should default to HTTP");
    
    // Test enum to string conversion
    TEST_ASSERT(strcmp(dap_net_trans_type_to_str(DAP_NET_TRANS_HTTP), "HTTP") == 0,
                "HTTP enum should convert to HTTP string");
    TEST_ASSERT(strcmp(dap_net_trans_type_to_str(DAP_NET_TRANS_UDP_BASIC), "UDP_BASIC") == 0,
                "UDP_BASIC enum should convert to UDP_BASIC string");
    TEST_ASSERT(strcmp(dap_net_trans_type_to_str(DAP_NET_TRANS_WEBSOCKET), "WEBSOCKET") == 0,
                "WEBSOCKET enum should convert to WEBSOCKET string");
    
    // Check HTTP trans registration
    dap_net_trans_t *l_http = dap_net_trans_find(DAP_NET_TRANS_HTTP);
    TEST_ASSERT_NOT_NULL(l_http, "HTTP trans should be registered");
    TEST_ASSERT(l_http->type == DAP_NET_TRANS_HTTP, "HTTP trans type should match");
    
    // Check WebSocket trans registration
    dap_net_trans_t *l_ws = dap_net_trans_find(DAP_NET_TRANS_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(l_ws, "WebSocket trans should be registered");
    TEST_ASSERT(l_ws->type == DAP_NET_TRANS_WEBSOCKET, "WebSocket trans type should match");
    
    // Check UDP trans registration
    dap_net_trans_t *l_udp = dap_net_trans_find(DAP_NET_TRANS_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(l_udp, "UDP trans should be registered");
    TEST_ASSERT(l_udp->type == DAP_NET_TRANS_UDP_BASIC, "UDP trans type should match");
    
    TEST_SUCCESS("Test 2 passed: All transs are registered and string parsing works");
}

/**
 * @brief Test 3: Client trans type API
 */
static void test_03_client_trans_api(void) {
    TEST_INFO("Test 3: Testing client trans type API");
    
    // Initialize client system
    int l_ret = dap_client_init();
    TEST_ASSERT(l_ret == 0, "Client initialization should succeed");
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    TEST_ASSERT_NOT_NULL(l_client, "Client should be created");
    
    // Wait for client initialization
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(l_client, 1000);
    TEST_ASSERT(l_client_ready, "Client should be properly initialized");
    
    // Test 1: Default trans type should be HTTP
    dap_net_trans_type_t l_default_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_default_type == DAP_NET_TRANS_HTTP, 
                "Default trans type should be HTTP");
    
    // Test 2: Set trans to WebSocket
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_WEBSOCKET);
    dap_net_trans_type_t l_ws_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_ws_type == DAP_NET_TRANS_WEBSOCKET, 
                "Trans type should be WebSocket after set");
    
    // Test 3: Set trans to UDP
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_UDP_RELIABLE);
    dap_net_trans_type_t l_udp_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_udp_type == DAP_NET_TRANS_UDP_RELIABLE, 
                "Trans type should be UDP_RELIABLE after set");
    
    // Test 4: Set trans to TLS
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_TLS_DIRECT);
    dap_net_trans_type_t l_tls_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_tls_type == DAP_NET_TRANS_TLS_DIRECT, 
                "Trans type should be TLS_DIRECT after set");
    
    // Test 5: Direct field access matches getter
    TEST_ASSERT(l_client->trans_type == l_tls_type, 
                "Direct field access should match getter");
    
    // Test 6: Set back to HTTP
    dap_client_set_trans_type(l_client, DAP_NET_TRANS_HTTP);
    l_default_type = dap_client_get_trans_type(l_client);
    TEST_ASSERT(l_default_type == DAP_NET_TRANS_HTTP, 
                "Trans type should be HTTP after reset");
    
    // Cleanup
    dap_client_delete_unsafe(l_client);
    dap_test_sleep_ms(100);
    
    dap_client_deinit();
    
    TEST_SUCCESS("Test 3 passed: Client trans API works correctly");
}

/**
 * @brief Test 4: Trans enumeration
 */
static void test_04_trans_enumeration(void) {
    TEST_INFO("Test 4: Testing trans enumeration");
    
    // Get all registered transs
    dap_list_t *l_transs = dap_net_trans_list_all();
    TEST_ASSERT_NOT_NULL(l_transs, "Trans list should not be NULL");
    
    // Count transs
    int l_count = 0;
    for (dap_list_t *l_item = l_transs; l_item; l_item = l_item->next) {
        dap_net_trans_t *l_trans = (dap_net_trans_t *)l_item->data;
        TEST_ASSERT_NOT_NULL(l_trans, "Trans in list should not be NULL");
        TEST_INFO("  Found trans: type=%d, name=%s", 
                  l_trans->type, l_trans->name);
        l_count++;
    }
    
    TEST_INFO("Total transs registered: %d", l_count);
    TEST_ASSERT(l_count >= 3, "Should have at least HTTP, WebSocket, UDP registered");
    
    // Free list (not contents)
    dap_list_free(l_transs);
    
    TEST_SUCCESS("Test 4 passed: Trans enumeration works");
}

/**
 * @brief Test 5: Cleanup trans system
 */
static void test_05_cleanup(void) {
    TEST_INFO("Test 5: Cleaning up trans system");
    
    // Cleanup stream system
    dap_stream_deinit();
    
    // Module system handles unregister automatically via dap_common_deinit()
    dap_common_deinit();
    
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
    FILE *f = fopen("test_trans_api.cfg", "w");
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
    g_config = dap_config_open("test_trans_api");
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
    
    TEST_SUITE_START("Trans API Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing trans API: set/get, registration, enumeration\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_init_trans_system);
    TEST_RUN(test_02_trans_registration);
    TEST_RUN(test_03_client_trans_api);
    TEST_RUN(test_04_trans_enumeration);
    TEST_RUN(test_05_cleanup);
    
    TEST_SUITE_END();
    
    // Final cleanup
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    
    // Remove temp config file
    remove("test_trans_api.cfg");
    
    return 0;
}

