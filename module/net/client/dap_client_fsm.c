/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2026
 * All rights reserved.
 *
 * dap_client_fsm - Client connection state machine
 *
 * Extracted from dap_client_pvt.c / dap_client_esocket.c
 * Runs on dedicated FSM thread pool with sticky binding (uuid % pool_size).
 * Heavy crypto operations (key gen, signing) run on FSM threads, not workers.
 *
 * This file is part of DAP SDK the open source project
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 */

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_config.h"
#include "dap_client_fsm.h"
#include "dap_client_esocket.h"
#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc.h"
#include "dap_cert.h"
#include "dap_context.h"
#include "dap_timerfd.h"
#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_pkt.h"
#include "dap_net.h"
#include "dap_net_trans.h"
#include "dap_net_trans_qos.h"
#include "dap_stream_handshake.h"
#include "dap_rand.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_thread_pool.h"
#include "dap_uuid.h"
#include "dap_ht.h"

#define LOG_TAG "dap_client_fsm"

// ===== Module state =====

static dap_client_fsm_t *s_fsm_table = NULL;
static pthread_rwlock_t s_fsm_table_lock = PTHREAD_RWLOCK_INITIALIZER;

static int s_max_attempts = 3;
static int s_timeout = 20;
static bool s_debug_more = false;
static time_t s_client_timeout_active_after_connect_seconds = 15;

// ===== FSM Thread Pool =====

static dap_thread_pool_t *s_fsm_pool = NULL;
static uint32_t s_fsm_thread_count = 0;

typedef void *(*s_fsm_callback_t)(void *);

static void s_fsm_thread_callback_add(uint32_t a_thread_idx,
                                       s_fsm_callback_t a_callback, void *a_arg)
{
    if (!s_fsm_pool) {
        log_it(L_ERROR, "FSM thread pool not initialized");
        DAP_DELETE(a_arg);
        return;
    }
    int l_ret = dap_thread_pool_submit_to(s_fsm_pool, a_thread_idx,
                                           a_callback, a_arg, NULL, NULL);
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to submit FSM task to thread %u: %d", a_thread_idx, l_ret);
        DAP_DELETE(a_arg);
    }
}

// ===== Forward declarations =====

static void s_fsm_process(dap_client_fsm_t *a_fsm);
static void s_fsm_dispatch_stage_to_worker(dap_client_fsm_t *a_fsm);
static void s_worker_execute_enc_init_io(void *a_arg);
static void s_handshake_es_delete_callback(dap_events_socket_t *a_es, void *a_arg);
static int s_add_tried_transport(dap_client_fsm_t *a_fsm, dap_net_trans_type_t a_trans_type);
static bool s_is_transport_tried(dap_client_fsm_t *a_fsm, dap_net_trans_type_t a_trans_type);
static int s_retry_handshake_with_fallback(dap_client_fsm_t *a_fsm);

// ===== Helper: set FSM stage/status with atomic copy =====

static inline void s_set_stage(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage)
{
    a_fsm->stage = a_stage;
    atomic_store(&a_fsm->stage_readable, (int)a_stage);
}

static inline void s_set_stage_status(dap_client_fsm_t *a_fsm, dap_client_stage_status_t a_status)
{
    a_fsm->stage_status = a_status;
    atomic_store(&a_fsm->stage_status_readable, (int)a_status);
}

static inline void s_set_stage_and_status(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage,
                                           dap_client_stage_status_t a_status)
{
    a_fsm->stage = a_stage;
    a_fsm->stage_status = a_status;
    atomic_store(&a_fsm->stage_readable, (int)a_stage);
    atomic_store(&a_fsm->stage_status_readable, (int)a_status);
}

// ===== UUID-based global registry =====

dap_client_fsm_t *dap_client_fsm_find(uint64_t a_uuid)
{
    dap_client_fsm_t *l_result = NULL;
    pthread_rwlock_rdlock(&s_fsm_table_lock);
    dap_ht_find(s_fsm_table, &a_uuid, sizeof(uint64_t), l_result);
    pthread_rwlock_unlock(&s_fsm_table_lock);
    return l_result;
}

void dap_client_fsm_register(dap_client_fsm_t *a_fsm)
{
    if (!a_fsm || !a_fsm->uuid)
        return;
    pthread_rwlock_wrlock(&s_fsm_table_lock);
    dap_client_fsm_t *l_existing = NULL;
    dap_ht_find(s_fsm_table, &a_fsm->uuid, sizeof(uint64_t), l_existing);
    if (!l_existing)
        dap_ht_add_keyptr(s_fsm_table, &a_fsm->uuid, sizeof(uint64_t), a_fsm);
    pthread_rwlock_unlock(&s_fsm_table_lock);
}

void dap_client_fsm_unregister(dap_client_fsm_t *a_fsm)
{
    if (!a_fsm)
        return;
    pthread_rwlock_wrlock(&s_fsm_table_lock);
    dap_client_fsm_t *l_existing = NULL;
    dap_ht_find(s_fsm_table, &a_fsm->uuid, sizeof(uint64_t), l_existing);
    if (l_existing)
        dap_ht_del(s_fsm_table, l_existing);
    pthread_rwlock_unlock(&s_fsm_table_lock);
}

// ===== Module init/deinit =====

int dap_client_fsm_init(void)
{
    s_max_attempts = dap_config_get_item_int32_default(g_config, "dap_client", "max_tries", s_max_attempts);
    s_timeout = dap_config_get_item_int32_default(g_config, "dap_client", "timeout", s_timeout);
    s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);
    s_client_timeout_active_after_connect_seconds = (time_t)dap_config_get_item_uint32_default(
        g_config, "dap_client", "timeout_active_after_connect", s_client_timeout_active_after_connect_seconds);

    s_fsm_thread_count = dap_config_get_item_uint32_default(g_config, "dap_client", "fsm_threads", 0);
    s_fsm_pool = dap_thread_pool_create(s_fsm_thread_count, 0);
    if (!s_fsm_pool) {
        log_it(L_CRITICAL, "Failed to create FSM thread pool");
        return -1;
    }
    s_fsm_thread_count = dap_thread_pool_get_thread_count(s_fsm_pool);
    log_it(L_INFO, "Client FSM module initialized (max_attempts=%d, timeout=%d, fsm_threads=%u)",
           s_max_attempts, s_timeout, s_fsm_thread_count);
    return 0;
}

void dap_client_fsm_deinit(void)
{
    if (s_fsm_pool) {
        dap_thread_pool_delete(s_fsm_pool);
        s_fsm_pool = NULL;
    }
    s_fsm_thread_count = 0;

    pthread_rwlock_wrlock(&s_fsm_table_lock);
    dap_client_fsm_t *l_current, *l_tmp;
    dap_ht_foreach(s_fsm_table, l_current, l_tmp) {
        dap_ht_del(s_fsm_table, l_current);
    }
    pthread_rwlock_unlock(&s_fsm_table_lock);
    log_it(L_INFO, "Client FSM module deinitialized");
}

// ===== Instance lifecycle =====

