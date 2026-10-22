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


#include <stdio.h>
#include <string.h>
#include <ctype.h>



#include <pthread.h>

#include "dap_common.h"
#include "dap_config.h"
#include "dap_events_socket.h"

#include "dap_time.h"
#include "dap_http_server.h"
#include "http_status_code.h"

#include "dap_http_client.h"
#include "dap_http_ban_list_client.h"
#include "dap_http_header_server.h"
#include "dap_http_line.h"
#include "dap_http_request_policy.h"
#include "dap_http_response_buffer.h"

#define LOG_TAG "dap_http_client"

static bool s_debug_more = false;

int s_debug_http = 1;                                                       /* Non-static, can be used in other modules */

#define	CR      '\r'
#define	LF      '\n'
#define	CRLF    "\r\n"
#define HTTP$SZ_MINSTARTLINE 8
#define HTTP$SZ_HTLINE 4096

/**
 * @brief dap_http_client_init Init HTTP client module
 * @return  Zero if ok others if not
 */
int dap_http_client_init( )
{
    log_it(L_NOTICE,"Initialized HTTP client module");
    s_debug_http = dap_config_get_item_bool_default(g_config,"general","debug_http",false);
    return 0;
}

/**
 * @brief dap_http_client_deinit Deinit HTTP client module
 */
void dap_http_client_deinit( )
{
  log_it(L_INFO,"HTTP client module deinit");
}

/**
 * @brief dap_http_client_new Creates HTTP client's internal structure
 * @param a_esocket ESocket instance
 * @param a_arg Additional argument (usualy not used)
 */
void dap_http_client_new( dap_events_socket_t *a_esocket, void *a_arg )
{
    (void) a_arg;

    a_esocket->_inheritor = DAP_NEW_Z( dap_http_client_t );

    dap_http_client_t *l_http_client = DAP_HTTP_CLIENT( a_esocket );
    l_http_client->esocket = a_esocket;
    l_http_client->http = a_esocket->server ? DAP_HTTP_SERVER( a_esocket->server ) : NULL;
    l_http_client->keep_alive = true;  /* HTTP/1.1 default — overridden by Connection header */
    debug_if(s_debug_more, L_DEBUG, "dap_http_client_new: created HTTP client, server=%p, http_server=%p (name='%s')", 
           (void*)a_esocket->server, (void*)l_http_client->http,
           l_http_client->http ? l_http_client->http->server_name : "NULL");
    l_http_client->state_read = DAP_HTTP_CLIENT_STATE_START;
    l_http_client->socket_num = a_esocket->socket;

    return;
}

/**
 * @brief dap_http_client_delete
 * @param a_esocket HTTP Client instance's esocket
 * @param a_arg Additional argument (usualy not used)
 */
void dap_http_client_delete( dap_events_socket_t * a_esocket, void *a_arg )
{
    (void) a_arg;

    dap_http_client_t *l_http_client;

    if ( !(l_http_client = DAP_HTTP_CLIENT( a_esocket )) )
        return;                                                             /* Client is in proc callback in another thread so we don't delete it */

    while( l_http_client->in_headers )
        dap_http_header_remove( &l_http_client->in_headers, l_http_client->in_headers );

    while( l_http_client->out_headers )
        dap_http_header_remove( &l_http_client->out_headers, l_http_client->out_headers );

    free(l_http_client->pending_headers);
    l_http_client->pending_headers = NULL;
    dap_http_input_clear(&l_http_client->plaintext_input);

    if( l_http_client->proc ) {
        if( l_http_client->proc->delete_callback ) {
          l_http_client->proc->delete_callback( l_http_client, NULL );
        }
    }
    DAP_DEL_Z(l_http_client->_inheritor);
}


const unsigned char *dap_http_client_input_data(const dap_http_client_t *client)
{
    if (!client) return NULL;
    if (client->use_plaintext_input) return client->plaintext_input.data;
    return client->esocket ? (const unsigned char *)client->esocket->buf_in : NULL;
}

size_t dap_http_client_input_size(const dap_http_client_t *client)
{
    if (!client) return 0;
    if (client->use_plaintext_input) return client->plaintext_input.size;
    return client->esocket ? client->esocket->buf_in_size : 0;
}

