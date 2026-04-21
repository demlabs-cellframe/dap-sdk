/*
 Copyright (c) 2017-2018 (c) Project "DeM Labs Inc" https://github.com/demlabsinc
  All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU Lesser General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>

#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#include <pthread.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#endif

#include "dap_common.h"
#include "dap_timerfd.h"
#include "dap_context.h"
#include "dap_stream.h"
#include "dap_stream_pkt.h"
#include "dap_stream_ch.h"
#include "dap_stream_ch_proc.h"
#include "dap_stream_ch_pkt.h"
#include "dap_stream_session.h"
#include "dap_events_socket.h"
#include "dap_io_flow_datagram.h"  // For UDP remote_addr access

#include "dap_http_server.h"
#include "dap_http_header_server.h"
#include "dap_http_client.h"
#include "dap_http_header.h"
#include "http_status_code.h"
#include "dap_stream_worker.h"
#include "dap_client_fsm.h"
#include "dap_client_trans_ctx.h"
#include "dap_strfuncs.h"
#include "uthash.h"
#include "dap_enc.h"
#include "dap_enc_ks.h"
#include "dap_stream_cluster.h"
#include "dap_link_manager.h"
#include "dap_net_trans.h"
#include "dap_net_trans_ctx.h"
#include "dap_net_trans_websocket_server.h"

#define LOG_TAG "dap_stream"

// Stream close timeout configuration (milliseconds)
// 0 = immediate close, >0 = graceful close with timeout
#define DAP_STREAM_CLOSE_TIMEOUT_MS 0  // Default: immediate close

// Globaly defined node address
dap_stream_node_addr_t g_node_addr;

typedef struct authorized_stream {
    union {
        unsigned long num;
        void *pointer;
    } id;
    dap_stream_node_addr_t node;
    UT_hash_handle hh;
} authorized_stream_t;

typedef struct s_stream_uuid_index {
    dap_events_socket_uuid_t esocket_uuid;
    dap_stream_t *stream;
    UT_hash_handle hh;
} s_stream_uuid_index_t;

typedef struct s_keepalive_timer_arg {
    dap_events_socket_uuid_t esocket_uuid;
    dap_timerfd_t *timer;
} s_keepalive_timer_arg_t;

static dap_cluster_t        *s_global_links_cluster = NULL;
static pthread_rwlock_t     s_streams_lock = PTHREAD_RWLOCK_INITIALIZER;    // Lock for all tables and list under
static dap_stream_t         *s_authorized_streams = NULL;                   // Authorized streams hashtable by addr
static dap_stream_t         *s_streams = NULL;                              // Double-linked list
static s_stream_uuid_index_t *s_streams_by_es_uuid = NULL;                  // Stream index by esocket UUID
static dap_enc_key_type_t   s_stream_get_preferred_encryption_type = DAP_ENC_KEY_TYPE_IAES;

static int s_add_stream_info(authorized_stream_t **a_hash_table, authorized_stream_t *a_item, dap_stream_t *a_stream);

static void s_stream_proc_pkt_in(dap_stream_t * a_stream, dap_stream_pkt_t *l_pkt);

// Callbacks for HTTP client
static void s_http_client_headers_read(dap_http_client_t * a_http_client, void * a_arg); // Prepare stream when all headers are read

static bool s_http_client_headers_write(dap_http_client_t * a_http_client, void * a_arg); // Output headers
static bool s_http_client_data_write(dap_http_client_t * a_http_client, void * a_arg); // Write the data
static void s_http_client_data_read(dap_http_client_t * a_http_client, void * a_arg); // Read the data

static void s_esocket_callback_worker_assign(dap_events_socket_t * a_esocket, dap_worker_t * a_worker);
static void s_esocket_callback_worker_unassign(dap_events_socket_t * a_esocket, dap_worker_t * a_worker);

static void s_esocket_data_read(dap_events_socket_t* a_esocket, void * a_arg);
static bool s_esocket_write(dap_events_socket_t* a_esocket, void * a_arg);
static void s_esocket_callback_delete(dap_events_socket_t* a_esocket, void * a_arg);
static void s_esocket_callback_error(dap_events_socket_t *a_esocket, int a_error);
static void s_udp_esocket_new(dap_events_socket_t* a_esocket,void * a_arg);

// Internal functions
static dap_stream_t * s_stream_new(dap_http_client_t * a_http_client, dap_stream_node_addr_t *a_addr); // Create new stream
static void s_http_client_delete(dap_http_client_t * a_esocket, void * a_arg);
void s_stream_delete_from_list(dap_stream_t *a_stream);

static bool s_callback_server_keepalive(void *a_arg);
static bool s_callback_client_keepalive(void *a_arg);

static bool s_dump_packet_headers = false;
static bool s_debug = false;
#define LOG_TAG "dap_stream"

bool dap_stream_get_dump_packet_headers(){ return  s_dump_packet_headers; }

static bool s_detect_loose_packet(dap_stream_t * a_stream);
static int s_stream_add_stream_info(dap_stream_t *a_stream, uint64_t a_id);

typedef struct s_keepalive_timer_cleanup_ctx {
    dap_timerfd_t *timer;
    void *arg_to_free;
} s_keepalive_timer_cleanup_ctx_t;

static void s_keepalive_timer_cleanup_cb(void *a_arg)
{
    s_keepalive_timer_cleanup_ctx_t *l_ctx = (s_keepalive_timer_cleanup_ctx_t *)a_arg;
    if (!l_ctx || !l_ctx->timer)
        return;
    l_ctx->arg_to_free = l_ctx->timer->callback_arg;
    l_ctx->timer->callback_arg = NULL;
    dap_timerfd_delete_unsafe(l_ctx->timer);
}

static dap_stream_t *s_stream_index_get_by_es_uuid_unsafe(dap_events_socket_uuid_t a_es_uuid)
{
    s_stream_uuid_index_t *l_idx = NULL;
    if (!a_es_uuid)
        return NULL;
    HASH_FIND(hh, s_streams_by_es_uuid, &a_es_uuid, sizeof(a_es_uuid), l_idx);
    return l_idx ? l_idx->stream : NULL;
}

static void s_stream_index_add_unsafe(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->esocket_uuid)
        return;
    s_stream_uuid_index_t *l_it = NULL, *l_tmp = NULL;
    HASH_ITER(hh, s_streams_by_es_uuid, l_it, l_tmp) {
        if (l_it->stream == a_stream) {
            HASH_DEL(s_streams_by_es_uuid, l_it);
            DAP_DELETE(l_it);
        }
    }
    s_stream_uuid_index_t *l_idx = NULL;
    HASH_FIND(hh, s_streams_by_es_uuid, &a_stream->esocket_uuid, sizeof(a_stream->esocket_uuid), l_idx);
    if (!l_idx) {
        l_idx = DAP_NEW_Z(s_stream_uuid_index_t);
        if (!l_idx) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return;
        }
        l_idx->esocket_uuid = a_stream->esocket_uuid;
        HASH_ADD(hh, s_streams_by_es_uuid, esocket_uuid, sizeof(l_idx->esocket_uuid), l_idx);
    }
    l_idx->stream = a_stream;
}

static void s_stream_index_remove_by_stream_unsafe(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;
    s_stream_uuid_index_t *l_it = NULL, *l_tmp = NULL;
    HASH_ITER(hh, s_streams_by_es_uuid, l_it, l_tmp) {
        if (l_it->stream == a_stream) {
            HASH_DEL(s_streams_by_es_uuid, l_it);
            DAP_DELETE(l_it);
        }
    }
}

static dap_timerfd_t *s_keepalive_timer_start(dap_worker_t *a_worker,
                                              dap_events_socket_uuid_t a_es_uuid,
                                              dap_timerfd_callback_t a_callback)
{
    if (!a_worker || !a_es_uuid || !a_callback)
        return NULL;

    s_keepalive_timer_arg_t *l_arg = DAP_NEW_Z(s_keepalive_timer_arg_t);
    if (!l_arg) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_arg->esocket_uuid = a_es_uuid;
    dap_timerfd_t *l_timer = dap_timerfd_start_on_worker(a_worker,
                                                          STREAM_KEEPALIVE_TIMEOUT * 1000,
                                                          a_callback,
                                                          l_arg);
    if (!l_timer) {
        DAP_DELETE(l_arg);
        return NULL;
    }
    l_arg->timer = l_timer;
    return l_timer;
}

static bool s_keepalive_timer_self_stop(s_keepalive_timer_arg_t *a_arg)
{
    if (!a_arg)
        return false;
    int l_lock = pthread_rwlock_wrlock(&s_streams_lock);
    assert(l_lock != EDEADLK);
    if (l_lock == 0) {
        dap_stream_t *l_stream = s_stream_index_get_by_es_uuid_unsafe(a_arg->esocket_uuid);
        if (l_stream && l_stream->keepalive_timer == a_arg->timer)
            l_stream->keepalive_timer = NULL;
        pthread_rwlock_unlock(&s_streams_lock);
    } else if (l_lock == EDEADLK) {
        log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !");
    }
    if (a_arg->timer && a_arg->timer->callback_arg == a_arg)
        a_arg->timer->callback_arg = NULL;
    DAP_DELETE(a_arg);
    return false;
}

/**
 * @brief Write data via transport layer (wrapper for trans->ops->write)
 * 
 * Calls transport-specific write callback. For UDP this goes through Flow Control.
 * FAIL-FAST: Returns error if trans->ops->write is not set (no fallback).
 * 
 * @param a_stream Stream instance
 * @param a_data Data to write
 * @param a_size Data size
 * @return Number of bytes written, or 0 on error
 */
ssize_t dap_stream_trans_write_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    if (!a_stream->trans_ctx) {
        log_it(L_ERROR, "Stream has no trans_ctx");
        return 0;
    }
    
    if (!a_stream->trans_ctx->trans || 
        !a_stream->trans_ctx->trans->ops || 
        !a_stream->trans_ctx->trans->ops->write) {
        log_it(L_WARNING, "Stream trans has no write callback (trans_ctx->trans may not be set yet)");
        return 0;
    }
    
    debug_if(s_debug, L_DEBUG, 
             "dap_stream_trans_write_unsafe: writing %zu bytes via trans->ops->write", a_size);
    
    ssize_t l_ret = a_stream->trans_ctx->trans->ops->write(
        a_stream,  // stream
        a_data,    // data
        a_size     // size
    );
    
    if (l_ret < 0) {
        log_it(L_WARNING, "trans->ops->write failed: ret=%zd", l_ret);
        return 0;
    }
    
    return l_ret;
}

/**
 * @brief Send data to datagram stream using flow callback
 * 
 * Helper function to send data via datagram stream (UDP, etc), properly resolving
 * the destination address through the datagram flow's get_remote_addr callback.
 * 
 * Works for both CLIENT and SERVER streams via stream->flow.
 * 
 * @param a_stream Stream instance
 * @param a_data Data to send
 * @param a_size Data size
 * @return Number of bytes sent, or 0 on error
 */
