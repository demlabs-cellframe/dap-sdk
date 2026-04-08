/**
 * @file test_trans_udp_multiclient.c
 * @brief Multi-client regression test for UDP transport with session tracking
 * 
 * This test simulates the UDP transport layer's behavior:
 * - Real io_flow_server on UDP
 * - Session-based packet routing (using session_id in header)
 * - Validates that packets from same session go to same worker
 * - Tests scenarios that failed in test_trans_integration
 * 
 * NO ENCRYPTION - plaintext headers for clear tracking.
 * 
 * @date 2026-02-03
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_proc_thread.h"
#include "dap_io_flow.h"
#include "dap_list.h"
#include "dap_server.h"

#ifdef DAP_OS_LINUX
#include "dap_io_flow_cbpf.h"
#include "dap_io_flow_ebpf.h"
#include "dap_io_flow_socket.h"
#endif

#define LOG_TAG "test_trans_udp_multiclient"

//===================================================================
// CONFIGURATION
//===================================================================

#define TEST_TIMEOUT_SEC        60
#define POLL_INTERVAL_US        5000
#define WORKER_STARTUP_MS       500

#define PACKET_MAGIC            0xFEEDFACE
#define SESSION_NOT_ASSIGNED    0xFFFFFFFFFFFFFFFFULL
#define WORKER_NOT_ASSIGNED     0xFF

//===================================================================
// SESSION PACKET HEADER (simulates UDP transport header)
//===================================================================

typedef struct __attribute__((packed)) {
    uint32_t magic;           // PACKET_MAGIC
    uint64_t session_id;      // Session identifier (like in real UDP transport)
    uint32_t seq_num;         // Sequence within session
    uint32_t client_id;       // For tracking (not in real protocol)
    char payload[48];         // Data
} session_packet_t;

//===================================================================
// SESSION TRACKER (per-session, per-worker tracking)
//===================================================================

typedef struct {
    uint64_t session_id;
    int8_t assigned_worker;   // -1 = not yet
    _Atomic uint32_t packets_recv;
    _Atomic uint32_t worker_violations;
} session_state_t;

typedef struct {
    session_state_t *sessions;
    uint32_t num_sessions;
    
    _Atomic uint32_t total_sent;
    _Atomic uint32_t total_recv;
    _Atomic uint32_t total_violations;
    
    // Per-worker stats
    _Atomic uint32_t *worker_packets;
    uint32_t num_workers;
    
    pthread_mutex_t lock;
} session_tracker_t;

static session_tracker_t s_tracker = {0};

static void s_tracker_init(uint32_t num_sessions, uint32_t num_workers)
{
    s_tracker.num_sessions = num_sessions;
    s_tracker.num_workers = num_workers;
    
    s_tracker.sessions = DAP_NEW_Z_SIZE(session_state_t, num_sessions * sizeof(session_state_t));
    for (uint32_t i = 0; i < num_sessions; i++) {
        s_tracker.sessions[i].session_id = ((uint64_t)i << 32) | (rand() & 0xFFFFFFFF);
        s_tracker.sessions[i].assigned_worker = -1;
        atomic_init(&s_tracker.sessions[i].packets_recv, 0);
        atomic_init(&s_tracker.sessions[i].worker_violations, 0);
    }
    
    s_tracker.worker_packets = DAP_NEW_Z_SIZE(_Atomic uint32_t, num_workers * sizeof(_Atomic uint32_t));
    for (uint32_t w = 0; w < num_workers; w++) {
        atomic_init(&s_tracker.worker_packets[w], 0);
    }
    
    atomic_init(&s_tracker.total_sent, 0);
    atomic_init(&s_tracker.total_recv, 0);
    atomic_init(&s_tracker.total_violations, 0);
    pthread_mutex_init(&s_tracker.lock, NULL);
}

static void s_tracker_deinit(void)
{
    DAP_DEL_Z(s_tracker.sessions);
    DAP_DEL_Z(s_tracker.worker_packets);
    pthread_mutex_destroy(&s_tracker.lock);
}

static void s_tracker_record_recv(uint32_t client_id, uint32_t worker_id)
{
    if (client_id >= s_tracker.num_sessions || worker_id >= s_tracker.num_workers) {
        return;
    }
    
    session_state_t *sess = &s_tracker.sessions[client_id];
    
    pthread_mutex_lock(&s_tracker.lock);
    
    if (sess->assigned_worker < 0) {
        sess->assigned_worker = (int8_t)worker_id;
    } else if (sess->assigned_worker != (int8_t)worker_id) {
        atomic_fetch_add(&sess->worker_violations, 1);
        atomic_fetch_add(&s_tracker.total_violations, 1);
        log_it(L_ERROR, "STICKY VIOLATION: Session %u (0x%" PRIx64 ") -> W%u (expected W%d)",
               client_id, sess->session_id, worker_id, sess->assigned_worker);
    }
    
    pthread_mutex_unlock(&s_tracker.lock);
    
    atomic_fetch_add(&sess->packets_recv, 1);
    atomic_fetch_add(&s_tracker.total_recv, 1);
    atomic_fetch_add(&s_tracker.worker_packets[worker_id], 1);
}

static void s_tracker_print_report(void)
{
    dap_test_msg("\n=== Session Routing Report ===");
    
    uint32_t sent = atomic_load(&s_tracker.total_sent);
    uint32_t recv = atomic_load(&s_tracker.total_recv);
    uint32_t viol = atomic_load(&s_tracker.total_violations);
    
    dap_test_msg("Packets: sent=%u, received=%u", sent, recv);
    dap_test_msg("Sticky violations: %u", viol);
    
    dap_test_msg("\nWorker distribution:");
    for (uint32_t w = 0; w < s_tracker.num_workers; w++) {
        uint32_t cnt = atomic_load(&s_tracker.worker_packets[w]);
        if (cnt > 0) {
            dap_test_msg("  Worker %u: %u packets", w, cnt);
        }
    }
    
    dap_test_msg("\nSession -> Worker mapping:");
    for (uint32_t c = 0; c < s_tracker.num_sessions; c++) {
        session_state_t *sess = &s_tracker.sessions[c];
        uint32_t r = atomic_load(&sess->packets_recv);
        uint32_t v = atomic_load(&sess->worker_violations);
        
        if (r > 0) {
            dap_test_msg("  Session %2u (0x%08" PRIx64 "): W%d, recv=%u%s",
                         c, sess->session_id, sess->assigned_worker, r,
                         v > 0 ? " [VIOLATIONS!]" : "");
        } else {
            dap_test_msg("  Session %2u (0x%08" PRIx64 "): NO DATA", c, sess->session_id);
        }
    }
}

//===================================================================
// TEST CONTEXT
//===================================================================

typedef struct {
    int client_id;
    int socket_fd;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
    uint64_t session_id;
    uint32_t seq_num;
    uint32_t packets_to_send;
} test_client_t;

typedef struct {
    dap_io_flow_server_t *flow_server;
    uint16_t server_port;
    
    test_client_t *clients;
    uint32_t num_clients;
    uint32_t packets_per_client;
    
    _Atomic bool sending_done;
    _Atomic bool test_done;
    _Atomic uint32_t clients_done;
} test_ctx_t;

static test_ctx_t s_ctx = {0};
static uint32_t s_num_workers = 0;

//===================================================================
// SERVER CALLBACKS
//===================================================================

static _Atomic uint32_t s_raw_recv_count = 0;
static _Atomic uint32_t s_invalid_count = 0;

static void s_server_packet_received(dap_io_flow_server_t *a_server,
                                      dap_io_flow_t *a_flow,
                                      const uint8_t *a_data,
                                      size_t a_size,
                                      const struct sockaddr_storage *a_addr,
                                      dap_events_socket_t *a_es)
{
    (void)a_server;
    (void)a_flow;
    (void)a_addr;
    (void)a_es;
    
    atomic_fetch_add(&s_raw_recv_count, 1);
    
    if (a_size < sizeof(session_packet_t)) {
        atomic_fetch_add(&s_invalid_count, 1);
        return;
    }
    
    session_packet_t *pkt = (session_packet_t*)a_data;
    
    if (pkt->magic != PACKET_MAGIC) {
        atomic_fetch_add(&s_invalid_count, 1);
        return;
    }
    
    dap_worker_t *worker = dap_worker_get_current();
    uint32_t worker_id = worker ? worker->id : 0;
    
    // Debug first few
    static _Atomic uint32_t s_logged = 0;
    if (atomic_fetch_add(&s_logged, 1) < 5) {
        printf("PKT: session=0x%" PRIx64 ", client=%u, seq=%u, worker=%u\n",
               pkt->session_id, pkt->client_id, pkt->seq_num, worker_id);
    }
    
    s_tracker_record_recv(pkt->client_id, worker_id);
}

static dap_io_flow_t* s_server_flow_create(dap_io_flow_server_t *a_server,
                                            const struct sockaddr_storage *a_addr,
                                            dap_events_socket_t *a_es)
{
    (void)a_server;
    (void)a_addr;
    (void)a_es;
    return DAP_NEW_Z(dap_io_flow_t);
}

static void s_server_flow_destroy(dap_io_flow_t *a_flow)
{
    DAP_DELETE(a_flow);
}

static dap_io_flow_ops_t s_server_ops = {
    .packet_received = s_server_packet_received,
    .flow_create = s_server_flow_create,
    .flow_destroy = s_server_flow_destroy,
};

//===================================================================
// CLIENT SEND
//===================================================================

static int s_client_send_packet(test_client_t *client)
{
    session_packet_t pkt = {
        .magic = PACKET_MAGIC,
        .session_id = client->session_id,
        .seq_num = client->seq_num++,
        .client_id = client->client_id,
    };
    snprintf(pkt.payload, sizeof(pkt.payload), "S%02d:P%03d", 
             client->client_id, pkt.seq_num);
    
    ssize_t sent = sendto(client->socket_fd, &pkt, sizeof(pkt), 0,
                          (struct sockaddr*)&client->server_addr,
                          client->server_addr_len);
    
    if (sent > 0) {
        atomic_fetch_add(&s_tracker.total_sent, 1);
        return 0;
    }
    return -1;
}

//===================================================================
// TEST SETUP
//===================================================================

static int s_setup_server(void)
{
#ifdef DAP_OS_LINUX
    dap_test_msg("Tier availability:");
    dap_test_msg("  CBPF: %s", dap_io_flow_cbpf_is_available() ? "YES" : "NO");
    dap_test_msg("  eBPF: %s", dap_io_flow_ebpf_is_available() ? "YES" : "NO");
    
    // Force Application tier for testing
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_APPLICATION);
    dap_test_msg("  Forced: APPLICATION");
#endif
    
    s_ctx.flow_server = dap_io_flow_server_new("test_trans_server", &s_server_ops,
                                                DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    if (!s_ctx.flow_server) {
        log_it(L_ERROR, "Failed to create flow server");
        return -1;
    }
    
    int ret = dap_io_flow_server_listen(s_ctx.flow_server, "127.0.0.1", 0);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to start server");
        dap_io_flow_server_delete(s_ctx.flow_server);
        return -1;
    }
    
    // Get port
    int listener_count = 0;
    if (s_ctx.flow_server->dap_server && s_ctx.flow_server->dap_server->es_listeners) {
        dap_list_t *l_list = s_ctx.flow_server->dap_server->es_listeners;
        while (l_list) {
            listener_count++;
            if (listener_count == 1 && l_list->data) {
                dap_events_socket_t *es = (dap_events_socket_t*)l_list->data;
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);
                if (getsockname(es->fd, (struct sockaddr*)&addr, &len) == 0) {
                    s_ctx.server_port = ntohs(addr.sin_port);
                }
            }
            l_list = l_list->next;
        }
    }
    
    if (s_ctx.server_port == 0) {
        log_it(L_ERROR, "Failed to get server port");
        return -1;
    }
    
    const char *tier_name = "UNKNOWN";
    switch (s_ctx.flow_server->lb_tier) {
        case DAP_IO_FLOW_LB_TIER_NONE: tier_name = "NONE"; break;
        case DAP_IO_FLOW_LB_TIER_APPLICATION: tier_name = "APPLICATION"; break;
#ifdef DAP_OS_LINUX
        case DAP_IO_FLOW_LB_TIER_CLASSIC_BPF: tier_name = "CBPF"; break;
        case DAP_IO_FLOW_LB_TIER_EBPF: tier_name = "eBPF"; break;
#endif
        default: break;
    }
    
    dap_test_msg("Server: port=%u, listeners=%d, tier=%s",
                 s_ctx.server_port, listener_count, tier_name);
    return 0;
}

static void s_cleanup_server(void)
{
    if (s_ctx.flow_server) {
        dap_io_flow_server_stop(s_ctx.flow_server);
        dap_io_flow_delete_all_flows(s_ctx.flow_server);
        dap_io_flow_server_delete(s_ctx.flow_server);
        s_ctx.flow_server = NULL;
    }
}

static int s_setup_clients(uint32_t num_clients, uint32_t packets_per)
{
    s_ctx.num_clients = num_clients;
    s_ctx.packets_per_client = packets_per;
    s_ctx.clients = DAP_NEW_Z_SIZE(test_client_t, num_clients * sizeof(test_client_t));
    
    for (uint32_t i = 0; i < num_clients; i++) {
        s_ctx.clients[i].client_id = i;
        s_ctx.clients[i].session_id = s_tracker.sessions[i].session_id;
        s_ctx.clients[i].seq_num = 1;
        s_ctx.clients[i].packets_to_send = packets_per;
        
        s_ctx.clients[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_ctx.clients[i].socket_fd < 0) {
            log_it(L_ERROR, "socket() failed for client %d", i);
            return -1;
        }
        
        struct sockaddr_in bind_addr = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        if (bind(s_ctx.clients[i].socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            log_it(L_ERROR, "bind() failed for client %d", i);
            return -1;
        }
        
        struct sockaddr_in *srv = (struct sockaddr_in*)&s_ctx.clients[i].server_addr;
        srv->sin_family = AF_INET;
        srv->sin_port = htons(s_ctx.server_port);
        srv->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        s_ctx.clients[i].server_addr_len = sizeof(struct sockaddr_in);
    }
    
    dap_test_msg("Created %u clients with sessions", num_clients);
    return 0;
}

static void s_cleanup_clients(void)
{
    for (uint32_t i = 0; i < s_ctx.num_clients; i++) {
        if (s_ctx.clients[i].socket_fd >= 0) {
            close(s_ctx.clients[i].socket_fd);
        }
    }
    DAP_DEL_Z(s_ctx.clients);
}

//===================================================================
// POLLING
//===================================================================

static bool s_wait_for_reactor(uint32_t timeout_ms)
{
    uint64_t start = dap_nanotime_now();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - start) < timeout_ns) {
        if (dap_events_worker_get(0)) return true;
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

//===================================================================
// TEST CASES
//===================================================================

static void test_session_routing(void)
{
    #define NUM_SESSIONS 10
    #define PACKETS_PER  30
    
    dap_test_msg("\n=== Test: Session Routing (%d sessions x %d packets) ===\n",
                 NUM_SESSIONS, PACKETS_PER);
    
    // Init reactor
    int ret = dap_events_init(0, 0);
    dap_assert(ret == 0, "dap_events_init");
    
    ret = dap_events_start();
    dap_assert(ret == 0, "dap_events_start");
    
    dap_assert(s_wait_for_reactor(2000), "Reactor start");
    usleep(WORKER_STARTUP_MS * 1000);
    
    s_num_workers = dap_proc_thread_get_count();
    dap_test_msg("Workers: %u", s_num_workers);
    
    // Init tracker
    s_tracker_init(NUM_SESSIONS, s_num_workers);
    
    atomic_store(&s_raw_recv_count, 0);
    atomic_store(&s_invalid_count, 0);
    atomic_store(&s_ctx.sending_done, false);
    atomic_store(&s_ctx.test_done, false);
    
    // Setup server
    ret = s_setup_server();
    dap_assert(ret == 0, "Server setup");
    
    // Setup clients
    ret = s_setup_clients(NUM_SESSIONS, PACKETS_PER);
    dap_assert(ret == 0, "Clients setup");
    
    // Send packets - interleaved from all clients
    dap_test_msg("Sending packets...");
    
    for (uint32_t pkt = 0; pkt < PACKETS_PER; pkt++) {
        for (uint32_t c = 0; c < NUM_SESSIONS; c++) {
            s_client_send_packet(&s_ctx.clients[c]);
        }
        usleep(1000);  // 1ms between rounds
    }
    
    // Wait for processing
    dap_test_msg("Waiting for processing...");
    usleep(500000);  // 500ms
    
    // Report
    dap_test_msg("\nRaw stats: recv=%u, invalid=%u",
                 atomic_load(&s_raw_recv_count), atomic_load(&s_invalid_count));
    s_tracker_print_report();
    
    uint32_t violations = atomic_load(&s_tracker.total_violations);
    uint32_t sent = atomic_load(&s_tracker.total_sent);
    uint32_t recv = atomic_load(&s_tracker.total_recv);
    
    dap_test_msg("\nResult: sent=%u, recv=%u, violations=%u", sent, recv, violations);
    
    // Cleanup
    s_cleanup_clients();
    s_cleanup_server();
    s_tracker_deinit();
    dap_events_deinit();
    
    // Assertions
    dap_assert(recv > 0, "Should receive some packets");
    
    if (violations > 0) {
        dap_test_msg("REGRESSION: %u sticky session violations!", violations);
    }
    
    dap_assert(violations == 0, "REGRESSION: No sticky session violations expected");
    
    dap_pass_msg("Session routing test PASSED");
    
    #undef NUM_SESSIONS
    #undef PACKETS_PER
}

/**
 * @brief Test: Scaling with many sessions
 */
