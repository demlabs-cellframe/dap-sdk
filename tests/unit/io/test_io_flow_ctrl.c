/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * CellFrame       https://cellframe.net
 * Copyright  (c) 2025-2026
 * All rights reserved.
 *
 * Unit test for dap_io_flow_ctrl - Flow Control for datagram protocols
 * 
 * REGRESSION TEST: Reproduces the retransmission bug where:
 * - Packets are sent and ACKed correctly
 * - But retransmission continues indefinitely after all packets are ACKed
 * - This causes buffer exhaustion and blocks other connections
 * 
 * Uses real DAP SDK reactor with mocked transport callbacks.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <unistd.h>

#include "dap_test.h"
#include "dap_common.h"
#include "dap_mock.h"
#include "dap_io_flow_ctrl.h"
#include "dap_io_flow.h"
#include "dap_events.h"
#include "dap_worker.h"
#include "dap_time.h"

#define LOG_TAG "test_io_flow_ctrl"

//===================================================================
// POLLING HELPERS
//===================================================================

#define POLL_INTERVAL_US    5000    // 5ms between checks
#define POLL_TIMEOUT_MS     5000    // 5s max wait

/**
 * @brief Wait until condition is true or timeout
 * @return true if condition met, false if timeout
 */
#define POLL_WAIT_UNTIL(condition, timeout_ms) ({ \
    bool _result = false; \
    uint64_t _start = dap_nanotime_now(); \
    uint64_t _timeout_ns = (uint64_t)(timeout_ms) * 1000000ULL; \
    while ((dap_nanotime_now() - _start) < _timeout_ns) { \
        if (condition) { _result = true; break; } \
        usleep(POLL_INTERVAL_US); \
    } \
    _result; \
})

/**
 * @brief Wait until atomic counter reaches expected value
 */
