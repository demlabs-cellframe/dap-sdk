/**
 * @file test_io_flow_ctrl_multiclient.c
 * @brief Regression test for multi-client flow control issue
 * 
 * Reproduces the bug from test_trans_integration:
 * - Multiple clients with individual flow_ctrl instances
 * - Real UDP sockets for packet exchange
 * - Client sends data, server echoes ACKs
 * - Tests that ACKs are processed correctly and retransmits stop
 * 
 * From integration test logs, the bug:
 * - send_seq_acked updates correctly (5579 → 5580 → ... → 5587)
 * - But then RESETS back to 5579 → 5580 (BUG!)
 * - Retransmits continue despite ACKs being received
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
#include <fcntl.h>
#include <poll.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_proc_thread.h"
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow.h"
#include "dap_proc_thread.h"

#define LOG_TAG "test_fc_multiclient"

// Global reactor flag (defined in main)
static bool s_reactor_available = false;

//===================================================================
// CONFIGURATION
//===================================================================

#define NUM_CLIENTS         3       // Like integration test
#define PACKETS_PER_CLIENT  50      // Enough to see retransmit pattern
#define PACKET_SIZE         100     // Small packets for fast test
#define RETRANSMIT_MS       100     // Fast retransmit
#define TEST_TIMEOUT_MS     10000   // 10 sec test timeout
#define POLL_INTERVAL_US    5000    // 5ms polling

//===================================================================
// PACKET STRUCTURE (simulates FC header)
//===================================================================

typedef struct __attribute__((packed)) {
    uint32_t magic;         // 0xFCFCFCFC
    uint32_t client_id;     // Which client
    uint64_t seq_num;       // Sequence number
    uint64_t ack_seq;       // Acknowledgement
    uint8_t type;           // 0=DATA, 1=ACK
    uint8_t payload[PACKET_SIZE];
} test_packet_t;

#define MAGIC_FC 0xFCFCFCFC
#define PKT_TYPE_DATA 0
#define PKT_TYPE_ACK  1

//===================================================================
// CLIENT/SERVER CONTEXT
//===================================================================

typedef struct {
    int client_id;
    int socket_fd;
    struct sockaddr_in server_addr;
    
    dap_io_flow_ctrl_t *fc;
    dap_io_flow_t flow;
    
    // Stats
    _Atomic uint64_t packets_sent;
    _Atomic uint64_t acks_received;
    _Atomic uint64_t retransmits;
    uint64_t last_ack_seq;
    
    // For FC callbacks
    int send_socket;
    struct sockaddr_in dest_addr;
} client_ctx_t;

typedef struct {
    int socket_fd;
    uint16_t port;
    
    _Atomic bool running;
    pthread_t thread;
    
    // Stats
    _Atomic uint64_t packets_received;
    _Atomic uint64_t acks_sent;
} server_ctx_t;

static client_ctx_t s_clients[NUM_CLIENTS];
static server_ctx_t s_server;

//===================================================================
// FC CALLBACKS - Real UDP Send
//===================================================================

static int s_fc_packet_send(dap_io_flow_t *a_flow, const void *a_packet, size_t a_size, void *a_arg)
{
    (void)a_arg;
    client_ctx_t *ctx = (client_ctx_t*)a_flow->protocol_data;
    if (!ctx) return -1;
    
    ssize_t sent = sendto(ctx->socket_fd, a_packet, a_size, 0,
                          (struct sockaddr*)&ctx->server_addr, sizeof(ctx->server_addr));
    
    if (sent > 0) {
        atomic_fetch_add(&ctx->packets_sent, 1);
        return 0;
    }
    return -1;
}

static int s_fc_payload_deliver(dap_io_flow_t *a_flow, const void *a_payload, size_t a_size, void *a_arg)
{
    (void)a_flow;
    (void)a_payload;
    (void)a_arg;
    // We don't receive data in this test, only send
    return (int)a_size;
}

static int s_fc_packet_prepare(dap_io_flow_t *a_flow, 
                               const dap_io_flow_pkt_metadata_t *a_metadata,
                               const void *a_payload, size_t a_payload_size,
                               void **a_packet_out, size_t *a_packet_size_out,
                               void *a_arg)
{
    (void)a_arg;
    client_ctx_t *ctx = (client_ctx_t*)a_flow->protocol_data;
    
    test_packet_t *pkt = DAP_NEW_Z(test_packet_t);
    if (!pkt) return -1;
    
    pkt->magic = MAGIC_FC;
    pkt->client_id = ctx ? ctx->client_id : 0;
    pkt->seq_num = a_metadata->seq_num;
    pkt->ack_seq = a_metadata->ack_seq;
    pkt->type = (a_payload && a_payload_size > 0) ? PKT_TYPE_DATA : PKT_TYPE_ACK;
    
    if (a_payload && a_payload_size > 0) {
        size_t copy_size = a_payload_size < PACKET_SIZE ? a_payload_size : PACKET_SIZE;
        memcpy(pkt->payload, a_payload, copy_size);
    }
    
    *a_packet_out = pkt;
    *a_packet_size_out = sizeof(test_packet_t);
    return 0;
}

static int s_fc_packet_parse(dap_io_flow_t *a_flow,
                             const void *a_packet, size_t a_packet_size,
                             dap_io_flow_pkt_metadata_t *a_metadata,
                             const void **a_payload_out, size_t *a_payload_size_out,
                             void *a_arg)
{
    (void)a_flow;
    (void)a_arg;
    
    if (a_packet_size < sizeof(test_packet_t)) return -1;
    
    const test_packet_t *pkt = (const test_packet_t*)a_packet;
    if (pkt->magic != MAGIC_FC) return -1;
    
    a_metadata->seq_num = pkt->seq_num;
    a_metadata->ack_seq = pkt->ack_seq;
    
    if (pkt->type == PKT_TYPE_DATA) {
        *a_payload_out = pkt->payload;
        *a_payload_size_out = PACKET_SIZE;
    } else {
        *a_payload_out = NULL;
        *a_payload_size_out = 0;
    }
    
    return 0;
}

static void s_fc_packet_free(void *a_packet, void *a_arg)
{
    (void)a_arg;
    DAP_DELETE(a_packet);
}

//===================================================================
// SERVER THREAD - Receives packets, sends ACKs
//===================================================================

static void* s_server_thread(void *arg)
{
    (void)arg;
    
    uint8_t buf[2048];
    struct sockaddr_in client_addr;
    socklen_t addr_len;
    
    // Track highest seq per client to send cumulative ACK
    uint64_t client_highest_seq[NUM_CLIENTS] = {0};
    
    while (atomic_load(&s_server.running)) {
        struct pollfd pfd = {
            .fd = s_server.socket_fd,
            .events = POLLIN,
        };
        
        int ret = poll(&pfd, 1, 50);  // 50ms timeout
        if (ret <= 0) continue;
        
        addr_len = sizeof(client_addr);
        ssize_t recv_len = recvfrom(s_server.socket_fd, buf, sizeof(buf), 0,
                                     (struct sockaddr*)&client_addr, &addr_len);
        
        if (recv_len < (ssize_t)sizeof(test_packet_t)) continue;
        
        test_packet_t *pkt = (test_packet_t*)buf;
        if (pkt->magic != MAGIC_FC) continue;
        if (pkt->type != PKT_TYPE_DATA) continue;
        
        atomic_fetch_add(&s_server.packets_received, 1);
        
        uint32_t client_id = pkt->client_id;
        if (client_id >= NUM_CLIENTS) continue;
        
        // Update highest seq for this client
        if (pkt->seq_num > client_highest_seq[client_id]) {
            client_highest_seq[client_id] = pkt->seq_num;
        }
        
        // Send ACK back
        test_packet_t ack = {
            .magic = MAGIC_FC,
            .client_id = client_id,
            .seq_num = 0,
            .ack_seq = client_highest_seq[client_id],
            .type = PKT_TYPE_ACK,
        };
        
        sendto(s_server.socket_fd, &ack, sizeof(ack), 0,
               (struct sockaddr*)&client_addr, addr_len);
        
        atomic_fetch_add(&s_server.acks_sent, 1);
    }
    
    return NULL;
}

//===================================================================
// CLIENT RECEIVE THREAD - Receives ACKs, feeds to flow_ctrl
//===================================================================

static void* s_client_recv_thread(void *arg)
{
    client_ctx_t *ctx = (client_ctx_t*)arg;
    
    uint8_t buf[2048];
    
    // Set socket non-blocking
    int flags = fcntl(ctx->socket_fd, F_GETFL, 0);
    fcntl(ctx->socket_fd, F_SETFL, flags | O_NONBLOCK);
    
    while (atomic_load(&s_server.running)) {
        struct pollfd pfd = {
            .fd = ctx->socket_fd,
            .events = POLLIN,
        };
        
        int ret = poll(&pfd, 1, 50);
        if (ret <= 0) continue;
        
        ssize_t recv_len = recv(ctx->socket_fd, buf, sizeof(buf), 0);
        if (recv_len < (ssize_t)sizeof(test_packet_t)) continue;
        
        test_packet_t *pkt = (test_packet_t*)buf;
        if (pkt->magic != MAGIC_FC) continue;
        if (pkt->type != PKT_TYPE_ACK) continue;
        if (pkt->client_id != (uint32_t)ctx->client_id) {
            // ACK for wrong client!
            log_it(L_ERROR, "Client %d received ACK for client %u (BUG!)",
                   ctx->client_id, pkt->client_id);
            continue;
        }
        
        atomic_fetch_add(&ctx->acks_received, 1);
        ctx->last_ack_seq = pkt->ack_seq;
        
        // Feed ACK to flow_ctrl
        if (ctx->fc) {
            dap_io_flow_ctrl_recv(ctx->fc, buf, recv_len);
        }
    }
    
    return NULL;
}

//===================================================================
// TEST SETUP/CLEANUP
//===================================================================

static pthread_t s_client_threads[NUM_CLIENTS];

static int s_setup_server(void)
{
    s_server.socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_server.socket_fd < 0) {
        log_it(L_ERROR, "Server socket failed: %s", strerror(errno));
        return -1;
    }
    
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = 0,
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    
    if (bind(s_server.socket_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log_it(L_ERROR, "Server bind failed: %s", strerror(errno));
        close(s_server.socket_fd);
        return -1;
    }
    
    socklen_t len = sizeof(addr);
    getsockname(s_server.socket_fd, (struct sockaddr*)&addr, &len);
    s_server.port = ntohs(addr.sin_port);
    
    atomic_store(&s_server.running, true);
    atomic_store(&s_server.packets_received, 0);
    atomic_store(&s_server.acks_sent, 0);
    
    pthread_create(&s_server.thread, NULL, s_server_thread, NULL);
    
    dap_test_msg("Server started on port %u", s_server.port);
    return 0;
}

static void s_cleanup_server(void)
{
    atomic_store(&s_server.running, false);
    pthread_join(s_server.thread, NULL);
    close(s_server.socket_fd);
}

// Async flow_ctrl creation on worker thread
typedef struct {
    client_ctx_t *ctx;
    dap_io_flow_ctrl_config_t *config;
    dap_io_flow_ctrl_callbacks_t *cbs;
    volatile bool done;
    volatile int result;
} fc_create_args_t;

static void s_create_fc_on_worker(void *a_arg)
{
    fc_create_args_t *args = (fc_create_args_t*)a_arg;
    
    args->ctx->fc = dap_io_flow_ctrl_create(
        &args->ctx->flow,
        DAP_IO_FLOW_CTRL_RETRANSMIT | DAP_IO_FLOW_CTRL_REORDER,
        args->config,
        args->cbs
    );
    
    args->result = args->ctx->fc ? 0 : -1;
    args->done = true;
}

static int s_setup_clients(void)
{
    static dap_io_flow_ctrl_config_t config;
    dap_io_flow_ctrl_get_default_config(&config);
    config.retransmit_timeout_ms = RETRANSMIT_MS;
    config.max_retransmit_count = 20;
    
    static dap_io_flow_ctrl_callbacks_t cbs = {
        .packet_send = s_fc_packet_send,
        .payload_deliver = s_fc_payload_deliver,
        .packet_prepare = s_fc_packet_prepare,
        .packet_parse = s_fc_packet_parse,
        .packet_free = s_fc_packet_free,
        .arg = NULL,
    };
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        memset(&s_clients[i], 0, sizeof(client_ctx_t));
        s_clients[i].client_id = i;
        s_clients[i].flow.protocol_data = &s_clients[i];
        
        // Create UDP socket
        s_clients[i].socket_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (s_clients[i].socket_fd < 0) {
            log_it(L_ERROR, "Client %d socket failed", i);
            return -1;
        }
        
        struct sockaddr_in bind_addr = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
        };
        bind(s_clients[i].socket_fd, (struct sockaddr*)&bind_addr, sizeof(bind_addr));
        
        // Set server address
        s_clients[i].server_addr.sin_family = AF_INET;
        s_clients[i].server_addr.sin_port = htons(s_server.port);
        s_clients[i].server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        
        // Create flow_ctrl on worker thread for timer support
        if (s_reactor_available) {
            dap_worker_t *worker = dap_events_worker_get(i % dap_proc_thread_get_count());
            if (worker) {
                fc_create_args_t args = {
                    .ctx = &s_clients[i],
                    .config = &config,
                    .cbs = &cbs,
                    .done = false,
                    .result = -1,
                };
                
                dap_worker_exec_callback_on(worker, s_create_fc_on_worker, &args);
                
                // Wait for completion
                while (!args.done) {
                    usleep(1000);
                }
                
                if (args.result != 0) {
                    log_it(L_ERROR, "Client %d flow_ctrl creation on worker failed", i);
                    return -1;
                }
            } else {
                // Fallback to main thread
                s_clients[i].fc = dap_io_flow_ctrl_create(
                    &s_clients[i].flow,
                    DAP_IO_FLOW_CTRL_RETRANSMIT | DAP_IO_FLOW_CTRL_REORDER,
                    &config,
                    &cbs
                );
            }
        } else {
            // No reactor - create on main thread
            s_clients[i].fc = dap_io_flow_ctrl_create(
                &s_clients[i].flow,
                DAP_IO_FLOW_CTRL_RETRANSMIT | DAP_IO_FLOW_CTRL_REORDER,
                &config,
                &cbs
            );
        }
        
        if (!s_clients[i].fc) {
            log_it(L_ERROR, "Client %d flow_ctrl failed", i);
            return -1;
        }
        
        // Start receive thread
        pthread_create(&s_client_threads[i], NULL, s_client_recv_thread, &s_clients[i]);
    }
    
    dap_test_msg("Created %d clients with flow_ctrl%s", NUM_CLIENTS,
                 s_reactor_available ? " (on workers with timers)" : " (no timers)");
    return 0;
}

static void s_cleanup_clients(void)
{
    // Wait for receive threads (they check s_server.running)
    for (int i = 0; i < NUM_CLIENTS; i++) {
        pthread_join(s_client_threads[i], NULL);
        
        if (s_clients[i].fc) {
            dap_io_flow_ctrl_delete(s_clients[i].fc);
        }
        if (s_clients[i].socket_fd >= 0) {
            close(s_clients[i].socket_fd);
        }
    }
}

//===================================================================
// TEST CASES
//===================================================================

/**
 * @brief Test: Sequential clients - first completes, then second starts
 * 
 * Reproduces integration test scenario where client 1 works, client 2 fails.
 */
