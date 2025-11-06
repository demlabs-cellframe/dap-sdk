/**
 * @file test_transport_integration.c
 * @brief Transport Integration Test Suite
 * @details Tests all transport types with parallel execution:
 *          - Initializes all available transports
 *          - Creates servers for each transport type
 *          - Tests full handshake cycle (ENC_INIT -> STREAM_CTL -> STREAM_SESSION -> STREAM_CONNECTED -> STREAM_STREAMING)
 *          - Tests data exchange via stream_ch with large data volumes (~10MB)
 *          - Runs all transport tests in parallel
 * 
 * @note This is a comprehensive integration test that validates the full DAP protocol
 *       implementation across all transport types simultaneously.
 * @date 2025-11-02
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

// DAP SDK headers
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_events.h"
#include "dap_client.h"
#include "dap_client_helpers.h"
#include "dap_stream.h"
#include "dap_net_transport.h"
#include "dap_net_transport_server.h"

#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_module.h"
#include "dap_link_manager.h"
#include "dap_global_db.h"
#include "dap_global_db_driver.h"
#include "dap_mock.h"
#include "dap_enc_ks.h"  // For DAP_STREAM_NODE_ADDR_CERT_TYPE

// Test framework headers
#include "dap_test_async.h"
#include "dap_test_helpers.h"
#include "dap_client_test_fixtures.h"
#include "test_transport_helpers.h"

#define LOG_TAG "test_transport"

// Test configuration
#define TEST_PARALLEL_TRANSPORTS 4  // Number of parallel transport instances per type
#define TEST_LARGE_DATA_SIZE (10 * 1024 * 1024)  // 10 MB
#define TEST_STREAM_CH_ID 'A'
#define TEST_TRANSPORT_TIMEOUT_MS 10000  // 30 seconds

// Transport configs are defined in test_transport_helpers.h
// Define the actual array here
const transport_test_config_t g_transport_configs[] = {
    {DAP_NET_TRANSPORT_HTTP, "HTTP", 18101, "127.0.0.1"},
    {DAP_NET_TRANSPORT_WEBSOCKET, "WebSocket", 18102, "127.0.0.1"},
    {DAP_NET_TRANSPORT_UDP_BASIC, "UDP", 18103, "127.0.0.1"},
    {DAP_NET_TRANSPORT_DNS_TUNNEL, "DNS", 18104, "127.0.0.1"},
};

// Define count as compile-time constant for use in array declarations
#define TRANSPORT_CONFIG_COUNT (sizeof(g_transport_configs) / sizeof(g_transport_configs[0]))
// Also export as runtime variable for use in other files
const size_t g_transport_config_count = TRANSPORT_CONFIG_COUNT;

// Per-transport test context
typedef struct transport_test_context {
    transport_test_config_t config;
    dap_net_transport_server_t *server;
    dap_client_t *clients[TEST_PARALLEL_TRANSPORTS];
    test_stream_ch_context_t stream_ctxs[TEST_PARALLEL_TRANSPORTS];
    dap_stream_node_addr_t client_node_addrs[TEST_PARALLEL_TRANSPORTS];  // Unique client node addresses
    pthread_t thread;
    int result;
    bool running;
} transport_test_context_t;

// Global test state
static transport_test_context_t s_transport_contexts[TRANSPORT_CONFIG_COUNT];
static pthread_mutex_t s_test_mutex = PTHREAD_MUTEX_INITIALIZER;

// =======================================================================================
// HELPER FUNCTIONS
// =======================================================================================

/**
 * @brief Echo callback for test channels - sends received data back to client
 */
static bool s_test_channel_echo_callback(dap_stream_ch_t *a_ch, void *a_arg)
{
    if (!a_ch || !a_arg) {
        return false;
    }
    
    // a_arg is dap_stream_ch_pkt_t*
    dap_stream_ch_pkt_t *a_pkt = (dap_stream_ch_pkt_t *)a_arg;
    
    // Echo data back to client
    debug_if(true, L_DEBUG, "Echoing %u bytes back to client on channel '%c'", 
             a_pkt->hdr.data_size, a_ch->proc->id);
    
    // Send data back through the same channel
    ssize_t l_sent = dap_stream_ch_pkt_write_unsafe(a_ch, a_pkt->hdr.type, 
                                                    a_pkt->data, a_pkt->hdr.data_size);
    
    if (l_sent < 0) {
        log_it(L_WARNING, "Failed to echo data back to client");
        return false;
    }
    
    return true; // Security check passed
}

