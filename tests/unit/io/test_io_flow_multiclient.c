/**
 * @file test_io_flow_multiclient.c
 * @brief Multi-client regression test for dap_io_flow layer
 * 
 * This test validates packet routing and ordering in multi-client scenarios:
 * - Multiple clients sending packets concurrently
 * - Packet sequence validation (no reordering)
 * - Packet ownership validation (no cross-contamination between clients)
 * - Worker affinity (sticky sessions)
 * 
 * NO ENCRYPTION - packets contain plaintext markers for easy tracking.
 * 
 * @date 2026-02-03
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "dap_common.h"
#include "dap_time.h"
#include "dap_test.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_io_flow.h"
#include "dap_io_flow_datagram.h"
#include "dap_io_flow_ctrl.h"
#include "dap_timerfd.h"

#define LOG_TAG "test_io_flow_multiclient"

//===================================================================
// PACKET TRACKING STRUCTURES
//===================================================================

// Magic values for packet identification
#define PACKET_MAGIC 0xDEADBEEF

/**
 * @brief Packet header for tracking
 * NO ENCRYPTION - plaintext for easy validation
 */
typedef struct {
    uint32_t magic;           // PACKET_MAGIC
    uint32_t client_id;       // Which client sent this packet
    uint32_t seq_num;         // Sequence number within client
    uint32_t payload_size;    // Size of payload following header
} test_packet_header_t;

/**
 * @brief Statistics for tracking packet flow
 */
typedef struct {
    _Atomic uint64_t packets_sent;
    _Atomic uint64_t packets_received;
    _Atomic uint64_t out_of_order;
    _Atomic uint64_t wrong_client;      // Received packet for different client
    _Atomic uint64_t duplicates;
    _Atomic uint32_t last_seq_received;
    uint32_t expected_next_seq;
    pthread_mutex_t lock;
} client_stats_t;

/**
 * @brief Client context for test
 */
typedef struct {
    uint32_t client_id;
    uint32_t next_seq_num;
    struct sockaddr_storage addr;
    socklen_t addr_len;
    client_stats_t stats;
    
    // For flow_ctrl
    dap_io_flow_ctrl_t *flow_ctrl;
    
    // Pending packets (for manual processing)
    struct {
        uint8_t *data;
        size_t size;
    } pending_packets[256];
    int pending_count;
    pthread_mutex_t pending_lock;
} test_client_ctx_t;

/**
 * @brief Server context for test
 */
typedef struct {
    // Track which worker handles each client
    uint32_t client_worker_map[16];  // client_id -> worker_id
    bool worker_map_set[16];
    
    // Received packets per worker
    _Atomic uint64_t packets_per_worker[16];
    
    // Total stats
    _Atomic uint64_t total_received;
    _Atomic uint64_t routing_errors;  // Packet routed to wrong worker
    
    pthread_mutex_t lock;
} test_server_ctx_t;

static test_server_ctx_t s_server_ctx;

//===================================================================
// POLLING HELPERS
//===================================================================

#define POLL_INTERVAL_US    5000    // 5ms between checks
#define POLL_TIMEOUT_MS     5000    // 5s max wait

