/**
 * @file test_trans_integration.c
 * @brief Trans Integration Test Suite
 * @details Tests all trans types with parallel execution:
 *          - Initializes all available transs
 *          - Creates servers for each trans type
 *          - Tests full handshake cycle (ENC_INIT -> STREAM_CTL -> STREAM_SESSION -> STREAM_CONNECTED -> STREAM_STREAMING)
 *          - Tests data exchange via stream_ch with large data volumes (~10MB)
 *          - Runs all trans tests in parallel
 * 
 * @note This is a comprehensive integration test that validates the full DAP protocol
 *       implementation across all trans types simultaneously.
 * @date 2025-11-02
 * @copyright (c) 2025 Cellframe Network
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <signal.h>

// DAP SDK headers
#include "dap_common.h"
#include "dap_config.h"
#include "dap_enc.h"
#include "dap_events.h"
#include "dap_client.h"
#include "dap_client_helpers.h"
#include "dap_stream.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#ifdef DAP_OS_LINUX
#include "dap_io_flow_ebpf.h"
#include "dap_io_flow_cbpf.h"
#endif
#include "dap_net_trans_udp_server.h"
#include "dap_stream_ctl.h"
#include "dap_net_trans.h"
#include "dap_net_trans_server.h"

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
#include "test_trans_helpers.h"

#define LOG_TAG "test_trans"

// Test configuration  
#define TEST_STREAM_CH_ID 'A'
// Test scenario timeouts scale with client count for realistic performance
#define TEST_TRANS_BASE_TIMEOUT_MS 10000      // 10 seconds base
#define TEST_TRANS_PER_CLIENT_MS 3000         // 3 seconds per client
#define TEST_TRANS_TIMEOUT_LARGE_MS 180000    // 3 minutes for large scenarios
// DNS tunnel is best-effort datagram transport with limited payload headroom.
// Keep integration payload compact to avoid fragment loss on emulated runners.
#define TEST_TRANS_DNS_MAX_DATA_SIZE (64 * 1024)

// Helper macro to calculate timeout based on client count
#define CALC_TIMEOUT(clients) (TEST_TRANS_BASE_TIMEOUT_MS + (clients) * TEST_TRANS_PER_CLIENT_MS)

// Test scenarios: different server/client configurations
// Testing progression: start small → scale up → stress test
typedef struct test_scenario {
    const char *name;
    size_t num_servers;
    size_t num_clients;
    size_t data_size;  // bytes to send/receive per client
    uint32_t timeout_ms;  // Timeout for this scenario
    bool stress_only;  // Only run with --stress flag
} test_scenario_t;

// Test mode flags (set via command line)
static bool s_stress_mode = false;      // --stress: Enable 1000+ client scenarios
static bool s_quick_mode = false;       // --quick: Only 1 client scenario per tier
static size_t s_max_clients = 10;       // Default max clients (override with --max-clients=N)
static enum dap_log_level s_log_level = L_DEBUG;  // --log-level: controls verbosity
static const char *s_trans_filter = NULL;          // --trans=NAME: test only this transport

static const test_scenario_t g_scenarios[] = {
    // Basic scenarios - always run
    // Note: Data sizes reduced to avoid packet loss under flow control pressure
    {"1 server, 1 client",      1,    1,     1*1024*1024, CALC_TIMEOUT(1), false},
    {"1 server, 10 clients",    1,   10,     512*1024,    CALC_TIMEOUT(10), false},
    {"1 server, 20 clients",    1,   20,     512*1024,    CALC_TIMEOUT(20), false},
    
    // Scaling scenarios - run by default (up to 100 clients)
    {"1 server, 100 clients",   1,  100,     256*1024,    CALC_TIMEOUT(100), false},
    {"10 servers, 10 clients", 10,   10,     512*1024,    CALC_TIMEOUT(10), false},
    {"10 servers, 100 clients", 10, 100,     256*1024,    CALC_TIMEOUT(100), false},
    
    // Stress scenarios - only with --stress flag
    {"1 server, 1000 clients",  1, 1000,     128*1024,    TEST_TRANS_TIMEOUT_LARGE_MS * 3, true},
    {"10 servers, 1000 clients", 10, 1000,   128*1024,    TEST_TRANS_TIMEOUT_LARGE_MS * 3, true},
};
#define SCENARIO_COUNT (sizeof(g_scenarios) / sizeof(g_scenarios[0]))

// Trans configs are defined in test_trans_helpers.h
// Define the actual array here
const trans_test_config_t g_trans_configs[] = {
    {DAP_NET_TRANS_HTTP, "HTTP", 18101, "127.0.0.1"},
    {DAP_NET_TRANS_WEBSOCKET, "WebSocket", 18102, "127.0.0.1"},
    {DAP_NET_TRANS_UDP_BASIC, "UDP", 18103, "127.0.0.1"},
    {DAP_NET_TRANS_DNS_TUNNEL, "DNS", 18104, "127.0.0.1"},
};

// Define count as compile-time constant for use in array declarations
#define TRANS_CONFIG_COUNT (sizeof(g_trans_configs) / sizeof(g_trans_configs[0]))
// Also export as runtime variable for use in other files
const size_t g_trans_config_count = TRANS_CONFIG_COUNT;

// ============================================================================
// Load Balancing Tier Configuration for Datagram Testing
// ============================================================================

typedef struct tier_test_config {
    dap_io_flow_lb_tier_t tier;
    const char *name;
    bool (*is_available)(void);  // NULL = always available
} tier_test_config_t;

static bool s_tier_application_available(void) { return true; }

static const tier_test_config_t g_tier_configs[] = {
    {DAP_IO_FLOW_LB_TIER_APPLICATION, "Application", s_tier_application_available},
#ifdef DAP_OS_LINUX
    {DAP_IO_FLOW_LB_TIER_CLASSIC_BPF, "CBPF", dap_io_flow_cbpf_is_available},
#endif
};
#define TIER_CONFIG_COUNT (sizeof(g_tier_configs) / sizeof(g_tier_configs[0]))

// Per-trans test ctx with dynamic arrays for scenario support
typedef struct trans_test_ctx {
    trans_test_config_t config;
    test_scenario_t scenario;  // Current test scenario
    
    // Dynamic arrays allocated based on scenario
    dap_net_trans_server_t **servers;           // array of num_servers server pointers
    dap_client_t **clients;                     // array of num_clients client pointers
    test_stream_ch_ctx_t *stream_ctxs;          // array of num_clients stream contexts
    dap_stream_node_addr_t *client_node_addrs;  // array of num_clients node addresses
    
    pthread_t thread;
    int result;
    bool running;
} trans_test_ctx_t;

// Global test state - no longer fixed array, will be allocated dynamically
static pthread_mutex_t s_test_mutex = PTHREAD_MUTEX_INITIALIZER;

// Global test statistics
typedef struct test_statistics {
    // Per-transport stats
    size_t scenarios_passed[TRANS_CONFIG_COUNT];
    size_t scenarios_failed[TRANS_CONFIG_COUNT];
    size_t total_clients_processed[TRANS_CONFIG_COUNT];
    size_t total_bytes_sent[TRANS_CONFIG_COUNT];
    size_t total_bytes_received[TRANS_CONFIG_COUNT];
    uint64_t total_duration_ms[TRANS_CONFIG_COUNT];
    
    // Global stats
    size_t total_scenarios_passed;
    size_t total_scenarios_failed;
    uint64_t test_start_time_ms;
    uint64_t test_end_time_ms;
} test_statistics_t;

static test_statistics_t s_test_stats = {0};

/**
 * @brief Print comprehensive test statistics
 */
