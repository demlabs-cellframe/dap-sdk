/*
* Authors:
* Dmitrii Gerasimov <naeper@demlabs.net>
* DeM Labs Inc.   https://demlabs.net
* Cellframe https://cellframe.net
* Copyright  (c) 2017-2020
* All rights reserved.

This file is part of DAP the open source project.

DAP is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

DAP is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

See more details here <http://www.gnu.org/licenses/>.
*/
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#ifndef _WIN32
#include <sys/queue.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <pthread.h>
#include "utlist.h"
#include "json_object.h"
#include "dap_common.h"
#include "dap_context.h"
#include "dap_worker.h"
#include "dap_events.h"
#include "dap_strfuncs.h"
#include "dap_proc_thread.h"
#include "dap_http_server.h"
#include "dap_http_client.h"
#include "dap_http_simple.h"
#include "dap_http_user_agent.h"
#include "dap_context.h"
#include "http_status_code.h"

#define LOG_TAG "dap_http_simple"

static void s_http_client_new( dap_http_client_t *a_http_client, void *arg );
static void s_http_client_delete( dap_http_client_t *a_http_client, void *arg );
static void s_http_simple_delete( dap_http_simple_t *a_http_simple);

static void s_http_client_headers_read( dap_http_client_t *cl_ht, void *arg );
static void s_http_client_data_read( dap_http_client_t * cl_ht, void *arg );
static bool s_http_client_headers_write(dap_http_client_t *cl_ht, void *arg);
static bool s_http_client_data_write( dap_http_client_t * a_http_client, void *a_arg );
static bool s_proc_queue_callback(void *a_arg );

typedef struct dap_http_simple_url_proc {
  dap_http_simple_callback_t proc_callback;
  size_t reply_size_max;
} dap_http_simple_url_proc_t;


typedef struct user_agents_item {
  dap_http_user_agent_ptr_t user_agent;
  struct user_agents_item *next;
} user_agents_item_t;

static user_agents_item_t *user_agents_list = NULL;
static int is_unknown_user_agents_pass = 0;

#define DAP_HTTP_SIMPLE_URL_PROC(a) ((dap_http_simple_url_proc_t*) (a)->_inheritor)

static void s_free_user_agents_list( void );

int dap_http_simple_module_init( )
{
    return 0;
}

void dap_http_simple_module_deinit( void )
{
    s_free_user_agents_list( );
}

/**
 * @brief dap_http_simple_proc_add Add simple HTTP processor
 * @param a_http HTTP server instance
 * @param a_url_path URL path
 * @param a_reply_size_max Maximum reply size
 * @param a_callback Callback for data processing
 */
struct dap_http_url_proc * dap_http_simple_proc_add( dap_http_server_t *a_http, const char *a_url_path, size_t a_reply_size_max, dap_http_simple_callback_t a_callback )
{
    dap_http_simple_url_proc_t *l_url_proc = DAP_NEW_Z( dap_http_simple_url_proc_t );
    if (!l_url_proc) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }

    l_url_proc->proc_callback = a_callback;
    l_url_proc->reply_size_max = a_reply_size_max;

    return dap_http_add_proc( a_http, a_url_path,
                     l_url_proc, // Internal structure
                     s_http_client_new, // Contrustor
                     s_http_client_delete, //  Destructor
                     s_http_client_headers_read, s_http_client_headers_write, // Headers read, write
                     s_http_client_data_read, s_http_client_data_write, // Data read, write
                     NULL); // errror
}

static void s_free_user_agents_list()
{
user_agents_item_t *elt, *tmp;

    LL_FOREACH_SAFE( user_agents_list, elt, tmp )
    {
        LL_DELETE( user_agents_list, elt );
        dap_http_user_agent_delete( elt->user_agent );
        free( elt );
    }
}