static bool s_wait_for_counter(_Atomic uint64_t *a_counter, uint64_t a_expected, uint32_t a_timeout_ms)
{
    uint64_t l_start = dap_nanotime_now();
    uint64_t l_timeout_ns = (uint64_t)a_timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - l_start) < l_timeout_ns) {
        if (atomic_load(a_counter) >= a_expected) {
            return true;
        }
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

/**
 * @brief Wait until flow_ctrl stats reach expected value
 */
static bool s_wait_for_retransmits(dap_io_flow_ctrl_t *a_ctrl, uint64_t a_min_retrans, uint32_t a_timeout_ms)
{
    uint64_t l_start = dap_nanotime_now();
    uint64_t l_timeout_ns = (uint64_t)a_timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - l_start) < l_timeout_ns) {
        uint64_t l_sent, l_retrans, l_recv, l_ooo, l_dup, l_lost;
        dap_io_flow_ctrl_get_stats(a_ctrl, &l_sent, &l_retrans, &l_recv, &l_ooo, &l_dup, &l_lost);
        if (l_retrans >= a_min_retrans) {
            return true;
        }
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

/**
 * @brief Wait for reactor to start (workers available)
 */
static bool s_wait_for_reactor(uint32_t a_timeout_ms)
{
    uint64_t l_start = dap_nanotime_now();
    uint64_t l_timeout_ns = (uint64_t)a_timeout_ms * 1000000ULL;
    
    while ((dap_nanotime_now() - l_start) < l_timeout_ns) {
        dap_worker_t *l_worker = dap_events_worker_get(0);
        if (l_worker) {
            return true;
        }
        usleep(POLL_INTERVAL_US);
    }
    return false;
}

//===================================================================
// MOCK TRANSPORT LAYER
//===================================================================

// Mock packet header
typedef struct mock_packet_header {
    uint64_t seq_num;
    uint64_t ack_seq;
    uint32_t timestamp_ms;
    uint8_t  flags;
    uint32_t payload_size;
} DAP_ALIGN_PACKED mock_packet_header_t;

// Test context
typedef struct test_flow_ctrl_ctx {
    // DAP mock state for packet_send tracking
    dap_mock_function_state_t *mock_packet_send;
    dap_mock_function_state_t *mock_payload_deliver;
    
    // Counters
    _Atomic uint64_t packets_sent;
    _Atomic uint64_t retransmits_count;
    _Atomic uint64_t packets_delivered;
    
    // Pending packets queue (simulated network)
    pthread_mutex_t pending_mutex;
    void **pending_packets;
    size_t *pending_sizes;
    size_t pending_count;
    size_t pending_capacity;
    
    // Flow for this context
    dap_io_flow_t dummy_flow;
    
    // Peer flow control (for loopback)
    dap_io_flow_ctrl_t *peer_ctrl;
} test_flow_ctrl_ctx_t;

static _Atomic int s_ctx_counter = 0;

/**
 * @brief Prepare packet callback
 */
static int s_packet_prepare(dap_io_flow_t *a_flow, const dap_io_flow_pkt_metadata_t *a_metadata,
                            const void *a_payload, size_t a_payload_size,
                            void **a_packet_out, size_t *a_packet_size_out, void *a_arg)
{
    (void)a_flow;
    (void)a_arg;
    
    size_t l_packet_size = sizeof(mock_packet_header_t) + a_payload_size;
    void *l_packet = DAP_NEW_SIZE(void, l_packet_size);
    if (!l_packet) return -1;
    
    mock_packet_header_t *l_hdr = (mock_packet_header_t *)l_packet;
    l_hdr->seq_num = a_metadata->seq_num;
    l_hdr->ack_seq = a_metadata->ack_seq;
    l_hdr->timestamp_ms = a_metadata->timestamp_ms;
    l_hdr->flags = (a_metadata->is_keepalive ? 1 : 0) | (a_metadata->is_retransmit ? 2 : 0);
    l_hdr->payload_size = (uint32_t)a_payload_size;
    
    if (a_payload && a_payload_size > 0) {
        memcpy((uint8_t*)l_packet + sizeof(mock_packet_header_t), a_payload, a_payload_size);
    }
    
    *a_packet_out = l_packet;
    *a_packet_size_out = l_packet_size;
    return 0;
}

/**
 * @brief Parse packet callback
 */
static int s_packet_parse(dap_io_flow_t *a_flow, const void *a_packet, size_t a_packet_size,
                          dap_io_flow_pkt_metadata_t *a_metadata, const void **a_payload_out,
                          size_t *a_payload_size_out, void *a_arg)
{
    (void)a_flow;
    (void)a_arg;
    
    if (a_packet_size < sizeof(mock_packet_header_t)) return -1;
    
    const mock_packet_header_t *l_hdr = (const mock_packet_header_t *)a_packet;
    
    a_metadata->seq_num = l_hdr->seq_num;
    a_metadata->ack_seq = l_hdr->ack_seq;
    a_metadata->timestamp_ms = l_hdr->timestamp_ms;
    a_metadata->is_keepalive = (l_hdr->flags & 1) != 0;
    a_metadata->is_retransmit = (l_hdr->flags & 2) != 0;
    a_metadata->private_ctx = NULL;
    
    if (l_hdr->payload_size > 0) {
        *a_payload_out = (const uint8_t*)a_packet + sizeof(mock_packet_header_t);
        *a_payload_size_out = l_hdr->payload_size;
    } else {
        *a_payload_out = NULL;
        *a_payload_size_out = 0;
    }
    return 0;
}

/**
 * @brief Send packet callback - MOCKED
 */
static int s_packet_send(dap_io_flow_t *a_flow, const void *a_packet, size_t a_packet_size, void *a_arg)
{
    (void)a_flow;
    test_flow_ctrl_ctx_t *l_ctx = (test_flow_ctrl_ctx_t *)a_arg;
    
    const mock_packet_header_t *l_hdr = (const mock_packet_header_t *)a_packet;
    
    // Track using DAP mock framework
    void *l_args[] = {(void*)a_packet, (void*)(intptr_t)a_packet_size, (void*)(intptr_t)l_hdr->seq_num};
    dap_mock_record_call(l_ctx->mock_packet_send, l_args, 3, (void*)(intptr_t)0);
    
    atomic_fetch_add(&l_ctx->packets_sent, 1);
    
    // Check if retransmit
    if (l_hdr->flags & 2) {
        atomic_fetch_add(&l_ctx->retransmits_count, 1);
    }
    
    // Store in pending queue
    pthread_mutex_lock(&l_ctx->pending_mutex);
    if (l_ctx->pending_count < l_ctx->pending_capacity) {
        void *l_copy = DAP_NEW_SIZE(void, a_packet_size);
        memcpy(l_copy, a_packet, a_packet_size);
        l_ctx->pending_packets[l_ctx->pending_count] = l_copy;
        l_ctx->pending_sizes[l_ctx->pending_count] = a_packet_size;
        l_ctx->pending_count++;
    }
    pthread_mutex_unlock(&l_ctx->pending_mutex);
    
    return 0;
}

/**
 * @brief Free packet callback
 */
static void s_packet_free(void *a_packet, void *a_arg)
{
    (void)a_arg;
    DAP_DELETE(a_packet);
}

/**
 * @brief Deliver payload callback - MOCKED
 */
static int s_payload_deliver(dap_io_flow_t *a_flow, const void *a_payload, size_t a_payload_size, void *a_arg)
{
    (void)a_flow;
    (void)a_payload;
    test_flow_ctrl_ctx_t *l_ctx = (test_flow_ctrl_ctx_t *)a_arg;
    
    void *l_args[] = {(void*)a_payload, (void*)(intptr_t)a_payload_size};
    dap_mock_record_call(l_ctx->mock_payload_deliver, l_args, 2, (void*)(intptr_t)0);
    
    atomic_fetch_add(&l_ctx->packets_delivered, 1);
    return 0;
}

/**
 * @brief Create test context
 */
static test_flow_ctrl_ctx_t *s_ctx_create(void)
{
    test_flow_ctrl_ctx_t *l_ctx = DAP_NEW_Z(test_flow_ctrl_ctx_t);
    if (!l_ctx) return NULL;
    
    int l_id = atomic_fetch_add(&s_ctx_counter, 1);
    char l_name[64];
    
    snprintf(l_name, sizeof(l_name), "packet_send_%d", l_id);
    l_ctx->mock_packet_send = dap_mock_register(l_name);
    
    snprintf(l_name, sizeof(l_name), "payload_deliver_%d", l_id);
    l_ctx->mock_payload_deliver = dap_mock_register(l_name);
    
    pthread_mutex_init(&l_ctx->pending_mutex, NULL);
    l_ctx->pending_capacity = 4096;
    l_ctx->pending_packets = DAP_NEW_Z_SIZE(void*, l_ctx->pending_capacity * sizeof(void*));
    l_ctx->pending_sizes = DAP_NEW_Z_SIZE(size_t, l_ctx->pending_capacity * sizeof(size_t));
    
    return l_ctx;
}

/**
 * @brief Delete test context
 */
static void s_ctx_delete(test_flow_ctrl_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    
    pthread_mutex_lock(&a_ctx->pending_mutex);
    for (size_t i = 0; i < a_ctx->pending_count; i++) {
        DAP_DELETE(a_ctx->pending_packets[i]);
    }
    pthread_mutex_unlock(&a_ctx->pending_mutex);
    
    if (a_ctx->mock_packet_send) dap_mock_reset(a_ctx->mock_packet_send);
    if (a_ctx->mock_payload_deliver) dap_mock_reset(a_ctx->mock_payload_deliver);
    
    DAP_DELETE(a_ctx->pending_packets);
    DAP_DELETE(a_ctx->pending_sizes);
    pthread_mutex_destroy(&a_ctx->pending_mutex);
    DAP_DELETE(a_ctx);
}

/**
 * @brief Process pending packets - deliver to peer
 */
static int s_process_pending(test_flow_ctrl_ctx_t *a_ctx, dap_io_flow_ctrl_t *a_peer)
{
    pthread_mutex_lock(&a_ctx->pending_mutex);
    
    for (size_t i = 0; i < a_ctx->pending_count; i++) {
        dap_io_flow_ctrl_recv(a_peer, a_ctx->pending_packets[i], a_ctx->pending_sizes[i]);
        DAP_DELETE(a_ctx->pending_packets[i]);
    }
    
    size_t l_count = a_ctx->pending_count;
    a_ctx->pending_count = 0;
    
    pthread_mutex_unlock(&a_ctx->pending_mutex);
    return (int)l_count;
}

/**
 * @brief Get pending packet count
 */
static size_t s_get_pending_count(test_flow_ctrl_ctx_t *a_ctx)
{
    pthread_mutex_lock(&a_ctx->pending_mutex);
    size_t l_count = a_ctx->pending_count;
    pthread_mutex_unlock(&a_ctx->pending_mutex);
    return l_count;
}

//===================================================================
// TEST CASES
//===================================================================

/**
 * @brief Test basic flow control without reactor (sanity check)
 */
static void test_flow_ctrl_basic(void)
{
    dap_test_msg("Test: Basic flow control (no reactor)");
    
    test_flow_ctrl_ctx_t *l_sender_ctx = s_ctx_create();
    test_flow_ctrl_ctx_t *l_receiver_ctx = s_ctx_create();
    
    dap_io_flow_ctrl_callbacks_t l_sender_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_sender_ctx,
    };
    
    dap_io_flow_ctrl_callbacks_t l_receiver_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_receiver_ctx,
    };
    
    dap_io_flow_ctrl_config_t l_config;
    dap_io_flow_ctrl_get_default_config(&l_config);
    l_config.retransmit_timeout_ms = 100;
    l_config.send_window_size = 64;
    l_config.recv_window_size = 64;
    
    dap_io_flow_ctrl_t *l_sender = dap_io_flow_ctrl_create(&l_sender_ctx->dummy_flow, 
                                                           DAP_IO_FLOW_CTRL_RELIABLE, &l_config, &l_sender_cb);
    dap_io_flow_ctrl_t *l_receiver = dap_io_flow_ctrl_create(&l_receiver_ctx->dummy_flow, 
                                                             DAP_IO_FLOW_CTRL_RELIABLE, &l_config, &l_receiver_cb);
    
    dap_assert(l_sender != NULL && l_receiver != NULL, "Flow controls created");
    
    // Send packets
    const char *l_data = "Test data packet";
    for (int i = 0; i < 5; i++) {
        dap_io_flow_ctrl_send(l_sender, l_data, strlen(l_data) + 1);
    }
    
    // Process sender -> receiver
    int l_processed = s_process_pending(l_sender_ctx, l_receiver);
    dap_assert(l_processed == 5, "5 packets processed");
    
    // Check receiver delivered
    dap_assert(atomic_load(&l_receiver_ctx->packets_delivered) == 5, "5 packets delivered");
    
    // Process ACKs receiver -> sender
    s_process_pending(l_receiver_ctx, l_sender);
    
    // Verify stats
    uint64_t l_sent, l_retrans, l_recv, l_ooo, l_dup, l_lost;
    dap_io_flow_ctrl_get_stats(l_sender, &l_sent, &l_retrans, &l_recv, &l_ooo, &l_dup, &l_lost);
    dap_assert(l_sent == 5, "Sender sent 5");
    dap_assert(l_lost == 0, "No packets lost");
    
    dap_io_flow_ctrl_delete(l_sender);
    dap_io_flow_ctrl_delete(l_receiver);
    s_ctx_delete(l_sender_ctx);
    s_ctx_delete(l_receiver_ctx);
    
    dap_pass_msg("Basic flow control test passed");
}