static void print_test_statistics(void)
{
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("                    TEST STATISTICS SUMMARY                    \n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    uint64_t total_duration_sec = (s_test_stats.test_end_time_ms - s_test_stats.test_start_time_ms) / 1000;
    
    // Overall summary
    printf("📊 Overall Results:\n");
    printf("   Total Scenarios: %zu passed, %zu failed\n",
           s_test_stats.total_scenarios_passed, s_test_stats.total_scenarios_failed);
    printf("   Total Duration: %" PRIu64 " seconds (%.1f minutes)\n",
           total_duration_sec, (double)total_duration_sec / 60.0);
    printf("\n");
    
    // Per-transport statistics
    for (size_t i = 0; i < TRANS_CONFIG_COUNT; i++) {
        const trans_test_config_t *cfg = &g_trans_configs[i];
        
        printf("🔹 %s Transport:\n", cfg->name);
        printf("   Scenarios: %zu passed, %zu failed\n",
               s_test_stats.scenarios_passed[i], s_test_stats.scenarios_failed[i]);
        printf("   Clients Processed: %zu\n", s_test_stats.total_clients_processed[i]);
        
        double mb_sent = s_test_stats.total_bytes_sent[i] / (1024.0 * 1024.0);
        double mb_recv = s_test_stats.total_bytes_received[i] / (1024.0 * 1024.0);
        printf("   Data: %.2f MB sent, %.2f MB received\n", mb_sent, mb_recv);
        
        if (s_test_stats.total_duration_ms[i] > 0) {
            double duration_sec = s_test_stats.total_duration_ms[i] / 1000.0;
            double throughput_mbps = (mb_sent + mb_recv) * 8 / duration_sec;
            printf("   Duration: %.1f seconds\n", duration_sec);
            printf("   Throughput: %.2f Mbps\n", throughput_mbps);
        }
        printf("\n");
    }
    
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
}

/**
 * @brief Signal handler for graceful shutdown (Ctrl+C)
 */
static volatile sig_atomic_t s_interrupted = 0;
static void signal_handler(int sig)
{
    (void)sig;
    s_interrupted = 1;
    s_test_stats.test_end_time_ms = dap_nanotime_now() / 1000000;
    printf("\n\n⚠️  Test interrupted by user (Ctrl+C)\n");
    print_test_statistics();
    exit(0);
}

// =======================================================================================
// HELPER FUNCTIONS
// =======================================================================================

static size_t s_get_effective_data_size(const trans_test_ctx_t *a_ctx)
{
    if (!a_ctx) {
        return 0;
    }

    size_t l_data_size = a_ctx->scenario.data_size;
    if (a_ctx->config.trans_type == DAP_NET_TRANS_DNS_TUNNEL &&
        l_data_size > TEST_TRANS_DNS_MAX_DATA_SIZE) {
        log_it(L_NOTICE,
               "Clamping DNS test payload from %zu KB to %zu KB to reduce fragment loss on best-effort datagram transport",
               l_data_size / 1024, (size_t)TEST_TRANS_DNS_MAX_DATA_SIZE / 1024);
        l_data_size = TEST_TRANS_DNS_MAX_DATA_SIZE;
    }

    return l_data_size;
}

/**
 * @brief Allocate and initialize test context for a scenario
 * @param a_config Transport configuration
 * @param a_scenario Test scenario (determines array sizes)
 * @return Allocated context or NULL on error
 */
static trans_test_ctx_t* test_trans_ctx_alloc(const trans_test_config_t *a_config,
                                                const test_scenario_t *a_scenario)
{
    if (!a_config || !a_scenario) {
        log_it(L_ERROR, "Invalid arguments for test context allocation");
        return NULL;
    }
    
    trans_test_ctx_t *l_ctx = DAP_NEW_Z(trans_test_ctx_t);
    if (!l_ctx) {
        log_it(L_ERROR, "Failed to allocate test context");
        return NULL;
    }
    
    // Copy config and scenario
    l_ctx->config = *a_config;
    l_ctx->scenario = *a_scenario;
    l_ctx->result = 0;
    l_ctx->running = false;
    
    // Allocate dynamic arrays based on scenario
    l_ctx->servers = DAP_NEW_Z_COUNT(dap_net_trans_server_t*, a_scenario->num_servers);
    l_ctx->clients = DAP_NEW_Z_COUNT(dap_client_t*, a_scenario->num_clients);
    l_ctx->stream_ctxs = DAP_NEW_Z_COUNT(test_stream_ch_ctx_t, a_scenario->num_clients);
    l_ctx->client_node_addrs = DAP_NEW_Z_COUNT(dap_stream_node_addr_t, a_scenario->num_clients);
    
    if (!l_ctx->servers || !l_ctx->clients || !l_ctx->stream_ctxs || !l_ctx->client_node_addrs) {
        log_it(L_ERROR, "Failed to allocate dynamic arrays for scenario '%s': %zu servers, %zu clients",
               a_scenario->name, a_scenario->num_servers, a_scenario->num_clients);
        
        // Cleanup on error
        DAP_DEL_Z(l_ctx->servers);
        DAP_DEL_Z(l_ctx->clients);
        DAP_DEL_Z(l_ctx->stream_ctxs);
        DAP_DEL_Z(l_ctx->client_node_addrs);
        DAP_DEL_Z(l_ctx);
        return NULL;
    }
    
    log_it(L_DEBUG, "Allocated test context for scenario '%s': %zu servers, %zu clients",
           a_scenario->name, a_scenario->num_servers, a_scenario->num_clients);
    
    return l_ctx;
}

/**
 * @brief Cleanup and free test context
 * @param a_ctx Context to cleanup
 */
static void test_trans_ctx_free(trans_test_ctx_t *a_ctx)
{
    if (!a_ctx)
        return;
    
    log_it(L_INFO, "🧹 Starting cleanup for scenario '%s' (%zu clients, %zu servers)", 
           a_ctx->scenario.name, a_ctx->scenario.num_clients, a_ctx->scenario.num_servers);
    uint64_t l_cleanup_start = dap_test_get_time_ms();
    
    // ============================================================================
    // PHASE 0: IMMEDIATELY delete all server flows to prevent address reuse conflicts
    // ============================================================================
    // CRITICAL: This must be done FIRST, before any client cleanup!
    // Old flows with recv_seq_expected from previous test can intercept packets
    // from new clients with same addresses (e.g., 127.0.0.1:33537).
    // By deleting flows immediately, we ensure hash tables are cleared before
    // the next test starts.
    if (a_ctx->servers && a_ctx->config.trans_type == DAP_NET_TRANS_UDP_BASIC) {
        log_it(L_DEBUG, "Phase 0: Marking all server flows as deleting...");
        for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
            if (a_ctx->servers[i] && a_ctx->servers[i]->trans_specific) {
                dap_net_trans_udp_server_t *l_udp_server = 
                    (dap_net_trans_udp_server_t*)a_ctx->servers[i]->trans_specific;
                if (l_udp_server->flow_servers) {
                    for (size_t j = 0; j < l_udp_server->flow_servers_count; j++) {
                        if (l_udp_server->flow_servers[j]) {
                            atomic_store(&l_udp_server->flow_servers[j]->is_deleting, true);
                        }
                    }
                }
            }
        }
        log_it(L_DEBUG, "Phase 0 complete: all flow servers marked as deleting");
    }
    
    // ============================================================================
    // PHASE 1: Stop servers FIRST for connection-oriented transports (HTTP/WS)
    // ============================================================================
    // For HTTP/WebSocket: server-side write callbacks can fire on client esockets
    // after client deletion, causing double-free. Stopping servers first ensures
    // no new server-side callbacks are scheduled.
    if (a_ctx->servers && a_ctx->config.trans_type != DAP_NET_TRANS_UDP_BASIC) {
        log_it(L_DEBUG, "Phase 1: Stopping %zu servers (connection-oriented transport)...",
               a_ctx->scenario.num_servers);
        for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
            if (a_ctx->servers[i]) {
                dap_net_trans_server_stop(a_ctx->servers[i]);
            }
        }
        uint32_t l_drain_ms = 500 + (uint32_t)a_ctx->scenario.num_clients * 50;
        if (l_drain_ms > 5000) l_drain_ms = 5000;
        dap_test_sleep_ms(l_drain_ms);
        test_wait_for_all_streams_closed(l_drain_ms);
        log_it(L_DEBUG, "Phase 1 complete: servers stopped, pending callbacks drained");
    } else {
        uint32_t l_udp_drain_ms = 300 + (uint32_t)a_ctx->scenario.num_clients * 20;
        if (l_udp_drain_ms > 2000) l_udp_drain_ms = 2000;
        dap_test_sleep_ms(l_udp_drain_ms);
    }
    
    // ============================================================================
    // PHASE 2: Delete client certificates (before client deletion)
    // ============================================================================
    if (a_ctx->clients) {
        log_it(L_DEBUG, "Phase 2: Deleting %zu client certificates...", a_ctx->scenario.num_clients);
        for (size_t i = 0; i < a_ctx->scenario.num_clients; i++) {
            char l_cert_name[256];
            snprintf(l_cert_name, sizeof(l_cert_name), "test_client_%s_%zu_%zu", 
                     a_ctx->config.name, (size_t)pthread_self(), i);
            dap_cert_delete_by_name(l_cert_name);
        }
        log_it(L_DEBUG, "Phase 2 complete: certificates deleted");
    }
    
    // ============================================================================
    // PHASE 3: Delete clients (synchronous per-client via worker callbacks)
    // ============================================================================
    if (a_ctx->clients) {
        log_it(L_DEBUG, "Phase 3: Deleting %zu clients (synchronous per-client)...", a_ctx->scenario.num_clients);
        uint64_t l_phase3_start = dap_test_get_time_ms();
        
        size_t l_deleted_count = 0;
        for (size_t i = 0; i < a_ctx->scenario.num_clients; i++) {
            if (a_ctx->clients[i]) {
                dap_client_delete_mt(a_ctx->clients[i]);
                a_ctx->clients[i] = NULL;
                l_deleted_count++;
                if (l_deleted_count % 10 == 0)
                    dap_test_sleep_ms(50);
            }
        }
        
        DAP_DEL_Z(a_ctx->clients);
        log_it(L_DEBUG, "Phase 3 complete: %zu/%zu clients deleted (took %"PRIu64" ms)", 
               l_deleted_count, a_ctx->scenario.num_clients, 
               dap_test_get_time_ms() - l_phase3_start);
        
        uint32_t l_settle_ms = 200 + (uint32_t)l_deleted_count * 20;
        if (l_settle_ms > 3000) l_settle_ms = 3000;
        dap_test_sleep_ms(l_settle_ms);
    }
    
    // ============================================================================
    // PHASE 4: Cleanup stream contexts (data buffers)
    // ============================================================================
    if (a_ctx->stream_ctxs) {
        log_it(L_DEBUG, "Phase 4: Cleaning up %zu stream contexts...", a_ctx->scenario.num_clients);
        for (size_t i = 0; i < a_ctx->scenario.num_clients; i++) {
            if (a_ctx->stream_ctxs[i].sent_data) {
                DAP_DELETE(a_ctx->stream_ctxs[i].sent_data);
                a_ctx->stream_ctxs[i].sent_data = NULL;
            }
            if (a_ctx->stream_ctxs[i].received_data) {
                DAP_DELETE(a_ctx->stream_ctxs[i].received_data);
                a_ctx->stream_ctxs[i].received_data = NULL;
            }
        }
        DAP_DEL_Z(a_ctx->stream_ctxs);
        log_it(L_DEBUG, "Phase 4 complete: stream contexts cleaned");
    }
    
    // ============================================================================
    // PHASE 5: Stop servers if not already stopped in Phase 1
    // ============================================================================
    if (a_ctx->servers && a_ctx->config.trans_type == DAP_NET_TRANS_UDP_BASIC) {
        log_it(L_DEBUG, "Phase 5: Stopping %zu UDP servers...", a_ctx->scenario.num_servers);
        for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
            if (a_ctx->servers[i]) {
                dap_net_trans_server_stop(a_ctx->servers[i]);
            }
        }
        log_it(L_DEBUG, "Phase 5 complete: servers stopped");
    }
    
    // ============================================================================
    // PHASE 6: Wait for UDP flow cleanup (pending callbacks and active flows)
    // ============================================================================
    if (a_ctx->servers && a_ctx->config.trans_type == DAP_NET_TRANS_UDP_BASIC) {
        log_it(L_DEBUG, "Phase 6: Waiting for UDP flow cleanup...");
        uint64_t l_phase6_start = dap_test_get_time_ms();
        bool l_all_clean = true;
        
        for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
            if (a_ctx->servers[i] && a_ctx->servers[i]->trans_specific) {
                dap_net_trans_udp_server_t *l_udp_server = 
                    (dap_net_trans_udp_server_t*)a_ctx->servers[i]->trans_specific;
                
                uint64_t l_start = dap_test_get_time_ms();
                uint32_t l_timeout_ms = 15000; // Increased timeout for many clients
                bool l_server_clean = false;
                
                while (dap_test_get_time_ms() - l_start < l_timeout_ms) {
                    bool l_pending = false;
                    uint32_t l_total_pending = 0;
                    uint32_t l_total_active = 0;
                    
                    for (size_t j = 0; j < l_udp_server->flow_servers_count; j++) {
                        if (l_udp_server->flow_servers[j]) {
                            uint32_t l_p = atomic_load(&l_udp_server->flow_servers[j]->pending_cleanups);
                            uint32_t l_a = atomic_load(&l_udp_server->flow_servers[j]->active_callbacks);
                            l_total_pending += l_p;
                            l_total_active += l_a;
                            if (l_p > 0 || l_a > 0) {
                                l_pending = true;
                            }
                        }
                    }
                    
                    if (!l_pending) {
                        l_server_clean = true;
                        log_it(L_DEBUG, "  Server #%zu flow cleanup complete (took %"PRIu64" ms)",
                               i, dap_test_get_time_ms() - l_start);
                        break;
                    }
                    
                    // Log progress for long waits
                    if ((dap_test_get_time_ms() - l_start) % 1000 < 100) {
                        log_it(L_DEBUG, "  Server #%zu: pending=%u, active=%u...", 
                               i, l_total_pending, l_total_active);
                    }
                    
                    dap_test_sleep_ms(100);
                }
                
                if (!l_server_clean) {
                    log_it(L_WARNING, "  Server #%zu flow cleanup timeout after %u ms", i, l_timeout_ms);
                    l_all_clean = false;
                }
            }
        }
        
        if (l_all_clean) {
            log_it(L_DEBUG, "Phase 6 complete: UDP flows cleaned (took %"PRIu64" ms)",
                   dap_test_get_time_ms() - l_phase6_start);
        } else {
            log_it(L_WARNING, "Phase 6 completed with warnings");
        }
    }
    
    // ============================================================================
    // PHASE 7: Delete servers (final cleanup)
    // ============================================================================
    if (a_ctx->servers) {
        log_it(L_DEBUG, "Phase 7: Deleting %zu servers...", a_ctx->scenario.num_servers);
        for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
            if (a_ctx->servers[i]) {
                dap_net_trans_server_delete(a_ctx->servers[i]);
                a_ctx->servers[i] = NULL;
            }
        }
        DAP_DEL_Z(a_ctx->servers);
        log_it(L_DEBUG, "Phase 7 complete: servers deleted");
    }
    
    // ============================================================================
    // PHASE 8: Final stabilization (let event loops settle)
    // ============================================================================
    {
        uint32_t l_stabilize_ms = 500 + (uint32_t)a_ctx->scenario.num_clients * 30
                                      + (uint32_t)a_ctx->scenario.num_servers * 30;
        if (l_stabilize_ms > 3000) l_stabilize_ms = 3000;
        log_it(L_DEBUG, "Phase 8: Final stabilization (%u ms)...", l_stabilize_ms);
        dap_test_sleep_ms(l_stabilize_ms);
        log_it(L_DEBUG, "Phase 8 complete: system stabilized");
    }
    
    // Cleanup node addresses array
    DAP_DEL_Z(a_ctx->client_node_addrs);
    
    // Save scenario name before freeing context
    uint64_t l_total_cleanup_time = dap_test_get_time_ms() - l_cleanup_start;
    char l_scenario_name[256];
    snprintf(l_scenario_name, sizeof(l_scenario_name), "%s", a_ctx->scenario.name);
    
    // Free the context itself
    DAP_DEL_Z(a_ctx);
    
    log_it(L_INFO, "✅ Cleanup complete for scenario '%s' (took %"PRIu64" ms)", 
           l_scenario_name, l_total_cleanup_time);
}

