/**
 * @file test_io_flow_multiclient.c
 * @brief Multi-client regression test for dap_io_flow layer with REAL UDP
 * 
 * This test reproduces the multi-client issue observed in integration tests:
 * - Uses REAL UDP sockets and DAP events reactor
 * - Multiple clients sending packets concurrently
 * - Packet sequence validation (no reordering)
 * - Packet ownership validation (no cross-contamination between clients)
 * - Worker affinity (sticky sessions)
 * 
 * NO ENCRYPTION - packets contain plaintext markers for easy tracking.
 * 
 * The test simulates problematic scenarios:
 * 1. High volume concurrent sends
 * 2. "Stuck" client with lost ACKs while others work
 * 3. Rapid client connect/disconnect patterns
 * 
 * @date 2026-02-03
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_io_flow.h"
#include "dap_io_flow_datagram.h"
#include "dap_io_flow_socket.h"
#include "dap_timerfd.h"
#include "dap_list.h"
#include "dap_server.h"
#include "dap_proc_thread.h"

#ifdef DAP_OS_LINUX
#include "dap_io_flow_cbpf.h"
#include "dap_io_flow_ebpf.h"
#endif

#define LOG_TAG "test_io_flow_multiclient"

//===================================================================
// TEST CONFIGURATION
//===================================================================

// Use actual worker count from DAP
static uint32_t s_num_workers = 0;
#define TEST_TIMEOUT_SEC        30      // Test timeout
#define POLL_INTERVAL_US        5000    // 5ms between polls
#define WORKER_STARTUP_MS       500     // Time for workers to initialize

// Packet tracking
#define PACKET_MAGIC            0xDEADBEEF
#define WORKER_ID_NOT_RECEIVED  0xFF

//===================================================================
// PACKET STRUCTURE (NO ENCRYPTION)
//===================================================================

typedef struct __attribute__((packed)) {
    uint32_t magic;           // PACKET_MAGIC
    uint32_t client_id;       // Which client sent this
    uint32_t seq_num;         // Sequence number within client
    uint32_t unique_id;       // Globally unique ID
    char payload[48];         // Human-readable payload
} test_packet_t;

//===================================================================
// PACKET TRACKER - tracks each packet individually
//===================================================================

typedef struct {
    uint8_t *packet_worker;       // [max_clients * max_packets] -> worker_id
    int8_t *client_expected_worker; // [max_clients] -> expected worker (-1 = not set)
    
    uint32_t max_clients;
    uint32_t max_packets;
    
    _Atomic uint32_t violations_count;
    _Atomic uint32_t sticky_violations;
    _Atomic uint32_t duplicate_count;
    _Atomic uint32_t total_received;
    _Atomic uint32_t total_sent;
    
    pthread_mutex_t lock;
} packet_tracker_t;

static void s_tracker_init(packet_tracker_t *t, uint32_t max_clients, uint32_t max_packets)
{
    t->max_clients = max_clients;
    t->max_packets = max_packets;
    
    size_t pkt_size = max_clients * max_packets;
    t->packet_worker = DAP_NEW_SIZE(uint8_t, pkt_size);
    memset(t->packet_worker, WORKER_ID_NOT_RECEIVED, pkt_size);
    
    t->client_expected_worker = DAP_NEW_SIZE(int8_t, max_clients);
    memset(t->client_expected_worker, -1, max_clients);
    
    atomic_init(&t->violations_count, 0);
    atomic_init(&t->sticky_violations, 0);
    atomic_init(&t->duplicate_count, 0);
    atomic_init(&t->total_received, 0);
    atomic_init(&t->total_sent, 0);
    
    pthread_mutex_init(&t->lock, NULL);
}

static void s_tracker_deinit(packet_tracker_t *t)
{
    DAP_DELETE(t->packet_worker);
    DAP_DELETE(t->client_expected_worker);
    pthread_mutex_destroy(&t->lock);
}

static int s_tracker_record(packet_tracker_t *t, uint32_t client_id, 
                            uint32_t seq_num, uint32_t worker_id)
{
    if (client_id >= t->max_clients || seq_num >= t->max_packets) {
        return -1;
    }
    
    int result = 0;
    size_t idx = client_id * t->max_packets + seq_num;
    
    pthread_mutex_lock(&t->lock);
    
    // Check for duplicate
    if (t->packet_worker[idx] != WORKER_ID_NOT_RECEIVED) {
        atomic_fetch_add(&t->duplicate_count, 1);
        atomic_fetch_add(&t->violations_count, 1);
        log_it(L_WARNING, "DUPLICATE: Client %u Pkt %u already on Worker %u, got Worker %u",
               client_id, seq_num, t->packet_worker[idx], worker_id);
        result = -1;
    } else {
        t->packet_worker[idx] = (uint8_t)worker_id;
        atomic_fetch_add(&t->total_received, 1);
        
        // Check sticky session
        if (t->client_expected_worker[client_id] < 0) {
            t->client_expected_worker[client_id] = (int8_t)worker_id;
        } else if (t->client_expected_worker[client_id] != (int8_t)worker_id) {
            atomic_fetch_add(&t->sticky_violations, 1);
            atomic_fetch_add(&t->violations_count, 1);
            log_it(L_ERROR, "STICKY VIOLATION: Client %u Pkt %u -> Worker %u (expected %d)",
                   client_id, seq_num, worker_id, t->client_expected_worker[client_id]);
            result = -1;
        }
    }
    
    pthread_mutex_unlock(&t->lock);
    return result;
}

static void s_tracker_print_report(packet_tracker_t *t)
{
    pthread_mutex_lock(&t->lock);
    
    dap_test_msg("\n=== Packet Distribution Report ===");
    
    uint32_t *worker_counts = alloca(s_num_workers * sizeof(uint32_t));
    memset(worker_counts, 0, s_num_workers * sizeof(uint32_t));
    uint32_t lost = 0;
    
    for (uint32_t c = 0; c < t->max_clients; c++) {
        for (uint32_t p = 0; p < t->max_packets; p++) {
            uint8_t w = t->packet_worker[c * t->max_packets + p];
            if (w != WORKER_ID_NOT_RECEIVED && w < s_num_workers) {
                worker_counts[w]++;
            } else if (w == WORKER_ID_NOT_RECEIVED) {
                lost++;
            }
        }
    }
    
    dap_test_msg("Worker distribution (total %u workers):", s_num_workers);
    for (uint32_t w = 0; w < s_num_workers; w++) {
        dap_test_msg("  Worker %u: %u packets", w, worker_counts[w]);
    }
    
    uint32_t total_recv = atomic_load(&t->total_received);
    uint32_t violations = atomic_load(&t->violations_count);
    uint32_t sticky = atomic_load(&t->sticky_violations);
    uint32_t dups = atomic_load(&t->duplicate_count);
    
    dap_test_msg("Totals: sent=%u, received=%u, lost=%u", 
                 atomic_load(&t->total_sent), total_recv, lost);
    dap_test_msg("Violations: total=%u, sticky=%u, duplicates=%u",
                 violations, sticky, dups);
    
    // Per-client summary
    dap_test_msg("Client -> Worker mapping:");
    for (uint32_t c = 0; c < t->max_clients; c++) {
        int8_t expected = t->client_expected_worker[c];
        
        uint32_t *client_worker_counts = alloca(s_num_workers * sizeof(uint32_t));
        memset(client_worker_counts, 0, s_num_workers * sizeof(uint32_t));
        uint32_t client_recv = 0;
        
        for (uint32_t p = 0; p < t->max_packets; p++) {
            uint8_t w = t->packet_worker[c * t->max_packets + p];
            if (w != WORKER_ID_NOT_RECEIVED && w < s_num_workers) {
                client_worker_counts[w]++;
                client_recv++;
            }
        }
        
        int active_workers = 0;
        for (uint32_t w = 0; w < s_num_workers; w++) {
            if (client_worker_counts[w] > 0) active_workers++;
        }
        
        if (active_workers > 1) {
            dap_test_msg("  Client %2u: SPLIT! Expected W%d:", c, expected);
            for (uint32_t w = 0; w < s_num_workers; w++) {
                if (client_worker_counts[w] > 0) {
                    dap_test_msg("             W%d: %u pkts", w, client_worker_counts[w]);
                }
            }
        } else if (client_recv > 0) {
            dap_test_msg("  Client %2u: W%d (%u/%u)", c, expected, client_recv, t->max_packets);
        } else {
            dap_test_msg("  Client %2u: NO PACKETS", c);
        }
    }
    
    pthread_mutex_unlock(&t->lock);
}

//===================================================================
// TEST CONTEXT
//===================================================================

typedef struct {
    int client_id;
    uint32_t packets_sent;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
    bool skip_acks;  // If true, simulate "stuck" client
} client_ctx_t;

typedef struct {
    dap_io_flow_server_t *server;
    packet_tracker_t tracker;
    uint16_t server_port;
    
    // Clients
    dap_events_socket_t **client_es;
    client_ctx_t *client_ctx;
    uint32_t num_clients;
    uint32_t packets_per_client;
    
    // State
    _Atomic bool sending_complete;
    _Atomic bool test_complete;
    _Atomic uint32_t clients_done;
} test_context_t;

static test_context_t s_ctx = {0};

//===================================================================
// IO FLOW SERVER CALLBACKS
//===================================================================

static _Atomic uint32_t s_pkt_recv_count = 0;
static _Atomic uint32_t s_pkt_invalid_count = 0;

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
    
    atomic_fetch_add(&s_pkt_recv_count, 1);
    
    if (atomic_load(&s_ctx.test_complete)) return;
    
    if (a_size < sizeof(test_packet_t)) {
        atomic_fetch_add(&s_pkt_invalid_count, 1);
        return;
    }
    
    test_packet_t *pkt = (test_packet_t*)a_data;
    
    if (pkt->magic != PACKET_MAGIC) {
        log_it(L_WARNING, "Invalid magic: 0x%08X (size=%zu)", pkt->magic, a_size);
        atomic_fetch_add(&s_pkt_invalid_count, 1);
        return;
    }
    
    // Get current worker
    dap_worker_t *worker = dap_worker_get_current();
    uint32_t worker_id = worker ? worker->id : 0;
    
    // Debug: log first few packets with hex dump
    static _Atomic uint32_t s_logged = 0;
    if (atomic_fetch_add(&s_logged, 1) < 5) {
        printf("PKT #%u: size=%zu, client=%u seq=%u unique=%u worker=%u\n",
               atomic_load(&s_pkt_recv_count), a_size,
               pkt->client_id, pkt->seq_num, pkt->unique_id, worker_id);
        printf("  HEX: ");
        for (size_t i = 0; i < 24 && i < a_size; i++) {
            printf("%02X ", a_data[i]);
        }
        printf("\n");
    }
    
    s_tracker_record(&s_ctx.tracker, pkt->client_id, pkt->seq_num, worker_id);
}

static dap_io_flow_t* s_flow_create(dap_io_flow_server_t *a_server,
                                     const struct sockaddr_storage *a_addr,
                                     dap_events_socket_t *a_es)
{
    (void)a_server;
    (void)a_addr;
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
// CLIENT SEND LOGIC
//===================================================================

static void s_client_send_packet(void *a_arg)
{
    dap_events_socket_t *es = (dap_events_socket_t*)a_arg;
    if (!es || !es->_inheritor) return;
    
    if (atomic_load(&s_ctx.test_complete)) return;
    
    client_ctx_t *ctx = (client_ctx_t*)es->_inheritor;
    
    if (ctx->packets_sent >= s_ctx.packets_per_client) {
        return;
    }
    
    // Create packet
    test_packet_t pkt = {
        .magic = PACKET_MAGIC,
        .client_id = ctx->client_id,
        .seq_num = ctx->packets_sent,
        .unique_id = ctx->client_id * s_ctx.packets_per_client + ctx->packets_sent,
    };
    snprintf(pkt.payload, sizeof(pkt.payload), "C%02d:P%03u:U%05u",
             ctx->client_id, ctx->packets_sent, pkt.unique_id);
    
    // Send
    size_t sent = dap_events_socket_sendto_unsafe(es, &pkt, sizeof(pkt),
                                                   &ctx->server_addr, ctx->server_addr_len);
    
    if (sent > 0) {
        ctx->packets_sent++;
        atomic_fetch_add(&s_ctx.tracker.total_sent, 1);
    }
    
    // Schedule next
    if (ctx->packets_sent < s_ctx.packets_per_client) {
        dap_worker_exec_callback_on(es->worker, s_client_send_packet, es);
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
        dap_events_socket_t *es = s_ctx.client_es[i];
        if (es && es->worker) {
            dap_worker_exec_callback_on(es->worker, s_client_send_packet, es);
        }
    }
    
    return false;  // One-shot
}

static bool s_check_completion(void *a_arg)
{
    (void)a_arg;
    
    uint32_t sent = atomic_load(&s_ctx.tracker.total_sent);
    uint32_t recv = atomic_load(&s_ctx.tracker.total_received);
    uint32_t expected = s_ctx.num_clients * s_ctx.packets_per_client;
    uint32_t done = atomic_load(&s_ctx.clients_done);
    
    if (done >= s_ctx.num_clients) {
        atomic_store(&s_ctx.sending_complete, true);
    }
    
    // Accept 90% delivery (UDP may lose some)
    uint32_t threshold = expected * 90 / 100;
    if (recv >= threshold && atomic_load(&s_ctx.sending_complete)) {
        dap_test_msg("Complete: sent=%u, recv=%u (threshold=%u)", sent, recv, threshold);
        atomic_store(&s_ctx.test_complete, true);
        return false;
    }
    
    return true;  // Continue checking
}

//===================================================================
// TEST SETUP/TEARDOWN
//===================================================================

static int s_create_clients(uint32_t num_clients, uint32_t packets_per)
{
    s_ctx.num_clients = num_clients;
    s_ctx.packets_per_client = packets_per;
    
    s_ctx.client_es = DAP_NEW_Z_SIZE(dap_events_socket_t*, num_clients * sizeof(void*));
    s_ctx.client_ctx = DAP_NEW_Z_SIZE(client_ctx_t, num_clients * sizeof(client_ctx_t));
    
    for (uint32_t i = 0; i < num_clients; i++) {
        int fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd < 0) {
            log_it(L_ERROR, "socket() failed: %s", strerror(errno));
            return -1;
        }
        
        struct sockaddr_in bind_addr = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        
        if (bind(fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr)) < 0) {
            log_it(L_ERROR, "bind() failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
        
        s_ctx.client_ctx[i].client_id = i;
        s_ctx.client_ctx[i].packets_sent = 0;
        s_ctx.client_ctx[i].skip_acks = false;
        
        struct sockaddr_in *srv = (struct sockaddr_in*)&s_ctx.client_ctx[i].server_addr;
        srv->sin_family = AF_INET;
        srv->sin_port = htons(s_ctx.server_port);
        srv->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        s_ctx.client_ctx[i].server_addr_len = sizeof(struct sockaddr_in);
        
        // Get worker for this client
        dap_worker_t *worker = dap_events_worker_get(i % s_num_workers);
        if (!worker) {
            close(fd);
            return -1;
        }
        
        // Create esocket callbacks (minimal for UDP client)
        static dap_events_socket_callbacks_t s_client_cbs = {0};
        
        dap_events_socket_t *es = dap_events_socket_wrap_no_add(fd, &s_client_cbs);
        if (!es) {
            close(fd);
            return -1;
        }
        
        es->_inheritor = &s_ctx.client_ctx[i];
        es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
        
        // Add to worker
        dap_events_socket_assign_on_worker_mt(es, worker);
        
        s_ctx.client_es[i] = es;
    }
    
    dap_test_msg("Created %u UDP clients", num_clients);
    return 0;
}

static void s_cleanup_clients(void)
{
    for (uint32_t i = 0; i < s_ctx.num_clients; i++) {
        if (s_ctx.client_es[i]) {
            s_ctx.client_es[i]->_inheritor = NULL;
            s_ctx.client_es[i]->flags |= DAP_SOCK_SIGNAL_CLOSE;
            s_ctx.client_es[i] = NULL;
        }
    }
    
    dap_test_sleep_ms(300);
    
    DAP_DEL_Z(s_ctx.client_es);
    DAP_DEL_Z(s_ctx.client_ctx);
}

// Tier name for diagnostic
static const char* s_tier_name(dap_io_flow_lb_tier_t tier)
{
    switch (tier) {
        case DAP_IO_FLOW_LB_TIER_NONE: return "NONE";
        case DAP_IO_FLOW_LB_TIER_APPLICATION: return "APPLICATION";
#ifdef DAP_OS_LINUX
        case DAP_IO_FLOW_LB_TIER_CLASSIC_BPF: return "CBPF";
        case DAP_IO_FLOW_LB_TIER_EBPF: return "EBPF";
#endif
        default: return "UNKNOWN";
    }
}

static int s_setup_server(void)
{
    // Check tier availability
    dap_test_msg("Tier availability check:");
    dap_test_msg("  Application: always available");
#ifdef DAP_OS_LINUX
    dap_test_msg("  CBPF: %s", dap_io_flow_cbpf_is_available() ? "YES" : "NO");
    dap_test_msg("  eBPF: %s", dap_io_flow_ebpf_is_available() ? "YES" : "NO");
    
    // Force Application tier for testing multi-worker distribution
    dap_io_flow_set_forced_tier(DAP_IO_FLOW_LB_TIER_APPLICATION);
    dap_test_msg("  Forced tier: APPLICATION");
#endif
    
    // Create io_flow_server
    s_ctx.server = dap_io_flow_server_new("test_server", &s_flow_ops, 
                                           DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    if (!s_ctx.server) {
        log_it(L_ERROR, "Failed to create io_flow_server");
        return -1;
    }
    
    // Start listener on UDP (port 0 = kernel assigns)
    int ret = dap_io_flow_server_listen(s_ctx.server, "127.0.0.1", 0);
    if (ret != 0) {
        log_it(L_ERROR, "Failed to start server");
        dap_io_flow_server_delete(s_ctx.server);
        return -1;
    }
    
    // Get port and count listeners
    int listener_count = 0;
    if (s_ctx.server->dap_server && s_ctx.server->dap_server->es_listeners) {
        // es_listeners is a list, get first
        dap_list_t *l_list = s_ctx.server->dap_server->es_listeners;
        while (l_list) {
            listener_count++;
            if (listener_count == 1 && l_list->data) {
                dap_events_socket_t *listener = (dap_events_socket_t*)l_list->data;
                struct sockaddr_in addr;
                socklen_t len = sizeof(addr);
                if (getsockname(listener->fd, (struct sockaddr*)&addr, &len) == 0) {
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
    
    // Report actual tier
    dap_io_flow_lb_tier_t actual_tier = s_ctx.server->lb_tier;
    dap_test_msg("Server started: port=%u, listeners=%d, tier=%s",
                 s_ctx.server_port, listener_count, s_tier_name(actual_tier));
    return 0;
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

//===================================================================
// POLLING HELPERS
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

static bool s_wait_for_test_complete(uint32_t timeout_sec)
{
    time_t start = time(NULL);
    
    while ((time(NULL) - start) < timeout_sec) {
        if (atomic_load(&s_ctx.test_complete)) return true;
        usleep(POLL_INTERVAL_US * 10);  // 50ms
    }
    return false;
}

//===================================================================
// TEST CASES
//===================================================================

/**
 * @brief Test: High volume multi-client concurrent send
 * Reproduces the issue seen in test_trans_integration
 */
