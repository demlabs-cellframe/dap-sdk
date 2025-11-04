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
#include "dap_client_pvt.h"
#include "dap_client_http.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_worker.h"
#include "dap_config.h"
#include "dap_net_transport.h"

#define LOG_TAG "dap_client"

// FSM realization: thats callback executes after the every stage is done
// and have to select the next one stage
static void s_stage_fsm_operator_unsafe(dap_client_t *, void *);

/**
 * @brief dap_client_init
 * @return
 */
int dap_client_init()
{
    static bool s_is_first_time=true;
    if (s_is_first_time ) {
        int err = 0;
        log_it(L_INFO, "Init DAP client module");
        dap_http_client_init();
        err = dap_client_http_init();
        if (err)
            return err;
        dap_client_pvt_init();
        s_is_first_time = false;
    }
    return 0;

}

/**
 * @brief dap_client_deinit
 */
void dap_client_deinit()
{
    dap_client_pvt_deinit();
    dap_http_client_deinit();
    log_it(L_INFO, "Deinit DAP client module");
}

/**
 * @brief dap_client_new
 * @param a_stage_status_callback
 * @param a_stage_status_error_callback
 * @return
 */
/**
 * @brief Get default transport type from config
 * @return Default transport type, or DAP_NET_TRANSPORT_HTTP if not configured
 */
static dap_net_transport_type_t s_get_default_transport_from_config(void)
{
    // Try to get default transport from config
    const char *l_transport_str = dap_config_get_item_str_default(g_config, "dap_client", 
                                                                    "default_transport", NULL);
    
    if (l_transport_str && l_transport_str[0] != '\0') {
        dap_net_transport_type_t l_transport_type = dap_net_transport_type_from_str(l_transport_str);
        log_it(L_INFO, "Default transport loaded from config: %s (%d)", l_transport_str, l_transport_type);
        return l_transport_type;
    }
    
    // Fallback to HTTP if not configured
    log_it(L_DEBUG, "No default transport in config, using HTTP");
    return DAP_NET_TRANSPORT_HTTP;
}

dap_client_t *dap_client_new(dap_client_callback_t a_stage_status_error_callback, void *a_callbacks_arg)
{
    dap_client_t *l_client = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_client_t, NULL);
    l_client->_internal = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_client_pvt_t, NULL, l_client);
    l_client->stage_status_error_callback = a_stage_status_error_callback;
    l_client->callbacks_arg = a_callbacks_arg;
    l_client->transport_type = s_get_default_transport_from_config(); // Load from config or default to HTTP
    // CONSTRUCT dap_client object
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);
    l_client_pvt->client = l_client;
    l_client_pvt->worker = dap_events_worker_get_auto();
    
    // Initialize tried transports list (dynamic array)
    l_client_pvt->tried_transport_count = 0;
    l_client_pvt->tried_transport_capacity = DAP_NET_TRANSPORT_MAX; // Start with capacity for MAX transports
    l_client_pvt->tried_transports = DAP_NEW_Z_SIZE(dap_net_transport_type_t, 
                                                    l_client_pvt->tried_transport_capacity);
    if (!l_client_pvt->tried_transports) {
        log_it(L_ERROR, "Failed to allocate tried_transports array");
        DAP_DELETE(l_client_pvt);
        DAP_DELETE(l_client);
        return NULL;
    }
    // Mark initial transport as tried (will be added via helper function in dap_client_pvt_new if needed)
    l_client_pvt->tried_transports[l_client_pvt->tried_transport_count++] = l_client->transport_type;
    
    dap_client_pvt_new(l_client_pvt);
    return l_client;
}

/**
 * @brief dap_client_set_uplink
 * @param a_client
 * @param a_addr
 * @param a_port
 */
void dap_client_set_uplink_unsafe(dap_client_t *a_client, dap_stream_node_addr_t *a_node, const char *a_addr, uint16_t a_port)
{
// sanity check
    dap_return_if_pass(!a_client || !a_addr || !a_addr[0] || !a_port);
// func work
    a_client->link_info.uplink_addr[0] = '\0';
    strncpy(a_client->link_info.uplink_addr, a_addr, sizeof(a_client->link_info.uplink_addr) - 1);
    a_client->link_info.uplink_port = a_port;
    a_client->link_info.node_addr = *a_node;
}

