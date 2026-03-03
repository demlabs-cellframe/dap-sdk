#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#ifdef WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <pthread.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http_client.h"
#include "dap_client.h"
#include "dap_client_fsm.h"
#include "dap_client_esocket.h"
#include "dap_client_http.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_worker.h"
#include "dap_config.h"
#include "dap_net_trans.h"
#include "dap_net_trans_http_stream.h"
#include "dap_uuid.h"

#define LOG_TAG "dap_client"

static bool s_debug_more = false;

/**
 * @brief dap_client_init
 * @return
 */
int dap_client_init()
{
    static bool s_is_first_time = true;
    if (s_is_first_time) {
        int err = 0;
        log_it(L_INFO, "Init DAP client module");

        extern dap_config_t *g_config;
        if (g_config)
            s_debug_more = dap_config_get_item_bool_default(g_config, "dap_client", "debug_more", false);

        dap_http_client_init();
        err = dap_client_http_init();
        if (err) return err;
        dap_client_esocket_init();
        dap_client_fsm_init();
        s_is_first_time = false;
    }
    return 0;
}

/**
 * @brief dap_client_deinit
 */
void dap_client_deinit()
{
    dap_client_fsm_deinit();
    dap_client_esocket_deinit();
    dap_http_client_deinit();
    log_it(L_INFO, "Deinit DAP client module");
}

/**
 * @brief Get default transport type from config
 */
static dap_net_trans_type_t s_get_default_transport_from_config(void)
{
    const char *l_transport_str = dap_config_get_item_str_default(g_config, "dap_client",
                                                                    "default_transport", NULL);
    if (l_transport_str && l_transport_str[0] != '\0') {
        dap_net_trans_type_t l_trans_type = dap_net_trans_type_from_str(l_transport_str);
        log_it(L_INFO, "Default transport from config: %s (%d)", l_transport_str, l_trans_type);
        return l_trans_type;
    }
    return DAP_NET_TRANS_HTTP;
}

dap_client_t *dap_client_new(dap_client_callback_t a_stage_status_error_callback, void *a_callbacks_arg)
{
    dap_client_t *l_client = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_client_t, NULL);
    l_client->stage_status_error_callback = a_stage_status_error_callback;
    l_client->callbacks_arg = a_callbacks_arg;
    l_client->trans_type = s_get_default_transport_from_config();

    // Create FSM (which also creates esocket)
    dap_client_fsm_t *l_fsm = dap_client_fsm_new(l_client);
    if (!l_fsm) {
        DAP_DELETE(l_client);
        return NULL;
    }
    l_client->_internal = l_fsm;

    debug_if(s_debug_more, L_DEBUG, "Created client %p (fsm=%p, esocket=%p, fsm_thread=%u)",
             l_client, l_fsm, l_fsm->esocket, l_fsm->fsm_thread_idx);

    return l_client;
}

void dap_client_set_uplink_unsafe(dap_client_t *a_client, dap_stream_node_addr_t *a_node,
                                   const char *a_addr, uint16_t a_port)
{
    dap_return_if_pass(!a_client || !a_addr || !a_addr[0] || !a_port);
    a_client->link_info.uplink_addr[0] = '\0';
    strncpy(a_client->link_info.uplink_addr, a_addr, sizeof(a_client->link_info.uplink_addr) - 1);
    a_client->link_info.uplink_port = a_port;
    a_client->link_info.node_addr = *a_node;
}

void dap_client_set_active_channels_unsafe(dap_client_t *a_client, const char *a_active_channels)
{
    if (!a_client) {
        log_it(L_ERROR, "Client is NULL for dap_client_set_active_channels");
        return;
    }
    DAP_DEL_Z(a_client->active_channels);
    a_client->active_channels = dap_strdup(a_active_channels);
}

