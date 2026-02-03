/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * Test suite for Extended BPF (eBPF) IO Flow tier
 * Tests the dap_io_flow_ebpf API, NOT raw BPF syscalls
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
#include "dap_io_flow_ebpf.h"
#include "dap_io_flow_test_fixtures.h"

// Number of sockets for SO_REUSEPORT group test
#define NUM_REUSEPORT_EBPF_SOCKETS 4

/**
 * @brief Test dap_io_flow_ebpf_is_available() API
 */
static void test_ebpf_is_available(void)
{
    dap_test_msg("Test: dap_io_flow_ebpf_is_available()");
    
    // Call twice to test caching
    bool l_available1 = dap_io_flow_ebpf_is_available();
    bool l_available2 = dap_io_flow_ebpf_is_available();
    
    dap_assert(l_available1 == l_available2, "Cached result should be consistent");
    dap_test_msg("eBPF is_available: %s", l_available1 ? "yes" : "no");
    
    if (l_available1) {
        dap_test_msg("eBPF available: process has CAP_BPF or running as root");
    } else {
        dap_test_msg("eBPF not available: need CAP_BPF, CAP_NET_ADMIN, or root");
        dap_test_msg("To enable: sudo setcap cap_bpf,cap_net_admin=ep <test_binary>");
    }
    
    dap_pass_msg("dap_io_flow_ebpf_is_available() test passed");
}

/**
 * @brief Test eBPF availability vs privilege level
 */
static void test_ebpf_capability_detection(void)
{
    dap_test_msg("Test: eBPF capability detection");
    
    bool l_capable = dap_io_flow_ebpf_is_available();
    bool l_is_root = (geteuid() == 0);
    
    dap_test_msg("Running as root: %s", l_is_root ? "yes" : "no");
    dap_test_msg("eBPF capable: %s", l_capable ? "yes" : "no");
    
    // Root should always have eBPF capability
    if (l_is_root) {
        dap_assert(l_capable, "Root should have eBPF capability");
    }
    
    dap_pass_msg("eBPF capability detection passed");
}

/**
 * @brief Test dap_io_flow_ebpf_attach_socket() with invalid fd
 */
static void test_ebpf_attach_invalid_fd(void)
{
    dap_test_msg("Test: dap_io_flow_ebpf_attach_socket() with invalid fd");
    
    if (!dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF not available, skipping invalid fd test");
        dap_pass_msg("Invalid fd test skipped");
        return;
    }
    
    // Attempt to attach to invalid fd should fail
    int l_ret = dap_io_flow_ebpf_attach_socket(-1);
    dap_assert(l_ret != 0, "Attach to invalid fd should fail");
    
    // Attempt to attach to non-socket fd
    l_ret = dap_io_flow_ebpf_attach_socket(STDOUT_FILENO);
    dap_assert(l_ret != 0, "Attach to non-socket fd should fail");
    
    dap_pass_msg("Invalid fd attach test passed");
}

/**
 * @brief Test dap_io_flow_ebpf_attach_socket() with socket without SO_REUSEPORT
 */
static void test_ebpf_attach_without_reuseport(void)
{
    dap_test_msg("Test: dap_io_flow_ebpf_attach_socket() without SO_REUSEPORT");
    
    if (!dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF not available, skipping test");
        dap_pass_msg("Test skipped");
        return;
    }
    
    int l_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (l_sock < 0) {
        dap_test_msg("Cannot create socket: %s", strerror(errno));
        dap_pass_msg("Test skipped (no socket)");
        return;
    }
    
    // Don't set SO_REUSEPORT - attach should fail
    int l_ret = dap_io_flow_ebpf_attach_socket(l_sock);
    dap_assert(l_ret != 0, "Attach without SO_REUSEPORT should fail");
    
    close(l_sock);
    dap_pass_msg("Attach without SO_REUSEPORT test passed");
}

/**
 * @brief Test full ebpf attach/detach lifecycle with valid SO_REUSEPORT socket
 */