/**
 * @brief Test fill_net_info callback for link manager
 */
static int s_test_fill_net_info(dap_link_t *a_link) {
    // Minimal stub for testing - just return success
    (void)a_link;
    return 0;
}

// Mock functions for global_db dependencies
DAP_MOCK_DECLARE(dap_global_db_driver_get_groups_by_mask);
DAP_MOCK_DECLARE(dap_global_db_erase_table_sync);

// Mock implementations using DAP_MOCK_WRAPPER_CUSTOM
DAP_MOCK_WRAPPER_CUSTOM(dap_list_t*, dap_global_db_driver_get_groups_by_mask,
    PARAM(const char*, a_group_mask)
)
{
    UNUSED(a_group_mask);
    // Return empty list - no groups to clean up
    return NULL;
}

DAP_MOCK_WRAPPER_CUSTOM(int, dap_global_db_erase_table_sync,
    PARAM(const char*, a_table_name)
)
{
    UNUSED(a_table_name);
    // Mock successful erase
    return 0;
}

/**
 * @brief Initialize all transport systems
 */
static int test_init_all_transports(void)
{
    TEST_INFO("Initializing all transport systems");
    
    // Events system is already initialized in main()
    // Just check if it's initialized
    if (!dap_events_workers_init_status()) {
        TEST_ERROR("Events system not initialized");
        return -1;
    }
    
    // Initialize stream system first (required for some modules)
    int l_ret = dap_stream_init(NULL);
    if (l_ret != 0) {
        TEST_ERROR("Stream initialization failed");
        return -2;
    }
    
    // Register channel processors for test channels A, B, C
    // These channels are used in tests but don't have processors registered by default
    // Add packet_in_callback to echo data back to client
    dap_stream_ch_proc_add('A', NULL, NULL, s_test_channel_echo_callback, NULL);
    dap_stream_ch_proc_add('B', NULL, NULL, s_test_channel_echo_callback, NULL);
    dap_stream_ch_proc_add('C', NULL, NULL, s_test_channel_echo_callback, NULL);
    log_it(L_DEBUG, "Registered channel processors for test channels A, B, C with echo callback");
    
    // Modules are initialized automatically via constructors when libraries are loaded
    // Constructors call init functions directly, which register transports
    // Verify all transports are registered (constructors should have registered them)
    bool l_all_transports_registered = true;
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        dap_net_transport_t *l_transport = dap_net_transport_find(g_transport_configs[i].transport_type);
        if (!l_transport) {
            TEST_ERROR("Transport %s not registered", g_transport_configs[i].name);
            l_all_transports_registered = false;
        } else {
            TEST_INFO("Transport %s registered successfully", g_transport_configs[i].name);
        }
    }
    
    if (!l_all_transports_registered) {
        TEST_ERROR("Not all transports are registered");
        return -4;
    }
    
    TEST_SUCCESS("All transport systems initialized");
    return 0;
}

/**
 * @brief Create and start server for a transport type
 */
static int test_create_transport_server(transport_test_context_t *a_ctx)
{
    if (!a_ctx) {
        return -1;
    }
    
    char l_server_name[256];
    snprintf(l_server_name, sizeof(l_server_name), "test_%s_server", a_ctx->config.name);
    
    // Verify server operations are registered before creating server
    const dap_net_transport_server_ops_t *l_ops = dap_net_transport_server_get_ops(a_ctx->config.transport_type);
    if (!l_ops) {
        TEST_ERROR("Server operations not registered for %s transport (type: %d)", 
                   a_ctx->config.name, a_ctx->config.transport_type);
        return -1;
    }
    
    // Create server instance
    a_ctx->server = dap_net_transport_server_new(a_ctx->config.transport_type, l_server_name);
    if (!a_ctx->server) {
        TEST_ERROR("Failed to create %s server", a_ctx->config.name);
        return -1;
    }
    
    // Start server
    const char *l_addr = a_ctx->config.address;
    uint16_t l_port = a_ctx->config.base_port;
    
    int l_ret = dap_net_transport_server_start(a_ctx->server, NULL, &l_addr, &l_port, 1);
    if (l_ret != 0) {
        TEST_ERROR("Failed to start %s server on %s:%u", a_ctx->config.name, l_addr, l_port);
        dap_net_transport_server_delete(a_ctx->server);
        a_ctx->server = NULL;
        return -2;
    }
    
    // Wait for server to be ready (listening)
    if (!test_wait_for_server_ready(a_ctx->server, 2000)) {
        TEST_ERROR("Server not ready within timeout");
        return -6;
    }
    
    TEST_INFO("%s server started on %s:%u", a_ctx->config.name, l_addr, l_port);
    return 0;
}

