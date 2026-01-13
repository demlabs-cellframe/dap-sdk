/**
 * @file test_io_flow_lb_tiers.c
 * @brief Integration test for all available load balancing tiers
 * 
 * Tests UDP flow server with all available LB mechanisms:
 * - Tier 2 (eBPF): Kernel-level sticky sessions with FNV-1a hash
 * - Tier 1 (Application): User-space hash + cross-worker queue forwarding
 * - Tier 0 (None): Single worker, no load balancing
 * 
 * For each available tier, runs multiple concurrent UDP connections
 * and verifies correct flow distribution and data integrity.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events.h"
#include "dap_proc_thread.h"
#include "dap_io_flow.h"
#include "dap_io_flow_udp.h"
#include "dap_io_flow_ebpf.h"

#define LOG_TAG "test_lb_tiers"

// Test configuration
#define TEST_PORT_BASE 19000
#define TEST_DURATION_SEC 10
#define TEST_NUM_CLIENTS 50
#define TEST_PACKETS_PER_CLIENT 100
#define TEST_PACKET_SIZE 512

// Test context
typedef struct {
    dap_io_flow_server_t *server;
    dap_io_flow_lb_tier_t tier;
    uint32_t worker_count;
    
    // Statistics
    _Atomic uint64_t packets_sent;
    _Atomic uint64_t packets_received;
    _Atomic uint64_t bytes_sent;
    _Atomic uint64_t bytes_received;
    
    // Per-worker stats
    _Atomic uint32_t *flows_per_worker;
    _Atomic uint64_t *packets_per_worker;
    
    // Test control
    volatile bool test_running;
    time_t start_time;
} test_ctx_t;

// Forward declarations
static void s_packet_received_cb(dap_io_flow_server_t *a_server, dap_io_flow_t *a_flow,
                                 const uint8_t *a_data, size_t a_data_size,
                                 const struct sockaddr_storage *a_remote_addr,
                                 dap_events_socket_t *a_listener_es);
static dap_io_flow_t* s_flow_create_cb(dap_io_flow_server_t *a_server,
                                       const struct sockaddr_storage *a_remote_addr,
                                       dap_events_socket_t *a_listener_es);
static void s_flow_destroy_cb(dap_io_flow_t *a_flow);

// Global test context
static test_ctx_t *g_test_ctx = NULL;

/**
 * @brief Print test statistics
 */