dap_client_fsm_t *dap_client_fsm_new(dap_client_t *a_client)
{
    dap_client_fsm_t *l_fsm = DAP_NEW_Z(dap_client_fsm_t);
    if (!l_fsm) {
        log_it(L_CRITICAL, "Insufficient memory for FSM");
        return NULL;
    }

    l_fsm->uuid = dap_uuid_generate_uint64();
    l_fsm->client = a_client;
    l_fsm->worker = dap_events_worker_get_auto();

    // Crypto defaults
    l_fsm->session_key_type = DAP_ENC_KEY_TYPE_SALSA2012;
    l_fsm->session_key_open_type = DAP_ENC_KEY_TYPE_KEM_KYBER512;
    l_fsm->session_key_block_size = 32;

    // FSM state
    s_set_stage_and_status(l_fsm, STAGE_BEGIN, STAGE_STATUS_COMPLETE);

    // FSM thread binding (sticky)
    l_fsm->fsm_thread_idx = s_fsm_thread_count ? (l_fsm->uuid % s_fsm_thread_count) : 0;

    // Transport fallback
    l_fsm->tried_transport_capacity = DAP_NET_TRANS_MAX;
    l_fsm->tried_transports = DAP_NEW_Z_SIZE(dap_net_trans_type_t,
                                              sizeof(dap_net_trans_type_t) * l_fsm->tried_transport_capacity);
    if (!l_fsm->tried_transports) {
        DAP_DELETE(l_fsm);
        return NULL;
    }
    // Mark initial transport as tried
    l_fsm->tried_transports[l_fsm->tried_transport_count++] = a_client->trans_type;

    // Create esocket context
    l_fsm->esocket = DAP_NEW_Z(dap_client_esocket_t);
    if (!l_fsm->esocket) {
        DAP_DELETE(l_fsm->tried_transports);
        DAP_DELETE(l_fsm);
        return NULL;
    }
    l_fsm->esocket->uuid = dap_uuid_generate_uint64();
    l_fsm->esocket->client = a_client;
    l_fsm->esocket->worker = l_fsm->worker;
    l_fsm->esocket->fsm_uuid = l_fsm->uuid;
    l_fsm->esocket->fsm_thread_idx = l_fsm->fsm_thread_idx;
    l_fsm->esocket->session_key_type = l_fsm->session_key_type;
    l_fsm->esocket->session_key_open_type = l_fsm->session_key_open_type;
    l_fsm->esocket->session_key_block_size = l_fsm->session_key_block_size;
    l_fsm->esocket->uplink_protocol_version = DAP_PROTOCOL_VERSION;
    dap_client_esocket_new(l_fsm->esocket);

    // Register FSM in global table
    dap_client_fsm_register(l_fsm);

    debug_if(s_debug_more, L_DEBUG, "FSM %p created (uuid=0x%016" PRIx64 ", proc_thread=%u, esocket=%p)",
             l_fsm, l_fsm->uuid, l_fsm->fsm_thread_idx, l_fsm->esocket);

    return l_fsm;
}

void dap_client_fsm_delete_unsafe(dap_client_fsm_t *a_fsm)
{
    if (!a_fsm)
        return;

    debug_if(s_debug_more, L_INFO, "FSM delete: %p (uuid=0x%016" PRIx64 ")", a_fsm, a_fsm->uuid);

    dap_client_fsm_unregister(a_fsm);

    if (a_fsm->esocket) {
        dap_client_esocket_delete_unsafe(a_fsm->esocket);
        a_fsm->esocket = NULL;
    }

    DAP_DEL_Z(a_fsm->tried_transports);
    DAP_DELETE(a_fsm);
}

// ===== Proc thread callback context =====

typedef struct {
    uint64_t fsm_uuid;
} fsm_proc_ctx_t;

typedef struct {
    uint64_t fsm_uuid;
    dap_client_stage_status_t status;
    dap_client_error_t error;
} fsm_notify_ctx_t;

typedef struct {
    uint64_t fsm_uuid;
    dap_client_stage_t stage_target;
    dap_client_callback_t done_callback;
} fsm_go_stage_ctx_t;

// ===== Transport fallback helpers =====

static int s_add_tried_transport(dap_client_fsm_t *a_fsm, dap_net_trans_type_t a_trans_type)
{
    if (!a_fsm)
        return -1;

    // Check if already tried
    for (size_t i = 0; i < a_fsm->tried_transport_count; i++) {
        if (a_fsm->tried_transports[i] == a_trans_type)
            return 0; // Already tried
    }

    // Expand array if needed
    if (a_fsm->tried_transport_count >= a_fsm->tried_transport_capacity) {
        size_t l_new_capacity = a_fsm->tried_transport_capacity * 2;
        if (l_new_capacity < 4) l_new_capacity = 4;
        dap_net_trans_type_t *l_new = DAP_REALLOC(a_fsm->tried_transports,
                                                   sizeof(dap_net_trans_type_t) * l_new_capacity);
        if (!l_new) return -1;
        a_fsm->tried_transports = l_new;
        a_fsm->tried_transport_capacity = l_new_capacity;
    }

    a_fsm->tried_transports[a_fsm->tried_transport_count++] = a_trans_type;
    return 0;
}

static bool s_is_transport_tried(dap_client_fsm_t *a_fsm, dap_net_trans_type_t a_trans_type)
{
    if (!a_fsm || !a_fsm->tried_transports)
        return false;
    for (size_t i = 0; i < a_fsm->tried_transport_count; i++) {
        if (a_fsm->tried_transports[i] == a_trans_type)
            return true;
    }
    return false;
}

static int s_retry_handshake_with_fallback(dap_client_fsm_t *a_fsm)
{
    if (!a_fsm || !a_fsm->client)
        return -1;

    dap_list_t *l_all_transports = dap_net_trans_list_all();
    if (!l_all_transports) {
        log_it(L_ERROR, "No transports available in registry");
        return -1;
    }

    dap_net_trans_type_t l_next_transport = 0;
    bool l_found = false;

    for (dap_list_t *l_item = l_all_transports; l_item; l_item = l_item->next) {
        dap_net_trans_t *l_transport = (dap_net_trans_t *)l_item->data;
        if (!l_transport || !l_transport->ops || !l_transport->ops->handshake_init)
            continue;
        if (!s_is_transport_tried(a_fsm, l_transport->type)) {
            l_next_transport = l_transport->type;
            l_found = true;
            break;
        }
    }
    dap_list_free(l_all_transports);

    if (!l_found) {
        log_it(L_WARNING, "No more untried transports available");
        return -1;
    }

    if (s_add_tried_transport(a_fsm, l_next_transport) != 0)
        return -1;

    log_it(L_INFO, "Retrying handshake with fallback transport: %s (type=%d)",
           dap_net_trans_type_to_str(l_next_transport), l_next_transport);

    a_fsm->client->trans_type = l_next_transport;

    // Reset to BEGIN to restart with new transport
    s_set_stage_and_status(a_fsm, STAGE_BEGIN, STAGE_STATUS_COMPLETE);
    s_fsm_process(a_fsm);
    return 0;
}

// ===== Worker dispatch context =====