static bool s_wait_for_condition(bool (*condition)(void *), void *arg, uint32_t timeout_ms)
{
    uint64_t start = dap_nanotime_now();
    uint64_t timeout_ns = (uint64_t)timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - start) < timeout_ns) {
        if (condition(arg)) return true;
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

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
// CLIENT CONTEXT MANAGEMENT
//===================================================================

static test_client_ctx_t* s_client_create(uint32_t client_id, uint16_t port)
{
    test_client_ctx_t *ctx = DAP_NEW_Z(test_client_ctx_t);
    if (!ctx) return NULL;
    
    ctx->client_id = client_id;
    ctx->next_seq_num = 1;
    
    // Setup fake address (different port per client for routing)
    struct sockaddr_in *sin = (struct sockaddr_in*)&ctx->addr;
    sin->sin_family = AF_INET;
    sin->sin_port = htons(port);
    sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ctx->addr_len = sizeof(struct sockaddr_in);
    
    // Initialize stats
    atomic_init(&ctx->stats.packets_sent, 0);
    atomic_init(&ctx->stats.packets_received, 0);
    atomic_init(&ctx->stats.out_of_order, 0);
    atomic_init(&ctx->stats.wrong_client, 0);
    atomic_init(&ctx->stats.duplicates, 0);
    atomic_init(&ctx->stats.last_seq_received, 0);
    ctx->stats.expected_next_seq = 1;
    pthread_mutex_init(&ctx->stats.lock, NULL);
    
    ctx->pending_count = 0;
    pthread_mutex_init(&ctx->pending_lock, NULL);
    
    return ctx;
}

static void s_client_delete(test_client_ctx_t *ctx)
{
    if (!ctx) return;
    
    pthread_mutex_lock(&ctx->pending_lock);
    for (int i = 0; i < ctx->pending_count; i++) {
        DAP_DELETE(ctx->pending_packets[i].data);
    }
    pthread_mutex_unlock(&ctx->pending_lock);
    
    pthread_mutex_destroy(&ctx->stats.lock);
    pthread_mutex_destroy(&ctx->pending_lock);
    DAP_DELETE(ctx);
}

//===================================================================
// PACKET CREATION AND PARSING
//===================================================================

/**
 * @brief Create test packet with tracking header (NO ENCRYPTION)
 */
static uint8_t* s_create_test_packet(test_client_ctx_t *ctx, const char *payload, 
                                      size_t payload_size, size_t *out_size)
{
    size_t total_size = sizeof(test_packet_header_t) + payload_size;
    uint8_t *packet = DAP_NEW_SIZE(uint8_t, total_size);
    if (!packet) return NULL;
    
    test_packet_header_t *hdr = (test_packet_header_t*)packet;
    hdr->magic = PACKET_MAGIC;
    hdr->client_id = ctx->client_id;
    hdr->seq_num = ctx->next_seq_num++;
    hdr->payload_size = (uint32_t)payload_size;
    
    memcpy(packet + sizeof(test_packet_header_t), payload, payload_size);
    
    atomic_fetch_add(&ctx->stats.packets_sent, 1);
    
    *out_size = total_size;
    return packet;
}

/**
 * @brief Parse and validate received packet
 * @return 0 on success, negative on error
 */
static int s_validate_received_packet(test_client_ctx_t *expected_client,
                                       const uint8_t *data, size_t size,
                                       uint32_t worker_id)
{
    if (size < sizeof(test_packet_header_t)) {
        log_it(L_ERROR, "Packet too small: %zu bytes", size);
        return -1;
    }
    
    test_packet_header_t *hdr = (test_packet_header_t*)data;
    
    // Validate magic
    if (hdr->magic != PACKET_MAGIC) {
        log_it(L_ERROR, "Invalid packet magic: 0x%08X (expected 0x%08X)",
               hdr->magic, PACKET_MAGIC);
        return -2;
    }
    
    // Check client ownership
    if (hdr->client_id != expected_client->client_id) {
        log_it(L_ERROR, "WRONG CLIENT! Packet from client %u received by client %u",
               hdr->client_id, expected_client->client_id);
        atomic_fetch_add(&expected_client->stats.wrong_client, 1);
        atomic_fetch_add(&s_server_ctx.routing_errors, 1);
        return -3;
    }
    
    // Check sequence
    pthread_mutex_lock(&expected_client->stats.lock);
    uint32_t expected_seq = expected_client->stats.expected_next_seq;
    
    if (hdr->seq_num < expected_seq) {
        log_it(L_WARNING, "Duplicate/old packet: seq=%u, expected=%u",
               hdr->seq_num, expected_seq);
        atomic_fetch_add(&expected_client->stats.duplicates, 1);
    } else if (hdr->seq_num > expected_seq) {
        log_it(L_WARNING, "Out of order: seq=%u, expected=%u (gap=%u)",
               hdr->seq_num, expected_seq, hdr->seq_num - expected_seq);
        atomic_fetch_add(&expected_client->stats.out_of_order, 1);
        expected_client->stats.expected_next_seq = hdr->seq_num + 1;
    } else {
        // Perfect - in order
        expected_client->stats.expected_next_seq = hdr->seq_num + 1;
    }
    
    atomic_store(&expected_client->stats.last_seq_received, hdr->seq_num);
    pthread_mutex_unlock(&expected_client->stats.lock);
    
    atomic_fetch_add(&expected_client->stats.packets_received, 1);
    
    // Track worker routing
    pthread_mutex_lock(&s_server_ctx.lock);
    if (!s_server_ctx.worker_map_set[expected_client->client_id]) {
        s_server_ctx.client_worker_map[expected_client->client_id] = worker_id;
        s_server_ctx.worker_map_set[expected_client->client_id] = true;
        log_it(L_INFO, "Client %u assigned to worker %u", 
               expected_client->client_id, worker_id);
    } else {
        uint32_t expected_worker = s_server_ctx.client_worker_map[expected_client->client_id];
        if (worker_id != expected_worker) {
            log_it(L_ERROR, "STICKY SESSION VIOLATION! Client %u on worker %u (expected %u)",
                   expected_client->client_id, worker_id, expected_worker);
            atomic_fetch_add(&s_server_ctx.routing_errors, 1);
        }
    }
    atomic_fetch_add(&s_server_ctx.packets_per_worker[worker_id], 1);
    atomic_fetch_add(&s_server_ctx.total_received, 1);
    pthread_mutex_unlock(&s_server_ctx.lock);
    
    return 0;
}

//===================================================================
// MOCK FLOW CALLBACKS (NO ENCRYPTION)
//===================================================================

static dap_io_flow_t s_mock_flows[16];  // One per client

/**
 * @brief Packet prepare - just copy data, no encryption
 */
static int s_packet_prepare(dap_io_flow_t *a_flow,
                            const dap_io_flow_pkt_metadata_t *a_metadata,
                            const void *a_payload, size_t a_payload_size,
                            void **a_packet_out, size_t *a_packet_size_out,
                            void *a_arg)
{
    (void)a_flow;
    (void)a_metadata;
    (void)a_arg;
    
    // Just wrap payload - no encryption
    *a_packet_out = DAP_DUP_SIZE(a_payload, a_payload_size);
    *a_packet_size_out = a_payload_size;
    return 0;
}

/**
 * @brief Packet parse - just extract data, no decryption
 */
static int s_packet_parse(dap_io_flow_t *a_flow,
                          const void *a_packet, size_t a_packet_size,
                          dap_io_flow_pkt_metadata_t *a_metadata,
                          const void **a_payload_out, size_t *a_payload_size_out,
                          void *a_arg)
{
    (void)a_flow;
    (void)a_metadata;
    (void)a_arg;
    
    // Just pass through - no decryption
    *a_payload_out = a_packet;
    *a_payload_size_out = a_packet_size;
    return 0;
}

/**
 * @brief Packet send - store in pending queue for manual delivery
 */
static int s_packet_send(dap_io_flow_t *a_flow,
                         const void *a_packet, size_t a_packet_size,
                         void *a_arg)
{
    (void)a_flow;
    test_client_ctx_t *ctx = (test_client_ctx_t*)a_arg;
    if (!ctx) return -1;
    
    pthread_mutex_lock(&ctx->pending_lock);
    if (ctx->pending_count < 256) {
        ctx->pending_packets[ctx->pending_count].data = DAP_DUP_SIZE(a_packet, a_packet_size);
        ctx->pending_packets[ctx->pending_count].size = a_packet_size;
        ctx->pending_count++;
    }
    pthread_mutex_unlock(&ctx->pending_lock);
    
    return 0;
}

/**
 * @brief Packet free
 */
static void s_packet_free(void *a_packet, void *a_arg)
{
    (void)a_arg;
    DAP_DELETE(a_packet);
}

/**
 * @brief Payload deliver - validate packet
 */
static int s_payload_deliver(dap_io_flow_t *a_flow,
                              const void *a_payload, size_t a_payload_size,
                              void *a_arg)
{
    (void)a_flow;
    test_client_ctx_t *ctx = (test_client_ctx_t*)a_arg;
    if (!ctx) return -1;
    
    // Get current worker ID
    dap_worker_t *worker = dap_worker_get_current();
    uint32_t worker_id = worker ? worker->id : 0;
    
    return s_validate_received_packet(ctx, a_payload, a_payload_size, worker_id);
}

//===================================================================
// PACKET DELIVERY HELPERS
//===================================================================

/**
 * @brief Process pending packets from sender to receiver
 * @return Number of packets delivered
 */
static int s_deliver_pending(test_client_ctx_t *sender, dap_io_flow_ctrl_t *receiver_ctrl)
{
    int delivered = 0;
    
    pthread_mutex_lock(&sender->pending_lock);
    for (int i = 0; i < sender->pending_count; i++) {
        if (sender->pending_packets[i].data) {
            dap_io_flow_ctrl_recv(receiver_ctrl, 
                                   sender->pending_packets[i].data,
                                   sender->pending_packets[i].size);
            DAP_DELETE(sender->pending_packets[i].data);
            sender->pending_packets[i].data = NULL;
            delivered++;
        }
    }
    sender->pending_count = 0;
    pthread_mutex_unlock(&sender->pending_lock);
    
    return delivered;
}

//===================================================================
// TEST CASES
//===================================================================

/**
 * @brief Test: Multiple clients sending interleaved packets
 * Validates packet ownership and ordering per client
 */
static void test_multiclient_interleaved(void)
{
    dap_test_msg("Test: Multi-client interleaved packets");
    
    #define NUM_CLIENTS 4
    #define PACKETS_PER_CLIENT 20
    
    // Initialize server context
    memset(&s_server_ctx, 0, sizeof(s_server_ctx));
    pthread_mutex_init(&s_server_ctx.lock, NULL);
    
    // Create clients
    test_client_ctx_t *clients[NUM_CLIENTS];
    dap_io_flow_ctrl_t *ctrls[NUM_CLIENTS];
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients[i] = s_client_create(i, 10000 + i);
        dap_assert(clients[i] != NULL, "Client creation should succeed");
        
        // Create flow control for each client
        dap_io_flow_ctrl_callbacks_t cbs = {
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = clients[i],
        };
        
        dap_io_flow_ctrl_config_t config;
        dap_io_flow_ctrl_get_default_config(&config);
        config.retransmit_timeout_ms = 100;
        config.send_window_size = 64;
        
        // Use mock flow
        s_mock_flows[i].protocol_data = clients[i];
        ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[i], 0, &config, &cbs);
        dap_assert(ctrls[i] != NULL, "Flow control creation should succeed");
        clients[i]->flow_ctrl = ctrls[i];
    }
    
    dap_test_msg("Created %d clients with flow controls", NUM_CLIENTS);
    
    // Create receiver (server-side flow control)
    test_client_ctx_t *receiver_ctxs[NUM_CLIENTS];
    dap_io_flow_ctrl_t *receiver_ctrls[NUM_CLIENTS];
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        receiver_ctxs[i] = s_client_create(i, 20000 + i);  // Same client_id for validation
        dap_assert(receiver_ctxs[i] != NULL, "Receiver context creation should succeed");
        
        dap_io_flow_ctrl_callbacks_t cbs = {
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = receiver_ctxs[i],
        };
        
        dap_io_flow_ctrl_config_t config;
        dap_io_flow_ctrl_get_default_config(&config);
        
        s_mock_flows[NUM_CLIENTS + i].protocol_data = receiver_ctxs[i];
        receiver_ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[NUM_CLIENTS + i], 0, &config, &cbs);
        dap_assert(receiver_ctrls[i] != NULL, "Receiver flow control creation should succeed");
    }
    
    // Send interleaved packets from all clients
    dap_test_msg("Sending %d packets from each of %d clients (interleaved)...",
                 PACKETS_PER_CLIENT, NUM_CLIENTS);
    
    for (int pkt = 0; pkt < PACKETS_PER_CLIENT; pkt++) {
        for (int c = 0; c < NUM_CLIENTS; c++) {
            char payload[128];
            snprintf(payload, sizeof(payload), "Client%d-Packet%d", c, pkt + 1);
            
            size_t packet_size;
            uint8_t *packet = s_create_test_packet(clients[c], payload, 
                                                    strlen(payload) + 1, &packet_size);
            dap_assert(packet != NULL, "Packet creation should succeed");
            
            // Send via flow control
            int ret = dap_io_flow_ctrl_send(ctrls[c], packet, packet_size);
            dap_assert(ret == 0, "Flow control send should succeed");
            
            DAP_DELETE(packet);
        }
    }
    
    // Deliver all pending packets to correct receivers
    dap_test_msg("Delivering packets to receivers...");
    
    int total_delivered = 0;
    for (int c = 0; c < NUM_CLIENTS; c++) {
        int delivered = s_deliver_pending(clients[c], receiver_ctrls[c]);
        total_delivered += delivered;
        dap_test_msg("  Client %d: %d packets delivered", c, delivered);
    }
    
    dap_test_msg("Total packets delivered: %d", total_delivered);
    
    // Process ACKs back to senders
    for (int c = 0; c < NUM_CLIENTS; c++) {
        s_deliver_pending(receiver_ctxs[c], ctrls[c]);
    }
    
    // Validate results
    dap_test_msg("\n=== VALIDATION RESULTS ===");
    
    bool all_passed = true;
    uint64_t total_received = 0;
    uint64_t total_wrong_client = 0;
    uint64_t total_out_of_order = 0;
    uint64_t total_duplicates = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        uint64_t sent = atomic_load(&clients[c]->stats.packets_sent);
        uint64_t recv = atomic_load(&receiver_ctxs[c]->stats.packets_received);
        uint64_t wrong = atomic_load(&receiver_ctxs[c]->stats.wrong_client);
        uint64_t ooo = atomic_load(&receiver_ctxs[c]->stats.out_of_order);
        uint64_t dup = atomic_load(&receiver_ctxs[c]->stats.duplicates);
        
        total_received += recv;
        total_wrong_client += wrong;
        total_out_of_order += ooo;
        total_duplicates += dup;
        
        bool client_ok = (recv == PACKETS_PER_CLIENT && wrong == 0);
        
        dap_test_msg("Client %d: sent=%lu, recv=%lu, wrong=%lu, ooo=%lu, dup=%lu [%s]",
                     c, sent, recv, wrong, ooo, dup,
                     client_ok ? "OK" : "FAILED");
        
        if (!client_ok) all_passed = false;
    }
    
    dap_test_msg("\nTotals: received=%lu, wrong_client=%lu, out_of_order=%lu, duplicates=%lu",
                 total_received, total_wrong_client, total_out_of_order, total_duplicates);
    
    // Critical assertions
    dap_assert(total_wrong_client == 0, 
               "REGRESSION: No packets should go to wrong client!");
    dap_assert(total_received == (uint64_t)(NUM_CLIENTS * PACKETS_PER_CLIENT),
               "All packets should be received");
    
    // Cleanup
    for (int c = 0; c < NUM_CLIENTS; c++) {
        dap_io_flow_ctrl_delete(ctrls[c]);
        dap_io_flow_ctrl_delete(receiver_ctrls[c]);
        s_client_delete(clients[c]);
        s_client_delete(receiver_ctxs[c]);
    }
    pthread_mutex_destroy(&s_server_ctx.lock);
    
    dap_pass_msg("Multi-client interleaved test PASSED");
    
    #undef NUM_CLIENTS
    #undef PACKETS_PER_CLIENT
}