/**
 * @brief dap_client_set_active_channels
 * @param a_client
 * @param a_active_channels
 */
void dap_client_set_active_channels_unsafe (dap_client_t * a_client, const char * a_active_channels)
{
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_set_active_channels");
        return;
    }

    DAP_DEL_Z(a_client->active_channels);
    a_client->active_channels = dap_strdup(a_active_channels);
}

ssize_t dap_client_write_unsafe(dap_client_t *a_client, const char a_ch_id, uint8_t a_type, void *a_data, size_t a_data_size)
{
    if (!a_client->active_channels || !strchr(a_client->active_channels, a_ch_id) || !a_client) {
        log_it(L_ERROR,"Arguments is NULL for dap_client_write_unsafe");
        return -1;
    }
    dap_stream_ch_t *l_ch = dap_client_get_stream_ch_unsafe(a_client, a_ch_id);
    if (l_ch)
        return dap_stream_ch_pkt_write_unsafe(l_ch, a_type, a_data, a_data_size);
    if (a_client->connect_on_demand) {
        dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
        dap_client_pvt_queue_add(l_client_pvt, a_ch_id, a_type, a_data, a_data_size);
        if (a_client->stage_target == STAGE_STREAM_STREAMING &&
                    l_client_pvt->stage_status == STAGE_STATUS_IN_PROGRESS)
            // Already going to aimed target stage
            return 0;
        a_client->stage_target = STAGE_STREAM_STREAMING;
        dap_client_pvt_stage_transaction_begin(l_client_pvt,
                                               STAGE_BEGIN,
                                               s_stage_fsm_operator_unsafe);
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

int dap_client_write_mt(dap_client_t *a_client, const char a_ch_id, uint8_t a_type, void *a_data, size_t a_data_size)
{
    if (DAP_CLIENT_PVT(a_client)->is_removing)
        return -1;
    struct dap_client_write_args *l_args = DAP_NEW_SIZE(struct dap_client_write_args, sizeof(struct dap_client_write_args) + a_data_size);
    l_args->client = a_client;
    l_args->ch_id = a_ch_id;
    l_args->type = a_type;
    l_args->data_size = a_data_size;
    memcpy(l_args->data, a_data, a_data_size);

    dap_worker_exec_callback_on(DAP_CLIENT_PVT(a_client)->worker, s_client_write_on_worker, l_args);

    return 0;
}

static void s_client_queue_clear_on_worker(void *a_arg)
{
    dap_client_pvt_queue_clear(DAP_CLIENT_PVT((dap_client_t *)a_arg));
}

void dap_client_queue_clear(dap_client_t *a_client)
{
    dap_worker_exec_callback_on(DAP_CLIENT_PVT(a_client)->worker, s_client_queue_clear_on_worker, a_client);
}

/**
 * @brief dap_client_set_auth_cert
 * @param a_client
 * @param a_chain_net_name
 * @param a_option
 */
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

/**
 * @brief s_client_delete
 * @param a_client
 */
void dap_client_delete_unsafe(dap_client_t *a_client)
{
    dap_return_if_fail(a_client);
    dap_client_pvt_delete_unsafe( DAP_CLIENT_PVT(a_client) );
    DAP_DEL_Z(a_client->active_channels);
    if (a_client->del_arg)
        DAP_DELETE(a_client->callbacks_arg);
    DAP_DELETE(a_client);
}


void s_client_delete_on_worker(void *a_arg)
{
    dap_client_delete_unsafe(a_arg);
}

void dap_client_delete_mt(dap_client_t *a_client)
{
    DAP_CLIENT_PVT(a_client)->is_removing = true;
    dap_worker_t *l_worker = DAP_CLIENT_PVT(a_client)->worker;
    dap_worker_exec_callback_on(l_worker, s_client_delete_on_worker, a_client);
}

struct go_stage_arg {
    dap_client_t *client;
    dap_client_stage_t stage_target;
    dap_client_callback_t stage_end_callback;
};

/**
 * @brief s_go_stage_on_client_worker_unsafe
 * @param a_worker
 * @param a_arg
 */
static void s_go_stage_on_client_worker_unsafe(void *a_arg)
{
    assert(a_arg);
    if (!a_arg) {
        log_it(L_ERROR, "Invalid arguments in s_go_stage_on_client_worker_unsafe");
        return;
    }
    struct go_stage_arg *l_args = a_arg;
    dap_client_stage_t l_stage_target = l_args->stage_target;
    dap_client_callback_t l_stage_end_callback = l_args->stage_end_callback;
    dap_client_t *l_client = l_args->client;
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(l_client);

    l_client->stage_target_done_callback = l_stage_end_callback;
    dap_client_stage_t l_cur_stage = l_client_pvt->stage;
    dap_client_stage_status_t l_cur_stage_status = l_client_pvt->stage_status;
    if (l_cur_stage_status == STAGE_STATUS_COMPLETE && l_client_pvt->stage == l_client->stage_target) {
        if (l_client->stage_target == l_stage_target) {
            log_it(L_DEBUG, "Already have target state %s", dap_client_stage_str(l_stage_target));
            if (l_stage_end_callback)
                l_stage_end_callback(l_client, l_client->callbacks_arg);
            DAP_DELETE(a_arg);
            return;
        }
        if (l_client->stage_target < l_stage_target) {
            l_client->stage_target = l_stage_target;
            log_it(L_DEBUG, "Start transitions chain for client %p -> %p from %s to %s", l_client_pvt, l_client,
                   dap_client_stage_str(l_cur_stage) , dap_client_stage_str(l_stage_target));
            dap_client_pvt_stage_transaction_begin(l_client_pvt,
                                                   l_cur_stage + 1,
                                                   s_stage_fsm_operator_unsafe);
            DAP_DELETE(a_arg);
            return;
        }
    }

    l_client->stage_target = l_stage_target;
    log_it(L_DEBUG, "Clear clinet state, then start transitions chain for client %p -> %p from %s to %s", l_client_pvt, l_client,
           dap_client_stage_str(l_cur_stage) , dap_client_stage_str(l_stage_target));
    dap_client_pvt_stage_transaction_begin(l_client_pvt,
                                           STAGE_BEGIN,
                                           s_stage_fsm_operator_unsafe);
    DAP_DELETE(a_arg);
}

/**
 * @brief dap_client_go_stage
 * @param a_client
 * @param a_stage_end
 */
void dap_client_go_stage(dap_client_t *a_client, dap_client_stage_t a_stage_target, dap_client_callback_t a_stage_end_callback)
{
    // ----- check parameters -----
    dap_return_if_fail(a_client);
    dap_client_pvt_t *l_client_pvt = DAP_CLIENT_PVT(a_client);
    if (NULL == l_client_pvt || l_client_pvt->is_removing) {
        log_it(L_ERROR, "dap_client_go_stage, client_pvt not exists or removing");
        return;
    }

    struct go_stage_arg *l_stage_arg = DAP_NEW_Z(struct go_stage_arg); if (! l_stage_arg) return;
    l_stage_arg->stage_end_callback = a_stage_end_callback;
    l_stage_arg->stage_target = a_stage_target;
    l_stage_arg->client = a_client;
    dap_worker_exec_callback_on(l_client_pvt->worker, s_go_stage_on_client_worker_unsafe, l_stage_arg);
}
/**
 * @brief s_stage_fsm_operator_unsafe
 * @param a_client
 * @param a_arg
 */
static void s_stage_fsm_operator_unsafe(dap_client_t * a_client, void * a_arg)
{
    UNUSED(a_arg);
    assert(a_client);
    if (!a_client) {
        log_it(L_ERROR, "Invalid arguments in s_stage_fsm_operator_unsafe");
        return;
    }
    dap_client_pvt_t * l_client_internal = DAP_CLIENT_PVT(a_client);
    assert(l_client_internal);
    if (!l_client_internal) {
        log_it(L_ERROR, "Crucial argument is NULL in s_stage_fsm_operator_unsafe");
        return;
    }

    if (a_client->stage_target == l_client_internal->stage){
        log_it(L_WARNING, "FSM Op: current stage %s is same as target one, nothing to do",
              dap_client_stage_str( l_client_internal->stage ) );
        l_client_internal->stage_status_done_callback = NULL;
        l_client_internal->stage_status = STAGE_STATUS_DONE;
        if (a_client->stage_target_done_callback)
            a_client->stage_target_done_callback(a_client, a_client->callbacks_arg);
        return;
    }

    assert(a_client->stage_target > l_client_internal->stage);
    dap_client_stage_t l_stage_next = l_client_internal->stage + 1;
    log_it(L_NOTICE, "FSM Op: current stage %s, go to %s (target %s)"
           ,dap_client_stage_str(l_client_internal->stage), dap_client_stage_str(l_stage_next)
           ,dap_client_stage_str(a_client->stage_target));
    dap_client_pvt_stage_transaction_begin(l_client_internal,
                                                l_stage_next, s_stage_fsm_operator_unsafe
                                                );
}


/**
 * @brief dap_client_error_str
 * @param a_client_error
 * @return
 */
const char * dap_client_error_str(dap_client_error_t a_client_error)
{
    switch (a_client_error) {
    case ERROR_NO_ERROR: return "NO_ERROR";
    case ERROR_OUT_OF_MEMORY: return "OUT_OF_MEMORY";
    case ERROR_ENC_NO_KEY: return "ENC_NO_KEY";
    case ERROR_ENC_WRONG_KEY: return "ENC_WRONG_KEY";
    case ERROR_ENC_SESSION_CLOSED:  return "ENC_SESSION_CLOSED";
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
    default : return "UNDEFINED";
    }
}

/**
 * @brief dap_client_get_error_str
 * @param a_client
 * @return
 */
const char * dap_client_get_error_str(dap_client_t * a_client)
{
    return dap_client_error_str( DAP_CLIENT_PVT(a_client)->last_error );
}
/**
 * @brief dap_client_get_stage
 * @param a_client
 * @return
 */
dap_client_stage_t dap_client_get_stage(dap_client_t * a_client)
{
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_get_stage");
        return -1;
    }
    return DAP_CLIENT_PVT(a_client)->stage;
}

/**
 * @brief dap_client_get_stage_status_str
 * @param a_client
 * @return
 */
const char * dap_client_get_stage_status_str(dap_client_t *a_client){
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_get_stage_status_str");
        return NULL;
    }
    return dap_client_stage_status_str(DAP_CLIENT_PVT(a_client)->stage_status);
}