int dap_http_client_input_consume(dap_http_client_t *client, size_t size)
{
    if (!client || size > dap_http_client_input_size(client)) return -1;
    if (client->use_plaintext_input)
        return dap_http_input_consume(&client->plaintext_input, size);
    if (!client->esocket) return -1;
    dap_events_socket_shrink_buf_in(client->esocket, size);
    return 0;
}

static char  *z_basename( char *path, uint32_t len )
{
  if ( !len )
    len = strlen( path );

  if ( len < 2 )
    return path;

  char *ptr = path + len - 1;

  while( ptr > path ) {
    if ( *ptr == '/' ) {
      ++ptr;
      break;
    }
    --ptr;
  }

  return ptr;
}

static int32_t  z_dirname( char *path, uint32_t len )
{
  if ( !len )
    len = strlen( path );

  if ( len < 2 )
    return 0;

  char *ptr = path + len - 1;

  while( ptr > path ) {
    if ( *ptr == '/' ) {
      break;
    }
    --ptr;
  }

  len = (uint32_t)(ptr - path);
  if ( len  )
    path[ len ] = 0;

  return len;
}

static int32_t  z_rootdirname( char *path, uint32_t len )
{
  if ( !len )
    len = strlen( path );

  if ( len < 2 )
    return 0;

  char *ptr = path + 1;

  while( ptr < path + len ) {
    if ( *ptr == '/' ) {
      break;
    }
    ++ptr;
  }

  uint32_t len2 = (uint32_t)(ptr - path);
  if ( len2 == len )
    return 0;

  path[ len2 ] = 0;

  return len2;
}

/**
 * @brief s_request_line_parse
 * @param a_http_client
 * @param a_buf
 * @param a_buf_length
 * @return
 */


static int s_http_start_line_parse( dap_http_client_t *a_http_client, char *a_buf, size_t a_buf_length )
{
    if (!a_http_client || !a_buf || a_buf_length < 2 ||
        memcmp(a_buf + a_buf_length - 2, "\r\n", 2))
        return -EINVAL;
    size_t n = a_buf_length - 2;
    const char *method_end = memchr(a_buf, ' ', n);
    if (!method_end || method_end == a_buf) return -EINVAL;
    size_t method_len = (size_t)(method_end - a_buf);
    if (method_len >= sizeof(a_http_client->action)) return -EINVAL;
    for (size_t i = 0; i < method_len; ++i)
        if (!dap_http_token_char((unsigned char)a_buf[i])) return -EINVAL;

    const char *target = method_end + 1;
    size_t remaining = n - (size_t)(target - a_buf);
    const char *target_end = memchr(target, ' ', remaining);
    /* This origin server routes origin-form targets, not proxy CONNECT or
     * absolute-form URLs. Never search past junk for an embedded slash. */
    if (!target_end || target_end == target || *target != '/') return -EINVAL;
    size_t target_len = (size_t)(target_end - target);
    for (size_t i = 0; i < target_len; ++i) {
        unsigned char c = (unsigned char)target[i];
        if (c <= 32 || c == 127 || c == '#') return -EINVAL;
    }
    const char *version = target_end + 1;
    if (n - (size_t)(version - a_buf) != 8 ||
        (memcmp(version, "HTTP/1.1", 8) && memcmp(version, "HTTP/1.0", 8)))
        return -EINVAL;
    const char *query = memchr(target, '?', target_len);
    size_t path_len = query ? (size_t)(query - target) : target_len;
    size_t query_len = query ? (size_t)(target_end - query - 1) : 0;
    if (path_len >= sizeof(a_http_client->url_path) ||
        query_len >= sizeof(a_http_client->in_query_string)) return -EINVAL;

    /* Publish only fully validated fields; no truncated routing identity. */
    memcpy(a_http_client->action, a_buf, method_len);
    a_http_client->action[method_len] = '\0';
    a_http_client->action_len = (uint32_t)method_len;
    memcpy(a_http_client->url_path, target, path_len);
    a_http_client->url_path[path_len] = '\0';
    a_http_client->url_path_len = (uint32_t)path_len;
    if (query_len) memcpy(a_http_client->in_query_string, query + 1, query_len);
    a_http_client->in_query_string[query_len] = '\0';
    a_http_client->in_query_string_len = (uint32_t)query_len;
    a_http_client->http_version_11 = !memcmp(version, "HTTP/1.1", 8);
    return 0;
}

