/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017-2026
 * All rights reserved.
 *
 * dap_client_esocket - Client IO/transport layer
 *
 * Manages IO resources on worker threads. FSM logic is in dap_client_fsm.c.
 * Callbacks notify the FSM via dap_client_fsm_notify().
 *
 * This file is part of DAP SDK the open source project
 *    DAP SDK is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
*/

#include "dap_common.h"
#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc.h"
#include "dap_strfuncs.h"
#include "dap_cert.h"
#include "dap_context.h"
#include "dap_timerfd.h"
#include "dap_client_esocket.h"
#include "dap_client_fsm.h"
#include "uthash.h"
#include "dap_enc_ks.h"
#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_pkt.h"
#include "dap_net.h"
#include "dap_net_trans.h"
#include "dap_stream_handshake.h"

#define LOG_TAG "dap_client_esocket"

// Global hash table for UUID-based lookup of client esocket contexts
static dap_client_esocket_t *s_esocket_table = NULL;
static pthread_rwlock_t s_esocket_table_lock = PTHREAD_RWLOCK_INITIALIZER;

#ifndef DAP_ENC_KS_KEY_ID_SIZE
#define DAP_ENC_KS_KEY_ID_SIZE 33
#endif

static bool s_debug_more = false;

// Forward declarations for callbacks
static void s_stream_es_callback_connected(dap_events_socket_t *a_es);
static void s_stream_es_callback_delete(dap_events_socket_t *a_es, void *a_arg);
static void s_stream_es_callback_read(dap_events_socket_t *a_es, void *a_arg);
static bool s_stream_es_callback_write(dap_events_socket_t *a_es, void *a_arg);
static void s_stream_es_callback_error(dap_events_socket_t *a_es, int a_error);

// Response processing
static void s_enc_init_response(dap_client_t *a_client, const void *a_data, size_t a_data_size);
static void s_enc_init_error(dap_client_t *a_client, void *a_arg, int a_error);
static void s_stream_ctl_response(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_stream_ctl_error(dap_client_t *a_client, void *a_arg, int a_error);
static void s_stage_stream_streaming(dap_client_t *a_client, void *a_arg);

// JSON parser helper
static int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...);

// ===== UUID-based global registry =====

dap_client_esocket_t *dap_client_esocket_find(uint64_t a_uuid)
{
    dap_client_esocket_t *l_result = NULL;
    pthread_rwlock_rdlock(&s_esocket_table_lock);
    HASH_FIND(hh, s_esocket_table, &a_uuid, sizeof(uint64_t), l_result);
    pthread_rwlock_unlock(&s_esocket_table_lock);
    return l_result;
}

void dap_client_esocket_register(dap_client_esocket_t *a_esocket_ctx)
{
    if (!a_esocket_ctx || !a_esocket_ctx->uuid)
        return;
    pthread_rwlock_wrlock(&s_esocket_table_lock);
    dap_client_esocket_t *l_existing = NULL;
    HASH_FIND(hh, s_esocket_table, &a_esocket_ctx->uuid, sizeof(uint64_t), l_existing);
    if (!l_existing)
        HASH_ADD(hh, s_esocket_table, uuid, sizeof(uint64_t), a_esocket_ctx);
    pthread_rwlock_unlock(&s_esocket_table_lock);
}

void dap_client_esocket_unregister(dap_client_esocket_t *a_esocket_ctx)
{
    if (!a_esocket_ctx)
        return;
    pthread_rwlock_wrlock(&s_esocket_table_lock);
    dap_client_esocket_t *l_existing = NULL;
    HASH_FIND(hh, s_esocket_table, &a_esocket_ctx->uuid, sizeof(uint64_t), l_existing);
    if (l_existing)
        HASH_DEL(s_esocket_table, l_existing);
    pthread_rwlock_unlock(&s_esocket_table_lock);
}

// ===== Module init/deinit =====

int dap_client_esocket_init(void)
{
    s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);
    return 0;
}