/**
 * @brief dap_client_stage_status_str
 * @param a_stage_status
 * @return
 */
const char * dap_client_stage_status_str(dap_client_stage_status_t a_stage_status)
{
    switch(a_stage_status){
        case STAGE_STATUS_NONE: return "NONE";
        case STAGE_STATUS_IN_PROGRESS: return "IN_PROGRESS";
        case STAGE_STATUS_ERROR: return "ERROR";
        case STAGE_STATUS_DONE: return "DONE";
        case STAGE_STATUS_COMPLETE: return "COMPLETE";
        default: return "UNDEFINED";
    }
}

/**
 * @brief dap_client_get_stage_str
 * @param a_client
 * @return
 */
const char * dap_client_get_stage_str(dap_client_t *a_client)
{
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_get_stage_str");
        return NULL;
    }
    return dap_client_stage_str(DAP_CLIENT_PVT(a_client)->stage);
}

/**
 * @brief dap_client_stage_str
 * @param a_stage
 * @return
 */
const char * dap_client_stage_str(dap_client_stage_t a_stage)
{
    switch(a_stage){
        case STAGE_BEGIN: return "BEGIN";
        case STAGE_ENC_INIT: return "ENC";
        case STAGE_STREAM_CTL: return "STREAM_CTL";
        case STAGE_STREAM_SESSION: return "STREAM_SESSION";
        case STAGE_STREAM_CONNECTED: return "STREAM_CONNECTED";
        case STAGE_STREAM_STREAMING: return "STREAM";
        default: return "UNDEFINED";
    }
}
/**
 * @brief dap_client_get_stage_status
 * @param a_client
 * @return
 */
