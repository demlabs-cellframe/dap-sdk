/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025
 * All rights reserved.
 *
 * Integration test for IO Flow tiers using DAP SDK
 * Tests sticky sessions via dap_io_flow_server with real reactor
 * All components use DAP SDK: events, workers, esockets, timers
 * 
 * Each packet is tracked individually to detect any violation of sticky sessions.
 * 
 * This file is only compiled on Linux (see CMakeLists.txt)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdatomic.h>
#include <pthread.h>

#include "dap_test.h"
#include "dap_test_async.h"
#include "dap_common.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_events_socket.h"
#include "dap_timerfd.h"
#include "dap_io_flow.h"
#include "dap_io_flow_cbpf.h"
#include "dap_io_flow_ebpf.h"

#define LOG_TAG "test_flow_tiers"

// Test configuration
#define NUM_WORKERS         4       // Number of worker threads
#define NUM_CLIENTS         16      // Number of client esockets
#define PACKETS_PER_CLIENT  50      // Packets to send from each client
#define PACKET_SIZE         64      // Size of test packet
#define TEST_PORT           0       // Let kernel choose port

// Special value: packet not yet received
#define WORKER_ID_NOT_RECEIVED  0xFF

// Packet reception thresholds (percentage)
#define COMPLETION_THRESHOLD_PCT    95  // Minimum % to consider test complete
#define PASS_THRESHOLD_PCT          90  // Minimum % to pass (allowing some UDP loss)

// Timeouts
#define WORKER_STARTUP_DELAY_MS     500  // Time to wait for workers to initialize
#define TEST_TIMEOUT_SEC            30   // Maximum test duration

// Test packet structure - each packet is uniquely identified
typedef struct {
    uint32_t client_id;
    uint32_t packet_num;
    uint32_t magic;
    uint32_t unique_id;  // = client_id * PACKETS_PER_CLIENT + packet_num
    char payload[PACKET_SIZE - 16];
} test_packet_t;

#define PACKET_MAGIC 0xDEADBEEF

// Per-packet tracking: which worker received each packet
// packet_worker[client_id][packet_num] = worker_id that received it
typedef struct {
    uint8_t packet_worker[NUM_CLIENTS][PACKETS_PER_CLIENT];
    pthread_mutex_t lock;
    
    // First worker that received packet from each client (expected worker)
    int8_t client_expected_worker[NUM_CLIENTS];  // -1 = not yet determined
    
    // Violation tracking
    atomic_uint violations_count;
    atomic_uint total_received;
} packet_tracker_t;

// Client context (for esocket)
typedef struct {
    int client_id;
    uint32_t packets_sent;
    struct sockaddr_storage server_addr;
    socklen_t server_addr_len;
} client_ctx_t;

// Violation info structure for deferred logging (outside mutex)
typedef struct {
    enum { VIOLATION_NONE, VIOLATION_DUPLICATE, VIOLATION_STICKY } type;
    uint32_t client_id;
    uint32_t packet_num;
    uint32_t worker_id;
    uint32_t prev_worker_id;  // For duplicate: which worker had it; for sticky: expected worker
} violation_info_t;

// Test context
typedef struct {
    dap_io_flow_server_t *server;
    packet_tracker_t tracker;
    atomic_uint total_packets_sent;
    atomic_uint flows_created;
    uint16_t server_port;
    
    // Client esockets
    dap_events_socket_t *client_es[NUM_CLIENTS];
    client_ctx_t client_ctx[NUM_CLIENTS];
    
    // Test state
    atomic_bool sending_complete;
    atomic_bool test_complete;
} test_context_t;

static test_context_t s_test_ctx = {0};

// ============================================================================
// Packet Tracker
// ============================================================================

/**
 * @brief Initialize packet tracker
 */
static void s_tracker_init(packet_tracker_t *a_tracker)
{
    pthread_mutex_init(&a_tracker->lock, NULL);
    atomic_store(&a_tracker->violations_count, 0);
    atomic_store(&a_tracker->total_received, 0);
    
    // Mark all packets as not received
    for (int c = 0; c < NUM_CLIENTS; c++) {
        a_tracker->client_expected_worker[c] = -1;
        for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
            a_tracker->packet_worker[c][p] = WORKER_ID_NOT_RECEIVED;
        }
    }
}