static void test_session_scaling(void)
{
    dap_test_msg("\n=== Test: Session Scaling ===\n");
    
    uint32_t session_counts[] = {10, 20, 50, 100};
    int num_tests = sizeof(session_counts) / sizeof(session_counts[0]);
    
    for (int t = 0; t < num_tests; t++) {
        uint32_t num_sessions = session_counts[t];
        uint32_t packets_per = 20;
        
        dap_test_msg("\n--- %u sessions x %u packets ---", num_sessions, packets_per);
        
        int ret = dap_events_init(0, 0);
        if (ret != 0) continue;
        
        ret = dap_events_start();
        if (ret != 0) { dap_events_deinit(); continue; }
        
        if (!s_wait_for_reactor(2000)) { dap_events_deinit(); continue; }
        usleep(WORKER_STARTUP_MS * 1000);
        
        s_num_workers = dap_proc_thread_get_count();
        s_tracker_init(num_sessions, s_num_workers);
        
        atomic_store(&s_raw_recv_count, 0);
        atomic_store(&s_invalid_count, 0);
        
        if (s_setup_server() != 0) {
            s_tracker_deinit();
            dap_events_deinit();
            continue;
        }
        
        if (s_setup_clients(num_sessions, packets_per) != 0) {
            s_cleanup_server();
            s_tracker_deinit();
            dap_events_deinit();
            continue;
        }
        
        // Send
        for (uint32_t pkt = 0; pkt < packets_per; pkt++) {
            for (uint32_t c = 0; c < num_sessions; c++) {
                s_client_send_packet(&s_ctx.clients[c]);
            }
        }
        
        usleep(300000);  // 300ms
        
        uint32_t sent = atomic_load(&s_tracker.total_sent);
        uint32_t recv = atomic_load(&s_tracker.total_recv);
        uint32_t violations = atomic_load(&s_tracker.total_violations);
        
        bool pass = (recv > 0 && violations == 0);
        
        dap_test_msg("%u sessions: sent=%u, recv=%u, violations=%u [%s]",
                     num_sessions, sent, recv, violations, pass ? "PASS" : "FAIL");
        
        if (violations > 0) {
            s_tracker_print_report();
        }
        
        s_cleanup_clients();
        s_cleanup_server();
        s_tracker_deinit();
        dap_events_deinit();
        
        usleep(100000);
    }
    
    dap_pass_msg("Scaling test completed");
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_common_init("test_trans_udp_multiclient", NULL);
    dap_log_level_set(L_WARNING);
    
    printf("\n==========================================\n");
    printf("UDP Transport Multi-Session Regression Test\n");
    printf("(Session routing + sticky session check)\n");
    printf("==========================================\n\n");
    
    dap_print_module_name("trans_udp_multiclient");
    
    srand((unsigned)time(NULL));
    
    test_session_routing();
    test_session_scaling();
    
    printf("\n=== All tests completed ===\n");
    
    dap_common_deinit();
    return 0;
}
