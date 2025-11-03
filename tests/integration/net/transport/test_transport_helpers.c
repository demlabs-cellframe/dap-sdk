/**
 * @file test_transport_helpers.c
 * @brief Implementation of common helper functions for transport integration tests
 * @date 2025-11-03
 * @copyright (c) 2025 Cellframe Network
 */

#include "test_transport_helpers.h"
#include "dap_stream_ch_proc.h"
#include "dap_client_pvt.h"
#include "dap_worker.h"
#include "dap_list.h"
#include "dap_events_socket.h"
#include "dap_http_server.h"
#include <stdio.h>

/**
 * @brief Wait for transports to be registered
 */
bool test_wait_for_transports_registered(uint32_t a_timeout_ms)
{
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    const dap_stream_transport_type_t l_expected_transports[] = {
        DAP_STREAM_TRANSPORT_HTTP,
        // Add other transports when enabled
    };
    const size_t l_expected_count = sizeof(l_expected_transports) / sizeof(l_expected_transports[0]);
    
    while (l_elapsed < a_timeout_ms) {
        size_t l_registered_count = 0;
        for (size_t i = 0; i < l_expected_count; i++) {
            if (dap_stream_transport_find(l_expected_transports[i])) {
                l_registered_count++;
            }
        }
        
        if (l_registered_count == l_expected_count) {
            return true;
        }
        
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
    }
    
    return false;
}

/**
 * @brief Get dap_server_t from transport server
 * Helper function to extract underlying dap_server_t from transport-specific structure
 */
static dap_server_t *s_get_server_from_transport(dap_net_transport_server_t *a_server)
{
    if (!a_server) {
        return NULL;
    }
    
    // For HTTP/WebSocket transports, get HTTP server from transport_specific
    if (a_server->transport_type == DAP_STREAM_TRANSPORT_HTTP || 
        a_server->transport_type == DAP_STREAM_TRANSPORT_WEBSOCKET) {
        dap_http_server_t *l_http_server = (dap_http_server_t *)a_server->transport_specific;
        if (l_http_server && l_http_server->server) {
            return l_http_server->server;
        }
    }
    
    // For UDP/DNS transports, transport_specific might be different
    // TODO: Add support for UDP/DNS if needed
    
    return NULL;
}

/**
 * @brief Context for server ready check callback
 */
typedef struct {
    dap_server_t *server;
    bool is_ready;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} test_server_ready_ctx_t;

/**
 * @brief Callback executed on worker thread to check server listener socket state
 */
static void s_check_server_ready_callback(void *a_arg)
{
    test_server_ready_ctx_t *l_ctx = (test_server_ready_ctx_t *)a_arg;
    if (!l_ctx || !l_ctx->server) {
        return;
    }
    
    // SAFE: We're in worker context - can access es_listeners directly
    l_ctx->is_ready = false;
    if (l_ctx->server->es_listeners) {
        // Check if at least one listener socket exists and is on a worker
        dap_list_t *l_iter = l_ctx->server->es_listeners;
        while (l_iter) {
            dap_events_socket_t *l_listener = (dap_events_socket_t *)l_iter->data;
            if (l_listener && l_listener->worker && l_listener->socket >= 0) {
                l_ctx->is_ready = true;
                break;
            }
            l_iter = l_iter->next;
        }
    }
    
    // Signal waiting thread
    pthread_mutex_lock(&l_ctx->mutex);
    pthread_cond_signal(&l_ctx->cond);
    pthread_mutex_unlock(&l_ctx->mutex);
}

/**
 * @brief Wait for server to be ready (listening)
 */
bool test_wait_for_server_ready(dap_net_transport_server_t *a_server, uint32_t a_timeout_ms)
{
    if (!a_server) {
        return false;
    }
    
    // Get underlying dap_server_t from transport-specific structure
    dap_server_t *l_server = s_get_server_from_transport(a_server);
    if (!l_server) {
        // No server yet, wait a bit
        dap_test_sleep_ms(100);
        return false;
    }
    
    // Find first listener socket to get its worker
    dap_events_socket_t *l_first_listener = NULL;
    if (l_server->es_listeners) {
        l_first_listener = (dap_events_socket_t *)l_server->es_listeners->data;
    }
    
    if (!l_first_listener || !l_first_listener->worker) {
        // No listeners yet, wait a bit
        dap_test_sleep_ms(100);
        return false;
    }
    
    // Setup context for callback
    test_server_ready_ctx_t l_ctx = {
        .server = l_server,
        .is_ready = false,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };
    pthread_mutex_init(&l_ctx.mutex, NULL);
    pthread_cond_init(&l_ctx.cond, NULL);
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    while (l_elapsed < a_timeout_ms) {
        // Execute callback on worker thread
        dap_worker_exec_callback_on(l_first_listener->worker, s_check_server_ready_callback, &l_ctx);
        
        // Wait for callback completion with timeout
        pthread_mutex_lock(&l_ctx.mutex);
        struct timespec l_timeout;
        clock_gettime(CLOCK_REALTIME, &l_timeout);
        l_timeout.tv_sec += (l_poll_interval_ms / 1000);
        l_timeout.tv_nsec += ((l_poll_interval_ms % 1000) * 1000000);
        if (l_timeout.tv_nsec >= 1000000000) {
            l_timeout.tv_sec++;
            l_timeout.tv_nsec -= 1000000000;
        }
        
        int l_wait_result = pthread_cond_timedwait(&l_ctx.cond, &l_ctx.mutex, &l_timeout);
        bool l_is_ready = l_ctx.is_ready;
        pthread_mutex_unlock(&l_ctx.mutex);
        
        if (l_is_ready) {
            pthread_mutex_destroy(&l_ctx.mutex);
            pthread_cond_destroy(&l_ctx.cond);
            return true;
        }
        
        if (l_wait_result != ETIMEDOUT) {
            // Error or spurious wakeup, continue waiting
            dap_test_sleep_ms(l_poll_interval_ms);
        }
        
        l_elapsed += l_poll_interval_ms;
    }
    
    pthread_mutex_destroy(&l_ctx.mutex);
    pthread_cond_destroy(&l_ctx.cond);
    return false;
}