static inline ssize_t s_stream_send_datagram_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    if (!a_stream->flow) {
        log_it(L_ERROR, "Datagram stream %p has no flow set", a_stream);
        return 0;
    }
    
    return dap_stream_trans_write_unsafe(a_stream, a_data, a_size);
}

static bool s_stream_esocket_is_detached(const dap_events_socket_t *a_es)
{
    if (!a_es) {
        return false;
    }
    if (a_es->_inheritor != NULL) {
        return false;
    }
    dap_client_t *l_client = DAP_ESOCKET_CLIENT((dap_events_socket_t*)a_es);
    if (l_client) {
        dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
        dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
        if (l_tc && l_tc->stream)
            return false;
    }

    bool l_stream_found = false;
    pthread_rwlock_rdlock(&s_streams_lock);
    l_stream_found = s_stream_index_get_by_es_uuid_unsafe(a_es->uuid) != NULL;
    pthread_rwlock_unlock(&s_streams_lock);

    return !l_stream_found;
}

/**
 * @brief Send raw data to stream (datagram-aware, public API)
 * 
 * Sends raw data to the stream, handling datagram (UDP, etc) destination resolution automatically.
 * For datagram streams (_server_session is datagram flow), uses callback to get destination.
 * For stream-oriented (TCP), uses direct esocket write.
 */
ssize_t dap_stream_send_unsafe(dap_stream_t *a_stream, const void *a_data, size_t a_size)
{
    if (!a_stream || !a_data || a_size == 0) {
        return 0;
    }
    
    if (!a_stream->esocket) {
        log_it(L_ERROR, "Stream has no esocket");
        return 0;
    }

    dap_events_socket_t *l_es = a_stream->esocket;
    
    // Check if this is a datagram transport (UDP, SCTP, etc)
    if (dap_events_socket_is_datagram(l_es)) {
        debug_if(s_debug, L_DEBUG, "dap_stream_send_unsafe: stream %p using datagram path, flow=%p", a_stream, a_stream->flow);
        return s_stream_send_datagram_unsafe(a_stream, a_data, a_size);
    }
    
    // Stream-oriented transport (TCP, etc) - use direct write
    return dap_events_socket_write_unsafe(l_es, a_data, a_size);
}


#ifdef  DAP_SYS_DEBUG
enum    {MEMSTAT$K_STM, MEMSTAT$K_NR};
static  dap_memstat_rec_t   s_memstat [MEMSTAT$K_NR] = {
    {.fac_len = sizeof(LOG_TAG) - 1, .fac_name = {LOG_TAG}, .alloc_sz = sizeof(dap_stream_t)},
};
#endif

dap_enc_key_type_t dap_stream_get_preferred_encryption_type()
{
    return s_stream_get_preferred_encryption_type;
}

void s_stream_load_preferred_encryption_type(dap_config_t * a_config)
{
    const char * l_preferred_encryption_name = dap_config_get_item_str(a_config, "stream", "preferred_encryption");
    if(l_preferred_encryption_name){
        dap_enc_key_type_t l_found_key_type = dap_enc_key_type_find_by_name(l_preferred_encryption_name);
        if(l_found_key_type != DAP_ENC_KEY_TYPE_INVALID)
            s_stream_get_preferred_encryption_type = l_found_key_type;
    }

    log_it(L_NOTICE, "Encryption type is set to %s", dap_enc_get_type_name(s_stream_get_preferred_encryption_type));
}

int s_stream_init_node_addr_cert()
{
    dap_cert_t *l_addr_cert = dap_cert_find_by_name(DAP_STREAM_NODE_ADDR_CERT_NAME);
    if (!l_addr_cert) {
        const char *l_cert_folder = dap_cert_get_folder(DAP_CERT_FOLDER_PATH_DEFAULT);
        // create new cert
        if(l_cert_folder) {
            char *l_cert_path = dap_strdup_printf("%s/" DAP_STREAM_NODE_ADDR_CERT_NAME ".dcert", l_cert_folder);
            l_addr_cert = dap_cert_generate(DAP_STREAM_NODE_ADDR_CERT_NAME, l_cert_path, DAP_STREAM_NODE_ADDR_CERT_TYPE);
            DAP_DELETE(l_cert_path);
        } else
            return -1;
    }
    g_node_addr = dap_stream_node_addr_from_cert(l_addr_cert);
    return 0;
}
static bool s_debug_more = false;

static void s_stream_fragments_reset(dap_stream_t *a_stream)
{
    if (!a_stream)
        return;
    DAP_DEL_Z(a_stream->buf_fragments);
    DAP_DEL_Z(a_stream->buf_fragments_map);
    a_stream->buf_fragments_size_total = 0;
    a_stream->buf_fragments_size_filled = 0;
}

/**
 * @brief stream_init Init stream module
 * @return  0 if ok others if not
 */
int dap_stream_init(dap_config_t * a_config)
{
    if( dap_stream_ch_init() != 0 ){
        log_it(L_CRITICAL, "Can't init channel types submodule");
        return -1;
    }
    if( dap_stream_worker_init() != 0 ){
        log_it(L_CRITICAL, "Can't init stream worker extention submodule");
        return -2;
    }
    if (s_stream_init_node_addr_cert()) {
        log_it(L_ERROR, "Can't initiazlize certificate containing secure node address");
        return -3;
    }
    
    // Transport layer is initialized automatically via dap_module system
    // No need to call dap_net_transport_init() manually
    
    s_stream_load_preferred_encryption_type(a_config);
    s_dump_packet_headers = dap_config_get_item_bool_default(g_config, "stream", "debug_dump_stream_headers", false);
    s_debug = dap_config_get_item_bool_default(g_config, "stream", "debug_more", false);
    debug_if(s_debug, L_DEBUG, "dap_stream_init: g_config=%p, s_debug=%d, s_dump_packet_headers=%d",
             (void*)g_config, s_debug, s_dump_packet_headers);
#ifdef DAP_SYS_DEBUG
    for (int i = 0; i < MEMSTAT$K_NR; i++)
        dap_memstat_reg(&s_memstat[i]);
#endif

#ifdef DAP_STREAM_TEST
#include "dap_stream_test.h"
    dap_stream_test_init();
#endif

    s_global_links_cluster = dap_cluster_new(DAP_STREAM_CLUSTER_GLOBAL, *(dap_guuid_t *)&uint128_0, DAP_CLUSTER_TYPE_SYSTEM);

    log_it(L_NOTICE,"Init streaming module with transport layer");

    return 0;
}

/**
 * @brief stream_media_deinit Deinint Stream module
 */
void dap_stream_deinit()
{
    // Transport layer is deinitialized automatically via dap_module system
    // No need to call dap_net_transport_deinit() manually
    
    dap_stream_ch_deinit( );
}

/**
 * @brief stream_add_proc_http Add URL processor callback for streaming
 * @param sh HTTP server instance
 * @param url URL
 */
void dap_stream_add_proc_http(struct dap_http_server* a_http, const char * a_url)
{
    dap_http_add_proc(a_http,
                      a_url,
                      NULL, // _internal
                      NULL, // New
                      s_http_client_delete, // Delete
                      s_http_client_headers_read, // Headers read
                      s_http_client_headers_write, // Headerts write
                      s_http_client_data_read, // Data read
                      s_http_client_data_write, // Data write
                      NULL); // Error callback
}

/**
 * @brief stream_add_proc_udp Add processor callback for streaming
 * @param a_udp_server UDP server instance
 */
void dap_stream_add_proc_udp(dap_server_t *a_udp_server)
{
    a_udp_server->client_callbacks.read_callback = s_esocket_data_read;
    a_udp_server->client_callbacks.write_callback = s_esocket_write;
    a_udp_server->client_callbacks.delete_callback = s_esocket_callback_delete;
    a_udp_server->client_callbacks.new_callback = s_udp_esocket_new;
    a_udp_server->client_callbacks.worker_assign_callback = s_esocket_callback_worker_assign;
    a_udp_server->client_callbacks.worker_unassign_callback = s_esocket_callback_worker_unassign;

}

/**
 * @brief stream_add_proc_dns Add processor callback for DNS streaming
 * @param a_dns_server DNS server instance
 * @note DNS uses the same callbacks as UDP since both are connectionless protocols
 */
void dap_stream_add_proc_dns(dap_server_t *a_dns_server)
{
    // DNS is connectionless like UDP, so we can reuse the same callbacks
    // The actual DNS query/response parsing will be handled by the transport layer
    a_dns_server->client_callbacks.read_callback = s_esocket_data_read;
    a_dns_server->client_callbacks.write_callback = s_esocket_write;
    a_dns_server->client_callbacks.delete_callback = s_esocket_callback_delete;
    a_dns_server->client_callbacks.new_callback = s_udp_esocket_new;  // Reuse UDP new callback
    a_dns_server->client_callbacks.worker_assign_callback = s_esocket_callback_worker_assign;
    a_dns_server->client_callbacks.worker_unassign_callback = s_esocket_callback_worker_unassign;
}

/**
 * @brief s_stream_states_update
 * @param a_stream stream instance
 */
static void s_stream_states_update(dap_stream_t *a_stream)
{
    if (!a_stream) {
        log_it(L_ERROR, "s_stream_states_update: stream is NULL");
        return;
    }
    if (!a_stream->esocket) {
        log_it(L_ERROR, "s_stream_states_update: stream->esocket is NULL");
        return;
    }
    size_t i;
    bool ready_to_write=false;
    for(i=0;i<a_stream->channel_count; i++) {
        if (!a_stream->channel || !a_stream->channel[i]) {
            log_it(L_ERROR, "s_stream_states_update: channel[%zu] is NULL (channel_count=%zu, channel=%p)", 
                   i, a_stream->channel_count, (void*)a_stream->channel);
            continue;
        }
        ready_to_write|=a_stream->channel[i]->ready_to_write;
    }
    dap_events_socket_set_writable_unsafe(a_stream->esocket, ready_to_write);
    
    // Transport-specific state updates should be handled by transport callbacks
    // No direct transport-specific logic here - all through dap_stream_transport API
}



/**
 * @brief stream_new_udp Create new stream instance for UDP client
 * @param sh DAP client structure
 */
dap_stream_t * stream_new_udp(dap_events_socket_t * a_esocket)
{
    dap_stream_t * l_stm = DAP_NEW_Z(dap_stream_t);
    assert(l_stm);
    if (!l_stm) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    // Esocket owned by stream
    l_stm->esocket = a_esocket;
    l_stm->esocket_uuid = a_esocket->uuid;
    l_stm->esocket_worker = a_esocket->worker;

    l_stm->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (l_stm->trans_ctx) {
        l_stm->trans_ctx->stream = l_stm;  // Back-reference
        dap_strncpy(l_stm->trans_ctx->remote_addr_str, a_esocket->remote_addr_str, sizeof(l_stm->trans_ctx->remote_addr_str) - 1);
        l_stm->trans_ctx->remote_port = a_esocket->remote_port;
    }

    // Unified: _inheritor always points to trans_ctx for all transport types
    a_esocket->_inheritor = l_stm->trans_ctx;
    dap_stream_add_to_list(l_stm);
    log_it(L_NOTICE,"New stream instance udp");
    return l_stm ;
}

