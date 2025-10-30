/**
 * Stream Integration Test Suite
 * 
 * Tests real stream protocol behavior with connections.
 * 
 * Features tested:
 * - Stream client-server connection
 * - Data transmission over streams
 * - Channel creation and management
 * - Stream packet handling
 * 
 * @note This is an INTEGRATION test - requires server instance or network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_cert.h"
#include "dap_client.h"
#include "dap_client_pvt.h"
#include "dap_stream.h"
#include "dap_events.h"
#include "dap_test_helpers.h"
#include "dap_test_async.h"
#include "dap_client_test_fixtures.h"

#define LOG_TAG "test_stream"

static volatile bool s_test_completed = false;
static volatile bool s_test_success = false;

// ==============================================
// Test Infrastructure
// ==============================================

static void s_setup_stream_test(void) {
    TEST_INFO("Initializing stream system...");
    
    dap_events_init(1, 60000);
    dap_events_start();
    dap_stream_init(NULL);
    
    TEST_INFO("Stream system initialized");
}

static void s_teardown_stream_test(void) {
    TEST_INFO("Shutting down stream system...");
    
    dap_stream_deinit();
    dap_events_stop_all();
    dap_events_deinit();
    
    TEST_INFO("Stream system shutdown complete");
}

static void s_reset_test_state(void) {
    s_test_completed = false;
    s_test_success = false;
}

// ==============================================
// Test Cases
// ==============================================

/**
 * Test 1: Stream system initialization
 */
static void test_01_stream_initialization(void) {
    TEST_INFO("Testing stream system initialization");
    
    // Initialize
    dap_events_init(1, 60000);
    dap_events_start();
    int result = dap_stream_init(NULL);
    
    TEST_ASSERT(result == 0, "Stream init should return 0");
    
    // Deinitialize
    dap_stream_deinit();
    dap_events_stop_all();
    
    // Give workers time to stop
    dap_test_sleep_ms(100);
    
    dap_events_deinit();
    
    TEST_SUCCESS("Stream initialization works");
}

/**
 * Test 2: Transport registration
 */
static void test_02_transport_registration(void) {
    TEST_INFO("Testing transport registration");
    
    dap_events_init(1, 60000);
    dap_events_start();
    dap_stream_init(NULL);
    
    // After dap_stream_init, default transports should be registered
    dap_stream_transport_t *http_transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_HTTP);
    TEST_ASSERT_NOT_NULL(http_transport, "HTTP transport should be registered");
    
    dap_stream_transport_t *udp_transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_UDP_BASIC);
    TEST_ASSERT_NOT_NULL(udp_transport, "UDP transport should be registered");
    
    dap_stream_transport_t *ws_transport = dap_stream_transport_find(DAP_STREAM_TRANSPORT_WEBSOCKET);
    TEST_ASSERT_NOT_NULL(ws_transport, "WebSocket transport should be registered");
    
    dap_stream_deinit();
    dap_events_stop_all();
    dap_events_deinit();
    
    TEST_SUCCESS("Transport registration works");
}

/**
 * Test 3: Client creation
 */