/**
 * @brief Cleanup packet tracker
 */
static void s_tracker_deinit(packet_tracker_t *a_tracker)
{
    pthread_mutex_destroy(&a_tracker->lock);
}

/**
 * @brief Record packet receipt and check for violations
 * @return 0 if OK, -1 if violation detected
 */
static int s_tracker_record_packet(packet_tracker_t *a_tracker, 
                                    uint32_t a_client_id, 
                                    uint32_t a_packet_num, 
                                    uint32_t a_worker_id)
{
    if (a_client_id >= NUM_CLIENTS || a_packet_num >= PACKETS_PER_CLIENT) {
        return -1;
    }
    
    violation_info_t l_violation = { .type = VIOLATION_NONE };
    int l_result = 0;
    
    pthread_mutex_lock(&a_tracker->lock);
    
    // Check if packet already received (duplicate)
    if (a_tracker->packet_worker[a_client_id][a_packet_num] != WORKER_ID_NOT_RECEIVED) {
        l_violation.type = VIOLATION_DUPLICATE;
        l_violation.client_id = a_client_id;
        l_violation.packet_num = a_packet_num;
        l_violation.worker_id = a_worker_id;
        l_violation.prev_worker_id = a_tracker->packet_worker[a_client_id][a_packet_num];
        atomic_fetch_add(&a_tracker->violations_count, 1);
        l_result = -1;
    } else {
        // Record which worker received this packet
        a_tracker->packet_worker[a_client_id][a_packet_num] = (uint8_t)a_worker_id;
        atomic_fetch_add(&a_tracker->total_received, 1);
        
        // Check sticky session violation
        if (a_tracker->client_expected_worker[a_client_id] < 0) {
            // First packet from this client - set expected worker
            a_tracker->client_expected_worker[a_client_id] = (int8_t)a_worker_id;
        } else if (a_tracker->client_expected_worker[a_client_id] != (int8_t)a_worker_id) {
            // VIOLATION: Packet went to different worker!
            l_violation.type = VIOLATION_STICKY;
            l_violation.client_id = a_client_id;
            l_violation.packet_num = a_packet_num;
            l_violation.worker_id = a_worker_id;
            l_violation.prev_worker_id = (uint32_t)a_tracker->client_expected_worker[a_client_id];
            atomic_fetch_add(&a_tracker->violations_count, 1);
            l_result = -1;
        }
    }
    
    pthread_mutex_unlock(&a_tracker->lock);
    
    // Log violations outside of mutex to avoid potential deadlocks
    if (l_violation.type == VIOLATION_DUPLICATE) {
        dap_test_msg("DUPLICATE: Client %u Packet %u received again on Worker %u (was Worker %u)",
                    l_violation.client_id, l_violation.packet_num, 
                    l_violation.worker_id, l_violation.prev_worker_id);
    } else if (l_violation.type == VIOLATION_STICKY) {
        dap_test_msg("VIOLATION: Client %u Packet %u -> Worker %u (expected Worker %d)",
                    l_violation.client_id, l_violation.packet_num, 
                    l_violation.worker_id, l_violation.prev_worker_id);
    }
    
    return l_result;
}

/**
 * @brief Print detailed packet distribution report
 */
