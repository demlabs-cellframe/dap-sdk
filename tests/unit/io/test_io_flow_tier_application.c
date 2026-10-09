/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * Test suite for Application-level IO Flow tier
 * This test is cross-platform and should work on all systems.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dap_test.h"
#include "dap_common.h"
#include "dap_io_flow_test_fixtures.h"

/**
 * @brief Test application tier availability
 */
static void test_application_tier_available(void)
{
    dap_test_msg("Test: Application tier availability");
    
    bool l_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_available, "Application tier should always be available");
    
    dap_pass_msg("Application tier availability check passed");
}

/**
 * @brief Test all tiers availability comparison
 */
static void test_application_tier_comparison(void)
{
    dap_test_msg("Test: Tier availability comparison");
    
    bool l_app = dap_io_flow_test_tier_available(IO_FLOW_TIER_APPLICATION);
    bool l_cbpf = dap_io_flow_test_tier_available(IO_FLOW_TIER_CBPF);
    bool l_ebpf = dap_io_flow_test_tier_available(IO_FLOW_TIER_EBPF);
    
    dap_test_msg("Application tier: %s", l_app ? "available" : "not available");
    dap_test_msg("Classic BPF tier: %s", l_cbpf ? "available" : "not available");
    dap_test_msg("eBPF tier: %s", l_ebpf ? "available" : "not available");
    
    // Application should always be available
    dap_assert(l_app, "Application tier must always be available");
    
    // If eBPF is available, CBPF should also be available
    if (l_ebpf) {
        dap_assert(l_cbpf, "If eBPF is available, CBPF should also be available");
    }
    
    dap_pass_msg("Tier availability comparison passed");
}

/**
 * @brief Test context creation for application tier
 */
static void test_application_context_create(void)
{
    dap_test_msg("Test: Application tier context creation");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    dap_assert(l_ctx->tier == IO_FLOW_TIER_APPLICATION, "Tier should be APPLICATION");
    dap_assert(l_ctx->initialized, "Context should be initialized");
    dap_assert(l_ctx->bytes_sent == 0, "Initial bytes_sent should be 0");
    dap_assert(l_ctx->bytes_received == 0, "Initial bytes_received should be 0");
    dap_assert(l_ctx->packets_sent == 0, "Initial packets_sent should be 0");
    dap_assert(l_ctx->packets_received == 0, "Initial packets_received should be 0");
    dap_assert(l_ctx->errors == 0, "Initial errors should be 0");
    dap_assert(l_ctx->user_data == NULL, "Initial user_data should be NULL");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Application tier context creation passed");
}

/**
 * @brief Test multiple context creation and deletion
 */
static void test_application_multiple_contexts(void)
{
    dap_test_msg("Test: Multiple context creation/deletion");
    
    #define NUM_APP_CONTEXTS 20
    dap_io_flow_test_context_t *l_contexts[NUM_APP_CONTEXTS];
    
    // Create all contexts
    for (int i = 0; i < NUM_APP_CONTEXTS; i++) {
        l_contexts[i] = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
        dap_assert(l_contexts[i] != NULL, "Context creation should succeed");
        dap_assert(l_contexts[i]->tier == IO_FLOW_TIER_APPLICATION, "Tier should match");
    }
    
    // Perform operations on each
    for (int i = 0; i < NUM_APP_CONTEXTS; i++) {
        int l_ret = dap_io_flow_test_basic_transfer(l_contexts[i], 100 * (i + 1));
        dap_assert(l_ret == 0, "Transfer should succeed");
    }
    
    // Verify each context is independent
    for (int i = 0; i < NUM_APP_CONTEXTS; i++) {
        dap_assert(l_contexts[i]->bytes_sent == (uint64_t)(100 * (i + 1)),
                   "Each context should have independent counters");
        dap_assert(l_contexts[i]->packets_sent == 1, "Each should have 1 packet");
    }
    
    // Delete in random order (0, 19, 1, 18, 2, 17, ...)
    for (int i = 0; i < NUM_APP_CONTEXTS / 2; i++) {
        dap_io_flow_test_context_delete(l_contexts[i]);
        dap_io_flow_test_context_delete(l_contexts[NUM_APP_CONTEXTS - 1 - i]);
    }
    
    dap_pass_msg("Multiple context creation/deletion passed");
}

/**
 * @brief Test basic data transfer with various sizes
 */