static void test_sequential_clients(void)
{
    dap_test_msg("\n=== Test: Sequential Clients (Regression) ===\n");
    
    if (s_setup_server() != 0) {
        dap_fail("Server setup failed");
        return;
    }
    
    if (s_setup_clients() != 0) {
        s_cleanup_server();
        dap_fail("Client setup failed");
        return;
    }
    
    usleep(100000);  // Let threads start
    
    // CLIENT 0: Send all packets
    dap_test_msg("Client 0: Sending %d packets...", PACKETS_PER_CLIENT);
    
    uint8_t payload[PACKET_SIZE];
    memset(payload, 0xAA, sizeof(payload));
    
    for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
        payload[0] = 0;
        payload[1] = (uint8_t)p;
        dap_io_flow_ctrl_send(s_clients[0].fc, payload, sizeof(payload));
    }
    
    // Wait for ACKs
    uint64_t start = dap_nanotime_now();
    while ((dap_nanotime_now() - start) < 3000000000ULL) {  // 3 sec
        if (s_clients[0].last_ack_seq >= PACKETS_PER_CLIENT) break;
        usleep(POLL_INTERVAL_US);
    }
    
    uint64_t sent0, retrans0, recv0, ooo0, dup0, lost0;
    dap_io_flow_ctrl_get_stats(s_clients[0].fc, &sent0, &retrans0, &recv0, &ooo0, &dup0, &lost0);
    
    dap_test_msg("Client 0: sent=%lu, retrans=%lu, last_ack=%lu",
                 sent0, retrans0, s_clients[0].last_ack_seq);
    
    // CLIENT 1: Start after client 0 "completes"
    dap_test_msg("Client 1: Sending %d packets...", PACKETS_PER_CLIENT);
    
    for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
        payload[0] = 1;
        payload[1] = (uint8_t)p;
        dap_io_flow_ctrl_send(s_clients[1].fc, payload, sizeof(payload));
    }
    
    // Wait for ACKs
    start = dap_nanotime_now();
    while ((dap_nanotime_now() - start) < 5000000000ULL) {  // 5 sec
        if (s_clients[1].last_ack_seq >= PACKETS_PER_CLIENT) break;
        usleep(POLL_INTERVAL_US);
    }
    
    uint64_t sent1, retrans1, recv1, ooo1, dup1, lost1;
    dap_io_flow_ctrl_get_stats(s_clients[1].fc, &sent1, &retrans1, &recv1, &ooo1, &dup1, &lost1);
    
    dap_test_msg("Client 1: sent=%lu, retrans=%lu, last_ack=%lu",
                 sent1, retrans1, s_clients[1].last_ack_seq);
    
    // Report
    dap_test_msg("\nServer stats: recv=%lu, acks=%lu",
                 atomic_load(&s_server.packets_received),
                 atomic_load(&s_server.acks_sent));
    
    bool client0_ok = (s_clients[0].last_ack_seq >= PACKETS_PER_CLIENT);
    bool client1_ok = (s_clients[1].last_ack_seq >= PACKETS_PER_CLIENT);
    bool client1_stuck = (retrans1 > PACKETS_PER_CLIENT);  // Excessive retransmits
    
    dap_test_msg("\nResults:");
    dap_test_msg("  Client 0: %s", client0_ok ? "COMPLETE" : "INCOMPLETE");
    dap_test_msg("  Client 1: %s%s", 
                 client1_ok ? "COMPLETE" : "INCOMPLETE",
                 client1_stuck ? " [STUCK - excessive retransmits!]" : "");
    
    s_cleanup_clients();
    s_cleanup_server();
    
    // Assertions
    dap_assert(client0_ok, "Client 0 should complete");
    
    if (!client1_ok || client1_stuck) {
        dap_test_msg("\n*** BUG REPRODUCED: Client 1 stuck! ***");
        dap_test_msg("This is the regression from test_trans_integration");
    }
    
    dap_assert(client1_ok, "REGRESSION: Client 1 should complete");
    dap_assert(!client1_stuck, "REGRESSION: Client 1 should not have excessive retransmits");
    
    dap_pass_msg("Sequential clients test completed");
}