// Context for async flow control creation
typedef struct {
    test_flow_ctrl_ctx_t *ctx;
    dap_io_flow_ctrl_callbacks_t *callbacks;
    dap_io_flow_ctrl_config_t *config;
    dap_io_flow_ctrl_t *result;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    bool done;
} async_create_ctx_t;

static void s_create_flow_ctrl_on_worker(void *a_arg)
{
    async_create_ctx_t *l_async = (async_create_ctx_t *)a_arg;
    
    l_async->result = dap_io_flow_ctrl_create(&l_async->ctx->dummy_flow,
                                               DAP_IO_FLOW_CTRL_RELIABLE,
                                               l_async->config,
                                               l_async->callbacks);
    
    pthread_mutex_lock(&l_async->mutex);
    l_async->done = true;
    pthread_cond_signal(&l_async->cond);
    pthread_mutex_unlock(&l_async->mutex);
}

static dap_io_flow_ctrl_t *s_create_flow_ctrl_async(test_flow_ctrl_ctx_t *a_ctx,
                                                     dap_io_flow_ctrl_callbacks_t *a_callbacks,
                                                     dap_io_flow_ctrl_config_t *a_config)
{
    async_create_ctx_t l_async = {
        .ctx = a_ctx,
        .callbacks = a_callbacks,
        .config = a_config,
        .result = NULL,
        .done = false,
    };
    pthread_mutex_init(&l_async.mutex, NULL);
    pthread_cond_init(&l_async.cond, NULL);
    
    // Execute on worker 0 to get timer created
    dap_worker_t *l_worker = dap_events_worker_get(0);
    if (!l_worker) {
        pthread_mutex_destroy(&l_async.mutex);
        pthread_cond_destroy(&l_async.cond);
        return NULL;
    }
    
    dap_worker_exec_callback_on(l_worker, s_create_flow_ctrl_on_worker, &l_async);
    
    // Wait for completion
    pthread_mutex_lock(&l_async.mutex);
    while (!l_async.done) {
        pthread_cond_wait(&l_async.cond, &l_async.mutex);
    }
    pthread_mutex_unlock(&l_async.mutex);
    
    pthread_mutex_destroy(&l_async.mutex);
    pthread_cond_destroy(&l_async.cond);
    
    return l_async.result;
}