void dap_client_esocket_deinit(void)
{
    pthread_rwlock_wrlock(&s_esocket_table_lock);
    dap_client_esocket_t *l_current, *l_tmp;
    HASH_ITER(hh, s_esocket_table, l_current, l_tmp) {
        HASH_DEL(s_esocket_table, l_current);
    }
    pthread_rwlock_unlock(&s_esocket_table_lock);
}

void dap_client_esocket_get_stream_callbacks(dap_events_socket_callbacks_t *a_callbacks)
{
    if (!a_callbacks) return;
    memset(a_callbacks, 0, sizeof(*a_callbacks));
    a_callbacks->connected_callback = s_stream_es_callback_connected;
    a_callbacks->read_callback = s_stream_es_callback_read;
    a_callbacks->write_callback = s_stream_es_callback_write;
    a_callbacks->error_callback = s_stream_es_callback_error;
    a_callbacks->delete_callback = s_stream_es_callback_delete;
}

// ===== Instance lifecycle =====

void dap_client_esocket_new(dap_client_esocket_t *a_es)
{
    // Register in global hash table
    dap_client_esocket_register(a_es);
}

/**
 * @brief Clean esocket IO resources (on worker thread)
 */
void dap_client_esocket_clean_unsafe(dap_client_esocket_t *a_es)
{
    if (!a_es)
        return;

    // Clean stream and transport resources
    if (a_es->stream) {
        dap_stream_t *l_stream = a_es->stream;
        a_es->stream = NULL;
        a_es->stream_es = NULL;
        a_es->stream_key = NULL;
        a_es->stream_id = 0;
        dap_stream_delete_unsafe(l_stream);
    }

    DAP_DEL_Z(a_es->session_key_id);
    if (a_es->session_key_open) {
        dap_enc_key_delete(a_es->session_key_open);
        a_es->session_key_open = NULL;
    }
    if (a_es->session_key) {
        dap_enc_key_delete(a_es->session_key);
        a_es->session_key = NULL;
    }

    a_es->is_closed_by_timeout = false;
    a_es->is_encrypted = false;
    a_es->is_encrypted_headers = false;
    a_es->is_close_session = false;
    a_es->remote_protocol_version = 0;
    a_es->ts_last_active = 0;
}

void dap_client_esocket_delete_unsafe(dap_client_esocket_t *a_es)
{
    if (!a_es)
        return;
    debug_if(s_debug_more, L_INFO, "dap_client_esocket_delete %p", a_es);
    dap_client_esocket_unregister(a_es);
    dap_client_esocket_clean_unsafe(a_es);
    DAP_DELETE(a_es);
}

// ===== Packet queue =====

void dap_client_esocket_queue_add(dap_client_esocket_t *a_es, const char a_ch_id, uint8_t a_type,
                                   void *a_data, size_t a_data_size)
{
    dap_client_pkt_queue_elm_t *l_pkt = DAP_NEW_Z_SIZE(dap_client_pkt_queue_elm_t,
                                                        sizeof(dap_client_pkt_queue_elm_t) + a_data_size);
    l_pkt->ch_id = a_ch_id;
    l_pkt->type = a_type;
    l_pkt->data_size = a_data_size;
    memcpy(l_pkt->data, a_data, a_data_size);
    a_es->pkt_queue = dap_list_append(a_es->pkt_queue, l_pkt);
}

int dap_client_esocket_queue_clear(dap_client_esocket_t *a_es)
{
    if (!a_es->pkt_queue)
        return -2;
    dap_list_free_full(a_es->pkt_queue, NULL);
    a_es->pkt_queue = NULL;
    return 0;
}

// ===== Callback wrappers for transport (called on worker, notify FSM) =====
// These are made non-static so dap_client_fsm.c can reference them via extern