static int s_is_user_agent_supported( const char *user_agent )
{
  int result = is_unknown_user_agents_pass;
  dap_http_user_agent_ptr_t find_agent = dap_http_user_agent_new_from_str( user_agent );

  if ( find_agent == NULL )
    return result;

  const char* find_agent_name = dap_http_user_agent_get_name( find_agent );

  user_agents_item_t *elt;
  LL_FOREACH( user_agents_list, elt ) {

    const char* user_agent_name = dap_http_user_agent_get_name( elt->user_agent );

    if ( strcmp(find_agent_name, user_agent_name) == 0) {
      if(dap_http_user_agent_versions_compare(find_agent, elt->user_agent) >= 0) {
        result = true;
        goto END;
      }
      else {
        result = false;
        goto END;
      }
    }
  }

END:
  dap_http_user_agent_delete( find_agent );
  return result;
}


int dap_http_simple_set_supported_user_agents( const char *user_agents, ... )
{
  va_list argptr;
  va_start( argptr, user_agents );

  const char* str = user_agents;

//  log_it(L_DEBUG,"dap_http_simple_set_supported_user_agents");
//  Sleep(300);

  while ( str != NULL )
  {
    dap_http_user_agent_ptr_t user_agent = dap_http_user_agent_new_from_str( str );

    if ( user_agent == NULL ) {
      log_it(L_ERROR, "Can't parse user agent string");
      va_end(argptr);
       s_free_user_agents_list();
       return 0;
    }

    user_agents_item_t *item = calloc( 1, sizeof (user_agents_item_t) );
    if (!item) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        va_end(argptr);
        s_free_user_agents_list();
        return 0;
    }

    item->user_agent = user_agent;
    LL_APPEND( user_agents_list, item );

    str = va_arg( argptr, const char * );
  }

  va_end( argptr );

  return 1;
}

// if this function was called. We checking version only supported user-agents
// other will pass automatically ( and request with without user-agents field too )
void dap_http_simple_set_pass_unknown_user_agents(int pass)
{
    is_unknown_user_agents_pass = pass;
}

static void s_esocket_worker_write_callback(void *a_arg)
{
    dap_http_simple_t *l_http_simple = (dap_http_simple_t*)a_arg;
    dap_worker_t *l_worker = dap_worker_get_current();
    if (!l_worker) {
        log_it(L_ERROR, "l_worker is NULL");
        return;
    }
    dap_events_socket_t *l_es = dap_context_find(l_worker->context, l_http_simple->esocket_uuid);
    if (!l_es) {
        debug_if(g_debug_reactor, L_INFO, "Esocket 0x%"DAP_UINT64_FORMAT_x" is already deleted", l_http_simple->esocket_uuid);
        dap_http_client_t *l_http_client = l_http_simple->http_client;
        if (l_http_client) {
            while (l_http_client->in_headers)
                dap_http_header_remove(&l_http_client->in_headers, l_http_client->in_headers);
            while (l_http_client->out_headers)
                dap_http_header_remove(&l_http_client->out_headers, l_http_client->out_headers);
            DAP_DELETE(l_http_client);
        }
        DAP_DEL_Z(l_http_simple->request);
        DAP_DEL_Z(l_http_simple->reply);
        DAP_DELETE(l_http_simple);
        return;
    }
    l_es->_inheritor = l_http_simple->http_client; // Back to the owner
    dap_http_client_write(l_http_simple->http_client);
}

inline static void s_write_data_to_socket(dap_http_simple_t *a_simple)
{
    dap_worker_exec_callback_on(dap_events_worker_get(a_simple->worker->id), s_esocket_worker_write_callback, a_simple);
}

static bool s_http_client_headers_write(dap_http_client_t *cl_ht, void *a_arg)
{
    dap_http_simple_t *l_hs = DAP_HTTP_SIMPLE(cl_ht);
    assert(a_arg == l_hs);
    if (!l_hs)
        return false;

    for (dap_http_header_t *i = l_hs->ext_headers; i; i = i->next) {
        dap_http_out_header_add(cl_ht, i->name, i->value);
        log_it(L_DEBUG, "Added http header. %s: %s", i->name, i->value);
    }
    
    return !l_hs->generate_default_header;
}


