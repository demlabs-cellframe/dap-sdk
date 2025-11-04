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

#include "dap_client_helpers.h"
#include "dap_client_pvt.h"
#include "dap_worker.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_common.h"
#include <pthread.h>
#include <time.h>
#include <errno.h>

#define LOG_TAG "dap_client_helpers"

/**
 * @brief Context for channels ready check callback
 */
typedef struct {
    dap_client_t *client;
    const char *expected_channels;
    bool is_ready;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} channels_ready_ctx_t;

/**
 * @brief Callback executed on worker thread to check channel readiness
 */
static void s_check_channels_ready_callback(void *a_arg)
{
    channels_ready_ctx_t *l_ctx = (channels_ready_ctx_t *)a_arg;
    if (!l_ctx || !l_ctx->client) {
        return;
    }
    
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_ctx->client);
    if (!l_client_pvt || !l_client_pvt->stream) {
        l_ctx->is_ready = false;
        pthread_mutex_lock(&l_ctx->mutex);
        pthread_cond_signal(&l_ctx->cond);
        pthread_mutex_unlock(&l_ctx->mutex);
        return;
    }
    
    // Check if all expected channels exist
    l_ctx->is_ready = true;
    dap_stream_t *l_stream = l_client_pvt->stream;
    for (const char *l_ch = l_ctx->expected_channels; *l_ch != '\0'; l_ch++) {
        dap_stream_ch_t *l_ch_obj = dap_stream_ch_by_id_unsafe(l_stream, *l_ch);
        if (!l_ch_obj) {
            l_ctx->is_ready = false;
            break;
        }
    }
    
    pthread_mutex_lock(&l_ctx->mutex);
    pthread_cond_signal(&l_ctx->cond);
    pthread_mutex_unlock(&l_ctx->mutex);
}

bool dap_client_is_connected(dap_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    return (dap_client_get_stage(a_client) == STAGE_STREAM_STREAMING &&
            dap_client_get_stage_status(a_client) == STAGE_STATUS_COMPLETE);
}

bool dap_client_is_in_stage(dap_client_t *a_client, dap_client_stage_t a_stage)
{
    if (!a_client) {
        return false;
    }
    
    return dap_client_get_stage(a_client) == a_stage;
}

bool dap_client_has_error(dap_client_t *a_client)
{
    if (!a_client) {
        return false;
    }
    
    return dap_client_get_stage_status(a_client) == STAGE_STATUS_ERROR;
}

bool dap_client_wait_for_stage(dap_client_t *a_client, dap_client_stage_t a_target_stage, uint32_t a_timeout_ms)
{
    if (!a_client) {
        return false;
    }
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    while (l_elapsed < a_timeout_ms) {
        dap_client_stage_t l_stage = dap_client_get_stage(a_client);
        dap_client_stage_status_t l_status = dap_client_get_stage_status(a_client);
        
        if (l_stage == a_target_stage && l_status == STAGE_STATUS_COMPLETE) {
            return true;
        }
        
        if (l_status == STAGE_STATUS_ERROR) {
            log_it(L_DEBUG, "Client reached error state at stage %d", l_stage);
            return false;
        }
        
        // Sleep using dap_common sleep function
        struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
        nanosleep(&l_ts, NULL);
        l_elapsed += l_poll_interval_ms;
    }
    
    log_it(L_DEBUG, "Timeout waiting for client stage %d (current: %d)", a_target_stage, dap_client_get_stage(a_client));
    return false;
}

bool dap_client_wait_for_deletion(dap_client_t **a_client_ptr, uint32_t a_timeout_ms)
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
            struct timespec l_ts = { .tv_sec = 0, .tv_nsec = 200000000 }; // 200ms
            nanosleep(&l_ts, NULL);
            *a_client_ptr = NULL;
            return true;
        }
        
        // Sleep and re-check
        struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
        nanosleep(&l_ts, NULL);
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

bool dap_client_wait_for_channels(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms)
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
    channels_ready_ctx_t l_ctx = {
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
            struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
            nanosleep(&l_ts, NULL);
        }
        
        l_elapsed += l_poll_interval_ms;
    }
    
    pthread_mutex_destroy(&l_ctx.mutex);
    pthread_cond_destroy(&l_ctx.cond);
    return false;
}