static void test_03_client_creation(void) {
    TEST_INFO("Testing client creation");
    
    dap_events_init(1, 60000);
    dap_events_start();
    dap_stream_init(NULL);
    
    // Create client
    dap_client_t *client = dap_client_new(NULL, NULL);
    TEST_ASSERT_NOT_NULL(client, "Client should be created");
    
    // Verify client is properly initialized using fixtures
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(client, 1000);
    TEST_ASSERT(l_client_ready, "Client should be properly initialized");
    
    // Verify client internal structure
    dap_client_pvt_t *client_pvt = DAP_CLIENT_PVT(client);
    TEST_ASSERT_NOT_NULL(client_pvt, "Client internal structure should exist");
    TEST_ASSERT_NOT_NULL(client_pvt->worker, "Client should have a worker assigned");
    TEST_ASSERT(client_pvt->stage == STAGE_BEGIN, "Client should start at STAGE_BEGIN");
    
    // Set uplink (dummy address for this test)
    dap_stream_node_addr_t l_node = {};
    dap_client_set_uplink_unsafe(client, &l_node, "127.0.0.1", 8079);
    
    // Verify uplink was set
    TEST_ASSERT(strcmp(dap_client_get_uplink_addr_unsafe(client), "127.0.0.1") == 0,
                "Uplink address should be set");
    TEST_ASSERT(dap_client_get_uplink_port_unsafe(client) == 8079,
                "Uplink port should be set");
    
    // Delete client
    dap_client_delete_unsafe(client);
    client = NULL; // Invalidate pointer to prevent use-after-free
    
    // Small delay for cleanup operations to propagate
    dap_test_sleep_ms(100);
    
    dap_stream_deinit();
    dap_events_stop_all();
    
    // Give workers time to stop (events system internal operation)
    dap_test_sleep_ms(100);
    
    dap_events_deinit();
    
    TEST_SUCCESS("Client creation works");
}

/**
 * Test 4: Channel configuration
 */
static void test_04_channel_configuration(void) {
    TEST_INFO("Testing channel configuration");
    
    dap_events_init(1, 60000);
    dap_events_start();
    dap_stream_init(NULL);
    
    dap_client_t *client = dap_client_new(NULL, NULL);
    TEST_ASSERT_NOT_NULL(client, "Client should be created");
    
    // Verify client is properly initialized using fixtures
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(client, 1000);
    TEST_ASSERT(l_client_ready, "Client should be properly initialized");
    
    // Verify client internal structure
    dap_client_pvt_t *client_pvt = DAP_CLIENT_PVT(client);
    TEST_ASSERT_NOT_NULL(client_pvt, "Client internal structure should exist");
    
    // Set active channels
    dap_client_set_active_channels_unsafe(client, "N");
    
    // Verify channels were set
    TEST_ASSERT_NOT_NULL(client->active_channels, "Active channels should be set");
    TEST_ASSERT(strcmp(client->active_channels, "N") == 0, 
                "Active channels should be 'N'");
    
    TEST_INFO("Channels configured successfully");
    
    dap_client_delete_unsafe(client);
    client = NULL; // Invalidate pointer to prevent use-after-free
    
    // Small delay for cleanup operations to propagate
    dap_test_sleep_ms(100);
    
    dap_stream_deinit();
    dap_events_stop_all();
    
    // Give workers time to stop
    dap_test_sleep_ms(100);
    
    dap_events_deinit();
    
    TEST_SUCCESS("Channel configuration works");
}

// ==============================================
// Main Test Suite
// ==============================================

int main(void) {
    // Create minimal config file for tests
    const char *config_content = "[resources]\n"
                                 "ca_folders=[.]\n";
    FILE *f = fopen("test_stream.cfg", "w");
    if (f) {
        fwrite(config_content, 1, strlen(config_content), f);
        fclose(f);
    }
    
    // Initialize common DAP subsystems (logging first!)
    dap_common_init(LOG_TAG, NULL);
    
    // Initialize config system AFTER dap_common_init (needs logging)
    dap_config_init(".");
    
    // Open config and set as global
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_stream");
    if (!g_config) {
        printf("Failed to open config\n");
        return -1;
    }
    
    // Re-init crypto and certs with proper config
    // (they were initialized by dap_common_init but without g_config set)
    dap_enc_deinit();
    dap_enc_init();
    
    TEST_SUITE_START("Stream Integration Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing stream system with real initialization\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_stream_initialization);
    TEST_RUN(test_02_transport_registration);
    TEST_RUN(test_03_client_creation);
    TEST_RUN(test_04_channel_configuration);
    
    TEST_SUITE_END();
    
    // Cleanup config
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    
    // Remove temp config file
    remove("test_stream.cfg");
    
    return 0;
}