static bool s_http_client_data_write(dap_http_client_t * a_http_client, void *a_arg)
{
    dap_http_simple_t *l_http_simple = DAP_HTTP_SIMPLE( a_http_client );
    assert(l_http_simple == a_arg);
    if (!a_arg)
        return false;
    l_http_simple->reply_sent += dap_events_socket_write_unsafe(l_http_simple->esocket,
                                              l_http_simple->reply_byte + l_http_simple->reply_sent,
                                              l_http_simple->http_client->out_content_length - l_http_simple->reply_sent);
    if (l_http_simple->reply_sent >= a_http_client->out_content_length) {
        log_it(L_INFO, "All the reply (%zu) is sent out", a_http_client->out_content_length);
        a_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return false;
    }
    return true;
}



inline static void s_copy_reply_and_mime_to_response( dap_http_simple_t *a_simple )
{
//  log_it(L_DEBUG,"_copy_reply_and_mime_to_response");
//  Sleep(300);

    if( !a_simple->reply_size )
        return  log_it( L_WARNING, " cl_sh->reply_size equal 0" );

    a_simple->http_client->out_content_length = a_simple->reply_size;
    dap_strncpy( a_simple->http_client->out_content_type, a_simple->reply_mime, sizeof(a_simple->http_client->out_content_type) );
    return;
}

inline static void s_write_response_bad_request( dap_http_simple_t * a_http_simple,
                                               const char* error_msg )
{
    //  log_it(L_DEBUG,"_write_response_bad_request");
    //  Sleep(300);

    struct json_object *jobj = json_object_new_object( );
    json_object_object_add( jobj, "error", json_object_new_string(error_msg) );

    log_it( L_DEBUG, "error message %s",  json_object_to_json_string(jobj) );
    a_http_simple->http_client->reply_status_code = Http_Status_BadRequest;

    const char* json_str = json_object_to_json_string( jobj );
    dap_http_simple_reply(a_http_simple, (void*) json_str, (size_t) strlen(json_str) );

    dap_strncpy( a_http_simple->reply_mime, "application/json", sizeof(a_http_simple->reply_mime) );

    s_copy_reply_and_mime_to_response( a_http_simple );

    json_object_put( jobj ); // free obj
}

/**
 * @brief dap_http_simple_proc Execute procession callback and switch to write state
 * @param cl_sh HTTP simple client instance
 */
static bool s_proc_queue_callback(void *a_arg)
{
    dap_http_simple_t *l_http_simple = (dap_http_simple_t*) a_arg;
    log_it(L_DEBUG, "dap http simple proc");
    if (!l_http_simple->http_client) {
        log_it(L_ERROR, "[!] HTTP client is already deleted!");
        return false;
    }
    if (!l_http_simple->reply_byte) {
        log_it(L_ERROR, "[!] HTTP client is corrupted!");
        return false;
    }
    http_status_code_t return_code = (http_status_code_t)0;

    user_agents_item_t *l_tmp;
    int l_cnt = 0;
    LL_COUNT(user_agents_list, l_tmp, l_cnt);
    if (l_cnt) {
        dap_http_header_t *l_header = dap_http_header_find(l_http_simple->http_client->in_headers, "User-Agent");
        if (!l_header && !is_unknown_user_agents_pass) {
            const char l_error_msg[] = "Not found User-Agent HTTP header";
            s_write_response_bad_request(l_http_simple, l_error_msg);
            s_write_data_to_socket(l_http_simple);
            return false;
        }

        if (l_header && s_is_user_agent_supported(l_header->value) == false) {
            log_it(L_DEBUG, "Not supported user agent in request: %s", l_header->value);
            const char *l_error_msg = "User-Agent version not supported. Update your software";
            s_write_response_bad_request(l_http_simple, l_error_msg);
            s_write_data_to_socket(l_http_simple);
            return false;
        }
    }

    DAP_HTTP_SIMPLE_URL_PROC(l_http_simple->http_client->proc)->proc_callback(l_http_simple,&return_code);

    if(return_code) {
        log_it(L_DEBUG, "Request was processed well return_code=%d", return_code);
        l_http_simple->http_client->reply_status_code = (uint16_t)return_code;
        s_copy_reply_and_mime_to_response(l_http_simple);
    } else {
        log_it(L_ERROR, "Request was processed with ERROR");
        l_http_simple->http_client->reply_status_code = Http_Status_InternalServerError;
    }
    s_write_data_to_socket(l_http_simple);
    return false;
}