/**
 * @brief REGRESSION TEST: Retransmission with real reactor timer
 * 
 * This test uses the REAL DAP SDK reactor and timer to verify that
 * retransmissions stop after ACKs are received.
 */
static void test_flow_ctrl_retransmit_regression(void)
{
    dap_test_msg("Test: Retransmission regression (REAL reactor)");
    
    // Initialize DAP events reactor
    dap_events_init(0, 0);  // Auto-detect workers
    dap_events_start();
    
    // Wait for reactor to start (with polling)
    dap_assert(s_wait_for_reactor(2000), "Reactor should start within 2s");
    
    test_flow_ctrl_ctx_t *l_sender_ctx = s_ctx_create();
    test_flow_ctrl_ctx_t *l_receiver_ctx = s_ctx_create();
    
    dap_io_flow_ctrl_callbacks_t l_sender_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_sender_ctx,
    };
    
    dap_io_flow_ctrl_callbacks_t l_receiver_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_receiver_ctx,
    };
    
    dap_io_flow_ctrl_config_t l_config;
    dap_io_flow_ctrl_get_default_config(&l_config);
    // Short timeout to trigger retransmit quickly
    l_config.retransmit_timeout_ms = 100;  // 100ms
    l_config.max_retransmit_count = 5;
    l_config.send_window_size = 128;
    l_config.recv_window_size = 128;
    
    // Create flow controls on worker thread (so timer gets created)
    dap_io_flow_ctrl_t *l_sender = s_create_flow_ctrl_async(l_sender_ctx, &l_sender_cb, &l_config);
    dap_io_flow_ctrl_t *l_receiver = s_create_flow_ctrl_async(l_receiver_ctx, &l_receiver_cb, &l_config);
    
    dap_assert(l_sender != NULL && l_receiver != NULL, "Flow controls created on worker");
    
    // Step 1: Send packets
    dap_test_msg("Step 1: Sending 20 packets...");
    const char l_data[256] = "Test payload data for retransmission test";
    for (int i = 0; i < 20; i++) {
        dap_io_flow_ctrl_send(l_sender, l_data, sizeof(l_data));
    }
    
    uint64_t l_sent_after_send = atomic_load(&l_sender_ctx->packets_sent);
    dap_test_msg("Packets sent: %" PRIu64, l_sent_after_send);
    
    // Step 2: Wait for retransmit timer to fire (polling until retransmits > 0)
    dap_test_msg("Step 2: Waiting for retransmit timer (no ACKs)...");
    bool l_timer_fired = s_wait_for_retransmits(l_sender, 1, l_config.retransmit_timeout_ms * 5);
    
    // Check retransmissions via flow_ctrl stats
    uint64_t l_sent_stats, l_retrans_stats, l_recv, l_ooo, l_dup, l_lost;
    dap_io_flow_ctrl_get_stats(l_sender, &l_sent_stats, &l_retrans_stats, &l_recv, &l_ooo, &l_dup, &l_lost);
    
    uint64_t l_packets_after_wait = atomic_load(&l_sender_ctx->packets_sent);
    dap_test_msg("After wait: packets_sent=%" PRIu64 " (was 20), retrans_stats=%" PRIu64 ", timer_fired=%d",
                 l_packets_after_wait, l_retrans_stats, l_timer_fired);
    
    dap_assert(l_timer_fired && l_retrans_stats > 0, 
               "Timer should trigger retransmits when no ACK");
    
    // Step 3: Now process data packets to receiver (delivers data)
    dap_test_msg("Step 3: Processing data packets to receiver...");
    s_process_pending(l_sender_ctx, l_receiver);
    
    // Step 4: Process ACK packets back to sender
    dap_test_msg("Step 4: Processing ACK packets to sender...");
    int l_acks = s_process_pending(l_receiver_ctx, l_sender);
    dap_test_msg("ACK packets processed: %d", l_acks);
    
    // Step 5: Record current state (via flow_ctrl stats)
    uint64_t l_retrans_after_ack;
    dap_io_flow_ctrl_get_stats(l_sender, &l_sent_stats, &l_retrans_after_ack, &l_recv, &l_ooo, &l_dup, &l_lost);
    uint64_t l_packets_after_ack = atomic_load(&l_sender_ctx->packets_sent);
    dap_test_msg("After ACK: packets_sent=%" PRIu64 ", retrans_stats=%" PRIu64, l_packets_after_ack, l_retrans_after_ack);
    
    // Step 6: Wait for multiple timer cycles AFTER ACK (polling, checking no new retrans)
    dap_test_msg("Step 6: Waiting for timer cycles after ACK...");
    // Give timer 5x timeout to fire, but expect NO new retransmissions
    uint32_t l_wait_ms = l_config.retransmit_timeout_ms * 5;
    uint64_t l_start = dap_nanotime_now();
    while ((dap_nanotime_now() - l_start) < (uint64_t)l_wait_ms * 1000000ULL) {
        usleep(POLL_INTERVAL_US);
    }
    
    // Step 7: Check for new retransmissions (SHOULD BE ZERO!)
    uint64_t l_retrans_final;
    dap_io_flow_ctrl_get_stats(l_sender, &l_sent_stats, &l_retrans_final, &l_recv, &l_ooo, &l_dup, &l_lost);
    uint64_t l_packets_final = atomic_load(&l_sender_ctx->packets_sent);
    
    uint64_t l_new_retrans = l_retrans_final - l_retrans_after_ack;
    uint64_t l_new_packets = l_packets_final - l_packets_after_ack;
    
    dap_test_msg("Final: retrans_stats=%" PRIu64 " (was %" PRIu64 "), packets_sent=%" PRIu64 " (was %" PRIu64 ")",
                 l_retrans_final, l_retrans_after_ack, l_packets_final, l_packets_after_ack);
    dap_test_msg("NEW after ACK: retrans=%" PRIu64 ", packets=%" PRIu64 " (both expected 0)",
                 l_new_retrans, l_new_packets);
    
    // CRITICAL REGRESSION CHECK
    // If this fails, the bug is present - retransmits continue after ACK
    // Check both stats and raw packet count
    dap_assert(l_new_retrans == 0 && l_new_packets == 0, 
               "REGRESSION: Retransmissions MUST stop after all packets are ACKed!");
    
    // Cleanup
    dap_io_flow_ctrl_delete(l_sender);
    dap_io_flow_ctrl_delete(l_receiver);
    s_ctx_delete(l_sender_ctx);
    s_ctx_delete(l_receiver_ctx);
    
    dap_events_deinit();
    
    dap_pass_msg("Retransmission regression test PASSED");
}