static void s_tracker_print_report(packet_tracker_t *a_tracker)
{
    pthread_mutex_lock(&a_tracker->lock);
    
    dap_test_msg("=== Packet Distribution Report ===");
    
    // Count packets per worker
    uint32_t l_worker_counts[NUM_WORKERS] = {0};
    uint32_t l_total_received = 0;
    uint32_t l_total_lost = 0;
    
    for (int c = 0; c < NUM_CLIENTS; c++) {
        for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
            uint8_t l_worker = a_tracker->packet_worker[c][p];
            if (l_worker != WORKER_ID_NOT_RECEIVED && l_worker < NUM_WORKERS) {
                l_worker_counts[l_worker]++;
                l_total_received++;
            } else if (l_worker == WORKER_ID_NOT_RECEIVED) {
                l_total_lost++;
            }
        }
    }
    
    dap_test_msg("Worker distribution:");
    for (int w = 0; w < NUM_WORKERS; w++) {
        dap_test_msg("  Worker %d: %u packets (%.1f%%)", 
                    w, l_worker_counts[w], 
                    l_total_received > 0 ? 100.0f * l_worker_counts[w] / l_total_received : 0.0f);
    }
    
    dap_test_msg("Total: received=%u, lost=%u, violations=%u",
                l_total_received, l_total_lost, atomic_load(&a_tracker->violations_count));
    
    // Print client->worker mapping
    dap_test_msg("Client -> Worker mapping:");
    for (int c = 0; c < NUM_CLIENTS; c++) {
        int8_t l_expected = a_tracker->client_expected_worker[c];
        
        // Count how many packets went to each worker for this client
        uint32_t l_client_worker_counts[NUM_WORKERS] = {0};
        uint32_t l_client_received = 0;
        
        for (int p = 0; p < PACKETS_PER_CLIENT; p++) {
            uint8_t l_worker = a_tracker->packet_worker[c][p];
            if (l_worker != WORKER_ID_NOT_RECEIVED && l_worker < NUM_WORKERS) {
                l_client_worker_counts[l_worker]++;
                l_client_received++;
            }
        }
        
        // Check if all packets went to same worker
        int l_active_workers = 0;
        for (int w = 0; w < NUM_WORKERS; w++) {
            if (l_client_worker_counts[w] > 0) l_active_workers++;
        }
        
        if (l_active_workers > 1) {
            // SPLIT - print details
            dap_test_msg("  Client %2d: SPLIT! Expected Worker %d, but:", c, l_expected);
            for (int w = 0; w < NUM_WORKERS; w++) {
                if (l_client_worker_counts[w] > 0) {
                    dap_test_msg("             Worker %d: %u packets", w, l_client_worker_counts[w]);
                }
            }
        } else if (l_client_received > 0) {
            dap_test_msg("  Client %2d: Worker %d (%u/%d packets)", 
                        c, l_expected, l_client_received, PACKETS_PER_CLIENT);
        } else {
            dap_test_msg("  Client %2d: NO PACKETS RECEIVED", c);
        }
    }
    
    pthread_mutex_unlock(&a_tracker->lock);
}

// ============================================================================
// Flow Server Callbacks (VTable)
// ============================================================================

/**
 * @brief Packet received callback - track each packet individually
 */
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
    
    if (atomic_load(&s_test_ctx.test_complete)) return;
    
    if (a_size != sizeof(test_packet_t)) {
        return;
    }
    
    const test_packet_t *l_packet = (const test_packet_t*)a_data;
    if (l_packet->magic != PACKET_MAGIC) {
        return;
    }
    
    // Validate unique_id matches client_id/packet_num
    uint32_t l_expected_uid = l_packet->client_id * PACKETS_PER_CLIENT + l_packet->packet_num;
    if (l_packet->unique_id != l_expected_uid) {
        dap_test_msg("CORRUPT: Packet unique_id mismatch: got %u, expected %u",
                    l_packet->unique_id, l_expected_uid);
        return;
    }
    
    // Get current worker ID
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        dap_test_msg("ERROR: Packet received outside of worker context");
        return;
    }
    
    uint32_t l_worker_id = l_worker->id;
    
    if (l_worker_id >= NUM_WORKERS) {
        dap_test_msg("ERROR: Worker ID %u exceeds NUM_WORKERS (%d)", l_worker_id, NUM_WORKERS);
        return;
    }
    
    // Record packet and check for violations
    s_tracker_record_packet(&s_test_ctx.tracker, 
                           l_packet->client_id, 
                           l_packet->packet_num, 
                           l_worker_id);
}

/**
 * @brief Flow create callback
 */
static dap_io_flow_t* s_flow_create(dap_io_flow_server_t *a_server,
                                     const struct sockaddr_storage *a_remote_addr,
                                     dap_events_socket_t *a_listener_es)
{
    (void)a_server;
    (void)a_remote_addr;
    (void)a_listener_es;
    
    dap_io_flow_t *l_flow = DAP_NEW_Z(dap_io_flow_t);
    if (l_flow) {
        atomic_fetch_add(&s_test_ctx.flows_created, 1);
    }
    return l_flow;
}

