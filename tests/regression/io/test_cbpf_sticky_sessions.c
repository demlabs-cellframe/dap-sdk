/**
 * @file test_cbpf_sticky_sessions.c
 * @brief Regression test for CBPF sticky sessions
 * 
 * Tests that SO_REUSEPORT + CBPF correctly routes packets from the same
 * client (src_ip:src_port) to the same worker. Uses REAL DAP reactor and e-sockets.
 * 
 * Based on test_flow_tiers.c integration test.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_events.h"
#include "dap_events_socket.h"
#include "dap_worker.h"
#include "dap_timerfd.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_server.h"

#ifdef DAP_OS_LINUX
#include "dap_io_flow_cbpf.h"
#endif

#define LOG_TAG "test_cbpf_sticky"

#define NUM_WORKERS         10      // Match integration test workers
#define NUM_CLIENTS         100     // 100 clients like integration test
#define PACKETS_PER_CLIENT  100     // More packets per client
#define PACKET_SIZE         1024    // Larger packets
#define TEST_TIMEOUT_SEC    60

// Extended tracking for debugging
static _Atomic uint64_t s_total_data_bytes = 0;
static _Atomic uint32_t s_acks_sent = 0;
static _Atomic uint32_t s_acks_received = 0;
static _Atomic uint32_t s_session_mismatches = 0;  // Packet arrived at wrong worker!

// Packet header for tracking (simulates real UDP packet with session)
typedef struct {
    uint32_t magic;
    uint32_t client_id;
    uint32_t packet_num;
    uint64_t session_id;        // Simulated session ID
    uint32_t expected_worker;   // Worker where session was created
    uint32_t is_ack;            // 1 = ACK packet, 0 = DATA packet
    uint64_t seq_num;           // Sequence number for flow control
    uint64_t ack_seq;           // ACK sequence number
    char payload[PACKET_SIZE - 48];
} test_packet_t;

#define PACKET_MAGIC 0xCAFEBABE
#define PKT_TYPE_DATA 0
#define PKT_TYPE_ACK  1

// Per-worker statistics
typedef struct {
    _Atomic uint32_t packets_received;
    _Atomic uint32_t clients_seen[NUM_CLIENTS];
} worker_stats_t;

// Client context (simulates real client with session)
typedef struct {
    int client_id;
    uint32_t packets_sent;
    uint64_t session_id;            // Unique session ID
    int32_t assigned_worker;        // Worker ID assigned during "handshake" (-1 = not assigned)
    uint64_t seq_num;               // Next sequence to send
    uint64_t ack_received;          // Highest ACK received
    _Atomic uint32_t packets_acked; // Packets acknowledged
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
} client_ctx_t;

// Test context
typedef struct {
    dap_io_flow_server_t *server;
    uint16_t server_port;
    worker_stats_t worker_stats[NUM_WORKERS];
    _Atomic uint32_t total_received;
    _Atomic uint32_t total_sent;
    _Atomic uint32_t flows_created;
    
    dap_events_socket_t *client_es[NUM_CLIENTS];
    client_ctx_t client_ctx[NUM_CLIENTS];
    
    // Track which worker first saw each client (for sticky session validation)
    _Atomic int32_t first_seen_worker[NUM_CLIENTS];  // -1 = not seen yet
    
    _Atomic bool test_complete;
} test_context_t;

static test_context_t s_ctx = {0};

// ============================================================================
// Flow Server Callbacks
// ============================================================================

static void s_packet_received(dap_io_flow_server_t *a_server,
                              dap_io_flow_t *a_flow,
                              const uint8_t *a_data,
                              size_t a_size,
                              const struct sockaddr_storage *a_remote_addr,
                              dap_events_socket_t *a_listener_es)
{
    (void)a_server; (void)a_flow; (void)a_listener_es; (void)a_remote_addr;
    
    if (a_size < sizeof(test_packet_t)) return;
    
    const test_packet_t *l_pkt = (const test_packet_t*)a_data;
    if (l_pkt->magic != PACKET_MAGIC) return;
    if (l_pkt->client_id >= NUM_CLIENTS) return;
    
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker || l_worker->id >= (uint32_t)NUM_WORKERS) return;
    
    int32_t l_worker_id = (int32_t)l_worker->id;
    uint32_t l_client_id = l_pkt->client_id;
    
    // Track data bytes
    atomic_fetch_add(&s_total_data_bytes, a_size);
    
    // === STICKY SESSION CHECK ===
    // First packet from client: record which worker saw it
    // Subsequent packets: verify they arrive at the same worker
    int32_t l_expected = -1;
    if (atomic_compare_exchange_strong(&s_ctx.first_seen_worker[l_client_id], &l_expected, l_worker_id)) {
        // First packet from this client - recorded worker
        log_it(L_DEBUG, "Client %u: first packet on worker %d", l_client_id, l_worker_id);
    } else {
        // Not first packet - check if same worker
        int32_t l_first_worker = atomic_load(&s_ctx.first_seen_worker[l_client_id]);
        if (l_first_worker != l_worker_id) {
            atomic_fetch_add(&s_session_mismatches, 1);
            uint32_t mismatches = atomic_load(&s_session_mismatches);
            if (mismatches <= 20) {
                log_it(L_ERROR, "STICKY VIOLATION! Client %u pkt %u: first seen on worker %d, now on worker %d",
                       l_client_id, l_pkt->packet_num, l_first_worker, l_worker_id);
            }
        }
    }
    
    atomic_fetch_add(&s_ctx.worker_stats[l_worker_id].packets_received, 1);
    atomic_fetch_add(&s_ctx.worker_stats[l_worker_id].clients_seen[l_client_id], 1);
    atomic_fetch_add(&s_ctx.total_received, 1);
    
    // Progress logging
    uint32_t total = atomic_load(&s_ctx.total_received);
    if (total % 2000 == 0) {
        uint32_t mismatches = atomic_load(&s_session_mismatches);
        log_it(L_NOTICE, "Progress: %u/%u packets, %u sticky violations", 
               total, NUM_CLIENTS * PACKETS_PER_CLIENT, mismatches);
    }
}

static dap_io_flow_t* s_flow_create(dap_io_flow_server_t *a_server,
                                     const struct sockaddr_storage *a_remote_addr,
                                     dap_events_socket_t *a_listener_es)
{
    (void)a_server; (void)a_remote_addr; (void)a_listener_es;
    
    dap_io_flow_t *l_flow = DAP_NEW_Z(dap_io_flow_t);
    if (l_flow) {
        atomic_fetch_add(&s_ctx.flows_created, 1);
    }
    return l_flow;
}

static void s_flow_destroy(dap_io_flow_t *a_flow)
{
    DAP_DELETE(a_flow);
}

static dap_io_flow_ops_t s_flow_ops = {
    .packet_received = s_packet_received,
    .flow_create = s_flow_create,
    .flow_destroy = s_flow_destroy,
};

// ============================================================================
// Client Send (via worker callback)
// ============================================================================

static void s_client_send_packet_cb(void *a_arg)
{
    dap_events_socket_t *l_es = (dap_events_socket_t*)a_arg;
    if (!l_es || !l_es->_inheritor) return;
    
    if (atomic_load(&s_ctx.test_complete)) return;
    
    client_ctx_t *l_ctx = (client_ctx_t*)l_es->_inheritor;
    
    if (l_ctx->packets_sent >= PACKETS_PER_CLIENT) return;
    
    // Build packet simulating real UDP with session
    test_packet_t l_pkt = {
        .magic = PACKET_MAGIC,
        .client_id = l_ctx->client_id,
        .packet_num = l_ctx->packets_sent,
        .session_id = l_ctx->session_id,
        .expected_worker = 0,  // Not used - server tracks this
        .is_ack = PKT_TYPE_DATA,
        .seq_num = l_ctx->seq_num++,
        .ack_seq = 0,
    };
    
    // Fill payload with pattern
    for (size_t i = 0; i < sizeof(l_pkt.payload); i++) {
        l_pkt.payload[i] = (uint8_t)((l_ctx->client_id + l_ctx->packets_sent + i) & 0xFF);
    }
    
    size_t l_sent = dap_events_socket_sendto_unsafe(l_es, &l_pkt, sizeof(l_pkt),
                                                    &l_ctx->server_addr, l_ctx->server_addr_len);
    if (l_sent > 0) {
        l_ctx->packets_sent++;
        atomic_fetch_add(&s_ctx.total_sent, 1);
    }
    
    // Send multiple packets per callback for higher throughput
    if (l_ctx->packets_sent < PACKETS_PER_CLIENT) {
        // Small delay between packets to avoid overwhelming
        dap_worker_exec_callback_on(l_es->worker, s_client_send_packet_cb, l_es);
    }
}

static bool s_start_sending_cb(void *a_arg)
{
    (void)a_arg;
    
    if (atomic_load(&s_ctx.test_complete)) return false;
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        dap_events_socket_t *l_es = s_ctx.client_es[i];
        if (l_es && l_es->worker) {
            dap_worker_exec_callback_on(l_es->worker, s_client_send_packet_cb, l_es);
        }
    }
    return false;  // One-shot
}

static bool s_check_completion_cb(void *a_arg)
{
    (void)a_arg;
    
    uint32_t l_expected = NUM_CLIENTS * PACKETS_PER_CLIENT;
    uint32_t l_received = atomic_load(&s_ctx.total_received);
    
    if (l_received >= l_expected * 95 / 100) {
        atomic_store(&s_ctx.test_complete, true);
        return false;  // Stop timer
    }
    return true;  // Continue checking
}

// ============================================================================
// Test Setup/Cleanup
// ============================================================================

static int s_setup_test(void)
{
    memset(&s_ctx, 0, sizeof(s_ctx));
    
    // Reset global counters
    atomic_store(&s_total_data_bytes, 0);
    atomic_store(&s_acks_sent, 0);
    atomic_store(&s_acks_received, 0);
    atomic_store(&s_session_mismatches, 0);
    
    // Initialize first_seen_worker to -1 (not seen)
    for (int i = 0; i < NUM_CLIENTS; i++) {
        atomic_store(&s_ctx.first_seen_worker[i], -1);
    }
    
#ifdef DAP_OS_LINUX
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_CLASSIC_BPF);
    
    if (!dap_io_flow_cbpf_is_available()) {
        dap_test_msg("CBPF not available, skipping");
        return 1;
    }
#else
    dap_test_msg("CBPF only on Linux");
    return 1;
#endif
    
    if (dap_events_init(NUM_WORKERS, 0) != 0) return -1;
    if (dap_events_start() != 0) return -1;
    
    // Create server
    s_ctx.server = dap_io_flow_server_new("cbpf_test", &s_flow_ops, 
                                           DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    if (!s_ctx.server) return -1;
    
    if (dap_io_flow_server_listen(s_ctx.server, "127.0.0.1", 0) != 0) return -1;
    
    // Get actual port from listener socket (use DAP SDK field)
    if (s_ctx.server->dap_server && s_ctx.server->dap_server->es_listeners) {
        dap_events_socket_t *l_listener = (dap_events_socket_t*)s_ctx.server->dap_server->es_listeners->data;
        if (l_listener)
            s_ctx.server_port = l_listener->listener_port;
    }
    if (s_ctx.server_port == 0) return -1;
    
    dap_test_msg("Server on port %u, tier=%d", s_ctx.server_port, s_ctx.server->lb_tier);
    
    // Create client e-sockets
    struct sockaddr_in l_server_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(s_ctx.server_port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
    };
    
    dap_events_socket_callbacks_t l_callbacks = {0};
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        s_ctx.client_ctx[i].client_id = i;
        s_ctx.client_ctx[i].packets_sent = 0;
        s_ctx.client_ctx[i].session_id = 0x1000000000000000ULL + i;  // Unique session ID
        s_ctx.client_ctx[i].assigned_worker = -1;  // Not assigned yet (will be set by first packet's response)
        s_ctx.client_ctx[i].seq_num = 1;
        s_ctx.client_ctx[i].ack_received = 0;
        atomic_store(&s_ctx.client_ctx[i].packets_acked, 0);
        memcpy(&s_ctx.client_ctx[i].server_addr, &l_server_addr, sizeof(l_server_addr));
        s_ctx.client_ctx[i].server_addr_len = sizeof(l_server_addr);
        
        // Create UDP socket via platform API
        s_ctx.client_es[i] = dap_events_socket_create_platform(AF_INET, SOCK_DGRAM, 0, &l_callbacks);
        if (!s_ctx.client_es[i]) return -1;
        
        s_ctx.client_es[i]->_inheritor = &s_ctx.client_ctx[i];
        
        // Assign to worker (spread across workers)
        dap_worker_t *l_worker = dap_events_worker_get(i % NUM_WORKERS);
        if (!l_worker) return -1;
        
        dap_events_socket_assign_on_worker_mt(s_ctx.client_es[i], l_worker);
    }
    
    dap_test_msg("Created %d clients", NUM_CLIENTS);
    return 0;
}

static void s_cleanup_test(void)
{
    atomic_store(&s_ctx.test_complete, true);
    dap_test_sleep_ms(500);

    if (s_ctx.server) {
        dap_io_flow_server_stop(s_ctx.server);
        dap_io_flow_delete_all_flows(s_ctx.server);
    }

    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_ctx.client_es[i]) {
            dap_worker_t *l_worker = s_ctx.client_es[i]->worker;
            dap_events_socket_uuid_t l_uuid = s_ctx.client_es[i]->uuid;
            s_ctx.client_es[i]->_inheritor = NULL;
            s_ctx.client_es[i] = NULL;
            if (l_worker)
                dap_events_socket_remove_and_delete_mt(l_worker, l_uuid);
        }
    }

    // Wait for async deletions to complete on worker threads
    dap_test_sleep_ms(500);

    if (s_ctx.server) {
        dap_io_flow_server_delete(s_ctx.server);
        s_ctx.server = NULL;
    }
    
    dap_events_deinit();
}

static int s_verify_results(void)
{
    uint32_t l_received = atomic_load(&s_ctx.total_received);
    uint32_t l_expected = NUM_CLIENTS * PACKETS_PER_CLIENT;
    uint32_t l_mismatches = atomic_load(&s_session_mismatches);
    uint64_t l_data_bytes = atomic_load(&s_total_data_bytes);
    
    printf("\n╔══════════════════════════════════════════════════════╗\n");
    printf("║          CBPF STRESS TEST RESULTS                    ║\n");
    printf("╠══════════════════════════════════════════════════════╣\n");
    printf("║ Clients:               %10d                   ║\n", NUM_CLIENTS);
    printf("║ Packets per client:    %10d                   ║\n", PACKETS_PER_CLIENT);
    printf("║ Packet size:           %10d bytes             ║\n", PACKET_SIZE);
    printf("║ Total expected:        %10u packets           ║\n", l_expected);
    printf("║ Total received:        %10u packets (%.1f%%)   ║\n", l_received, 100.0 * l_received / l_expected);
    printf("║ Data transferred:      %10.2f MB               ║\n", l_data_bytes / (1024.0 * 1024.0));
    printf("║ Session mismatches:    %10u                   ║\n", l_mismatches);
    printf("╚══════════════════════════════════════════════════════╝\n\n");
    
    // Check sticky sessions - how many clients are split across workers
    int l_violations = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        int l_workers_with_packets = 0;
        for (int w = 0; w < NUM_WORKERS; w++) {
            if (atomic_load(&s_ctx.worker_stats[w].clients_seen[c]) > 0) {
                l_workers_with_packets++;
            }
        }
        if (l_workers_with_packets > 1) {
            if (l_violations < 10) {  // Only log first 10
                dap_test_msg("Client %d: SPLIT across %d workers!", c, l_workers_with_packets);
            }
            l_violations++;
        }
    }
    
    if (l_violations > 10) {
        dap_test_msg("... and %d more clients with violations", l_violations - 10);
    }
    
    // Print distribution
    dap_test_msg("Worker distribution:");
    int l_workers_with_traffic = 0;
    uint32_t l_max_on_single_worker = 0;
    for (int w = 0; w < NUM_WORKERS; w++) {
        uint32_t l_count = atomic_load(&s_ctx.worker_stats[w].packets_received);
        dap_test_msg("  Worker %d: %u packets (%.1f%%)", w, l_count, 
                     100.0 * l_count / (l_received > 0 ? l_received : 1));
        if (l_count > 0) {
            l_workers_with_traffic++;
        }
        if (l_count > l_max_on_single_worker) {
            l_max_on_single_worker = l_count;
        }
    }
    
    // SESSION MISMATCH IS THE KEY BUG INDICATOR
    if (l_mismatches > 0) {
        printf("\n*** CRITICAL BUG: %u SESSION MISMATCHES! ***\n", l_mismatches);
        printf("Packets routed to wrong worker after session established.\n");
        printf("This causes 'Invalid handshake size' errors in real system.\n\n");
        return -4;
    }
    
    if (l_violations > 0) {
        dap_test_msg("STICKY SESSIONS VIOLATED: %d clients affected", l_violations);
        return -1;
    }
    
    if (l_received < l_expected * 90 / 100) {
        dap_test_msg("Too many packets lost: %u/%u (%.1f%%)", l_received, l_expected,
                     100.0 * l_received / l_expected);
        return -1;
    }
    
    // Check distribution — with flow control / CBPF, traffic must distribute across workers
    if (l_workers_with_traffic < 2 && NUM_CLIENTS >= 4) {
        dap_test_msg("BUG: NO DISTRIBUTION! All %u packets on single worker", l_received);
        return -2;
    }
    
    if (l_max_on_single_worker > l_received * 90 / 100 && NUM_CLIENTS >= 4) {
        dap_test_msg("WARNING: Extremely unbalanced: %.1f%% on single worker",
                     100.0 * l_max_on_single_worker / l_received);
        return -3;
    }
    
    dap_test_msg("Distribution OK: %d workers received traffic", l_workers_with_traffic);
    return 0;
}

// ============================================================================
// Main Test
// ============================================================================

static void test_cbpf_sticky_sessions(void)
{
    dap_test_msg("=== CBPF Sticky Sessions Regression Test ===");
    
    int ret = s_setup_test();
    if (ret > 0) {
        dap_pass_msg("Test skipped (CBPF not available)");
        return;
    }
    dap_assert(ret == 0, "Setup should succeed");
    
    // Start sending via timer
    dap_worker_t *l_worker = dap_events_worker_get(0);
    dap_assert(l_worker != NULL, "Worker 0 exists");
    
    dap_timerfd_start_on_worker(l_worker, 100, s_start_sending_cb, NULL);
    dap_timerfd_start_on_worker(l_worker, 100, s_check_completion_cb, NULL);
    
    // Wait for completion
    for (int i = 0; i < TEST_TIMEOUT_SEC * 10; i++) {
        if (atomic_load(&s_ctx.test_complete)) break;
        usleep(100000);
    }
    
    ret = s_verify_results();
    s_cleanup_test();
    
    dap_assert(ret == 0, "CBPF sticky sessions should work");
    dap_pass_msg("CBPF sticky sessions test passed");
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    
    // Initialize DAP common for proper logging
    dap_common_init("test_cbpf", NULL);
    dap_log_level_set(L_NOTICE);  // Use L_DEBUG for verbose output
    
    printf("\n");
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║   CBPF STICKY SESSIONS STRESS TEST                         ║\n");
    printf("║   Testing: %d clients x %d packets = %d total              ║\n", 
           NUM_CLIENTS, PACKETS_PER_CLIENT, NUM_CLIENTS * PACKETS_PER_CLIENT);
    printf("║   Packet size: %d bytes, Total data: %.2f MB               ║\n",
           PACKET_SIZE, (double)(NUM_CLIENTS * PACKETS_PER_CLIENT * PACKET_SIZE) / (1024.0 * 1024.0));
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    test_cbpf_sticky_sessions();
    
    dap_common_deinit();
    return 0;
}