void s_handshake_callback_wrapper(dap_stream_t *a_stream, const void *a_data, size_t a_data_size, int a_error)
{
    debug_if(s_debug_more, L_DEBUG, "Handshake callback: stream=%p, data=%p, size=%zu, error=%d",
           a_stream, a_data, a_data_size, a_error);
    
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
        log_it(L_WARNING, "Handshake failed with error %d, trying fallback", a_error);
        // Try fallback via FSM (transport fallback is FSM logic)
        dap_client_error_t l_err = (a_error == ETIMEDOUT)
            ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_NETWORK_CONNECTION_REFUSE;
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, l_err);
        return;
    }
    
    // Process handshake response
    if (a_data && a_data_size > 0) {
        // HTTP-style response with JSON data
        s_enc_init_response(l_client, a_data, a_data_size);
    } else {
        // Transport-protocol handshake completed directly
        debug_if(s_debug_more, L_DEBUG, "Handshake completed via transport protocol");
        if (a_stream->session && a_stream->session->key) {
            if (l_es->stream_key)
                dap_enc_key_delete(l_es->stream_key);
            l_es->stream_key = dap_enc_key_dup(a_stream->session->key);
        }
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
    }
}

void s_session_create_callback_wrapper(dap_stream_t *a_stream, uint32_t a_session_id,
                                        const char *a_response_data, size_t a_response_size, int a_error)
{
    if (!a_stream || !a_stream->trans_ctx)
        return;
    
    dap_client_t *l_client = NULL;
    if (a_stream->trans && a_stream->trans->ops && a_stream->trans->ops->get_client_context)
        l_client = (dap_client_t *)a_stream->trans->ops->get_client_context(a_stream);
    else if (a_stream->trans_ctx->esocket && a_stream->trans_ctx->esocket->_inheritor)
        l_client = (dap_client_t *)a_stream->trans_ctx->esocket->_inheritor;

    dap_client_esocket_t *l_es = l_client ? DAP_CLIENT_ESOCKET(l_client) : NULL;
    if (!l_es)
        return;
    
    if (a_error != 0) {
        dap_client_error_t l_err = (a_error == ETIMEDOUT)
            ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_STREAM_CTL_ERROR;
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, l_err);
        return;
    }
    
    if (a_session_id != 0 || (a_response_data && a_response_size > 0)) {
        if (a_response_data && a_response_size > 0) {
            s_stream_ctl_response(l_client, (void *)a_response_data, a_response_size);
            DAP_DELETE(a_response_data);
        } else {
            bool l_have_key = l_es->stream_key ||
                              (a_stream->session && a_stream->session->key);
            if (l_have_key) {
                l_es->stream_id = a_session_id;
                dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                                      STAGE_STATUS_DONE, ERROR_NO_ERROR);
            } else {
                dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                                      STAGE_STATUS_ERROR, ERROR_ENC_NO_KEY);
            }
        }
    } else {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT);
    }
}

void s_stream_transport_connect_callback(dap_stream_t *a_stream, int a_error_code)
{
    if (!a_stream || !a_stream->trans_ctx || !a_stream->trans_ctx->esocket)
        return;
    
    dap_client_t *l_client = NULL;
    if (a_stream->trans && a_stream->trans->ops && a_stream->trans->ops->get_client_context)
        l_client = (dap_client_t *)a_stream->trans->ops->get_client_context(a_stream);
    else
        l_client = DAP_ESOCKET_CLIENT(a_stream->trans_ctx->esocket);
    
    dap_client_esocket_t *l_es = l_client ? DAP_CLIENT_ESOCKET(l_client) : NULL;
    if (!l_es)
        return;
    
    if (a_error_code != 0) {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CONNECT);
        return;
    }
    
    log_it(L_INFO, "Transport connected for streaming on %s:%u",
                   l_client->link_info.uplink_addr, l_client->link_info.uplink_port);
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_DONE, ERROR_NO_ERROR);
}

// ===== ENC response processing (runs on worker) =====

static int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...)
{
    dap_return_val_if_pass(!a_key || !a_val || a_count % 2, 0);
    int l_ret = 0;
    va_list l_args;
    va_start(l_args, a_count);
    for (int i = 0; i < a_count / 2; ++i) {
        const char *l_key = va_arg(l_args, const char *);
        char **l_pointer = va_arg(l_args, char **);
        if (!strcmp(a_key, l_key)) {
            DAP_DEL_Z(*l_pointer);
            *l_pointer = dap_strdup(a_val);
            l_ret++;
        }
    }
    va_end(l_args);
    return l_ret;
}