/**
 * @brief s_check_session CHeck session status, open if need
 * @param id session id
 * @param a_esocket stream event socket
 */
static void s_check_session( unsigned int a_id, dap_events_socket_t *a_esocket )
{
    dap_stream_session_t *l_session = NULL;

    l_session = dap_stream_session_id_mt( a_id );

    if ( l_session == NULL ) {
        log_it(L_ERROR,"No session id %u was found",a_id);
        return;
    }

    log_it( L_INFO, "Session id %u was found with media_id = %d", a_id,l_session->media_id );

    if ( dap_stream_session_open(l_session) != 0 ) { // Create new stream

        log_it( L_ERROR, "Can't open session id %u", a_id );
        return;
    }

    dap_stream_t *l_stream;
    // Unified: check trans_ctx for existing stream
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    if (!l_trans_ctx || !l_trans_ctx->stream)
        l_stream = stream_new_udp(a_esocket);
    else
        l_stream = l_trans_ctx->stream;

    l_stream->session = l_session;

    if ( l_session->create_empty )
        log_it( L_INFO, "Session created empty" );

    log_it( L_INFO, "Opened stream session technical and data channels" );

    //size_t count_channels = strlen(l_session->active_channels);
    for (size_t i =0; i<sizeof (l_session->active_channels); i++ )
        if ( l_session->active_channels[i])
            dap_stream_ch_new( l_stream, l_session->active_channels[i] );

    s_stream_states_update(l_stream);

#ifdef DAP_EVENTS_CAPS_IOCP
     a_esocket->flags |= DAP_SOCK_READY_TO_READ;
#else
    dap_events_socket_set_readable_unsafe( a_esocket, true );
#endif
}

/**
 * @brief stream_new Create new stream instance for HTTP client
 * @return New stream_t instance
 */