typedef struct {
    uint64_t fsm_uuid;
    uint32_t fsm_thread_idx;
    dap_client_t *client;
    dap_client_stage_t stage;
} fsm_worker_dispatch_t;

// Dispatch context for STAGE_ENC_INIT IO-only part (crypto done on FSM thread)
typedef struct {
    uint64_t fsm_uuid;
    uint32_t fsm_thread_idx;
    dap_client_t *client;
    dap_net_handshake_params_t handshake_params;
    dap_net_trans_type_t trans_type;
} fsm_enc_init_io_ctx_t;

// ===== Reconnect timer =====

typedef struct {
    uint64_t fsm_uuid;
    uint32_t fsm_thread_idx;
} fsm_reconnect_timer_ctx_t;

static bool s_timer_reconnect_callback(void *a_arg)
{
    if (!a_arg)
        return false;
    fsm_reconnect_timer_ctx_t *l_ctx = (fsm_reconnect_timer_ctx_t *)a_arg;
    dap_client_fsm_notify_timer_fired(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx);
    DAP_DELETE(l_ctx);
    return false;
}

// ===== Forward declarations for callbacks used by worker stage execution =====
// Defined in dap_client_esocket.c
extern void s_handshake_callback_wrapper(dap_stream_t *a_stream, const void *a_data, size_t a_data_size, int a_error);
extern void s_session_create_callback_wrapper(dap_stream_t *a_stream, uint32_t a_session_id,
                                               const char *a_response_data, size_t a_response_size, int a_error);
extern void s_stream_transport_connect_callback(dap_stream_t *a_stream, int a_error_code);
extern void s_session_start_callback_wrapper(dap_stream_t *a_stream, int a_error_code);

// Timer callbacks (defined below)
static bool s_stream_timer_timeout_check(void *a_arg);
static bool s_stream_timer_timeout_after_connected_check(void *a_arg);

static void s_qos_handshake_callback(dap_stream_t *a_stream, const void *a_data,
                                      size_t a_data_size, int a_error)
{
    if (!a_stream)
        return;

    dap_client_t *l_client = NULL;
    if (a_stream->trans && a_stream->trans->ops && a_stream->trans->ops->get_client_context)
        l_client = (dap_client_t *)a_stream->trans->ops->get_client_context(a_stream);
    else if (a_stream->trans_ctx && a_stream->trans_ctx->esocket && a_stream->trans_ctx->esocket->_inheritor)
        l_client = (dap_client_t *)a_stream->trans_ctx->esocket->_inheritor;

    if (!l_client)
        return;

    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es)
        return;

    if (a_error != 0) {
        log_it(L_WARNING, "QoS probe handshake error %d", a_error);
        dap_client_error_t l_err = (a_error == ETIMEDOUT)
            ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_NETWORK_CONNECTION_REFUSE;
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, l_err);
        return;
    }

    if (a_data && a_data_size >= sizeof(dap_qos_echo_pkt_t)) {
        const dap_qos_echo_pkt_t *l_echo = (const dap_qos_echo_pkt_t *)a_data;
        if (l_echo->magic == DAP_QOS_ECHO_MAGIC) {
            dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                                  STAGE_STATUS_DONE, ERROR_NO_ERROR);
            return;
        }
    }

    log_it(L_WARNING, "QoS probe: invalid echo response (size=%zu)", a_data_size);
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_ERROR, ERROR_STREAM_RESPONSE_WRONG);
}

// ===== Worker-side stage execution (called on worker thread) =====