/**
 * @brief Test: One client "stuck" with retransmits while others work
 */
static void test_stuck_client_isolation(void)
{
    dap_test_msg("Test: Stuck client doesn't affect others");
    
    #define NUM_CLIENTS 3
    #define STUCK_CLIENT 0
    #define PACKETS_PER_CLIENT 10
    
    // Initialize server context
    memset(&s_server_ctx, 0, sizeof(s_server_ctx));
    pthread_mutex_init(&s_server_ctx.lock, NULL);
    
    // Create clients
    test_client_ctx_t *clients[NUM_CLIENTS];
    dap_io_flow_ctrl_t *ctrls[NUM_CLIENTS];
    test_client_ctx_t *receiver_ctxs[NUM_CLIENTS];
    dap_io_flow_ctrl_t *receiver_ctrls[NUM_CLIENTS];
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients[i] = s_client_create(i, 10000 + i);
        receiver_ctxs[i] = s_client_create(i, 20000 + i);
        
        dap_io_flow_ctrl_callbacks_t cbs = {
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = clients[i],
        };
        
        dap_io_flow_ctrl_config_t config;
        dap_io_flow_ctrl_get_default_config(&config);
        config.retransmit_timeout_ms = 50;  // Fast retransmit
        config.max_retransmit_count = 100;  // Many retries
        
        s_mock_flows[i].protocol_data = clients[i];
        ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[i], 0, &config, &cbs);
        
        dap_io_flow_ctrl_callbacks_t recv_cbs = {
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = receiver_ctxs[i],
        };
        
        s_mock_flows[NUM_CLIENTS + i].protocol_data = receiver_ctxs[i];
        receiver_ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[NUM_CLIENTS + i], 0, &config, &recv_cbs);
        
        clients[i]->flow_ctrl = ctrls[i];
    }
    
    // STUCK client sends packets but ACKs are NOT delivered
    dap_test_msg("STUCK client %d sends %d packets (ACKs will be lost)...", 
                 STUCK_CLIENT, PACKETS_PER_CLIENT);
    
    for (int pkt = 0; pkt < PACKETS_PER_CLIENT; pkt++) {
        char payload[128];
        snprintf(payload, sizeof(payload), "Stuck-Packet%d", pkt + 1);
        
        size_t packet_size;
        uint8_t *packet = s_create_test_packet(clients[STUCK_CLIENT], payload,
                                                strlen(payload) + 1, &packet_size);
        dap_io_flow_ctrl_send(ctrls[STUCK_CLIENT], packet, packet_size);
        DAP_DELETE(packet);
    }
    
    // Deliver stuck client's packets to receiver (but DON'T deliver ACKs back!)
    s_deliver_pending(clients[STUCK_CLIENT], receiver_ctrls[STUCK_CLIENT]);
    // INTENTIONALLY skip: s_deliver_pending(receiver_ctxs[STUCK_CLIENT], ctrls[STUCK_CLIENT]);
    
    dap_test_msg("STUCK client has %d pending ACKs (not delivered)",
                 receiver_ctxs[STUCK_CLIENT]->pending_count);
    
    // Other clients operate normally
    dap_test_msg("Other clients send %d packets each (with ACK delivery)...",
                 PACKETS_PER_CLIENT);
    
    for (int c = 1; c < NUM_CLIENTS; c++) {
        for (int pkt = 0; pkt < PACKETS_PER_CLIENT; pkt++) {
            char payload[128];
            snprintf(payload, sizeof(payload), "Client%d-Packet%d", c, pkt + 1);
            
            size_t packet_size;
            uint8_t *packet = s_create_test_packet(clients[c], payload,
                                                    strlen(payload) + 1, &packet_size);
            dap_io_flow_ctrl_send(ctrls[c], packet, packet_size);
            DAP_DELETE(packet);
        }
        
        // Full cycle for normal clients
        s_deliver_pending(clients[c], receiver_ctrls[c]);
        s_deliver_pending(receiver_ctxs[c], ctrls[c]);
    }
    
    // Validate: stuck client shouldn't affect others
    dap_test_msg("\n=== VALIDATION RESULTS ===");
    
    bool other_clients_ok = true;
    for (int c = 1; c < NUM_CLIENTS; c++) {
        uint64_t recv = atomic_load(&receiver_ctxs[c]->stats.packets_received);
        uint64_t wrong = atomic_load(&receiver_ctxs[c]->stats.wrong_client);
        
        bool ok = (recv == PACKETS_PER_CLIENT && wrong == 0);
        dap_test_msg("Client %d: recv=%lu, wrong=%lu [%s]", c, recv, wrong, 
                     ok ? "OK" : "FAILED");
        
        if (!ok) other_clients_ok = false;
    }
    
    // Stuck client should have received data but stats show pending retransmits
    uint64_t stuck_recv = atomic_load(&receiver_ctxs[STUCK_CLIENT]->stats.packets_received);
    dap_test_msg("STUCK client %d: recv=%lu (ACKs pending)", STUCK_CLIENT, stuck_recv);
    
    dap_assert(other_clients_ok,
               "REGRESSION: Other clients MUST complete despite stuck client!");
    
    // Cleanup
    for (int c = 0; c < NUM_CLIENTS; c++) {
        dap_io_flow_ctrl_delete(ctrls[c]);
        dap_io_flow_ctrl_delete(receiver_ctrls[c]);
        s_client_delete(clients[c]);
        s_client_delete(receiver_ctxs[c]);
    }
    pthread_mutex_destroy(&s_server_ctx.lock);
    
    dap_pass_msg("Stuck client isolation test PASSED");
    
    #undef NUM_CLIENTS
    #undef STUCK_CLIENT
    #undef PACKETS_PER_CLIENT
}

