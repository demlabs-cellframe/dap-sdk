/*
 * Authors:
 * Cellframe Team <https://cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2017-2025
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/**
 * @file test_stream_transport.c
 * @brief Unit tests for DAP Stream Transport Layer
 * 
 * Tests transport registration, configuration, and capabilities.
 * Isolated unit tests with minimal dependencies.
 * 
 * @date 2025-10-28
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_helpers.h"
#include "dap_mock.h"
#include "dap_net_transport.h"
#include "dap_net_transport_http_stream.h"
#include "dap_net_transport_udp_stream.h"
#include "dap_net_transport_websocket_stream.h"

#define LOG_TAG "test_stream_transport"

// ============================================================================
// Test Suite State
// ============================================================================

static bool s_test_initialized = false;

// ============================================================================
// Setup/Teardown Functions
// ============================================================================

/**
 * @brief Setup function called before each test
 */
static void setup_test(void)
{
    if (!s_test_initialized) {
        // Initialize DAP mock framework
        dap_mock_init();
        
        
        s_test_initialized = true;
        TEST_INFO("Transport test suite initialized");
    }
}

/**
 * @brief Teardown function called after each test
 */
static void teardown_test(void)
{
    // Cleanup is done in suite cleanup
}

/**
 * @brief Suite cleanup function
 */
static void suite_cleanup(void)
{
    if (s_test_initialized) {
        // Deinitialize transport layer
        dap_net_transport_deinit();
        
        // Deinitialize mock framework
        dap_mock_deinit();
        
        s_test_initialized = false;
        TEST_INFO("Transport test suite cleaned up");
    }
}

// ============================================================================
// Transport Registry Tests
// ============================================================================

/**
 * @brief Test 1: Verify transports are auto-registered
 */
static void test_01_transport_auto_registration(void)
{
    setup_test();
    
    TEST_INFO("Test 1: Testing automatic transport registration...");
    
    // Transports are registered automatically via __attribute__((constructor))
    // Just verify they are available
    
    // Find HTTP transport
    dap_net_transport_t *http_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT(http_transport != NULL, "HTTP transport not found (should be auto-registered)");
    TEST_ASSERT(http_transport->type == DAP_NET_TRANSPORT_HTTP, 
                "HTTP transport type mismatch");
    
    // Find by name
    dap_net_transport_t *http_by_name = 
        dap_net_transport_find_by_name("HTTP");
    TEST_ASSERT(http_by_name != NULL, "HTTP transport not found by name");
    TEST_ASSERT(http_by_name == http_transport, 
                "Transport found by name doesn't match transport found by type");
    
    TEST_SUCCESS("Test 1 passed: Transports are auto-registered correctly");
    teardown_test();
}

/**
 * @brief Test 2: Verify all transports are registered
 */
static void test_02_all_transports_registered(void)
{
    setup_test();
    
    TEST_INFO("Test 2: Testing all transports are auto-registered...");
    
    // Verify all transports are registered automatically
    dap_net_transport_t *http_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT(http_transport != NULL, "HTTP transport not found");
    
    dap_net_transport_t *udp_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_UDP_BASIC);
    TEST_ASSERT(udp_transport != NULL, "UDP transport not found");
    
    dap_net_transport_t *ws_transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT(ws_transport != NULL, "WebSocket transport not found");
    
    TEST_SUCCESS("Test 2 passed: All transports are auto-registered");
    teardown_test();
}

/**
 * @brief Test 3: Multiple transports registration
 */
static void test_03_multiple_transports(void)
{
    setup_test();
    
    TEST_INFO("Test 3: Testing multiple transports coexistence...");
    
    // Verify all are registered (auto-registered via constructors)
    dap_net_transport_t *http = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    dap_net_transport_t *udp = 
        dap_net_transport_find(DAP_NET_TRANSPORT_UDP_BASIC);
    dap_net_transport_t *ws = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    
    TEST_ASSERT(http != NULL, "HTTP transport not found");
    TEST_ASSERT(udp != NULL, "UDP transport not found");
    TEST_ASSERT(ws != NULL, "WebSocket transport not found");
    
    // Verify they're different
    TEST_ASSERT(http != udp, "HTTP and UDP transports are the same");
    TEST_ASSERT(http != ws, "HTTP and WebSocket transports are the same");
    TEST_ASSERT(udp != ws, "UDP and WebSocket transports are the same");
    
    // Get list of all transports
    dap_list_t *transport_list = dap_net_transport_list_all();
    TEST_ASSERT(transport_list != NULL, "Transport list is NULL");
    
    // Count transports
    int count = 0;
    for (dap_list_t *it = transport_list; it; it = it->next) {
        count++;
        dap_net_transport_t *transport = (dap_net_transport_t*)it->data;
        TEST_ASSERT(transport != NULL, "Transport in list is NULL");
        TEST_INFO("  Found transport: %s (type=0x%02X)", 
                  transport->name, transport->type);
    }
    
    TEST_ASSERT(count >= 3, "Expected at least 3 transports, found %d", count);
    
    // Free list (not contents)
    dap_list_free(transport_list);
    
    TEST_SUCCESS("Test 3 passed: Multiple transports coexist correctly");
    teardown_test();
}

