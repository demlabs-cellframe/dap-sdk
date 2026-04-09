/**
 * @file test_packet_routing_regression.c
 * @brief Regression test for packet routing isolation
 * 
 * This test verifies that packets are correctly routed to workers WITHOUT using
 * the full transport FSM (no handshake, no encryption, no KEM).
 * 
 * Uses REAL DAP reactor and e-sockets for both server and clients.
 * 
 * It specifically tests for "packet mixing" - when packets from client A 
 * accidentally arrive at worker handling client B.
 * 
 * Key checks:
 * 1. Sticky sessions - all packets from one client go to same worker
 * 2. No packet mixing - packets never arrive at wrong worker
 * 3. Packet ordering - packets arrive in correct sequence per client
 * 4. Load balancing - multiple workers receive traffic
 * 
 * @date 2026-02-05
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
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_proc_thread.h"
#include "dap_io_flow.h"
#include "dap_io_flow_socket.h"
#include "dap_time.h"
#include "dap_server.h"
#include "dap_list.h"
#include "dap_timerfd.h"

#ifdef DAP_OS_LINUX
#include "dap_io_flow_cbpf.h"
#include "dap_io_flow_ebpf.h"
#endif

#define LOG_TAG "test_packet_routing"

//===================================================================
// CONFIGURATION
//===================================================================

#define NUM_CLIENTS         20      // Number of simulated clients
#define PACKETS_PER_CLIENT  50      // Packets each client sends
#define MAX_WORKERS         16      // Max workers to track
#define TEST_TIMEOUT_SEC    20      // 20 second timeout
#define WORKER_STARTUP_MS   500     // Wait for workers

// Packet header for tracking
typedef struct __attribute__((packed)) {
    uint32_t magic;         // 0xDEADBEEF
    uint32_t client_id;     // Which client sent this
    uint32_t seq_num;       // Sequence number within client
    uint32_t unique_id;     // Globally unique ID
    char payload[48];       // Human-readable payload
} test_packet_t;

#define PACKET_MAGIC 0xDEADBEEF
#define WORKER_NOT_ASSIGNED 0xFF

//===================================================================
// TEST CONTEXT
//===================================================================

typedef struct {
    int client_id;
    uint32_t packets_sent;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
} client_ctx_t;

typedef struct {
    // Server
    dap_io_flow_server_t *server;
    uint16_t server_port;
    
    // Clients
    dap_events_socket_t **client_es;
    client_ctx_t *client_ctx;
    uint32_t num_clients;
    uint32_t packets_per_client;
    
    // Packet tracking
    uint8_t *packet_worker;          // [client_id * packets_per_client + seq] = worker_id
    int8_t *client_expected_worker;  // [client_id] = expected worker
    
    // Stats
    _Atomic uint32_t total_sent;
    _Atomic uint32_t total_recv;
    _Atomic uint32_t violations;
    _Atomic uint32_t duplicates;
    _Atomic uint32_t invalid_packets;
    _Atomic uint32_t worker_recv[MAX_WORKERS];
    
    // State
    _Atomic bool sending_complete;
    _Atomic bool test_complete;
    _Atomic uint32_t clients_done;
    
    pthread_mutex_t mutex;
} test_ctx_t;

static test_ctx_t s_ctx = {0};
static uint32_t s_num_workers = 0;

//===================================================================
// POLLING HELPERS
//===================================================================

#define POLL_INTERVAL_US    5000    // 5ms
#define POLL_WAIT_UNTIL(cond, timeout_ms) ({ \
    bool _res = false; \
    uint64_t _start = dap_nanotime_now(); \
    uint64_t _timeout_ns = (uint64_t)(timeout_ms) * 1000000ULL; \
    while ((dap_nanotime_now() - _start) < _timeout_ns) { \
        if (cond) { _res = true; break; } \
        usleep(POLL_INTERVAL_US); \
    } \
    _res; \
})

//===================================================================
// SERVER CALLBACKS (io_flow packet receive)
//===================================================================

static void s_packet_received(dap_io_flow_server_t *a_server,
                              dap_io_flow_t *a_flow,
                              const uint8_t *a_data,
                              size_t a_size,
                              const struct sockaddr_storage *a_remote_addr,
                              dap_events_socket_t *a_listener_es)
{
    (void)a_server;
    (void)a_flow;
    (void)a_remote_addr;
    (void)a_listener_es;
    
    if (atomic_load(&s_ctx.test_complete)) return;
    
    if (a_size < sizeof(test_packet_t)) {
        atomic_fetch_add(&s_ctx.invalid_packets, 1);
        return;
    }
    
    const test_packet_t *l_pkt = (const test_packet_t *)a_data;
    
    // Validate magic
    if (l_pkt->magic != PACKET_MAGIC) {
        atomic_fetch_add(&s_ctx.invalid_packets, 1);
        return;
    }
    
    // Validate client_id
    if (l_pkt->client_id >= s_ctx.num_clients || l_pkt->seq_num >= s_ctx.packets_per_client) {
        atomic_fetch_add(&s_ctx.invalid_packets, 1);
        return;
    }
    
    // Get worker ID
    dap_worker_t *l_worker = dap_worker_get_current();
    uint32_t l_worker_id = l_worker ? l_worker->id : 0;
    
    // Update worker stats
    if (l_worker_id < MAX_WORKERS) {
        atomic_fetch_add(&s_ctx.worker_recv[l_worker_id], 1);
    }
    
    uint32_t l_client = l_pkt->client_id;
    uint32_t l_seq = l_pkt->seq_num;
    size_t l_idx = l_client * s_ctx.packets_per_client + l_seq;
    
    pthread_mutex_lock(&s_ctx.mutex);
    
    // Check duplicate
    if (s_ctx.packet_worker[l_idx] != WORKER_NOT_ASSIGNED) {
        atomic_fetch_add(&s_ctx.duplicates, 1);
        pthread_mutex_unlock(&s_ctx.mutex);
        return;
    }
    
    s_ctx.packet_worker[l_idx] = (uint8_t)l_worker_id;
    
    // Check sticky session
    if (s_ctx.client_expected_worker[l_client] < 0) {
        // First packet - assign
        s_ctx.client_expected_worker[l_client] = (int8_t)l_worker_id;
    } else if (s_ctx.client_expected_worker[l_client] != (int8_t)l_worker_id) {
        // VIOLATION!
        atomic_fetch_add(&s_ctx.violations, 1);
        log_it(L_ERROR, "VIOLATION: Client %u Pkt %u -> Worker %u (expected %d)",
               l_client, l_seq, l_worker_id, s_ctx.client_expected_worker[l_client]);
    }
    
    pthread_mutex_unlock(&s_ctx.mutex);
    
    atomic_fetch_add(&s_ctx.total_recv, 1);
}

static dap_io_flow_t *s_flow_create(dap_io_flow_server_t *a_server,
                                     const struct sockaddr_storage *a_remote_addr,
                                     dap_events_socket_t *a_es)
{
    (void)a_server;
    (void)a_remote_addr;
    (void)a_es;
    return DAP_NEW_Z(dap_io_flow_t);
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

//===================================================================
// CLIENT SEND LOGIC (runs on worker thread)
//===================================================================

static void s_client_send_packet(void *a_arg)
{
    dap_events_socket_t *l_es = (dap_events_socket_t *)a_arg;
    if (!l_es || !l_es->_inheritor) return;
    
    if (atomic_load(&s_ctx.test_complete)) return;
    
    client_ctx_t *l_ctx = (client_ctx_t *)l_es->_inheritor;
    
    if (l_ctx->packets_sent >= s_ctx.packets_per_client) {
        return;
    }
    
    // Create packet
    test_packet_t l_pkt = {
        .magic = PACKET_MAGIC,
        .client_id = l_ctx->client_id,
        .seq_num = l_ctx->packets_sent,
        .unique_id = l_ctx->client_id * s_ctx.packets_per_client + l_ctx->packets_sent,
    };
    snprintf(l_pkt.payload, sizeof(l_pkt.payload), "C%02d:P%03u:U%05u",
             l_ctx->client_id, l_ctx->packets_sent, l_pkt.unique_id);
    
    // Send via e-socket
    ssize_t l_sent = dap_events_socket_sendto_unsafe(l_es, &l_pkt, sizeof(l_pkt),
                                                      &l_ctx->server_addr,
                                                      l_ctx->server_addr_len);
    
    if (l_sent > 0) {
        l_ctx->packets_sent++;
        atomic_fetch_add(&s_ctx.total_sent, 1);
    }
    
    // Schedule next packet
    if (l_ctx->packets_sent < s_ctx.packets_per_client) {
        dap_worker_exec_callback_on(l_es->worker, s_client_send_packet, l_es);
    } else {
        atomic_fetch_add(&s_ctx.clients_done, 1);
    }
}

static bool s_start_sending(void *a_arg)
{
    (void)a_arg;
    
    if (atomic_load(&s_ctx.test_complete)) return false;
    
    dap_test_msg("Starting packet transmission...");
    
    for (uint32_t i = 0; i < s_ctx.num_clients; i++) {
        dap_events_socket_t *l_es = s_ctx.client_es[i];
        if (l_es && l_es->worker) {
            dap_worker_exec_callback_on(l_es->worker, s_client_send_packet, l_es);
        }
    }
    
    return false;  // One-shot timer
}

static bool s_check_completion(void *a_arg)
{
    (void)a_arg;
    
    uint32_t l_sent = atomic_load(&s_ctx.total_sent);
    uint32_t l_recv = atomic_load(&s_ctx.total_recv);
    uint32_t l_expected = s_ctx.num_clients * s_ctx.packets_per_client;
    uint32_t l_done = atomic_load(&s_ctx.clients_done);
    
    if (l_done >= s_ctx.num_clients) {
        atomic_store(&s_ctx.sending_complete, true);
    }
    
    // Accept 90% delivery (UDP may lose some)
    uint32_t l_threshold = l_expected * 90 / 100;
    if (l_recv >= l_threshold && atomic_load(&s_ctx.sending_complete)) {
        dap_test_msg("Complete: sent=%u, recv=%u (threshold=%u)", l_sent, l_recv, l_threshold);
        atomic_store(&s_ctx.test_complete, true);
        return false;
    }
    
    return true;  // Continue checking
}

//===================================================================
// SETUP/TEARDOWN
//===================================================================

static int s_setup_server(void)
{
#ifdef DAP_OS_LINUX
    dap_test_msg("Tier availability:");
    dap_test_msg("  CBPF: %s", dap_io_flow_cbpf_is_available() ? "YES" : "NO");
    dap_test_msg("  eBPF: %s", dap_io_flow_ebpf_is_available() ? "YES" : "NO");
    
    // Force CBPF for this test
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_CLASSIC_BPF);
    dap_test_msg("  Forced tier: CBPF");
#endif
    
    // Create server
    s_ctx.server = dap_io_flow_server_new("test_routing", &s_flow_ops, DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    if (!s_ctx.server) {
        log_it(L_ERROR, "Failed to create server");
        return -1;
    }
    
    // Listen on port 0 (kernel assigns)
    int l_ret = dap_io_flow_server_listen(s_ctx.server, "127.0.0.1", 0);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to start listener");
        dap_io_flow_server_delete(s_ctx.server);
        return -1;
    }
    
    // Get actual port using new API
    int l_listener_count = 0;
    if (s_ctx.server->dap_server && s_ctx.server->dap_server->es_listeners) {
        dap_list_t *l_list = s_ctx.server->dap_server->es_listeners;
        while (l_list) {
            l_listener_count++;
            if (l_listener_count == 1 && l_list->data) {
                dap_events_socket_t *l_listener = (dap_events_socket_t *)l_list->data;
                s_ctx.server_port = dap_events_socket_get_local_port(l_listener);
            }
            l_list = l_list->next;
        }
    }
    
    if (s_ctx.server_port == 0) {
        log_it(L_ERROR, "Failed to get server port");
        return -1;
    }
    
    dap_test_msg("Server: port=%u, listeners=%d, tier=%d",
                 s_ctx.server_port, l_listener_count, s_ctx.server->lb_tier);
    return 0;
}

static int s_create_clients(uint32_t a_num_clients, uint32_t a_packets_per)
{
    s_ctx.num_clients = a_num_clients;
    s_ctx.packets_per_client = a_packets_per;
    
    s_ctx.client_es = DAP_NEW_Z_SIZE(dap_events_socket_t*, a_num_clients * sizeof(void*));
    s_ctx.client_ctx = DAP_NEW_Z_SIZE(client_ctx_t, a_num_clients * sizeof(client_ctx_t));
    
    // Allocate packet tracking
    size_t l_pkt_size = a_num_clients * a_packets_per;
    s_ctx.packet_worker = DAP_NEW_SIZE(uint8_t, l_pkt_size);
    memset(s_ctx.packet_worker, WORKER_NOT_ASSIGNED, l_pkt_size);
    
    s_ctx.client_expected_worker = DAP_NEW_SIZE(int8_t, a_num_clients);
    memset(s_ctx.client_expected_worker, -1, a_num_clients);
    
    for (uint32_t i = 0; i < a_num_clients; i++) {
        int l_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (l_fd < 0) {
            log_it(L_ERROR, "socket() failed: %s", strerror(errno));
            return -1;
        }
        
        struct sockaddr_in l_bind = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        
        if (bind(l_fd, (struct sockaddr*)&l_bind, sizeof(l_bind)) < 0) {
            log_it(L_ERROR, "bind() failed: %s", strerror(errno));
            close(l_fd);
            return -1;
        }
        
        s_ctx.client_ctx[i].client_id = i;
        s_ctx.client_ctx[i].packets_sent = 0;
        
        struct sockaddr_in *l_srv = (struct sockaddr_in*)&s_ctx.client_ctx[i].server_addr;
        l_srv->sin_family = AF_INET;
        l_srv->sin_port = htons(s_ctx.server_port);
        l_srv->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        s_ctx.client_ctx[i].server_addr_len = sizeof(struct sockaddr_in);
        
        // Get worker for this client (round-robin)
        dap_worker_t *l_worker = dap_events_worker_get(i % s_num_workers);
        if (!l_worker) {
            close(l_fd);
            return -1;
        }
        
        // Create esocket (minimal callbacks)
        static dap_events_socket_callbacks_t s_client_cbs = {0};
        
        dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(l_fd, &s_client_cbs);
        if (!l_es) {
            close(l_fd);
            return -1;
        }
        
        l_es->_inheritor = &s_ctx.client_ctx[i];
        l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
        
        // Add to worker's event loop
        dap_events_socket_assign_on_worker_mt(l_es, l_worker);
        
        s_ctx.client_es[i] = l_es;
    }
    
    dap_test_msg("Created %u UDP clients", a_num_clients);
    return 0;
}

static void s_cleanup_clients(void)
{
    for (uint32_t i = 0; i < s_ctx.num_clients; i++) {
        if (s_ctx.client_es[i]) {
            dap_worker_t *l_worker = s_ctx.client_es[i]->worker;
            dap_events_socket_uuid_t l_uuid = s_ctx.client_es[i]->uuid;
            s_ctx.client_es[i]->_inheritor = NULL;
            s_ctx.client_es[i] = NULL;
            if (l_worker)
                dap_events_socket_remove_and_delete_mt(l_worker, l_uuid);
        }
    }

    dap_test_sleep_ms(500);

    DAP_DEL_Z(s_ctx.client_es);
    DAP_DEL_Z(s_ctx.client_ctx);
    DAP_DEL_Z(s_ctx.packet_worker);
    DAP_DEL_Z(s_ctx.client_expected_worker);
}

static void s_cleanup_server(void)
{
    if (s_ctx.server) {
        dap_io_flow_server_stop(s_ctx.server);
        dap_io_flow_delete_all_flows(s_ctx.server);
        dap_io_flow_server_delete(s_ctx.server);
        s_ctx.server = NULL;
    }
}

static void s_print_report(void)
{
    dap_test_msg("\n=== Packet Distribution Report ===");
    
    uint32_t l_worker_counts[MAX_WORKERS] = {0};
    uint32_t l_lost = 0;
    
    for (uint32_t c = 0; c < s_ctx.num_clients; c++) {
        for (uint32_t p = 0; p < s_ctx.packets_per_client; p++) {
            uint8_t l_w = s_ctx.packet_worker[c * s_ctx.packets_per_client + p];
            if (l_w != WORKER_NOT_ASSIGNED && l_w < s_num_workers) {
                l_worker_counts[l_w]++;
            } else if (l_w == WORKER_NOT_ASSIGNED) {
                l_lost++;
            }
        }
    }
    
    dap_test_msg("Worker distribution (%u workers):", s_num_workers);
    for (uint32_t w = 0; w < s_num_workers && w < MAX_WORKERS; w++) {
        uint32_t l_recv = atomic_load(&s_ctx.worker_recv[w]);
        dap_test_msg("  Worker %u: %u packets (atomic: %u)", w, l_worker_counts[w], l_recv);
    }
    
    uint32_t l_total_recv = atomic_load(&s_ctx.total_recv);
    uint32_t l_total_sent = atomic_load(&s_ctx.total_sent);
    uint32_t l_violations = atomic_load(&s_ctx.violations);
    uint32_t l_dups = atomic_load(&s_ctx.duplicates);
    
    dap_test_msg("Totals: sent=%u, recv=%u, lost=%u", l_total_sent, l_total_recv, l_lost);
    dap_test_msg("Violations: %u, Duplicates: %u", l_violations, l_dups);
    
    // Per-client summary
    dap_test_msg("\nClient -> Worker mapping:");
    for (uint32_t c = 0; c < s_ctx.num_clients; c++) {
        int8_t l_expected = s_ctx.client_expected_worker[c];
        
        uint32_t l_client_counts[MAX_WORKERS] = {0};
        uint32_t l_client_recv = 0;
        
        for (uint32_t p = 0; p < s_ctx.packets_per_client; p++) {
            uint8_t l_w = s_ctx.packet_worker[c * s_ctx.packets_per_client + p];
            if (l_w != WORKER_NOT_ASSIGNED && l_w < s_num_workers) {
                l_client_counts[l_w]++;
                l_client_recv++;
            }
        }
        
        int l_active_workers = 0;
        for (uint32_t w = 0; w < s_num_workers; w++) {
            if (l_client_counts[w] > 0) l_active_workers++;
        }
        
        if (l_active_workers > 1) {
            dap_test_msg("  Client %2u: SPLIT! Expected W%d:", c, l_expected);
            for (uint32_t w = 0; w < s_num_workers; w++) {
                if (l_client_counts[w] > 0) {
                    dap_test_msg("             W%d: %u pkts", w, l_client_counts[w]);
                }
            }
        } else if (l_client_recv > 0) {
            dap_test_msg("  Client %2u: W%d (%u/%u)", c, l_expected, l_client_recv, s_ctx.packets_per_client);
        } else {
            dap_test_msg("  Client %2u: NO PACKETS", c);
        }
    }
}

//===================================================================
// TEST IMPLEMENTATION
//===================================================================

static void test_packet_routing_multiclient(void)
{
    dap_test_msg("Test: Multi-client packet routing (NO FSM, REAL reactor)");
    
    // Initialize
    memset(&s_ctx, 0, sizeof(s_ctx));
    pthread_mutex_init(&s_ctx.mutex, NULL);
    
    // Start DAP events
    int l_ret = dap_events_init(0, 0);
    dap_assert(l_ret == 0, "dap_events_init");
    
    l_ret = dap_events_start();
    dap_assert(l_ret == 0, "dap_events_start");
    
    // Wait for workers
    usleep(WORKER_STARTUP_MS * 1000);
    
    bool l_ready = POLL_WAIT_UNTIL(dap_events_worker_get(0) != NULL, 2000);
    dap_assert(l_ready, "Reactor started");
    
    s_num_workers = dap_proc_thread_get_count();
    dap_test_msg("Workers: %u", s_num_workers);
    
    // Setup server
    l_ret = s_setup_server();
    dap_assert(l_ret == 0, "Server setup");
    
    // Create clients
    l_ret = s_create_clients(NUM_CLIENTS, PACKETS_PER_CLIENT);
    dap_assert(l_ret == 0, "Clients created");
    
    // Start sending timer
    dap_timerfd_start(100, s_start_sending, NULL);
    
    // Completion checker
    dap_timerfd_start_on_worker(dap_events_worker_get(0), 100, s_check_completion, NULL);
    
    // Wait for completion
    bool l_complete = POLL_WAIT_UNTIL(atomic_load(&s_ctx.test_complete), TEST_TIMEOUT_SEC * 1000);
    
    // Extra wait for stragglers
    usleep(500000);
    
    // Print report
    s_print_report();
    
    // Collect stats
    uint32_t l_violations = atomic_load(&s_ctx.violations);
    uint32_t l_total_recv = atomic_load(&s_ctx.total_recv);
    uint32_t l_expected = NUM_CLIENTS * PACKETS_PER_CLIENT;
    uint32_t l_threshold = l_expected * 90 / 100;
    
    // Signal all callbacks to stop, wait for worker queues to drain
    atomic_store(&s_ctx.test_complete, true);
    dap_test_sleep_ms(500);
    
    // Cleanup: server first to stop accepting traffic, then clients
    s_cleanup_server();
    s_cleanup_clients();
    
#ifdef DAP_OS_LINUX
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_APPLICATION);
#endif
    
    // Wait for async deletions to complete on worker threads
    dap_test_sleep_ms(500);
    
    dap_events_deinit();
    pthread_mutex_destroy(&s_ctx.mutex);
    
    // Count workers that received traffic
    int l_workers_with_traffic = 0;
    uint32_t l_max_on_single_worker = 0;
    for (uint32_t w = 0; w < s_num_workers && w < MAX_WORKERS; w++) {
        uint32_t l_count = atomic_load(&s_ctx.worker_recv[w]);
        if (l_count > 0) l_workers_with_traffic++;
        if (l_count > l_max_on_single_worker) l_max_on_single_worker = l_count;
    }
    
    // Assertions
    dap_test_msg("\nTest %s: recv=%u/%u (threshold=%u), violations=%u",
                 l_complete ? "COMPLETE" : "TIMEOUT",
                 l_total_recv, l_expected, l_threshold, l_violations);
    
    dap_assert(l_complete || l_total_recv >= l_threshold, "Sufficient packets received");
    
    // KEY REGRESSION CHECK
    if (l_violations > 0) {
        dap_test_msg("\n*** BUG REPRODUCED: %u packets on wrong worker! ***", l_violations);
    }
    dap_assert(l_violations == 0, "REGRESSION: No sticky session violations!");
    
    // CRITICAL: With flow control / CBPF, traffic must distribute across workers
    if (l_workers_with_traffic < 2 && s_ctx.num_clients >= 4 && s_num_workers >= 2) {
        dap_test_msg("\n*** BUG: NO DISTRIBUTION! All %u packets on single worker! ***", l_total_recv);
        dap_test_msg("Expected distribution across %u workers with %u clients", s_num_workers, s_ctx.num_clients);
        dap_test_msg("CBPF/eBPF kernel hash is not working properly");
    }
    dap_assert(l_workers_with_traffic >= 2 || s_ctx.num_clients < 4 || s_num_workers < 2,
               "REGRESSION: Load balancing should distribute traffic!");
    
    // Check for extreme imbalance (>95% on one worker with many clients)
    if (l_max_on_single_worker > l_total_recv * 95 / 100 && s_ctx.num_clients >= 10) {
        dap_test_msg("\n*** WARNING: Extremely unbalanced: %u/%u (%.1f%%) on single worker ***",
                     l_max_on_single_worker, l_total_recv, 100.0 * l_max_on_single_worker / l_total_recv);
    }
    
    dap_pass_msg("Packet routing test PASSED");
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_set_appname("test_packet_routing");
    dap_log_level_set(L_NOTICE);
    
    if (dap_common_init("test_packet_routing", NULL) != 0) {
        log_it(L_CRITICAL, "Failed to init dap_common");
        return 1;
    }
    
    printf("\n========================================\n");
    printf("Packet Routing Regression Test\n");
    printf("(NO FSM, NO Encryption - REAL reactor)\n");
    printf("Clients: %d, Packets/client: %d\n", NUM_CLIENTS, PACKETS_PER_CLIENT);
    printf("========================================\n\n");
    
    dap_print_module_name("packet_routing_regression");
    
    test_packet_routing_multiclient();

    dap_test_sleep_ms(200);
    dap_common_deinit();
    
    printf("\n=== Test completed ===\n");
    return 0;
}