static void s_worker_execute_stage(void *a_arg)
{
    fsm_worker_dispatch_t *l_ctx = (fsm_worker_dispatch_t *)a_arg;
    if (!l_ctx) return;

    dap_client_t *l_client = l_ctx->client;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es) {
        log_it(L_ERROR, "No esocket for stage execution");
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        DAP_DELETE(l_ctx);
        return;
    }

    dap_worker_t *l_worker = l_es->worker;
    dap_client_stage_t l_stage = l_ctx->stage;

    switch (l_stage) {
    case STAGE_BEGIN: {
        // Clean esocket resources
        dap_client_esocket_clean_unsafe(l_es);
        // Notify FSM: BEGIN done
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
    } break;

    case STAGE_ENC_INIT:
        // STAGE_ENC_INIT is now handled by s_fsm_dispatch_stage_to_worker (crypto on FSM thread)
        // and s_worker_execute_enc_init_io (IO on worker). This path should never be reached.
        log_it(L_CRITICAL, "BUG: STAGE_ENC_INIT dispatched to s_worker_execute_stage instead of s_worker_execute_enc_init_io");
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        break;

    case STAGE_STREAM_CTL: {
        debug_if(s_debug_more, L_INFO, "Worker: executing STAGE_STREAM_CTL for client %p", l_client);

        dap_net_trans_type_t l_trans_type = l_client->trans_type;
        dap_net_trans_t *l_transport = dap_net_trans_find(l_trans_type);

        if (!l_transport || !l_transport->ops || !l_transport->ops->session_create) {
            debug_if(s_debug_more, L_DEBUG, "Transport doesn't support session_create, skipping");
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_DONE, ERROR_NO_ERROR);
            break;
        }

        if (!l_es->stream) {
            log_it(L_ERROR, "No stream for session_create");
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        dap_net_session_params_t l_session_params = {
            .channels = l_client->active_channels,
            .enc_type = l_es->session_key_type,
            .enc_key_size = l_es->session_key_block_size,
            .enc_headers = false,
            .protocol_version = DAP_CLIENT_PROTOCOL_VERSION,
            .session_key = l_es->session_key,
            .session_key_id = l_es->session_key_id
        };

        int l_ret = l_transport->ops->session_create(l_es->stream, &l_session_params,
                                                      s_session_create_callback_wrapper);
        if (l_ret != 0) {
            log_it(L_ERROR, "Failed to initiate session create: %d", l_ret);
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }
        // Async; callback will notify FSM
    } break;

    case STAGE_STREAM_SESSION: {
        debug_if(s_debug_more, L_INFO, "Worker: executing STAGE_STREAM_SESSION for client %p", l_client);

        if (!l_es->stream) {
            log_it(L_ERROR, "No stream for STAGE_STREAM_SESSION");
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        if (!l_es->stream->session || !l_es->stream->session->key) {
            l_es->stream->session = dap_stream_session_pure_new();
            if (!l_es->stream->session) {
                dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                      STAGE_STATUS_ERROR, ERROR_OUT_OF_MEMORY);
                break;
            }
            l_es->stream->session->key = l_es->stream_key;
        }

        if (l_worker->_inheritor) {
            l_es->stream_worker = DAP_STREAM_WORKER(l_worker);
            l_es->stream->stream_worker = l_es->stream_worker;
        } else {
            l_es->stream_worker = NULL;
            l_es->stream->stream_worker = NULL;
        }

        dap_net_trans_t *l_transport = l_es->stream->trans;
        if (!l_transport) {
            log_it(L_ERROR, "Stream has no transport");
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        if (l_transport->ops && l_transport->ops->connect) {
            int l_ret = l_transport->ops->connect(l_es->stream,
                                                   l_client->link_info.uplink_addr,
                                                   l_client->link_info.uplink_port,
                                                   s_stream_transport_connect_callback);
            if (l_ret != 0) {
                log_it(L_ERROR, "Transport connect failed: %d", l_ret);
                dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                      STAGE_STATUS_ERROR, ERROR_STREAM_CONNECT);
                break;
            }
        } else if (l_es->stream_es) {
            dap_events_socket_uuid_t *l_es_uuid_ptr = DAP_DUP(&l_es->stream_es->uuid);
            if (!dap_timerfd_start_on_worker(l_worker,
                                             (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                             s_stream_timer_timeout_check, l_es_uuid_ptr)) {
                DAP_DELETE(l_es_uuid_ptr);
                dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                      STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
                break;
            }
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_DONE, ERROR_NO_ERROR);
        } else {
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_DONE, ERROR_NO_ERROR);
        }
    } break;

    case STAGE_STREAM_CONNECTED: {
        debug_if(s_debug_more, L_INFO, "Worker: executing STAGE_STREAM_CONNECTED for client %p", l_client);

        if (!l_es->stream) {
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        // Create channels
        size_t l_count_channels = dap_strlen(l_client->active_channels);
        for (size_t i = 0; i < l_count_channels; i++)
            dap_stream_ch_new(l_es->stream, (uint8_t)l_client->active_channels[i]);

        // Install stream callbacks on the esocket BEFORE session_start sends data
        // This ensures read/write/error/delete are handled when server responds
        // CRITICAL: For datagram transports (UDP/DNS), the transport layer has already
        // installed its own read_callback that handles decryption, Flow Control, etc.
        // Overwriting it would break the transport's read path!
        if (l_es->stream_es) {
            dap_events_socket_callbacks_t l_stream_cbs;
            dap_client_esocket_get_stream_callbacks(&l_stream_cbs);
            bool l_is_datagram = (l_es->stream_es->type == DESCRIPTOR_TYPE_SOCKET_UDP);
            if (!l_is_datagram) {
                l_es->stream_es->callbacks.read_callback = l_stream_cbs.read_callback;
            }
            l_es->stream_es->callbacks.write_callback = l_stream_cbs.write_callback;
            l_es->stream_es->callbacks.error_callback = l_stream_cbs.error_callback;
            l_es->stream_es->callbacks.delete_callback = l_stream_cbs.delete_callback;
        }

        dap_net_trans_t *l_transport = l_es->stream->trans;
        if (l_transport && l_transport->ops && l_transport->ops->session_start) {
            int l_start_ret = l_transport->ops->session_start(
                l_es->stream, l_es->stream_id, s_session_start_callback_wrapper);
            if (l_start_ret != 0) {
                log_it(L_ERROR, "Session start failed: %d", l_start_ret);
                dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                      STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
                break;
            }
        } else {
            if (l_es->stream_es) {
                dap_events_socket_uuid_t *l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
                if (l_es_uuid_ptr) {
                    *l_es_uuid_ptr = l_es->stream_es->uuid;
                    if (!dap_timerfd_start_on_worker(l_worker,
                                                     s_client_timeout_active_after_connect_seconds * 1024,
                                                     s_stream_timer_timeout_after_connected_check, l_es_uuid_ptr)) {
                        DAP_DELETE(l_es_uuid_ptr);
                    }
                }
            }
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_DONE, ERROR_NO_ERROR);
        }
    } break;

    case STAGE_STREAM_STREAMING: {
        debug_if(s_debug_more, L_INFO, "Worker: executing STAGE_STREAM_STREAMING for client %p", l_client);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
    } break;

    case STAGE_QOS_PROBE: {
        debug_if(s_debug_more, L_INFO, "Worker: executing STAGE_QOS_PROBE for client %p", l_client);

        dap_net_trans_t *l_transport = dap_net_trans_find(l_client->trans_type);
        if (!l_transport || !l_transport->ops || !l_transport->ops->handshake_init) {
            log_it(L_ERROR, "Transport type %d not available or missing handshake_init for QoS probe",
                   l_client->trans_type);
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        static dap_events_socket_callbacks_t s_qos_callbacks = {
            .read_callback = NULL,
            .write_callback = NULL,
            .error_callback = NULL,
            .delete_callback = s_handshake_es_delete_callback,
            .connected_callback = NULL
        };

        dap_net_stage_prepare_params_t l_prepare_params = {
            .host = l_client->link_info.uplink_addr,
            .port = l_client->link_info.uplink_port,
            .node_addr = &l_client->link_info.node_addr,
            .authorized = false,
            .callbacks = &s_qos_callbacks,
            .client_ctx = l_client,
            .worker = l_worker
        };

        dap_net_stage_prepare_result_t l_prepare_result;
        int l_ret = dap_net_trans_stage_prepare(l_client->trans_type, &l_prepare_params, &l_prepare_result);
        if (l_ret != 0) {
            log_it(L_ERROR, "Stage prepare failed for QoS probe: transport %d, error %d",
                   l_client->trans_type, l_prepare_result.error_code);
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
            break;
        }

        if (!l_prepare_result.stream) {
            log_it(L_CRITICAL, "Transport failed to create stream for QoS probe");
            if (l_prepare_result.esocket)
                dap_events_socket_delete_unsafe(l_prepare_result.esocket, true);
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_OUT_OF_MEMORY);
            break;
        }

        l_es->stream = l_prepare_result.stream;
        l_es->stream_es = l_prepare_result.esocket;

        #define DAP_QOS_PROBE_PAYLOAD_SIZE 800
        uint8_t *l_probe_buf = DAP_NEW_Z_SIZE(uint8_t, DAP_QOS_PROBE_PAYLOAD_SIZE);
        if (!l_probe_buf) {
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_OUT_OF_MEMORY);
            break;
        }

        dap_qos_probe_pkt_t *l_probe = (dap_qos_probe_pkt_t *)l_probe_buf;
        l_probe->magic = DAP_QOS_PROBE_MAGIC;
        l_probe->type  = DAP_QOS_TYPE_PROBE;
        dap_random_bytes((uint8_t *)&l_probe->probe_id, sizeof(l_probe->probe_id));
        l_probe->client_ts = 0;
        if (DAP_QOS_PROBE_PAYLOAD_SIZE > sizeof(dap_qos_probe_pkt_t))
            dap_random_bytes(l_probe_buf + sizeof(dap_qos_probe_pkt_t),
                        DAP_QOS_PROBE_PAYLOAD_SIZE - sizeof(dap_qos_probe_pkt_t));

        dap_net_handshake_params_t l_hs_params = {
            .pkey_exchange_type = DAP_ENC_KEY_TYPE_QOS_PROBE,
            .pkey_exchange_size = DAP_QOS_PROBE_PAYLOAD_SIZE,
            .alice_pub_key = l_probe_buf,
            .alice_pub_key_size = DAP_QOS_PROBE_PAYLOAD_SIZE,
            .sign_count = 0
        };

        int l_hs_ret = l_transport->ops->handshake_init(l_es->stream, &l_hs_params,
                                                         s_qos_handshake_callback);
        if (l_hs_ret != 0) {
            log_it(L_ERROR, "Failed to initiate QoS probe handshake: %d", l_hs_ret);
            DAP_DELETE(l_probe_buf);
            dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                                  STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        }
    } break;

    default:
        log_it(L_ERROR, "Unknown stage %d for worker execution", l_stage);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        break;
    }

    DAP_DELETE(l_ctx);
}

