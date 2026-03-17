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
#include "dap_io_flow.h"
#include <stdio.h>

#define LOG_TAG "test_trans_helpers"

static bool s_debug_more = false;

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
    
    log_it(L_INFO, "test_stream_ch_receive_callback: RECEIVED %zu bytes on channel '%c', type=0x%02x", 
           a_data_size, a_ch->proc->id, a_type);
    
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
    
    log_it(L_INFO, "test_stream_ch_send_and_wait: START sending %zu bytes on channel '%c'",
           a_ctx->sent_data_size, a_ctx->channel_id);
    
    // Send data (without mutex - dap_client_write_mt is thread-safe)
    int l_sent = dap_client_write_mt(a_client, a_ctx->channel_id, 
                                     a_ctx->packet_type,
                                     a_ctx->sent_data, a_ctx->sent_data_size);
    if (l_sent < 0) {
        log_it(L_ERROR, "test_stream_ch_send_and_wait: failed to send data");
        return -1;
    }
    
    log_it(L_INFO, "test_stream_ch_send_and_wait: Data sent, waiting for response...");
    
    // Wait for response with proper condition variable pattern
    // CRITICAL: Check condition BEFORE entering wait to prevent lost wakeup!
    pthread_mutex_lock(&a_ctx->mutex);
    
    // Prepare timeout
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += a_timeout_ms / 1000;
    l_timeout.tv_nsec += (a_timeout_ms % 1000) * 1000000;
    if (l_timeout.tv_nsec >= 1000000000) {
        l_timeout.tv_sec++;
        l_timeout.tv_nsec -= 1000000000;
    }
    
    int l_ret = 0;
    
    // Check condition FIRST - response may have arrived during send!
    // This prevents lost wakeup if pthread_cond_signal came before pthread_cond_timedwait
    if (!a_ctx->data_received) {
        log_it(L_INFO, "test_stream_ch_send_and_wait: Entering wait loop, data_received=%s",
               a_ctx->data_received ? "true" : "false");
        // Only wait if data not yet received
        while (!a_ctx->data_received) {
            int l_cond_ret = pthread_cond_timedwait(&a_ctx->cond, &a_ctx->mutex, &l_timeout);
            if (l_cond_ret == ETIMEDOUT) {
                log_it(L_ERROR, "test_stream_ch_send_and_wait: timeout waiting for response (data_received=%s)",
                       a_ctx->data_received ? "true" : "false");
                l_ret = -1;
                break;
            } else if (l_cond_ret != 0 && l_cond_ret != ETIMEDOUT) {
                // Other errors (EINVAL, etc.)
                log_it(L_ERROR, "test_stream_ch_send_and_wait: pthread_cond_timedwait error: %d", l_cond_ret);
                l_ret = -1;
                break;
            }
            // Spurious wakeup protection: loop continues if data_received is still false
            log_it(L_DEBUG, "test_stream_ch_send_and_wait: Woke up, data_received=%s",
                   a_ctx->data_received ? "true" : "false");
        }
    } else {
        // Data already received during send - no need to wait!
        log_it(L_INFO, "test_stream_ch_send_and_wait: data already received (fast response)");
    }
    
    pthread_mutex_unlock(&a_ctx->mutex);
    
    log_it(L_INFO, "test_stream_ch_send_and_wait: DONE, ret=%d, data_received=%s",
           l_ret, a_ctx->data_received ? "true" : "false");
    
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
/**
 * @brief Структура аргументов для регистрации notifier
 */
struct register_notifier_args {
    dap_stream_t *stream;
    char channel_id;
    test_stream_ch_ctx_t *ctx;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
    int *result;
};

/**
 * @brief Callback для регистрации notifier в worker потоке
 */