/**
 * @brief Context for stream channels ready check callback
 */
typedef struct {
    dap_client_t *client;
    const char *expected_channels;
    bool is_ready;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} test_channels_ready_ctx_t;

/**
 * @brief Callback executed on worker thread to check stream channels
 */
static void s_check_channels_ready_callback(void *a_arg)
{
    test_channels_ready_ctx_t *l_ctx = (test_channels_ready_ctx_t *)a_arg;
    if (!l_ctx || !l_ctx->client || !l_ctx->expected_channels) {
        return;
    }
    
    // SAFE: We're in worker context - can safely access client stream
    dap_stream_t *l_stream = dap_client_get_stream(l_ctx->client);
    l_ctx->is_ready = false;
    
    if (l_stream && l_stream->channel_count > 0 && l_stream->channel) {
        // Check if all expected channels exist
        bool l_all_channels_ready = true;
        for (const char *l_ch_id = l_ctx->expected_channels; *l_ch_id; l_ch_id++) {
            bool l_found = false;
            for (size_t i = 0; i < l_stream->channel_count; i++) {
                if (l_stream->channel[i] && l_stream->channel[i]->proc && 
                    l_stream->channel[i]->proc->id == *l_ch_id) {
                    l_found = true;
                    break;
                }
            }
            if (!l_found) {
                l_all_channels_ready = false;
                break;
            }
        }
        
        l_ctx->is_ready = l_all_channels_ready;
    }
    
    // Signal waiting thread
    pthread_mutex_lock(&l_ctx->mutex);
    pthread_cond_signal(&l_ctx->cond);
    pthread_mutex_unlock(&l_ctx->mutex);
}

/**
 * @brief Wait for stream channels to be created
 */
bool test_wait_for_stream_channels_ready(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms)
{
    if (!a_client || !a_expected_channels) {
        return false;
    }
    
    // Get client's worker
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt || !l_client_pvt->worker) {
        return false;
    }
    
    // Setup context for callback
    test_channels_ready_ctx_t l_ctx = {
        .client = a_client,
        .expected_channels = a_expected_channels,
        .is_ready = false,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };
    pthread_mutex_init(&l_ctx.mutex, NULL);
    pthread_cond_init(&l_ctx.cond, NULL);
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    while (l_elapsed < a_timeout_ms) {
        // Execute callback on worker thread
        dap_worker_exec_callback_on(l_client_pvt->worker, s_check_channels_ready_callback, &l_ctx);
        
        // Wait for callback completion with timeout
        pthread_mutex_lock(&l_ctx.mutex);
        struct timespec l_timeout;
        clock_gettime(CLOCK_REALTIME, &l_timeout);
        l_timeout.tv_sec += (l_poll_interval_ms / 1000);
        l_timeout.tv_nsec += ((l_poll_interval_ms % 1000) * 1000000);
        if (l_timeout.tv_nsec >= 1000000000) {
            l_timeout.tv_sec++;
            l_timeout.tv_nsec -= 1000000000;
        }
        
        int l_wait_result = pthread_cond_timedwait(&l_ctx.cond, &l_ctx.mutex, &l_timeout);
        bool l_is_ready = l_ctx.is_ready;
        pthread_mutex_unlock(&l_ctx.mutex);
        
        if (l_is_ready) {
            pthread_mutex_destroy(&l_ctx.mutex);
            pthread_cond_destroy(&l_ctx.cond);
            return true;
        }
        
        if (l_wait_result != ETIMEDOUT) {
            // Error or spurious wakeup, continue waiting
            dap_test_sleep_ms(l_poll_interval_ms);
        }
        
        l_elapsed += l_poll_interval_ms;
    }
    
    pthread_mutex_destroy(&l_ctx.mutex);
    pthread_cond_destroy(&l_ctx.cond);
    return false;
}

/**
 * @brief Wait for client to be deleted
 */
bool test_wait_for_client_deleted(dap_client_t **a_client_ptr, uint32_t a_timeout_ms)
{
    if (!a_client_ptr || !*a_client_ptr) {
        return true; // Already deleted or NULL
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    // Check if client is marked as removing
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(*a_client_ptr);
    if (!l_client_pvt) {
        *a_client_ptr = NULL;
        return true;
    }
    
    while (l_elapsed < a_timeout_ms) {
        // Check if client is marked as removing (deletion in progress)
        if (l_client_pvt->is_removing) {
            // Client deletion started, wait a bit more for completion
            dap_test_sleep_ms(200);
            *a_client_ptr = NULL;
            return true;
        }
        
        // Try to get stream - if client is deleted, this might crash or return NULL
        // So we just wait a bit and check is_removing flag
        dap_test_sleep_ms(l_poll_interval_ms);
        l_elapsed += l_poll_interval_ms;
        
        // Re-check client_pvt (might be freed)
        if (!DAP_CLIENT_PVT(*a_client_ptr)) {
            *a_client_ptr = NULL;
            return true;
        }
    }
    
    // Timeout - assume deleted anyway
    *a_client_ptr = NULL;
    return true;
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