// ===== Timer callbacks (run on worker, notify FSM) =====

static bool s_stream_timer_timeout_check(void *a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t *)a_arg;
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        DAP_DELETE(l_es_uuid_ptr);
        return false;
    }

    dap_events_socket_t *l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if (l_es && (l_es->flags & DAP_SOCK_CONNECTING)) {
        dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
        if (l_client) {
            dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
            if (l_fsm) {
                dap_client_esocket_t *l_client_es = l_fsm->esocket;
                if (l_client_es)
                    l_client_es->is_closed_by_timeout = true;
            }
            log_it(L_WARNING, "Connecting timeout for stream uplink %s:%u",
                   l_client->link_info.uplink_addr, l_client->link_info.uplink_port);
        }
        if (l_es->callbacks.error_callback)
            l_es->callbacks.error_callback(l_es, ETIMEDOUT);
        dap_events_socket_remove_and_delete_unsafe(l_es, true);
    }

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

static bool s_stream_timer_timeout_after_connected_check(void *a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t *)a_arg;
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        DAP_DELETE(l_es_uuid_ptr);
        return false;
    }

    dap_events_socket_t *l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if (l_es) {
        dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
        if (!l_client) {
            DAP_DELETE(l_es_uuid_ptr);
            return false;
        }
        dap_client_esocket_t *l_client_es = DAP_CLIENT_ESOCKET(l_client);
        if (!l_client_es || l_client_es->is_removing) {
            DAP_DELETE(l_es_uuid_ptr);
            return false;
        }

        if (dap_time_now() - l_client_es->ts_last_active >= (dap_time_t)s_client_timeout_active_after_connect_seconds) {
            log_it(L_WARNING, "Activity timeout for streaming uplink %s:%u",
                   l_client->link_info.uplink_addr, l_client->link_info.uplink_port);
            l_client_es->is_closed_by_timeout = true;
            if (l_es->callbacks.error_callback)
                l_es->callbacks.error_callback(l_es, ETIMEDOUT);
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        }
    }

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

// ===== Main FSM process function (runs on FSM thread) =====

static void s_fsm_process(dap_client_fsm_t *a_fsm)
{
    if (!a_fsm || !a_fsm->client)
        return;

    dap_client_stage_status_t l_stage_status = a_fsm->stage_status;
    dap_client_stage_t l_stage = a_fsm->stage;

    switch (l_stage_status) {
    case STAGE_STATUS_IN_PROGRESS: {
        // Dispatch stage execution to worker
        s_fsm_dispatch_stage_to_worker(a_fsm);
    } break;

    case STAGE_STATUS_ERROR: {
        bool l_is_last_attempt = a_fsm->reconnect_attempts >= s_max_attempts;

        if (!l_is_last_attempt) {
            if (!a_fsm->reconnect_attempts) {
                log_it(L_ERROR, "Error state(%s), doing callback",
                       dap_client_error_str(a_fsm->last_error));
                if (a_fsm->client->stage_status_error_callback)
                    a_fsm->client->stage_status_error_callback(a_fsm->client, (void *)(intptr_t)l_is_last_attempt);
            }
            s_set_stage_status(a_fsm, STAGE_STATUS_IN_PROGRESS);
        } else {
            if (s_retry_handshake_with_fallback(a_fsm) == 0) {
                log_it(L_NOTICE, "Switching to fallback transport %s after %d failed attempts",
                       dap_net_trans_type_to_str(a_fsm->client->trans_type),
                       a_fsm->reconnect_attempts);
                a_fsm->reconnect_attempts = 0;
                return;
            }
            log_it(L_ERROR, "Disconnect state(%s), all transports exhausted, doing callback",
                   dap_client_error_str(a_fsm->last_error));
            if (a_fsm->client->stage_status_error_callback)
                a_fsm->client->stage_status_error_callback(a_fsm->client, (void *)(intptr_t)l_is_last_attempt);
            if (a_fsm->client->always_reconnect) {
                log_it(L_INFO, "Too many attempts, reconnect in %d seconds (reset tried transports)", s_timeout);
                s_set_stage_status(a_fsm, STAGE_STATUS_IN_PROGRESS);
                a_fsm->reconnect_attempts = 0;
                a_fsm->tried_transport_count = 0;
                s_add_tried_transport(a_fsm, a_fsm->client->trans_type);
            } else {
                log_it(L_ERROR, "Connect to %s:%u failed",
                       a_fsm->client->link_info.uplink_addr, a_fsm->client->link_info.uplink_port);
            }
        }

        ++a_fsm->reconnect_attempts;

        if (a_fsm->stage_status == STAGE_STATUS_IN_PROGRESS) {
            // Schedule cleanup + reconnect on worker
            s_set_stage(a_fsm, STAGE_ENC_INIT);

            // Start reconnect timer on worker
            fsm_reconnect_timer_ctx_t *l_timer_ctx = DAP_NEW_Z(fsm_reconnect_timer_ctx_t);
            if (l_timer_ctx) {
                l_timer_ctx->fsm_uuid = a_fsm->uuid;
                l_timer_ctx->fsm_thread_idx = a_fsm->fsm_thread_idx;

                unsigned long l_delay_ms = l_is_last_attempt ? (s_timeout * 1000) : 300;
                log_it(L_INFO, "Reconnect attempt %d in %lu ms", a_fsm->reconnect_attempts, l_delay_ms);

                if (!dap_timerfd_start_on_worker(a_fsm->worker, l_delay_ms,
                                                 s_timer_reconnect_callback, l_timer_ctx)) {
                    log_it(L_ERROR, "Can't start reconnect timer");
                    DAP_DELETE(l_timer_ctx);
                }
            }
        } else {
            // Final error, clean up on worker
            // Dispatch STAGE_BEGIN to clean resources
            fsm_worker_dispatch_t *l_dispatch = DAP_NEW_Z(fsm_worker_dispatch_t);
            if (l_dispatch) {
                l_dispatch->fsm_uuid = a_fsm->uuid;
                l_dispatch->fsm_thread_idx = a_fsm->fsm_thread_idx;
                l_dispatch->client = a_fsm->client;
                l_dispatch->stage = STAGE_BEGIN;
                dap_worker_exec_callback_on(a_fsm->worker, s_worker_execute_stage, l_dispatch);
            }
        }
    } break;

    case STAGE_STATUS_DONE: {
        debug_if(s_debug_more, L_INFO, "Stage %s done for client %p (target: %s)",
               dap_client_stage_str(a_fsm->stage),
               a_fsm->client,
               dap_client_stage_str(a_fsm->client->stage_target));

        bool l_is_last_stage = (a_fsm->stage == a_fsm->client->stage_target);
        if (l_is_last_stage) {
            s_set_stage_status(a_fsm, STAGE_STATUS_COMPLETE);

            if (a_fsm->esocket && a_fsm->esocket->stream)
                dap_stream_add_to_list(a_fsm->esocket->stream);

            if (a_fsm->client->stage_target_done_callback) {
                log_it(L_NOTICE, "Stage %s achieved", dap_client_stage_str(a_fsm->stage));
                a_fsm->client->stage_target_done_callback(a_fsm->client, a_fsm->client->callbacks_arg);
            }

            // Send queued packets (needs worker)
            if (a_fsm->stage == STAGE_STREAM_STREAMING && a_fsm->esocket && a_fsm->esocket->pkt_queue) {
                // Send queued packets on worker
                for (dap_list_t *it = a_fsm->esocket->pkt_queue; it; it = it->next) {
                    dap_client_pkt_queue_elm_t *l_pkt = it->data;
                    dap_client_write_unsafe(a_fsm->client, l_pkt->ch_id, l_pkt->type, l_pkt->data, l_pkt->data_size);
                }
                dap_list_free_full(a_fsm->esocket->pkt_queue, NULL);
                a_fsm->esocket->pkt_queue = NULL;
            }
        } else {
            // Advance to next stage
            if (!a_fsm->stage_status_done_callback) {
                log_it(L_ERROR, "Stage %s completed but no done_callback", dap_client_stage_str(a_fsm->stage));
                s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
                a_fsm->last_error = ERROR_STREAM_ABORTED;
            } else {
                a_fsm->stage_status_done_callback(a_fsm->client, NULL);
            }
        }
    } break;

    case STAGE_STATUS_COMPLETE: {
        if (a_fsm->stage < a_fsm->client->stage_target) {
            debug_if(s_debug_more, L_DEBUG, "Stage %s COMPLETE but target is %s, advancing",
                   dap_client_stage_str(a_fsm->stage),
                   dap_client_stage_str(a_fsm->client->stage_target));
            if (a_fsm->stage_status_done_callback)
                a_fsm->stage_status_done_callback(a_fsm->client, NULL);
            else
                dap_client_fsm_stage_transaction_begin(a_fsm, a_fsm->stage + 1, dap_client_fsm_advance);
        }
    } break;

    default:
        log_it(L_ERROR, "Unknown stage status %d", l_stage_status);
        break;
    }
}