dap_stream_t *s_stream_new(dap_http_client_t *a_http_client, dap_stream_node_addr_t *a_addr)
{
    debug_if(s_debug, L_DEBUG, "s_stream_new: entering, a_http_client=%p, a_addr=%p", (void*)a_http_client, (void*)a_addr);
    if (!a_http_client) {
        log_it(L_ERROR, "s_stream_new: a_http_client is NULL");
        return NULL;
    }
    if (!a_http_client->esocket) {
        log_it(L_ERROR, "s_stream_new: a_http_client->esocket is NULL");
        return NULL;
    }
    if (!a_http_client->esocket->worker) {
        log_it(L_ERROR, "s_stream_new: a_http_client->esocket->worker is NULL");
        return NULL;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_new: allocating dap_stream_t");
    dap_stream_t *l_ret = DAP_NEW_Z(dap_stream_t);
    debug_if(s_debug, L_DEBUG, "s_stream_new: DAP_NEW_Z returned %p", (void*)l_ret);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_new: allocated stream %p", (void*)l_ret);

#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM].alloc_nr, 1);
#endif

    debug_if(s_debug, L_DEBUG, "s_stream_new: setting esocket");
    // Esocket owned by stream
    l_ret->esocket = a_http_client->esocket;
    l_ret->esocket_uuid = a_http_client->esocket->uuid;
    l_ret->esocket_worker = a_http_client->esocket->worker;

    l_ret->trans_ctx = DAP_NEW_Z(dap_net_trans_ctx_t);
    if (l_ret->trans_ctx) {
        l_ret->trans_ctx->stream = l_ret;  // Back-reference
        dap_strncpy(l_ret->trans_ctx->remote_addr_str, a_http_client->esocket->remote_addr_str, sizeof(l_ret->trans_ctx->remote_addr_str) - 1);
        l_ret->trans_ctx->remote_port = a_http_client->esocket->remote_port;
        l_ret->trans_ctx->http_client = a_http_client;
        a_http_client->esocket->_inheritor = l_ret->trans_ctx;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_new: getting stream_worker");
    debug_if(s_debug, L_DEBUG, "s_stream_new: worker=%p, worker->_inheritor=%p", 
           (void*)a_http_client->esocket->worker,
           a_http_client->esocket->worker ? (void*)a_http_client->esocket->worker->_inheritor : NULL);
    l_ret->stream_worker = DAP_STREAM_WORKER(a_http_client->esocket->worker);
    debug_if(s_debug, L_DEBUG, "s_stream_new: stream_worker=%p", (void*)l_ret->stream_worker);
    if (!l_ret->stream_worker) {
        log_it(L_ERROR, "stream_worker is NULL for worker %p (worker->_inheritor=%p)", 
               (void*)a_http_client->esocket->worker,
               a_http_client->esocket->worker ? (void*)a_http_client->esocket->worker->_inheritor : NULL);
        DAP_DELETE(l_ret);
        return NULL;
    }
    
    debug_if(s_debug, L_DEBUG, "s_stream_new: assigning HTTP transport");
    // Assign HTTP transport for this stream
    dap_net_trans_t *l_transport = dap_net_trans_find(DAP_NET_TRANS_HTTP);
    debug_if(s_debug, L_DEBUG, "s_stream_new: found transport=%p", (void*)l_transport);
    if (l_transport) {
        l_ret->trans = l_transport;
        debug_if(s_debug, L_DEBUG, "s_stream_new: assigned transport");
        // Store HTTP client in transport-specific data
        // Note: HTTP client binding is managed by the transport layer
        // This will be properly implemented when HTTP transport is fully integrated
    }
    
    debug_if(s_debug, L_DEBUG, "s_stream_new: initializing seq_id");
    l_ret->seq_id = 0;
    l_ret->client_last_seq_id_packet = (size_t)-1;
    
    debug_if(s_debug, L_DEBUG, "s_stream_new: copying esocket uuid");
    if (l_ret->esocket) {
        debug_if(s_debug, L_DEBUG, "s_stream_new: starting keepalive timer");
        l_ret->keepalive_timer = s_keepalive_timer_start(l_ret->esocket_worker,
                                                         l_ret->esocket_uuid,
                                                         (dap_timerfd_callback_t)s_callback_server_keepalive);

        if (!l_ret->keepalive_timer) {
            log_it(L_ERROR, "Failed to start keepalive timer");
        }

        debug_if(s_debug, L_DEBUG, "s_stream_new: keepalive timer started=%p", (void*)l_ret->keepalive_timer);
        a_http_client->reply_status_code = Http_Status_OK;
        strcpy(a_http_client->reply_reason_phrase, "OK");
        a_http_client->keep_alive = true;
        debug_if(s_debug, L_DEBUG, "s_stream_new: sending HTTP response before callback takeover, esocket=%p, read_cb=%p",
                 (void*)l_ret->esocket, (void*)l_ret->esocket->callbacks.read_callback);
        dap_http_client_write(a_http_client);

        debug_if(s_debug, L_DEBUG, "s_stream_new: TAKEOVER old read_cb=%p -> new read_cb=%p, esocket=%p",
                 (void*)l_ret->esocket->callbacks.read_callback, (void*)s_esocket_data_read,
                 (void*)l_ret->esocket);
        l_ret->esocket->callbacks.read_callback = s_esocket_data_read;
        l_ret->esocket->callbacks.write_callback = s_esocket_write;
        l_ret->esocket->callbacks.delete_callback = s_esocket_callback_delete;
        l_ret->esocket->callbacks.error_callback = s_esocket_callback_error;
        l_ret->esocket->callbacks.worker_assign_callback = s_esocket_callback_worker_assign;
        l_ret->esocket->callbacks.worker_unassign_callback = s_esocket_callback_worker_unassign;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_new: callbacks set");
    if (a_addr && !dap_stream_node_addr_is_blank(a_addr)) {
        debug_if(s_debug, L_DEBUG, "s_stream_new: setting node address");
        l_ret->node = *a_addr;
        l_ret->authorized = true;
        log_it(L_INFO, "s_stream_new: stream %p AUTHORIZED, node=" NODE_ADDR_FP_STR, (void*)l_ret, NODE_ADDR_FP_ARGS_S(*a_addr));
    } else {
        log_it(L_WARNING, "s_stream_new: stream %p NOT authorized (a_addr=%p, blank=%d)",
               (void*)l_ret, (void*)a_addr, a_addr ? dap_stream_node_addr_is_blank(a_addr) : -1);
    }
    debug_if(s_debug, L_DEBUG, "s_stream_new: adding stream to list");
    dap_stream_add_to_list(l_ret);
    debug_if(s_debug, L_DEBUG, "s_stream_new: stream added to list");
    log_it(L_NOTICE,"New stream instance");
    debug_if(s_debug, L_DEBUG, "s_stream_new: returning stream %p", (void*)l_ret);
    return l_ret;
}

/**
 * @brief dap_stream_new_es
 * @param a_es
 * @return
 */
dap_stream_t *dap_stream_new_es_client(dap_events_socket_t *a_esocket, dap_stream_node_addr_t *a_addr, bool a_authorized)
{
    dap_stream_t *l_ret = DAP_NEW_Z(dap_stream_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM].alloc_nr, 1);
#endif
    l_ret->esocket = a_esocket;
    l_ret->is_client_to_uplink = true;
    if (a_esocket) {
        l_ret->esocket_uuid = a_esocket->uuid;
        l_ret->esocket_worker = a_esocket->worker;
        a_esocket->callbacks.worker_assign_callback = s_esocket_callback_worker_assign;
        a_esocket->callbacks.worker_unassign_callback = s_esocket_callback_worker_unassign;
        if (a_esocket->worker) {
            // Use server or client keepalive based on connection direction.
            // Server-side (incoming) connections have a_esocket->server != NULL.
            // Using the wrong callback causes assert(a_server_side == !!l_es->server) to fire.
            dap_timerfd_callback_t l_keepalive_cb = a_esocket->server
                ? (dap_timerfd_callback_t)s_callback_server_keepalive
                : (dap_timerfd_callback_t)s_callback_client_keepalive;
            bool l_is_server = !!a_esocket->server;
            l_ret->keepalive_timer = s_keepalive_timer_start(a_esocket->worker,
                                                             a_esocket->uuid,
                                                             l_keepalive_cb);
            if (l_ret->keepalive_timer)
                log_it(L_INFO, "%s stream %p: keepalive timer started on worker #%u, es_uuid=0x%"DAP_UINT64_FORMAT_x" sock=%"DAP_FORMAT_SOCKET,
                       l_is_server ? "Server" : "Client",
                       l_ret, a_esocket->worker->id, a_esocket->uuid, a_esocket->socket);
        } else {
            log_it(L_WARNING, "Client stream %p: esocket %"DAP_FORMAT_SOCKET" has no worker, keepalive NOT started",
                   l_ret, a_esocket->socket);
        }
    }
    if (a_addr)
        l_ret->node = *a_addr;
    l_ret->authorized = a_authorized;
    log_it(L_NOTICE, "Client P2P stream created: stream=%p es=%p sock=%"DAP_FORMAT_SOCKET" keepalive=%s",
           l_ret, (void*)a_esocket, a_esocket ? a_esocket->socket : -1,
           l_ret->keepalive_timer ? "YES" : "NO");
    return l_ret;
}

/**
 * @brief Start client keepalive timer for a stream that was created without an esocket.
 *        Called when the esocket is assigned to the stream after deferred connection (e.g. HTTP transport).
 * @param a_stream Stream with esocket and esocket->worker already set
 * @return 0 on success, -1 on error
 */
int dap_stream_start_keepalive(dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->esocket || !a_stream->esocket->worker) {
        log_it(L_ERROR, "Cannot start keepalive: stream=%p es=%p",
               (void*)a_stream, a_stream ? (void*)a_stream->esocket : NULL);
        return -1;
    }
    if (a_stream->keepalive_timer) {
        return 0;
    }
    a_stream->keepalive_timer = s_keepalive_timer_start(a_stream->esocket->worker,
                                                        a_stream->esocket->uuid,
                                                        (dap_timerfd_callback_t)s_callback_client_keepalive);
    if (!a_stream->keepalive_timer) {
        return -1;
    }
    log_it(L_INFO, "Stream %p: deferred keepalive started on worker #%u, es_uuid=0x%"DAP_UINT64_FORMAT_x,
           a_stream, a_stream->esocket->worker->id, a_stream->esocket->uuid);
    return 0;
}

/**
 * @brief dap_stream_delete_unsafe
 * @param a_stream
 */
void dap_stream_delete_unsafe(dap_stream_t *a_stream)
{  
    if(!a_stream) {
        log_it(L_ERROR,"stream delete NULL instance");
        return;
    }
    if (a_stream->is_client_to_uplink)
        debug_if(s_debug_more, L_DEBUG, "P2P stream DELETE (client): stream=%p es=%p sock=%"DAP_FORMAT_SOCKET" keepalive=%s",
               a_stream, (void*)a_stream->esocket,
               a_stream->esocket ? a_stream->esocket->socket : -1,
               a_stream->keepalive_timer ? "active" : "none");
    else
        log_it(L_INFO, "P2P stream DELETE (server): stream=%p es=%p sock=%"DAP_FORMAT_SOCKET,
               a_stream, (void*)a_stream->esocket,
               a_stream->esocket ? a_stream->esocket->socket : -1);
    if (a_stream->keepalive_timer) {
        dap_timerfd_t *l_timer = a_stream->keepalive_timer;
        a_stream->keepalive_timer = NULL;
        s_keepalive_timer_cleanup_ctx_t l_cleanup_ctx = { .timer = l_timer, .arg_to_free = NULL };
        if (l_timer->worker)
            dap_worker_exec_callback_on_sync(l_timer->worker, s_keepalive_timer_cleanup_cb, &l_cleanup_ctx);
        else
            s_keepalive_timer_cleanup_cb(&l_cleanup_ctx);
        DAP_DELETE(l_cleanup_ctx.arg_to_free);
    }
    s_stream_delete_from_list(a_stream);
    // a_stream->esocket_uuid = 0;
    while (a_stream->channel_count)
        dap_stream_ch_delete(a_stream->channel[a_stream->channel_count - 1]);

    if(a_stream->session) {
        // Graceful close with configurable timeout
        // Configure via DAP_STREAM_CLOSE_TIMEOUT_MS
        #if DAP_STREAM_CLOSE_TIMEOUT_MS > 0
            // Future: Implement delayed close using dap_timerfd
            // This would allow pending data to be sent before session close
            // For now, fallback to immediate close
            debug_if(s_debug_more, L_DEBUG, "Stream close timeout configured but not yet implemented, closing immediately");
        #endif
        dap_stream_session_close_mt(a_stream->session->id);
    }

    // Call trans->ops->close() FIRST to let transport manage esocket
    dap_net_trans_t *l_trans = a_stream->trans;
    if (l_trans) {
        a_stream->trans = NULL;  // Prevent double close
        if (l_trans->ops && l_trans->ops->close) {
            l_trans->ops->close(a_stream);
        }
    }

    // Only delete esocket if trans didn't handle it (stream owns esocket)
    if (a_stream->esocket && a_stream->esocket_worker) {
        dap_events_socket_t *l_es = a_stream->esocket;
        dap_worker_t *l_es_worker = a_stream->esocket_worker;
        l_es->callbacks.delete_callback = NULL;
        l_es->_inheritor = NULL;
        a_stream->esocket = NULL;
        a_stream->esocket_uuid = 0;
        a_stream->esocket_worker = NULL;
        dap_worker_t *l_current = dap_worker_get_current();
        if (l_current == l_es_worker) {
            dap_events_socket_remove_and_delete_unsafe(l_es, false);
        } else {
            dap_events_socket_remove_and_delete_mt(l_es_worker, l_es->uuid);
        }
    }
    if(a_stream->client_stream_ref)
    {
        *a_stream->client_stream_ref = NULL;
        a_stream->client_stream_ref = NULL;
        // FSM owns trans_ctx — don't free it here, just detach
        a_stream->trans_ctx = NULL;
    } else {
        DAP_DELETE(a_stream->trans_ctx);
        a_stream->trans_ctx = NULL;
    }

#ifdef  DAP_SYS_DEBUG
    atomic_fetch_add(&s_memstat[MEMSTAT$K_STM].free_nr, 1);
#endif

    s_stream_fragments_reset(a_stream);
    DAP_DEL_Z(a_stream->pkt_cache);
    DAP_DELETE(a_stream);
    debug_if(s_debug, L_DEBUG, "Stream connection is over");
}

/**
 * @brief stream_dap_delete Delete callback for UDP client
 * @param a_esocket DAP client instance
 * @param arg Not used
 */
static void s_esocket_callback_delete(dap_events_socket_t* a_esocket, void * a_arg)
{
    UNUSED(a_arg);
    assert(a_esocket);
    log_it(L_WARNING, "STREAM_SOCKET_DELETE: esocket=%p fd=%d uuid=0x%"DAP_UINT64_FORMAT_x" flags=0x%x",
           (void*)a_esocket, a_esocket->fd, a_esocket->uuid, a_esocket->flags);

    // Unified: _inheritor is always trans_ctx
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    a_esocket->_inheritor = NULL;  // Clear FIRST to prevent use-after-free
    
    if (!l_trans_ctx)
        return;
    
    dap_stream_t *l_stm = l_trans_ctx->stream;
    dap_http_client_t *l_http_client = l_trans_ctx->http_client;
    
    if (l_stm) {
        l_stm->esocket = NULL;
        l_stm->esocket_uuid = 0;
        l_stm->esocket_worker = NULL;
    }

    // Clean up HTTP client resources if this was an HTTP-based stream
    if (l_http_client) {
        // Clean up HTTP headers
        while (l_http_client->in_headers)
            dap_http_header_remove(&l_http_client->in_headers, l_http_client->in_headers);
        while (l_http_client->out_headers)
            dap_http_header_remove(&l_http_client->out_headers, l_http_client->out_headers);
        DAP_DEL_Z(l_http_client->_inheritor);
    }
    
    // Delete the stream (removes from global list) — for ALL transport types
    if (l_stm)
        dap_stream_delete_unsafe(l_stm);
}

/**
 * @brief s_esocket_callback_error Unified error callback for stream esockets
 */
static void s_esocket_callback_error(dap_events_socket_t *a_esocket, int a_error)
{
    log_it(L_WARNING, "Stream esocket error: %d", a_error);
    // Mark for close, the delete callback will clean up
    a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

/**
 * @brief stream_header_read Read headers callback for HTTP
 * @param a_http_client HTTP client structure
 * @param a_arg Not used
 */
void s_http_client_headers_read(dap_http_client_t * a_http_client, void UNUSED_ARG *a_arg)
{
    if (dap_net_trans_websocket_try_upgrade(a_http_client) == 0)
        return;

    unsigned int l_id=0;
    //debug_if(s_debug_more, L_DEBUG,"Prepare data stream");
    if(a_http_client->in_query_string[0]){
        log_it(L_INFO,"Query string [%s]",a_http_client->in_query_string);
        if(sscanf(a_http_client->in_query_string,"session_id=%u",&l_id) == 1 ||
                sscanf(a_http_client->in_query_string,"fj913htmdgaq-d9hf=%u",&l_id) == 1) {
            dap_stream_session_t *l_ss = dap_stream_session_id_mt(l_id);
            if(!l_ss) {
                log_it(L_ERROR,"No session id %u was found", l_id);
                a_http_client->reply_status_code = Http_Status_NotFound;
                strcpy(a_http_client->reply_reason_phrase,"Not found");
            } else {
                log_it(L_INFO,"Session id %u was found with channels = %s", l_id, l_ss->active_channels);
                debug_if(s_debug, L_DEBUG, "Session pointer: %p, mutex: %p, active_channels: %p", 
                       (void*)l_ss, (void*)&l_ss->mutex, (void*)l_ss->active_channels);
                debug_if(s_debug, L_DEBUG, "Calling dap_stream_session_open for session %u", l_id);
                int l_open_ret = dap_stream_session_open(l_ss);
                debug_if(s_debug, L_DEBUG, "dap_stream_session_open returned %d for session %u", l_open_ret, l_id);
                if(!l_open_ret){ // Create new stream
                    debug_if(s_debug, L_DEBUG, "Opening session %u, creating stream", l_id);
                    debug_if(s_debug, L_DEBUG, "Before s_stream_new: a_http_client=%p, l_ss=%p, l_ss->node offset=%zu", 
                           (void*)a_http_client, (void*)l_ss, 
                           (size_t)((char*)&l_ss->node - (char*)l_ss));
                    dap_stream_node_addr_t *l_node_addr = &l_ss->node;
                    debug_if(s_debug, L_DEBUG, "l_node_addr=%p", (void*)l_node_addr);
                    dap_stream_t *l_stream = s_stream_new(a_http_client, l_node_addr);
                    debug_if(s_debug, L_DEBUG, "After s_stream_new: l_stream=%p", (void*)l_stream);
                    if (!l_stream) {
                        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                        a_http_client->reply_status_code = Http_Status_NotFound;
                        return;
                    }
                    debug_if(s_debug, L_DEBUG, "Stream created successfully: %p (esocket=%p, stream_worker=%p)",
                           (void*)l_stream, (void*)l_stream->esocket, (void*)l_stream->stream_worker);
                    l_stream->session = l_ss;
                    debug_if(s_debug, L_DEBUG, "Session assigned to stream");
                    dap_http_header_t *header = dap_http_header_find(a_http_client->in_headers, "Service-Key");
                    if (header)
                        l_ss->service_key = strdup(header->value);
                    size_t count_channels = strlen(l_ss->active_channels);
                    debug_if(s_debug, L_DEBUG, "Creating %zu channels for session %u", count_channels, l_id);
                    for(size_t i = 0; i < count_channels; i++) {
                        dap_stream_ch_t * l_ch = dap_stream_ch_new(l_stream, l_ss->active_channels[i]);
                        if (!l_ch) {
                            log_it(L_ERROR, "Failed to create channel '%c' for session %u", l_ss->active_channels[i], l_id);
                            a_http_client->reply_status_code = Http_Status_InternalServerError;
                            return;
                        }
                        l_ch->ready_to_read = true;
                        //l_stream->channel[i]->ready_to_write = true;
                    }
                    debug_if(s_debug, L_DEBUG, "All %zu channels created successfully, updating stream states", count_channels);

                    a_http_client->reply_status_code = Http_Status_OK;
                    strcpy(a_http_client->reply_reason_phrase,"OK");
                    debug_if(s_debug, L_DEBUG, "Calling s_stream_states_update for stream %p (esocket=%p, channel_count=%zu)",
                           (void*)l_stream, (void*)l_stream->esocket, l_stream->channel_count);
                    s_stream_states_update(l_stream);
                    debug_if(s_debug, L_DEBUG, "s_stream_states_update completed successfully");
                    a_http_client->state_read = DAP_HTTP_CLIENT_STATE_DATA;
#ifdef DAP_EVENTS_CAPS_IOCP
                    a_http_client->esocket->flags |= DAP_SOCK_READY_TO_READ | DAP_SOCK_READY_TO_WRITE;
#else
                    dap_events_socket_set_readable_unsafe(a_http_client->esocket,true);
                    dap_events_socket_set_writable_unsafe(a_http_client->esocket,true);
#endif
                }else{
                    log_it(L_ERROR,"Can't open session id %u", l_id);
                    a_http_client->reply_status_code = Http_Status_NotFound;
                    strcpy(a_http_client->reply_reason_phrase,"Not found");
                }
            }
        }
    }else{
        log_it(L_ERROR,"No query string");
    }
}

/**
 * @brief s_http_client_headers_write Prepare headers for output. Creates stream structure
 * @param sh HTTP client instance
 * @param arg Not used
 */
static bool s_http_client_headers_write(dap_http_client_t * a_http_client, void *a_arg)
{
    (void) a_arg;
    //debug_if(s_debug_more, L_DEBUG,"s_http_client_headers_write()");
    if(a_http_client->reply_status_code == Http_Status_OK){
        // Unified: get stream via trans_ctx
        dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_http_client->esocket->_inheritor;
        dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;

        dap_http_out_header_add(a_http_client,"Content-Type","application/octet-stream");
        dap_http_out_header_add(a_http_client,"Connection","keep-alive");
        dap_http_out_header_add(a_http_client,"Cache-Control","no-cache");

        if(l_stream && l_stream->stream_size > 0)
            dap_http_header_server_out_header_add_f(a_http_client,"Content-Length","%u", (unsigned int) l_stream->stream_size );

        a_http_client->state_read=DAP_HTTP_CLIENT_STATE_DATA;
        dap_events_socket_set_readable_unsafe(a_http_client->esocket,true);
    }
    return false;
}

/**
 * @brief stream_data_write HTTP data write callback
 * @param a_http_client HTTP client instance
 * @param a_arg Not used
 */
static bool s_http_client_data_write(dap_http_client_t * a_http_client, void UNUSED_ARG *a_arg)
{
    if (a_http_client->reply_status_code == Http_Status_OK)
        return s_esocket_write(a_http_client->esocket, a_arg);

    log_it(L_WARNING, "Wrong request, reply status code is %u", a_http_client->reply_status_code);
    return false;
}

/**
 * @brief s_esocket_callback_worker_assign
 * @param a_esocket
 * @param a_worker
 */
static void s_esocket_callback_worker_assign(dap_events_socket_t * a_esocket, dap_worker_t * a_worker)
{
    if (!a_esocket->is_initalized)
        return;
    dap_stream_t *l_stream = dap_stream_get_from_es(a_esocket);
    if (!l_stream) {
        log_it(L_DEBUG, "Skip worker assign callback for events socket "DAP_FORMAT_ESOCKET_UUID" (stream teardown in progress)", a_esocket->uuid);
        return;
    }
    dap_stream_add_to_list(l_stream);
    // Restart server keepalive timer if it was unassigned before
    if (!l_stream->keepalive_timer) {
        dap_timerfd_callback_t l_callback = a_esocket->server ? s_callback_server_keepalive : s_callback_client_keepalive;
        l_stream->keepalive_timer = s_keepalive_timer_start(a_worker,
                                                            a_esocket->uuid,
                                                            l_callback);
    }
}

/**
 * @brief s_esocket_callback_worker_unassign
 * @param a_esocket
 * @param a_worker
 */
static void s_esocket_callback_worker_unassign(dap_events_socket_t * a_esocket, dap_worker_t * a_worker)
{
    UNUSED(a_worker);
    dap_stream_t *l_stream = dap_stream_get_from_es(a_esocket);
    if (!l_stream) {
        log_it(L_DEBUG, "Skip worker unassign callback for events socket "DAP_FORMAT_ESOCKET_UUID" (stream teardown in progress)", a_esocket->uuid);
        return;
    }
    s_stream_delete_from_list(l_stream);
    if (l_stream->keepalive_timer) {
        dap_timerfd_t *l_timer = l_stream->keepalive_timer;
        l_stream->keepalive_timer = NULL;
        s_keepalive_timer_cleanup_ctx_t l_cleanup_ctx = { .timer = l_timer, .arg_to_free = NULL };
        s_keepalive_timer_cleanup_cb(&l_cleanup_ctx);
        DAP_DELETE(l_cleanup_ctx.arg_to_free);
    }
}

/**
 * @brief s_data_read
 * @param a_client
 * @param a_arg
 */
static void s_esocket_data_read(dap_events_socket_t* a_esocket, void * a_arg)
{
    int *l_ret = (int *)a_arg;
    
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;

    log_it(L_INFO, "s_esocket_data_read: stream=%p buf_in_size=%zu sock=%"DAP_FORMAT_SOCKET,
           (void*)l_stream, a_esocket->buf_in_size, a_esocket->socket);
    if (!l_stream) {
        log_it(L_ERROR, "s_esocket_data_read: no stream! trans_ctx=%p", (void*)l_trans_ctx);
        if (l_ret) *l_ret = 0;
        return;
    }
    if (a_esocket->buf_in_size > 0 && a_esocket->buf_in_size < 256) {
        char l_hex[768];
        size_t l_dump = a_esocket->buf_in_size < 64 ? a_esocket->buf_in_size : 64;
        for (size_t i = 0; i < l_dump; i++)
            snprintf(l_hex + i * 3, 4, "%02x ", a_esocket->buf_in[i]);
        l_hex[l_dump * 3] = '\0';
        log_it(L_INFO, "s_esocket_data_read: first %zu bytes: %s", l_dump, l_hex);
    }
    size_t l_processed = dap_stream_data_proc_read(l_stream);
    log_it(L_INFO, "s_esocket_data_read: processed=%zu / %zu", l_processed, a_esocket->buf_in_size);
    if (l_ret)
        *l_ret = (int)l_processed;
    if (l_processed > 0)
        dap_events_socket_shrink_buf_in(a_esocket, l_processed);
}



/**
 * @brief stream_dap_data_write Write callback for UDP client
 * @param sh DAP client instance
 * @param arg Not used
 */
static bool s_esocket_write(dap_events_socket_t *a_esocket , void *a_arg)
{
    bool l_ret = false;
    
    // Unified: get stream from trans_ctx
    dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_esocket->_inheritor;
    dap_stream_t *l_stream = l_trans_ctx ? l_trans_ctx->stream : NULL;
    
    if (!l_stream) {
        return false;
    }
    
    // Channel identification: iterate all channels and let each one process pending data
    // Each channel maintains its own write queue and will only write if it has pending data
    // This approach works for current use cases but could be optimized in future by:
    // - Maintaining a "dirty" flag for channels with pending writes
    // - Using a priority queue for channels with different QoS requirements
    //debug_if(s_debug_more, L_DEBUG,"Process channels data output (%u channels)", l_stream->channel_count );
    for (size_t i = 0; i < l_stream->channel_count; i++) {
        dap_stream_ch_t *l_ch = l_stream->channel[i];
        if (l_ch->ready_to_write && l_ch->proc->packet_out_callback)
            l_ret |= l_ch->proc->packet_out_callback(l_ch, a_arg);
    }
    return l_ret;
}

/**
 * @brief s_udp_esocket_new New connection callback for UDP client
 * @param a_esocket DAP client instance
 * @param arg Not used
 */
static void s_udp_esocket_new(dap_events_socket_t* a_esocket, UNUSED_ARG void * a_arg)
{
    stream_new_udp(a_esocket);
}


/**
 * @brief s_http_client_data_read HTTP data read callback. Read packet and passes that to the channel's callback
 * @param a_http_client HTTP client instance
 * @param arg Processed number of bytes
 */
static void s_http_client_data_read(dap_http_client_t * a_http_client, void * arg)
{
    s_esocket_data_read(a_http_client->esocket,arg);
}

/**
 * @brief stream_delete Delete stream and free its resources
 * @param sid Stream id
 */
static void s_http_client_delete(dap_http_client_t * a_http_client, void *a_arg)
{
    UNUSED(a_arg);
    UNUSED(a_http_client);
}

/**
 * @brief dap_stream_data_proc_read
 * @param a_stream
 * @return
 */
/**
 * @brief Process incoming stream data from external buffer (transport agnostic)
 * @param a_stream Stream instance
 * @param a_data Data buffer to process
 * @param a_data_size Size of data buffer
 * @return Number of bytes processed
 */
size_t dap_stream_data_proc_read_ext(dap_stream_t *a_stream, const void *a_data, size_t a_data_size)
{
    if (!a_stream || !a_data || a_data_size == 0) {
        log_it(L_WARNING, "proc_read_ext: early return stream=%p data=%p size=%zu",
               (void*)a_stream, a_data, a_data_size);
        return 0;
    }
    
    dap_events_socket_t *l_es = a_stream->esocket;
    byte_t *l_pos = (byte_t*)a_data;
    byte_t *l_end = l_pos + a_data_size;
    size_t l_shift = 0, l_processed_size = 0;
    bool l_found_sig0 = false;
    
    while (l_pos < l_end && (l_pos = memchr(l_pos, c_dap_stream_sig[0], (size_t)(l_end - l_pos)))) {
        l_found_sig0 = true;
        if ((size_t)(l_end - l_pos) < sizeof(dap_stream_pkt_hdr_t)) {
            log_it(L_INFO, "proc_read_ext: partial header, remain=%zu need=%zu",
                   (size_t)(l_end - l_pos), sizeof(dap_stream_pkt_hdr_t));
            break;
        }
        
        if (!memcmp(l_pos, c_dap_stream_sig, sizeof(c_dap_stream_sig))) {
            dap_stream_pkt_t *l_pkt = (dap_stream_pkt_t*)l_pos;
            debug_if(s_debug_more, L_DEBUG, "proc_read_ext: SIG FOUND type=0x%02x size=%u at offset=%zu",
                   l_pkt->hdr.type, l_pkt->hdr.size, (size_t)(l_pos - (byte_t*)a_data));
            if (l_pkt->hdr.size > DAP_STREAM_PKT_SIZE_MAX) {
                log_it(L_ERROR, "Invalid packet size %u, dump it", l_pkt->hdr.size);
                l_shift = sizeof(dap_stream_pkt_hdr_t);
            } else if ((l_shift = sizeof(dap_stream_pkt_hdr_t) + l_pkt->hdr.size) <= (size_t)(l_end - l_pos)) {
                debug_if(s_debug_more, L_DEBUG, "proc_read_ext: full packet %zu bytes, dispatching", l_shift);
                s_stream_proc_pkt_in(a_stream, l_pkt);
                if (s_stream_esocket_is_detached(l_es)) {
                    l_processed_size += l_shift;
                    break;
                }
            } else {
                debug_if(s_debug_more, L_DEBUG, "proc_read_ext: incomplete packet need=%zu have=%zu",
                       l_shift, (size_t)(l_end - l_pos));
                break;
            }
            l_pos += l_shift;
            l_processed_size += l_shift;
        } else {
            ++l_pos;
        }
    }
    if (a_data_size > 0 && !l_found_sig0 && l_processed_size == 0)
        log_it(L_WARNING, "proc_read_ext: no sig[0]=0x%02x found in %zu bytes (first byte=0x%02x)",
               c_dap_stream_sig[0], a_data_size, ((byte_t*)a_data)[0]);
    
    debug_if(s_dump_packet_headers && l_processed_size, L_DEBUG,
             "Processed %zu / %zu bytes", l_processed_size, a_data_size);

    return l_processed_size;
}

/**
 * @brief Process incoming stream data from esocket buffer (legacy wrapper)
 * @param a_stream Stream instance
 * @return Number of bytes processed
 */
size_t dap_stream_data_proc_read (dap_stream_t *a_stream)
{
    if (!a_stream || !a_stream->esocket)
        return 0;

    dap_events_socket_t *l_es = a_stream->esocket;
    
    if (!l_es->buf_in)
        return 0;
    
    return dap_stream_data_proc_read_ext(a_stream, l_es->buf_in, l_es->buf_in_size);
}

/**
 * @brief stream_proc_pkt_in
 * @param sid
 */
static void s_stream_proc_pkt_in(dap_stream_t * a_stream, dap_stream_pkt_t *a_pkt)
{
    size_t a_pkt_size = sizeof(dap_stream_pkt_hdr_t) + a_pkt->hdr.size;
    bool l_is_clean_fragments = false;
    a_stream->is_active = true;
    dap_events_socket_t *l_es = a_stream->esocket;

    debug_if(s_dump_packet_headers, L_INFO, "s_stream_proc_pkt_in: stream=%p, packet type=0x%02X size=%u", 
           a_stream, a_pkt->hdr.type, a_pkt->hdr.size);

    switch (a_pkt->hdr.type) {
    case STREAM_PKT_TYPE_FRAGMENT_PACKET: {

        debug_if(s_dump_packet_headers, L_INFO, "Processing FRAGMENT_PACKET, size=%u", a_pkt->hdr.size);

        size_t l_fragm_dec_size = dap_enc_decode_out_size(a_stream->session->key, a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: stream=%p, session=%p, key=%p, fragm_dec_size=%zu",
                 a_stream, a_stream->session, a_stream->session ? a_stream->session->key : NULL, l_fragm_dec_size);
        
        a_stream->pkt_cache = DAP_NEW_Z_SIZE(byte_t, l_fragm_dec_size);
        dap_stream_fragment_pkt_t *l_fragm_pkt = (dap_stream_fragment_pkt_t*)a_stream->pkt_cache;
        
        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: CALLING dap_stream_pkt_read_unsafe (stream=%p, pkt=%p, out=%p, out_size=%zu)",
                 a_stream, a_pkt, l_fragm_pkt, l_fragm_dec_size);
        
        size_t l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_fragm_pkt, l_fragm_dec_size);

        debug_if(s_dump_packet_headers, L_DEBUG, "FRAG: dap_stream_pkt_read_unsafe returned l_dec_pkt_size=%zu (expected_min=%zu)",
                 l_dec_pkt_size, sizeof(dap_stream_fragment_pkt_t));

        if(l_dec_pkt_size == 0) {
            debug_if(s_dump_packet_headers, L_WARNING, "Input: can't decode packet size = %zu (stream=%p)", a_pkt_size, a_stream);
            l_is_clean_fragments = true;
            break;
        }
        if(l_dec_pkt_size != l_fragm_pkt->size + sizeof(dap_stream_fragment_pkt_t)) {
            debug_if(s_dump_packet_headers, L_WARNING, "Input: decoded packet has bad size = %zu, decoded size = %zu",
                     l_fragm_pkt->size + sizeof(dap_stream_fragment_pkt_t), l_dec_pkt_size);
            l_is_clean_fragments = true;
            break;
        }

        debug_if(s_dump_packet_headers, L_INFO, "Fragment decoded: size=%u mem_shift=%u filled=%zu", 
               l_fragm_pkt->size, l_fragm_pkt->mem_shift, a_stream->buf_fragments_size_filled);

        if (!l_fragm_pkt->full_size ||
            l_fragm_pkt->full_size > DAP_STREAM_PKT_SIZE_MAX ||
            (size_t)l_fragm_pkt->mem_shift + (size_t)l_fragm_pkt->size > l_fragm_pkt->full_size) {
            debug_if(s_dump_packet_headers, L_WARNING,
                     "Input: invalid fragment bounds shift=%u size=%u full=%u",
                     l_fragm_pkt->mem_shift, l_fragm_pkt->size, l_fragm_pkt->full_size);
            l_is_clean_fragments = true;
            break;
        }

        if (!a_stream->buf_fragments || !a_stream->buf_fragments_map ||
            a_stream->buf_fragments_size_total != l_fragm_pkt->full_size) {
            s_stream_fragments_reset(a_stream);
            a_stream->buf_fragments = DAP_NEW_Z_SIZE(uint8_t, l_fragm_pkt->full_size);
            a_stream->buf_fragments_map = DAP_NEW_Z_SIZE(uint8_t, l_fragm_pkt->full_size);
            if (!a_stream->buf_fragments || !a_stream->buf_fragments_map) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                l_is_clean_fragments = true;
                break;
            }
            a_stream->buf_fragments_size_total = l_fragm_pkt->full_size;
            debug_if(s_dump_packet_headers, L_DEBUG,
                     "Input: started fragment assembly (full=%u, first_shift=%u)",
                     l_fragm_pkt->full_size, l_fragm_pkt->mem_shift);
        }

        memcpy(a_stream->buf_fragments + l_fragm_pkt->mem_shift, l_fragm_pkt->data, l_fragm_pkt->size);
        size_t l_new_bytes = 0;
        size_t l_begin = l_fragm_pkt->mem_shift;
        size_t l_end = l_begin + l_fragm_pkt->size;
        for (size_t i = l_begin; i < l_end; i++) {
            if (!a_stream->buf_fragments_map[i]) {
                a_stream->buf_fragments_map[i] = 1;
                l_new_bytes++;
            }
        }
        a_stream->buf_fragments_size_filled += l_new_bytes;

        // Not last fragment, otherwise go to parsing STREAM_PKT_TYPE_DATA_PACKET
        if(a_stream->buf_fragments_size_filled < l_fragm_pkt->full_size) {
            debug_if(s_debug, L_DEBUG, "Fragment not complete yet: filled=%zu full=%u", 
                   a_stream->buf_fragments_size_filled, l_fragm_pkt->full_size);
            break;
        }
        // All fragments collected, move forward
        debug_if(s_debug, L_INFO, "All fragments collected! Falling through to DATA_PACKET processing");
    }
    case STREAM_PKT_TYPE_DATA_PACKET: {
        dap_stream_ch_pkt_t *l_ch_pkt;
        size_t l_dec_pkt_size;

        debug_if(s_debug, L_INFO, "Processing DATA_PACKET: from_fragment=%s", 
               (a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) ? "yes" : "no");

        if (a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) {
            l_ch_pkt = (dap_stream_ch_pkt_t*)a_stream->buf_fragments;
            l_dec_pkt_size = a_stream->buf_fragments_size_total;
        } else {
            size_t l_pkt_dec_size = dap_enc_decode_out_size(a_stream->session->key, a_pkt->hdr.size, DAP_ENC_DATA_TYPE_RAW);
            a_stream->pkt_cache = DAP_NEW_Z_SIZE(byte_t, l_pkt_dec_size);
            l_ch_pkt = (dap_stream_ch_pkt_t*)a_stream->pkt_cache;
            l_dec_pkt_size = dap_stream_pkt_read_unsafe(a_stream, a_pkt, l_ch_pkt, l_pkt_dec_size);
            
            debug_if(s_dump_packet_headers, L_INFO, 
                     "DATA_PACKET decryption: key=%p, encrypted_size=%u, expected_dec=%zu, actual_dec=%zu",
                     a_stream->session->key, a_pkt->hdr.size, l_pkt_dec_size, l_dec_pkt_size);
        }

        if (l_dec_pkt_size < sizeof(l_ch_pkt->hdr)) {
            log_it(L_WARNING, "Input: decoded size %zu is lesser than size of packet header %zu", l_dec_pkt_size, sizeof(l_ch_pkt->hdr));
            l_is_clean_fragments = true;
            break;
        }
        if (l_dec_pkt_size != l_ch_pkt->hdr.data_size + sizeof(l_ch_pkt->hdr)) {
            log_it(L_WARNING, "Input: decoded packet BAD SIZE: expected_dec=%zu (hdr.data_size=%u + hdr_size=%zu), actual_dec=%zu",
                   l_ch_pkt->hdr.data_size + sizeof(l_ch_pkt->hdr), l_ch_pkt->hdr.data_size, sizeof(l_ch_pkt->hdr), l_dec_pkt_size);
            l_is_clean_fragments = true;
            break;
        }

        // If seq_id is less than previous - doomp eet
        if (!s_detect_loose_packet(a_stream)) {
            dap_stream_ch_t * l_ch = NULL;
            
            debug_if(s_dump_packet_headers, L_INFO, "Looking for channel '%c' (0x%02x) in stream (channel_count=%zu)", 
                   (char)l_ch_pkt->hdr.id, l_ch_pkt->hdr.id, a_stream->channel_count);
            
            // DEBUG: Check for duplicate channels
            size_t l_duplicates = 0;
            for(size_t j=0;j<a_stream->channel_count;j++){
                if(a_stream->channel[j]->proc && a_stream->channel[j]->proc->id == l_ch_pkt->hdr.id) {
                    debug_if(s_dump_packet_headers, L_DEBUG, "s_stream_proc_pkt_in: channel '%c' at index %zu: %p (notifiers=%"PRIu64")",
                           (char)l_ch_pkt->hdr.id, j, a_stream->channel[j], 
                           dap_list_length(a_stream->channel[j]->packet_in_notifiers));
                    l_duplicates++;
                }
            }
            if (l_duplicates > 1) {
                log_it(L_ERROR, "s_stream_proc_pkt_in: Found %zu DUPLICATE channels for ID '%c'!", 
                       l_duplicates, (char)l_ch_pkt->hdr.id);
            }
            
            for(size_t i=0;i<a_stream->channel_count;i++){
                if(a_stream->channel[i]->proc){
                    if(a_stream->channel[i]->proc->id == l_ch_pkt->hdr.id ){
                        l_ch=a_stream->channel[i];
                        break;
                    }
                }
            }
            if(l_ch) {
                l_ch->stat.bytes_read += l_ch_pkt->hdr.data_size;
                if(l_ch->proc && l_ch->proc->packet_in_callback) {
                    debug_if(s_dump_packet_headers, L_INFO, "Calling channel '%c' packet_in_callback: data_size=%u type=0x%02X",
                           (char)l_ch_pkt->hdr.id, l_ch_pkt->hdr.data_size, l_ch_pkt->hdr.type);
                    
                    bool l_security_check_passed = l_ch->proc->packet_in_callback(l_ch, l_ch_pkt);
                    if (s_stream_esocket_is_detached(l_es))
                        goto cleanup;
                    debug_if(s_dump_packet_headers, L_INFO, "Income channel packet: id='%c' size=%u type=0x%02X seq_id=0x%016"
                                                            DAP_UINT64_FORMAT_X" enc_type=0x%02X (stream=%p)", (char)l_ch_pkt->hdr.id,
                                                            l_ch_pkt->hdr.data_size, l_ch_pkt->hdr.type, l_ch_pkt->hdr.seq_id, l_ch_pkt->hdr.enc_type,
                                                            a_stream);
                    
                    // DEBUG: Check notifiers status
                    size_t l_notifier_count = dap_list_length(l_ch->packet_in_notifiers);
                    debug_if(s_dump_packet_headers, L_INFO, 
                             "Channel '%c' (%p) notifiers: count=%zu, security_passed=%s, closing=%s",
                             (char)l_ch->proc->id, l_ch, l_notifier_count,
                             l_security_check_passed ? "YES" : "NO",
                             l_ch->closing ? "YES" : "NO");
                    
                    for (dap_list_t *it = l_ch->packet_in_notifiers; !l_ch->closing && it && l_security_check_passed; it = it->next) {
                        dap_stream_ch_notifier_t *l_notifier = it->data;
                        assert(l_notifier);
                        l_notifier->callback(l_ch, l_ch_pkt->hdr.type, l_ch_pkt->data, l_ch_pkt->hdr.data_size, l_notifier->arg);
                        if (s_stream_esocket_is_detached(l_es))
                            goto cleanup;
                    }
                    if (l_ch->closing)
                        break;
                }
            } else{
                log_it(L_WARNING, "Input: unprocessed channel packet id '%c'",(char) l_ch_pkt->hdr.id );
            }
        }
        // packet already defragmented
        if(a_pkt->hdr.type == STREAM_PKT_TYPE_FRAGMENT_PACKET) {
            l_is_clean_fragments = true;
        }
    } break;
    case STREAM_PKT_TYPE_SERVICE_PACKET: {
        if (a_pkt_size != sizeof(dap_stream_pkt_t) + sizeof(dap_stream_srv_pkt_t)) {
            log_it(L_WARNING, "Input: incorrect service packet size %zu, estimated %zu", a_pkt_size - sizeof(dap_stream_pkt_t), sizeof(dap_stream_srv_pkt_t));
            break;
        }
        dap_stream_srv_pkt_t *l_srv_pkt = (dap_stream_srv_pkt_t *)a_pkt->data;
        uint32_t l_session_id = l_srv_pkt->session_id;
        if (a_stream->esocket)
            s_check_session(l_session_id, a_stream->esocket);
    } break;
    case STREAM_PKT_TYPE_KEEPALIVE: {
        debug_if(s_debug, L_DEBUG, "Keep alive check recieved");
        dap_stream_pkt_hdr_t l_ret_pkt = {
            .type = STREAM_PKT_TYPE_ALIVE
        };
        memcpy(l_ret_pkt.sig, c_dap_stream_sig, sizeof(c_dap_stream_sig));
        if (a_stream->trans_ctx) {
            dap_stream_send_unsafe(a_stream, &l_ret_pkt, sizeof(l_ret_pkt));
        }
        // Reset client keepalive timer
        if (a_stream->keepalive_timer) {
            dap_timerfd_reset_unsafe(a_stream->keepalive_timer);
        }
    } break;
    case STREAM_PKT_TYPE_ALIVE:
        a_stream->is_active = false; // To prevent keep-alive concurrency
        debug_if(s_debug, L_DEBUG, "Keep alive response recieved");
        break;
    default:
        log_it(L_WARNING, "Unknown header type");
    }