// ============================================================================
// HTTP Transport Tests
// ============================================================================

/**
 * @brief Test 4: HTTP transport capabilities
 */
static void test_04_http_capabilities(void)
{
    setup_test();
    
    TEST_INFO("Test 4: Testing HTTP transport capabilities...");
    
    // HTTP transport is auto-registered via constructor
    // Find HTTP transport
    dap_net_transport_t *transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_HTTP);
    TEST_ASSERT(transport != NULL, "HTTP transport not found");
    
    // Check name
    TEST_ASSERT(strcmp(transport->name, "HTTP") == 0, 
                "HTTP transport name mismatch: got '%s'", transport->name);
    
    // Check capabilities
    uint32_t caps = transport->capabilities;
    TEST_INFO("  HTTP capabilities: 0x%04X", caps);
    
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_RELIABLE, 
                "HTTP should be reliable");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_ORDERED, 
                "HTTP should be ordered");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_BIDIRECTIONAL, 
                "HTTP should be bidirectional");
    
    TEST_SUCCESS("Test 4 passed: HTTP transport capabilities correct");
    teardown_test();
}

// ============================================================================
// UDP Transport Tests
// ============================================================================

/**
 * @brief Test 5: UDP transport capabilities
 */
static void test_05_udp_capabilities(void)
{
    setup_test();
    
    TEST_INFO("Test 5: Testing UDP transport capabilities...");
    
    // UDP transport is auto-registered via constructor
    // Find UDP transport
    dap_net_transport_t *transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_UDP_BASIC);
    TEST_ASSERT(transport != NULL, "UDP transport not found");
    
    // Check name
    TEST_ASSERT(strcmp(transport->name, "UDP") == 0, 
                "UDP transport name mismatch: got '%s'", transport->name);
    
    // Check capabilities
    uint32_t caps = transport->capabilities;
    TEST_INFO("  UDP capabilities: 0x%04X", caps);
    
    TEST_ASSERT(!(caps & DAP_NET_TRANSPORT_CAP_RELIABLE), 
                "UDP basic should not be reliable");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_LOW_LATENCY, 
                "UDP should be low latency");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_BIDIRECTIONAL, 
                "UDP should be bidirectional");
    
    TEST_SUCCESS("Test 5 passed: UDP transport capabilities correct");
    teardown_test();
}

/**
 * @brief Test 6: UDP transport configuration
 */
static void test_06_udp_configuration(void)
{
    setup_test();
    
    TEST_INFO("Test 6: Testing UDP transport configuration...");
    
    // UDP transport is auto-registered via constructor
    // Find UDP transport
    dap_net_transport_t *transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_UDP_BASIC);
    TEST_ASSERT(transport != NULL, "UDP transport not found");
    
    // Get default config
    dap_stream_transport_udp_config_t config = 
        dap_stream_transport_udp_config_default();
    TEST_ASSERT(config.max_packet_size > 0, "Default max packet size is 0");
    TEST_ASSERT(config.keepalive_ms > 0, "Default keepalive is 0");
    TEST_INFO("  Default config: max_packet_size=%u, keepalive_ms=%u", 
              config.max_packet_size, config.keepalive_ms);
    
    // Set custom config
    config.max_packet_size = 2000;
    config.keepalive_ms = 15000;
    config.enable_checksum = true;
    config.allow_fragmentation = false;
    
    int ret = dap_stream_transport_udp_set_config(transport, &config);
    TEST_ASSERT(ret == 0, "Failed to set UDP config");
    
    // Get config back
    dap_stream_transport_udp_config_t config_read;
    ret = dap_stream_transport_udp_get_config(transport, &config_read);
    TEST_ASSERT(ret == 0, "Failed to get UDP config");
    
    TEST_ASSERT(config_read.max_packet_size == 2000, 
                "Max packet size mismatch: expected 2000, got %u", 
                config_read.max_packet_size);
    TEST_ASSERT(config_read.keepalive_ms == 15000, 
                "Keepalive mismatch: expected 15000, got %u", 
                config_read.keepalive_ms);
    TEST_ASSERT(config_read.enable_checksum == true, 
                "Enable checksum mismatch");
    TEST_ASSERT(config_read.allow_fragmentation == false, 
                "Allow fragmentation mismatch");
    
    TEST_SUCCESS("Test 6 passed: UDP transport configuration works correctly");
    teardown_test();
}

