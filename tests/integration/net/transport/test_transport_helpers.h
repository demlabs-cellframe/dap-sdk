/**
 * @file test_transport_helpers.h
 * @brief Common helper functions for transport integration tests
 * @date 2025-11-01
 * @copyright (c) 2025 Cellframe Network
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <errno.h>
#include "dap_common.h"
#include "dap_client.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream.h"
#include "dap_test.h"
#include "dap_test_async.h"

/**
 * @brief Create test data for stream testing
 * @param a_size Size of test data to create
 * @return Pointer to allocated test data (caller must free with DAP_DELETE)
 */
static inline uint8_t *test_transport_create_test_data(size_t a_size)
{
    uint8_t *l_data = DAP_NEW_SIZE(uint8_t, a_size);
    if (!l_data) {
        return NULL;
    }
    
    // Fill with pattern: first 4 bytes are size, rest is incrementing pattern
    memset(l_data, 0, a_size);
    *(uint32_t *)l_data = (uint32_t)a_size;
    for (size_t i = 4; i < a_size; i++) {
        l_data[i] = (uint8_t)(i % 256);
    }
    
    return l_data;
}

/**
 * @brief Verify stream data integrity
 * @param a_sent Original sent data
 * @param a_received Received data
 * @param a_size Size of data to compare
 * @return true if data matches, false otherwise
 */
static inline bool test_transport_verify_data(const void *a_sent, const void *a_received, size_t a_size)
{
    if (!a_sent || !a_received || a_size == 0) {
        return false;
    }
    
    return memcmp(a_sent, a_received, a_size) == 0;
}

/**
 * @brief Default test data size for stream tests
 */
#define TEST_TRANSPORT_STREAM_DATA_SIZE 1024

/**
 * @brief Default test server address
 */
#define TEST_TRANSPORT_SERVER_ADDR "127.0.0.1"

/**
 * @brief Default test server ports for different transports
 */
#define TEST_WEBSOCKET_SERVER_PORT 18100
#define TEST_HTTP_SERVER_PORT      18101
#define TEST_UDP_SERVER_PORT       18102
#define TEST_DNS_SERVER_PORT       18103

/**
 * @brief Default test timeout in seconds
 */
#define TEST_TRANSPORT_TIMEOUT_SEC 60

// ============================================================================
// Stream Channel Test Helpers
// ============================================================================

/**
 * @brief Test context for stream channel data exchange
 */
typedef struct test_stream_ch_context {
    uint8_t *sent_data;
    size_t sent_data_size;
    uint8_t *received_data;
    size_t received_data_size;
    bool data_received;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char channel_id;
    uint8_t packet_type;
} test_stream_ch_context_t;

/**
 * @brief Initialize stream channel test context
 * @param a_ctx Context to initialize
 * @param a_channel_id Channel ID (e.g., 'A', 'B', 'C')
 * @param a_data_size Size of test data
 * @return 0 on success, -1 on failure
 */
static inline int test_stream_ch_context_init(test_stream_ch_context_t *a_ctx, 
                                               char a_channel_id, 
                                               size_t a_data_size)
{
    if (!a_ctx) {
        return -1;
    }
    
    memset(a_ctx, 0, sizeof(test_stream_ch_context_t));
    a_ctx->channel_id = a_channel_id;
    a_ctx->packet_type = STREAM_CH_PKT_TYPE_REQUEST;
    a_ctx->sent_data = test_transport_create_test_data(a_data_size);
    if (!a_ctx->sent_data) {
        return -1;
    }
    a_ctx->sent_data_size = a_data_size;
    a_ctx->received_data = NULL;
    a_ctx->received_data_size = 0;
    a_ctx->data_received = false;
    
    pthread_mutex_init(&a_ctx->mutex, NULL);
    pthread_cond_init(&a_ctx->cond, NULL);
    
    return 0;
}

/**
 * @brief Cleanup stream channel test context
 * @param a_ctx Context to cleanup
 */
static inline void test_stream_ch_context_cleanup(test_stream_ch_context_t *a_ctx)
{
    if (!a_ctx) {
        return;
    }
    
    DAP_DELETE(a_ctx->sent_data);
    DAP_DELETE(a_ctx->received_data);
    pthread_mutex_destroy(&a_ctx->mutex);
    pthread_cond_destroy(&a_ctx->cond);
}

/**
 * @brief Callback for receiving data through stream channel
 * @param a_ch Channel that received the packet
 * @param a_type Packet type
 * @param a_data Packet data
 * @param a_data_size Packet data size
 * @param a_arg Context pointer (test_stream_ch_context_t*)
 */