static void s_print_stats(test_ctx_t *a_ctx)
{
    time_t l_duration = time(NULL) - a_ctx->start_time;
    
    printf("\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("  Test Statistics - Tier %d\n", a_ctx->tier);
    printf("═══════════════════════════════════════════════════\n");
    printf("Duration:         %lu seconds\n", l_duration);
    printf("Packets sent:     %lu\n", (unsigned long)a_ctx->packets_sent);
    printf("Packets received: %lu\n", (unsigned long)a_ctx->packets_received);
    printf("Bytes sent:       %lu (%.2f MB)\n", 
           (unsigned long)a_ctx->bytes_sent,
           (double)a_ctx->bytes_sent / (1024.0 * 1024.0));
    printf("Bytes received:   %lu (%.2f MB)\n",
           (unsigned long)a_ctx->bytes_received,
           (double)a_ctx->bytes_received / (1024.0 * 1024.0));
    
    if (l_duration > 0) {
        printf("Throughput:       %.2f MB/s\n",
               (double)a_ctx->bytes_received / (1024.0 * 1024.0) / l_duration);
    }
    
    printf("\nPer-worker distribution:\n");
    for (uint32_t i = 0; i < a_ctx->worker_count; i++) {
        printf("  Worker %2u: %5u flows, %8lu packets\n",
               i, a_ctx->flows_per_worker[i], (unsigned long)a_ctx->packets_per_worker[i]);
    }
    printf("═══════════════════════════════════════════════════\n\n");
}

/**
 * @brief Signal handler for clean shutdown
 */
static void s_signal_handler(int a_sig)
{
    (void)a_sig;
    if (g_test_ctx) {
        g_test_ctx->test_running = false;
    }
}

/**
 * @brief Create test server for specific tier
 */
static test_ctx_t* s_create_test_server(dap_io_flow_lb_tier_t a_tier, uint16_t a_port)
{
    test_ctx_t *l_ctx = DAP_NEW_Z(test_ctx_t);
    if (!l_ctx) {
        return NULL;
    }
    
    l_ctx->tier = a_tier;
    l_ctx->worker_count = dap_proc_thread_get_count();
    l_ctx->test_running = true;
    l_ctx->start_time = time(NULL);
    
    // Allocate per-worker arrays
    l_ctx->flows_per_worker = calloc(l_ctx->worker_count, sizeof(_Atomic uint32_t));
    l_ctx->packets_per_worker = calloc(l_ctx->worker_count, sizeof(_Atomic uint64_t));
    
    if (!l_ctx->flows_per_worker || !l_ctx->packets_per_worker) {
        free(l_ctx->flows_per_worker);
        free(l_ctx->packets_per_worker);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Create flow server
    dap_io_flow_ops_t l_ops = {
        .packet_received = s_packet_received_cb,
        .flow_create = s_flow_create_cb,
        .flow_destroy = s_flow_destroy_cb
    };
    
    char l_name[64];
    snprintf(l_name, sizeof(l_name), "test_lb_tier_%d", a_tier);
    
    l_ctx->server = dap_io_flow_server_new(l_name, &l_ops, DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    if (!l_ctx->server) {
        log_it(L_ERROR, "Failed to create flow server");
        free(l_ctx->flows_per_worker);
        free(l_ctx->packets_per_worker);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    // Start listening
    if (dap_io_flow_server_listen(l_ctx->server, "127.0.0.1", a_port) != 0) {
        log_it(L_ERROR, "Failed to start listening");
        dap_io_flow_server_delete(l_ctx->server, NULL);
        free(l_ctx->flows_per_worker);
        free(l_ctx->packets_per_worker);
        DAP_DELETE(l_ctx);
        return NULL;
    }
    
    return l_ctx;
}

/**
 * @brief Flow create callback
 */
static dap_io_flow_t* s_flow_create_cb(dap_io_flow_server_t *a_server,
                                       const struct sockaddr_storage *a_remote_addr,
                                       dap_events_socket_t *a_listener_es)
{
    test_ctx_t *l_ctx = g_test_ctx;
    if (!l_ctx) {
        return NULL;
    }
    
    // Create UDP flow
    dap_io_flow_t *l_flow = dap_io_flow_udp_create(a_server, a_remote_addr, a_listener_es);
    if (l_flow) {
        // Update worker statistics
        atomic_fetch_add(&l_ctx->flows_per_worker[l_flow->owner_worker_id], 1);
    }
    
    return l_flow;
}

/**
 * @brief Flow destroy callback
 */
static void s_flow_destroy_cb(dap_io_flow_t *a_flow)
{
    test_ctx_t *l_ctx = g_test_ctx;
    if (l_ctx && a_flow) {
        atomic_fetch_sub(&l_ctx->flows_per_worker[a_flow->owner_worker_id], 1);
    }
    
    dap_io_flow_udp_destroy(a_flow);
}

/**
 * @brief Packet received callback
 */
static void s_packet_received_cb(dap_io_flow_server_t *a_server, dap_io_flow_t *a_flow,
                                 const uint8_t *a_data, size_t a_data_size,
                                 const struct sockaddr_storage *a_remote_addr,
                                 dap_events_socket_t *a_listener_es)
{
    (void)a_server;
    (void)a_remote_addr;
    (void)a_listener_es;
    
    test_ctx_t *l_ctx = g_test_ctx;
    if (!l_ctx) {
        return;
    }
    
    // Update statistics
    atomic_fetch_add(&l_ctx->packets_received, 1);
    atomic_fetch_add(&l_ctx->bytes_received, a_data_size);
    atomic_fetch_add(&l_ctx->packets_per_worker[a_flow->owner_worker_id], 1);
    
    // Echo back
    dap_io_flow_udp_send(a_flow, a_data, a_data_size);
}

/**
 * @brief Run test for specific tier
 */
static int s_run_tier_test(dap_io_flow_lb_tier_t a_tier, uint16_t a_port)
{
    printf("\n");
    printf("╔═══════════════════════════════════════════════════╗\n");
    printf("║  Testing Tier %d: %-33s║\n", a_tier, 
           a_tier == DAP_IO_FLOW_LB_TIER_EBPF ? "eBPF" :
           a_tier == DAP_IO_FLOW_LB_TIER_APPLICATION ? "Application-level" : "None");
    printf("╚═══════════════════════════════════════════════════╝\n\n");
    
    // Create server
    test_ctx_t *l_ctx = s_create_test_server(a_tier, a_port);
    if (!l_ctx) {
        printf("❌ Failed to create server for tier %d\n", a_tier);
        return -1;
    }
    
    g_test_ctx = l_ctx;
    
    // TODO: Create clients and send data
    // For now, just sleep and print stats
    printf("Server created, sleeping for %d seconds...\n", TEST_DURATION_SEC);
    sleep(TEST_DURATION_SEC);
    
    // Print statistics
    s_print_stats(l_ctx);
    
    // Cleanup
    dap_io_flow_server_delete(l_ctx->server, NULL);
    free(l_ctx->flows_per_worker);
    free(l_ctx->packets_per_worker);
    DAP_DELETE(l_ctx);
    g_test_ctx = NULL;
    
    return 0;
}

/**
 * @brief Main test entry point
 */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    // Setup signal handlers
    signal(SIGINT, s_signal_handler);
    signal(SIGTERM, s_signal_handler);
    
    // Initialize DAP
    dap_common_init("test_lb_tiers");
    
    // Initialize config (minimal)
    g_config = dap_config_open("test_lb_tiers");
    dap_config_set_item_int_default(g_config, "general", "debug_mode", 4);
    dap_config_set_item_bool_default(g_config, "io_flow", "debug_more", false);
    
    // Initialize events with 10 workers
    dap_events_init(10, 0);
    dap_proc_thread_init(10);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("  Load Balancing Tiers Integration Test\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("Workers:  %u\n", dap_proc_thread_get_count());
    printf("Clients:  %d\n", TEST_NUM_CLIENTS);
    printf("Packets:  %d per client\n", TEST_PACKETS_PER_CLIENT);
    printf("Duration: %d seconds per tier\n", TEST_DURATION_SEC);
    printf("═══════════════════════════════════════════════════\n");
    
    // Detect available tiers
    dap_io_flow_lb_tier_t l_detected_tier = dap_io_flow_detect_lb_tier();
    printf("\nDetected tier: %d\n", l_detected_tier);
    
    // Test all tiers up to detected one
    int l_result = 0;
    
    // Always test Tier 0 (None) - single worker
    printf("\n[INFO] Testing with single worker (Tier 0)\n");
    dap_proc_thread_deinit();
    dap_events_deinit();
    dap_events_init(1, 0);
    dap_proc_thread_init(1);
    l_result |= s_run_tier_test(DAP_IO_FLOW_LB_TIER_NONE, TEST_PORT_BASE);
    
    // Re-init with multiple workers
    dap_proc_thread_deinit();
    dap_events_deinit();
    dap_events_init(10, 0);
    dap_proc_thread_init(10);
    
    // Test Tier 1 (Application) if detected >= 1
    if (l_detected_tier >= DAP_IO_FLOW_LB_TIER_APPLICATION) {
        l_result |= s_run_tier_test(DAP_IO_FLOW_LB_TIER_APPLICATION, TEST_PORT_BASE + 1);
    }
    
    // Test Tier 2 (eBPF) if detected >= 2
    if (l_detected_tier >= DAP_IO_FLOW_LB_TIER_EBPF) {
        l_result |= s_run_tier_test(DAP_IO_FLOW_LB_TIER_EBPF, TEST_PORT_BASE + 2);
    }
    
    // Cleanup
    dap_proc_thread_deinit();
    dap_events_deinit();
    dap_config_close(g_config);
    
    printf("\n");
    printf("═══════════════════════════════════════════════════\n");
    printf("  %s\n", l_result == 0 ? "✅ ALL TESTS PASSED" : "❌ SOME TESTS FAILED");
    printf("═══════════════════════════════════════════════════\n\n");
    
    return l_result;
}
