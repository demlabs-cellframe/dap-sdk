/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2017-2020
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

#include "dap_common.h"

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <sys/types.h>          /* See NOTES */
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include "json.h"

#include "dap_enc_key.h"
#include "dap_enc_base64.h"
#include "dap_enc.h"
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_cert.h"
#include "dap_uuid.h"
#include "dap_context.h"
#include "dap_timerfd.h"
#include "dap_client_pvt.h"
#include "dap_server.h"
#include "dap_stream.h"
#include "dap_stream_worker.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_pkt.h"
#include "dap_http_client.h"

#define LOG_TAG "dap_client_pvt"

#ifndef DAP_ENC_KS_KEY_ID_SIZE
#define DAP_ENC_KS_KEY_ID_SIZE 33
#endif

static int s_max_attempts = 3;
static int s_timeout = 20;
static bool s_debug_more = false;
static time_t s_client_timeout_active_after_connect_seconds = 15;


static void s_stage_status_after(dap_client_pvt_t * a_client_internal);
static int s_add_cert_sign_to_data(const dap_cert_t *a_cert, uint8_t **a_data, size_t *a_size, const void* a_signing_data, size_t a_signing_size);
static int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...);

// ENC stage callbacks
static void s_enc_init_response(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_enc_init_error(dap_client_t *a_client, void *a_arg, int a_error);

// STREAM_CTL stage callbacks
static void s_stream_ctl_response(dap_client_t *a_client, void *a_data, size_t a_data_size);
static void s_stream_ctl_error(dap_client_t *a_client, void *a_arg, int a_error);
static void s_stage_stream_streaming(dap_client_t *a_client, void *a_arg);

// STREAM stage callbacks
static void s_request_response(void *a_response, size_t a_response_size, void * a_obj);
static void s_request_error(int a_error_code, void *a_obj);

// stream callbacks
static void s_stream_es_callback_connected(dap_events_socket_t * a_es);
static void s_stream_es_callback_delete(dap_events_socket_t * a_es, void *a_arg);
static void s_stream_es_callback_read(dap_events_socket_t * a_es, void *a_arg);
static void s_stream_es_callback_write(dap_events_socket_t * a_es, void *a_arg);
static void s_stream_es_callback_error(dap_events_socket_t * a_es, int a_error);

// Timer callbacks
static bool s_stream_timer_timeout_after_connected_check(void * a_arg);
static bool s_stream_timer_timeout_check(void * a_arg);



/**
 * @brief dap_client_internal_init
 * @return
 */
int dap_client_pvt_init()
{
    s_max_attempts = dap_config_get_item_int32_default(g_config, "dap_client", "max_tries", s_max_attempts);
    s_timeout = dap_config_get_item_int32_default(g_config, "dap_client", "timeout", s_timeout);
    s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);
    s_client_timeout_active_after_connect_seconds = (time_t) dap_config_get_item_uint32_default(g_config,
                                                  "dap_client","timeout_active_after_connect", s_client_timeout_active_after_connect_seconds);
    return 0;
}

/**
 * @brief dap_client_internal_deinit
 */
void dap_client_pvt_deinit()
{
}

/**
 * @brief dap_client_internal_new
 * @param a_client_internal
 */
void dap_client_pvt_new(dap_client_pvt_t * a_client_pvt)
{
    a_client_pvt->session_key_type = DAP_ENC_KEY_TYPE_SALSA2012 ;
    a_client_pvt->session_key_open_type = DAP_ENC_KEY_TYPE_MSRLN ;
    a_client_pvt->session_key_block_size = 32;

    a_client_pvt->stage = STAGE_BEGIN; // start point of state machine
    a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
    a_client_pvt->uplink_protocol_version = DAP_PROTOCOL_VERSION;
}


static void s_client_internal_clean(dap_client_pvt_t *a_client_pvt)
{
    if (a_client_pvt->http_client) {
        dap_client_http_close_unsafe(a_client_pvt->http_client);
        a_client_pvt->http_client = NULL;
    }
    if (a_client_pvt->stream_es) {
        dap_stream_delete_unsafe(a_client_pvt->stream);
        a_client_pvt->stream = NULL;
        a_client_pvt->stream_es = NULL;
        a_client_pvt->stream_id = 0;
        a_client_pvt->stream_key = NULL;
    }

    DAP_DEL_Z(a_client_pvt->session_key_id);
    if (a_client_pvt->session_key_open) {
        dap_enc_key_delete(a_client_pvt->session_key_open);
        a_client_pvt->session_key_open = NULL;
    }
    if (a_client_pvt->session_key) {
        dap_enc_key_delete(a_client_pvt->session_key);
        a_client_pvt->session_key = NULL;
    }

    a_client_pvt->is_closed_by_timeout = false;
    a_client_pvt->is_encrypted = false;
    a_client_pvt->is_encrypted_headers = false;
    a_client_pvt->is_close_session = false;
    a_client_pvt->remote_protocol_version = 0;
    a_client_pvt->ts_last_active = 0;

    a_client_pvt->last_error = ERROR_NO_ERROR;
    a_client_pvt->stage = STAGE_BEGIN;
    a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
}

/**
 * @brief dap_client_pvt_delete_unsafe
 * @param a_client_pvt
 */
void dap_client_pvt_delete_unsafe(dap_client_pvt_t * a_client_pvt)
{
    assert(a_client_pvt);
    debug_if(s_debug_more, L_INFO, "dap_client_pvt_delete 0x%p", a_client_pvt);
    if (a_client_pvt->reconnect_timer) {
        dap_timerfd_delete_unsafe(a_client_pvt->reconnect_timer);
        a_client_pvt->reconnect_timer = NULL;
    }
    s_client_internal_clean(a_client_pvt);
    DAP_DELETE(a_client_pvt);
}

/**
 * @brief s_stream_connected
 * @param a_client_pvt
 */
static void s_stream_connected(dap_client_pvt_t * a_client_pvt)
{
    if (!a_client_pvt->client)
        return;

    log_it(L_INFO, "[client:%p] Remote address connected for streaming on (%s:%u) with Socket #%"DAP_FORMAT_SOCKET" (assign on worker #%u)",
                            a_client_pvt->client, a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port,
                            a_client_pvt->stream_es->socket, a_client_pvt->stream_worker->worker->id);

    a_client_pvt->stage_status = STAGE_STATUS_DONE;

    s_stage_status_after(a_client_pvt);
    dap_events_socket_uuid_t * l_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
    if (!l_es_uuid_ptr) {
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }
    assert(a_client_pvt->stream_es);

    *l_es_uuid_ptr = a_client_pvt->stream_es->uuid;

    if( dap_timerfd_start_on_worker(a_client_pvt->stream_es->worker,
                                    s_client_timeout_active_after_connect_seconds * 1024,
                                    s_stream_timer_timeout_after_connected_check,
                                    l_es_uuid_ptr) == NULL) {
        log_it(L_ERROR,"Can't run timer for stream after connect check for esocket uuid %"DAP_UINT64_FORMAT_U, *l_es_uuid_ptr);
        DAP_DEL_Z(l_es_uuid_ptr);
    }
}

/**
 * @brief s_stream_timer_timeout_check
 * @param a_arg
 * @return
 */