static inline void test_stream_ch_receive_callback(dap_stream_ch_t *a_ch, 
                                                    uint8_t a_type,
                                                    const void *a_data, 
                                                    size_t a_data_size, 
                                                    void *a_arg)
{
    UNUSED(a_ch);
    UNUSED(a_type);
    
    test_stream_ch_context_t *l_ctx = (test_stream_ch_context_t*)a_arg;
    if (!l_ctx) {
        return;
    }
    
    pthread_mutex_lock(&l_ctx->mutex);
    
    // Allocate buffer for received data
    if (l_ctx->received_data) {
        DAP_DELETE(l_ctx->received_data);
    }
    
    l_ctx->received_data = DAP_NEW_SIZE(uint8_t, a_data_size);
    if (l_ctx->received_data) {
        memcpy(l_ctx->received_data, a_data, a_data_size);
        l_ctx->received_data_size = a_data_size;
        l_ctx->data_received = true;
    }
    
    pthread_cond_signal(&l_ctx->cond);
    pthread_mutex_unlock(&l_ctx->mutex);
}

/**
 * @brief Wait for client to reach STAGE_STREAM_STREAMING
 * @param a_client Client to wait for
 * @param a_timeout_ms Timeout in milliseconds
 * @return true if streaming stage reached, false on timeout
 */
static inline bool test_transport_wait_for_streaming(dap_client_t *a_client, uint32_t a_timeout_ms)
{
    if (!a_client) {
        return false;
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    dap_client_stage_t l_last_stage = STAGE_UNDEFINED;
    
    while (l_elapsed < a_timeout_ms) {
        dap_client_stage_t l_stage = dap_client_get_stage(a_client);
        if (l_stage != l_last_stage) {
            printf("  Client stage: %d (elapsed: %u ms)\n", l_stage, l_elapsed);
            l_last_stage = l_stage;
        }
        
        if (l_stage == STAGE_STREAM_STREAMING) {
            return true;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    printf("  Timeout reached at stage: %d\n", l_last_stage);
    return false;
}

/**
 * @brief Send data through stream channel and wait for response
 * @param a_client Client to send data through
 * @param a_ctx Test context (contains sent/received data)
 * @param a_timeout_ms Timeout in milliseconds
 * @return 0 on success, -1 on failure
 */
static inline int test_stream_ch_send_and_wait(dap_client_t *a_client,
                                                test_stream_ch_context_t *a_ctx,
                                                uint32_t a_timeout_ms)
{
    if (!a_client || !a_ctx) {
        return -1;
    }
    
    // Reset receive flag
    pthread_mutex_lock(&a_ctx->mutex);
    a_ctx->data_received = false;
    if (a_ctx->received_data) {
        DAP_DELETE(a_ctx->received_data);
        a_ctx->received_data = NULL;
        a_ctx->received_data_size = 0;
    }
    pthread_mutex_unlock(&a_ctx->mutex);
    
    // Send data
    ssize_t l_sent = dap_client_write_unsafe(a_client, a_ctx->channel_id, 
                                              a_ctx->packet_type,
                                              a_ctx->sent_data, a_ctx->sent_data_size);
    if (l_sent < 0) {
        return -1;
    }
    
    // Wait for response
    pthread_mutex_lock(&a_ctx->mutex);
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += a_timeout_ms / 1000;
    l_timeout.tv_nsec += (a_timeout_ms % 1000) * 1000000;
    if (l_timeout.tv_nsec >= 1000000000) {
        l_timeout.tv_sec++;
        l_timeout.tv_nsec -= 1000000000;
    }
    
    int l_ret = 0;
    while (!a_ctx->data_received) {
        int l_cond_ret = pthread_cond_timedwait(&a_ctx->cond, &a_ctx->mutex, &l_timeout);
        if (l_cond_ret == ETIMEDOUT) {
            l_ret = -1;
            break;
        }
    }
    
    pthread_mutex_unlock(&a_ctx->mutex);
    
    return l_ret;
}

/**
 * @brief Register notifier for receiving data on a channel
 * @param a_client Client to register notifier for
 * @param a_channel_id Channel ID
 * @param a_callback_arg Context pointer (test_stream_ch_context_t*)
 * @return 0 on success, -1 on failure
 */
static inline int test_stream_ch_register_receiver(dap_client_t *a_client,
                                                    char a_channel_id,
                                                    void *a_callback_arg)
{
    if (!a_client) {
        return -1;
    }
    
    // Get stream from client
    dap_stream_t *l_stream = dap_client_get_stream(a_client);
    if (!l_stream) {
        return -1;
    }
    
    dap_stream_node_addr_t l_node_addr = l_stream->node;
    
    // Register notifier for incoming packets
    int l_ret = dap_stream_ch_add_notifier(&l_node_addr, 
                                           (uint8_t)a_channel_id,
                                           DAP_STREAM_PKT_DIR_IN,
                                           test_stream_ch_receive_callback,
                                           a_callback_arg);
    
    return l_ret;
}