/**
 * @brief Flow destroy callback
 */
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
// Client esocket send callback (runs in worker context)
// ============================================================================

/**
 * @brief Send next packet from client (called via worker callback)
 */
static void s_client_send_packet_callback(void *a_arg)
{
    dap_events_socket_t *l_es = (dap_events_socket_t*)a_arg;
    if (!l_es || !l_es->_inheritor) return;
    
    if (atomic_load(&s_test_ctx.test_complete)) return;
    
    client_ctx_t *l_ctx = (client_ctx_t*)l_es->_inheritor;
    
    if (l_ctx->packets_sent >= PACKETS_PER_CLIENT) {
        return;  // Done sending
    }
    
    // Prepare packet with unique identification
    test_packet_t l_packet = {
        .client_id = l_ctx->client_id,
        .packet_num = l_ctx->packets_sent,
        .magic = PACKET_MAGIC,
        .unique_id = l_ctx->client_id * PACKETS_PER_CLIENT + l_ctx->packets_sent
    };
    snprintf(l_packet.payload, sizeof(l_packet.payload), 
             "C%02d:P%03d:U%05u", l_ctx->client_id, l_ctx->packets_sent, l_packet.unique_id);
    
    // Send via DAP SDK
    size_t l_sent = dap_events_socket_sendto_unsafe(l_es, &l_packet, sizeof(l_packet),
                                                    &l_ctx->server_addr, l_ctx->server_addr_len);
    
    if (l_sent > 0) {
        l_ctx->packets_sent++;
        atomic_fetch_add(&s_test_ctx.total_packets_sent, 1);
    }
    
    // Schedule next packet if not done
    if (l_ctx->packets_sent < PACKETS_PER_CLIENT) {
        dap_worker_exec_callback_on(l_es->worker, s_client_send_packet_callback, l_es);
    }
}

/**
 * @brief Timer callback to start sending from all clients
 */
static bool s_start_sending_timer_callback(void *a_arg)
{
    (void)a_arg;
    
    if (atomic_load(&s_test_ctx.test_complete)) return false;
    
    dap_test_msg("Starting packet transmission: %d clients x %d packets = %d total",
                NUM_CLIENTS, PACKETS_PER_CLIENT, NUM_CLIENTS * PACKETS_PER_CLIENT);
    
    // Start sending from each client
    for (int i = 0; i < NUM_CLIENTS; i++) {
        dap_events_socket_t *l_es = s_test_ctx.client_es[i];
        if (l_es && l_es->worker) {
            dap_worker_exec_callback_on(l_es->worker, s_client_send_packet_callback, l_es);
        }
    }
    
    return false;  // One-shot timer
}

/**
 * @brief Timer callback to check test completion
 */
static bool s_check_completion_timer_callback(void *a_arg)
{
    (void)a_arg;
    
    uint32_t l_sent = atomic_load(&s_test_ctx.total_packets_sent);
    uint32_t l_received = atomic_load(&s_test_ctx.tracker.total_received);
    uint32_t l_expected = NUM_CLIENTS * PACKETS_PER_CLIENT;
    uint32_t l_threshold = l_expected * COMPLETION_THRESHOLD_PCT / 100;
    
    // Check if all packets sent
    if (l_sent >= l_expected) {
        atomic_store(&s_test_ctx.sending_complete, true);
    }
    
    // Check if received enough (allow small loss due to UDP nature)
    if (l_received >= l_threshold && atomic_load(&s_test_ctx.sending_complete)) {
        dap_test_msg("Transmission complete: sent=%u, received=%u (threshold=%u)", 
                    l_sent, l_received, l_threshold);
        atomic_store(&s_test_ctx.test_complete, true);
        return false;  // Stop timer
    }
    
    return true;  // Continue checking
}

// ============================================================================
// Test Helpers
// ============================================================================

/**
 * @brief Initialize test context
 */
static void s_init_test_context(void)
{
    memset(&s_test_ctx, 0, sizeof(s_test_ctx));
    
    s_tracker_init(&s_test_ctx.tracker);
    
    atomic_store(&s_test_ctx.total_packets_sent, 0);
    atomic_store(&s_test_ctx.flows_created, 0);
    atomic_store(&s_test_ctx.sending_complete, false);
    atomic_store(&s_test_ctx.test_complete, false);
}