ssize_t dap_client_write_unsafe(dap_client_t *a_client, const char a_ch_id, uint8_t a_type,
                                 void *a_data, size_t a_data_size)
{
    if (!a_client || !a_client->active_channels || !strchr(a_client->active_channels, a_ch_id)) {
        log_it(L_ERROR, "Invalid arguments for dap_client_write_unsafe");
        return -1;
    }
    dap_stream_ch_t *l_ch = dap_client_get_stream_ch_unsafe(a_client, a_ch_id);
    if (l_ch)
        return dap_stream_ch_pkt_write_unsafe(l_ch, a_type, a_data, a_data_size);

    if (a_client->connect_on_demand) {
        dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
        dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
        if (!l_es || !l_fsm) return -1;

        dap_client_esocket_queue_add(l_es, a_ch_id, a_type, a_data, a_data_size);

        if (a_client->stage_target == STAGE_STREAM_STREAMING &&
            l_fsm->stage_status == STAGE_STATUS_IN_PROGRESS)
            return 0; // Already in progress

        a_client->stage_target = STAGE_STREAM_STREAMING;
        dap_client_fsm_stage_transaction_begin(l_fsm, STAGE_BEGIN, dap_client_fsm_advance);
        return 0;
    }
    return -2;
}

struct dap_client_write_args {
    dap_client_t *client;
    char ch_id;
    uint8_t type;
    size_t data_size;
    byte_t data[];
};

static void s_client_write_on_worker(void *a_arg)
{
    struct dap_client_write_args *l_args = a_arg;
    dap_client_write_unsafe(l_args->client, l_args->ch_id, l_args->type, l_args->data, l_args->data_size);
    DAP_DELETE(a_arg);
}

int dap_client_write_mt(dap_client_t *a_client, const char a_ch_id, uint8_t a_type,
                         void *a_data, size_t a_data_size)
{
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm || l_fsm->is_removing)
        return -1;

    struct dap_client_write_args *l_args = DAP_NEW_SIZE(struct dap_client_write_args,
                                                         sizeof(struct dap_client_write_args) + a_data_size);
    l_args->client = a_client;
    l_args->ch_id = a_ch_id;
    l_args->type = a_type;
    l_args->data_size = a_data_size;
    memcpy(l_args->data, a_data, a_data_size);
    dap_worker_exec_callback_on(l_fsm->worker, s_client_write_on_worker, l_args);
    return 0;
}

static void s_client_queue_clear_on_worker(void *a_arg)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET((dap_client_t *)a_arg);
    if (l_es)
        dap_client_esocket_queue_clear(l_es);
}

void dap_client_queue_clear(dap_client_t *a_client)
{
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (l_fsm)
        dap_worker_exec_callback_on(l_fsm->worker, s_client_queue_clear_on_worker, a_client);
}

void dap_client_set_auth_cert(dap_client_t *a_client, const char *a_cert_name)
{
    dap_return_if_fail(a_client && a_cert_name);
    dap_cert_t *l_cert = dap_cert_find_by_name(a_cert_name);
    if (!l_cert) {
        log_it(L_ERROR, "Certificate %s not found", a_cert_name);
        return;
    }
    a_client->auth_cert = l_cert;
}

void dap_client_delete_unsafe(dap_client_t *a_client)
{
    dap_return_if_fail(a_client);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (l_fsm)
        dap_client_fsm_delete_unsafe(l_fsm);
    a_client->_internal = NULL;
    DAP_DEL_Z(a_client->active_channels);
    if (a_client->del_arg)
        DAP_DELETE(a_client->callbacks_arg);
    DAP_DELETE(a_client);
}

static void s_client_delete_on_worker(void *a_arg)
{
    dap_client_t *l_client = (dap_client_t *)a_arg;
    if (!l_client) return;
    dap_client_delete_unsafe(l_client);
}