cleanup:
    // Clean memory
    DAP_DEL_Z(a_stream->pkt_cache);
    if(l_is_clean_fragments) {
        s_stream_fragments_reset(a_stream);
    }
}

/**
 * @brief _detect_loose_packet
 * @param a_stream
 * @return
 */
static bool s_detect_loose_packet(dap_stream_t * a_stream) {
    dap_stream_ch_pkt_t *l_ch_pkt = a_stream->buf_fragments_size_filled
            ? (dap_stream_ch_pkt_t*)a_stream->buf_fragments
            : (dap_stream_ch_pkt_t*)a_stream->pkt_cache;

    long long l_count_lost_packets =
            l_ch_pkt->hdr.seq_id || a_stream->client_last_seq_id_packet
            ? (long long) l_ch_pkt->hdr.seq_id - (long long) (a_stream->client_last_seq_id_packet + 1)
            : 0;

    if (l_count_lost_packets) {
        if (l_count_lost_packets > 0)
            log_it(L_WARNING, "Packet loss detected. Current seq_id: %"DAP_UINT64_FORMAT_U", last seq_id: %zu",
                l_ch_pkt->hdr.seq_id, a_stream->client_last_seq_id_packet);
        else
            log_it(L_WARNING, "Packet replay detected, seq_id: %"DAP_UINT64_FORMAT_U, l_ch_pkt->hdr.seq_id);
    }
    debug_if(s_debug, L_DEBUG, "Current seq_id: %"DAP_UINT64_FORMAT_U", last: %zu",
                                l_ch_pkt->hdr.seq_id, a_stream->client_last_seq_id_packet);
    a_stream->client_last_seq_id_packet = l_ch_pkt->hdr.seq_id;
    return l_count_lost_packets < 0;
}