static void s_register_notifier_in_worker_callback(void *a_arg)
{
    static bool s_initialized = false;
    if (!s_initialized) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "test_trans_helpers", "debug_more", false);
        s_initialized = true;
    }
    
    struct register_notifier_args *l_args = (struct register_notifier_args *)a_arg;
    
    if (!l_args || !l_args->stream) {
        log_it(L_ERROR, "s_register_notifier_in_worker_callback: invalid args");
        if (l_args) {
            pthread_mutex_lock(l_args->mutex);
            *l_args->result = -1;
            pthread_cond_signal(l_args->cond);
            pthread_mutex_unlock(l_args->mutex);
            DAP_DELETE(l_args);
        }
        return;
    }
    
    dap_worker_t *l_current_worker = dap_worker_get_current();
    log_it(L_INFO, "s_register_notifier_in_worker_callback: EXECUTING in worker=%p, stream=%p, channel_id='%c'",
           l_current_worker, l_args->stream, l_args->channel_id);
    
    // Get channel from stream (now in worker thread!)
    dap_stream_ch_t *l_ch = dap_stream_ch_by_id_unsafe(l_args->stream, l_args->channel_id);
    if (!l_ch) {
        log_it(L_ERROR, "s_register_notifier_in_worker_callback: Channel '%c' not found", l_args->channel_id);
        pthread_mutex_lock(l_args->mutex);
        *l_args->result = -1;
        pthread_cond_signal(l_args->cond);
        pthread_mutex_unlock(l_args->mutex);
        DAP_DELETE(l_args);
        return;
    }
    
    debug_if(s_debug_more, L_DEBUG, "s_register_notifier_in_worker_callback: found channel '%c' (%p), notifiers_before=%zu",
           l_args->channel_id, l_ch, dap_list_length(l_ch->packet_in_notifiers));
    
    // Register notifier directly on the channel (в worker потоке!)
    int l_ret = test_stream_ch_add_notifier_direct(l_ch, test_stream_ch_receive_callback, l_args->ctx);
    
    if (l_ret != 0) {
        log_it(L_ERROR, "s_register_notifier_in_worker_callback: test_stream_ch_add_notifier_direct failed with code %d", l_ret);
    } else {
        debug_if(s_debug_more, L_DEBUG, "s_register_notifier_in_worker_callback: notifier registered, notifiers_after=%zu",
               dap_list_length(l_ch->packet_in_notifiers));
    }
    
    // Signal completion
    pthread_mutex_lock(l_args->mutex);
    *l_args->result = l_ret;
    pthread_cond_signal(l_args->cond);
    pthread_mutex_unlock(l_args->mutex);
    
    DAP_DELETE(l_args);
}

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
    
    // Get worker from client
    dap_client_esocket_t *l_client_esocket = DAP_CLIENT_ESOCKET(a_client);
    if (!l_client_esocket || !l_client_esocket->worker) {
        log_it(L_ERROR, "Client has no worker");
        return -1;
    }
    
    dap_worker_t *l_current_worker = dap_worker_get_current();
    
    log_it(L_INFO, "test_stream_ch_register_receiver: stream=%p, channel_id='%c', client_worker=%p, current_worker=%p (main_thread=%s)",
           l_stream, a_channel_id, l_client_esocket->worker, l_current_worker,
           l_current_worker ? "NO" : "YES");
    
    // DEBUG: Check for duplicate channels
    size_t l_duplicates = 0;
    for (size_t i = 0; i < l_stream->channel_count; i++) {
        if (l_stream->channel[i] && l_stream->channel[i]->proc && l_stream->channel[i]->proc->id == a_channel_id) {
            debug_if(s_debug_more, L_DEBUG, "test_stream_ch_register_receiver: channel '%c' at index %zu: %p",
                   a_channel_id, i, l_stream->channel[i]);
            l_duplicates++;
        }
    }
    if (l_duplicates > 1) {
        log_it(L_ERROR, "test_stream_ch_register_receiver: Found %zu DUPLICATE channels for ID '%c'!", 
               l_duplicates, a_channel_id);
    }
    
    // Prepare synchronization primitives
    pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t l_cond = PTHREAD_COND_INITIALIZER;
    int l_result = -1;
    
    // Prepare arguments for worker callback
    struct register_notifier_args *l_args = DAP_NEW_Z(struct register_notifier_args);
    
    if (!l_args) {
        log_it(L_CRITICAL, "Failed to allocate callback args");
        pthread_mutex_destroy(&l_mutex);
        pthread_cond_destroy(&l_cond);
        return -1;
    }
    
    l_args->stream = l_stream;
    l_args->channel_id = a_channel_id;
    l_args->ctx = (test_stream_ch_ctx_t*)a_callback_arg;
    l_args->mutex = &l_mutex;
    l_args->cond = &l_cond;
    l_args->result = &l_result;
    
    // Execute callback in worker thread
    dap_worker_exec_callback_on(l_client_esocket->worker, s_register_notifier_in_worker_callback, l_args);
    
    log_it(L_INFO, "test_stream_ch_register_receiver: scheduled notifier registration in worker thread, waiting for completion...");
    
    // Wait for callback to complete (with timeout)
    struct timespec l_timeout;
    clock_gettime(CLOCK_REALTIME, &l_timeout);
    l_timeout.tv_sec += 5; // 5 second timeout
    
    pthread_mutex_lock(&l_mutex);
    while (l_result == -1) {
        int l_wait_ret = pthread_cond_timedwait(&l_cond, &l_mutex, &l_timeout);
        if (l_wait_ret == ETIMEDOUT) {
            log_it(L_ERROR, "Timeout waiting for notifier registration");
            l_result = -1;
            break;
        }
    }
    pthread_mutex_unlock(&l_mutex);
    
    pthread_mutex_destroy(&l_mutex);
    pthread_cond_destroy(&l_cond);
    
    debug_if(s_debug_more, L_DEBUG, "test_stream_ch_register_receiver: notifier registration completed with result=%d", l_result);
    
    return l_result;
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
        if (l_udp_server && l_udp_server->flow_servers && 
            l_udp_server->flow_servers_count > 0 &&
            l_udp_server->flow_servers[0] && 
            l_udp_server->flow_servers[0]->dap_server) {
            return l_udp_server->flow_servers[0]->dap_server;
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
/**
 * @brief Wait for all streams to close with REAL intelligent polling
 * @param a_timeout_ms Maximum time to wait in milliseconds
 * @return true if all streams closed, false on timeout
 * 
 * @details This function actively checks the REAL stream count via dap_stream API
 */
bool test_wait_for_all_streams_closed(uint32_t a_timeout_ms)
{
    uint64_t l_start_time = dap_test_get_time_ms();
    uint64_t l_deadline = l_start_time + (uint64_t)a_timeout_ms;
    
    size_t l_stable_count = 0;
    const size_t STABLE_THRESHOLD = 3;  // 3 consecutive checks with count==0
    
    while (dap_test_get_time_ms() < l_deadline) {
        // Get REAL stream count
        size_t l_stream_count = 0;
        dap_stream_info_t *l_info = dap_stream_get_links_info(NULL, &l_stream_count);
        
        // Free the info array (we only needed the count)
        if (l_info) {
            dap_stream_delete_links_info(l_info, l_stream_count);
        }
        
        log_it(L_DEBUG, "Active streams: %zu", l_stream_count);
        
        // If count is 0 and stable, we're done
        if (l_stream_count == 0) {
            l_stable_count++;
            if (l_stable_count >= STABLE_THRESHOLD) {
                uint64_t l_elapsed = dap_test_get_time_ms() - l_start_time;
                log_it(L_DEBUG, "All streams closed (verified %zu times, took %"PRIu64" ms)", 
                       STABLE_THRESHOLD, l_elapsed);
                return true;
            }
        } else {
            // Reset stability counter if count increased
            l_stable_count = 0;
        }
        
        // Poll every 100ms
        dap_test_sleep_ms(100);
    }
    
    // One final check
    size_t l_final_count = 0;
    dap_stream_info_t *l_final_info = dap_stream_get_links_info(NULL, &l_final_count);
    if (l_final_info) {
        dap_stream_delete_links_info(l_final_info, l_final_count);
    }
    
    log_it(L_WARNING, "Timeout waiting for streams to close: %zu streams still active after %u ms", 
           l_final_count, a_timeout_ms);
    return false;
}

/**
 * @brief Server-side channel callback to echo data back to client
 */
bool test_server_channel_echo_callback(dap_stream_ch_t *a_ch, void *a_arg)
{
    static bool s_initialized = false;
    if (!s_initialized) {
        s_debug_more = dap_config_get_item_bool_default(g_config, "test_trans_helpers", "debug_more", false);
        s_initialized = true;
    }
    
    debug_if(s_debug_more, L_DEBUG, "ECHO_CALLBACK ENTRY: ch=%p, arg=%p", a_ch, a_arg);
    
    if (!a_ch || !a_arg) {
        debug_if(s_debug_more, L_DEBUG, "ECHO_CALLBACK: NULL check failed");
        return false;
    }
    
    if (!a_ch->stream || !a_ch->stream->is_active) {
        debug_if(s_debug_more, L_DEBUG, "ECHO_CALLBACK: stream/esocket already closed");
        return false;
    }
    
    dap_stream_ch_pkt_t *l_ch_pkt = (dap_stream_ch_pkt_t *)a_arg;
    
    debug_if(s_debug_more, L_DEBUG, "ECHO_CALLBACK: ch_pkt=%p, data_size=%u",
             l_ch_pkt, l_ch_pkt->hdr.data_size);
    
    if (l_ch_pkt->hdr.data_size == 0) {
        debug_if(s_debug_more, L_DEBUG, "ECHO_CALLBACK: data_size is 0");
        return false;
    }
    
    debug_if(s_debug_more, L_DEBUG,
             "SERVER: Echo callback received %u bytes on channel '%c' (type=%u)",
             l_ch_pkt->hdr.data_size, a_ch->proc ? a_ch->proc->id : '?', l_ch_pkt->hdr.type);
    
    // Echo data back to client through the same channel
    int l_ret = dap_stream_ch_pkt_write_unsafe(a_ch, l_ch_pkt->hdr.type, l_ch_pkt->data, l_ch_pkt->hdr.data_size);
    
    debug_if(s_debug_more, L_DEBUG,
             "SERVER: Echoed %u bytes back to client (ret=%d)", l_ch_pkt->hdr.data_size, l_ret);
    
    return true;
}
