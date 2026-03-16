/*
 * Authors:
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2025
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP SDK is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP SDK based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "dap_server_helpers.h"
#include "dap_server.h"
#include "dap_worker.h"
#include "dap_common.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define LOG_TAG "dap_server_helpers"

/**
 * @brief Context for server ready check callback
 */
typedef struct {
    dap_server_t *server;
    bool is_ready;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} server_ready_ctx_t;

/**
 * @brief Callback executed on worker thread to check server listener socket state
 */
static void s_check_server_ready_callback(void *a_arg)
{
    server_ready_ctx_t *l_ctx = (server_ready_ctx_t *)a_arg;
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

bool dap_server_is_ready(dap_server_t *a_server)
{
    if (!a_server || !a_server->es_listeners) {
        return false;
    }
    
    // Check if at least one listener socket exists and is on a worker
    dap_list_t *l_iter = a_server->es_listeners;
    while (l_iter) {
        dap_events_socket_t *l_listener = (dap_events_socket_t *)l_iter->data;
        if (l_listener && l_listener->worker && l_listener->socket >= 0) {
            return true;
        }
        l_iter = l_iter->next;
    }
    
    return false;
}

bool dap_server_wait_for_ready(dap_server_t *a_server, uint32_t a_timeout_ms)
{
    if (!a_server) {
        return false;
    }
    
    // First check if already ready
    if (dap_server_is_ready(a_server)) {
        return true;
    }
    
    // Wait for listeners to be created and attached to workers
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    dap_events_socket_t *l_first_listener = NULL;
    while (l_elapsed < a_timeout_ms) {
        if (a_server->es_listeners) {
            l_first_listener = (dap_events_socket_t *)a_server->es_listeners->data;
            if (l_first_listener && l_first_listener->worker) {
                break; // Found listener with worker
            }
        }
        
        struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
        nanosleep(&l_ts, NULL);
        l_elapsed += l_poll_interval_ms;
    }
    
    if (!l_first_listener || !l_first_listener->worker) {
        // No listeners yet or listener not attached to worker
        return false;
    }
    
    // Setup context for callback
    server_ready_ctx_t l_ctx = {
        .server = a_server,
        .is_ready = false,
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .cond = PTHREAD_COND_INITIALIZER
    };
    pthread_mutex_init(&l_ctx.mutex, NULL);
    pthread_cond_init(&l_ctx.cond, NULL);
    
    // Now wait for server to actually be listening (socket ready)
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
            struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
            nanosleep(&l_ts, NULL);
        }
        
        l_elapsed += l_poll_interval_ms;
    }
    
    pthread_mutex_destroy(&l_ctx.mutex);
    pthread_cond_destroy(&l_ctx.cond);
    return false;
}

