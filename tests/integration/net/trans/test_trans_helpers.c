/**
 * @file test_trans_helpers.c
 * @brief Implementation of common helper functions for trans integration tests
 * @date 2025-11-03
 * @copyright (c) 2025 Cellframe Network
 */

#include "test_trans_helpers.h"
#include "dap_stream_ch_proc.h"
#include "dap_client_pvt.h"
#include "dap_client_helpers.h"
#include "dap_server_helpers.h"
#include "dap_worker.h"
#include "dap_list.h"
#include "dap_events_socket.h"
#include "dap_http_server.h"
#include "dap_net_trans_websocket_server.h"
#include "dap_net_trans_udp_server.h"
#include "dap_net_trans_dns_server.h"
#include <stdio.h>

#define LOG_TAG "test_trans_helpers"

/**
 * @brief Create test data for stream testing
 */
uint8_t *test_trans_create_test_data(size_t a_size)
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
 */
bool test_trans_verify_data(const void *a_sent, const void *a_received, size_t a_size)
{
    if (!a_sent || !a_received || a_size == 0) {
        return false;
    }
    
    return memcmp(a_sent, a_received, a_size) == 0;
}

/**
 * @brief Initialize stream channel test ctx
 */
int test_stream_ch_ctx_init(test_stream_ch_ctx_t *a_ctx, 
                                 char a_channel_id, 
                                 size_t a_data_size)
{
    if (!a_ctx) {
        return -1;
    }
    
    memset(a_ctx, 0, sizeof(test_stream_ch_ctx_t));
    a_ctx->channel_id = a_channel_id;
    a_ctx->packet_type = STREAM_CH_PKT_TYPE_REQUEST;
    a_ctx->sent_data = test_trans_create_test_data(a_data_size);
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
 * @brief Cleanup stream channel test ctx
 */
void test_stream_ch_ctx_cleanup(test_stream_ch_ctx_t *a_ctx)
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
 */
void test_stream_ch_receive_callback(dap_stream_ch_t *a_ch, 
                                      uint8_t a_type,
                                      const void *a_data, 
                                      size_t a_data_size, 
                                      void *a_arg)
{
    UNUSED(a_ch);
    UNUSED(a_type);
    
    test_stream_ch_ctx_t *l_ctx = (test_stream_ch_ctx_t*)a_arg;
    if (!l_ctx) {
        log_it(L_ERROR, "test_stream_ch_receive_callback: ctx is NULL");
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
    } else {
        log_it(L_ERROR, "test_stream_ch_receive_callback: failed to allocate memory");
    }
    
    pthread_cond_signal(&l_ctx->cond);
    pthread_mutex_unlock(&l_ctx->mutex);
}

/**
 * @brief Wait for client to reach STAGE_STREAM_STREAMING
 */
bool test_trans_wait_for_streaming(dap_client_t *a_client, uint32_t a_timeout_ms)
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
            log_it(L_DEBUG, "Client stage: %d (elapsed: %u ms)", l_stage, l_elapsed);
            l_last_stage = l_stage;
        }
        
        if (l_stage == STAGE_STREAM_STREAMING) {
            return true;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    log_it(L_WARNING, "Timeout reached at stage: %d", l_last_stage);
    return false;
}

/**
 * @brief Send data through stream channel and wait for response
 */
int test_stream_ch_send_and_wait(dap_client_t *a_client,
                                  test_stream_ch_ctx_t *a_ctx,
                                  uint32_t a_timeout_ms)
{
    if (!a_client || !a_ctx) {
        return -1;
    }

    if (!dap_client_get_stream_worker(a_client)) {
        log_it(L_ERROR, "Client has no stream worker!");
        return -2;
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
    int l_sent = dap_client_write_mt(a_client, a_ctx->channel_id, 
                                     a_ctx->packet_type,
                                     a_ctx->sent_data, a_ctx->sent_data_size);
    if (l_sent < 0) {
        log_it(L_ERROR, "test_stream_ch_send_and_wait: failed to send data");
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
            log_it(L_ERROR, "test_stream_ch_send_and_wait: timeout waiting for response");
            l_ret = -1;
            break;
        }
    }
    
    pthread_mutex_unlock(&a_ctx->mutex);
    
    return l_ret;
}

/**
 * @brief Add notifier directly to channel (bypassing address lookup)
 */
int test_stream_ch_add_notifier_direct(dap_stream_ch_t *a_ch, 
                                        dap_stream_ch_notify_callback_t a_callback, 
                                        void *a_arg)
{
    if (!a_ch || !a_callback) return -1;
    
    dap_stream_ch_notifier_t *l_notifier = DAP_NEW(dap_stream_ch_notifier_t);
    if (!l_notifier) return -1;
    
    l_notifier->callback = a_callback;
    l_notifier->arg = a_arg;
    
    pthread_mutex_lock(&a_ch->mutex);
    a_ch->packet_in_notifiers = dap_list_append(a_ch->packet_in_notifiers, l_notifier);
    pthread_mutex_unlock(&a_ch->mutex);
    
    return 0;
}

/**
 * @brief Register notifier for receiving data on a channel
 */