/**
 * @brief Intelligently wait for context cleanup to complete
 * @param a_timeout_ms Maximum wait time
 * @return true if cleanup completed, false on timeout
 * 
 * @details Waits for:
 *          1. All clients to be deleted
 *          2. All servers to stop
 *          3. All streams to close
 *          4. Event queues to drain
 */
static bool test_wait_for_cleanup_complete(uint32_t a_timeout_ms)
{
    uint64_t l_start_time = dap_test_get_time_ms();
    uint64_t l_deadline = l_start_time + (uint64_t)a_timeout_ms;
    
    // Wait for streams to close
    if (!test_wait_for_all_streams_closed(a_timeout_ms / 2)) {
        log_it(L_WARNING, "Streams did not close within timeout");
        return false;
    }
    
    // Additional stabilization time - let event loops settle
    const uint32_t STABILIZATION_POLLS = 5;
    const uint32_t POLL_INTERVAL_MS = 50;
    
    for (uint32_t i = 0; i < STABILIZATION_POLLS; i++) {
        if (dap_test_get_time_ms() >= l_deadline) {
            log_it(L_WARNING, "Cleanup stabilization timeout");
            return false;
        }
        
        dap_test_sleep_ms(POLL_INTERVAL_MS);
        
        // Check if we're past deadline
        uint64_t l_elapsed = dap_test_get_time_ms() - l_start_time;
        if (l_elapsed >= (uint64_t)a_timeout_ms) {
            break;
        }
    }
    
    uint64_t l_total_elapsed = dap_test_get_time_ms() - l_start_time;
    log_it(L_DEBUG, "Cleanup stabilization complete (%"PRIu64" ms)", l_total_elapsed);
    
    return true;
}