dap_client_stage_status_t dap_client_get_stage_status(dap_client_t * a_client)
{
    return (a_client && DAP_CLIENT_PVT(a_client)) ? DAP_CLIENT_PVT(a_client)->stage_status : STAGE_STATUS_NONE;
}

/**
 * @brief dap_client_get_key_stream
 * @param a_client
 * @return
 */
dap_enc_key_t * dap_client_get_key_stream(dap_client_t * a_client){
    return (a_client && DAP_CLIENT_PVT(a_client)) ? DAP_CLIENT_PVT(a_client)->stream_key : NULL;
}


/**
 * @brief dap_client_get_stream
 * @param a_client
 * @return
 */
dap_stream_t * dap_client_get_stream(dap_client_t * a_client)
{
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_get_stream");
        return NULL;
    }

    dap_client_pvt_t * l_client_internal = DAP_CLIENT_PVT(a_client);
    return (l_client_internal) ? l_client_internal->stream : NULL;
}

/**
 * @brief dap_client_get_stream_worker
 * @param a_client
 * @return
 */
dap_stream_worker_t * dap_client_get_stream_worker(dap_client_t * a_client)
{
    if(a_client == NULL){
        log_it(L_ERROR,"Client is NULL for dap_client_get_stream_worker");
        return NULL;
    }
    dap_client_pvt_t * l_client_internal = DAP_CLIENT_PVT(a_client);
    return (l_client_internal) ? l_client_internal->stream_worker : NULL;

}