void dap_client_delete_mt(dap_client_t *a_client)
{
    if (!a_client) return;

    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) return;

    debug_if(s_debug_more, L_DEBUG, "Deleting client %p synchronously", a_client);

    l_fsm->is_removing = true;
    if (l_fsm->esocket)
        l_fsm->esocket->is_removing = true;

    dap_worker_exec_callback_on_sync(l_fsm->worker, s_client_delete_on_worker, a_client);

    debug_if(s_debug_more, L_DEBUG, "Client %p deleted", a_client);
}

/**
 * @brief dap_client_go_stage
 * Dispatches stage transition to FSM on its proc_thread.
 */
void dap_client_go_stage(dap_client_t *a_client, dap_client_stage_t a_stage_target,
                          dap_client_callback_t a_stage_end_callback)
{
    dap_return_if_fail(a_client);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm || l_fsm->is_removing) {
        log_it(L_ERROR, "dap_client_go_stage: no FSM or removing");
        return;
    }
    dap_client_fsm_go_stage(l_fsm, a_stage_target, a_stage_end_callback);
}

// ===== String conversion functions =====

const char *dap_client_error_str(dap_client_error_t a_client_error)
{
    switch (a_client_error) {
    case ERROR_NO_ERROR: return "NO_ERROR";
    case ERROR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case ERROR_ENC_NO_KEY: return "ENC_NO_KEY";
    case ERROR_ENC_WRONG_KEY: return "ENC_WRONG_KEY";
    case ERROR_ENC_SESSION_CLOSED: return "ENC_SESSION_CLOSED";
    case ERROR_STREAM_CTL_ERROR: return "STREAM_CTL_ERROR";
    case ERROR_STREAM_CTL_ERROR_AUTH: return "STREAM_CTL_ERROR_AUTH";
    case ERROR_STREAM_CTL_ERROR_RESPONSE_FORMAT: return "STREAM_CTL_ERROR_RESPONSE_FORMAT";
    case ERROR_STREAM_CONNECT: return "STREAM_CONNECTION_ERROR";
    case ERROR_STREAM_RESPONSE_WRONG: return "STREAM_RESPONSE_WRONG";
    case ERROR_STREAM_RESPONSE_TIMEOUT: return "STREAM_RESPONSE_TIMEOUT";
    case ERROR_STREAM_FREEZED: return "STREAM_FREEZED";
    case ERROR_STREAM_ABORTED: return "STREAM_ABORTED";
    case ERROR_NETWORK_CONNECTION_REFUSE: return "NETWORK_CONNECTION_REFUSED";
    case ERROR_NETWORK_CONNECTION_TIMEOUT: return "NETWORK_CONNECTION_TIMEOUT";
    case ERROR_WRONG_STAGE: return "INCORRECT_CLIENT_STAGE";
    case ERROR_WRONG_ADDRESS: return "INCORRECT_CLIENT_ADDRESS";
    default: return "UNDEFINED";
    }
}

const char *dap_client_get_error_str(dap_client_t *a_client)
{
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    return l_fsm ? dap_client_error_str(l_fsm->last_error) : "NO_FSM";
}

dap_client_stage_t dap_client_get_stage(dap_client_t *a_client)
{
    if (!a_client) return STAGE_UNDEFINED;
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) return STAGE_UNDEFINED;
    return (dap_client_stage_t)atomic_load(&l_fsm->stage_readable);
}

const char *dap_client_get_stage_status_str(dap_client_t *a_client)
{
    if (!a_client) return NULL;
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) return NULL;
    return dap_client_stage_status_str(
        (dap_client_stage_status_t)atomic_load(&l_fsm->stage_status_readable));
}

const char *dap_client_stage_status_str(dap_client_stage_status_t a_stage_status)
{
    switch (a_stage_status) {
    case STAGE_STATUS_NONE: return "NONE";
    case STAGE_STATUS_IN_PROGRESS: return "IN_PROGRESS";
    case STAGE_STATUS_ERROR: return "ERROR";
    case STAGE_STATUS_DONE: return "DONE";
    case STAGE_STATUS_COMPLETE: return "COMPLETE";
    default: return "UNDEFINED";
    }
}