// ============================================================================
// WebSocket Transport Tests
// ============================================================================

/**
 * @brief Test 7: WebSocket transport capabilities
 */
static void test_07_websocket_capabilities(void)
{
    setup_test();
    
    TEST_INFO("Test 7: Testing WebSocket transport capabilities...");
    
    // WebSocket transport is auto-registered via constructor
    // Find WebSocket transport
    dap_net_transport_t *transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT(transport != NULL, "WebSocket transport not found");
    
    // Check name
    TEST_ASSERT(strcmp(transport->name, "WebSocket") == 0, 
                "WebSocket transport name mismatch: got '%s'", transport->name);
    
    // Check capabilities
    uint32_t caps = transport->capabilities;
    TEST_INFO("  WebSocket capabilities: 0x%04X", caps);
    
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_RELIABLE, 
                "WebSocket should be reliable");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_ORDERED, 
                "WebSocket should be ordered");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_BIDIRECTIONAL, 
                "WebSocket should be bidirectional");
    TEST_ASSERT(caps & DAP_NET_TRANSPORT_CAP_MULTIPLEXING, 
                "WebSocket should support multiplexing");
    
    TEST_SUCCESS("Test 7 passed: WebSocket transport capabilities correct");
    teardown_test();
}

/**
 * @brief Test 8: WebSocket transport configuration
 */
static void test_08_websocket_configuration(void)
{
    setup_test();
    
    TEST_INFO("Test 8: Testing WebSocket transport configuration...");
    
    // WebSocket transport is auto-registered via constructor
    // Find WebSocket transport
    dap_net_transport_t *transport = 
        dap_net_transport_find(DAP_NET_TRANSPORT_WEBSOCKET);
    TEST_ASSERT(transport != NULL, "WebSocket transport not found");
    
    // Get default config
    dap_stream_transport_ws_config_t config = 
        dap_stream_transport_ws_config_default();
    TEST_ASSERT(config.max_frame_size > 0, "Default max frame size is 0");
    TEST_ASSERT(config.ping_interval_ms > 0, "Default ping interval is 0");
    TEST_ASSERT(config.client_mask_frames == true, 
                "Client masking should be enabled by default (RFC 6455)");
    TEST_ASSERT(config.server_mask_frames == false, 
                "Server masking should be disabled by default");
    TEST_INFO("  Default config: max_frame_size=%u, ping_interval_ms=%u", 
              config.max_frame_size, config.ping_interval_ms);
    
    // Set custom config
    config.max_frame_size = 2 * 1024 * 1024;  // 2MB
    config.ping_interval_ms = 20000;  // 20s
    config.pong_timeout_ms = 5000;    // 5s
    config.enable_compression = true;
    
    int ret = dap_stream_transport_ws_set_config(transport, &config);
    TEST_ASSERT(ret == 0, "Failed to set WebSocket config");
    
    // Get config back
    dap_stream_transport_ws_config_t config_read;
    ret = dap_stream_transport_ws_get_config(transport, &config_read);
    TEST_ASSERT(ret == 0, "Failed to get WebSocket config");
    
    TEST_ASSERT(config_read.max_frame_size == 2 * 1024 * 1024, 
                "Max frame size mismatch: expected %u, got %u", 
                2 * 1024 * 1024, config_read.max_frame_size);
    TEST_ASSERT(config_read.ping_interval_ms == 20000, 
                "Ping interval mismatch: expected 20000, got %u", 
                config_read.ping_interval_ms);
    TEST_ASSERT(config_read.pong_timeout_ms == 5000, 
                "Pong timeout mismatch: expected 5000, got %u", 
                config_read.pong_timeout_ms);
    TEST_ASSERT(config_read.enable_compression == true, 
                "Enable compression mismatch");
    
    TEST_SUCCESS("Test 8 passed: WebSocket transport configuration works correctly");
    teardown_test();
}

// ============================================================================
// Test Suite Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    TEST_SUITE_START("DAP Stream Transport Layer Unit Tests");
    
    // Transport Registry Tests
    TEST_RUN(test_01_transport_auto_registration);
    TEST_RUN(test_02_all_transports_registered);
    TEST_RUN(test_03_multiple_transports);
    
    // HTTP Transport Tests
    TEST_RUN(test_04_http_capabilities);
    
    // UDP Transport Tests
    TEST_RUN(test_05_udp_capabilities);
    TEST_RUN(test_06_udp_configuration);
    
    // WebSocket Transport Tests
    TEST_RUN(test_07_websocket_capabilities);
    TEST_RUN(test_08_websocket_configuration);
    
    // Cleanup
    suite_cleanup();
    
    TEST_SUITE_END();
    
    return 0;
}