static bool s_stream_timer_timeout_check(void * a_arg)
{
    assert(a_arg);
    if (!a_arg) {
        log_it(L_ERROR, "Invalid arguments in s_stream_timer_timeout_check");
        return false;
    }
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t*) a_arg;
    dap_worker_t *l_worker = dap_worker_get_current();
    assert(l_worker);
    if (!l_worker) {
        log_it(L_ERROR, "Invalid arguments in s_stream_timer_timeout_check");
        return false;
    }

    dap_events_socket_t *l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if(l_es){
        if (l_es->flags & DAP_SOCK_CONNECTING ){
            dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
            dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
            log_it(L_WARNING,"Connecting timeout for stream uplink request http://%s:%u/, possible network problems or host is down",
                   l_client->uplink_addr, l_client->uplink_port);
            l_client_pvt->is_closed_by_timeout = true;
            log_it(L_INFO, "Close %s sock %"DAP_FORMAT_SOCKET" type %d by timeout", l_es->remote_addr_str, l_es->socket, l_es->type);
            // Esocket wiil be removed here!
            if (l_es->callbacks.error_callback)
                l_es->callbacks.error_callback(l_es, ETIMEDOUT);
        }else
            if(s_debug_more)
                log_it(L_DEBUG,"Socket %"DAP_FORMAT_SOCKET" is connected, close check timer", l_es->socket);
    }else
        if(s_debug_more)
            log_it(L_DEBUG,"Esocket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid_ptr);

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

/**
 * @brief s_stream_timer_timeout_after_connected_check
 * @param a_arg
 * @return
 */
static bool s_stream_timer_timeout_after_connected_check(void * a_arg)
{
    assert(a_arg);
    dap_events_socket_uuid_t *l_es_uuid_ptr = (dap_events_socket_uuid_t*) a_arg;

    dap_worker_t * l_worker = dap_worker_get_current();
    assert(l_worker);

    dap_events_socket_t * l_es = dap_context_find(l_worker->context, *l_es_uuid_ptr);
    if( l_es ){
        dap_client_t *l_client = DAP_ESOCKET_CLIENT(l_es);
        dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
        if (time(NULL) - l_client_pvt->ts_last_active >= s_client_timeout_active_after_connect_seconds) {
            log_it(L_WARNING, "Activity timeout for streaming uplink http://%s:%u/, possible network problems or host is down",
                                l_client->uplink_addr, l_client->uplink_port);
            l_client_pvt->is_closed_by_timeout = true;
            if(l_es->callbacks.error_callback)
                l_es->callbacks.error_callback(l_es, ETIMEDOUT);
            log_it(L_INFO, "Close streaming socket %s (%"DAP_FORMAT_SOCKET") by timeout",
                   l_es->remote_addr_str, l_es->socket);
            dap_events_socket_remove_and_delete_unsafe(l_es, true);
        } else
            if(s_debug_more)
                log_it(L_DEBUG,"Streaming socket %"DAP_FORMAT_SOCKET" is connected, close check timer", l_es->socket);
    } else
        debug_if(s_debug_more, L_DEBUG, "Streaming socket %"DAP_UINT64_FORMAT_U" is finished, close check timer", *l_es_uuid_ptr);

    DAP_DELETE(l_es_uuid_ptr);
    return false;
}

void dap_client_pvt_queue_add(dap_client_pvt_t *a_client_pvt, const char a_ch_id, uint8_t a_type, void *a_data, size_t a_data_size)
{
    dap_client_pkt_queue_elm_t *l_pkt = DAP_NEW_SIZE(dap_client_pkt_queue_elm_t, sizeof(dap_client_pkt_queue_elm_t) + a_data_size);
    l_pkt->ch_id = a_ch_id;
    l_pkt->type = a_type;
    l_pkt->data_size = a_data_size;
    memcpy(l_pkt->data, a_data, a_data_size);
    a_client_pvt->pkt_queue = dap_list_append(a_client_pvt->pkt_queue, l_pkt);
}

int dap_client_pvt_queue_clear(dap_client_pvt_t *a_client_pvt)
{
    if (!a_client_pvt->pkt_queue)
        return -2;
    dap_list_free_full(a_client_pvt->pkt_queue, NULL);
    a_client_pvt->pkt_queue = NULL;
    return 0;
}

static bool s_timer_reconnect_callback(void *a_arg)
{
    assert(a_arg);
    ((dap_client_pvt_t *)a_arg)->reconnect_timer = NULL;
    s_stage_status_after(a_arg);
    return false;
}

/**
 * @brief s_add_cert_sign_to_data
 * @param a_cert - certificate to get sign
 * @param a_size - input/output data size
 * @param a_data - input/output data
 * @param a_signing_data - signing data
 * @param a_signing_size - signing data size
 */
int s_add_cert_sign_to_data(const dap_cert_t *a_cert, uint8_t **a_data, size_t *a_size, const void* a_signing_data, size_t a_signing_size)
{
    dap_return_val_if_pass(!a_cert || !a_size || !a_data, 0);

    dap_sign_t *l_sign = dap_sign_create(a_cert->enc_key, a_signing_data, a_signing_size, 0);
    dap_return_val_if_pass(!l_sign, 0);

    size_t l_sign_size = dap_sign_get_size(l_sign);
    *a_data = DAP_REALLOC(*a_data, (*a_size + l_sign_size) * sizeof(uint8_t));
    if (!*a_data) {
        log_it(L_CRITICAL, "Memory allocation error");
        return 0;
    }
    memcpy(*a_data + *a_size, l_sign, l_sign_size);
    *a_size += l_sign_size;
    DAP_DELETE(l_sign);
    return 1;
}

/**
 * @brief s_client_internal_stage_status_proc
 * @param a_client
 */
static void s_stage_status_after(dap_client_pvt_t *a_client_pvt)
{
    if (!a_client_pvt)
        return;
    dap_worker_t * l_worker= a_client_pvt->worker;
    assert(l_worker);
    assert(l_worker->_inheritor);
    //bool l_is_unref = false;
    dap_client_stage_status_t l_stage_status = a_client_pvt->stage_status;
    dap_client_stage_t l_stage = a_client_pvt->stage;

    switch (l_stage_status) {
        case STAGE_STATUS_IN_PROGRESS: {
            switch (l_stage) {
                case STAGE_BEGIN:
                    s_client_internal_clean(a_client_pvt);
                    a_client_pvt->reconnect_attempts = 0;
                    s_stage_status_after(a_client_pvt);
                    return;

                case STAGE_ENC_INIT: {
                    log_it(L_INFO, "Go to stage ENC: prepare the request");

                    if (!a_client_pvt->client->uplink_addr || !a_client_pvt->client->uplink_addr[0] || !a_client_pvt->client->uplink_port) {
                        log_it(L_ERROR, "Wrong remote address %s : %u", a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_WRONG_ADDRESS;
                        break;
                    }

                    if (a_client_pvt->session_key_open)
                        dap_enc_key_delete(a_client_pvt->session_key_open);
                    a_client_pvt->session_key_open = dap_enc_key_new_generate(a_client_pvt->session_key_open_type, NULL, 0, NULL, 0,
                                                                              a_client_pvt->session_key_block_size);
                    if (!a_client_pvt->session_key_open) {
                        log_it(L_ERROR, "Insufficient memory! May be a huge memory leak present");
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_OUT_OF_MEMORY;
                        break;
                    }
                    size_t l_data_size = a_client_pvt->session_key_open->pub_key_data_size;
                    uint8_t *l_data = DAP_NEW_Z_SIZE(uint8_t, l_data_size);
                    memcpy(l_data, a_client_pvt->session_key_open->pub_key_data, a_client_pvt->session_key_open->pub_key_data_size);

                    dap_cert_t *l_node_cert = dap_cert_find_by_name("node-addr");
                    size_t l_sign_count = s_add_cert_sign_to_data(a_client_pvt->client->auth_cert, &l_data, &l_data_size, a_client_pvt->session_key_open->pub_key_data, a_client_pvt->session_key_open->pub_key_data_size) +
                                            s_add_cert_sign_to_data(l_node_cert, &l_data, &l_data_size, a_client_pvt->session_key_open->pub_key_data, a_client_pvt->session_key_open->pub_key_data_size);
                

                    size_t l_data_str_size_max = DAP_ENC_BASE64_ENCODE_SIZE(l_data_size);
                    char *l_data_str = DAP_NEW_Z_SIZE(char, l_data_str_size_max + 1);
                    // DAP_ENC_DATA_TYPE_B64_URLSAFE not need because send it by POST request
                    size_t l_data_str_enc_size = dap_enc_base64_encode(l_data, l_data_size, l_data_str, DAP_ENC_DATA_TYPE_B64);

                    debug_if(s_debug_more, L_DEBUG, "ENC request size %zu", l_data_str_enc_size);

                    char l_enc_init_url[1024] = { '\0' };
                    snprintf(l_enc_init_url, sizeof(l_enc_init_url), DAP_UPLINK_PATH_ENC_INIT
                                 "/gd4y5yh78w42aaagh" "?enc_type=%d,pkey_exchange_type=%d,pkey_exchange_size=%zd,block_key_size=%zd,protocol_version=%d,sign_count=%zu",
                                 a_client_pvt->session_key_type, a_client_pvt->session_key_open_type, a_client_pvt->session_key_open->pub_key_data_size,
                                 a_client_pvt->session_key_block_size,  DAP_CLIENT_PROTOCOL_VERSION, l_sign_count);
                    int l_res = dap_client_pvt_request(a_client_pvt, l_enc_init_url,
                            l_data_str, l_data_str_enc_size, s_enc_init_response, s_enc_init_error);
                    // bad request
                    if (l_res < 0)
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                    DAP_DEL_Z(l_data);
                    DAP_DEL_Z(l_data_str);
                } break;

                case STAGE_STREAM_CTL: {
                    log_it(L_INFO, "Go to stage STREAM_CTL: prepare the request");

                    char l_request[16];
                    size_t l_request_size;

                    l_request_size = snprintf(l_request, sizeof(l_request), "%d",  DAP_CLIENT_PROTOCOL_VERSION);
 
                    debug_if(s_debug_more, L_DEBUG, "STREAM_CTL request size %zu", l_request_size);

                    char *l_suburl;

                    uint32_t l_least_common_dap_protocol = min(a_client_pvt->remote_protocol_version,
                                                               a_client_pvt->uplink_protocol_version);

                    if(l_least_common_dap_protocol < 23){
                        l_suburl = dap_strdup_printf("stream_ctl,channels=%s",
                                                     a_client_pvt->client->active_channels);
                    } else {
                        l_suburl = dap_strdup_printf("channels=%s,enc_type=%d,enc_key_size=%zu,enc_headers=%d",
                                                     a_client_pvt->client->active_channels, a_client_pvt->session_key_type,
                                                     a_client_pvt->session_key_block_size, 0);
                    }
                    debug_if(s_debug_more, L_DEBUG, "Prepared enc request for streaming");
                    dap_client_pvt_request_enc(a_client_pvt, DAP_UPLINK_PATH_STREAM_CTL,
                            l_suburl, "type=tcp,maxconn=4", l_request, l_request_size,
                            s_stream_ctl_response, s_stream_ctl_error);
                    log_it(L_DEBUG, "Sent enc request for streaming");

                    DAP_DELETE(l_suburl);
                } break;

                case STAGE_STREAM_SESSION: {
                    log_it(L_INFO, "Go to stage STREAM_SESSION: process the state ops");

                    SOCKET l_stream_socket = socket(PF_INET, SOCK_STREAM, 0);
                    if (l_stream_socket == INVALID_SOCKET) {
#ifdef DAP_OS_WINDOWS
                        log_it(L_ERROR, "Socket create error %d", WSAGetLastError());
#else
                        log_it(L_ERROR, "Error %d with socket create", errno);
#endif
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        break;
                    }
#ifdef DAP_OS_WINDOWS
                    u_long l_socket_flags = 1;
                    if (ioctlsocket(l_stream_socket, (long)FIONBIO, &l_socket_flags) == SOCKET_ERROR) {
                        log_it(L_ERROR, "Can't set socket %zu to nonblocking mode, error %d", l_stream_socket, WSAGetLastError());
                    }
#else
                    // Get socket flags
                    int l_socket_flags = fcntl(l_stream_socket, F_GETFL);
                    if (l_socket_flags == -1){
                        log_it(L_ERROR, "Error %d can't get socket flags", errno);
                        break;;
                    }
                    // Make it non-block
                    if (fcntl( l_stream_socket, F_SETFL,l_socket_flags| O_NONBLOCK) == -1){
                        log_it(L_ERROR, "Error %d can't get socket flags", errno);
                        break;
                    }
#endif

                    // Wrap socket and setup callbacks
                    static dap_events_socket_callbacks_t l_s_callbacks = {
                        .read_callback = s_stream_es_callback_read,
                        .write_callback = s_stream_es_callback_write,
                        .error_callback = s_stream_es_callback_error,
                        .delete_callback = s_stream_es_callback_delete,
                        .connected_callback = s_stream_es_callback_connected
                    };//
                    a_client_pvt->stream_es = dap_events_socket_wrap_no_add( l_stream_socket,
                                                                             &l_s_callbacks);
                    a_client_pvt->stream_es->flags |= DAP_SOCK_CONNECTING ; // To catch non-blocking error when connecting we should up WRITE flag
                    a_client_pvt->stream_es->flags |= DAP_SOCK_READY_TO_WRITE;
                    a_client_pvt->stream_es->_inheritor = a_client_pvt->client;
                    a_client_pvt->stream = dap_stream_new_es_client(a_client_pvt->stream_es);
                    assert(a_client_pvt->stream);
                    a_client_pvt->stream->session = dap_stream_session_pure_new(); // may be from in packet?

                    // new added, whether it is necessary?
                    a_client_pvt->stream->session->key = a_client_pvt->stream_key;
                    a_client_pvt->stream_worker = DAP_STREAM_WORKER(l_worker);
                    a_client_pvt->stream->stream_worker = a_client_pvt->stream_worker;

                    // connect
                    memset(&a_client_pvt->stream_es->remote_addr, 0, sizeof(a_client_pvt->stream_es->remote_addr));
                    a_client_pvt->stream_es->remote_addr.sin_family = AF_INET;
                    a_client_pvt->stream_es->remote_addr.sin_port = htons(a_client_pvt->client->uplink_port);
                    if(inet_pton(AF_INET, a_client_pvt->client->uplink_addr, &(a_client_pvt->stream_es->remote_addr.sin_addr)) < 0) {
                        log_it(L_ERROR, "Wrong remote address '%s:%u'", a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_WRONG_ADDRESS;
                    } else {
                        int l_err = 0;
                        strncpy(a_client_pvt->stream_es->remote_addr_str, a_client_pvt->client->uplink_addr, INET_ADDRSTRLEN);

                        if((l_err = connect(a_client_pvt->stream_es->socket, (struct sockaddr *) &a_client_pvt->stream_es->remote_addr,
                                sizeof(struct sockaddr_in))) ==0) {
                            log_it(L_INFO, "Connected momentaly with %s:%u", a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
                            // add to dap_worker
                            dap_worker_add_events_socket(l_worker, a_client_pvt->stream_es);

                            // Add check timer
                            assert(a_client_pvt->stream_es);
                            dap_events_socket_uuid_t * l_stream_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
                            if (!l_stream_es_uuid_ptr) {
                                log_it(L_CRITICAL, "Memory allocation error");
                                a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                                a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                                s_stage_status_after(a_client_pvt);
                                return;
                            }
                            *l_stream_es_uuid_ptr  = a_client_pvt->stream_es->uuid;
                            dap_stream_change_id(a_client_pvt->session_key, a_client_pvt->stream_id);  // change id in hash tab
                            dap_timerfd_start_on_worker(a_client_pvt->worker, (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                                        s_stream_timer_timeout_check,l_stream_es_uuid_ptr);
                        }
                        else if (l_err != EINPROGRESS && l_err != -1){
                            char l_errbuf[128] = {0};

                            if (l_err)
                                strerror_r(l_err,l_errbuf,sizeof (l_errbuf));
                            else
                                strncpy(l_errbuf,"Unknown Error",sizeof(l_errbuf)-1);
                            log_it(L_ERROR, "Remote address can't connect (%s:%hu) with sock_id %"DAP_FORMAT_SOCKET": \"%s\" (code %d)",
                                                a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port,
                                                a_client_pvt->stream_es->socket, l_errbuf, l_err);
                            dap_events_socket_delete_unsafe(a_client_pvt->stream_es, true);
                            a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                            a_client_pvt->last_error = ERROR_STREAM_CONNECT;
                        } else {
                            log_it(L_INFO, "Connecting stream to remote %s:%u", a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
                            // add to dap_worker
                            assert (a_client_pvt->stream_es);
                            dap_worker_add_events_socket(l_worker, a_client_pvt->stream_es);
                            dap_events_socket_uuid_t * l_stream_es_uuid_ptr = DAP_NEW_Z(dap_events_socket_uuid_t);
                            if (!l_stream_es_uuid_ptr) {
                                log_it(L_CRITICAL, "Memory allocation error");
                                a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                                a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                                s_stage_status_after(a_client_pvt);
                                return;
                            }
                            *l_stream_es_uuid_ptr = a_client_pvt->stream_es->uuid;
                            dap_stream_change_id(a_client_pvt->session_key, a_client_pvt->stream_id);  // change id in hash tab
                            dap_timerfd_start_on_worker(a_client_pvt->worker, (unsigned long)s_client_timeout_active_after_connect_seconds * 1000,
                                                        s_stream_timer_timeout_check,l_stream_es_uuid_ptr);
                        }
                    }
                    if (a_client_pvt->stage_status == STAGE_STATUS_ERROR)
                        s_stage_status_after(a_client_pvt);
                } break;

                case STAGE_STREAM_CONNECTED: {
                    log_it(L_INFO, "Go to stage STAGE_STREAM_CONNECTED");
                    if(!a_client_pvt->stream){
                        a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                        a_client_pvt->last_error = ERROR_STREAM_ABORTED;
                        dap_stream_delete_prep_addr(a_client_pvt->stream_id, NULL);
                        s_stage_status_after(a_client_pvt);
                        return;
                    }

                    size_t l_count_channels = dap_strlen(a_client_pvt->client->active_channels);
                    for(size_t i = 0; i < l_count_channels; i++)
                        dap_stream_ch_new(a_client_pvt->stream, (uint8_t)a_client_pvt->client->active_channels[i]);

                    char l_full_path[2048];
                    snprintf(l_full_path, sizeof(l_full_path) - 1, "%s/globaldb?session_id=%u", DAP_UPLINK_PATH_STREAM,
                                                dap_client_get_stream_id(a_client_pvt->client));

                    dap_events_socket_write_f_unsafe( a_client_pvt->stream_es, "GET /%s HTTP/1.1\r\n"
                                                                        "Host: %s:%d\r\n"
                                                                        "\r\n",
                                               l_full_path, a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);

                    a_client_pvt->stage_status = STAGE_STATUS_DONE;
                    s_stage_status_after(a_client_pvt);
                } break;

                case STAGE_STREAM_STREAMING: {
                    log_it(L_INFO, "Go to stage STAGE_STREAM_STREAMING");
                    a_client_pvt->reconnect_attempts = 0;

                    a_client_pvt->stage_status = STAGE_STATUS_DONE;
                    dap_stream_add_stream_info(a_client_pvt->stream, a_client_pvt->stream_id);
                    s_stage_status_after(a_client_pvt);

                } break;

                default: {
                    log_it(L_ERROR, "Undefined proccessing actions for stage status %s",
                            dap_client_stage_status_str(a_client_pvt->stage_status));
                    a_client_pvt->stage_status = STAGE_STATUS_ERROR;
                    s_stage_status_after(a_client_pvt); // be carefull to not to loop!
                }
            }
        } break;

        case STAGE_STATUS_ERROR: {
            // limit the number of attempts
            bool l_is_last_attempt = ++a_client_pvt->reconnect_attempts > s_max_attempts ? true : false;

            log_it(L_ERROR, "Error state(%s), doing callback if present", dap_client_error_str(a_client_pvt->last_error));
            if(a_client_pvt->stage_status_error_callback)
                a_client_pvt->stage_status_error_callback(a_client_pvt->client, (void *)l_is_last_attempt);

            if(!l_is_last_attempt ) {
                // Trying the step again
                a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                log_it(L_INFO, "Reconnect attempt %d in 0.3 seconds with %s:%u", a_client_pvt->reconnect_attempts,
                       a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
                // small delay before next request
                a_client_pvt->reconnect_timer = dap_timerfd_start_on_worker(
                            a_client_pvt->worker, 300, s_timer_reconnect_callback, a_client_pvt);
                if (!a_client_pvt->reconnect_timer)
                    log_it(L_ERROR ,"Can't run timer for small delay before the next enc_init request");
            } else {
                if (a_client_pvt->client->always_reconnect) {
                    log_it(L_INFO, "Too many attempts, reconnect attempt in %d seconds with %s:%u", s_timeout,
                           a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);                    // Trying the step again
                    a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                    a_client_pvt->reconnect_attempts = 0;
                    // bigger delay before next request
                    a_client_pvt->reconnect_timer = dap_timerfd_start_on_worker(
                                a_client_pvt->worker, s_timeout * 1000, s_timer_reconnect_callback, a_client_pvt);
                    if (!a_client_pvt->reconnect_timer)
                        log_it(L_ERROR,"Can't run timer for bigger delay before the next enc_init request");
                } else
                    log_it(L_ERROR, "Connect to %s:%u failed", a_client_pvt->client->uplink_addr, a_client_pvt->client->uplink_port);
            }
            if (a_client_pvt->stage_status != STAGE_STATUS_ERROR) {
                s_client_internal_clean(a_client_pvt);
                a_client_pvt->stage_status = STAGE_STATUS_IN_PROGRESS;
                a_client_pvt->stage = STAGE_ENC_INIT;
            }
        } break;

        case STAGE_STATUS_DONE: {
            log_it(L_INFO, "Stage status %s is done", dap_client_stage_str(a_client_pvt->stage));
            bool l_is_last_stage = (a_client_pvt->stage == a_client_pvt->client->stage_target);
            if (l_is_last_stage) {
                a_client_pvt->stage_status = STAGE_STATUS_COMPLETE;
                if (a_client_pvt->client->stage_target_done_callback) {
                    log_it(L_NOTICE, "Stage %s is achieved", dap_client_stage_str(a_client_pvt->stage));
                    a_client_pvt->client->stage_target_done_callback(a_client_pvt->client, NULL);
                }
                if (a_client_pvt->stage == STAGE_STREAM_STREAMING) {
                    // Send all pkts in queue
                    for (dap_list_t *it = a_client_pvt->pkt_queue; it; it = it->next) {
                        dap_client_pkt_queue_elm_t *l_pkt = it->data;
                        dap_client_write_unsafe(a_client_pvt->client, l_pkt->ch_id, l_pkt->type, l_pkt->data, l_pkt->data_size);
                    }
                    dap_list_free_full(a_client_pvt->pkt_queue, NULL);
                    a_client_pvt->pkt_queue = NULL;
                }
            } else if (a_client_pvt->stage_status_done_callback) {
                // go to next stage
                a_client_pvt->stage_status_done_callback(a_client_pvt->client, NULL);
            }
        } break;

        case STAGE_STATUS_COMPLETE:
            break;

        default:
            log_it(L_ERROR, "Undefined proccessing actions for stage status %s",
                    dap_client_stage_status_str(a_client_pvt->stage_status));
    }

    return;
}


/**
 * @brief dap_client_internal_stage_transaction_begin
 * @param a_client_internal
 * @param a_stage_next
 * @param a_done_callback
 */
void dap_client_pvt_stage_transaction_begin(dap_client_pvt_t * a_client_internal, dap_client_stage_t a_stage_next,
        dap_client_callback_t a_done_callback)
{
    assert(a_client_internal);
    debug_if(s_debug_more, L_DEBUG, "Begin transaction for client %p to the next stage %s",
             a_client_internal->client, dap_client_stage_str(a_stage_next));

    a_client_internal->stage_status_done_callback = a_done_callback;
    a_client_internal->stage = a_stage_next;
    a_client_internal->stage_status = STAGE_STATUS_IN_PROGRESS;
    s_stage_status_after(a_client_internal);
}

/**
 * @brief dap_client_internal_request
 * @param a_client_internal
 * @param a_path
 * @param a_request
 * @param a_request_size
 * @param a_response_proc
 */
int dap_client_pvt_request(dap_client_pvt_t * a_client_internal, const char * a_path, void * a_request,
        size_t a_request_size, dap_client_callback_data_size_t a_response_proc,
        dap_client_callback_int_t a_response_error)
{
    a_client_internal->request_response_callback = a_response_proc;
    a_client_internal->request_error_callback = a_response_error;
    a_client_internal->is_encrypted = false;
    a_client_internal->http_client = dap_client_http_request(a_client_internal->worker, a_client_internal->client->uplink_addr,
                                            a_client_internal->client->uplink_port,
                                            a_request ? "POST" : "GET", "text/text", a_path, a_request,
                                            a_request_size, NULL, s_request_response, s_request_error, a_client_internal, NULL);
    return a_client_internal->http_client == NULL;
}

/**
 * @brief dap_client_internal_request_enc
 * @param a_client_internal
 * @param a_path
 * @param a_sub_url
 * @param a_query
 * @param a_request
 * @param a_request_size
 * @param a_response_proc
 * @param a_response_error
 */
void dap_client_pvt_request_enc(dap_client_pvt_t * a_client_internal, const char * a_path,
        const char * a_sub_url, const char * a_query
        , void * a_request, size_t a_request_size
        , dap_client_callback_data_size_t a_response_proc
        , dap_client_callback_int_t a_response_error)
{
    bool is_query_enc = true; // if true, then encode a_query string  [Why do we even need this?]

    debug_if(s_debug_more, L_DEBUG, "Encrypted request: sub_url '%s' query '%s'", a_sub_url ? a_sub_url : "NULL",
            a_query ? a_query : "NULL");

    size_t l_sub_url_size = a_sub_url ? strlen(a_sub_url) : 0;
    size_t l_query_size = a_query ? strlen(a_query) : 0;
//    size_t l_url_size;
//    char l_url[1024] = { 0 };
//    snprintf(l_url, 1024, "http://%s:%u", a_client_internal->uplink_addr, a_client_internal->uplink_port);
//    l_url_size = strlen(l_url);

    size_t l_sub_url_enc_size_max = l_sub_url_size ? (5 * l_sub_url_size + 16) : 0;
    char *l_sub_url_enc = l_sub_url_size ? DAP_NEW_Z_SIZE(char, l_sub_url_enc_size_max + 1) : NULL;

    size_t l_query_enc_size_max = (is_query_enc) ? (l_query_size * 5 + 16) : l_query_size;
    char *l_query_enc =
            (is_query_enc) ? (l_query_size ? DAP_NEW_Z_SIZE(char, l_query_enc_size_max + 1) : NULL) : (char*) a_query;

//    size_t l_url_full_size_max = 5 * l_sub_url_size + 5 * l_query_size + 16 + l_url_size + 2;
//    char * l_url_full = DAP_NEW_Z_SIZE(char, l_url_full_size_max + 1);

    size_t l_request_enc_size_max = a_request_size ? a_request_size * 2 + 16 : 0;
    char * l_request_enc = a_request_size ? DAP_NEW_Z_SIZE(char, l_request_enc_size_max + 1) : NULL;
    size_t l_request_enc_size = 0;

    a_client_internal->request_response_callback = a_response_proc;
    a_client_internal->request_error_callback = a_response_error;
    a_client_internal->is_encrypted = true;
    dap_enc_data_type_t l_enc_type;

    if(a_client_internal->uplink_protocol_version >= 21)
        l_enc_type = DAP_ENC_DATA_TYPE_B64_URLSAFE;
    else
        l_enc_type = DAP_ENC_DATA_TYPE_B64;

    if(l_sub_url_size)
        dap_enc_code(a_client_internal->session_key,
                a_sub_url, l_sub_url_size,
                l_sub_url_enc, l_sub_url_enc_size_max,
                l_enc_type);

    if(is_query_enc && l_query_size)
        dap_enc_code(a_client_internal->session_key,
                a_query, l_query_size,
                l_query_enc, l_query_enc_size_max,
                l_enc_type);

    if(a_request_size)
        l_request_enc_size = dap_enc_code(a_client_internal->session_key,
                a_request, a_request_size,
                l_request_enc, l_request_enc_size_max,
                DAP_ENC_DATA_TYPE_RAW);

    size_t l_path_size= l_query_enc_size_max + l_sub_url_enc_size_max + 1;
    char *l_path = DAP_NEW_Z_SIZE(char, l_path_size);
    l_path[0] = '\0';
    if(a_path) {
        if(l_sub_url_size){
            if(l_query_size){
                snprintf(l_path, l_path_size, "%s/%s?%s", a_path?a_path:"",
                             l_sub_url_enc?l_sub_url_enc:"",
                                   l_query_enc?l_query_enc:"");
            }else{
                snprintf(l_path, l_path_size, "%s/%s", a_path, l_sub_url_enc);
            }
        } else {
            dap_stpcpy(l_path, a_path);
        }
    }

    size_t l_size_required = a_client_internal->session_key_id ? strlen(a_client_internal->session_key_id) + 40 : 40;
    char *l_custom = DAP_NEW_Z_SIZE(char, l_size_required);
    size_t l_off = snprintf(l_custom, l_size_required, "KeyID: %s\r\n", a_client_internal->session_key_id ? a_client_internal->session_key_id : "NULL");
    if (a_client_internal->is_close_session)
        snprintf(l_custom + l_off, l_size_required - l_off, "%s\r\n", "SessionCloseAfterRequest: true");

    a_client_internal->http_client = dap_client_http_request(a_client_internal->worker, a_client_internal->client->uplink_addr,
                            a_client_internal->client->uplink_port,
                            a_request ? "POST" : "GET", "text/text",
                            l_path, l_request_enc, l_request_enc_size, NULL,
                            s_request_response, s_request_error, a_client_internal, l_custom);
    DAP_DEL_Z(l_sub_url_enc);
    DAP_DEL_Z(l_custom);
    DAP_DEL_Z(l_query_enc);
    DAP_DEL_Z(l_path);
    DAP_DEL_Z(l_request_enc);
}

/**
 * @brief s_request_error
 * @param a_err_code
 * @param a_obj
 */
static void s_request_error(int a_err_code, void * a_obj)
{
    if (a_obj == NULL) {
        log_it(L_ERROR,"Object is NULL for s_request_error");
        return;
    }
    dap_client_pvt_t * l_client_pvt = (dap_client_pvt_t *) a_obj;
    assert(l_client_pvt);
    l_client_pvt->http_client = NULL;
    if (l_client_pvt && l_client_pvt->request_error_callback)
          l_client_pvt->request_error_callback(l_client_pvt->client, l_client_pvt->callback_arg, a_err_code);
}

/**
 * @brief s_request_response
 * @param a_response
 * @param a_response_size
 * @param a_obj
 */
static void s_request_response(void * a_response, size_t a_response_size, void * a_obj)
{
    dap_client_pvt_t * l_client_pvt = (dap_client_pvt_t *) a_obj;
    assert(l_client_pvt);
    l_client_pvt->http_client = NULL;
    if (l_client_pvt->is_encrypted && l_client_pvt->session_key) {
        size_t l_response_dec_size_max = a_response_size ? a_response_size * 2 + 16 : 0;
        char * l_response_dec = a_response_size ? DAP_NEW_Z_SIZE(char, l_response_dec_size_max) : NULL;
        size_t l_response_dec_size = 0;
        if(a_response_size)
            l_response_dec_size = dap_enc_decode(l_client_pvt->session_key,
                    a_response, a_response_size,
                    l_response_dec, l_response_dec_size_max,
                    DAP_ENC_DATA_TYPE_RAW);

        if ( l_client_pvt->request_response_callback )
            l_client_pvt->request_response_callback(l_client_pvt->client, l_response_dec, l_response_dec_size);
        else
            log_it(L_ERROR, "NULL request_response_callback for encrypted client %p", l_client_pvt->client );

        if(l_response_dec)
            DAP_DELETE(l_response_dec);
    } else {
        if ( l_client_pvt->request_response_callback )
            l_client_pvt->request_response_callback(l_client_pvt->client, a_response, a_response_size);
        else
            log_it(L_ERROR, "NULL request_response_callback for unencrypted  client %p", l_client_pvt->client );
    }
}

/**
 * @brief s_json_str_multy_obj_parse - check a_key and copy a_val to args. Args count should be even.
 * @param a_key - compare key
 * @param a_val - coping value
 * @param a_count - args count
 * @return count of success copies
 */
int s_json_multy_obj_parse_str(const char *a_key, const char *a_val, int a_count, ...)
{
    dap_return_val_if_pass(!a_key || !a_val || a_count % 2, 0);
    int l_ret = 0;
    va_list l_args;
    va_start(l_args, a_count);
    for (int i = 0; i < a_count / 2; ++i) {
        const char *l_key = va_arg(l_args, const char *);
        char **l_pointer = va_arg(l_args, char **);
        if(!strcmp(a_key, l_key)) {
            DAP_DEL_Z(*l_pointer);
            *l_pointer = dap_strdup(a_val);
            l_ret++;
        }
    }
    va_end(l_args);
    return l_ret;
}

/**
 * @brief s_enc_init_response
 * @param a_client
 * @param a_response
 * @param a_response_size
 */
static void s_enc_init_response(dap_client_t *a_client, void * a_data, size_t a_data_size)
{
    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);

    l_client_pvt->last_error = ERROR_NO_ERROR;

    if (!l_client_pvt->session_key_open){
        log_it(L_ERROR, "m_enc_init_response: session is NULL!");
        l_client_pvt->last_error = ERROR_ENC_SESSION_CLOSED ;
    }

    if (l_client_pvt->last_error == ERROR_NO_ERROR) {
        if (!a_data_size) {
            log_it(L_ERROR, "ENC: Wrong response (size %zu data '%s')", a_data_size, (char* ) a_data);
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
        } else if (a_data_size <= 10) {
            log_it(L_ERROR, "ENC: Wrong response (size %zu)", a_data_size);
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
        }
    }
    if (l_client_pvt->last_error == ERROR_NO_ERROR) {

        char *l_session_id_b64 = NULL;
        char *l_bob_message_b64 = NULL;
        char *l_node_sign_b64 = NULL;
        int l_json_parse_count = 0;
        struct json_object *jobj = json_tokener_parse((const char *) a_data);
        if(jobj) {
            // parse encrypt_id & encrypt_msg
            json_object_object_foreach(jobj, key, val)
            {
                if(json_object_get_type(val) == json_type_string) {
                    const char *l_str = json_object_get_string(val);
                    l_json_parse_count += s_json_multy_obj_parse_str( key, l_str, 6,
                                            "encrypt_id", &l_session_id_b64, 
                                            "encrypt_msg",  &l_bob_message_b64,
                                            "node_sign", &l_node_sign_b64);
                }
                if(json_object_get_type(val) == json_type_int) {
                    int val_int = json_object_get_int(val);
                    if(!strcmp(key, "dap_protocol_version")) {
                        l_client_pvt->remote_protocol_version = val_int;
                        l_json_parse_count++;
                    }
                }
            }
            // free jobj
            json_object_put(jobj);
            if(!l_client_pvt->remote_protocol_version)
                l_client_pvt->remote_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT;
        }
        if (l_json_parse_count < 2 || l_json_parse_count > 4) {
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            log_it(L_ERROR, "ENC: Wrong response (size %zu data '%s')", a_data_size, (char* ) a_data);
        }
        if (!l_session_id_b64) {
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            log_it(L_WARNING,"ENC: no session id in base64");
        }

        if (l_client_pvt->last_error == ERROR_NO_ERROR) {
            l_client_pvt->session_key_id = DAP_NEW_Z_SIZE(char, strlen(l_session_id_b64) + 1);
            size_t l_rc = dap_enc_base64_decode(l_session_id_b64, strlen(l_session_id_b64),
                    l_client_pvt->session_key_id, DAP_ENC_DATA_TYPE_B64);
            if (!l_rc) {
                log_it(L_WARNING, "ENC: Can't decode session id from base64");
                l_client_pvt->last_error = ERROR_ENC_WRONG_KEY;
            } else
                log_it(L_DEBUG, "ENC: session Key ID %s", l_client_pvt->session_key_id);
        }
        DAP_DEL_Z(l_session_id_b64);
        if(!l_bob_message_b64){
            l_client_pvt->last_error = ERROR_ENC_NO_KEY;
            log_it(L_WARNING,"ENC: no bob message in base64");
        }
        char *l_bob_message = NULL;
        size_t l_bob_message_size = 0;
        if (l_client_pvt->last_error == ERROR_NO_ERROR) {
            l_bob_message = DAP_NEW_Z_SIZE(char, strlen(l_bob_message_b64) + 1);
            l_bob_message_size = dap_enc_base64_decode(l_bob_message_b64, strlen(l_bob_message_b64),
                    l_bob_message, DAP_ENC_DATA_TYPE_B64);
            if (!l_bob_message_size) {
                log_it(L_WARNING, "ENC: Can't decode bob message from base64");
                l_client_pvt->last_error = ERROR_ENC_WRONG_KEY;
            }
        }
        DAP_DEL_Z(l_bob_message_b64);

        if (l_client_pvt->last_error == ERROR_NO_ERROR) {
            size_t l_rc = l_client_pvt->session_key_open->gen_alice_shared_key(
                    l_client_pvt->session_key_open, l_client_pvt->session_key_open->priv_key_data,
                    l_bob_message_size, (unsigned char*) l_bob_message);
            if (!l_rc) {
                log_it(L_WARNING, "ENC: Can't generate private key from bob message");
                l_client_pvt->last_error = ERROR_ENC_WRONG_KEY;
            }
        }
        if (l_client_pvt->last_error == ERROR_NO_ERROR) {
            l_client_pvt->session_key = dap_enc_key_new_generate(l_client_pvt->session_key_type,
                    l_client_pvt->session_key_open->priv_key_data, // shared key
                    l_client_pvt->session_key_open->priv_key_data_size,
                    l_client_pvt->session_key_id, strlen(l_client_pvt->session_key_id), l_client_pvt->session_key_block_size);
        }
        if (l_client_pvt->last_error == ERROR_NO_ERROR &&
                    l_client_pvt->stage != STAGE_ENC_INIT) { // We are in wrong stage
            l_client_pvt->last_error = ERROR_WRONG_STAGE;
            log_it(L_WARNING, "ENC: initialized encryption but current stage is %s (%s)",
                    dap_client_get_stage_str(a_client), dap_client_get_stage_status_str(a_client));
        }
        if (l_client_pvt->last_error == ERROR_NO_ERROR && l_node_sign_b64) {
            dap_sign_t *l_sign = (dap_sign_t *)DAP_NEW_Z_SIZE(uint8_t, strlen(l_node_sign_b64) + 1);
            if (!l_sign){
                log_it(L_CRITICAL, "Memory allocation error");
            }
            size_t l_decode_len = dap_enc_base64_decode(l_node_sign_b64, strlen(l_node_sign_b64), l_sign, DAP_ENC_DATA_TYPE_B64);
            if (!dap_sign_verify_all(l_sign, l_decode_len, l_bob_message, l_bob_message_size)) {
                dap_stream_add_addr(dap_stream_node_addr_from_sign(l_sign), l_client_pvt->session_key);
            } else {
                log_it(L_WARNING, "ENC: Invalid node sign");
            }
        }
        DAP_DEL_Z(l_node_sign_b64);
        DAP_DEL_Z(l_bob_message);
    }
    if (l_client_pvt->last_error == ERROR_NO_ERROR)
        l_client_pvt->stage_status = STAGE_STATUS_DONE;
    else {
        DAP_DEL_Z(l_client_pvt->session_key_id);
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    }
    dap_enc_key_delete(l_client_pvt->session_key_open);
    l_client_pvt->session_key_open = NULL;
    s_stage_status_after(l_client_pvt);
}

/**
 * @brief s_enc_init_error
 * @param a_client
 * @param a_err_code
 */
static void s_enc_init_error(dap_client_t * a_client, UNUSED_ARG void *a_arg, int a_err_code)
{
    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);
    log_it(L_ERROR, "ENC: Can't init ecnryption session, err code %d", a_err_code);
    if (!l_client_pvt) return;
    if (a_err_code == ETIMEDOUT) {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
    } else {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_REFUSE;
    }
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    s_stage_status_after(l_client_pvt);
}

/**
 * @brief s_stream_ctl_response
 * @param a_client
 * @param a_data
 * @param a_data_size
 */
static void s_stream_ctl_response(dap_client_t * a_client, void * a_data, size_t a_data_size)
{
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (!l_client_pvt) return;
    if(s_debug_more)
        log_it(L_DEBUG, "STREAM_CTL response %zu bytes length recieved", a_data_size);
    char * l_response_str = DAP_NEW_Z_SIZE(char, a_data_size + 1);
    if (!l_response_str) {
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }
    memcpy(l_response_str, a_data, (uint32_t)a_data_size);

    if(a_data_size < 4) {
        log_it(L_ERROR, "STREAM_CTL Wrong reply: '%s'", l_response_str);
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        s_stage_status_after(l_client_pvt);
    } else if(strcmp(l_response_str, "ERROR") == 0) {
        log_it(L_WARNING, "STREAM_CTL Got ERROR from the remote site,expecting thats ERROR_AUTH");
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_AUTH;
        l_client_pvt->stage_status = STAGE_STATUS_ERROR;
        s_stage_status_after(l_client_pvt);
    } else {
        int l_arg_count;
        char *l_stream_key = DAP_NEW_Z_SIZE(char, 4096 * 3);
        if (!l_stream_key) {
            log_it(L_CRITICAL, "Memory allocation error");
            DAP_DEL_Z(l_response_str);
            return;
        }
        uint32_t l_remote_protocol_version;
        dap_enc_key_type_t l_enc_type = l_client_pvt->session_key_type;
        int l_enc_headers = 0;
        uint32_t l_stream_id_int = 0;

        l_arg_count = sscanf(l_response_str, "%u %4096s %u %d %d"
                , &l_stream_id_int, l_stream_key, &l_remote_protocol_version, &l_enc_type, &l_enc_headers);
        if(l_arg_count < 2) {
            log_it(L_WARNING, "STREAM_CTL Need at least 2 arguments in reply (got %d)", l_arg_count);
            l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
            l_client_pvt->stage_status = STAGE_STATUS_ERROR;
            s_stage_status_after(l_client_pvt);
        } else {

            if(l_arg_count > 2) {
                l_client_pvt->uplink_protocol_version = l_remote_protocol_version;
                log_it(L_DEBUG, "Uplink protocol version %u", l_remote_protocol_version);
            } else
                log_it(L_WARNING, "No uplink protocol version, use default version %d"
                        , l_client_pvt->uplink_protocol_version = DAP_PROTOCOL_VERSION_DEFAULT);

            if(l_stream_id_int) {
                //log_it(L_DEBUG, "Stream server id %s, stream key length(base64 encoded) %u"
                //       ,l_stream_id,strlen(l_stream_key) );
                log_it(L_DEBUG, "Stream server id %u", l_stream_id_int);

                // Delete old key if present
                if(l_client_pvt->stream_key)
                    dap_enc_key_delete(l_client_pvt->stream_key);

                l_client_pvt->stream_id = l_stream_id_int;
                l_client_pvt->stream_key =
                        dap_enc_key_new_generate(l_enc_type, l_stream_key, strlen(l_stream_key), NULL, 0,
                                32);

                l_client_pvt->is_encrypted_headers = l_enc_headers;

                if(l_client_pvt->stage == STAGE_STREAM_CTL) { // We are on the right stage
                    l_client_pvt->stage_status = STAGE_STATUS_DONE;
                    s_stage_status_after(l_client_pvt);
                } else {
                    log_it(L_WARNING, "Expected to be stage STREAM_CTL but current stage is %s (%s)",
                            dap_client_get_stage_str(a_client), dap_client_get_stage_status_str(a_client));

                }
            } else {
                log_it(L_WARNING, "Wrong stream id response");
                l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT;
                l_client_pvt->stage_status = STAGE_STATUS_ERROR;
                s_stage_status_after(l_client_pvt);
            }

        }
        DAP_DELETE(l_stream_key);
    }
    DAP_DELETE(l_response_str);
}

/**
 * @brief s_stream_ctl_error
 * @param a_client
 * @param a_error
 */
static void s_stream_ctl_error(dap_client_t * a_client, UNUSED_ARG void *a_arg, int a_error)
{
    log_it(L_WARNING, "STREAM_CTL error %d", a_error);

    dap_client_pvt_t * l_client_pvt = DAP_CLIENT_PVT(a_client);
    assert(l_client_pvt);

    if (a_error == ETIMEDOUT) {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
    } else {
        l_client_pvt->last_error = ERROR_STREAM_CTL_ERROR;
    }
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    dap_stream_delete_prep_addr(0, l_client_pvt->session_key);

    s_stage_status_after(l_client_pvt);

}

/**
 * @brief s_stage_stream_opened
 * @param a_client
 * @param arg
 */
static void s_stage_stream_streaming(UNUSED_ARG dap_client_t *a_client, UNUSED_ARG void *a_arg)
{
    log_it(L_INFO, "Stream  is opened");
}

/**
 * @brief s_stream_es_callback_new
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_connected(dap_events_socket_t * a_es)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    if (!l_client_pvt) {
        log_it(L_ERROR, "Invalid client!");
        return;
    }
    s_stream_connected(l_client_pvt);
}

/**
 * @brief s_es_stream_delete
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_delete(dap_events_socket_t *a_es, UNUSED_ARG void *a_arg)
{
    log_it(L_INFO, "Stream events socket delete callback");
    if (a_es == NULL) {
        log_it(L_ERROR,"Esocket is NULL for s_stream_es_callback_delete");
        return;
    }
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    if (!l_client)
        return;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    l_client_pvt->last_error = ERROR_STREAM_ABORTED;
    l_client_pvt->stream->esocket = NULL; // Prevent to delete twice
    s_stage_status_after(l_client_pvt);
    a_es->_inheritor = NULL; // To prevent delete in reactor
}

/**
 * @brief s_es_stream_read
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_read(dap_events_socket_t * a_es, void * arg)
{
    (void) arg;
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);

    l_client_pvt->ts_last_active = time(NULL);
    switch (l_client_pvt->stage) {
        case STAGE_STREAM_SESSION:
            dap_client_go_stage(l_client_pvt->client, STAGE_STREAM_STREAMING, s_stage_stream_streaming);
        break;
        case STAGE_STREAM_CONNECTED: { // Collect HTTP headers before streaming
            if(a_es->buf_in_size > 1) {
                char * l_pos_endl;
                l_pos_endl = (char*) memchr(a_es->buf_in, '\r', a_es->buf_in_size - 1);
                if(l_pos_endl) {
                    if(*(l_pos_endl + 1) == '\n') {
                        dap_events_socket_shrink_buf_in(a_es, l_pos_endl - (char*)a_es->buf_in);
                        log_it(L_DEBUG, "Header passed, go to streaming (%zu bytes already are in input buffer",
                                a_es->buf_in_size);

                        l_client_pvt->stage = STAGE_STREAM_STREAMING;
                        l_client_pvt->stage_status = STAGE_STATUS_DONE;
                        s_stage_status_after(l_client_pvt);

                        dap_stream_data_proc_read(l_client_pvt->stream);
                        dap_events_socket_shrink_buf_in(a_es, a_es->buf_in_size);
                    }
                }
            }
        }
            break;
        case STAGE_STREAM_STREAMING: { // if streaming - process data with stream processor
            dap_stream_data_proc_read(l_client_pvt->stream);
            dap_events_socket_shrink_buf_in(a_es, a_es->buf_in_size);
        }
            break;
        default: {
        }
    }
}

/**
 * @brief s_es_stream_write
 * @param a_es
 * @param arg
 */
static void s_stream_es_callback_write(dap_events_socket_t * a_es, UNUSED_ARG void *a_arg)
{
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);

    if (l_client_pvt->stage_status == STAGE_STATUS_ERROR || !l_client_pvt->stream)
        return;
    switch (l_client_pvt->stage) {
        case STAGE_STREAM_STREAMING: {
            //  log_it(DEBUG,"Process channels data output (%u channels)",STREAM(sh)->channel_count);
            for (size_t i = 0; i < l_client_pvt->stream->channel_count; i++) {
                dap_stream_ch_t *ch = l_client_pvt->stream->channel[i];
                if (ch->ready_to_write && ch->proc->packet_out_callback)
                    ch->proc->packet_out_callback(ch, NULL);
            }
        } break;
        default: {}
    }
}

/**
 * @brief s_stream_es_callback_error
 * @param a_es
 * @param a_error
 */
static void s_stream_es_callback_error(dap_events_socket_t * a_es, int a_error)
{
    if (!a_es->_inheritor || !a_es ) {
        log_it(L_WARNING, "No client with client stream erro callback");
        return;
    }
    dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);

    char l_errbuf[128];
    if (a_error)
        strerror_r(a_error, l_errbuf, sizeof(l_errbuf));
    else
        strncpy(l_errbuf, "Unknown error", sizeof(l_errbuf) - 1);

    log_it(L_WARNING, "STREAM error \"%s\" (code %d)", l_errbuf, a_error);

    if (a_error == ETIMEDOUT) {
        l_client_pvt->last_error = ERROR_NETWORK_CONNECTION_TIMEOUT;
    } else {
        l_client_pvt->last_error = ERROR_STREAM_RESPONSE_WRONG;
    }
    l_client_pvt->stage_status = STAGE_STATUS_ERROR;
    l_client_pvt->stream->esocket = NULL; // Prevent to delete twice
    s_stage_status_after(l_client_pvt);
    a_es->_inheritor = NULL; // To prevent delete in reactor
}