dap_stream_ch_t * dap_client_get_stream_ch_unsafe(dap_client_t * a_client, uint8_t a_ch_id)
{
    dap_stream_ch_t * l_ch = NULL;
    dap_client_pvt_t * l_client_internal = a_client ? DAP_CLIENT_PVT(a_client) : NULL;
    if (l_client_internal && l_client_internal->stream && l_client_internal->stream_es)
        l_ch = dap_stream_ch_by_id_unsafe(l_client_internal->stream, a_ch_id);
    return l_ch;
}

/**
 * @brief dap_client_get_stream_id
 * @param a_client
 * @return
 */
uint32_t dap_client_get_stream_id(dap_client_t * a_client)
{
    if(!(a_client || !DAP_CLIENT_PVT(a_client)))
        return 0;
    return DAP_CLIENT_PVT(a_client)->stream_id;
}

/**
 * @brief dap_client_get_is_always_reconnect
 * @param a_client
 * @return
 */
bool dap_client_get_is_always_reconnect(dap_client_t * a_client)
{
    assert(a_client);
    return a_client->always_reconnect;
}

/**
 * @brief dap_client_set_is_always_reconnect
 * @param a_client
 * @param a_value
 */
void dap_client_set_is_always_reconnect(dap_client_t * a_client, bool a_value)
{
    assert(a_client);
    a_client->always_reconnect = a_value;
}

/**
 * @brief dap_client_set_transport_type
 * Set the transport layer type for stream connection
 * @param a_client Client instance
 * @param a_transport_type Transport type (HTTP, UDP, WebSocket, TLS, etc.)
 */
void dap_client_set_transport_type(dap_client_t *a_client, dap_net_transport_type_t a_transport_type)
{
    if (!a_client) {
        log_it(L_ERROR, "Client is NULL for dap_client_set_transport_type");
        return;
    }
    a_client->transport_type = a_transport_type;
    log_it(L_DEBUG, "Set transport type to %d for client %p", a_transport_type, a_client);
}

/**
 * @brief dap_client_get_transport_type
 * Get the transport layer type for stream connection
 * @param a_client Client instance
 * @return Transport type
 */
dap_net_transport_type_t dap_client_get_transport_type(dap_client_t *a_client)
{
    if (!a_client) {
        log_it(L_ERROR, "Client is NULL for dap_client_get_transport_type");
        return DAP_NET_TRANSPORT_HTTP; // Default fallback
    }
    return a_client->transport_type;
}

/**
 * @brief dap_client_from_esocket
 * @param a_esocket
 * @return
 */
dap_client_t * dap_client_from_esocket(dap_events_socket_t * a_esocket)
{
   return (dap_client_t *) a_esocket->_inheritor;
}