static void test_ebpf_attach_detach_lifecycle(void)
{
    dap_test_msg("Test: eBPF attach/detach lifecycle");
    
    if (!dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF not available, skipping lifecycle test");
        dap_pass_msg("Lifecycle test skipped (no capabilities)");
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
    
    // Attach eBPF program
    int l_ret = dap_io_flow_ebpf_attach_socket(l_sock);
    dap_assert(l_ret == 0, "eBPF attach should succeed");
    
    // Detach eBPF program
    l_ret = dap_io_flow_ebpf_detach_socket(l_sock);
    dap_test_msg("eBPF detach returned: %d", l_ret);
    
    close(l_sock);
    dap_pass_msg("eBPF attach/detach lifecycle passed");
}

/**
 * @brief Test eBPF with multiple sockets in SO_REUSEPORT group
 */
static void test_ebpf_reuseport_group(void)
{
    dap_test_msg("Test: eBPF with SO_REUSEPORT socket group");
    
    if (!dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF not available, skipping group test");
        dap_pass_msg("Group test skipped (no capabilities)");
        return;
    }
    
    int l_sockets[NUM_REUSEPORT_EBPF_SOCKETS] = {-1, -1, -1, -1};
    uint16_t l_port = 0;
    bool l_setup_failed = false;
    
    // Create socket group sharing same port
    for (int i = 0; i < NUM_REUSEPORT_EBPF_SOCKETS; i++) {
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
            .sin_port = htons(l_port),
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
        for (int i = 0; i < NUM_REUSEPORT_EBPF_SOCKETS; i++) {
            if (l_sockets[i] >= 0) {
                close(l_sockets[i]);
            }
        }
        dap_pass_msg("Group test skipped (setup failed)");
        return;
    }
    
    // Attach eBPF to first socket (affects entire group)
    int l_ret = dap_io_flow_ebpf_attach_socket(l_sockets[0]);
    dap_assert(l_ret == 0, "eBPF attach to group should succeed");
    
    dap_test_msg("eBPF attached to SO_REUSEPORT group of %d sockets", NUM_REUSEPORT_EBPF_SOCKETS);
    
    // Detach
    l_ret = dap_io_flow_ebpf_detach_socket(l_sockets[0]);
    dap_test_msg("eBPF detach from group returned: %d", l_ret);
    
    // Cleanup
    for (int i = 0; i < NUM_REUSEPORT_EBPF_SOCKETS; i++) {
        if (l_sockets[i] >= 0) {
            close(l_sockets[i]);
        }
    }
    
    dap_pass_msg("eBPF SO_REUSEPORT group test passed");
}

/**
 * @brief Test eBPF detach with invalid fd
 */
static void test_ebpf_detach_invalid_fd(void)
{
    dap_test_msg("Test: dap_io_flow_ebpf_detach_socket() with invalid fd");
    
    // Detach from invalid fd should fail gracefully
    int l_ret = dap_io_flow_ebpf_detach_socket(-1);
    dap_assert(l_ret != 0, "Detach from invalid fd should fail");
    
    dap_pass_msg("Invalid fd detach test passed");
}

/**
 * @brief Test graceful fallback when eBPF not available
 */
static void test_ebpf_fallback_behavior(void)
{
    dap_test_msg("Test: eBPF fallback behavior");
    
    if (dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF is available, testing successful operations");
        
        // When eBPF is available, fixture tier should also be available
        bool l_fixture_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF);
        dap_assert(l_fixture_available, "Fixture should report eBPF available");
    } else {
        dap_test_msg("eBPF not available, testing graceful failure");
        
        // Attach should fail gracefully
        int l_sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (l_sock >= 0) {
            int l_ret = dap_io_flow_ebpf_attach_socket(l_sock);
            dap_assert(l_ret != 0, "Attach should fail when eBPF unavailable");
            close(l_sock);
        }
        
        // Fixture tier should also be unavailable
        bool l_fixture_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF);
        dap_assert(!l_fixture_available, "Fixture should report eBPF unavailable");
        
        // Application tier fallback should work
        bool l_app_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_APPLICATION);
        dap_assert(l_app_available, "Application tier fallback should work");
    }
    
    dap_pass_msg("eBPF fallback behavior test passed");
}

/**
 * @brief Test fixture tier availability consistency
 */
static void test_ebpf_fixture_tier_available(void)
{
    dap_test_msg("Test: Fixture tier availability for eBPF");
    
    bool l_fixture_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF);
    bool l_capable = dap_io_flow_test_ebpf_capable();
    
    dap_test_msg("Fixture eBPF available: %s", l_fixture_available ? "yes" : "no");
    dap_test_msg("Fixture eBPF capable: %s", l_capable ? "yes" : "no");
    
    // Available should match capable for eBPF
    dap_assert(l_fixture_available == l_capable, 
               "Fixture tier availability should match capability");
    
    dap_pass_msg("Fixture tier availability test passed");
}

/**
 * @brief Test fixture context create/delete for eBPF tier
 */
static void test_ebpf_fixture_context(void)
{
    dap_test_msg("Test: Fixture context for eBPF tier");
    
    if (!dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF)) {
        dap_test_msg("eBPF not available, skipping fixture context test");
        dap_pass_msg("Fixture context test skipped");
        return;
    }
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_EBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    dap_assert(l_ctx->tier == IO_FLOW_TIER_EBPF, "Tier should be EBPF");
    dap_assert(l_ctx->initialized, "Context should be initialized");
    dap_assert(l_ctx->bytes_sent == 0, "Initial bytes_sent should be 0");
    dap_assert(l_ctx->bytes_received == 0, "Initial bytes_received should be 0");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Fixture context test passed");
}

/**
 * @brief Test fixture basic transfer simulation
 */