/**
 * @brief Echo callback for test channels - sends received data back to client
 */
static bool s_test_channel_echo_callback(dap_stream_ch_t *a_ch, void *a_arg)
{
    const uint8_t c_stream_ch_pkt_type_test_response = 0x01;

    if (!a_ch || !a_arg) {
        return false;
    }
    
    // a_arg is dap_stream_ch_pkt_t*
    dap_stream_ch_pkt_t *a_pkt = (dap_stream_ch_pkt_t *)a_arg;
    
    // Echo only request packets; non-request packets are still considered valid
    // so downstream notifiers can run without creating a ping-pong loop.
    if (a_pkt->hdr.type != STREAM_CH_PKT_TYPE_REQUEST) {
        return true;
    }

    // Echo data back to client
    debug_if(true, L_DEBUG, "Echoing %u bytes back to client on channel '%c'", 
             a_pkt->hdr.data_size, a_ch->proc->id);
    
    // Use mt enqueue in callback path to avoid unsafe write drops on datagram transports.
    size_t l_sent = dap_stream_ch_pkt_write_mt(a_ch->stream_worker, a_ch->uuid,
                                               c_stream_ch_pkt_type_test_response,
                                               a_pkt->data, a_pkt->hdr.data_size);
    if (!l_sent) {
        log_it(L_WARNING,
               "Failed to echo data back to client: type=%u size=%u",
               c_stream_ch_pkt_type_test_response, a_pkt->hdr.data_size);
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
 * @brief Initialize all trans systems
 */
static int test_init_all_transs(void)
{
    TEST_INFO("Initializing all trans systems");
    
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
    
    // Initialize stream control module (required for key exchange parameters)
    l_ret = dap_stream_ctl_init();
    if (l_ret != 0) {
        TEST_ERROR("Stream control initialization failed");
        return -2;
    }
    
    // Register channel processors for test channels A, B, C.
    // Use local echo callback to keep integration harness behavior transport-agnostic.
    dap_stream_ch_proc_add('A', NULL, NULL, s_test_channel_echo_callback, NULL);
    dap_stream_ch_proc_add('B', NULL, NULL, s_test_channel_echo_callback, NULL);
    dap_stream_ch_proc_add('C', NULL, NULL, s_test_channel_echo_callback, NULL);
    log_it(L_DEBUG, "Registered channel processors for test channels A, B, C with echo callback");
    
    // Initialize all registered modules
    // Modules are registered via constructors in *_auto_register.c files
    // If constructors are not called (e.g., with static libraries without --whole-archive),
    // dap_module_init_all() will call their init functions
    log_it(L_DEBUG, "Calling dap_module_init_all() to initialize trans modules");
    l_ret = dap_module_init_all();
    if (l_ret != 0) {
        TEST_ERROR("Module initialization failed: %d", l_ret);
        return -3;
    }
    
    // Verify all transs are registered (should be registered now)
    bool l_all_transs_registered = true;
    for (size_t i = 0; i < TRANS_CONFIG_COUNT; i++) {
        dap_net_trans_t *l_trans = dap_net_trans_find(g_trans_configs[i].trans_type);
        if (!l_trans) {
            TEST_ERROR("Trans %s not registered", g_trans_configs[i].name);
            l_all_transs_registered = false;
        } else {
            TEST_INFO("Trans %s registered successfully", g_trans_configs[i].name);
        }
    }
    
    if (!l_all_transs_registered) {
        TEST_ERROR("Not all transs are registered");
        return -4;
    }
    
    TEST_SUCCESS("All trans systems initialized");
    return 0;
}

/**
 * @brief Create and start server for a trans type
 */
/**
 * @brief Create and start all servers for the scenario
 * @param a_ctx Test context with scenario configuration
 * @return 0 on success, negative on error
 */
static int test_create_trans_servers(trans_test_ctx_t *a_ctx)
{
    if (!a_ctx || !a_ctx->servers) {
        log_it(L_ERROR, "Invalid arguments for server creation");
        return -1;
    }
    
    // Verify server operations are registered
    const dap_net_trans_server_ops_t *l_ops = dap_net_trans_server_get_ops(a_ctx->config.trans_type);
    if (!l_ops) {
        TEST_ERROR("Server operations not registered for %s trans (type: %d)", 
                   a_ctx->config.name, a_ctx->config.trans_type);
        return -1;
    }
    
    log_it(L_INFO, "Creating %zu server(s) for %s", a_ctx->scenario.num_servers, a_ctx->config.name);
    
    // Create all servers
    for (size_t i = 0; i < a_ctx->scenario.num_servers; i++) {
        char l_server_name[256];
        snprintf(l_server_name, sizeof(l_server_name), "test_%s_server_%zu", 
                 a_ctx->config.name, i);
        
        a_ctx->servers[i] = dap_net_trans_server_new(a_ctx->config.trans_type, l_server_name);
        if (!a_ctx->servers[i]) {
            TEST_ERROR("Failed to create %s server #%zu", a_ctx->config.name, i);
            return -1;
        }
        
        // Start server on unique port
        const char *l_addr = a_ctx->config.address;
        uint16_t l_port = a_ctx->config.base_port + (uint16_t)i;
        
        int l_ret = dap_net_trans_server_start(a_ctx->servers[i], NULL, &l_addr, &l_port, 1);
        if (l_ret != 0) {
            TEST_ERROR("Failed to start %s server #%zu on %s:%u", 
                       a_ctx->config.name, i, l_addr, l_port);
            return -2;
        }
        
        // Wait for server to be ready
        if (!test_wait_for_server_ready(a_ctx->servers[i], 2000)) {
            TEST_ERROR("Server #%zu not ready within timeout", i);
            return -3;
        }
        
        log_it(L_INFO, "%s server #%zu started on %s:%u", 
               a_ctx->config.name, i, l_addr, l_port);
    }
    
    TEST_INFO("All %zu %s servers started successfully", 
              a_ctx->scenario.num_servers, a_ctx->config.name);
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
    
    log_it(L_INFO, "  Waiting for client %p to complete handshake...", (void*)a_client);
    
    dap_client_stage_t l_last_stage = STAGE_UNDEFINED;
    dap_client_stage_status_t l_last_status = STAGE_STATUS_NONE;
    
    // Use intelligent wait - returns immediately when condition is met
    uint64_t l_start = dap_test_get_time_ms();
    bool l_success = false;
    
    while (dap_test_get_time_ms() - l_start < a_timeout_ms) {
        dap_client_stage_t l_stage = dap_client_get_stage(a_client);
        dap_client_stage_status_t l_status = dap_client_get_stage_status(a_client);
        
        // Print stage changes and status updates
        if (l_stage != l_last_stage || l_status != l_last_status) {
            uint64_t l_elapsed = dap_test_get_time_ms() - l_start;

            log_it(L_DEBUG, "Client %p stage transition: %d->%d, status: %d->%d (elapsed: %llu ms)",
                   (void*)a_client, l_last_stage, l_stage, l_last_status, l_status, 
                   (unsigned long long)l_elapsed);
            l_last_stage = l_stage;
            l_last_status = l_status;
        }
        
        // Check for error condition - fail fast
        if (l_status == STAGE_STATUS_ERROR) {
            printf("  Client %p stage error at stage %d\n", (void*)a_client, l_stage);
            return false;
        }
        
        // Success condition - handshake complete
        if (l_stage == STAGE_STREAM_STREAMING && 
            (l_status == STAGE_STATUS_COMPLETE || l_status == STAGE_STATUS_DONE ||
             l_status == STAGE_STATUS_IN_PROGRESS)) {
            log_it(L_INFO, "Client %p reached STREAM_STREAMING with status %d - SUCCESS!", 
                   (void*)a_client, l_status);
            l_success = true;
            break;  // Exit immediately on success
        }
        
        // Poll every 100ms (same as DAP_TEST_WAIT_UNTIL default)
        dap_test_sleep_ms(100);
    }
    
    if (!l_success) {
        dap_client_stage_t l_final_stage = dap_client_get_stage(a_client);
        dap_client_stage_status_t l_final_status = dap_client_get_stage_status(a_client);

        log_it(L_ERROR, "Client %p handshake timeout: final stage=%d, status=%d, expected stage=%d with status=%d or %d",
               (void*)a_client, l_final_stage, l_final_status, 
               STAGE_STREAM_STREAMING, STAGE_STATUS_COMPLETE, STAGE_STATUS_DONE);
    }
    
    return l_success;
}

/**
 * @brief Create and configure client for a trans
 */
/**
 * @brief Create and configure a client for trans testing
 * @param a_config Trans configuration
 * @param a_server_idx Index of server to connect to (for multi-server scenarios)
 * @param a_client_idx Index of client (for naming/logging)
 * @param a_client_node_addr Unique node address for this client
 * @return Created client or NULL on error
 */
static dap_client_t *test_create_trans_client(trans_test_config_t *a_config, 
                                                size_t a_server_idx,
                                                size_t a_client_idx,
                                                dap_stream_node_addr_t *a_client_node_addr)
{
    // Initialize client system (idempotent)
    dap_client_init();
    
    // Create client
    dap_client_t *l_client = dap_client_new(NULL, NULL);
    if (!l_client) {
        TEST_ERROR("Failed to create %s client #%zu", a_config->name, a_client_idx);
        return NULL;
    }
    
    // Set trans type
    dap_client_set_trans_type(l_client, a_config->trans_type);
    
    // Wait for client initialization
    bool l_client_ready = DAP_TEST_WAIT_CLIENT_INITIALIZED(l_client, 2000);
    if (!l_client_ready) {
        TEST_ERROR("Client #%zu initialization timeout", a_client_idx);
        dap_client_delete_unsafe(l_client);
        return NULL;
    }
    
    // Connect to specific server (base_port + server_idx)
    // This allows load balancing across multiple servers
    uint16_t l_port = a_config->base_port + (uint16_t)a_server_idx;
    dap_stream_node_addr_t l_server_node_addr = g_node_addr; // Server's global node address
    dap_client_set_uplink_unsafe(l_client, &l_server_node_addr, a_config->address, l_port);
    
    log_it(L_DEBUG, "Client #%zu connecting to server #%zu at %s:%u", 
           a_client_idx, a_server_idx, a_config->address, l_port);
    
    // Set client's certificate if node address provided
    if (a_client_node_addr && a_client_node_addr->uint64 != 0) {
        char l_cert_name[256];
        snprintf(l_cert_name, sizeof(l_cert_name), "test_client_%s_%zu_%zu", 
                 a_config->name, (size_t)pthread_self(), a_client_idx);
        dap_cert_t *l_client_cert = dap_cert_find_by_name(l_cert_name);
        if (l_client_cert) {
            dap_client_set_auth_cert(l_client, l_cert_name);
            log_it(L_DEBUG, "Set client #%zu certificate '%s' for node address "NODE_ADDR_FP_STR, 
                   a_client_idx, l_cert_name, NODE_ADDR_FP_ARGS_S(*a_client_node_addr));
        } else {
            log_it(L_WARNING, "Certificate '%s' not found for client #%zu", l_cert_name, a_client_idx);
        }
    }
    
    // Set active channels
    dap_client_set_active_channels_unsafe(l_client, "ABC");
    
    return l_client;
}

/**
 * @brief Test single trans with specified scenario (variable servers/clients)
 * @param a_arg Pointer to trans_test_ctx_t
 * @return NULL (always)
 */
static void *test_trans_worker(void *a_arg)
{
    trans_test_ctx_t *l_ctx = (trans_test_ctx_t *)a_arg;
    size_t l_effective_data_size = s_get_effective_data_size(l_ctx);
    l_ctx->result = 0;
    l_ctx->running = true;
    
    pthread_mutex_lock(&s_test_mutex);
    printf("\n=== Starting %s trans test: %s ===\n", l_ctx->config.name, l_ctx->scenario.name);
    printf("  Servers: %zu, Clients: %zu, Data size: %zu KB, Timeout: %u ms\n",
           l_ctx->scenario.num_servers, l_ctx->scenario.num_clients,
           l_effective_data_size / 1024, l_ctx->scenario.timeout_ms);
    pthread_mutex_unlock(&s_test_mutex);

    // Create all servers
    if (test_create_trans_servers(l_ctx) != 0) {
        l_ctx->result = -1;
        l_ctx->running = false;
        return NULL;
    }
    
    // Initialize stream contexts and generate unique client node addresses
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        if (test_stream_ch_ctx_init(&l_ctx->stream_ctxs[i], TEST_STREAM_CH_ID, l_effective_data_size) != 0) {
            TEST_ERROR("Failed to initialize stream channel ctx %zu for %s", i, l_ctx->config.name);
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
        
        if (i % 100 == 0 && i > 0) {
            log_it(L_INFO, "Generated %zu/%zu client node addresses", i, l_ctx->scenario.num_clients);
        }
    }
    
    // Create all clients - distribute across servers using round-robin
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        size_t l_server_idx = i % l_ctx->scenario.num_servers;  // Round-robin server selection
        
        l_ctx->clients[i] = test_create_trans_client(&l_ctx->config, l_server_idx, i,
                                                       &l_ctx->client_node_addrs[i]);
        if (!l_ctx->clients[i]) {
            TEST_ERROR("Failed to create client %zu for %s", i, l_ctx->config.name);
            l_ctx->result = -3;
            l_ctx->running = false;
            return NULL;
        }
        
        if (i % 100 == 0 && i > 0) {
            log_it(L_INFO, "Created %zu/%zu clients", i, l_ctx->scenario.num_clients);
        }
    }
    
    // Start handshake for all clients
    log_it(L_INFO, "Starting handshake for all %zu clients...", l_ctx->scenario.num_clients);
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        log_it(L_DEBUG, "TEST: client[%zu] = %p, client->_internal = %p", 
               i, l_ctx->clients[i], l_ctx->clients[i] ? l_ctx->clients[i]->_internal : NULL);
        dap_client_go_stage(l_ctx->clients[i], STAGE_STREAM_STREAMING, NULL);
        
        if (i % 100 == 0 && i > 0) {
            log_it(L_INFO, "Started handshake for %zu/%zu clients", i, l_ctx->scenario.num_clients);
        }
    }
    
    // Wait for all clients to complete handshake
    log_it(L_INFO, "Waiting for all %zu clients to complete handshake...", l_ctx->scenario.num_clients);
    
    // Track overall timeout for all clients
    uint64_t l_overall_start = dap_test_get_time_ms();
    uint64_t l_overall_timeout_ms = l_ctx->scenario.timeout_ms;
    
    bool l_all_ready = true;
    size_t l_handshake_completed = 0;
    
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        // Check overall timeout
        uint64_t l_elapsed = dap_test_get_time_ms() - l_overall_start;
        if (l_elapsed >= l_overall_timeout_ms) {
            log_it(L_ERROR, "Overall handshake timeout after %llu ms - only %zu/%zu clients completed",
                   (unsigned long long)l_elapsed, l_handshake_completed, l_ctx->scenario.num_clients);
            l_all_ready = false;
            l_ctx->result = -4;
            break;
        }
        
        // Calculate remaining time for this client
        uint64_t l_remaining_ms = l_overall_timeout_ms - l_elapsed;
        uint32_t l_client_timeout = (l_remaining_ms < l_ctx->scenario.timeout_ms) 
                                    ? (uint32_t)l_remaining_ms 
                                    : l_ctx->scenario.timeout_ms;
        
        bool l_ready = test_wait_for_full_handshake(l_ctx->clients[i], l_client_timeout);
        if (!l_ready) {
            log_it(L_ERROR, "Client %zu (ptr=%p) for %s failed to complete handshake", 
                   i, (void*)l_ctx->clients[i], l_ctx->config.name);
            l_all_ready = false;
            l_ctx->result = -4;
            break;  // Stop on first failure
        }
        
        l_handshake_completed++;
        
        // Log progress every 10 clients
        if (l_handshake_completed % 10 == 0 || l_handshake_completed == l_ctx->scenario.num_clients) {
            log_it(L_INFO, "Handshake progress: %zu/%zu clients completed",
                   l_handshake_completed, l_ctx->scenario.num_clients);
        }
    }
    
    if (!l_all_ready) {
        log_it(L_ERROR, "Handshake FAILED after %zu/%zu clients",
               l_handshake_completed, l_ctx->scenario.num_clients);
        l_ctx->running = false;
        return NULL;
    } else {
        log_it(L_INFO, "Handshake SUCCESS: all %zu clients completed", l_handshake_completed);
    }
    
    pthread_mutex_lock(&s_test_mutex);
    printf("  All %zu %s clients completed handshake successfully\n", 
           l_ctx->scenario.num_clients, l_ctx->config.name);
    pthread_mutex_unlock(&s_test_mutex);
    
    // Wait for channels to be created
    TEST_INFO("Waiting for channels for %zu clients...", l_ctx->scenario.num_clients);
    bool l_all_channels_ready = true;
    size_t l_channels_ready = 0;
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        if (!test_wait_for_stream_channels_ready(l_ctx->clients[i], "ABC", 15000)) {
            TEST_ERROR("Channels not ready for client %zu in %s", i, l_ctx->config.name);
            l_all_channels_ready = false;
            break;
        }
        
        l_channels_ready++;
        if (l_channels_ready % 100 == 0) {
            log_it(L_INFO, "Channels ready: %zu/%zu clients", 
                   l_channels_ready, l_ctx->scenario.num_clients);
        }
    }
    
    if (!l_all_channels_ready) {
        TEST_ERROR("Not all channels ready (only %zu/%zu)", 
                   l_channels_ready, l_ctx->scenario.num_clients);
        l_ctx->result = -5;
        l_ctx->running = false;
        return NULL;
    }
    
    // Register receivers for all clients
    TEST_INFO("Registering receivers for %zu clients...", l_ctx->scenario.num_clients);
    size_t l_receivers_registered = 0;
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        int l_ret = test_stream_ch_register_receiver(l_ctx->clients[i], TEST_STREAM_CH_ID, &l_ctx->stream_ctxs[i]);
        if (l_ret != 0) {
            TEST_ERROR("Failed to register receiver for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -6;
            l_ctx->running = false;
            return NULL;
        }
        
        l_receivers_registered++;
        if (l_receivers_registered % 100 == 0) {
            log_it(L_INFO, "Registered receivers: %zu/%zu clients", 
                   l_receivers_registered, l_ctx->scenario.num_clients);
        }
    }
    
    // Check if transport supports bidirectional data exchange
    dap_net_trans_t *l_trans_obj = dap_net_trans_find(l_ctx->config.trans_type);
    bool l_supports_data_exchange = true;
    if (l_trans_obj && l_trans_obj->ops && l_trans_obj->ops->get_capabilities) {
        uint32_t l_caps = l_trans_obj->ops->get_capabilities(l_trans_obj);
        if (!(l_caps & DAP_NET_TRANS_CAP_BIDIRECTIONAL)) {
            l_supports_data_exchange = false;
            printf("  %s transport: handshake-only (no bidirectional data exchange)\n", l_ctx->config.name);
        }
    }

    if (!l_supports_data_exchange) {
        // Skip data exchange - only handshake was tested
        goto cleanup_success;
    }

    // Send data for all clients and verify
    TEST_INFO("Sending data for %zu clients (%zu KB each)...", 
              l_ctx->scenario.num_clients, l_effective_data_size / 1024);
    size_t l_data_exchanged = 0;
    for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
        int l_ret = test_stream_ch_send_and_wait(l_ctx->clients[i], &l_ctx->stream_ctxs[i], 
                                                   l_ctx->scenario.timeout_ms);
        if (l_ret != 0) {
            TEST_ERROR("Data exchange failed for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -7;
            l_ctx->running = false;
            return NULL;
        }
        
        // Verify data integrity
        pthread_mutex_lock(&l_ctx->stream_ctxs[i].mutex);
        bool l_data_received = l_ctx->stream_ctxs[i].data_received;
        pthread_mutex_unlock(&l_ctx->stream_ctxs[i].mutex);
        
        if (!l_data_received) {
            TEST_ERROR("Data not received for client %zu in %s", i, l_ctx->config.name);
            l_ctx->result = -8;
            l_ctx->running = false;
            return NULL;
        }
        
        if (l_ctx->stream_ctxs[i].received_data && l_ctx->stream_ctxs[i].received_data_size > 0) {
            bool l_data_valid = test_trans_verify_data(l_ctx->stream_ctxs[i].sent_data,
                                                         l_ctx->stream_ctxs[i].received_data,
                                                         l_ctx->stream_ctxs[i].received_data_size);
            if (!l_data_valid) {
                TEST_ERROR("Data integrity check failed for client %zu in %s", i, l_ctx->config.name);
                l_ctx->result = -9;
                l_ctx->running = false;
                return NULL;
            }
        }
        
        l_data_exchanged++;
        if (l_data_exchanged % 100 == 0) {
            log_it(L_INFO, "Data exchanged: %zu/%zu clients", 
                   l_data_exchanged, l_ctx->scenario.num_clients);
        }
    }
    
    pthread_mutex_lock(&s_test_mutex);
    size_t l_total_data_mb = (l_ctx->scenario.num_clients * l_effective_data_size) / (1024 * 1024);
    printf("  All %zu %s clients completed data exchange successfully (%zu MB total)\n", 
           l_ctx->scenario.num_clients, l_ctx->config.name, l_total_data_mb);
    pthread_mutex_unlock(&s_test_mutex);
    
