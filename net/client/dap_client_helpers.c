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
#include "dap_client_fsm.h"
#include "dap_client_trans_ctx.h"
#include "dap_net_trans_ctx.h"
#include "dap_worker.h"
#include "dap_stream.h"
#include "dap_stream_ch.h"
#include "dap_common.h"
#include <time.h>

#define LOG_TAG "dap_client_helpers"

static bool s_debug_more = false;
/**
 * @brief Context for channels ready check callback
 */
typedef struct {
    dap_client_t *client;
    const char *expected_channels;
    bool is_ready;
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
    
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_ctx->client);
    dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
    if (!l_tc || !l_tc->stream) {
        l_ctx->is_ready = false;
        return;
    }
    
    // Check if all expected channels exist
    l_ctx->is_ready = true;
    dap_stream_t *l_stream = l_tc->stream;
    for (const char *l_ch = l_ctx->expected_channels; *l_ch != '\0'; l_ch++) {
        dap_stream_ch_t *l_ch_obj = dap_stream_ch_by_id_unsafe(l_stream, *l_ch);
        if (!l_ch_obj) {
            l_ctx->is_ready = false;
            break;
        }
    }
    
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
            debug_if(s_debug_more, L_DEBUG, "Client reached error state at stage %d", l_stage);
            return false;
        }
        
        // Sleep using dap_common sleep function
        struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
        nanosleep(&l_ts, NULL);
        l_elapsed += l_poll_interval_ms;
    }
    
    debug_if(s_debug_more, L_DEBUG, "Timeout waiting for client stage %d (current: %d)", a_target_stage, dap_client_get_stage(a_client));
    return false;
}

bool dap_client_wait_for_deletion(dap_client_t **a_client_ptr, uint32_t a_timeout_ms)
{
    if (!a_client_ptr || !*a_client_ptr) {
        return true; // Already deleted or NULL
    }
    
    // Since dap_client_delete_mt() is now SYNCHRONOUS, the client is already deleted
    // by the time we reach here. Just clear the pointer and return.
    *a_client_ptr = NULL;
    return true;
}

bool dap_client_wait_for_channels(dap_client_t *a_client, const char *a_expected_channels, uint32_t a_timeout_ms)
{
    if (!a_client || !a_expected_channels) {
        return false;
    }
    
    // Get client's worker
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm || !l_fsm->worker) {
        return false;
    }
    
    // Setup context for callback
    channels_ready_ctx_t l_ctx = {
        .client = a_client,
        .expected_channels = a_expected_channels,
        .is_ready = false
    };
    
    uint32_t l_elapsed = 0;
    const uint32_t l_poll_interval_ms = 50;
    
    while (l_elapsed < a_timeout_ms) {
        // Execute callback synchronously on worker thread.
        // This avoids use-after-scope from async callbacks with stack context.
        l_ctx.is_ready = false;
        dap_worker_exec_callback_on_sync(l_fsm->worker, s_check_channels_ready_callback, &l_ctx);

        if (l_ctx.is_ready) {
            return true;
        }

        struct timespec l_ts = { .tv_sec = l_poll_interval_ms / 1000, .tv_nsec = (l_poll_interval_ms % 1000) * 1000000 };
        nanosleep(&l_ts, NULL);
        l_elapsed += l_poll_interval_ms;
    }

    return false;
}