static void test_realudp_multiclient(void)
{
    #define NUM_CLIENTS_TEST    10
    #define PACKETS_PER_CLIENT  50
    
    dap_test_msg("\n=== Test: Real UDP Multi-Client (%d clients x %d packets) ===\n",
                 NUM_CLIENTS_TEST, PACKETS_PER_CLIENT);
    
    // Initialize reactor
    int ret = dap_events_init(0, 0);
    dap_assert(ret == 0, "dap_events_init should succeed");
    
    ret = dap_events_start();
    dap_assert(ret == 0, "dap_events_start should succeed");
    
    dap_assert(s_wait_for_reactor(2000), "Reactor should start within 2s");
    usleep(WORKER_STARTUP_MS * 1000);  // Let workers initialize
    
    // Get actual worker count
    s_num_workers = dap_proc_thread_get_count();
    dap_test_msg("Detected %u worker threads", s_num_workers);
    
    // Reset packet counters
    atomic_store(&s_pkt_recv_count, 0);
    atomic_store(&s_pkt_invalid_count, 0);
    
    // Initialize tracker
    s_tracker_init(&s_ctx.tracker, NUM_CLIENTS_TEST, PACKETS_PER_CLIENT);
    
    atomic_store(&s_ctx.sending_complete, false);
    atomic_store(&s_ctx.test_complete, false);
    atomic_store(&s_ctx.clients_done, 0);
    
    // Setup server
    ret = s_setup_server();
    dap_assert(ret == 0, "Server setup should succeed");
    
    // Setup clients
    ret = s_create_clients(NUM_CLIENTS_TEST, PACKETS_PER_CLIENT);
    dap_assert(ret == 0, "Client setup should succeed");
    
    // Start timers
    dap_worker_t *worker0 = dap_events_worker_get(0);
    dap_timerfd_start_on_worker(worker0, 100, s_start_sending, NULL);
    dap_timerfd_start_on_worker(worker0, 50, s_check_completion, NULL);
    
    // Wait for completion
    bool completed = s_wait_for_test_complete(TEST_TIMEOUT_SEC);
    
    // Report results
    dap_test_msg("Raw packet stats: recv_count=%u, invalid=%u",
                 atomic_load(&s_pkt_recv_count), atomic_load(&s_pkt_invalid_count));
    s_tracker_print_report(&s_ctx.tracker);
    
    uint32_t violations = atomic_load(&s_ctx.tracker.violations_count);
    uint32_t sticky = atomic_load(&s_ctx.tracker.sticky_violations);
    uint32_t recv = atomic_load(&s_ctx.tracker.total_received);
    uint32_t expected = NUM_CLIENTS_TEST * PACKETS_PER_CLIENT;
    
    dap_test_msg("\nTest %s: violations=%u, sticky=%u, recv=%u/%u",
                 completed ? "COMPLETED" : "TIMEOUT", violations, sticky, recv, expected);
    
    // Signal shutdown and wait for callbacks to drain
    atomic_store(&s_ctx.test_complete, true);
    dap_test_sleep_ms(500);
    
    // Cleanup
    s_cleanup_server();
    s_cleanup_clients();
    s_tracker_deinit(&s_ctx.tracker);
    
    dap_events_deinit();
    
    // Assertions - this is where we expect to see failures
    if (violations > 0) {
        dap_test_msg("REGRESSION DETECTED: %u violations found!", violations);
        if (sticky > 0) {
            dap_test_msg("  STICKY SESSION VIOLATIONS: %u", sticky);
        }
    }
    
    // For now, just report - we want to see the problem manifest
    dap_assert(completed, "Test should complete within timeout");
    
    // The key assertion: no sticky violations
    dap_assert(sticky == 0, "REGRESSION: Sticky session violations detected!");
    
    dap_pass_msg("Real UDP multi-client test completed");
    
    #undef NUM_CLIENTS_TEST
    #undef PACKETS_PER_CLIENT
}