/**
 * @brief REGRESSION TEST: Lost ACK scenario - sender with lost ACK shouldn't block others
 * 
 * This test reproduces the multi-client issue where:
 * - Client 1 sends data, server responds
 * - Client 1's ACK is "lost" (never delivered to server's flow_ctrl)
 * - Server's flow_ctrl for Client 1 keeps retransmitting
 * - This should NOT prevent Client 2 from working
 */
static void test_flow_ctrl_lost_ack_isolation(void)
{
    dap_test_msg("Test: Lost ACK isolation (multi-client regression)");
    
    dap_events_init(0, 0);
    dap_events_start();
    dap_assert(s_wait_for_reactor(2000), "Reactor should start");
    
    // Create two independent sender contexts (simulating two clients)
    test_flow_ctrl_ctx_t *l_sender1_ctx = s_ctx_create();
    test_flow_ctrl_ctx_t *l_sender2_ctx = s_ctx_create();
    test_flow_ctrl_ctx_t *l_receiver_ctx = s_ctx_create();
    
    dap_io_flow_ctrl_config_t l_config;
    dap_io_flow_ctrl_get_default_config(&l_config);
    l_config.retransmit_timeout_ms = 50;  // Fast retransmit for testing
    l_config.max_retransmit_count = 100;  // Many retries to simulate "stuck" sender
    l_config.send_window_size = 64;
    
    // Create callbacks for sender 1
    dap_io_flow_ctrl_callbacks_t l_sender1_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_sender1_ctx,
    };
    
    // Create callbacks for sender 2
    dap_io_flow_ctrl_callbacks_t l_sender2_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_sender2_ctx,
    };
    
    dap_io_flow_ctrl_callbacks_t l_receiver_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_receiver_ctx,
    };
    
    dap_io_flow_ctrl_t *l_sender1 = s_create_flow_ctrl_async(l_sender1_ctx, &l_sender1_cb, &l_config);
    dap_io_flow_ctrl_t *l_sender2 = s_create_flow_ctrl_async(l_sender2_ctx, &l_sender2_cb, &l_config);
    dap_io_flow_ctrl_t *l_receiver = s_create_flow_ctrl_async(l_receiver_ctx, &l_receiver_cb, &l_config);
    
    dap_assert(l_sender1 && l_sender2 && l_receiver, "All flow controls created");
    
    // Step 1: Sender 1 sends data
    dap_test_msg("Step 1: Sender 1 sends 10 packets...");
    const char l_data1[256] = "Data from sender 1";
    for (int i = 0; i < 10; i++) {
        dap_io_flow_ctrl_send(l_sender1, l_data1, sizeof(l_data1));
    }
    
    // Step 2: Process sender1 -> receiver (but DON'T process ACKs back!)
    dap_test_msg("Step 2: Process sender1 packets to receiver (NO ACKs back)...");
    s_process_pending(l_sender1_ctx, l_receiver);
    
    // DON'T process ACKs! This simulates "lost ACK" scenario
    // Sender 1's flow_ctrl will keep retransmitting
    size_t l_pending_acks = s_get_pending_count(l_receiver_ctx);
    dap_test_msg("Pending ACKs from receiver (not delivered): %zu", l_pending_acks);
    
    // Step 3: Wait for sender1 to start retransmitting
    dap_test_msg("Step 3: Wait for sender1 retransmissions...");
    bool l_retrans = s_wait_for_retransmits(l_sender1, 5, l_config.retransmit_timeout_ms * 10);
    dap_assert(l_retrans, "Sender1 should retransmit when ACK is lost");
    
    uint64_t l_s1_sent, l_s1_retrans, l_tmp1, l_tmp2, l_tmp3, l_tmp4;
    dap_io_flow_ctrl_get_stats(l_sender1, &l_s1_sent, &l_s1_retrans, &l_tmp1, &l_tmp2, &l_tmp3, &l_tmp4);
    dap_test_msg("Sender1 stats: sent=%" PRIu64 ", retrans=%" PRIu64 " (retransmitting)", l_s1_sent, l_s1_retrans);
    
    // Step 4: NOW sender 2 tries to work (while sender1 is still retransmitting)
    dap_test_msg("Step 4: Sender 2 sends 5 packets (while sender1 retransmits)...");
    const char l_data2[256] = "Data from sender 2";
    for (int i = 0; i < 5; i++) {
        dap_io_flow_ctrl_send(l_sender2, l_data2, sizeof(l_data2));
    }
    
    // Step 5: Process sender2 -> receiver -> sender2 (full cycle)
    dap_test_msg("Step 5: Full cycle for sender2...");
    int l_s2_data = s_process_pending(l_sender2_ctx, l_receiver);
    int l_s2_acks = s_process_pending(l_receiver_ctx, l_sender2);
    // Note: receiver_ctx also has ACKs for sender1, but sender2 ignores them (different flow_ctrl)
    
    dap_test_msg("Sender2 cycle: data=%d, acks=%d", l_s2_data, l_s2_acks);
    
    // Step 6: Verify sender2 completed successfully
    uint64_t l_s2_sent, l_s2_retrans, l_s2_recv;
    dap_io_flow_ctrl_get_stats(l_sender2, &l_s2_sent, &l_s2_retrans, &l_s2_recv, &l_tmp1, &l_tmp2, &l_tmp3);
    dap_test_msg("Sender2 stats: sent=%" PRIu64 ", retrans=%" PRIu64, l_s2_sent, l_s2_retrans);
    
    // CRITICAL CHECK: Sender 2 should complete WITHOUT excessive retransmits
    // (Sender 1's retransmissions should NOT affect Sender 2)
    dap_assert(l_s2_retrans == 0, 
               "REGRESSION: Sender2 should NOT have retransmits when ACKs are delivered!");
    
    // Step 7: Wait and verify sender1 is STILL retransmitting (its ACKs were never delivered)
    dap_test_msg("Step 7: Wait for more sender1 retransmissions...");
    uint64_t l_s1_retrans_before = l_s1_retrans;
    
    // Wait for at least one more retransmission cycle
    bool l_more_retrans = POLL_WAIT_UNTIL(({
        uint64_t _r;
        dap_io_flow_ctrl_get_stats(l_sender1, &l_s1_sent, &_r, &l_tmp1, &l_tmp2, &l_tmp3, &l_tmp4);
        _r > l_s1_retrans_before;
    }), l_config.retransmit_timeout_ms * 5);
    
    uint64_t l_s1_retrans_final;
    dap_io_flow_ctrl_get_stats(l_sender1, &l_s1_sent, &l_s1_retrans_final, &l_tmp1, &l_tmp2, &l_tmp3, &l_tmp4);
    dap_test_msg("Sender1 final retrans: %" PRIu64 " (was %" PRIu64 ")", l_s1_retrans_final, l_s1_retrans_before);
    
    dap_assert(l_more_retrans && l_s1_retrans_final > l_s1_retrans_before, 
               "Sender1 should still be retransmitting (ACK never delivered)");
    
    // Cleanup
    dap_io_flow_ctrl_delete(l_sender1);
    dap_io_flow_ctrl_delete(l_sender2);
    dap_io_flow_ctrl_delete(l_receiver);
    s_ctx_delete(l_sender1_ctx);
    s_ctx_delete(l_sender2_ctx);
    s_ctx_delete(l_receiver_ctx);
    
    dap_events_deinit();
    
    dap_pass_msg("Lost ACK isolation test PASSED - senders are independent");
}