dap_stream_t *dap_stream_get_from_es(dap_events_socket_t *a_es)
{
    if (!a_es)
        return NULL;
    if (a_es->server) {
        // Server-side: unified trans_ctx approach
        dap_net_trans_ctx_t *l_trans_ctx = (dap_net_trans_ctx_t *)a_es->_inheritor;
        return l_trans_ctx ? l_trans_ctx->stream : NULL;
    } else {
        // Client-side: dap_client hierarchy
        dap_client_t *l_client = DAP_ESOCKET_CLIENT(a_es);
        if (l_client) {
            dap_client_fsm_t *l_fsm = DAP_CLIENT_FSM(l_client);
            dap_net_trans_ctx_t *l_tc = l_fsm ? l_fsm->trans_ctx : NULL;
            if (l_tc)
                return l_tc->stream;
        }
        // Transport fallback (UDP and others may keep trans_ctx in callbacks.arg
        // and leave _inheritor for client infra). Resolve by esocket UUID.
        dap_stream_t *l_stream = NULL;
        pthread_rwlock_rdlock(&s_streams_lock);
        l_stream = s_stream_index_get_by_es_uuid_unsafe(a_es->uuid);
        pthread_rwlock_unlock(&s_streams_lock);
        return l_stream;
    }
}

/**
 * @brief s_callback_keepalive
 * @param a_arg
 * @return
 */