// ===== Handshake esocket delete callback: prevent reactor from freeing dap_client_t =====

static void s_handshake_es_delete_callback(dap_events_socket_t *a_es, void *a_arg)
{
    (void)a_arg;
    if (a_es)
        a_es->_inheritor = NULL;
}

// ===== Worker-side ENC_INIT IO (only transport calls, crypto already done on FSM thread) =====

static void s_worker_execute_enc_init_io(void *a_arg)
{
    fsm_enc_init_io_ctx_t *l_ctx = (fsm_enc_init_io_ctx_t *)a_arg;
    if (!l_ctx) return;

    dap_client_t *l_client = l_ctx->client;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es) {
        log_it(L_ERROR, "No esocket for ENC_INIT IO");
        DAP_DELETE(l_ctx->handshake_params.alice_pub_key);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        DAP_DELETE(l_ctx);
        return;
    }

    dap_worker_t *l_worker = l_es->worker;

    // Get transport
    dap_net_trans_t *l_transport = dap_net_trans_find(l_ctx->trans_type);
    if (!l_transport || !l_transport->ops || !l_transport->ops->handshake_init) {
        log_it(L_ERROR, "Transport type %d not available or missing handshake_init", l_ctx->trans_type);
        DAP_DELETE(l_ctx->handshake_params.alice_pub_key);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        DAP_DELETE(l_ctx);
        return;
    }

    // Stage prepare: create esocket/stream via transport (must be on worker)
    // Start with minimal callbacks - stream callbacks will be installed when streaming begins
    // connected_callback MUST be NULL here - transport handles connection flow
    // delete_callback MUST nullify _inheritor to prevent reactor from freeing dap_client_t
    static dap_events_socket_callbacks_t s_handshake_callbacks = {
        .read_callback = NULL,
        .write_callback = NULL,
        .error_callback = NULL,
        .delete_callback = s_handshake_es_delete_callback,
        .connected_callback = NULL
    };

    dap_net_stage_prepare_params_t l_prepare_params = {
        .host = l_client->link_info.uplink_addr,
        .port = l_client->link_info.uplink_port,
        .node_addr = &l_client->link_info.node_addr,
        .authorized = false,
        .callbacks = &s_handshake_callbacks,
        .client_ctx = l_client,
        .worker = l_worker
    };

    dap_net_stage_prepare_result_t l_prepare_result;
    int l_ret = dap_net_trans_stage_prepare(l_ctx->trans_type, &l_prepare_params, &l_prepare_result);

    if (l_ret != 0) {
        log_it(L_ERROR, "Stage prepare failed: transport %d, error %d", l_ctx->trans_type,
               l_prepare_result.error_code);
        DAP_DELETE(l_ctx->handshake_params.alice_pub_key);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
        DAP_DELETE(l_ctx);
        return;
    }

    if (!l_prepare_result.stream) {
        log_it(L_CRITICAL, "Transport failed to create stream for handshake");
        if (l_prepare_result.esocket)
            dap_events_socket_delete_unsafe(l_prepare_result.esocket, true);
        DAP_DELETE(l_ctx->handshake_params.alice_pub_key);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_OUT_OF_MEMORY);
        DAP_DELETE(l_ctx);
        return;
    }

    l_es->stream = l_prepare_result.stream;
    l_es->stream_es = l_prepare_result.esocket;

    // Handshake init: async IO, transport callback will notify FSM
    int l_handshake_ret = l_transport->ops->handshake_init(l_es->stream, &l_ctx->handshake_params,
                                                            s_handshake_callback_wrapper);
    if (l_handshake_ret != 0) {
        log_it(L_ERROR, "Failed to initiate handshake: %d", l_handshake_ret);
        DAP_DELETE(l_ctx->handshake_params.alice_pub_key);
        dap_client_fsm_notify(l_ctx->fsm_uuid, l_ctx->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);
    }
    // On success: transport owns alice_pub_key, callback will call dap_client_fsm_notify

    DAP_DELETE(l_ctx);
}

// ===== Dispatch stage to worker (called on FSM thread) =====

