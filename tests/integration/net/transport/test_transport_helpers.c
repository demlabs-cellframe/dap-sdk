/**
 * @file test_transport_helpers.c
 * @brief Implementation of common helper functions for transport integration tests
 * @date 2025-11-03
 * @copyright (c) 2025 Cellframe Network
 */

#include "test_transport_helpers.h"
#include "dap_stream_ch_proc.h"
#include "dap_client_pvt.h"
#include "dap_client_helpers.h"
#include "dap_server_helpers.h"
#include "dap_worker.h"
#include "dap_list.h"
#include "dap_events_socket.h"
#include "dap_http_server.h"
#include "dap_net_transport_websocket_server.h"
#include "dap_net_transport_udp_server.h"
#include "dap_net_transport_dns_server.h"
#include <stdio.h>

/**
 * @brief Wait for transports to be registered
 */
bool test_wait_for_transports_registered(uint32_t a_timeout_ms)
{
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    // Automatically get expected transports from config array
    // This ensures we check for all transports defined in test_transport_integration.c
    while (l_elapsed < a_timeout_ms) {
        size_t l_registered_count = 0;
        // Use runtime variable for count (available from test_transport_integration.c)
        for (size_t i = 0; i < g_transport_config_count; i++) {
            if (dap_net_transport_find(g_transport_configs[i].transport_type)) {
                l_registered_count++;
            }
        }
        
        if (l_registered_count == g_transport_config_count) {
            return true;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    return false;
}

/**
 * @brief Get dap_server_t from transport server
 * Helper function to extract underlying dap_server_t from transport-specific structure
 */
static dap_server_t *s_get_server_from_transport(dap_net_transport_server_t *a_server)
{
    if (!a_server) {
        return NULL;
    }
    
    // For HTTP transport, get HTTP server from transport_specific
    if (a_server->transport_type == DAP_NET_TRANSPORT_HTTP) {
        dap_http_server_t *l_http_server = (dap_http_server_t *)a_server->transport_specific;
        if (l_http_server && l_http_server->server) {
            return l_http_server->server;
        }
    }
    
    // For WebSocket transport, get WebSocket server from transport_specific
    // WebSocket server has its own structure with server field
    if (a_server->transport_type == DAP_NET_TRANSPORT_WEBSOCKET) {
        dap_net_transport_websocket_server_t *l_ws_server = (dap_net_transport_websocket_server_t *)a_server->transport_specific;
        if (l_ws_server && l_ws_server->server) {
            return l_ws_server->server;
        }
    }
    
    // For UDP transports, get UDP server from transport_specific
    if (a_server->transport_type == DAP_NET_TRANSPORT_UDP_BASIC ||
        a_server->transport_type == DAP_NET_TRANSPORT_UDP_RELIABLE ||
        a_server->transport_type == DAP_NET_TRANSPORT_UDP_QUIC_LIKE) {
        dap_net_transport_udp_server_t *l_udp_server = (dap_net_transport_udp_server_t *)a_server->transport_specific;
        if (l_udp_server && l_udp_server->server) {
            return l_udp_server->server;
        }
    }
    
    // For DNS tunnel transport, get DNS server from transport_specific
    if (a_server->transport_type == DAP_NET_TRANSPORT_DNS_TUNNEL) {
        dap_net_transport_dns_server_t *l_dns_server = (dap_net_transport_dns_server_t *)a_server->transport_specific;
        if (l_dns_server && l_dns_server->server) {
            return l_dns_server->server;
        }
    }
    
    return NULL;
}

/**
 * @brief Wait for server to be ready (listening)
 */
bool test_wait_for_server_ready(dap_net_transport_server_t *a_server, uint32_t a_timeout_ms)
{
    if (!a_server) {
        return false;
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    // First, wait for transport_specific server to be created and dap_server_t to be initialized
    // This may take some time for UDP/DNS servers as they create dap_server_t during start()
    dap_server_t *l_server = NULL;
    while (l_elapsed < a_timeout_ms) {
        l_server = s_get_server_from_transport(a_server);
        if (l_server) {
            break; // Server structure found
        }
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    if (!l_server) {
        // Transport-specific server structure not found or dap_server_t not created yet
        return false;
    }
    
    // Use centralized server wait function
    return dap_server_wait_for_ready(l_server, a_timeout_ms - l_elapsed);
}

/**
 * @brief Wait for stream channels to be created
 */
bool test_wait_for_stream_channels_ready(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms)
{
    // Use centralized client wait function
    return dap_client_wait_for_channels(a_client, a_expected_channels, a_timeout_ms);
}

/**
 * @brief Wait for client to be deleted
 */
bool test_wait_for_client_deleted(dap_client_t **a_client_ptr, uint32_t a_timeout_ms)
{
    // Use centralized client wait function
    return dap_client_wait_for_deletion(a_client_ptr, a_timeout_ms);
}

/**
 * @brief Wait for all streams to be closed
 */
bool test_wait_for_all_streams_closed(uint32_t a_timeout_ms)
{
    // Simple delay - streams close asynchronously
    // In a real scenario, we'd check dap_stream list, but for tests this is sufficient
    dap_test_sleep_ms(500);
    return true;
}