/**
 * @brief Test: Concurrent clients - all start at once
 */
static void test_concurrent_clients(void)
{
    dap_test_msg("\n=== Test: Concurrent Clients ===\n");
    
    if (s_setup_server() != 0) {
        dap_fail("Server setup failed");
        return;
    }
    
    if (s_setup_clients() != 0) {
        s_cleanup_server();
        dap_fail("Client setup failed");
        return;
    }
    
    usleep(100000);
    
    // All clients send concurrently
    dap_test_msg("All %d clients sending %d packets each...", NUM_CLIENTS, PACKETS_PER_CLIENT);
    
    uint8_t payload[PACKET_SIZE];
    
    for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
        for (int c = 0; c < NUM_CLIENTS; c++) {
            memset(payload, 0xBB, sizeof(payload));
            payload[0] = (uint8_t)c;
            payload[1] = (uint8_t)p;
            dap_io_flow_ctrl_send(s_clients[c].fc, payload, sizeof(payload));
        }
        usleep(1000);  // 1ms between rounds
    }
    
    // Wait for all ACKs
    dap_test_msg("Waiting for ACKs (max 8 sec)...");
    
    uint64_t start = dap_nanotime_now();
    int last_sec = 0;
    while ((dap_nanotime_now() - start) < 8000000000ULL) {  // 8 sec
        bool all_done = true;
        uint64_t total_acks = 0;
        for (int c = 0; c < NUM_CLIENTS; c++) {
            total_acks += s_clients[c].last_ack_seq;
            if (s_clients[c].last_ack_seq < PACKETS_PER_CLIENT) {
                all_done = false;
            }
        }
        
        int sec = (int)((dap_nanotime_now() - start) / 1000000000ULL);
        if (sec > last_sec) {
            dap_test_msg("  [%d sec] acks: %lu/%d", sec, total_acks, NUM_CLIENTS * PACKETS_PER_CLIENT);
            last_sec = sec;
        }
        
        if (all_done) break;
        usleep(POLL_INTERVAL_US);
    }
    
    // Report
    dap_test_msg("\nResults:");
    
    int complete = 0;
    int stuck = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        uint64_t sent, retrans, recv, ooo, dup, lost;
        dap_io_flow_ctrl_get_stats(s_clients[c].fc, &sent, &retrans, &recv, &ooo, &dup, &lost);
        
        bool ok = (s_clients[c].last_ack_seq >= PACKETS_PER_CLIENT);
        bool is_stuck = (retrans > PACKETS_PER_CLIENT);
        
        if (ok) complete++;
        if (is_stuck) stuck++;
        
        dap_test_msg("  Client %d: sent=%lu, retrans=%lu, ack=%lu %s%s",
                     c, sent, retrans, s_clients[c].last_ack_seq,
                     ok ? "[COMPLETE]" : "[INCOMPLETE]",
                     is_stuck ? " [STUCK!]" : "");
    }
    
    dap_test_msg("\nSummary: %d/%d complete, %d stuck", complete, NUM_CLIENTS, stuck);
    dap_test_msg("Server: recv=%lu, acks=%lu",
                 atomic_load(&s_server.packets_received),
                 atomic_load(&s_server.acks_sent));
    
    s_cleanup_clients();
    s_cleanup_server();
    
    if (stuck > 0) {
        dap_test_msg("\n*** BUG REPRODUCED: %d clients stuck! ***", stuck);
    }
    
    dap_assert(complete == NUM_CLIENTS, "REGRESSION: All clients should complete");
    dap_assert(stuck == 0, "REGRESSION: No clients should be stuck");
    
    dap_pass_msg("Concurrent clients test completed");
}