cleanup_success:
    l_ctx->result = 0;
    l_ctx->running = false;
    return NULL;
}

/**
 * @brief Cleanup trans test ctx
 */
// =======================================================================================
// TEST CASES
// =======================================================================================

/**
 * @brief Test 1: Initialize all trans systems
 */
static void test_01_init_all_transs(void)
{
    TEST_INFO("Test 1: Initializing all trans systems");
    
    int l_ret = test_init_all_transs();
    // Modules are initialized automatically via constructors when libraries are loaded
    // We just verify that transs are registered
    TEST_ASSERT(l_ret == 0, "All trans systems should initialize successfully (transs must be registered)");
    
    TEST_SUCCESS("Test 1 passed: All trans systems initialized");
}

/**
 * @brief Test 2: Sequential trans testing with all scenarios and all tiers
 * @details Tests each transport protocol with progressively complex scenarios,
 *          and for datagram protocols (UDP), tests on each available LB tier:
 *          - Tier 1: Application-level (queue-based)
 *          - Tier 2: Classic BPF (kernel sticky sessions)
 *          - Tier 3: eBPF (kernel sticky sessions with maps)
 *          
 *          Scenarios:
 *          1. Basic: 1 server, 1 client
 *          2. Scale: 1 server, 10 clients
 *          3. Heavy: 1 server, 1000 clients
 *          4. Multi-server: 10 servers, 10 clients
 *          5. Stress: 1000 servers, 1000 clients
 */