static void s_http_client_new(dap_http_client_t *a_http_client, UNUSED_ARG void *arg)
{
    a_http_client->_inheritor = DAP_NEW_Z(dap_http_simple_t);
    dap_http_simple_t *l_http_simple = DAP_HTTP_SIMPLE(a_http_client);
    *l_http_simple = (dap_http_simple_t) {
        .esocket        = a_http_client->esocket,
        .worker         = a_http_client->esocket->worker,
        .http_client    = a_http_client,
        .esocket_uuid   = a_http_client->esocket->uuid,
        .reply_byte     = DAP_NEW_Z_SIZE(uint8_t, DAP_HTTP_SIMPLE_URL_PROC(a_http_client->proc)->reply_size_max),
        .reply_size_max = DAP_HTTP_SIMPLE_URL_PROC(a_http_client->proc)->reply_size_max,
        .generate_default_header = true
    };
    dap_strncpy(l_http_simple->es_hostaddr, l_http_simple->esocket->remote_addr_str, INET6_ADDRSTRLEN);
    a_http_client->esocket->callbacks.arg = l_http_simple;
}

static void s_http_client_delete(dap_http_client_t *a_http_client, void *arg)
{
    dap_http_simple_t * l_http_simple = DAP_HTTP_SIMPLE(a_http_client);

    if (l_http_simple) {
        for (dap_http_header_t *i = l_http_simple->ext_headers; i ;i = i->next){
            dap_http_header_remove(&l_http_simple->ext_headers, i);
        }
        DAP_DEL_Z(l_http_simple->request);
        DAP_DEL_Z(l_http_simple->reply_byte);
        l_http_simple->http_client = NULL;
    }
}

static void s_http_client_headers_read( dap_http_client_t *a_http_client, void UNUSED_ARG *a_arg )
{
    dap_http_simple_t *l_http_simple = DAP_HTTP_SIMPLE(a_http_client);
//    Made a temporary solution to handle simple CORS requests.
//    This is necessary in order to be able to request information using JavaScript obtained from another source.
    if ( dap_http_header_find(a_http_client->in_headers, "Origin") ){
        dap_http_out_header_add(a_http_client, "Access-Control-Allow-Origin", "*");
    }

    if( a_http_client->in_content_length ) {
        // dbg if( a_http_client->in_content_length < 3){
        if( a_http_client->in_content_length > 0){
            l_http_simple->request_size_max = a_http_client->in_content_length + 1;
            l_http_simple->request = DAP_NEW_Z_SIZE(void, l_http_simple->request_size_max);
            if(!l_http_simple->request){
                l_http_simple->request_size_max = 0;
                log_it(L_ERROR, "Too big content-length %zu in request", a_http_client->in_content_length);
            }
        }
        else
            log_it(L_ERROR, "Not defined content-length %zu in request", a_http_client->in_content_length);
    } else {
        log_it( L_DEBUG, "No data section, execution proc callback" );
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        a_http_client->esocket->_inheritor = NULL;
        dap_proc_thread_callback_add_pri(l_http_simple->worker->proc_queue_input, s_proc_queue_callback, l_http_simple, DAP_QUEUE_MSG_PRIORITY_HIGH);

    }
}