static void test_application_basic_transfer(void)
{
    dap_test_msg("Test: Application tier basic transfer");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    // Test various data sizes
    size_t l_sizes[] = {1, 10, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 65536, 1048576};
    size_t l_num_sizes = sizeof(l_sizes) / sizeof(l_sizes[0]);
    uint64_t l_total = 0;
    
    for (size_t i = 0; i < l_num_sizes; i++) {
        int l_ret = dap_io_flow_test_basic_transfer(l_ctx, l_sizes[i]);
        dap_assert(l_ret == 0, "Transfer should succeed");
        l_total += l_sizes[i];
    }
    
    // Verify counters
    dap_assert(l_ctx->bytes_sent == l_total, "Total bytes sent should match");
    dap_assert(l_ctx->bytes_received == l_total, "Total bytes received should match");
    dap_assert(l_ctx->packets_sent == l_num_sizes, "Packet count should match");
    dap_assert(l_ctx->packets_received == l_num_sizes, "Received packet count should match");
    dap_assert(l_ctx->errors == 0, "No errors should occur");
    
    dap_test_msg("Total transferred: %lu bytes in %zu packets", 
                 (unsigned long)l_total, l_num_sizes);
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Application tier basic transfer passed");
}

/**
 * @brief Test zero-size transfer
 */
static void test_application_zero_transfer(void)
{
    dap_test_msg("Test: Zero-size transfer");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    int l_ret = dap_io_flow_test_basic_transfer(l_ctx, 0);
    dap_assert(l_ret == 0, "Zero-size transfer should succeed");
    dap_assert(l_ctx->bytes_sent == 0, "Bytes sent should be 0");
    dap_assert(l_ctx->packets_sent == 1, "Packet count should be 1 (empty packet)");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Zero-size transfer passed");
}

/**
 * @brief Test large number of small transfers
 */
static void test_application_many_small_transfers(void)
{
    dap_test_msg("Test: Many small transfers");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    #define NUM_SMALL_TRANSFERS 1000
    for (int i = 0; i < NUM_SMALL_TRANSFERS; i++) {
        int l_ret = dap_io_flow_test_basic_transfer(l_ctx, 64);
        dap_assert(l_ret == 0, "Transfer should succeed");
    }
    
    dap_assert(l_ctx->bytes_sent == 64 * NUM_SMALL_TRANSFERS, "Total bytes should match");
    dap_assert(l_ctx->packets_sent == NUM_SMALL_TRANSFERS, "Packet count should match");
    
    dap_test_msg("Completed %d small transfers", NUM_SMALL_TRANSFERS);
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Many small transfers passed");
}

/**
 * @brief Test throughput measurement
 */
static void test_application_throughput(void)
{
    dap_test_msg("Test: Application tier throughput");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    uint64_t l_throughput = 0;
    int l_ret = dap_io_flow_test_throughput(l_ctx, 100, &l_throughput);  // 100ms test
    dap_assert(l_ret == 0, "Throughput test should succeed");
    dap_assert(l_throughput > 0, "Throughput should be positive");
    
    dap_test_msg("Measured throughput: %lu bytes/sec", (unsigned long)l_throughput);
    dap_test_msg("Bytes transferred: %lu", (unsigned long)l_ctx->bytes_sent);
    dap_test_msg("Packets sent: %lu", (unsigned long)l_ctx->packets_sent);
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Application tier throughput test passed");
}

/**
 * @brief Test throughput with different durations
 */
static void test_application_throughput_durations(void)
{
    dap_test_msg("Test: Throughput with different durations");
    
    uint32_t l_durations[] = {50, 100, 200};
    size_t l_num = sizeof(l_durations) / sizeof(l_durations[0]);
    
    for (size_t i = 0; i < l_num; i++) {
        dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
        dap_assert(l_ctx != NULL, "Context creation should succeed");
        
        uint64_t l_throughput = 0;
        int l_ret = dap_io_flow_test_throughput(l_ctx, l_durations[i], &l_throughput);
        dap_assert(l_ret == 0, "Throughput test should succeed");
        
        dap_test_msg("Duration %ums: %lu bytes/sec", l_durations[i], (unsigned long)l_throughput);
        
        dap_io_flow_test_context_delete(l_ctx);
    }
    
    dap_pass_msg("Throughput durations test passed");
}

/**
 * @brief Test tier name
 */
static void test_application_tier_name(void)
{
    dap_test_msg("Test: Application tier name");
    
    const char *l_name = dap_io_flow_test_tier_name(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_name != NULL, "Name should not be NULL");
    dap_assert(strcmp(l_name, "Application") == 0, "Name should be 'Application'");
    
    // Test all tier names for completeness
    const char *l_names[] = {"Application", "Classic BPF", "Extended BPF", "Unknown"};
    dap_io_flow_test_tier_t l_tiers[] = {
        IO_FLOW_TIER_APPLICATION, 
        IO_FLOW_TIER_CBPF, 
        IO_FLOW_TIER_EBPF, 
        IO_FLOW_TIER_COUNT
    };
    
    for (size_t i = 0; i < sizeof(l_tiers) / sizeof(l_tiers[0]); i++) {
        const char *l_tier_name = dap_io_flow_test_tier_name(l_tiers[i]);
        dap_assert(strcmp(l_tier_name, l_names[i]) == 0, "Tier name should match");
    }
    
    dap_pass_msg("Application tier name test passed");
}