/**
 * @brief s_report_error_and_restart
 * @param a_esocket
 * @param a_http_client
 */
static inline void s_report_error_and_restart( dap_events_socket_t *a_esocket, dap_http_client_t *a_http_client, uint16_t error_code )
{
    dap_http_client_input_consume(a_http_client, dap_http_client_input_size(a_http_client));
    a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
    a_http_client->keep_alive = false;
    a_http_client->out_connection_close = true;
    a_http_client->reply_status_code = error_code;
#ifdef DAP_EVENTS_CAPS_IOCP
    a_esocket->flags &= ~DAP_SOCK_READY_TO_READ;
#else
    dap_events_socket_set_readable_unsafe( a_http_client->esocket, false );
#endif
    dap_http_client_write(a_http_client);
}

/**
 * @brief dap_http_client_read
 * @param cl HTTP Client instance
 * @param arg Additional argument (usualy not used)
 */

void dap_http_client_read( dap_events_socket_t *a_esocket, void *a_arg )
{
    UNUSED(a_arg);

    char *l_cp;
    int l_ret;
    size_t l_len = 0;

    dap_http_client_t *l_http_client = DAP_HTTP_CLIENT( a_esocket );
    if (!l_http_client) {
        log_it(L_WARNING, "dap_http_client_read: _inheritor is NULL for es=%p fd=%d, ignoring read event",
               (void*)a_esocket, a_esocket->socket);
        return;
    }
    dap_http_url_proc_t *url_proc = NULL;
    dap_http_cache_t * l_http_cache;

    /*
    HTTP-message   = start-line CRLF
                         *( header-field CRLF )
                         CRLF
                         [ message-body ]
    */

//  log_it( L_DEBUG, "dap_http_client_read..." );
    unsigned l_iter_count = 0;
    do{
        debug_if(s_debug_http, L_DEBUG, "HTTP client in state read %d taked bytes in input %zu",
                                        l_http_client->state_read, dap_http_client_input_size(l_http_client) );

        switch( l_http_client->state_read )
        {
            case DAP_HTTP_CLIENT_STATE_START: { // Beginning of the session. We try to detect URL with CRLF pair at end
                if (l_http_client->esocket->type == DESCRIPTOR_TYPE_SOCKET_CLIENT 
                    || l_http_client->esocket->type == DESCRIPTOR_TYPE_SOCKET_UDP) {
                    if ( dap_http_ban_list_client_check(l_http_client->esocket->remote_addr_str, NULL, NULL) ) {
                        log_it(L_ERROR, "Client %s is banned", l_http_client->esocket->remote_addr_str);
                        s_report_error_and_restart( a_esocket, l_http_client, Http_Status_Forbidden);
                        break;
                    }
                }

                l_ret = dap_http_line_read(dap_http_client_input_data(l_http_client), dap_http_client_input_size(l_http_client),
                                           HTTP$SZ_HTLINE, &l_len);
                if (!l_ret)
                    return; /* TCP/TLS may split a line at any byte. */
                if (l_ret < 0 || l_len < HTTP$SZ_MINSTARTLINE) {
                    log_it(L_ERROR, "Malformed or oversized HTTP start-line");
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                                                                            /* Parse HTTP's start-line */
                if ( 0 > s_http_start_line_parse(l_http_client, (char *) dap_http_client_input_data(l_http_client), l_len) ) {
                    log_it( L_WARNING, "Error parsing request line '%.*s'", (int)l_len, dap_http_client_input_data(l_http_client) );
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                /* A fresh request on a persistent connection owns a fresh
                 * header budget and framing state. */
                while (l_http_client->in_headers)
                    dap_http_header_remove(&l_http_client->in_headers, l_http_client->in_headers);
                l_http_client->in_header_bytes = l_http_client->in_header_count = 0;
                l_http_client->in_connection_close = false;
                l_http_client->in_content_length = 0;
                l_http_client->in_content_type[0] = l_http_client->in_cookie[0] = '\0';
                l_http_client->in_cookie_len = 0;
                /* Persistence is the default only for HTTP/1.1. */
                l_http_client->keep_alive = l_len >= 10 &&
                    !memcmp(dap_http_client_input_data(l_http_client) + l_len - 10, "HTTP/1.1\r\n", 10);

                dap_http_client_input_consume(l_http_client, l_len);

                log_it( L_INFO, "Input: '%.*s' request for '%.*s' document (query string '%.*s')",
                        (int) l_http_client->action_len, l_http_client->action,
                        (int) l_http_client->url_path_len, l_http_client->url_path,
                        (int) l_http_client->in_query_string_len, l_http_client->in_query_string);

                /*
                 * Find URL processor
                */
                                                                            /* url_path = '/p1/p2/p3/target' */
                l_ret = z_dirname( l_http_client->url_path, l_http_client->url_path_len );
                                                                            /* url_path = '/p1/p2/p3/ */
                debug_if(s_debug_more, L_DEBUG, "Searching for URL processor with path '%s' (len=%d, original_len=%u)", 
                       l_http_client->url_path, l_ret, l_http_client->url_path_len);
                HASH_FIND_STR( l_http_client->http->url_proc, l_http_client->url_path, url_proc );
                l_http_client->proc = url_proc;
                
                if (!url_proc) {
                    log_it(L_WARNING, "URL processor not found for path '%s'. Available processors:", l_http_client->url_path);
                    dap_http_url_proc_t *l_tmp_proc, *l_tmp;
                    HASH_ITER(hh, l_http_client->http->url_proc, l_tmp_proc, l_tmp) {
                        log_it(L_WARNING, "  - '%s'", l_tmp_proc->url);
                    }
                }

                if ( l_ret )
                    l_http_client->url_path[ l_ret ] = '/';

                                                                            /* url_path = '/p1/p2/p3/target' */
                l_cp = z_basename( l_http_client->url_path, l_http_client->url_path_len );
                memmove( l_http_client->url_path, l_cp, strlen(l_cp) + 1 );
                                                                            /* url_path = 'target' */
                if ( !url_proc )
                {
                    log_it( L_WARNING, "Input: unprocessed URL request %s is rejected", l_http_client->url_path );
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_NotFound );
                    break;
                }

                l_http_client->state_read = DAP_HTTP_CLIENT_STATE_HEADERS;

                // Check if present cache
                pthread_rwlock_rdlock(&l_http_client->proc->cache_rwlock);
                if ( (l_http_cache = l_http_client->proc->cache) ) {
                    if ( ! l_http_cache->ts_expire || l_http_cache->ts_expire >= time(NULL) ){
                        l_http_client->out_headers = dap_http_headers_dup(l_http_cache->headers);
                        l_http_client->out_content_length = l_http_cache->body_size;
                        l_http_client->reply_status_code = l_http_cache->response_code;
                        if(l_http_cache->response_phrase)
                            strncpy(l_http_client->reply_reason_phrase,l_http_cache->response_phrase,sizeof (l_http_client->reply_reason_phrase)-1);

                    debug_if (s_debug_http, L_DEBUG,"%"DAP_FORMAT_SOCKET" Out: prepare cached headers", l_http_client->esocket->socket);

                    } else {
                        pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                        pthread_rwlock_wrlock(&l_http_client->proc->cache_rwlock);
                        dap_http_cache_delete(l_http_cache);
                        l_http_client->proc->cache = NULL;
                        l_http_cache = NULL;
                    }
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                } else { /* No cache */
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    if (l_http_client->proc->new_callback) /* Call client constructor */
                        l_http_client->proc->new_callback(l_http_client, NULL);
                }
            } /* case DAP_HTTP_CLIENT_STATE_START: */

            /* no break here just step to next phase */

            case DAP_HTTP_CLIENT_STATE_HEADERS: { // Parse input headers
                l_ret = dap_http_line_read(dap_http_client_input_data(l_http_client), dap_http_client_input_size(l_http_client),
                                           HTTP$SZ_HTLINE, &l_len);
                if (!l_ret)
                    return;
                if (l_ret < 0) {
                    log_it(L_ERROR, "Malformed or oversized HTTP header line");
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                if ( 0 > (l_ret = dap_http_header_server_parse( l_http_client, (char *) dap_http_client_input_data(l_http_client), l_len )) ) {
                    log_it( L_WARNING, "Input: not a valid header '%.*s'", (int)l_len, dap_http_client_input_data(l_http_client) );
                    s_report_error_and_restart(a_esocket, l_http_client, Http_Status_BadRequest);
                    break;
                }else if ( l_ret == 1 ) {
                    log_it( L_INFO, "Input: HTTP headers are over" );

                    if (!l_http_client->proc) {
                        log_it(L_ERROR, "HTTP headers over but URL proc is NULL path='%s'",
                               l_http_client->url_path);
                        s_report_error_and_restart(a_esocket, l_http_client, Http_Status_InternalServerError);
                        break;
                    }

                    if ( l_http_client->proc->access_callback )
                    {
                        int isOk = true;
                        l_http_client->proc->access_callback( l_http_client, &isOk );
                        if ( !isOk )
                        {
                            log_it( L_NOTICE, "Access restricted" );
                            s_report_error_and_restart( a_esocket, l_http_client, Http_Status_Unauthorized );
                            break;
                        }
                    }

                    /* Copy callback under lock, invoke outside — avoids holding
                     * cache_rwlock across stream/session work (can stall other
                     * GET /stream on the same url_proc). */
                    bool l_has_cache = false;
                    dap_http_client_callback_t l_hdr_cb = NULL;
                    log_it(L_INFO, "HTTP headers_read: acquiring cache_rwlock path='%s'",
                           l_http_client->url_path);
                    pthread_rwlock_rdlock(&l_http_client->proc->cache_rwlock);
                    l_has_cache = l_http_client->proc->cache != NULL;
                    l_hdr_cb = l_http_client->proc->headers_read_callback;
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    log_it(L_INFO, "HTTP headers_read: cache_rwlock released cache=%d cb=%p server='%s'",
                           l_has_cache, (void *)l_hdr_cb,
                           l_http_client->http ? l_http_client->http->server_name : "?");

                    if (!l_has_cache && l_hdr_cb) {
                        /* Transfer only payload bytes to a protocol takeover.
                         * Consume the final HTTP CRLF before invoking a handler
                         * that may replace socket ownership/callbacks. */
                        dap_http_client_input_consume(l_http_client, l_len);
                        l_len = 0;
                        void *previous_owner = a_esocket->_inheritor;
                        void (*previous_read)(dap_events_socket_t *, void *) = a_esocket->callbacks.read_callback;
                        log_it(L_INFO, "HTTP calling headers_read cb=%p path='%s' query='%s' server='%s'",
                               (void *)l_hdr_cb, l_http_client->url_path,
                               l_http_client->in_query_string,
                               l_http_client->http ? l_http_client->http->server_name : "?");
                        l_hdr_cb(l_http_client, NULL);
                        /* Do not dereference the old HTTP context after a
                         * successful takeover. The new owner drains the tail. */
                        if (a_esocket->_inheritor != previous_owner ||
                            a_esocket->callbacks.read_callback != previous_read)
                            return;
                    } else {
                        log_it(L_WARNING, "Skipping headers_read: cache=%d cb=%p path='%s' query='%s'",
                               l_has_cache, (void *)l_hdr_cb, l_http_client->url_path,
                               l_http_client->in_query_string);
                    }

                    if (l_http_client->state_read == DAP_HTTP_CLIENT_STATE_NONE) {
                        /* The route rejected the request (size/OOM/etc.). Do
                         * not resurrect DATA or dispatch buffered body bytes. */
                        dap_http_client_input_consume(l_http_client, dap_http_client_input_size(l_http_client));
                        return;
                    }
                    if (l_http_client->state_read == DAP_HTTP_CLIENT_STATE_DATA) {
                        log_it(L_INFO, "HTTP: stream takeover detected, buf_in_size=%zu after headers", dap_http_client_input_size(l_http_client));
                    } else if( l_http_client->in_content_length ) {
                        debug_if (s_debug_http, L_DEBUG, "headers -> DAP_HTTP_CLIENT_STATE_DATA" );
                        l_http_client->state_read = DAP_HTTP_CLIENT_STATE_DATA;
                    } else if (l_http_client->proc->cache) {
                        dap_http_client_write(l_http_client);
                    } else if (a_esocket->buf_out_size == 0) {
                        /* Bodyless request produced no reply (headers_read skipped
                         * or returned without write). Never silent-drop: GET /stream
                         * then hung until client timeout (~half of sessions on busy
                         * nodes showed headers over without stream headers_read). */
                        if (!l_http_client->reply_status_code)
                            l_http_client->reply_status_code = Http_Status_InternalServerError;
                        log_it(L_WARNING, "HTTP bodyless request without DATA/cache — forcing reply status=%u path='%s'",
                               l_http_client->reply_status_code, l_http_client->url_path);
                        dap_http_client_write(l_http_client);
                    }
                }
                dap_http_client_input_consume(l_http_client, l_len);
                /* Final CRLF may already have been consumed before callback;
                 * still process a coalesced body in this input turn. */
                if (!l_len) l_len = 1;
            } break;

            case DAP_HTTP_CLIENT_STATE_DATA:{
                log_it(L_INFO, "HTTP DATA: buf_in_size=%zu, has_data_read_cb=%d",
                       dap_http_client_input_size(l_http_client),
                       l_http_client->proc && l_http_client->proc->data_read_callback ? 1 : 0);

                pthread_rwlock_rdlock(&l_http_client->proc->cache_rwlock);
                if ( l_http_client->proc->cache == NULL && l_http_client->proc->data_read_callback ) {
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    l_len = 0;
                    l_http_client->proc->data_read_callback( l_http_client, &l_len );
                    log_it(L_INFO, "HTTP DATA: data_read_callback returned l_len=%zu", l_len);
                    if (l_len > dap_http_client_input_size(l_http_client)) {
                        a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
                        return;
                    }
                    dap_http_client_input_consume(l_http_client, l_len);
                    /* One body dispatch per input turn. A completed request
                     * may now belong to an asynchronous handler; do not invoke
                     * it again on the next request's bytes in this loop. */
                    return;
                } else {
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    dap_http_client_input_consume(l_http_client, dap_http_client_input_size(l_http_client));
                    dap_http_client_write(l_http_client);
                }
            } break;
            case DAP_HTTP_CLIENT_STATE_NONE: {
                dap_http_client_input_consume(l_http_client, dap_http_client_input_size(l_http_client));
            } break;
        } // switch
        if (++l_iter_count > 1000) {
            log_it(L_ERROR, "Indefinite loop in DAP HTTP client read");
            s_report_error_and_restart( a_esocket, l_http_client, Http_Status_LoopDetected );
            break;
        }
    } while (dap_http_client_input_size(l_http_client) && l_len);
}

/**
 * @brief dap_http_client_write Process write event
 * @param a_esocket HTTP Client instance's esocket
 * @param a_arg Additional argument (usualy not used)
 */
/* Headers must be accepted in full before producing any body bytes. */
static int s_http_flush_headers(dap_http_client_t *client)
{
    if (!client->pending_headers) return 1;
    ssize_t n = dap_http_client_write_body(client,
        client->pending_headers + client->pending_headers_offset,
        client->pending_headers_size - client->pending_headers_offset);
    if (n < 0) return -1;
    client->pending_headers_offset += (size_t)n;
    if (client->pending_headers_offset < client->pending_headers_size) return 0;
    free(client->pending_headers);
    client->pending_headers = NULL;
    client->pending_headers_size = client->pending_headers_offset = 0;
    return 1;
}

void dap_http_client_write(dap_http_client_t *a_http_client)
{
    if (a_http_client->pending_headers) {
        if (s_http_flush_headers(a_http_client) == 0)
            dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
        return;
    }
    if ( a_http_client->proc ) {
        // We check out_headers because if they are - we send only cached headers and don't call headers_write_callback at all
        if (!a_http_client->out_headers) {
            bool l_generate_default_headers = a_http_client->proc->headers_write_callback && a_http_client->state_read != DAP_HTTP_CLIENT_STATE_NONE ?
                        !a_http_client->proc->headers_write_callback(a_http_client, a_http_client->handler_arg) : true;
            if (l_generate_default_headers)
                dap_http_client_out_header_generate( a_http_client );
        } else {
            if (a_http_client->proc->cache && a_http_client->state_read != DAP_HTTP_CLIENT_STATE_NONE)
                a_http_client->reply_status_code = Http_Status_OK;
            //TODO: make cached reply status code
        }
    }
    log_it( L_INFO," HTTP response with %u status code", a_http_client->reply_status_code );
    char *serialized = malloc(DAP_HTTP_RESPONSE_HEADERS_MAX);
    size_t used = 0;
    if (!serialized) goto serialization_failed;
    if (dap_http_response_append(serialized, DAP_HTTP_RESPONSE_HEADERS_MAX, &used,
            "HTTP/1.1 %u %s" CRLF, a_http_client->reply_status_code,
            a_http_client->reply_reason_phrase[0] ? a_http_client->reply_reason_phrase :
            http_status_reason_phrase(a_http_client->reply_status_code))) goto serialization_failed;
    /* Write HTTP headres */
    char l_buf[DAP_TIME_STR_SIZE];
    dap_time_to_str_rfc822( l_buf, DAP_TIME_STR_SIZE, time( NULL ) );
    if (!dap_http_header_add( &a_http_client->out_headers, "Date", l_buf )) goto serialization_failed;

    for ( dap_http_header_t *hdr = a_http_client->out_headers; hdr; hdr = a_http_client->out_headers ) {
        if (dap_http_response_append(serialized, DAP_HTTP_RESPONSE_HEADERS_MAX, &used,
                                    "%s: %s" CRLF, hdr->name, hdr->value)) goto serialization_failed;
        dap_http_header_remove( &a_http_client->out_headers, hdr );
    }
    if (dap_http_response_append(serialized, DAP_HTTP_RESPONSE_HEADERS_MAX, &used, CRLF))
        goto serialization_failed;
    a_http_client->pending_headers = serialized;
    a_http_client->pending_headers_size = used;
    a_http_client->pending_headers_offset = 0;
    if (s_http_flush_headers(a_http_client) == 0)
        dap_events_socket_set_writable_unsafe(a_http_client->esocket, true);
    return;
serialization_failed:
    free(serialized);
    a_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

ssize_t dap_http_client_write_body(dap_http_client_t *client, const void *data, size_t size)
{
    if (!client || !client->esocket || (!data && size)) return -1;
    if (!size) return 0;
    ssize_t accepted = client->output.write ?
        dap_http_output_write(&client->output, data, size) :
        (ssize_t)dap_events_socket_write_unsafe(client->esocket, data, size);
    if (accepted < 0 || (size_t)accepted > size) {
        client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return -1;
    }
    return accepted;
}

int dap_http_client_output_drained(dap_http_client_t *client)
{
    if (!client || !client->esocket) return -1;
    if (client->pending_headers) return 0;
    if (client->output.write) {
        /* A custom sink must explicitly account for its own queues. */
        if (!client->output.drained) return -1;
        int rc = client->output.drained(client->output.arg);
        return rc < 0 ? -1 : rc == 1 ? 1 : 0;
    }
    return client->esocket->buf_out_size == 0 ? 1 : 0;
}

void dap_http_client_write_finished(dap_events_socket_t *socket, void *arg)
{
    (void)arg; /* Transport owns socket callback arguments. */
    dap_http_client_t *client = socket ? DAP_HTTP_CLIENT(socket) : NULL;
    if (client && client->handler_write_finished)
        client->handler_write_finished(socket, client->handler_arg);
    /* Handler may have retired its owner; do not access client here. */
}

bool dap_http_client_write_callback(dap_events_socket_t *a_esocket, void *a_arg)
{
    dap_return_val_if_fail(a_esocket, false);
    dap_http_client_t *l_http_client = DAP_HTTP_CLIENT(a_esocket);
    if (!l_http_client)
        return false;
    int headers = s_http_flush_headers(l_http_client);
    if (headers <= 0) return headers == 0;
    if (l_http_client->state_read == DAP_HTTP_CLIENT_STATE_NONE) {
        // No write data if error code set
        a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
        return false;
    }
    bool l_ret = false;
    debug_if(s_debug_http, L_DEBUG, "Entering HTTP data write callback, a_esocket: %p, a_arg: %p", a_esocket, a_arg);
    /* Write HTTP data */
    if (l_http_client->proc && l_http_client->proc->data_write_callback) {
        pthread_rwlock_wrlock(&l_http_client->proc->cache_rwlock);
        if (!l_http_client->proc->cache) {
            debug_if(s_debug_http, L_DEBUG, "No cache so we call write callback");
            pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
            l_ret = l_http_client->proc->data_write_callback(l_http_client, l_http_client->handler_arg);
        } else {
            ssize_t l_sent = dap_http_client_write_body(l_http_client,
                                           l_http_client->proc->cache->body + l_http_client->out_cache_position,
                                           l_http_client->proc->cache->body_size-l_http_client->out_cache_position);
            if (l_sent < 0) {
                a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
            } else {
                l_http_client->out_cache_position += (size_t)l_sent;
                if (l_http_client->out_cache_position >= l_http_client->proc->cache->body_size)
                    a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
                else
                    l_ret = true; /* Includes zero-byte backpressure. */
            }
            pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
        }
    } else {
        log_it(L_WARNING, "No http proc, nothing to write");
        a_esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
    }
    return l_ret;
}

/**
 * @brief dap_http_client_out_header_generate Produce general headers
 * @param cl_ht HTTP client instance
 */
void dap_http_client_out_header_generate(dap_http_client_t *a_http_client)
{
    char buf[1024];

    if ( a_http_client->reply_status_code == Http_Status_OK ) {
        if (s_debug_http)
            debug_if(s_debug_more, L_DEBUG, "Out headers generate for sock %"DAP_FORMAT_SOCKET, a_http_client->socket_num);
        if ( a_http_client->out_last_modified ) {
            dap_time_to_str_rfc822( buf, sizeof(buf), a_http_client->out_last_modified );
            dap_http_header_add( &a_http_client->out_headers, "Last-Modified", buf );
        }
        if ( a_http_client->out_content_type[0] ) {
            dap_http_header_add(&a_http_client->out_headers,"Content-Type",a_http_client->out_content_type);
            debug_if(s_debug_more, L_DEBUG,"Output: Content-Type = '%s'",a_http_client->out_content_type);
        }
        if ( a_http_client->out_content_length ) {
            snprintf(buf,sizeof(buf),"%zu",a_http_client->out_content_length);
            dap_http_header_add(&a_http_client->out_headers,"Content-Length",buf);
            debug_if(s_debug_more, L_DEBUG,"Output: Content-Length = %zu",a_http_client->out_content_length);
        }
    }else
        if (s_debug_http)
            log_it(L_WARNING, "Out headers: nothing generate for sock %"DAP_FORMAT_SOCKET", http code %d", a_http_client->socket_num,
                   a_http_client->reply_status_code);

    if ( a_http_client->out_connection_close || !a_http_client->keep_alive )
        dap_http_header_add( &a_http_client->out_headers, "Connection","Close" );

    dap_http_header_add( &a_http_client->out_headers, "Server", a_http_client->http->server_name );

    log_it( L_DEBUG,"Output: Headers generated" );
}

/**
 * @brief dap_http_client_error Process errors
 * @param cl HTTP Client instance
 * @param arg Additional argument (usualy not used)
 */
void dap_http_client_error( dap_events_socket_t *cl, int arg )
{
  (void) arg;

  log_it( L_NOTICE, "dap_http_client_error" );

  dap_http_client_t *cl_ht = DAP_HTTP_CLIENT( cl );
  if (cl_ht){
      if ( cl_ht->proc )
        if ( cl_ht->proc->error_callback )
          cl_ht->proc->error_callback( cl_ht, arg );
  }
}