const char *dap_client_get_stage_str(dap_client_t *a_client)
{
    if (!a_client) return NULL;
    return dap_client_stage_str(dap_client_get_stage(a_client));
}

const char *dap_client_stage_str(dap_client_stage_t a_stage)
{
    switch (a_stage) {
    case STAGE_BEGIN: return "BEGIN";
    case STAGE_ENC_INIT: return "ENC";
    case STAGE_STREAM_CTL: return "STREAM_CTL";
    case STAGE_STREAM_SESSION: return "STREAM_SESSION";
    case STAGE_STREAM_CONNECTED: return "STREAM_CONNECTED";
    case STAGE_STREAM_STREAMING: return "STREAM";
    case STAGE_QOS_PROBE: return "QOS_PROBE";
    default: return "UNDEFINED";
    }
}

dap_client_stage_status_t dap_client_get_stage_status(dap_client_t *a_client)
{
    if (!a_client) return STAGE_STATUS_NONE;
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) return STAGE_STATUS_NONE;
    return (dap_client_stage_status_t)atomic_load(&l_fsm->stage_status_readable);
}

dap_enc_key_t *dap_client_get_key_stream(dap_client_t *a_client)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    return l_es ? l_es->stream_key : NULL;
}

dap_stream_t *dap_client_get_stream(dap_client_t *a_client)
{
    if (!a_client) return NULL;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    return l_es ? l_es->stream : NULL;
}

dap_stream_worker_t *dap_client_get_stream_worker(dap_client_t *a_client)
{
    if (!a_client) return NULL;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    return l_es ? l_es->stream_worker : NULL;
}

dap_stream_ch_t *dap_client_get_stream_ch_unsafe(dap_client_t *a_client, uint8_t a_ch_id)
{
    dap_client_esocket_t *l_es = a_client ? DAP_CLIENT_ESOCKET(a_client) : NULL;
    if (l_es && l_es->stream && l_es->stream_es)
        return dap_stream_ch_by_id_unsafe(l_es->stream, a_ch_id);
    return NULL;
}

uint32_t dap_client_get_stream_id(dap_client_t *a_client)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    return l_es ? l_es->stream_id : 0;
}

bool dap_client_get_is_always_reconnect(dap_client_t *a_client)
{
    assert(a_client);
    return a_client->always_reconnect;
}

void dap_client_set_is_always_reconnect(dap_client_t *a_client, bool a_value)
{
    assert(a_client);
    a_client->always_reconnect = a_value;
}

void dap_client_set_trans_type(dap_client_t *a_client, dap_net_trans_type_t a_trans_type)
{
    if (!a_client) return;
    a_client->trans_type = a_trans_type;
}

dap_net_trans_type_t dap_client_get_trans_type(dap_client_t *a_client)
{
    return a_client ? a_client->trans_type : DAP_NET_TRANS_HTTP;
}

dap_client_t *dap_client_from_esocket(dap_events_socket_t *a_esocket)
{
    return (dap_client_t *)a_esocket->_inheritor;
}

const char *dap_client_get_auth_cookie(dap_client_t *a_client)
{
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(a_client);
    return l_es ? l_es->session_key_id : NULL;
}

// ===== Request functions (thread-safe) =====

struct dap_client_request_args {
    dap_client_t *client;
    char *path;
    void *request;
    size_t request_size;
    dap_client_callback_data_size_t response_proc;
    dap_client_callback_int_t response_error;
    void *callback_arg;
};

struct dap_client_request_enc_args {
    dap_client_t *client;
    char *path;
    char *sub_url;
    char *query;
    void *request;
    size_t request_size;
    dap_client_callback_data_size_t response_proc;
    dap_client_callback_int_t response_error;
    void *callback_arg;
};