/**
 * @brief Wait for client to complete full handshake cycle
 */
static bool test_wait_for_full_handshake(dap_client_t *a_client, uint32_t a_timeout_ms)
{
    if (!a_client) {
        return false;
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 5000;
    dap_client_stage_t l_last_stage = STAGE_UNDEFINED;
    
    while (l_elapsed < a_timeout_ms) {
        dap_client_stage_t l_stage = dap_client_get_stage(a_client);
        dap_client_stage_status_t l_status = dap_client_get_stage_status(a_client);
        
        if (l_stage != l_last_stage || l_status != STAGE_STATUS_COMPLETE) {
            printf("  Client stage: %d (status: %d, elapsed: %u ms)\n", l_stage, l_status, l_elapsed);
            l_last_stage = l_stage;
        }
        
        if (l_stage == STAGE_STREAM_STREAMING && l_status == STAGE_STATUS_COMPLETE) {
            return true;
        }
        
        if (l_status == STAGE_STATUS_ERROR) {
            printf("  Client stage error at stage %d\n", l_stage);
            return false;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    printf("  Timeout reached at stage: %d, status: %d\n", l_last_stage, dap_client_get_stage_status(a_client));
    return false;
}

/**
 * @brief Create and configure client for a transport
 */
static dap_client_t *test_create_transport_client(transport_test_config_t *a_config, uint16_t a_port_offset, dap_stream_node_addr_t *a_client_node_addr)
{
    // Initialize client system (idempotent)
    dap_client_init();
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    if (!l_client) {
        TEST_ERROR("Failed to create %s client", a_config->name);
        return NULL;
    }
    
    // Set transport type
    dap_client_set_transport_type(l_client, a_config->transport_type);
    
    // Wait for client initialization
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(l_client, 2000);
    if (!l_client_ready) {
        TEST_ERROR("Client initialization timeout");
        dap_client_delete_unsafe(l_client);
        return NULL;
    }
    
    // Set uplink address and port
    // NOTE: All clients connect to the same server port (base_port)
    // The server handles multiple clients on the same port
    // link_info.node_addr should be server's address (g_node_addr)
    // This will be updated from server signature during handshake
    uint16_t l_port = a_config->base_port;
    dap_stream_node_addr_t l_server_node_addr = g_node_addr; // Use server's global node address
    dap_client_set_uplink_unsafe(l_client, &l_server_node_addr, a_config->address, l_port);
    
    // Set client's certificate if node address provided
    // This certificate will be used during handshake to identify client
    // The address from certificate will be used on server side to identify the stream
    if (a_client_node_addr && a_client_node_addr->uint64 != 0) {
        // Find certificate by name (it was created by dap_test_generate_unique_node_addr)
        // Format: "test_client_%s_%zu_%zu"
        char l_cert_name[256];
        snprintf(l_cert_name, sizeof(l_cert_name), "test_client_%s_%zu_%zu", 
                 a_config->name, (size_t)pthread_self(), (size_t)a_port_offset);
        dap_cert_t *l_client_cert = dap_cert_find_by_name(l_cert_name);
        if (l_client_cert) {
            dap_client_set_auth_cert(l_client, l_cert_name);
            log_it(L_DEBUG, "Set client certificate '%s' for node address "NODE_ADDR_FP_STR, 
                   l_cert_name, NODE_ADDR_FP_ARGS_S(*a_client_node_addr));
        } else {
            log_it(L_WARNING, "Certificate '%s' not found, client address may not be set correctly", l_cert_name);
        }
    }
    
    // Set active channels
    dap_client_set_active_channels_unsafe(l_client, "ABC");
    
    return l_client;
}

/**
 * @brief Test single transport with parallel clients
 */
static void *test_transport_worker(void *a_arg)
{
    transport_test_context_t *l_ctx = (transport_test_context_t *)a_arg;
    l_ctx->result = 0;
    l_ctx->running = true;
    
    pthread_mutex_lock(&s_test_mutex);
    printf("\n=== Starting %s transport test ===\n", l_ctx->config.name);
    pthread_mutex_unlock(&s_test_mutex);
    
    // Create server
    if (test_create_transport_server(l_ctx) != 0) {
        l_ctx->result = -1;
        l_ctx->running = false;
        return NULL;
    }
    
    // Note: We don't need to generate unique server node address for tests
    // Server uses g_node_addr which is set from node-addr certificate during dap_stream_init()
    // Each transport server uses the same g_node_addr
    // For tests, we generate unique client node addresses to ensure each client has unique identity
    // But server address is always g_node_addr (the global server node address)
    
    // Initialize all client contexts and generate unique client node addresses
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        if (test_stream_ch_context_init(&l_ctx->stream_ctxs[i], TEST_STREAM_CH_ID, TEST_LARGE_DATA_SIZE) != 0) {
            TEST_ERROR("Failed to initialize stream channel context %zu for %s", i, l_ctx->config.name);
            l_ctx->result = -2;
            l_ctx->running = false;
            return NULL;
        }
        
        // Generate unique client node address
        char l_client_cert_name[256];
        snprintf(l_client_cert_name, sizeof(l_client_cert_name), "test_client_%s_%zu_%zu", 
                 l_ctx->config.name, (size_t)pthread_self(), i);
        if (dap_test_generate_unique_node_addr(l_client_cert_name, DAP_STREAM_NODE_ADDR_CERT_TYPE, 
                                               &l_ctx->client_node_addrs[i]) != 0) {
            TEST_ERROR("Failed to generate client node address %zu for %s", i, l_ctx->config.name);
            l_ctx->result = -2;
            l_ctx->running = false;
            return NULL;
        }
        log_it(L_DEBUG, "Generated client %zu node address for %s: "NODE_ADDR_FP_STR, 
               i, l_ctx->config.name, NODE_ADDR_FP_ARGS_S(l_ctx->client_node_addrs[i]));
    }
    
    // Create all clients with their unique node addresses
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        l_ctx->clients[i] = test_create_transport_client(&l_ctx->config, i, &l_ctx->client_node_addrs[i]);
        if (!l_ctx->clients[i]) {
            TEST_ERROR("Failed to create client %zu for %s", i, l_ctx->config.name);
            l_ctx->result = -3;
            l_ctx->running = false;
            return NULL;
        }
    }
    
    // Start handshake for all clients
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        dap_client_go_stage(l_ctx->clients[i], STAGE_STREAM_STREAMING, NULL);
    }
    
    // Wait for all clients to complete handshake
    bool l_all_ready = true;
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        bool l_ready = test_wait_for_full_handshake(l_ctx->clients[i], TEST_TRANSPORT_TIMEOUT_MS);
        if (!l_ready) {
            TEST_ERROR("Client %zu for %s failed to complete handshake", i, l_ctx->config.name);
            l_all_ready = false;
            l_ctx->result = -4;
            break;
        }
    }
    
    if (!l_all_ready) {
        l_ctx->running = false;
        return NULL;
    }
    
    pthread_mutex_lock(&s_test_mutex);
    printf("  All %s clients completed handshake successfully\n", l_ctx->config.name);
    pthread_mutex_unlock(&s_test_mutex);
    
    // link_info.node_addr should already be set to g_node_addr (server address)
    // No need to update it after handshake - it's already correct
    // Server address is used for finding stream on server when registering receivers
    
    // Wait for stream connection to complete and channels to be created
    // Channels are created on server when client connects to stream endpoint
    // Channels are created on client in STAGE_STREAM_CONNECTED
    // We need to wait for this before registering receivers
    bool l_all_channels_ready = true;
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        if (!test_wait_for_stream_channels_ready(l_ctx->clients[i], "ABC", 5000)) {
            TEST_ERROR("Channels not ready for client %zu in %s", i, l_ctx->config.name);
            l_all_channels_ready = false;
        }
    }
    
    if (!l_all_channels_ready) {
        TEST_ERROR("Not all channels ready");
        l_ctx->result = -4;
        l_ctx->running = false;
        return NULL;
    }
    
    // Register stream channel receivers for all clients
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        int l_ret = test_stream_ch_register_receiver(l_ctx->clients[i], TEST_STREAM_CH_ID, &l_ctx->stream_ctxs[i]);
        if (l_ret != 0) {
            TEST_ERROR("Failed to register receiver for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -5;
            l_ctx->running = false;
            return NULL;
        }
    }
    
    // Send large data volumes for all clients in parallel
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        int l_ret = test_stream_ch_send_and_wait(l_ctx->clients[i], &l_ctx->stream_ctxs[i], TEST_TRANSPORT_TIMEOUT_MS);
        if (l_ret != 0) {
            TEST_ERROR("Data exchange failed for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -6;
            l_ctx->running = false;
            return NULL;
        }
        
        // Verify data integrity
        pthread_mutex_lock(&l_ctx->stream_ctxs[i].mutex);
        bool l_data_received = l_ctx->stream_ctxs[i].data_received;
        pthread_mutex_unlock(&l_ctx->stream_ctxs[i].mutex);
        
        if (!l_data_received) {
            TEST_ERROR("Data not received for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -7;
            l_ctx->running = false;
            return NULL;
        }
        
        // Verify data integrity
        if (l_ctx->stream_ctxs[i].received_data && l_ctx->stream_ctxs[i].received_data_size > 0) {
            bool l_data_valid = test_transport_verify_data(l_ctx->stream_ctxs[i].sent_data,
                                                           l_ctx->stream_ctxs[i].received_data,
                                                           l_ctx->stream_ctxs[i].received_data_size);
            if (!l_data_valid) {
                TEST_ERROR("Data integrity check failed for client %zu in %s", i, l_ctx->config.name);
                l_ctx->result = -8;
                l_ctx->running = false;
                return NULL;
            }
        }
    }
    
    pthread_mutex_lock(&s_test_mutex);
    printf("  All %s clients completed data exchange successfully (%u MB per client)\n", 
           l_ctx->config.name, TEST_LARGE_DATA_SIZE / (1024 * 1024));
    pthread_mutex_unlock(&s_test_mutex);
    
    l_ctx->result = 0;
    l_ctx->running = false;
    return NULL;
}