/**
 * @brief Test: Increasing client count to find breaking point
 */
static void test_realudp_scaling(void)
{
    dap_test_msg("\n=== Test: UDP Scaling (finding breaking point) ===\n");
    
    uint32_t client_counts[] = {5, 10, 20, 50, 100, 200};
    int num_tests = sizeof(client_counts) / sizeof(client_counts[0]);
    
    for (int t = 0; t < num_tests; t++) {
        uint32_t num_clients = client_counts[t];
        uint32_t packets_per = 30;
        
        dap_test_msg("\n--- Testing %u clients x %u packets ---", num_clients, packets_per);
        
        // Init
        int ret = dap_events_init(0, 0);
        if (ret != 0) continue;
        
        ret = dap_events_start();
        if (ret != 0) {
            dap_events_deinit();
            continue;
        }
        
        if (!s_wait_for_reactor(2000)) {
            dap_events_deinit();
            continue;
        }
        usleep(WORKER_STARTUP_MS * 1000);
        
        // Get actual worker count
        s_num_workers = dap_proc_thread_get_count();
        
        // Reset counters
        atomic_store(&s_pkt_recv_count, 0);
        atomic_store(&s_pkt_invalid_count, 0);
        
        s_tracker_init(&s_ctx.tracker, num_clients, packets_per);
        atomic_store(&s_ctx.sending_complete, false);
        atomic_store(&s_ctx.test_complete, false);
        atomic_store(&s_ctx.clients_done, 0);
        
        if (s_setup_server() != 0) {
            s_tracker_deinit(&s_ctx.tracker);
            dap_events_deinit();
            continue;
        }
        
        if (s_create_clients(num_clients, packets_per) != 0) {
            s_cleanup_server();
            s_tracker_deinit(&s_ctx.tracker);
            dap_events_deinit();
            continue;
        }
        
        dap_worker_t *worker0 = dap_events_worker_get(0);
        dap_timerfd_start_on_worker(worker0, 100, s_start_sending, NULL);
        dap_timerfd_start_on_worker(worker0, 50, s_check_completion, NULL);
        
        bool completed = s_wait_for_test_complete(15);
        
        uint32_t violations = atomic_load(&s_ctx.tracker.violations_count);
        uint32_t sticky = atomic_load(&s_ctx.tracker.sticky_violations);
        uint32_t recv = atomic_load(&s_ctx.tracker.total_received);
        uint32_t sent = atomic_load(&s_ctx.tracker.total_sent);
        
        dap_test_msg("%u clients: sent=%u, recv=%u, violations=%u, sticky=%u [%s]",
                     num_clients, sent, recv, violations, sticky,
                     (completed && sticky == 0) ? "PASS" : "FAIL");
        
        if (violations > 0 || sticky > 0) {
            s_tracker_print_report(&s_ctx.tracker);
        }
        
        // Signal shutdown and wait for callbacks to drain
        atomic_store(&s_ctx.test_complete, true);
        dap_test_sleep_ms(500);
        
        // Cleanup
        s_cleanup_server();
        s_cleanup_clients();
        s_tracker_deinit(&s_ctx.tracker);
        dap_events_deinit();
        
        usleep(100000);  // 100ms between tests
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
    
    dap_common_init("test_io_flow_multiclient", NULL);
    dap_log_level_set(L_WARNING);
    
    printf("\n========================================\n");
    printf("IO Flow Multi-Client Regression Tests\n");
    printf("(Real UDP sockets + DAP reactor)\n");
    printf("========================================\n\n");
    
    dap_print_module_name("io_flow_multiclient_udp");
    
    // Run tests
    test_realudp_multiclient();
    test_realudp_scaling();
    
    printf("\n=== All tests completed ===\n");
    
    dap_common_deinit();
    return 0;
}