/**
 * @brief Cleanup test context
 */
static void s_cleanup_test_context(void)
{
    s_tracker_deinit(&s_test_ctx.tracker);
}

/**
 * @brief Cleanup helper for partially created client sockets
 */
static void s_cleanup_client_esockets_partial(int a_created_count)
{
    for (int i = 0; i < a_created_count; i++) {
        if (s_test_ctx.client_es[i]) {
            s_test_ctx.client_es[i]->_inheritor = NULL;
            s_test_ctx.client_es[i]->flags |= DAP_SOCK_SIGNAL_CLOSE;
            s_test_ctx.client_es[i] = NULL;
        }
    }
}

/**
 * @brief Create client UDP esockets via DAP SDK
 */
static int s_create_client_esockets(void)
{
    dap_test_msg("Creating %d client esockets", NUM_CLIENTS);
    
    for (int i = 0; i < NUM_CLIENTS; i++) {
        // Create UDP socket
        int l_fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (l_fd < 0) {
            dap_test_msg("Failed to create client socket %d: %s", i, strerror(errno));
            s_cleanup_client_esockets_partial(i);
            return -1;
        }
        
        // Bind to ephemeral port
        struct sockaddr_in l_bind_addr = {
            .sin_family = AF_INET,
            .sin_port = 0,
            .sin_addr.s_addr = htonl(INADDR_LOOPBACK)
        };
        
        if (bind(l_fd, (struct sockaddr*)&l_bind_addr, sizeof(l_bind_addr)) < 0) {
            dap_test_msg("Failed to bind client socket %d: %s", i, strerror(errno));
            close(l_fd);
            s_cleanup_client_esockets_partial(i);
            return -1;
        }
        
        // Initialize client context
        s_test_ctx.client_ctx[i].client_id = i;
        s_test_ctx.client_ctx[i].packets_sent = 0;
        
        // Set server address
        struct sockaddr_in *l_server = (struct sockaddr_in*)&s_test_ctx.client_ctx[i].server_addr;
        l_server->sin_family = AF_INET;
        l_server->sin_port = htons(s_test_ctx.server_port);
        l_server->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        s_test_ctx.client_ctx[i].server_addr_len = sizeof(struct sockaddr_in);
        
        // Wrap socket in events_socket
        dap_events_socket_callbacks_t l_callbacks = {0};
        dap_events_socket_t *l_es = dap_events_socket_wrap_no_add(l_fd, &l_callbacks);
        if (!l_es) {
            dap_test_msg("Failed to wrap client socket %d", i);
            close(l_fd);
            s_cleanup_client_esockets_partial(i);
            return -1;
        }
        
        l_es->type = DESCRIPTOR_TYPE_SOCKET_UDP;
        l_es->_inheritor = &s_test_ctx.client_ctx[i];
        l_es->flags |= DAP_SOCK_READY_TO_WRITE;
        
        // Add to worker with auto-balancing
        dap_worker_t *l_worker = dap_worker_add_events_socket_auto(l_es);
        if (!l_worker) {
            dap_test_msg("Failed to add client socket %d to worker", i);
            l_es->_inheritor = NULL;
            dap_events_socket_delete_unsafe(l_es, false);
            s_cleanup_client_esockets_partial(i);
            return -1;
        }
        
        s_test_ctx.client_es[i] = l_es;
    }
    
    dap_test_msg("Created %d client esockets", NUM_CLIENTS);
    return 0;
}

/**
 * @brief Cleanup client esockets
 * 
 * Note: This sets the close signal and clears our pointers.
 * The actual socket cleanup is performed asynchronously by the workers.
 * Caller should wait (e.g., sleep) before deinitializing events to ensure
 * workers have time to process the close signals.
 */
static void s_cleanup_client_esockets(void)
{
    for (int i = 0; i < NUM_CLIENTS; i++) {
        if (s_test_ctx.client_es[i]) {
            s_test_ctx.client_es[i]->_inheritor = NULL;
            s_test_ctx.client_es[i]->flags |= DAP_SOCK_SIGNAL_CLOSE;
            s_test_ctx.client_es[i] = NULL;
        }
    }
}

/**
 * @brief Verify sticky sessions by analyzing packet tracker
 */