/**
 * @brief Cleanup transport test context
 */
static void test_cleanup_transport_context(transport_test_context_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }
    
    // Cleanup clients
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        if (a_ctx->clients[i]) {
            // Use mt version to safely delete from any thread
            dap_client_delete_mt(a_ctx->clients[i]);
            // Wait for client to be deleted
            test_wait_for_client_deleted(&a_ctx->clients[i], 1000);
            a_ctx->clients[i] = NULL;
        }
        test_stream_ch_context_cleanup(&a_ctx->stream_ctxs[i]);
    }
    
    // Wait for all streams to close
    test_wait_for_all_streams_closed(1000);
    
    // Cleanup server
    if (a_ctx->server) {
        dap_net_transport_server_stop(a_ctx->server);
        dap_net_transport_server_delete(a_ctx->server);
        a_ctx->server = NULL;
    }
}

// =======================================================================================
// TEST CASES
// =======================================================================================

/**
 * @brief Test 1: Initialize all transport systems
 */
static void test_01_init_all_transports(void)
{
    TEST_INFO("Test 1: Initializing all transport systems");
    
    int l_ret = test_init_all_transports();
    // Modules are initialized automatically via constructors when libraries are loaded
    // We just verify that transports are registered
    TEST_ASSERT(l_ret == 0, "All transport systems should initialize successfully (transports must be registered)");
    
    TEST_SUCCESS("Test 1 passed: All transport systems initialized");
}