static void s_fsm_dispatch_stage_to_worker(dap_client_fsm_t *a_fsm)
{
    // For STAGE_BEGIN: dispatch clean to worker
    if (a_fsm->stage == STAGE_BEGIN) {
        fsm_worker_dispatch_t *l_dispatch = DAP_NEW_Z(fsm_worker_dispatch_t);
        if (!l_dispatch) return;
        l_dispatch->fsm_uuid = a_fsm->uuid;
        l_dispatch->fsm_thread_idx = a_fsm->fsm_thread_idx;
        l_dispatch->client = a_fsm->client;
        l_dispatch->stage = STAGE_BEGIN;
        a_fsm->reconnect_attempts = 0;
        dap_worker_exec_callback_on(a_fsm->worker, s_worker_execute_stage, l_dispatch);
        return;
    }

    // For STAGE_STREAM_STREAMING: no IO needed, immediate done
    if (a_fsm->stage == STAGE_STREAM_STREAMING) {
        a_fsm->reconnect_attempts = 0;
        s_set_stage_status(a_fsm, STAGE_STATUS_DONE);
        s_fsm_process(a_fsm);
        return;
    }

    // For STAGE_ENC_INIT: heavy crypto on FSM thread, then IO-only dispatch to worker
    if (a_fsm->stage == STAGE_ENC_INIT) {
        dap_client_esocket_t *l_es = a_fsm->esocket;
        dap_client_t *l_client = a_fsm->client;

        // Validate address
        if (!*l_client->link_info.uplink_addr || !l_client->link_info.uplink_port) {
            log_it(L_ERROR, "Client remote address is empty");
            s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
            a_fsm->last_error = ERROR_WRONG_ADDRESS;
            s_fsm_process(a_fsm);
            return;
        }

        // Generate session_key_open (HEAVY CRYPTO - runs on FSM thread, not worker!)
        debug_if(s_debug_more, L_INFO, "FSM thread: generating session key for client %p", l_client);
        if (l_es->session_key_open)
            dap_enc_key_delete(l_es->session_key_open);
        l_es->session_key_open = dap_enc_key_new_generate(l_es->session_key_open_type, NULL, 0, NULL, 0,
                                                           l_es->session_key_block_size);
        if (!l_es->session_key_open) {
            log_it(L_ERROR, "Insufficient memory for session_key_open");
            s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
            a_fsm->last_error = ERROR_OUT_OF_MEMORY;
            s_fsm_process(a_fsm);
            return;
        }

        // Prepare alice_pub_key with signatures (crypto - on FSM thread)
        size_t l_data_size = l_es->session_key_open->pub_key_data_size;
        uint8_t *l_alice_pub_key = DAP_DUP_SIZE((uint8_t *)l_es->session_key_open->pub_key_data, l_data_size);
        if (!l_alice_pub_key) {
            s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
            a_fsm->last_error = ERROR_OUT_OF_MEMORY;
            s_fsm_process(a_fsm);
            return;
        }

        dap_cert_t *l_node_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
        size_t l_sign_count = 0;
        if (l_client->auth_cert)
            l_sign_count += dap_cert_add_sign_to_data(l_client->auth_cert, &l_alice_pub_key, &l_data_size,
                                                       l_es->session_key_open->pub_key_data,
                                                       l_es->session_key_open->pub_key_data_size);
        if (l_node_cert)
            l_sign_count += dap_cert_add_sign_to_data(l_node_cert, &l_alice_pub_key, &l_data_size,
                                                      l_es->session_key_open->pub_key_data,
                                                      l_es->session_key_open->pub_key_data_size);

        // Build dispatch context with prepared handshake params for worker
        fsm_enc_init_io_ctx_t *l_dispatch = DAP_NEW_Z(fsm_enc_init_io_ctx_t);
        if (!l_dispatch) {
            DAP_DELETE(l_alice_pub_key);
            s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
            a_fsm->last_error = ERROR_OUT_OF_MEMORY;
            s_fsm_process(a_fsm);
            return;
        }

        l_dispatch->fsm_uuid = a_fsm->uuid;
        l_dispatch->fsm_thread_idx = a_fsm->fsm_thread_idx;
        l_dispatch->client = l_client;
        l_dispatch->trans_type = l_client->trans_type;
        l_dispatch->handshake_params = (dap_net_handshake_params_t){
            .enc_type = l_es->session_key_type,
            .pkey_exchange_type = l_es->session_key_open_type,
            .pkey_exchange_size = l_es->session_key_open->pub_key_data_size,
            .block_key_size = l_es->session_key_block_size,
            .protocol_version = DAP_CLIENT_PROTOCOL_VERSION,
            .auth_cert = l_client->auth_cert,
            .alice_pub_key = l_alice_pub_key,
            .alice_pub_key_size = l_data_size,
            .sign_count = l_sign_count
        };

        debug_if(s_debug_more, L_INFO, "FSM thread: ENC_INIT crypto done, dispatching IO to worker");
        dap_worker_exec_callback_on(a_fsm->worker, s_worker_execute_enc_init_io, l_dispatch);
        return;
    }

    // For STAGE_QOS_PROBE: build probe payload on FSM thread, dispatch IO to worker
    if (a_fsm->stage == STAGE_QOS_PROBE) {
        dap_client_t *l_client = a_fsm->client;

        if (!*l_client->link_info.uplink_addr || !l_client->link_info.uplink_port) {
            log_it(L_ERROR, "Client remote address is empty for QOS_PROBE");
            s_set_stage_status(a_fsm, STAGE_STATUS_ERROR);
            a_fsm->last_error = ERROR_WRONG_ADDRESS;
            s_fsm_process(a_fsm);
            return;
        }

        fsm_worker_dispatch_t *l_dispatch = DAP_NEW_Z(fsm_worker_dispatch_t);
        if (!l_dispatch) return;
        l_dispatch->fsm_uuid = a_fsm->uuid;
        l_dispatch->fsm_thread_idx = a_fsm->fsm_thread_idx;
        l_dispatch->client = l_client;
        l_dispatch->stage = STAGE_QOS_PROBE;
        dap_worker_exec_callback_on(a_fsm->worker, s_worker_execute_stage, l_dispatch);
        return;
    }

    // For all other stages (STREAM_CTL, STREAM_SESSION, STREAM_CONNECTED): dispatch to worker
    fsm_worker_dispatch_t *l_dispatch = DAP_NEW_Z(fsm_worker_dispatch_t);
    if (!l_dispatch) return;
    l_dispatch->fsm_uuid = a_fsm->uuid;
    l_dispatch->fsm_thread_idx = a_fsm->fsm_thread_idx;
    l_dispatch->client = a_fsm->client;
    l_dispatch->stage = a_fsm->stage;
    dap_worker_exec_callback_on(a_fsm->worker, s_worker_execute_stage, l_dispatch);
}

// ===== FSM stage transaction =====

void dap_client_fsm_stage_transaction_begin(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage_next,
                                             dap_client_callback_t a_done_callback)
{
    assert(a_fsm);

    debug_if(s_debug_more, L_DEBUG, "FSM transaction begin: %s -> %s",
             dap_client_stage_str(a_fsm->stage), dap_client_stage_str(a_stage_next));

    a_fsm->stage_status_done_callback = a_done_callback;
    s_set_stage_and_status(a_fsm, a_stage_next, STAGE_STATUS_IN_PROGRESS);
    s_fsm_process(a_fsm);
}

// ===== FSM advance (stage_status_done_callback) =====