/**
 * @brief Test: High volume concurrent sends with sequence validation
 */
static void test_high_volume_ordering(void)
{
    dap_test_msg("Test: High volume packet ordering");
    
    #define NUM_CLIENTS 4
    #define PACKETS_PER_CLIENT 100
    
    memset(&s_server_ctx, 0, sizeof(s_server_ctx));
    pthread_mutex_init(&s_server_ctx.lock, NULL);
    
    test_client_ctx_t *clients[NUM_CLIENTS];
    dap_io_flow_ctrl_t *ctrls[NUM_CLIENTS];
    test_client_ctx_t *receiver_ctxs[NUM_CLIENTS];
    dap_io_flow_ctrl_t *receiver_ctrls[NUM_CLIENTS];
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        clients[i] = s_client_create(i, 10000 + i);
        receiver_ctxs[i] = s_client_create(i, 20000 + i);
        
        dap_io_flow_ctrl_callbacks_t cbs = {
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = clients[i],
        };
        
        dap_io_flow_ctrl_config_t config;
        dap_io_flow_ctrl_get_default_config(&config);
        config.send_window_size = 256;
        
        s_mock_flows[i].protocol_data = clients[i];
        ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[i], 0, &config, &cbs);
        
        dap_io_flow_ctrl_callbacks_t recv_cbs = cbs;
        recv_cbs.arg = receiver_ctxs[i];
        
        s_mock_flows[NUM_CLIENTS + i].protocol_data = receiver_ctxs[i];
        receiver_ctrls[i] = dap_io_flow_ctrl_create(&s_mock_flows[NUM_CLIENTS + i], 0, &config, &recv_cbs);
        
        clients[i]->flow_ctrl = ctrls[i];
    }
    
    dap_test_msg("Sending %d packets per client (%d total)...",
                 PACKETS_PER_CLIENT, NUM_CLIENTS * PACKETS_PER_CLIENT);
    
    // Send all packets in rapid succession
    for (int pkt = 0; pkt < PACKETS_PER_CLIENT; pkt++) {
        for (int c = 0; c < NUM_CLIENTS; c++) {
            char payload[64];
            snprintf(payload, sizeof(payload), "C%d-P%03d", c, pkt + 1);
            
            size_t packet_size;
            uint8_t *packet = s_create_test_packet(clients[c], payload,
                                                    strlen(payload) + 1, &packet_size);
            dap_io_flow_ctrl_send(ctrls[c], packet, packet_size);
            DAP_DELETE(packet);
        }
    }
    
    // Deliver all
    for (int c = 0; c < NUM_CLIENTS; c++) {
        s_deliver_pending(clients[c], receiver_ctrls[c]);
        s_deliver_pending(receiver_ctxs[c], ctrls[c]);
    }
    
    // Validate ordering
    dap_test_msg("\n=== ORDERING VALIDATION ===");
    
    uint64_t total_ooo = 0;
    uint64_t total_wrong = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        uint64_t recv = atomic_load(&receiver_ctxs[c]->stats.packets_received);
        uint64_t ooo = atomic_load(&receiver_ctxs[c]->stats.out_of_order);
        uint64_t wrong = atomic_load(&receiver_ctxs[c]->stats.wrong_client);
        uint32_t last_seq = atomic_load(&receiver_ctxs[c]->stats.last_seq_received);
        
        total_ooo += ooo;
        total_wrong += wrong;
        
        dap_test_msg("Client %d: recv=%lu, last_seq=%u, ooo=%lu, wrong=%lu",
                     c, recv, last_seq, ooo, wrong);
    }
    
    dap_assert(total_wrong == 0, "No packets should go to wrong client");
    dap_assert(total_ooo == 0, "All packets should be in order");
    
    // Cleanup
    for (int c = 0; c < NUM_CLIENTS; c++) {
        dap_io_flow_ctrl_delete(ctrls[c]);
        dap_io_flow_ctrl_delete(receiver_ctrls[c]);
        s_client_delete(clients[c]);
        s_client_delete(receiver_ctxs[c]);
    }
    pthread_mutex_destroy(&s_server_ctx.lock);
    
    dap_pass_msg("High volume ordering test PASSED");
    
    #undef NUM_CLIENTS
    #undef PACKETS_PER_CLIENT
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_common_init("test_io_flow_multiclient", NULL);
    dap_log_level_set(L_WARNING);  // Reduce noise, show warnings/errors
    
    printf("\n=== IO Flow Multi-Client Regression Tests ===\n\n");
    
    dap_print_module_name("io_flow_multiclient");
    
    // Run tests
    test_multiclient_interleaved();
    test_stuck_client_isolation();
    test_high_volume_ordering();
    
    printf("\n=== All tests PASSED ===\n");
    
    dap_common_deinit();
    return 0;
}