/**
 * @brief Test 2: Parallel transport testing
 */
static void test_02_parallel_transport_testing(void)
{
    TEST_INFO("Test 2: Parallel transport testing with full handshake cycle");
    
    // Initialize all transport contexts
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        s_transport_contexts[i].config = g_transport_configs[i];
        s_transport_contexts[i].server = NULL;
        memset(s_transport_contexts[i].clients, 0, sizeof(s_transport_contexts[i].clients));
        memset(s_transport_contexts[i].client_node_addrs, 0, sizeof(s_transport_contexts[i].client_node_addrs));
        s_transport_contexts[i].result = 0;
        s_transport_contexts[i].running = false;
    }
    
    // Start all transport tests in parallel threads
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        int l_ret = pthread_create(&s_transport_contexts[i].thread, NULL, test_transport_worker, &s_transport_contexts[i]);
        TEST_ASSERT(l_ret == 0, "Failed to create thread for transport %s", g_transport_configs[i].name);
    }
    
    // Wait for all threads to complete
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        pthread_join(s_transport_contexts[i].thread, NULL);
    }
    
    // Check results
    bool l_all_passed = true;
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        if (s_transport_contexts[i].result != 0) {
            TEST_ERROR("Transport %s test failed with code %d", g_transport_configs[i].name, s_transport_contexts[i].result);
            l_all_passed = false;
        }
    }
    
    TEST_ASSERT(l_all_passed, "All transport tests should pass");
    
    TEST_SUCCESS("Test 2 passed: All transports tested in parallel");
}