static int s_verify_sticky_sessions(void)
{
    // Print detailed report
    s_tracker_print_report(&s_test_ctx.tracker);
    
    uint32_t l_violations = atomic_load(&s_test_ctx.tracker.violations_count);
    uint32_t l_received = atomic_load(&s_test_ctx.tracker.total_received);
    uint32_t l_expected = NUM_CLIENTS * PACKETS_PER_CLIENT;
    uint32_t l_pass_threshold = l_expected * PASS_THRESHOLD_PCT / 100;
    
    dap_test_msg("Flows created: %u", atomic_load(&s_test_ctx.flows_created));
    
    if (l_violations > 0) {
        dap_test_msg("FAIL: %u sticky session violations detected!", l_violations);
        return -1;
    }
    
    if (l_received < l_pass_threshold) {
        dap_test_msg("FAIL: Too many packets lost: %u/%u (%.1f%%, threshold %u%%)", 
                    l_received, l_expected, 100.0f * l_received / l_expected, 
                    PASS_THRESHOLD_PCT);
        return -1;
    }
    
    dap_test_msg("PASS: All packets correctly routed, no violations");
    return 0;
}

/**
 * @brief Wait for test completion with timeout
 */
static int s_wait_for_completion(void)
{
    time_t l_start = time(NULL);
    
    while (!atomic_load(&s_test_ctx.test_complete)) {
        if (time(NULL) - l_start > TEST_TIMEOUT_SEC) {
            dap_test_msg("Test timeout after %d seconds", TEST_TIMEOUT_SEC);
            dap_test_msg("  Sent: %u, Received: %u", 
                        atomic_load(&s_test_ctx.total_packets_sent),
                        atomic_load(&s_test_ctx.tracker.total_received));
            return -1;
        }
        usleep(100000);  // 100ms polling interval
    }
    
    return 0;
}

// ============================================================================
// Tests
// ============================================================================

/**
 * @brief Run sticky sessions test for specified BPF tier
 */
static void s_run_sticky_test(const char *a_tier_name)
{
    dap_test_msg("=== Test: %s Sticky Sessions ===", a_tier_name);
    
    // Initialize
    s_init_test_context();
    
    int l_ret = dap_events_init(NUM_WORKERS, 60);
    dap_assert(l_ret == 0, "Events init");
    
    l_ret = dap_events_start();
    dap_assert(l_ret == 0, "Events start");
    
    // Give workers time to initialize
    usleep(WORKER_STARTUP_DELAY_MS * 1000);
    
    // Create flow server
    s_test_ctx.server = dap_io_flow_server_new("test_flow", &s_flow_ops, DAP_IO_FLOW_BOUNDARY_DATAGRAM);
    dap_assert(s_test_ctx.server != NULL, "Server created");
    
    // Start listening
    l_ret = dap_io_flow_server_listen(s_test_ctx.server, "127.0.0.1", TEST_PORT);
    dap_assert(l_ret == 0, "Server listen");
    
    // Get actual port from listener socket using getsockname()
    // Note: addr_storage is for remote address, not local bind address!
    s_test_ctx.server_port = 0;
    if (s_test_ctx.server->dap_server && s_test_ctx.server->dap_server->es_listeners) {
        dap_events_socket_t *l_listener = (dap_events_socket_t*)s_test_ctx.server->dap_server->es_listeners->data;
        if (l_listener && l_listener->socket != INVALID_SOCKET) {
            struct sockaddr_in l_local_addr;
            socklen_t l_addr_len = sizeof(l_local_addr);
            if (getsockname(l_listener->socket, (struct sockaddr*)&l_local_addr, &l_addr_len) == 0) {
                s_test_ctx.server_port = ntohs(l_local_addr.sin_port);
            }
        }
    }
    
    // Validate server port was obtained
    dap_assert(s_test_ctx.server_port != 0, "Server port must be valid");
    dap_test_msg("Server port: %u, LB tier: %d", s_test_ctx.server_port, s_test_ctx.server->lb_tier);
    
    // Create client esockets
    l_ret = s_create_client_esockets();
    dap_assert(l_ret == 0, "Client esockets created");
    
    // Schedule sending start (after 100ms)
    dap_timerfd_t *l_send_timer = dap_timerfd_start(100, s_start_sending_timer_callback, NULL);
    dap_assert(l_send_timer != NULL, "Send timer must be created");
    (void)l_send_timer;  // Timer is self-managed, we don't need to track it
    
    // Schedule completion checker (every 200ms) on primary worker
    // This timer is critical - without it, test_complete will never be set
    dap_worker_t *l_worker0 = dap_events_worker_get(0);
    dap_assert(l_worker0 != NULL, "Primary worker must exist");
    
    dap_timerfd_t *l_check_timer = dap_timerfd_start_on_worker(l_worker0, 200, 
                                                               s_check_completion_timer_callback, NULL);
    dap_assert(l_check_timer != NULL, "Completion check timer must be created");
    (void)l_check_timer;  // Timer is self-managed, we don't need to track it
    
    // Wait for completion
    l_ret = s_wait_for_completion();
    dap_assert(l_ret == 0, "Test completed in time");
    
    // Verify results with detailed packet analysis
    l_ret = s_verify_sticky_sessions();
    dap_assert(l_ret == 0, "Sticky sessions verified");
    
    // Signal shutdown and wait for worker queues to drain
    atomic_store(&s_test_ctx.test_complete, true);
    dap_test_sleep_ms(500);
    
    // Phase 1: Stop flow server (prevents new packets)
    dap_io_flow_server_stop(s_test_ctx.server);
    
    // Phase 2: Mark client sockets for close
    s_cleanup_client_esockets();
    
    // Phase 3: Wait for close signals to be processed
    dap_test_sleep_ms(500);
    
    // Phase 4: Delete all flows (protocol cleanup)
    dap_io_flow_delete_all_flows(s_test_ctx.server);
    
    // Phase 5: Delete flow server
    dap_io_flow_server_delete(s_test_ctx.server);
    s_test_ctx.server = NULL;
    
    // Phase 6: Local test context cleanup (before deinit)
    s_cleanup_test_context();
    
    dap_test_msg("%s sticky sessions test passed", a_tier_name);
    dap_pass_msg("Sticky sessions test passed");
    
    // Phase 7: Deinit events system
    dap_events_deinit();
}