static void s_enc_init_response(dap_client_t *a_client, const void *a_data, size_t a_data_size)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    dap_return_if_pass(!l_es || !a_data);

    char *l_data = (char *)a_data;
    char *l_session_id_b64 = NULL, *l_bob_message_b64 = NULL, *l_node_sign_b64 = NULL, *l_bob_message = NULL;
    dap_client_error_t l_error = ERROR_NO_ERROR;

    while (l_error == ERROR_NO_ERROR) {
        if (!l_es->session_key_open) {
            l_error = ERROR_ENC_SESSION_CLOSED;
            break;
        }
        if (a_data_size <= 10) {
            l_error = ERROR_ENC_NO_KEY;
            break;
        }

        size_t l_bob_message_size = 0;
        int l_json_parse_count = 0;
        struct json_object *jobj = json_tokener_parse(l_data);
        if (jobj) {
            json_object_object_foreach(jobj, key, val) {
                if (json_object_get_type(val) == json_type_string) {
                    const char *l_str = json_object_get_string(val);
                    l_json_parse_count += s_json_multy_obj_parse_str(key, l_str, 6,
                                            "encrypt_id", &l_session_id_b64, 
                                            "encrypt_msg", &l_bob_message_b64,
                                            "node_sign", &l_node_sign_b64);
                }
                if (json_object_get_type(val) == json_type_int) {
                    int val_int = json_object_get_int(val);
                    if (!strcmp(key, "dap_protocol_version")) {
                        l_es->remote_protocol_version = val_int;
                        l_json_parse_count++;
                    }
                }
            }
            json_object_put(jobj);
            if (!l_es->remote_protocol_version)
                l_es->remote_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT;
        }

        if (l_json_parse_count < 2 || l_json_parse_count > 4) {
            l_error = ERROR_ENC_NO_KEY;
            break;
        }
        if (!l_session_id_b64 || !l_bob_message_b64) {
            l_error = ERROR_ENC_NO_KEY;
            break;
        }

        // Decode session key id
        size_t l_len = strlen(l_session_id_b64), l_decoded_len;
        l_es->session_key_id = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1,
                                                            l_session_id_b64, l_bob_message_b64, l_node_sign_b64);
        l_decoded_len = dap_enc_base64_decode(l_session_id_b64, l_len, l_es->session_key_id, DAP_ENC_DATA_TYPE_B64);

        // Decode bob message
        l_len = strlen(l_bob_message_b64);
        l_bob_message = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1,
                                                    l_session_id_b64, l_bob_message_b64, l_node_sign_b64,
                                                    l_es->session_key_id);
        l_bob_message_size = dap_enc_base64_decode(l_bob_message_b64, l_len, l_bob_message, DAP_ENC_DATA_TYPE_B64);
        if (!l_bob_message_size) {
            l_error = ERROR_ENC_WRONG_KEY;
            break;
        }

        // Generate shared key
        if (!l_es->session_key_open->gen_alice_shared_key(
                l_es->session_key_open, l_es->session_key_open->priv_key_data,
                l_bob_message_size, (unsigned char *)l_bob_message)) {
            l_error = ERROR_ENC_WRONG_KEY;
            break;
        }

        // Generate session key
        l_es->session_key = dap_enc_key_new_generate(l_es->session_key_type,
                l_es->session_key_open->priv_key_data,
                l_es->session_key_open->priv_key_data_size,
                l_es->session_key_id, l_decoded_len, l_es->session_key_block_size);

        // Verify node sign
        if (l_node_sign_b64) {
            l_len = strlen(l_node_sign_b64);
            dap_sign_t *l_sign = DAP_NEW_Z_SIZE_RET_IF_FAIL(dap_sign_t, DAP_ENC_BASE64_DECODE_SIZE(l_len) + 1,
                l_session_id_b64, l_bob_message_b64, l_node_sign_b64, l_bob_message, l_es->session_key_id);
            l_decoded_len = dap_enc_base64_decode(l_node_sign_b64, l_len, l_sign, DAP_ENC_DATA_TYPE_B64);
            if (!dap_sign_verify_all(l_sign, l_decoded_len, l_bob_message, l_bob_message_size)) {
                dap_stream_node_addr_t l_sign_addr = dap_stream_node_addr_from_sign(l_sign);
                l_es->authorized = (l_sign_addr.uint64 == a_client->link_info.node_addr.uint64);
                } else {
                l_es->authorized = false;
            }
            DAP_DELETE(l_sign);
        } else {
            l_es->authorized = false;
        }
        break;
    }

    DAP_DEL_MULTY(l_session_id_b64, l_bob_message_b64, l_node_sign_b64, l_bob_message);

    if (l_error == ERROR_NO_ERROR) {
        // Clean up key exchange
        dap_enc_key_delete(l_es->session_key_open);
        l_es->session_key_open = NULL;
        // Notify FSM: handshake done
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
        } else {
        DAP_DEL_Z(l_es->session_key_id);
        dap_enc_key_delete(l_es->session_key_open);
        l_es->session_key_open = NULL;
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, l_error);
    }
}