/**
 * @brief Test 3: Cleanup all resources
 */
static void test_03_cleanup_all_resources(void)
{
    TEST_INFO("Test 3: Cleaning up all resources");
    
    // Cleanup all transport contexts (stops servers and deletes clients)
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        test_cleanup_transport_context(&s_transport_contexts[i]);
    }
    
    // Wait for all streams to close
    test_wait_for_all_streams_closed(1000);
    
    // Cleanup client system
    dap_client_deinit();
    
    // Cleanup stream system
    dap_stream_deinit();
    
    // Deinitialize link manager
    dap_link_manager_deinit();
    
    // Deinitialize modules BEFORE stopping events system
    // Some modules may need events system to be active during cleanup
    log_it(L_DEBUG, "Deinitializing all modules...");
    dap_module_deinit_all();
    
    // Deinit events system (it will stop workers and wait for them internally)
    if (dap_events_workers_init_status()) {
        log_it(L_DEBUG, "Deinitializing events system...");
        dap_events_deinit();
    }
    
    TEST_SUCCESS("Test 3 passed: All resources cleaned up");
}

// =======================================================================================
// MAIN TEST SUITE
// =======================================================================================

int main(void)
{
    // Create minimal config file for tests
    const char *config_content = "[resources]\n"
                                 "ca_folders=[./test_ca]\n"
                                 "[dap_client]\n"
                                 "max_tries=3\n"
                                 "timeout=20\n"
                                 "debug_more=true\n"
                                 "timeout_active_after_connect=15\n"
                                 "[stream]\n"
                                 "debug_more=true\n"
                                 "debug_channels=true\n"
                                 "debug_dump_stream_headers=false\n";
    FILE *f = fopen("test_transport.cfg", "w");
    if (f) {
        fwrite(config_content, 1, strlen(config_content), f);
        fclose(f);
    }
    
    // Initialize common DAP subsystems
    dap_common_init(LOG_TAG, NULL);
    // Set logging output to stdout and level to DEBUG
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_level_set(L_DEBUG);
    dap_config_init(".");
    
    // Open config and set as global
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_transport");
    if (!g_config) {
        printf("Failed to open config\n");
        return -1;
    }
    
    // Initialize encryption system
    dap_enc_init();
    
    // Initialize events system (required for dap_proc_thread_get_auto used by dap_link_manager)
    int l_events_ret = dap_events_init(1, 60000);
    if (l_events_ret != 0) {
        log_it(L_ERROR, "dap_events_init failed: %d", l_events_ret);
        return -10;
    }
    
    // Start events system (required for dap_proc_thread_init inside dap_events_start)
    dap_events_start();
    
    // Initialize link manager (required for stream operations)
    dap_link_manager_callbacks_t l_link_manager_callbacks = {
        .connected = NULL,
        .disconnected = NULL,
        .error = NULL,
        .fill_net_info = s_test_fill_net_info,
        .link_request = NULL,
        .link_count_changed = NULL
    };
    int l_link_manager_ret = dap_link_manager_init(&l_link_manager_callbacks);
    if (l_link_manager_ret != 0) {
        log_it(L_ERROR, "Link manager initialization failed (may be OK for basic tests): %d", l_link_manager_ret);
        return -11;
    }
    
    // Setup test certificate environment
    int l_ret = dap_test_setup_certificates(".");
    if (l_ret != 0) {
        printf("Failed to setup test certificates\n");
        return -1;
    }
    
    TEST_SUITE_START("Transport Integration Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing all transports in parallel with full handshake cycle\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_init_all_transports);
    TEST_RUN(test_02_parallel_transport_testing);
    TEST_RUN(test_03_cleanup_all_resources);
    
    TEST_SUITE_END();
    
    // Final cleanup
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    
    // Remove temp config file
    remove("test_transport.cfg");
    
    return 0;
}

