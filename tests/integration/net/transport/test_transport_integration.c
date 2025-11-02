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
#include "dap_stream.h"
#include "dap_stream_transport.h"
#include "dap_net_transport_server.h"

#include "dap_stream_ch.h"
#include "dap_module.h"

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
#define TEST_TRANSPORT_TIMEOUT_MS 30000  // 30 seconds

// Transport type configuration
typedef struct transport_test_config {
    dap_stream_transport_type_t transport_type;
    const char *name;
    uint16_t base_port;
    const char *address;
} transport_test_config_t;

static const transport_test_config_t s_transport_configs[] = {
    {DAP_STREAM_TRANSPORT_HTTP, "HTTP", 18101, "127.0.0.1"},
    {DAP_STREAM_TRANSPORT_WEBSOCKET, "WebSocket", 18102, "127.0.0.1"},
    {DAP_STREAM_TRANSPORT_UDP_BASIC, "UDP", 18103, "127.0.0.1"},
    {DAP_STREAM_TRANSPORT_DNS_TUNNEL, "DNS", 18104, "127.0.0.1"},
};

#define TRANSPORT_CONFIG_COUNT (sizeof(s_transport_configs) / sizeof(s_transport_configs[0]))

// Per-transport test context
typedef struct transport_test_context {
    transport_test_config_t config;
    dap_net_transport_server_t *server;
    dap_client_t *clients[TEST_PARALLEL_TRANSPORTS];
    test_stream_ch_context_t stream_ctxs[TEST_PARALLEL_TRANSPORTS];
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
 * @brief Initialize all transport systems
 */
static int test_init_all_transports(void)
{
    TEST_INFO("Initializing all transport systems");
    
    // Initialize event system
    int l_ret = dap_events_init(1, 60000);
    if (l_ret != 0) {
        TEST_ERROR("Events initialization failed");
        return -1;
    }
    
    dap_events_start();
    
    // Initialize stream system first (required for some modules)
    l_ret = dap_stream_init(NULL);
    if (l_ret != 0) {
        TEST_ERROR("Stream initialization failed");
        return -2;
    }
    
    // Initialize all registered modules via dap_module system
    // This must be called after basic subsystems are initialized
    // to ensure transport server modules are initialized and registered
    // Note: Some modules may fail to initialize, but that's OK if they're not needed
    
    // Check if any modules are registered before calling init_all
    // If constructors didn't run, we need to manually register transport modules
    log_it(L_DEBUG, "Calling dap_module_init_all()...");
    int l_module_ret = dap_module_init_all();
    if (l_module_ret != 0) {
        // Log warning but continue - some modules may fail if dependencies aren't met
        TEST_ERROR("Some modules failed to initialize");
        return -22;
    }
    
    // Give system time to stabilize
    dap_test_sleep_ms(200);
    
    // Verify all transports are registered
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        dap_stream_transport_t *l_transport = dap_stream_transport_find(s_transport_configs[i].transport_type);
        if (!l_transport) {
            TEST_ERROR("Transport %s not registered", s_transport_configs[i].name);
            return -4;
        }
        TEST_INFO("Transport %s registered successfully", s_transport_configs[i].name);
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
    
    // Give server time to bind and start listening
    dap_test_sleep_ms(500);
    
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
    const uint32_t l_poll_interval_ms = 50;
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
static dap_client_t *test_create_transport_client(transport_test_config_t *a_config, uint16_t a_port_offset)
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
    
    // Initialize test node address
    dap_stream_node_addr_t l_node_addr;
    memset(&l_node_addr, 0, sizeof(l_node_addr));
    
    // Set uplink address and port
    uint16_t l_port = a_config->base_port + a_port_offset;
    dap_client_set_uplink_unsafe(l_client, &l_node_addr, a_config->address, l_port);
    
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
    
    // Initialize all client contexts
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        if (test_stream_ch_context_init(&l_ctx->stream_ctxs[i], TEST_STREAM_CH_ID, TEST_LARGE_DATA_SIZE) != 0) {
            TEST_ERROR("Failed to initialize stream channel context %zu for %s", i, l_ctx->config.name);
            l_ctx->result = -2;
            l_ctx->running = false;
            return NULL;
        }
    }
    
    // Create all clients
    for (size_t i = 0; i < TEST_PARALLEL_TRANSPORTS; i++) {
        l_ctx->clients[i] = test_create_transport_client(&l_ctx->config, i);
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
    
    // Give system time to stabilize
    dap_test_sleep_ms(500);
    
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
            dap_client_delete_unsafe(a_ctx->clients[i]);
            a_ctx->clients[i] = NULL;
        }
        test_stream_ch_context_cleanup(&a_ctx->stream_ctxs[i]);
    }
    
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
    TEST_ASSERT(l_ret == 0, "All transport systems should initialize successfully");
    
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
        s_transport_contexts[i].config = s_transport_configs[i];
        s_transport_contexts[i].server = NULL;
        memset(s_transport_contexts[i].clients, 0, sizeof(s_transport_contexts[i].clients));
        s_transport_contexts[i].result = 0;
        s_transport_contexts[i].running = false;
    }
    
    // Start all transport tests in parallel threads
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        int l_ret = pthread_create(&s_transport_contexts[i].thread, NULL, test_transport_worker, &s_transport_contexts[i]);
        TEST_ASSERT(l_ret == 0, "Failed to create thread for transport %s", s_transport_configs[i].name);
    }
    
    // Wait for all threads to complete
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        pthread_join(s_transport_contexts[i].thread, NULL);
    }
    
    // Check results
    bool l_all_passed = true;
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        if (s_transport_contexts[i].result != 0) {
            TEST_ERROR("Transport %s test failed with code %d", s_transport_configs[i].name, s_transport_contexts[i].result);
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
    
    // Cleanup all transport contexts
    for (size_t i = 0; i < TRANSPORT_CONFIG_COUNT; i++) {
        test_cleanup_transport_context(&s_transport_contexts[i]);
    }
    
    // Cleanup client system
    dap_client_deinit();
    
    // Cleanup stream system
    dap_stream_deinit();
    
    // Deinitialize all modules via dap_module system
    dap_module_deinit_all();
    
    // Give time for cleanup to propagate
    dap_test_sleep_ms(200);
    
    // Deinit events system
    if (dap_events_workers_init_status()) {
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
                                 "timeout_active_after_connect=15\n";
    FILE *f = fopen("test_transport.cfg", "w");
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
    g_config = dap_config_open("test_transport");
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