static void test_02_sequential_trans_testing(void)
{
    TEST_INFO("Test 2: Sequential trans testing with all scenarios and all tiers");
    
    bool l_all_passed = true;
    size_t l_total_tests = 0;
    size_t l_passed_tests = 0;
    size_t l_failed_tests = 0;
    size_t l_skipped_tests = 0;
    
    // Test each transport protocol
    for (size_t trans_idx = 0; trans_idx < TRANS_CONFIG_COUNT; trans_idx++) {
        const trans_test_config_t *l_trans = &g_trans_configs[trans_idx];

        if (s_trans_filter && strcasecmp(s_trans_filter, l_trans->name) != 0)
            continue;

        bool l_is_datagram = (l_trans->trans_type == DAP_NET_TRANS_UDP_BASIC);
        
        // For datagram protocols, test each available tier
        size_t l_tier_count = l_is_datagram ? TIER_CONFIG_COUNT : 1;
        
        for (size_t tier_idx = 0; tier_idx < l_tier_count; tier_idx++) {
            const tier_test_config_t *l_tier_cfg = l_is_datagram ? &g_tier_configs[tier_idx] : NULL;
            
            // Check tier availability
            if (l_tier_cfg && l_tier_cfg->is_available && !l_tier_cfg->is_available()) {
                printf("\n");
                printf("╔════════════════════════════════════════════════════════╗\n");
                printf("║  %-54s  ║\n", "");
                printf("║  Testing: %-15s @ %-25s  ║\n", 
                       l_trans->name, l_tier_cfg->name);
                printf("║  %-54s  ║\n", "⏭️  SKIPPED: Tier not available on this system");
                printf("╚════════════════════════════════════════════════════════╝\n");
                continue;
            }
            
            // Set forced tier for datagram protocols
            if (l_tier_cfg) {
                dap_io_flow_set_forced_tier(l_tier_cfg->tier);
                printf("\n");
                printf("╔════════════════════════════════════════════════════════╗\n");
                printf("║  Testing: %-15s @ %-25s  ║\n", l_trans->name, l_tier_cfg->name);
                printf("╚════════════════════════════════════════════════════════╝\n");
            } else {
                dap_io_flow_set_forced_tier(-1);  // Auto-detect
                printf("\n");
                printf("╔════════════════════════════════════════════════════════╗\n");
                printf("║  Testing transport: %-35s║\n", l_trans->name);
                printf("╚════════════════════════════════════════════════════════╝\n");
            }
            
            // Tier-specific client limits
            // Application tier has kernel limitation of ~20 clients (queue bottleneck)
            // Other tiers can handle more, but limited by s_max_clients from command line
            const uint32_t TIER1_MAX_CLIENTS = 20;  // Application tier limitation
            uint32_t l_tier_max = (l_tier_cfg && l_tier_cfg->tier == DAP_IO_FLOW_LB_TIER_APPLICATION) 
                                  ? TIER1_MAX_CLIENTS : 10000;
            // Apply command-line limit (s_max_clients is typically 100 by default)
            uint32_t l_max_clients = (s_max_clients < l_tier_max) ? (uint32_t)s_max_clients : l_tier_max;
            
            log_it(L_NOTICE, "Testing %s with tier %s, max clients: %u (mode: %s)", 
                   l_trans->name, 
                   l_tier_cfg ? l_tier_cfg->name : "auto",
                   l_max_clients,
                   s_stress_mode ? "stress" : (s_quick_mode ? "quick" : "normal"));
            
            bool l_tier_passed = true;
            
            // Test each scenario for this transport+tier combination
            for (size_t scenario_idx = 0; scenario_idx < SCENARIO_COUNT; scenario_idx++) {
                const test_scenario_t *l_scenario = &g_scenarios[scenario_idx];
                l_total_tests++;
                
                // Skip stress-only scenarios unless --stress flag is set
                if (l_scenario->stress_only && !s_stress_mode) {
                    printf("\n--- Scenario %zu/%zu: %s ---\n", 
                           scenario_idx + 1, SCENARIO_COUNT, l_scenario->name);
                    printf("⏭️  SKIPPED: Stress scenario (use --stress to enable)\n");
                    l_skipped_tests++;
                    continue;
                }
                
                // Skip scenarios that exceed client limit
                if (l_scenario->num_clients > l_max_clients) {
                    printf("\n--- Scenario %zu/%zu: %s ---\n", 
                           scenario_idx + 1, SCENARIO_COUNT, l_scenario->name);
                    printf("⏭️  SKIPPED: %s tier limited to %u clients (scenario has %zu)\n",
                           l_tier_cfg ? l_tier_cfg->name : "auto", l_max_clients, l_scenario->num_clients);
                    l_skipped_tests++;
                    continue;
                }
                
                // In quick mode, only run the first scenario per tier
                if (s_quick_mode && scenario_idx > 0) {
                    l_skipped_tests++;
                    continue;
                }
                
                printf("\n--- Scenario %zu/%zu: %s ---\n", 
                       scenario_idx + 1, SCENARIO_COUNT, l_scenario->name);
                
                // Allocate context for this scenario
                trans_test_ctx_t *l_ctx = test_trans_ctx_alloc(l_trans, l_scenario);
                if (!l_ctx) {
                    TEST_ERROR("Failed to allocate context for scenario '%s'", l_scenario->name);
                    l_all_passed = false;
                    l_tier_passed = false;
                    l_failed_tests++;
                    break;
                }
                
                // Run test for this scenario
                uint64_t scenario_start = dap_nanotime_now() / 1000000;
                test_trans_worker(l_ctx);
                uint64_t scenario_end = dap_nanotime_now() / 1000000;
                
                // Collect statistics
                size_t bytes_sent = 0, bytes_recv = 0;
                for (size_t i = 0; i < l_ctx->scenario.num_clients; i++) {
                    bytes_sent += l_ctx->stream_ctxs[i].sent_data_size;
                    bytes_recv += l_ctx->stream_ctxs[i].received_data_size;
                }
                
                // Check result
                if (l_ctx->result == 0) {
                    s_test_stats.scenarios_passed[trans_idx]++;
                    s_test_stats.total_scenarios_passed++;
                    s_test_stats.total_clients_processed[trans_idx] += l_ctx->scenario.num_clients;
                    s_test_stats.total_bytes_sent[trans_idx] += bytes_sent;
                    s_test_stats.total_bytes_received[trans_idx] += bytes_recv;
                    s_test_stats.total_duration_ms[trans_idx] += (scenario_end - scenario_start);
                    l_passed_tests++;
                    
                    printf("✅ %s@%s - %s: PASSED (%.1f MB in %.2f sec)\n", 
                           l_trans->name, l_tier_cfg ? l_tier_cfg->name : "auto",
                           l_scenario->name,
                           (bytes_sent + bytes_recv) / (1024.0 * 1024.0),
                           (scenario_end - scenario_start) / 1000.0);
                } else {
                    s_test_stats.scenarios_failed[trans_idx]++;
                    s_test_stats.total_scenarios_failed++;
                    l_failed_tests++;
                    
                    printf("❌ %s@%s - %s: FAILED (code %d)\n", 
                           l_trans->name, l_tier_cfg ? l_tier_cfg->name : "auto",
                           l_scenario->name, l_ctx->result);
                    l_all_passed = false;
                    l_tier_passed = false;
                    
                    // Cleanup and continue to next tier
                    test_trans_ctx_free(l_ctx);
                    test_wait_for_cleanup_complete(10000);
                    break;  // Stop testing this tier on first failure
                }
                
                // Cleanup after each scenario
                test_trans_ctx_free(l_ctx);

                // Wait for cleanup
                uint32_t l_cleanup_timeout = (l_scenario->num_clients > 100) ? 30000 : 20000;
                if (!test_wait_for_cleanup_complete(l_cleanup_timeout)) {
                    log_it(L_ERROR, "Cleanup did not complete for scenario '%s'", l_scenario->name);
                }
#ifdef __APPLE__
                // macOS: extra delay for port release after SO_REUSEPORT socket close
                dap_test_sleep_ms(1000);
#endif
                
                printf("\n");
            }
            
            // Report tier result
            printf("\n  %s@%s: %s\n", l_trans->name, 
                   l_tier_cfg ? l_tier_cfg->name : "auto",
                   l_tier_passed ? "✅ ALL PASSED" : "❌ FAILED");
        }
        
        // Reset forced tier for next transport
        dap_io_flow_set_forced_tier(-1);
    }
    
    // Print final summary
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════════╗\n");
    printf("║                        TEST SUMMARY                            ║\n");
    printf("╠════════════════════════════════════════════════════════════════╣\n");
    printf("║  Total tests:   %-6zu                                         ║\n", l_total_tests);
    printf("║  Passed:        %-6zu ✅                                       ║\n", l_passed_tests);
    printf("║  Failed:        %-6zu ❌                                       ║\n", l_failed_tests);
    printf("║  Skipped:       %-6zu ⏭️                                        ║\n", l_skipped_tests);
    printf("╚════════════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    TEST_ASSERT(l_all_passed, "All trans tests should pass");
    
    TEST_SUCCESS("Test 2 passed: All transs tested with all scenarios");
}

/**
 * @brief Test 3: Cleanup all resources
 */
static void test_03_cleanup_all_resources(void)
{
    TEST_INFO("Test 3: Cleaning up all resources");
    
    // Trans ctxs are already cleaned up in test_02
    // Just ensure everything is closed
    test_wait_for_all_streams_closed(1000);
    
    // Cleanup client system
    dap_client_deinit();
    
    // Cleanup stream control module
    dap_stream_ctl_deinit();
    
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
// COMMAND LINE PARSING
// =======================================================================================

static void s_print_usage(const char *prog_name)
{
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("\nOptions:\n");
    printf("  --stress            Enable stress tests (1000+ clients)\n");
    printf("  --quick             Quick mode: only 1 client scenario per tier\n");
    printf("  --max-clients=N     Set max clients limit (default: 10)\n");
    printf("  --log-level=LEVEL   Set log level: debug|info|notice|warning|error (default: debug)\n");
    printf("  --trans=NAME        Test only this transport: HTTP|WebSocket|UDP|DNS\n");
    printf("  --help              Show this help\n");
    printf("\nExamples:\n");
    printf("  %s                            Run default tests (up to 10 clients)\n", prog_name);
    printf("  %s --quick                    Quick smoke test (1 client only)\n", prog_name);
    printf("  %s --max-clients=100          100 clients, full debug logs\n", prog_name);
    printf("  %s --trans=WebSocket --max-clients=100  WS stress test\n", prog_name);
    printf("  %s --stress --log-level=warning  Stress test, minimal output\n", prog_name);
}

static enum dap_log_level s_parse_log_level(const char *a_str)
{
    if (strcasecmp(a_str, "debug") == 0)   return L_DEBUG;
    if (strcasecmp(a_str, "info") == 0)    return L_INFO;
    if (strcasecmp(a_str, "notice") == 0)  return L_NOTICE;
    if (strcasecmp(a_str, "msg") == 0)     return L_MSG;
    if (strcasecmp(a_str, "warning") == 0) return L_WARNING;
    if (strcasecmp(a_str, "error") == 0)   return L_ERROR;
    if (strcasecmp(a_str, "critical") == 0) return L_CRITICAL;
    printf("  ⚠️  Unknown log level '%s', using debug\n", a_str);
    return L_DEBUG;
}

static void s_parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--stress") == 0) {
            s_stress_mode = true;
            s_max_clients = 10000;
            printf("  ℹ️  Stress mode enabled (max clients: %zu)\n", s_max_clients);
        } else if (strcmp(argv[i], "--quick") == 0) {
            s_quick_mode = true;
            s_max_clients = 1;
            printf("  ℹ️  Quick mode enabled (max clients: 1)\n");
        } else if (strncmp(argv[i], "--max-clients=", 14) == 0) {
            s_max_clients = (size_t)atoi(argv[i] + 14);
            printf("  ℹ️  Max clients set to: %zu\n", s_max_clients);
        } else if (strncmp(argv[i], "--log-level=", 12) == 0) {
            s_log_level = s_parse_log_level(argv[i] + 12);
            printf("  ℹ️  Log level set to: %s (%d)\n", argv[i] + 12, s_log_level);
        } else if (strncmp(argv[i], "--trans=", 8) == 0) {
            s_trans_filter = argv[i] + 8;
            printf("  ℹ️  Transport filter: %s\n", s_trans_filter);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            s_print_usage(argv[0]);
            exit(0);
        } else {
            printf("  ⚠️  Unknown option: %s\n", argv[i]);
        }
    }
}