/**
 * @brief Test error handling with NULL context
 */
static void test_application_null_context(void)
{
    dap_test_msg("Test: NULL context handling");
    
    // Transfer with NULL context should fail
    int l_ret = dap_io_flow_test_basic_transfer(NULL, 1024);
    dap_assert(l_ret != 0, "Should fail with NULL context");
    
    // Throughput with NULL context should fail
    uint64_t l_throughput = 0;
    l_ret = dap_io_flow_test_throughput(NULL, 100, &l_throughput);
    dap_assert(l_ret != 0, "Should fail with NULL context");
    
    // Delete NULL should not crash
    dap_io_flow_test_context_delete(NULL);
    
    dap_pass_msg("NULL context handling passed");
}

/**
 * @brief Test error handling with invalid tier
 */
static void test_application_invalid_tier(void)
{
    dap_test_msg("Test: Invalid tier handling");
    
    // Create context with invalid tier
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_COUNT);
    dap_assert(l_ctx == NULL, "Should fail with invalid tier");
    
    // Create context with large invalid tier
    l_ctx = dap_io_flow_test_context_create((dap_io_flow_test_tier_t)999);
    dap_assert(l_ctx == NULL, "Should fail with large invalid tier");
    
    // Check availability of invalid tier
    bool l_available = dap_io_flow_test_tier_available(IO_FLOW_TIER_COUNT);
    dap_assert(!l_available, "Invalid tier should not be available");
    
    dap_pass_msg("Invalid tier handling passed");
}

/**
 * @brief Test throughput with NULL output pointer
 */
static void test_application_throughput_null_output(void)
{
    dap_test_msg("Test: Throughput with NULL output");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    int l_ret = dap_io_flow_test_throughput(l_ctx, 100, NULL);
    dap_assert(l_ret != 0, "Should fail with NULL output pointer");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("Throughput NULL output handling passed");
}

/**
 * @brief Test user_data field
 */
static void test_application_user_data(void)
{
    dap_test_msg("Test: User data field");
    
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation should succeed");
    
    // Initial value should be NULL
    dap_assert(l_ctx->user_data == NULL, "Initial user_data should be NULL");
    
    // Set user data
    int l_my_data = 42;
    l_ctx->user_data = &l_my_data;
    dap_assert(l_ctx->user_data == &l_my_data, "User data should be set");
    dap_assert(*(int*)l_ctx->user_data == 42, "User data value should match");
    
    // Clear user data
    l_ctx->user_data = NULL;
    dap_assert(l_ctx->user_data == NULL, "User data should be cleared");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    dap_pass_msg("User data field test passed");
}

/**
 * @brief Test fixture init/deinit multiple times
 */
static void test_application_fixture_reinit(void)
{
    dap_test_msg("Test: Fixture reinit");
    
    // Deinit and reinit should work
    dap_io_flow_test_fixtures_deinit();
    
    int l_ret = dap_io_flow_test_fixtures_init();
    dap_assert(l_ret == 0, "Reinit should succeed");
    
    // Create context after reinit
    dap_io_flow_test_context_t *l_ctx = dap_io_flow_test_context_create(IO_FLOW_TIER_APPLICATION);
    dap_assert(l_ctx != NULL, "Context creation after reinit should succeed");
    
    dap_io_flow_test_context_delete(l_ctx);
    
    // Double init should be safe
    l_ret = dap_io_flow_test_fixtures_init();
    dap_assert(l_ret == 0, "Double init should be safe");
    
    dap_pass_msg("Fixture reinit test passed");
}

int main(void)
{
    dap_test_msg("=== IO Flow Application Tier Tests ===");
    
    // Initialize fixtures
    int l_ret = dap_io_flow_test_fixtures_init();
    if (l_ret != 0) {
        dap_test_msg("Failed to initialize fixtures");
        return 1;
    }
    
    // Run tests
    test_application_tier_available();
    test_application_tier_comparison();
    test_application_context_create();
    test_application_multiple_contexts();
    test_application_basic_transfer();
    test_application_zero_transfer();
    test_application_many_small_transfers();
    test_application_throughput();
    test_application_throughput_durations();
    test_application_tier_name();
    test_application_null_context();
    test_application_invalid_tier();
    test_application_throughput_null_output();
    test_application_user_data();
    test_application_fixture_reinit();
    
    // Cleanup
    dap_io_flow_test_fixtures_deinit();
    
    dap_test_msg("=== All Application Tier Tests Passed ===");
    return 0;
}