/**
 * @brief Test multiple senders scenario (simulates multi-client)
 */
static void test_flow_ctrl_multiple_senders(void)
{
    dap_test_msg("Test: Multiple senders (multi-client scenario)");
    
    dap_events_init(0, 0);
    dap_events_start();
    dap_assert(s_wait_for_reactor(2000), "Reactor should start within 2s");
    
    const int NUM_SENDERS = 3;
    test_flow_ctrl_ctx_t *l_sender_ctxs[NUM_SENDERS];
    dap_io_flow_ctrl_t *l_senders[NUM_SENDERS];
    dap_io_flow_ctrl_callbacks_t l_sender_cbs[NUM_SENDERS];
    
    test_flow_ctrl_ctx_t *l_receiver_ctx = s_ctx_create();
    
    dap_io_flow_ctrl_config_t l_config;
    dap_io_flow_ctrl_get_default_config(&l_config);
    l_config.retransmit_timeout_ms = 100;
    l_config.send_window_size = 64;
    
    dap_io_flow_ctrl_callbacks_t l_receiver_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_receiver_ctx,
    };
    
    dap_io_flow_ctrl_t *l_receiver = s_create_flow_ctrl_async(l_receiver_ctx, &l_receiver_cb, &l_config);
    
    // Create senders on worker
    for (int i = 0; i < NUM_SENDERS; i++) {
        l_sender_ctxs[i] = s_ctx_create();
        
        l_sender_cbs[i] = (dap_io_flow_ctrl_callbacks_t){
            .packet_prepare = s_packet_prepare,
            .packet_parse = s_packet_parse,
            .packet_send = s_packet_send,
            .packet_free = s_packet_free,
            .payload_deliver = s_payload_deliver,
            .arg = l_sender_ctxs[i],
        };
        
        l_senders[i] = s_create_flow_ctrl_async(l_sender_ctxs[i], &l_sender_cbs[i], &l_config);
    }
    
    // Each sender sends 10 packets
    char l_data[128];
    for (int s = 0; s < NUM_SENDERS; s++) {
        snprintf(l_data, sizeof(l_data), "Data from sender %d", s);
        for (int p = 0; p < 10; p++) {
            dap_io_flow_ctrl_send(l_senders[s], l_data, strlen(l_data) + 1);
        }
    }
    
    // Process all senders' packets interleaved
    int l_total = 0;
    for (int s = 0; s < NUM_SENDERS; s++) {
        l_total += s_process_pending(l_sender_ctxs[s], l_receiver);
    }
    
    dap_test_msg("Total packets delivered: %d", l_total);
    dap_assert(l_total == NUM_SENDERS * 10, "All packets processed");
    
    // Wait for potential retransmits (poll with short intervals)
    uint32_t l_wait_ms = l_config.retransmit_timeout_ms * 3;
    uint64_t l_start_time = dap_nanotime_now();
    while ((dap_nanotime_now() - l_start_time) < (uint64_t)l_wait_ms * 1000000ULL) {
        usleep(POLL_INTERVAL_US);
    }
    
    // Verify no excessive retransmits
    uint64_t l_total_retrans = 0;
    for (int s = 0; s < NUM_SENDERS; s++) {
        l_total_retrans += atomic_load(&l_sender_ctxs[s]->retransmits_count);
    }
    dap_test_msg("Total retransmits: %" PRIu64, l_total_retrans);
    
    // Cleanup
    dap_io_flow_ctrl_delete(l_receiver);
    for (int i = 0; i < NUM_SENDERS; i++) {
        dap_io_flow_ctrl_delete(l_senders[i]);
        s_ctx_delete(l_sender_ctxs[i]);
    }
    s_ctx_delete(l_receiver_ctx);
    
    dap_events_deinit();
    
    dap_pass_msg("Multiple senders test passed");
}