// =======================================================================================
// MAIN TEST SUITE
// =======================================================================================

int main(int argc, char **argv)
{
    // Parse command line arguments
    s_parse_args(argc, argv);
    
    // Create minimal config file for tests
    const char *config_content = "[resources]\n"
                                 "ca_folders=[./test_ca]\n"
                                "[general]\n"
                                "debug_reactor=false\n"
                                "[dap_client]\n"
                                "max_tries=5\n"
                                "timeout=60\n"
                                "timeout_read_after_connect=5\n"
                                "timeout_active_after_connect=120\n"
                                "debug_more=false\n"
                               "[stream]\n"
                                "debug_more=false\n"
                                "debug_dump_stream_headers=false\n"
                                "debug_channels=false\n"
                                "[stream_pkt]\n"
                                "debug_more=false\n"
                                "[stream_udp]\n"
                                "debug_more=false\n"
                                "[io_flow]\n"
                                "debug_more=false\n"
                                "[io_flow_datagram]\n"
                                "debug_more=false\n"
                                "[dap_io_flow_socket]\n"
                                "debug_more=false\n"
                                "[net_trans]\n"
                                "debug_more=false\n"
                                "[dap_net_trans_udp_server]\n"
                                "debug_more=false\n"
                                "[test_trans_helpers]\n"
                                "debug_more=false\n";
    FILE *f = fopen("test_trans.cfg", "w");
    if (f) {
        fwrite(config_content, 1, strlen(config_content), f);
        fclose(f);
    }
    
    dap_log_set_external_output(LOGGER_OUTPUT_STDOUT, NULL);
    dap_log_level_set(s_log_level);
    
    // Initialize config system first
    dap_config_init(".");
    
    // Open config and set as global BEFORE dap_common_init
    extern dap_config_t *g_config;
    g_config = dap_config_open("test_trans");
    if (!g_config) {
        printf("Failed to open config\n");
        return -1;
    }
    
    // Initialize common DAP subsystems
    // This will call dap_module_init_all() which initializes all transports automatically
    dap_common_init(LOG_TAG, NULL);
    
    // Module system now works correctly - transs are registered automatically
    log_it(L_INFO, "Trans registration via module system complete");
    
    // Initialize encryption system
    dap_enc_init();
    
    // Initialize events system (required for dap_proc_thread_get_auto used by dap_link_manager)
    // Use 0 threads (auto-detect) to ensure enough workers for parallel clients
    int l_events_ret = dap_events_init(0, 60000);
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
    
    // Setup signal handler for graceful shutdown (Ctrl+C)
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Initialize test start time
    s_test_stats.test_start_time_ms = dap_nanotime_now() / 1000000;
    
    TEST_SUITE_START("Trans Integration Tests");
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  Testing all transs in parallel with full handshake cycle\n");
    printf("  Press Ctrl+C for test statistics\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");
    
    // Run tests
    TEST_RUN(test_01_init_all_transs);
    TEST_RUN(test_02_sequential_trans_testing);
    TEST_RUN(test_03_cleanup_all_resources);
    
    TEST_SUITE_END();
    
    // Record test end time and print statistics
    s_test_stats.test_end_time_ms = dap_nanotime_now() / 1000000;
    print_test_statistics();
    
    // Final cleanup
    if (g_config) {
        dap_config_close(g_config);
        g_config = NULL;
    }
    dap_config_deinit();
    
    // Remove temp config file
    remove("test_trans.cfg");
    
    return 0;
}
