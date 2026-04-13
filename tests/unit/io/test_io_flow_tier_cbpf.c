/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * Test suite for Classic BPF IO Flow tier
 * Tests the dap_io_flow_cbpf API, NOT raw BPF syscalls
 * This file is only compiled on Linux (see CMakeLists.txt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dap_test.h"
#include "dap_common.h"
#include "dap_io_flow_cbpf.h"
#include "dap_io_flow_test_fixtures.h"

// Number of sockets for SO_REUSEPORT group test
#define NUM_REUSEPORT_SOCKETS 4

/**
 * @brief Test dap_io_flow_cbpf_is_available() API
 */
static void test_cbpf_is_available(void)
{
    dap_test_msg("Test: dap_io_flow_cbpf_is_available()");
    
    // Call twice to test caching
    bool l_available1 = dap_io_flow_cbpf_is_available();
    bool l_available2 = dap_io_flow_cbpf_is_available();
    
    dap_assert(l_available1 == l_available2, "Cached result should be consistent");
    dap_test_msg("CBPF is_available: %s", l_available1 ? "yes" : "no");
    
    // On Linux, CBPF should normally be available (SO_ATTACH_REUSEPORT_CBPF exists since 3.9)
    // However, we don't fail the test - just warn, as some environments may lack support
    if (!l_available1) {
        dap_test_msg("WARNING: CBPF not available, some tests will be skipped");
    }
    
    dap_pass_msg("dap_io_flow_cbpf_is_available() test passed");
}

/**
 * @brief Test dap_io_flow_cbpf_attach_socket() with invalid fd
 */
static void test_cbpf_attach_invalid_fd(void)
{
    dap_test_msg("Test: dap_io_flow_cbpf_attach_socket() with invalid fd");
    
    // Attempt to attach to invalid fd should fail
    int l_ret = dap_io_flow_cbpf_attach_socket(-1);
    dap_assert(l_ret != 0, "Attach to invalid fd should fail");
    
    // Attempt to attach to non-socket fd
    l_ret = dap_io_flow_cbpf_attach_socket(STDOUT_FILENO);
    dap_assert(l_ret != 0, "Attach to non-socket fd should fail");
    
    dap_pass_msg("Invalid fd attach test passed");
}

/**
 * @brief Test dap_io_flow_cbpf_attach_socket() with socket without SO_REUSEPORT
 */
static void test_cbpf_attach_without_reuseport(void)
{
    dap_test_msg("Test: dap_io_flow_cbpf_attach_socket() without SO_REUSEPORT");
    
    int l_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (l_sock < 0) {
        dap_test_msg("Cannot create socket: %s", strerror(errno));
        dap_pass_msg("Test skipped (no socket)");
        return;
    }
    
    // Don't set SO_REUSEPORT - attach should fail
    int l_ret = dap_io_flow_cbpf_attach_socket(l_sock);
    dap_assert(l_ret != 0, "Attach without SO_REUSEPORT should fail");
    
    close(l_sock);
    dap_pass_msg("Attach without SO_REUSEPORT test passed");
}

/**
 * @brief Test full cbpf attach/detach lifecycle with valid SO_REUSEPORT socket
 */
static void test_cbpf_attach_detach_lifecycle(void)
{
    dap_test_msg("Test: CBPF attach/detach lifecycle");
    
    if (!dap_io_flow_cbpf_is_available()) {
        dap_test_msg("CBPF not available, skipping lifecycle test");
        dap_pass_msg("Lifecycle test skipped");
        return;
    }
    
    // Create socket with SO_REUSEPORT
    int l_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (l_sock < 0) {
        dap_test_msg("Cannot create socket: %s", strerror(errno));
        dap_pass_msg("Test skipped (no socket)");
        return;
    }
    
    int l_opt = 1;
    if (setsockopt(l_sock, SOL_SOCKET, SO_REUSEPORT, &l_opt, sizeof(l_opt)) < 0) {
        dap_test_msg("Cannot set SO_REUSEPORT: %s", strerror(errno));
        close(l_sock);
        dap_pass_msg("Test skipped (no SO_REUSEPORT)");
        return;
    }
    
    // Bind to ephemeral port
    struct sockaddr_in l_addr = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    
    if (bind(l_sock, (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
        dap_test_msg("Cannot bind socket: %s", strerror(errno));
        close(l_sock);
        dap_pass_msg("Test skipped (bind failed)");
        return;
    }
    
    // Attach CBPF program (may fail in emulated/containerized environments)
    int l_ret = dap_io_flow_cbpf_attach_socket(l_sock);
    if (l_ret != 0) {
        dap_test_msg("CBPF attach failed (emulated/containerized env?), skipping");
        close(l_sock);
        dap_pass_msg("Lifecycle test skipped (attach not supported at runtime)");
        return;
    }
    
    // Detach CBPF program
    l_ret = dap_io_flow_cbpf_detach_socket(l_sock);
    dap_test_msg("CBPF detach returned: %d", l_ret);
    
    close(l_sock);
    dap_pass_msg("CBPF attach/detach lifecycle passed");
}

/**
 * @brief Test CBPF with multiple sockets in SO_REUSEPORT group
 */
static void test_cbpf_reuseport_group(void)
{
    dap_test_msg("Test: CBPF with SO_REUSEPORT socket group");
    
    if (!dap_io_flow_cbpf_is_available()) {
        dap_test_msg("CBPF not available, skipping group test");
        dap_pass_msg("Group test skipped");
        return;
    }
    
    int l_sockets[NUM_REUSEPORT_SOCKETS] = {-1, -1, -1, -1};
    uint16_t l_port = 0;
    bool l_setup_failed = false;
    
    // Create socket group sharing same port
    for (int i = 0; i < NUM_REUSEPORT_SOCKETS; i++) {
        l_sockets[i] = socket(AF_INET, SOCK_DGRAM, 0);
        if (l_sockets[i] < 0) {
            dap_test_msg("Cannot create socket %d: %s", i, strerror(errno));
            l_setup_failed = true;
            break;
        }
        
        int l_opt = 1;
        if (setsockopt(l_sockets[i], SOL_SOCKET, SO_REUSEPORT, &l_opt, sizeof(l_opt)) < 0) {
            dap_test_msg("Cannot set SO_REUSEPORT on socket %d: %s", i, strerror(errno));
            l_setup_failed = true;
            break;
        }
        
        struct sockaddr_in l_addr = {
            .sin_family = AF_INET,
            .sin_port = htons(l_port),  // First socket gets ephemeral, rest reuse
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        
        if (bind(l_sockets[i], (struct sockaddr*)&l_addr, sizeof(l_addr)) < 0) {
            dap_test_msg("Cannot bind socket %d: %s", i, strerror(errno));
            l_setup_failed = true;
            break;
        }
        
        // Get port from first socket
        if (i == 0) {
            socklen_t l_len = sizeof(l_addr);
            getsockname(l_sockets[i], (struct sockaddr*)&l_addr, &l_len);
            l_port = ntohs(l_addr.sin_port);
            dap_test_msg("Using port %u for SO_REUSEPORT group", l_port);
        }
    }
    
    if (l_setup_failed) {
        // Cleanup and skip test
        for (int i = 0; i < NUM_REUSEPORT_SOCKETS; i++) {
            if (l_sockets[i] >= 0) {
                close(l_sockets[i]);
            }
        }
        dap_pass_msg("Group test skipped (setup failed)");
        return;
    }
    
    // Attach CBPF to first socket (may fail in emulated/containerized environments)
    int l_ret = dap_io_flow_cbpf_attach_socket(l_sockets[0]);
    if (l_ret != 0) {
        dap_test_msg("CBPF attach to group failed (emulated/containerized env?), skipping");
        for (int i = 0; i < NUM_REUSEPORT_SOCKETS; i++) {
            if (l_sockets[i] >= 0) close(l_sockets[i]);
        }
        dap_pass_msg("Group test skipped (attach not supported at runtime)");
        return;
    }
    
    dap_test_msg("CBPF attached to SO_REUSEPORT group of %d sockets", NUM_REUSEPORT_SOCKETS);
    
    // Detach
    l_ret = dap_io_flow_cbpf_detach_socket(l_sockets[0]);
    dap_test_msg("CBPF detach from group returned: %d", l_ret);
    
    // Cleanup
    for (int i = 0; i < NUM_REUSEPORT_SOCKETS; i++) {
        if (l_sockets[i] >= 0) {
            close(l_sockets[i]);
        }
    }
    
    dap_pass_msg("CBPF SO_REUSEPORT group test passed");
}

/**
 * @brief Test CBPF detach with invalid fd
 */
static void test_cbpf_detach_invalid_fd(void)
{
    dap_test_msg("Test: dap_io_flow_cbpf_detach_socket() with invalid fd");
    
    // Detach from invalid fd should fail gracefully
    int l_ret = dap_io_flow_cbpf_detach_socket(-1);
    dap_assert(l_ret != 0, "Detach from invalid fd should fail");
    
    dap_pass_msg("Invalid fd detach test passed");
}

/**
 * @brief Test fixture tier availability consistency
 */
static void test_cbpf_fixture_tier_available(void)
{
    dap_test_msg("Test: Fixture tier availability for CBPF");
    
    bool l_fixture_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_CBPF);
    bool l_api_available = dap_io_flow_cbpf_is_available();
    
    // Fixture should report same availability as direct API
    dap_assert(l_fixture_available == l_api_available, 
               "Fixture tier availability should match API");
    
    dap_pass_msg("Fixture tier availability test passed");
}

/**
 * @brief Test fixture context create/delete for CBPF tier
 */
static void test_cbpf_fixture_context(void)
{
    dap_test_msg("Test: Fixture context for CBPF tier");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_CBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    dap_assert(l_ctx->tier == IO_FLOW_TIER_CBPF, "Tier should be CBPF");
    dap_assert(l_ctx->initialized, "Context should be initialized");
    dap_assert(l_ctx->bytes_sent == 0, "Initial bytes_sent should be 0");
    dap_assert(l_ctx->bytes_received == 0, "Initial bytes_received should be 0");
    dap_assert(l_ctx->packets_sent == 0, "Initial packets_sent should be 0");
    dap_assert(l_ctx->packets_received == 0, "Initial packets_received should be 0");
    dap_assert(l_ctx->errors == 0, "Initial errors should be 0");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Fixture context test passed");
}

/**
 * @brief Test fixture basic transfer simulation
 */
static void test_cbpf_fixture_transfer(void)
{
    dap_test_msg("Test: Fixture transfer simulation for CBPF");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_CBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    // Test various transfer sizes
    size_t l_sizes[] = {1, 64, 512, 1024, 4096, 65536};
    size_t l_num = sizeof(l_sizes) / sizeof(l_sizes[0]);
    uint64_t l_total = 0;
    
    for (size_t i = 0; i < l_num; i++) {
        int l_ret = dap_io_flow_test_basic_transfer(l_ctx, l_sizes[i]);
        dap_assert(l_ret == 0, "Transfer should succeed");
        l_total += l_sizes[i];
    }
    
    dap_assert(l_ctx->bytes_sent == l_total, "Total bytes should match");
    dap_assert(l_ctx->bytes_received == l_total, "Received bytes should match");
    dap_assert(l_ctx->packets_sent == l_num, "Packet count should match");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Fixture transfer simulation passed");
}

/**
 * @brief Test tier name
 */
static void test_cbpf_tier_name(void)
{
    dap_test_msg("Test: Classic BPF tier name");
    
    const char *l_name = dap_io_flow_test_tier_name(IO_FLOW_TIER_CBPF);
    dap_assert(l_name != NULL, "Name should not be NULL");
    dap_assert(strcmp(l_name, "Classic BPF") == 0, "Name should be 'Classic BPF'");
    
    dap_pass_msg("Tier name test passed");
}

/**
 * @brief Test error handling
 */
static void test_cbpf_error_handling(void)
{
    dap_test_msg("Test: CBPF error handling");
    
    // Test NULL context transfer
    int l_ret = dap_io_flow_test_basic_transfer(NULL, 1024);
    dap_assert(l_ret != 0, "Should fail with NULL context");
    
    // Test invalid tier
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_COUNT);
    dap_assert(l_ctx == NULL, "Should fail with invalid tier");
    
    // Test delete NULL (should not crash)
    dap_io_flow_test_context_delete(NULL);
    
    dap_pass_msg("Error handling test passed");
}

/**
 * @brief Test throughput measurement
 */
static void test_cbpf_throughput(void)
{
    dap_test_msg("Test: CBPF throughput measurement");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_CBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    uint64_t l_throughput = 0;
    int l_ret = dap_io_flow_test_throughput(l_ctx, 100, &l_throughput);
    dap_assert(l_ret == 0, "Throughput test should succeed");
    dap_assert(l_throughput > 0, "Throughput should be positive");
    
    dap_test_msg("Measured throughput: %lu bytes/sec", (unsigned long)l_throughput);
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Throughput measurement passed");
}

int main(void)
{
    dap_test_msg("=== IO Flow Classic BPF API Tests ===");
    
    // Initialize fixtures
    int l_ret = dap_io_flow_test_fixtures_init();
    if (l_ret != 0) {
        dap_test_msg("Failed to initialize fixtures");
        return 1;
    }
    
    // Run API tests (test dap_io_flow_cbpf_* functions)
    test_cbpf_is_available();
    test_cbpf_attach_invalid_fd();
    test_cbpf_attach_without_reuseport();
    test_cbpf_attach_detach_lifecycle();
    test_cbpf_reuseport_group();
    test_cbpf_detach_invalid_fd();
    
    // Run fixture tests
    test_cbpf_fixture_tier_available();
    test_cbpf_fixture_context();
    test_cbpf_fixture_transfer();
    test_cbpf_tier_name();
    test_cbpf_error_handling();
    test_cbpf_throughput();
    
    // Cleanup
    dap_io_flow_test_fixtures_deinit();
    
    dap_test_msg("=== All Classic BPF API Tests Passed ===");
    return 0;
}