static void test_ebpf_fixture_transfer(void)
{
    dap_test_msg("Test: Fixture transfer simulation for eBPF");
    
    if (!dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF)) {
        dap_test_msg("eBPF not available, skipping transfer test");
        dap_pass_msg("Fixture transfer test skipped");
        return;
    }
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_EBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    // Test various sizes
    size_t l_sizes[] = {1, 100, 1000, 10000, 100000};
    size_t l_num = sizeof(l_sizes) / sizeof(l_sizes[0]);
    uint64_t l_total = 0;
    
    for (size_t i = 0; i < l_num; i++) {
        int l_ret = dap_io_flow_test_basic_transfer(l_ctx, l_sizes[i]);
        dap_assert(l_ret == 0, "Transfer should succeed");
        l_total += l_sizes[i];
    }
    
    dap_assert(l_ctx->bytes_sent == l_total, "Total bytes should match");
    dap_assert(l_ctx->packets_sent == l_num, "Packet count should match");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Fixture transfer simulation passed");
}

/**
 * @brief Test tier names
 */
static void test_ebpf_tier_name(void)
{
    dap_test_msg("Test: eBPF tier name");
    
    const char *l_name = dap_io_flow_test_tier_name(IO_FLOW_TIER_EBPF);
    dap_assert(l_name != NULL, "Name should not be NULL");
    dap_assert(strcmp(l_name, "Extended BPF") == 0, "Name should be 'Extended BPF'");
    
    // Test other tier names for consistency
    dap_assert(strcmp(dap_io_flow_test_tier_name(IO_FLOW_TIER_APPLICATION), "Application") == 0,
               "Application tier name should match");
    dap_assert(strcmp(dap_io_flow_test_tier_name(IO_FLOW_TIER_CBPF), "Classic BPF") == 0,
               "CBPF tier name should match");
    
    dap_pass_msg("Tier name test passed");
}

/**
 * @brief Test error handling
 */
static void test_ebpf_error_handling(void)
{
    dap_test_msg("Test: eBPF error handling");
    
    // Test NULL context transfer
    int l_ret = dap_io_flow_test_basic_transfer(NULL, 1024);
    dap_assert(l_ret != 0, "Should fail with NULL context");
    
    // Test invalid tier
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_COUNT);
    dap_assert(l_ctx == NULL, "Should fail with invalid tier");
    
    // Test throughput with NULL output
    if (dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF)) {
        l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_EBPF);
        dap_assert(l_ctx != NULL, "Context creation should succeed");
        
        l_ret = dap_io_flow_test_throughput(l_ctx, 100, NULL);
        dap_assert(l_ret != 0, "Should fail with NULL throughput output");
        
        dap_io_flow_test_context_delete(l_ctx);
    }
    
    // Delete NULL should not crash
    dap_io_flow_test_context_delete(NULL);
    
    dap_pass_msg("Error handling test passed");
}

/**
 * @brief Test throughput measurement
 */
static void test_ebpf_throughput(void)
{
    dap_test_msg("Test: eBPF throughput measurement");
    
    if (!dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF)) {
        dap_test_msg("eBPF not available, skipping throughput test");
        dap_pass_msg("Throughput test skipped");
        return;
    }
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_EBPF);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    uint64_t l_throughput = 0;
    int l_ret = dap_io_flow_test_throughput(l_ctx, 100, &l_throughput);
    dap_assert(l_ret == 0, "Throughput test should succeed");
    dap_assert(l_throughput > 0, "Throughput should be positive");
    
    dap_test_msg("Measured throughput: %lu bytes/sec", (unsigned long)l_throughput);
    dap_assert(l_ctx->bytes_sent > 0, "Bytes should have been sent");
    dap_assert(l_ctx->packets_sent > 0, "Packets should have been sent");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Throughput measurement passed");
}

int main(void)
{
    dap_test_msg("=== IO Flow Extended BPF (eBPF) API Tests ===");
    
    // Initialize fixtures
    int l_ret = dap_io_flow_test_fixtures_init();
    if (l_ret != 0) {
        dap_test_msg("Failed to initialize fixtures");
        return 1;
    }
    
    // Run API tests (test dap_io_flow_ebpf_* functions)
    test_ebpf_is_available();
    test_ebpf_capability_detection();
    test_ebpf_attach_invalid_fd();
    test_ebpf_attach_without_reuseport();
    test_ebpf_attach_detach_lifecycle();
    test_ebpf_reuseport_group();
    test_ebpf_detach_invalid_fd();
    test_ebpf_fallback_behavior();
    
    // Run fixture tests
    test_ebpf_fixture_tier_available();
    test_ebpf_fixture_context();
    test_ebpf_fixture_transfer();
    test_ebpf_tier_name();
    test_ebpf_error_handling();
    test_ebpf_throughput();
    
    // Cleanup
    dap_io_flow_test_fixtures_deinit();
    
    dap_test_msg("=== All eBPF API Tests Passed ===");
    return 0;
}