static void s_enc_init_error(dap_client_t *a_client, UNUSED_ARG void *a_arg, int a_err_code)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    if (!l_es) return;
    dap_client_error_t l_err = (a_err_code == ETIMEDOUT)
        ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_NETWORK_CONNECTION_REFUSE;
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_ERROR, l_err);
}

static void s_stream_ctl_response(dap_client_t *a_client, void *a_data, size_t a_data_size)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    if (!l_es) return;

    char *l_response_str = (char *)a_data;
    if (!l_response_str || a_data_size < 4) {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT);
        return;
    }

    if (!strncmp(l_response_str, "ERROR", a_data_size)) {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CTL_ERROR_AUTH);
        return;
    }

    uint32_t l_stream_id_int = 0;
    char l_stream_key[4096 + 1] = {'\0'};
        uint32_t l_remote_protocol_version;
    dap_enc_key_type_t l_enc_type = l_es->session_key_type;
        int l_enc_headers = 0;

    int l_arg_count = sscanf(l_response_str, "%u %4096s %u %d %d",
                                             &l_stream_id_int, l_stream_key,
                                             &l_remote_protocol_version,
                                             &l_enc_type, &l_enc_headers);

    if (l_arg_count < 2) {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT);
        return;
    }

    if (l_arg_count > 2)
        l_es->uplink_protocol_version = l_remote_protocol_version;
    else
        l_es->uplink_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT;

    if (l_stream_id_int) {
        if (l_es->stream_key)
            dap_enc_key_delete(l_es->stream_key);

        l_es->stream_id = l_stream_id_int;
        l_es->stream_key = dap_enc_key_new_generate(l_enc_type, l_stream_key, strlen(l_stream_key), NULL, 0, 32);
        l_es->is_encrypted_headers = l_enc_headers;

        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
                } else {
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_ERROR, ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT);
    }
}

static void s_stream_ctl_error(dap_client_t *a_client, UNUSED_ARG void *a_arg, int a_error)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    if (!l_es) return;
    dap_client_error_t l_err = (a_error == ETIMEDOUT)
        ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_STREAM_CTL_ERROR;
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_ERROR, l_err);
}

static void s_stage_stream_streaming(dap_client_t *a_client, void *a_arg)
{
    (void)a_arg;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    if (l_es)
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_COMPLETE, ERROR_NO_ERROR);
}

// ===== Stream esocket callbacks (run on worker) =====

static void s_stream_es_callback_connected(dap_events_socket_t *a_es)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es)
        return;
    // Notify FSM: connected
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_DONE, ERROR_NO_ERROR);
}