static bool s_callback_keepalive(void *a_arg, bool a_server_side)
{
    if (!a_arg)
        return false;
    s_keepalive_timer_arg_t *l_timer_arg = (s_keepalive_timer_arg_t*)a_arg;
    dap_events_socket_uuid_t l_es_uuid = l_timer_arg->esocket_uuid;
    dap_worker_t * l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_ERROR, "l_worker is NULL");
        return s_keepalive_timer_self_stop(l_timer_arg);
    }
    dap_events_socket_t * l_es = dap_context_find(l_worker->context, l_es_uuid);
    if(l_es) {
        assert(a_server_side == !!l_es->server);
        dap_stream_t *l_stream = dap_stream_get_from_es(l_es);
        if (!l_stream) {
            log_it(L_WARNING, "Keepalive %s: esocket uuid 0x%016"DAP_UINT64_FORMAT_x" found but stream detached on worker #%u — timer stopped",
                   a_server_side ? "srv" : "cli", l_es_uuid, l_worker->id);
            return s_keepalive_timer_self_stop(l_timer_arg);
        }
        if (a_server_side) {
            int l_pending = 0, l_sockerr = 0;
            socklen_t l_sockerr_len = sizeof(l_sockerr);
#ifdef DAP_OS_LINUX
            ioctl(l_es->fd, FIONREAD, &l_pending);
#endif
            getsockopt(l_es->fd, SOL_SOCKET, SO_ERROR, &l_sockerr, &l_sockerr_len);
            log_it(L_WARNING, "KEEPALIVE_CHECK: fd=%d uuid=0x%"DAP_UINT64_FORMAT_x
                   " flags=0x%x pending_in=%d so_err=%d buf_in=%zu buf_out=%zu"
                   " READY_READ=%d read_cb=%p active=%d trans=%p session=%p",
                   l_es->fd, l_es->uuid, l_es->flags,
                   l_pending, l_sockerr, l_es->buf_in_size, l_es->buf_out_size,
                   !!(l_es->flags & DAP_SOCK_READY_TO_READ),
                   (void*)l_es->callbacks.read_callback,
                   l_stream->is_active,
                   l_stream->trans_ctx ? (void*)l_stream->trans_ctx->trans : NULL,
                   (void*)l_stream->session);
        }
        if (l_stream->is_active) {
            l_stream->is_active = false;
            return true;
        }
        if (!l_stream->trans_ctx || !l_stream->trans_ctx->trans ||
            !l_stream->trans_ctx->trans->ops || !l_stream->trans_ctx->trans->ops->write ||
            !l_stream->session || !l_stream->session->key) {
            return true;
        }
        debug_if(s_debug_more, L_DEBUG,"Keepalive %s sock %"DAP_FORMAT_SOCKET" uuid 0x%016"DAP_UINT64_FORMAT_x,
                 a_server_side ? "srv" : "cli", l_es->socket, l_es_uuid);
        dap_stream_pkt_hdr_t l_pkt = {};
        l_pkt.type = STREAM_PKT_TYPE_KEEPALIVE;
        memcpy(l_pkt.sig, c_dap_stream_sig, sizeof(l_pkt.sig));
        dap_stream_send_unsafe(l_stream, &l_pkt, sizeof(l_pkt));
        return true;
    }else{
        debug_if(s_debug_more, L_INFO,"Keepalive for sock uuid %016"DAP_UINT64_FORMAT_x" removed", l_es_uuid);
        return s_keepalive_timer_self_stop(l_timer_arg);
    }
}