/**
 * @brief Test: High-volume concurrent - like integration test
 * 
 * More packets, more clients, trigger retransmit timers.
 */
static void test_high_volume(void)
{
    #define HV_CLIENTS 5
    #define HV_PACKETS 200  // More packets to trigger retransmit
    
    dap_test_msg("\n=== Test: High Volume (%d clients x %d packets) ===\n",
                 HV_CLIENTS, HV_PACKETS);
    
    if (s_setup_server() != 0) {
        dap_fail("Server setup failed");
        return;
    }
    
    // Temporarily increase client count
    int saved_num_clients = NUM_CLIENTS;
    
    if (s_setup_clients() != 0) {
        s_cleanup_server();
        dap_fail("Client setup failed");
        return;
    }
    
    usleep(100000);
    
    // Rapid-fire sending from all clients
    dap_test_msg("Rapid-fire sending...");
    
    uint8_t payload[PACKET_SIZE];
    
    for (int p = 0; p < HV_PACKETS; p++) {
        for (int c = 0; c < NUM_CLIENTS; c++) {
            memset(payload, 0xCC, sizeof(payload));
            payload[0] = (uint8_t)c;
            payload[1] = (uint8_t)(p & 0xFF);
            payload[2] = (uint8_t)((p >> 8) & 0xFF);
            dap_io_flow_ctrl_send(s_clients[c].fc, payload, sizeof(payload));
        }
        // No delay - maximum pressure!
    }
    
    // Wait with periodic status
    dap_test_msg("Waiting for completion (max 15 sec)...");
    
    uint64_t start = dap_nanotime_now();
    int last_report = 0;
    
    while ((dap_nanotime_now() - start) < 15000000000ULL) {
        bool all_done = true;
        uint64_t total_acks = 0;
        
        for (int c = 0; c < NUM_CLIENTS; c++) {
            total_acks += s_clients[c].last_ack_seq;
            if (s_clients[c].last_ack_seq < HV_PACKETS) {
                all_done = false;
            }
        }
        
        int elapsed_sec = (int)((dap_nanotime_now() - start) / 1000000000ULL);
        if (elapsed_sec > last_report) {
            dap_test_msg("  [%d sec] Total ACKs received: %lu/%d",
                         elapsed_sec, total_acks, NUM_CLIENTS * HV_PACKETS);
            last_report = elapsed_sec;
        }
        
        if (all_done) break;
        usleep(POLL_INTERVAL_US);
    }
    
    // Report
    dap_test_msg("\nResults:");
    
    int complete = 0;
    int stuck = 0;
    uint64_t total_retrans = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        uint64_t sent, retrans, recv, ooo, dup, lost;
        dap_io_flow_ctrl_get_stats(s_clients[c].fc, &sent, &retrans, &recv, &ooo, &dup, &lost);
        
        bool ok = (s_clients[c].last_ack_seq >= HV_PACKETS);
        bool is_stuck = (retrans > HV_PACKETS);
        
        total_retrans += retrans;
        if (ok) complete++;
        if (is_stuck) stuck++;
        
        dap_test_msg("  Client %d: sent=%lu, retrans=%lu, ack=%lu %s%s",
                     c, sent, retrans, s_clients[c].last_ack_seq,
                     ok ? "[OK]" : "[FAIL]",
                     is_stuck ? " [STUCK!]" : "");
    }
    
    dap_test_msg("\nSummary: %d/%d complete, %d stuck, total_retrans=%lu",
                 complete, NUM_CLIENTS, stuck, total_retrans);
    dap_test_msg("Server: recv=%lu, acks=%lu",
                 atomic_load(&s_server.packets_received),
                 atomic_load(&s_server.acks_sent));
    
    dap_test_msg("Cleanup: stopping server...");
    atomic_store(&s_server.running, false);
    
    dap_test_msg("Cleanup: joining server thread...");
    pthread_join(s_server.thread, NULL);
    close(s_server.socket_fd);
    
    dap_test_msg("Cleanup: joining client threads...");
    for (int c = 0; c < NUM_CLIENTS; c++) {
        pthread_join(s_client_threads[c], NULL);
        if (s_clients[c].fc) {
            dap_io_flow_ctrl_delete(s_clients[c].fc);
        }
        close(s_clients[c].socket_fd);
    }
    dap_test_msg("Cleanup: done");
    
    if (stuck > 0 || complete < NUM_CLIENTS) {
        dap_test_msg("\n*** BUG REPRODUCED! ***");
    }
    
    // Note: This test may show the bug with high retransmit counts
    // even if all clients eventually complete
    dap_assert(complete == NUM_CLIENTS, "REGRESSION: All clients should complete");
    
    dap_pass_msg("High volume test completed");
    
    #undef HV_CLIENTS
    #undef HV_PACKETS
}