void dap_client_fsm_advance(dap_client_t *a_client, void *a_arg)
{
    (void)a_arg;
    assert(a_client);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) {
        log_it(L_ERROR, "No FSM in dap_client_fsm_advance");
        return;
    }

    if (a_client->stage_target == l_fsm->stage) {
        log_it(L_WARNING, "FSM advance: already at target %s", dap_client_stage_str(l_fsm->stage));
        l_fsm->stage_status_done_callback = NULL;
        s_set_stage_status(l_fsm, STAGE_STATUS_DONE);
        if (a_client->stage_target_done_callback)
            a_client->stage_target_done_callback(a_client, a_client->callbacks_arg);
        return;
    }

    dap_client_stage_t l_next;
    if (a_client->stage_target == STAGE_QOS_PROBE) {
        if (l_fsm->stage == STAGE_BEGIN) {
            l_next = STAGE_QOS_PROBE;
        } else {
            log_it(L_ERROR, "FSM advance: QOS_PROBE branch reached unexpected stage %s",
                   dap_client_stage_str(l_fsm->stage));
            s_set_stage_status(l_fsm, STAGE_STATUS_ERROR);
            l_fsm->last_error = ERROR_WRONG_STAGE;
            s_fsm_process(l_fsm);
            return;
        }
    } else {
        assert(a_client->stage_target > l_fsm->stage);
        l_next = l_fsm->stage + 1;
    }
    log_it(L_NOTICE, "FSM advance: %s -> %s (target %s)",
           dap_client_stage_str(l_fsm->stage), dap_client_stage_str(l_next),
           dap_client_stage_str(a_client->stage_target));
    dap_client_fsm_stage_transaction_begin(l_fsm, l_next, dap_client_fsm_advance);
}

// Backward compat
void dap_client_pvt_stage_fsm_advance(dap_client_t *a_client, void *a_arg)
{
    dap_client_fsm_advance(a_client, a_arg);
}

// ===== FSM go_stage (proc_thread callback) =====

static void *s_fsm_go_stage_on_fsm_thread(void *a_arg)
{
    fsm_go_stage_ctx_t *l_ctx = (fsm_go_stage_ctx_t *)a_arg;
    if (!l_ctx) return NULL;

    dap_client_fsm_t *l_fsm = dap_client_fsm_find(l_ctx->fsm_uuid);
    if (!l_fsm || l_fsm->is_removing) {
        DAP_DELETE(l_ctx);
        return NULL;
    }

    dap_client_t *l_client = l_fsm->client;
    l_client->stage_target = l_ctx->stage_target;
    l_client->stage_target_done_callback = l_ctx->done_callback;

    // Handle already at target
    if (l_fsm->stage_status == STAGE_STATUS_COMPLETE &&
        l_fsm->stage == l_ctx->stage_target) {
        debug_if(s_debug_more, L_DEBUG, "Already at target stage %s", dap_client_stage_str(l_ctx->stage_target));
        if (l_ctx->done_callback)
            l_ctx->done_callback(l_client, l_client->callbacks_arg);
        DAP_DELETE(l_ctx);
        return NULL;
    }

    // If COMPLETE and below target, advance from current
    if (l_fsm->stage_status == STAGE_STATUS_COMPLETE && l_fsm->stage != l_ctx->stage_target) {
        debug_if(s_debug_more, L_DEBUG, "FSM at %s COMPLETE, advancing to %s",
               dap_client_stage_str(l_fsm->stage), dap_client_stage_str(l_ctx->stage_target));
        dap_client_stage_t l_next;
        if (l_ctx->stage_target == STAGE_QOS_PROBE && l_fsm->stage == STAGE_BEGIN)
            l_next = STAGE_QOS_PROBE;
        else
            l_next = l_fsm->stage + 1;
        dap_client_fsm_stage_transaction_begin(l_fsm, l_next, dap_client_fsm_advance);
        DAP_DELETE(l_ctx);
        return NULL;
    }

    // Otherwise, start from BEGIN
    debug_if(s_debug_more, L_DEBUG, "FSM go_stage: start from BEGIN to %s", dap_client_stage_str(l_ctx->stage_target));
    dap_client_fsm_stage_transaction_begin(l_fsm, STAGE_BEGIN, dap_client_fsm_advance);
    DAP_DELETE(l_ctx);
    return NULL;
}

void dap_client_fsm_go_stage(dap_client_fsm_t *a_fsm, dap_client_stage_t a_stage_target,
                              dap_client_callback_t a_done_callback)
{
    if (!a_fsm || a_fsm->is_removing)
        return;

    fsm_go_stage_ctx_t *l_ctx = DAP_NEW_Z(fsm_go_stage_ctx_t);
    if (!l_ctx) return;
    l_ctx->fsm_uuid = a_fsm->uuid;
    l_ctx->stage_target = a_stage_target;
    l_ctx->done_callback = a_done_callback;

    s_fsm_thread_callback_add(a_fsm->fsm_thread_idx, s_fsm_go_stage_on_fsm_thread, l_ctx);
}

// ===== FSM notification from worker =====

static void *s_fsm_notify_on_fsm_thread(void *a_arg)
{
    fsm_notify_ctx_t *l_ctx = (fsm_notify_ctx_t *)a_arg;
    if (!l_ctx) return NULL;

    dap_client_fsm_t *l_fsm = dap_client_fsm_find(l_ctx->fsm_uuid);
    if (!l_fsm || l_fsm->is_removing) {
        DAP_DELETE(l_ctx);
        return NULL;
    }

    s_set_stage_status(l_fsm, l_ctx->status);
    if (l_ctx->status == STAGE_STATUS_ERROR)
        l_fsm->last_error = l_ctx->error;

    s_fsm_process(l_fsm);

    DAP_DELETE(l_ctx);
    return NULL;
}

void dap_client_fsm_notify(uint64_t a_fsm_uuid, uint32_t a_fsm_thread_idx,
                            dap_client_stage_status_t a_status, dap_client_error_t a_error)
{
    fsm_notify_ctx_t *l_ctx = DAP_NEW_Z(fsm_notify_ctx_t);
    if (!l_ctx) return;
    l_ctx->fsm_uuid = a_fsm_uuid;
    l_ctx->status = a_status;
    l_ctx->error = a_error;

    s_fsm_thread_callback_add(a_fsm_thread_idx, s_fsm_notify_on_fsm_thread, l_ctx);
}

static void *s_fsm_timer_fired_on_fsm_thread(void *a_arg)
{
    fsm_proc_ctx_t *l_ctx = (fsm_proc_ctx_t *)a_arg;
    if (!l_ctx) return NULL;

    dap_client_fsm_t *l_fsm = dap_client_fsm_find(l_ctx->fsm_uuid);
    if (l_fsm && !l_fsm->is_removing) {
        l_fsm->reconnect_pending = false;
        // Timer fired = retry the stage
        s_set_stage_status(l_fsm, STAGE_STATUS_IN_PROGRESS);
        s_fsm_process(l_fsm);
    }

    DAP_DELETE(l_ctx);
    return NULL;
}

void dap_client_fsm_notify_timer_fired(uint64_t a_fsm_uuid, uint32_t a_fsm_thread_idx)
{
    fsm_proc_ctx_t *l_ctx = DAP_NEW_Z(fsm_proc_ctx_t);
    if (!l_ctx) return;
    l_ctx->fsm_uuid = a_fsm_uuid;

    s_fsm_thread_callback_add(a_fsm_thread_idx, s_fsm_timer_fired_on_fsm_thread, l_ctx);
}