static void s_stream_es_callback_delete(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg)
{
    debug_if(s_debug_more, L_INFO, "Stream esocket delete callback");
    if (!a_es)
        return;

    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    if (!l_client) {
        a_es->_inheritor = NULL;
        return;
    }
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es) {
        a_es->_inheritor = NULL;
        return;
    }

    if (l_es->stream && l_es->stream->trans_ctx)
        l_es->stream->trans_ctx->esocket = NULL;

    // Notify FSM of stream abort
    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_ERROR, ERROR_STREAM_ABORTED);

    a_es->_inheritor = NULL; // Prevent reactor from freeing dap_client_t
}

static void s_stream_es_callback_read(dap_events_socket_t *a_es, void *arg)
{
    (void)arg;
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es) return;

    l_es->ts_last_active = dap_time_now();

    // Read stage from FSM (atomic)
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
    dap_client_stage_t l_stage = l_fsm ? (dap_client_stage_t)atomic_load(&l_fsm->stage_readable) : STAGE_UNDEFINED;

    // Delegate reading to transport
    dap_net_trans_t *l_transport = l_es->stream ? l_es->stream->trans : NULL;
    size_t l_bytes_read = 0;
    if (l_transport && l_transport->ops && l_transport->ops->read) {
        ssize_t l_ret = l_transport->ops->read(l_es->stream, NULL, 0);
        if (l_ret > 0) l_bytes_read = (size_t)l_ret;
    } else if (l_es->stream) {
        l_bytes_read = dap_stream_data_proc_read(l_es->stream);
    }

    if (l_bytes_read > 0)
        dap_events_socket_shrink_buf_in(a_es, l_bytes_read);

    switch (l_stage) {
        case STAGE_STREAM_SESSION:
        dap_client_go_stage(l_client, STAGE_STREAM_STREAMING, s_stage_stream_streaming);
        break;
    case STAGE_STREAM_CONNECTED:
        // Transition to streaming
        dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                              STAGE_STATUS_DONE, ERROR_NO_ERROR);
        if (l_es->stream)
            s_stream_es_callback_read(a_es, arg); // Process remaining data
            break;
    case STAGE_STREAM_STREAMING:
        // Normal streaming read, already processed
            break;
    default:
        break;
    }
}

static bool s_stream_es_callback_write(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es || !l_es->stream) return false;

    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
    dap_client_stage_t l_stage = l_fsm ? (dap_client_stage_t)atomic_load(&l_fsm->stage_readable) : STAGE_UNDEFINED;
    dap_client_stage_status_t l_status = l_fsm ? (dap_client_stage_status_t)atomic_load(&l_fsm->stage_status_readable) : STAGE_STATUS_NONE;

    if (l_status == STAGE_STATUS_ERROR)
        return false;

    bool l_ret = false;
    if (l_stage == STAGE_STREAM_STREAMING) {
        for (size_t i = 0; i < l_es->stream->channel_count; i++) {
            dap_stream_ch_t *ch = l_es->stream->channel[i];
                if (ch->ready_to_write && ch->proc->packet_out_callback)
                    l_ret |= ch->proc->packet_out_callback(ch, NULL);
            }
    }
    return l_ret;
}

static void s_stream_es_callback_error(dap_events_socket_t *a_es, int a_error)
{
    if (!a_es || !a_es->_inheritor)
        return;

    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_client);
    if (!l_es) return;

    log_it(L_WARNING, "STREAM error %d: \"%s\"", a_error, dap_strerror(a_error));
#ifdef DAP_OS_WINDOWS
    if (a_error == WSAETIMEDOUT || a_error == ERROR_SEM_TIMEOUT)
        a_error = ETIMEDOUT;
#endif

    dap_client_error_t l_err = (a_error == ETIMEDOUT)
        ? ERROR_NETWORK_CONNECTION_TIMEOUT : ERROR_STREAM_RESPONSE_WRONG;

    if (l_es->stream && l_es->stream->trans_ctx)
        l_es->stream->trans_ctx->esocket = NULL;

    dap_client_fsm_notify(l_es->fsm_uuid, l_es->fsm_thread_idx,
                          STAGE_STATUS_ERROR, l_err);
    a_es->_inheritor = NULL;
}