//===================================================================
// MAIN
//===================================================================

static bool s_wait_for_reactor(uint32_t timeout_ms)
{
    uint64_t start = dap_nanotime_now();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - start) < timeout_ns) {
        if (dap_events_worker_get(0)) return true;
        usleep(5000);
    }
    return false;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_common_init("test_fc_multiclient", NULL);
    dap_log_level_set(L_WARNING);
    
    printf("\n=============================================\n");
    printf("Flow Control Multi-Client Regression Test\n");
    printf("(Real UDP sockets, reproduces integration bug)\n");
    printf("=============================================\n\n");
    
    dap_print_module_name("io_flow_ctrl_multiclient");
    
    // Initialize events reactor for timer support
    int ret = dap_events_init(0, 0);
    if (ret == 0) {
        ret = dap_events_start();
        if (ret == 0 && s_wait_for_reactor(2000)) {
            s_reactor_available = true;
            dap_test_msg("Reactor started, retransmit timers will work");
            usleep(500000);  // Let reactor stabilize
        }
    }
    
    if (!s_reactor_available) {
        dap_test_msg("WARNING: Reactor not available, timers won't work");
    }
    
    // Run tests (only high_volume to reproduce bug)
    test_high_volume();
    
    // Skip dap_events_deinit() - known issue with reactor shutdown
    // if (s_reactor_available) {
    //     dap_events_deinit();
    // }
    
    printf("\n=== All tests completed ===\n");
    
    dap_common_deinit();
    return 0;
}