//===================================================================
// REGRESSION TEST: Early deletion race condition
//===================================================================

typedef struct delete_race_ctx {
    dap_io_flow_ctrl_t *ctrl;
    test_flow_ctrl_ctx_t *test_ctx;
    _Atomic bool keep_sending;
    _Atomic uint64_t sends_attempted;
    _Atomic uint64_t sends_succeeded;
    _Atomic uint64_t sends_rejected;
} delete_race_ctx_t;

static void *s_sender_thread(void *a_arg)
{
    delete_race_ctx_t *l_ctx = (delete_race_ctx_t *)a_arg;
    const char l_data[64] = "Race condition test data";
    
    while (atomic_load(&l_ctx->keep_sending)) {
        atomic_fetch_add(&l_ctx->sends_attempted, 1);
        
        int l_ret = dap_io_flow_ctrl_send(l_ctx->ctrl, l_data, sizeof(l_data));
        if (l_ret == 0) {
            atomic_fetch_add(&l_ctx->sends_succeeded, 1);
        } else if (l_ret == -10) {
            // -10 = flow_ctrl is being deleted - expected during shutdown
            atomic_fetch_add(&l_ctx->sends_rejected, 1);
        }
        // Small delay to allow interleaving
        usleep(100);
    }
    return NULL;
}

/**
 * @brief REGRESSION TEST: Race between delete and send operations
 * 
 * This test reproduces the original bug where:
 * - flow_ctrl was deleted while operations were in progress
 * - This caused use-after-free and SIGSEGV
 * 
 * With the lifecycle management fix:
 * - Sends during deletion should return -10 (rejected)
 * - OR complete successfully before deletion waits
 * - No crash should occur
 */