void s_http_client_data_read( dap_http_client_t *a_http_client, void * a_arg )
{
    int *ret = (int *)a_arg;

    //log_it(L_DEBUG,"dap_http_simple_data_read");
    //  Sleep(300);

    dap_http_simple_t *l_http_simple = DAP_HTTP_SIMPLE(a_http_client);
    if(!l_http_simple){
        a_http_client->esocket->buf_in = 0;
        a_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        log_it( L_WARNING, "No http_simple object in read callback, close connection" );
        return;
    }

    size_t bytes_to_read = (a_http_client->esocket->buf_in_size + l_http_simple->request_size) < a_http_client->in_content_length ?
                            a_http_client->esocket->buf_in_size : ( a_http_client->in_content_length - l_http_simple->request_size );

    if( bytes_to_read ) {
        // Oops! The client sent more data than write in the CONTENT_LENGTH header
        if(l_http_simple->request_size + bytes_to_read > l_http_simple->request_size_max){
            log_it(L_WARNING, "Client sent more data length=%zu than in content-length=%zu in request", l_http_simple->request_size + bytes_to_read, a_http_client->in_content_length);
            l_http_simple->request_size_max = l_http_simple->request_size + bytes_to_read + 1;
            // increase input buffer
            byte_t *l_req_new = DAP_REALLOC((byte_t*)l_http_simple->request, l_http_simple->request_size_max);
            if (!l_req_new) {
                log_it(L_CRITICAL, "%s", c_error_memory_alloc);
                dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
                return;
            }
            l_http_simple->request = l_req_new;
        }
        if(l_http_simple->request){// request_byte=request
            memcpy( l_http_simple->request_byte + l_http_simple->request_size, a_http_client->esocket->buf_in, bytes_to_read );
            l_http_simple->request_size += bytes_to_read;
        }
    }
    *ret = (int) a_http_client->esocket->buf_in_size;
    if( l_http_simple->request_size >= a_http_client->in_content_length ) {

        // bool isOK=true;
        log_it( L_INFO,"Data for http_simple_request collected" );
        dap_events_socket_set_readable_unsafe(a_http_client->esocket, false);
        a_http_client->esocket->_inheritor = NULL;
        dap_proc_thread_callback_add( l_http_simple->worker->proc_queue_input , s_proc_queue_callback, l_http_simple);
    }
}


/**
 * @brief dap_http_simple_reply Add data to the reply buffer
 * @param shs HTTP simple client instance
 * @param data
 * @param data_size
 */
size_t dap_http_simple_reply(dap_http_simple_t *a_http_simple, void *a_data, size_t a_data_size)
{
    size_t l_data_copy_size = (a_data_size > (a_http_simple->reply_size_max - a_http_simple->reply_size) ) ?
                                    (a_http_simple->reply_size_max - a_http_simple->reply_size) : a_data_size;

    memcpy(a_http_simple->reply_byte + a_http_simple->reply_size, a_data, l_data_copy_size );

    a_http_simple->reply_size += l_data_copy_size;

    return l_data_copy_size;
}

/**
 * @brief dap_http_simple_make_cache_from_reply
 * @param a_http_simple
 * @param a_ts_expire
 */
dap_http_cache_t * dap_http_simple_make_cache_from_reply(dap_http_simple_t * a_http_simple, time_t a_ts_expire  )
{
    // Because we call it from callback, we have no headers ready for output
    s_copy_reply_and_mime_to_response(a_http_simple);
    a_http_simple->http_client->reply_status_code = Http_Status_OK;
    dap_http_client_out_header_generate(a_http_simple->http_client);
    return dap_http_cache_update(a_http_simple->http_client->proc,
                                 a_http_simple->reply_byte,
                                 a_http_simple->reply_size,
                                 a_http_simple->http_client->out_headers,NULL,
                                  200, a_ts_expire);
}

/**
 * @brief dap_http_simple_reply_f
 * @param shs
 * @param data
 */
size_t dap_http_simple_reply_f(dap_http_simple_t *a_http_simple, const char *a_format, ... )
{
    va_list ap, ap_copy;
    va_start(ap, a_format);
    va_copy(ap_copy, ap);
    ssize_t l_buf_size = vsnprintf(NULL, 0, a_format, ap);
    va_end(ap);

    if (l_buf_size++ < 0) {
        va_end(ap_copy);
        return 0;
    }
    char *l_buf = DAP_NEW_SIZE(char, l_buf_size);
    vsnprintf(l_buf, l_buf_size, a_format, ap_copy);
    va_end(ap_copy);

    size_t l_ret = dap_http_simple_reply(a_http_simple, l_buf, l_buf_size);
    DAP_DELETE(l_buf);
    return l_ret;
}

void dap_http_simple_set_flag_generate_default_header(dap_http_simple_t *a_http_simple, bool flag){
    a_http_simple->generate_default_header = flag;
}