static void s_client_request_on_worker(void *a_arg)
{
    struct dap_client_request_args *l_args = (struct dap_client_request_args *)a_arg;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_args->client);

    if (!l_es) {
        log_it(L_ERROR, "Invalid client_esocket in request");
    } else {
        l_es->callback_arg = l_args->callback_arg;
        if (l_args->client->trans_type == DAP_NET_TRANS_HTTP) {
            dap_net_trans_http_request(l_es, l_args->path,
                                       l_args->request, l_args->request_size,
                                       l_args->response_proc, l_args->response_error);
        }
    }

    DAP_DELETE(l_args->path);
    DAP_DEL_Z(l_args->request);
    DAP_DELETE(l_args);
}

static void s_client_request_enc_on_worker(void *a_arg)
{
    struct dap_client_request_enc_args *l_args = (struct dap_client_request_enc_args *)a_arg;
    dap_client_esocket_t *l_es = DAP_CLIENT_ESOCKET(l_args->client);

    if (!l_es || !l_es->session_key) {
        log_it(L_ERROR, "Invalid client or no session key for enc request");
    } else {
        l_es->callback_arg = l_args->callback_arg;
        if (l_args->client->trans_type == DAP_NET_TRANS_HTTP) {
            dap_net_trans_http_request_enc(l_es, l_args->path,
                                            l_args->sub_url, l_args->query,
                                            l_args->request, l_args->request_size,
                                            l_args->response_proc, l_args->response_error);
        }
    }

    DAP_DELETE(l_args->path);
    DAP_DEL_Z(l_args->sub_url);
    DAP_DEL_Z(l_args->query);
    DAP_DEL_Z(l_args->request);
    DAP_DELETE(l_args);
}

int dap_client_request(dap_client_t *a_client, const char *a_path, void *a_request, size_t a_request_size,
                       dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error,
                       void *a_callback_arg)
{
    dap_return_val_if_fail(a_client && a_path, -1);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    if (!l_fsm) return -1;

    struct dap_client_request_args *l_args = DAP_NEW_Z(struct dap_client_request_args);
    if (!l_args) return -1;
    l_args->client = a_client;
    l_args->path = dap_strdup(a_path);
    l_args->request = a_request ? DAP_DUP_SIZE(a_request, a_request_size) : NULL;
    l_args->request_size = a_request_size;
    l_args->response_proc = a_response_proc;
    l_args->response_error = a_response_error;
    l_args->callback_arg = a_callback_arg;

    dap_worker_exec_callback_on(l_fsm->worker, s_client_request_on_worker, l_args);
    return 0;
}

int dap_client_request_enc(dap_client_t *a_client, const char *a_path, const char *a_sub_url, const char *a_query,
                           void *a_request, size_t a_request_size,
                           dap_client_callback_data_size_t a_response_proc, dap_client_callback_int_t a_response_error,
                           void *a_callback_arg)
{
    dap_return_val_if_fail(a_client && a_path, -1);
    dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(a_client);
    dap_client_esocket_t *l_es = l_fsm ? l_fsm->esocket : NULL;
    if (!l_es || !l_es->session_key) return -1;

    struct dap_client_request_enc_args *l_args = DAP_NEW_Z(struct dap_client_request_enc_args);
    if (!l_args) return -1;
    l_args->client = a_client;
    l_args->path = dap_strdup(a_path);
    l_args->sub_url = a_sub_url ? dap_strdup(a_sub_url) : NULL;
    l_args->query = a_query ? dap_strdup(a_query) : NULL;
    l_args->request = a_request ? DAP_DUP_SIZE(a_request, a_request_size) : NULL;
    l_args->request_size = a_request_size;
    l_args->response_proc = a_response_proc;
    l_args->response_error = a_response_error;
    l_args->callback_arg = a_callback_arg;

    dap_worker_exec_callback_on(l_fsm->worker, s_client_request_enc_on_worker, l_args);
    return 0;
}