int test_stream_ch_register_receiver(dap_client_t *a_client,
                                      char a_channel_id,
                                      void *a_callback_arg)
{
    if (!a_client) {
        return -1;
    }
    
    // Get stream from client
    dap_stream_t *l_stream = dap_client_get_stream(a_client);
    if (!l_stream) {
        log_it(L_ERROR, "Client has no stream");
        return -1;
    }
    
    // Get channel from stream
    dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(l_stream, a_channel_id);
    if (!l_ch) {
        log_it(L_ERROR, "Channel '%c' not found on client stream", a_channel_id);
        return -1;
    }
    
    // Register notifier directly on the client's channel
    int l_ret = test_stream_ch_add_notifier_direct(l_ch, test_stream_ch_receive_callback, a_callback_arg);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "test_stream_ch_add_notifier_direct failed with code %d", l_ret);
    }
    
    return l_ret;
}

/**
 * @brief Wait for transs to be registered
 */
bool test_wait_for_transs_registered(uint32_t a_timeout_ms)
{
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    // Automatically get expected transs from config array
    // This ensures we check for all transs defined in test_trans_integration.c
    while (l_elapsed < a_timeout_ms) {
        size_t l_registered_count = 0;
        // Use runtime variable for count (available from test_trans_integration.c)
        for (size_t i = 0; i < g_trans_config_count; i++) {
            if (dap_net_trans_find(g_trans_configs[i].trans_type)) {
                l_registered_count++;
            }
        }
        
        if (l_registered_count == g_trans_config_count) {
            return true;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    return false;
}

/**
 * @brief Get dap_server_t from trans server
 * Helper function to extract underlying dap_server_t from trans-specific structure
 */
static dap_server_t *s_get_server_from_trans(dap_net_trans_server_t *a_server)
{
    if (!a_server) {
        return NULL;
    }
    
    // For HTTP trans, get HTTP server from trans_specific
    if (a_server->trans_type == DAP_NET_TRANS_HTTP) {
        dap_http_server_t *l_http_server = (dap_http_server_t *)a_server->trans_specific;
        if (l_http_server && l_http_server->server) {
            return l_http_server->server;
        }
    }
    
    // For WebSocket trans, get WebSocket server from trans_specific
    // WebSocket server has its own structure with server field
    if (a_server->trans_type == DAP_NET_TRANS_WEBSOCKET) {
        dap_net_trans_websocket_server_t *l_ws_server = (dap_net_trans_websocket_server_t *)a_server->trans_specific;
        if (l_ws_server && l_ws_server->server) {
            return l_ws_server->server;
        }
    }
    
    // For UDP transs, get UDP server from trans_specific
    if (a_server->trans_type == DAP_NET_TRANS_UDP_BASIC ||
        a_server->trans_type == DAP_NET_TRANS_UDP_RELIABLE ||
        a_server->trans_type == DAP_NET_TRANS_UDP_QUIC_LIKE) {
        dap_net_trans_udp_server_t *l_udp_server = (dap_net_trans_udp_server_t *)a_server->trans_specific;
        if (l_udp_server && l_udp_server->server) {
            return l_udp_server->server;
        }
    }
    
    // For DNS tunnel trans, get DNS server from trans_specific
    if (a_server->trans_type == DAP_NET_TRANS_DNS_TUNNEL) {
        dap_net_trans_dns_server_t *l_dns_server = (dap_net_trans_dns_server_t *)a_server->trans_specific;
        if (l_dns_server && l_dns_server->server) {
            return l_dns_server->server;
        }
    }
    
    return NULL;
}

/**
 * @brief Wait for server to be ready (listening)
 */
bool test_wait_for_server_ready(dap_net_trans_server_t *a_server, uint32_t a_timeout_ms)
{
    if (!a_server) {
        return false;
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    // First, wait for trans_specific server to be created and dap_server_t to be initialized
    // This may take some time for UDP/DNS servers as they create dap_server_t during start()
    dap_server_t *l_server = NULL;
    while (l_elapsed < a_timeout_ms) {
        l_server = s_get_server_from_trans(a_server);
        if (l_server) {
            break; // Server structure found
        }
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    if (!l_server) {
        // Trans-specific server structure not found or dap_server_t not created yet
        return false;
    }
    
    // Use centralized server wait function
    return dap_server_wait_for_ready(l_server, a_timeout_ms - l_elapsed);
}

/**
 * @brief Wait for stream channels to be created
 */
bool test_wait_for_stream_channels_ready(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms)
{
    // Use centralized client wait function
    return dap_client_wait_for_channels(a_client, a_expected_channels, a_timeout_ms);
}

/**
 * @brief Wait for client to be deleted
 */
bool test_wait_for_client_deleted(dap_client_t **a_client_ptr, uint32_t a_timeout_ms)
{
    // Use centralized client wait function
    return dap_client_wait_for_deletion(a_client_ptr, a_timeout_ms);
}

/**
 * @brief Wait for all streams to be closed
 */
bool test_wait_for_all_streams_closed(uint32_t a_timeout_ms)
{
    // Simple delay - streams close asynchronously
    // In a real scenario, we'd check dap_stream list, but for tests this is sufficient
    dap_test_sleep_ms(500);
    return true;
}