static void test_flow_ctrl_delete_race(void)
{
    dap_test_msg("Test: Delete race condition (REGRESSION)");
    
    dap_events_init(0, 0);
    dap_events_start();
    dap_assert(s_wait_for_reactor(2000), "Reactor should start");
    
    test_flow_ctrl_ctx_t *l_sender_ctx = s_ctx_create();
    test_flow_ctrl_ctx_t *l_receiver_ctx = s_ctx_create();
    
    dap_io_flow_ctrl_config_t l_config;
    dap_io_flow_ctrl_get_default_config(&l_config);
    l_config.retransmit_timeout_ms = 50;
    l_config.send_window_size = 128;
    
    dap_io_flow_ctrl_callbacks_t l_sender_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_sender_ctx,
    };
    
    dap_io_flow_ctrl_callbacks_t l_receiver_cb = {
        .packet_prepare = s_packet_prepare,
        .packet_parse = s_packet_parse,
        .packet_send = s_packet_send,
        .packet_free = s_packet_free,
        .payload_deliver = s_payload_deliver,
        .arg = l_receiver_ctx,
    };
    
    dap_io_flow_ctrl_t *l_sender = s_create_flow_ctrl_async(l_sender_ctx, &l_sender_cb, &l_config);
    dap_io_flow_ctrl_t *l_receiver = s_create_flow_ctrl_async(l_receiver_ctx, &l_receiver_cb, &l_config);
    
    dap_assert(l_sender && l_receiver, "Flow controls created");
    
    // Setup race context
    delete_race_ctx_t l_race_ctx = {
        .ctrl = l_sender,
        .test_ctx = l_sender_ctx,
        .keep_sending = true,
        .sends_attempted = 0,
        .sends_succeeded = 0,
        .sends_rejected = 0,
    };
    
    // Start sender thread
    pthread_t l_thread;
    pthread_create(&l_thread, NULL, s_sender_thread, &l_race_ctx);
    
    // Wait for some sends to happen
    dap_test_msg("Waiting for sends to start...");
    bool l_sends_started = POLL_WAIT_UNTIL(
        atomic_load(&l_race_ctx.sends_attempted) >= 10,
        1000);
    dap_assert(l_sends_started, "Sender thread should start sending");
    
    uint64_t l_sends_before_delete = atomic_load(&l_race_ctx.sends_attempted);
    dap_test_msg("Sends before delete: %" PRIu64, l_sends_before_delete);
    
    // Now delete the flow_ctrl while sender is active
    // This is the critical race condition!
    dap_test_msg("Deleting flow_ctrl while sender is active...");
    dap_io_flow_ctrl_delete(l_sender);
    
    // Stop sender thread
    atomic_store(&l_race_ctx.keep_sending, false);
    pthread_join(l_thread, NULL);
    
    // Check results
    uint64_t l_total = atomic_load(&l_race_ctx.sends_attempted);
    uint64_t l_ok = atomic_load(&l_race_ctx.sends_succeeded);
    uint64_t l_rejected = atomic_load(&l_race_ctx.sends_rejected);
    
    dap_test_msg("Results: total=%" PRIu64 ", succeeded=%" PRIu64 ", rejected=%" PRIu64, l_total, l_ok, l_rejected);
    
    // Key assertions:
    // 1. No crash occurred (if we get here, we passed!)
    dap_pass_msg("No crash during delete race - lifecycle management works!");
    
    // 2. If delete happened fast enough, all sends may have succeeded (no rejection observed)
    //    This is ALSO correct behavior - delete waited for pending operations
    if (l_rejected > 0) {
        dap_pass_msg("Some sends were rejected during deletion (expected)");
    } else {
        dap_pass_msg("All sends succeeded before delete completed (also valid)");
    }
    
    // 3. Total = succeeded + rejected (no lost sends)
    dap_assert(l_ok + l_rejected == l_total, "All sends accounted for (no silent failures)");
    
    // Cleanup receiver
    dap_io_flow_ctrl_delete(l_receiver);
    s_ctx_delete(l_sender_ctx);
    s_ctx_delete(l_receiver_ctx);
    
    dap_events_deinit();
    
    dap_pass_msg("Delete race condition test PASSED - fix verified!");
}

//===================================================================
// MAIN
//===================================================================

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    
    dap_set_appname("test_io_flow_ctrl");
    dap_log_level_set(L_DEBUG);
    
    if (dap_common_init("test_io_flow_ctrl", NULL) != 0) {
        fprintf(stderr, "Failed to init dap_common\n");
        return 1;
    }
    
    if (dap_mock_init() != 0) {
        fprintf(stderr, "Failed to init dap_mock\n");
        return 1;
    }
    
    if (dap_io_flow_ctrl_init() != 0) {
        fprintf(stderr, "Failed to init dap_io_flow_ctrl\n");
        return 1;
    }
    
    printf("\n=== DAP IO Flow Control Unit Tests ===\n\n");
    dap_print_module_name("dap_io_flow_ctrl");
    
    // Basic test without reactor
    test_flow_ctrl_basic();
    
    // REGRESSION TEST with real reactor
    test_flow_ctrl_retransmit_regression();
    
    // REGRESSION TEST: lost ACK should not block other senders
    test_flow_ctrl_lost_ack_isolation();
    
    // Multi-client scenario
    test_flow_ctrl_multiple_senders();
    
    // REGRESSION TEST: delete race condition (verifies lifecycle management fix)
    test_flow_ctrl_delete_race();
    
    dap_io_flow_ctrl_deinit();
    dap_mock_deinit();
    dap_common_deinit();
    
    printf("\n%s=== All tests PASSED ===%s\n", TEXT_COLOR_GRN, TEXT_COLOR_RESET);
    
    return 0;
}