/**
 * @brief Test CBPF tier
 */
static void test_cbpf_sticky_sessions(void)
{
    if (!dap_io_flow_cbpf_is_available()) {
        dap_test_msg("CBPF not available, skipping");
        dap_pass_msg("CBPF test skipped");
        return;
    }
    s_run_sticky_test("CBPF");
}

/**
 * @brief Test eBPF tier
 */
static void test_ebpf_sticky_sessions(void)
{
    if (!dap_io_flow_ebpf_is_available()) {
        dap_test_msg("eBPF not available (need CAP_BPF), skipping");
        dap_pass_msg("eBPF test skipped");
        return;
    }
    s_run_sticky_test("eBPF");
}

// CTest skip return code (standard convention)
#define CTEST_SKIP_RETURN_CODE 77

int main(void)
{
    dap_common_init("test_flow_tiers", NULL);
    dap_log_level_set(L_NOTICE);

    dap_test_msg("=== IO Flow Tiers Integration Test (Full DAP SDK) ===");
    dap_test_msg("Testing dap_io_flow_server with dap_events_socket clients");
    dap_test_msg("Each packet tracked individually to detect routing violations");
    
    bool l_ebpf = dap_io_flow_ebpf_is_available();
    bool l_cbpf = dap_io_flow_cbpf_is_available();
    
    dap_test_msg("BPF availability: eBPF=%s, CBPF=%s", 
                 l_ebpf ? "yes" : "no", l_cbpf ? "yes" : "no");
    
    if (!l_ebpf && !l_cbpf) {
        dap_test_msg("SKIP: No BPF available. Run as root or grant CAP_BPF.");
        dap_test_msg("To enable: sudo setcap cap_bpf,cap_net_admin=ep <test_binary>");
        return CTEST_SKIP_RETURN_CODE;  // Skip test gracefully in CI
    }
    
    if (l_cbpf) test_cbpf_sticky_sessions();
    if (l_ebpf) test_ebpf_sticky_sessions();
    
    dap_test_msg("=== All Integration Tests Passed ===");
    return 0;
}