static bool s_callback_client_keepalive(void *a_arg)
{
    return s_callback_keepalive(a_arg, false);
}

static bool s_callback_server_keepalive(void *a_arg)
{
    return s_callback_keepalive(a_arg, true);
}

int s_stream_add_to_hashtable(dap_stream_t *a_stream)
{
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: entering, stream=%p", (void*)a_stream);
    dap_stream_t *l_double = NULL;
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: searching for duplicate");
    HASH_FIND(hh, s_authorized_streams, &a_stream->node, sizeof(a_stream->node), l_double);
    if (l_double) {
        debug_if(s_debug_more, L_DEBUG, "Stream already present in hash table for node "NODE_ADDR_FP_STR"", NODE_ADDR_FP_ARGS_S(a_stream->node));
        return -1;
    }
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: no duplicate found, setting primary=true");
    a_stream->primary = true;
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: adding to hash table");
    HASH_ADD(hh, s_authorized_streams, node, sizeof(a_stream->node), a_stream);
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: added to hash, calling dap_cluster_member_add");
    dap_cluster_member_add(s_global_links_cluster, &a_stream->node, 0, NULL); // Used own rwlock for this cluster members
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: dap_cluster_member_add completed, calling dap_link_manager_stream_add");
    dap_link_manager_stream_add(&a_stream->node, a_stream->is_client_to_uplink);
    debug_if(s_debug, L_DEBUG, "s_stream_add_to_hashtable: completed successfully");
    return 0;
}

void s_stream_delete_from_list(dap_stream_t *a_stream)
{
    dap_return_if_fail(a_stream);
    int lock = pthread_rwlock_wrlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !");
    s_stream_index_remove_by_stream_unsafe(a_stream);

    // Check if stream is in the list (prev is set by DL_APPEND/DL_DELETE)
    // Client-side streams may never be added if worker_assign didn't fire
    dap_stream_t *l_stream = NULL;
    bool l_in_list = false;
    DL_FOREACH(s_streams, l_stream) {
        if (l_stream == a_stream) {
            l_in_list = true;
            break;
        }
    }
    l_stream = NULL;
    if (l_in_list)
        DL_DELETE(s_streams, a_stream);
    if (a_stream->authorized) {
        // It's an authorized stream, try to replace it in hastable
        if (a_stream->primary)
            HASH_DEL(s_authorized_streams, a_stream);
        DL_FOREACH(s_streams, l_stream)
            if (l_stream->node.uint64 == a_stream->node.uint64)
                break;
        if (l_stream) {
            s_stream_add_to_hashtable(l_stream);
            dap_link_manager_stream_replace(&a_stream->node, l_stream->is_client_to_uplink);
        } else {
            dap_cluster_member_delete(s_global_links_cluster, &a_stream->node);
            dap_link_manager_stream_delete(&a_stream->node); // Used own rwlock for this cluster members
        }
    }
    pthread_rwlock_unlock(&s_streams_lock);
}

int dap_stream_add_to_list(dap_stream_t *a_stream)
{
    dap_return_val_if_fail(a_stream, -1);
    int l_ret = 0;
    int lock = pthread_rwlock_wrlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), -666;
    dap_stream_t *l_tmp = NULL;
    DL_FOREACH(s_streams, l_tmp) {
        if (l_tmp == a_stream) {
            s_stream_index_add_unsafe(a_stream);
            pthread_rwlock_unlock(&s_streams_lock);
            return 0;
        }
    }
    DL_APPEND(s_streams, a_stream);
    s_stream_index_add_unsafe(a_stream);
    if (a_stream->authorized)
        l_ret = s_stream_add_to_hashtable(a_stream);
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

/**
 * @brief dap_stream_find_by_addr find a_stream with current node
 * @param a_addr - autorrized node address
 * @param a_worker - pointer to worker
 * @return  esocket_uuid if ok 0 if not
 */
dap_events_socket_uuid_t dap_stream_find_by_addr(dap_stream_node_addr_t *a_addr, dap_worker_t **a_worker)
{
    dap_return_val_if_fail(a_addr && a_addr->uint64, 0);
    dap_stream_t *l_auth_stream = NULL;
    dap_events_socket_uuid_t l_ret = 0;
    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), 0;

    HASH_FIND(hh, s_authorized_streams, a_addr, sizeof(*a_addr), l_auth_stream);
    if (l_auth_stream) {
        if (a_worker)
            *a_worker = l_auth_stream->stream_worker->worker;
        if (l_auth_stream->esocket)
            l_ret = l_auth_stream->esocket_uuid;
    } else if (a_worker)
        *a_worker = NULL;
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

dap_list_t *dap_stream_find_all_by_addr(dap_stream_node_addr_t *a_addr)
{
    dap_list_t *l_ret = NULL;
    dap_return_val_if_fail(a_addr, l_ret);
    dap_stream_t *l_stream;

    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), NULL;

    DL_FOREACH(s_streams, l_stream) {
        if (!l_stream->authorized || a_addr->uint64 != l_stream->node.uint64)
            continue;
        dap_events_socket_uuid_ctrl_t *l_ret_item = DAP_NEW(dap_events_socket_uuid_ctrl_t);
        if (!l_ret_item) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            dap_list_free_full(l_ret, NULL);
            return NULL;
        }
        l_ret_item->worker = l_stream->stream_worker->worker;
        if (l_stream->esocket)
            l_ret_item->uuid = l_stream->esocket_uuid;
        l_ret = dap_list_append(l_ret, l_ret_item);
    }
    pthread_rwlock_unlock(&s_streams_lock);
    return l_ret;
}

/**
 * @brief dap_stream_node_addr_from_sign create dap_stream_node_addr_t from dap_sign_t, need memory free
 * @param a_hash - pointer to hash_fast_t
 * @return  pointer if ok NULL if not
 */
dap_stream_node_addr_t dap_stream_node_addr_from_sign(dap_sign_t *a_sign)
{
    dap_stream_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_sign, l_ret);

    dap_hash_fast_t l_node_addr_hash;
    if ( dap_sign_get_pkey_hash(a_sign, &l_node_addr_hash) )
        dap_stream_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}

dap_stream_node_addr_t dap_stream_node_addr_from_cert(dap_cert_t *a_cert)
{
    dap_stream_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_cert, l_ret);

    // Get certificate public key hash
    dap_hash_fast_t l_node_addr_hash;
    if ( !dap_cert_get_pkey_hash(a_cert, &l_node_addr_hash) )
        dap_stream_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}

dap_stream_node_addr_t dap_stream_node_addr_from_pkey(dap_pkey_t *a_pkey)
{
    dap_stream_node_addr_t l_ret = { };
    dap_return_val_if_pass(!a_pkey, l_ret);

    // Get certificate public key hash
    dap_hash_fast_t l_node_addr_hash;
    if ( dap_pkey_get_hash(a_pkey, &l_node_addr_hash) )
        dap_stream_node_addr_from_hash(&l_node_addr_hash, &l_ret);
    return l_ret;
}

static void s_stream_fill_info(dap_stream_t *a_stream, dap_stream_info_t *a_out_info)
{
    a_out_info->node_addr = a_stream->node;
    // Use cached remote address from trans_ctx — safe for cross-thread access,
    // never dereference esocket pointer outside its worker context
    if (a_stream->trans_ctx && a_stream->trans_ctx->remote_addr_str[0]) {
        a_out_info->remote_addr_str = dap_strdup_printf("%-*s", INET_ADDRSTRLEN - 1, a_stream->trans_ctx->remote_addr_str);
        a_out_info->remote_port = a_stream->trans_ctx->remote_port;
    }
    a_out_info->channels = DAP_NEW_Z_SIZE_RET_IF_FAIL(char, a_stream->channel_count + 1, a_out_info->remote_addr_str);
    for (size_t i = 0; i < a_stream->channel_count; i++)
        a_out_info->channels[i] = a_stream->channel[i]->proc->id;
    a_out_info->total_packets_sent = a_stream->seq_id;
    a_out_info->is_uplink = a_stream->is_client_to_uplink;
}

dap_stream_info_t *dap_stream_get_links_info(dap_cluster_t *a_cluster, size_t *a_count)
{
    dap_return_val_if_pass(!a_cluster && !s_streams, NULL);
    int lock = pthread_rwlock_rdlock(&s_streams_lock);
    assert(lock != EDEADLK);
    if ( lock == EDEADLK )
        return log_it(L_CRITICAL, "! Attempt to aquire streams lock recursively !"), NULL;

    dap_stream_t *it = NULL;
    size_t l_streams_count = 0, i = 0;
    if (a_cluster) {
        pthread_rwlock_rdlock(&a_cluster->members_lock);
        l_streams_count = HASH_COUNT(a_cluster->members);
    } else
        DL_COUNT(s_streams, it, l_streams_count);
    if (!l_streams_count) {
        if(a_cluster)
            pthread_rwlock_unlock(&a_cluster->members_lock);
        pthread_rwlock_unlock(&s_streams_lock);
        return NULL;
    }
    dap_stream_info_t *l_ret = DAP_NEW_Z_COUNT(dap_stream_info_t, l_streams_count);
    if (!l_ret) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        if (a_cluster)
            pthread_rwlock_unlock(&a_cluster->members_lock);
        pthread_rwlock_unlock(&s_streams_lock);
        return NULL;
    }
    if (a_cluster) {
        for (dap_cluster_member_t *l_member = a_cluster->members; l_member; l_member = l_member->hh.next) {
            HASH_FIND(hh, s_authorized_streams, &l_member->addr, sizeof(l_member->addr), it);
            if (!it) {
                log_it(L_ERROR, "Link cluster contains member " NODE_ADDR_FP_STR " not found in streams HT", NODE_ADDR_FP_ARGS_S(l_member->addr));
                continue;
            }
            assert(it->node.uint64 == l_member->addr.uint64);
            s_stream_fill_info(it, l_ret + i++);
        }
        pthread_rwlock_unlock(&a_cluster->members_lock);
    } else {
        DL_FOREACH(s_streams, it)
            s_stream_fill_info(it, l_ret + i++);
    }
    pthread_rwlock_unlock(&s_streams_lock);
    if (a_count)
        *a_count = i;
    return l_ret;
}

void dap_stream_delete_links_info(dap_stream_info_t *a_info, size_t a_count)
{
    dap_return_if_fail(a_info && a_count);
    for (size_t i = 0; i < a_count; i++) {
        dap_stream_info_t *it = a_info + i;
        DAP_DEL_Z(it->remote_addr_str);
        DAP_DEL_Z(it->channels);
    }
    DAP_DELETE(a_info);
}
