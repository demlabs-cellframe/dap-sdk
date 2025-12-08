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

#ifndef _WIN32
#include <libgen.h>
#else
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

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

#define LOG_TAG "dap_http_client"


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
    l_http_client->http = DAP_HTTP_SERVER( a_esocket->server );
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

    if( l_http_client->proc ) {
        if( l_http_client->proc->delete_callback ) {
          l_http_client->proc->delete_callback( l_http_client, NULL );
        }
    }
    DAP_DEL_Z(l_http_client->_inheritor);
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
size_t  l_len, l_buf_len;
char    *l_cp_start, *l_cp_end;
const char ht_ver [] = "HTTP/1.";                                           /* We are not interested by minor version */

    log_it(L_NOTICE, "Parse %.*s" , (int) a_buf_length, a_buf);

    if ( (a_buf_length == 2) && (*a_buf == CR) && (*(a_buf + 1)  == LF) )          /* Check for HTTP End-Of-Header sequence */
        return  1;


    l_buf_len = a_buf_length;
    l_cp_start = a_buf;

    /*
    * request-line   = method SP request-target SP HTTP-version CRLF
    * https://projects.demlabs.net/issues/6099?issue_count=148&issue_position=1&next_issue_id=6096
    */


    /* Extract HTTP method name, eg: POST, GET, PATCH, DELETE, HEAD ...
    ** GET /issues/6099?issue_count=148 HTTP/1.1   -> "GET"
    */

    for ( ; isspace(*l_cp_start) && l_buf_len; l_cp_start++, l_buf_len--);  /* Skip possible anti-DPI whitespaces */
    l_cp_end = l_cp_start;
    for ( ; !isspace(*l_cp_end) && l_buf_len; l_cp_end++, l_buf_len--);     /* Run method's symbols until first whitespace */

    l_len = l_cp_end - l_cp_start;
    a_http_client->action_len = dap_min(l_len, sizeof(a_http_client->action) - 1 );
    memcpy( a_http_client->action, l_cp_start, a_http_client->action_len);  /* Save HTTP method's name into the HT-client context */
    a_http_client->action[a_http_client->action_len] = '\0';                /* ASCIZ */


    /* Extract <path> part of the <request-target>
    ** /issues/6099?issue_count=148 HTTP/1.1   -> "/issues/6099"
    */
    l_cp_start = l_cp_end;
    for ( ; (*l_cp_start != '/') && l_len && l_buf_len; l_cp_start++, l_buf_len--);      /* Skip possible anti-DPI whitespaces to '/' */
    l_cp_end = l_cp_start;
    for ( ; (*l_cp_end != '?') && !isspace(*l_cp_end) && l_buf_len; l_cp_end++, l_buf_len--); /* Run over <path> up to first <space> or '?' */

    l_len = l_cp_end - l_cp_start;
    a_http_client->url_path_len = dap_min(l_len, sizeof( a_http_client->url_path) - 1 );
    memcpy( a_http_client->url_path, l_cp_start, a_http_client->url_path_len);
    a_http_client->url_path[a_http_client->url_path_len] = '\0';            /* ASCIZ */


    /* Extract <arguments> part of the <request-target>
    ** issue_count=148 HTTP/1.1  -> "issue_count=148"
    */
    if ( *l_cp_end == '?' )
    {
        l_cp_end++;
        l_cp_start = l_cp_end;
        for ( ; !isspace(*l_cp_end) && l_buf_len; l_cp_end++, l_buf_len--); /* Run over <arguments> up to first <space> */

        l_len = l_cp_end - l_cp_start;
        a_http_client->in_query_string_len = dap_min(l_len, sizeof( a_http_client->in_query_string) - 1 );
        memcpy( a_http_client->in_query_string, l_cp_start, a_http_client->in_query_string_len);
        a_http_client->in_query_string[a_http_client->in_query_string_len] = '\0';          /* ASCIZ */
    }


    /* Extract HTTP version mark and check for :
    ** HTTP/1.1
    */
    l_cp_start = l_cp_end;
    for ( ; isspace(*l_cp_start) && l_buf_len; l_cp_start++, l_buf_len--);      /* Skip possible anti-DPI whitespaces */
    if ( memcmp(l_cp_start, ht_ver, sizeof(ht_ver) -1) )
        return  log_it(L_WARNING, "This ('%s') is not HTTP/1.x like start-line, so ...", l_cp_start), -EINVAL;

    return  0;  /* SUCCESS */
}

/**
 * @brief s_report_error_and_restart
 * @param a_esocket
 * @param a_http_client
 */
static inline void s_report_error_and_restart( dap_events_socket_t *a_esocket, dap_http_client_t *a_http_client, uint16_t error_code )
{
    a_esocket->buf_in_size = 0;
    a_http_client->state_read = DAP_HTTP_CLIENT_STATE_NONE;
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

    byte_t *l_peol;
    char *l_cp;
    int l_ret;
    size_t l_len = 0;

    dap_http_client_t *l_http_client = DAP_HTTP_CLIENT( a_esocket );
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
        debug_if(s_debug_http, L_DEBUG, "HTTP client in state read %d taked bytes in input %"DAP_UINT64_FORMAT_U, 
                                        l_http_client->state_read, a_esocket->buf_in_size );

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

                if ( a_esocket->buf_in_size < HTTP$SZ_MINSTARTLINE )         /* Is the length of the start-line looks to be enough ? */
                {
                    log_it( L_ERROR, "Start-line '%.*s' is too short (%d < %d)",
                            (int ) a_esocket->buf_in_size, a_esocket->buf_in, (int) a_esocket->buf_in_size , HTTP$SZ_MINSTARTLINE );
                    s_report_error_and_restart( a_esocket, l_http_client,  Http_Status_BadRequest);
                    break;
                }

                if ( (l_peol = memchr(a_esocket->buf_in, LF, a_esocket->buf_in_size)) ) /* Found LF ? */
                    if ( *(l_peol - 1) != CR )                              /* Check CR at previous position */
                        l_peol = NULL;

                if ( !l_peol ) {
                    log_it( L_ERROR, "Start-line with size %zu is not terminated by CRLF pair", a_esocket->buf_in_size);
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                l_peol++;                                                   /* Count terminal  <LF> */
                l_len = l_peol - a_esocket->buf_in;                         /* <l_len> - actual data length of the HTTP's start-line  */

                                                                            /* Parse HTTP's start-line */
                if ( 0 > s_http_start_line_parse(l_http_client, (char *) a_esocket->buf_in, l_len) ) {
                    log_it( L_WARNING, "Error parsing request line '%.*s'", (int)l_len, a_esocket->buf_in );
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                dap_events_socket_shrink_buf_in( a_esocket, l_len);         /* Shrink input buffer over start-line */

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
                HASH_FIND_STR( l_http_client->http->url_proc, l_http_client->url_path, url_proc );
                l_http_client->proc = url_proc;

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
                if ( a_esocket->buf_in_size < 2 )                          /* 2 = CRLF pair */
                    {
                        log_it( L_ERROR, "HTTP Header field is too short (%d octets) to be useful", (int) a_esocket->buf_in_size);
                        s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                        break;
                    }

                if ( (l_peol = memchr(a_esocket->buf_in, LF, a_esocket->buf_in_size)) ) /* Found LF ? */
                    if ( *(l_peol - 1) != CR )                              /* Check CR at previous position */
                        l_peol = NULL;

                if ( !l_peol ) {
                    if (HTTP$SZ_HTLINE > a_esocket->buf_in_size) {
                        debug_if(s_debug_http, L_DEBUG, "May be incomplete request in buffer, wait another part");
                        return;
                    }
                    log_it( L_ERROR, "Line with size %zu is not terminated by CRLF pair: %s", a_esocket->buf_in_size, a_esocket->buf_in);
                    s_report_error_and_restart( a_esocket, l_http_client, Http_Status_BadRequest );
                    break;
                }

                l_peol++;                                                   /* Count terminal  <LF> */
                l_len = l_peol - a_esocket->buf_in;

                if ( 0 > (l_ret = dap_http_header_parse( l_http_client, (char *) a_esocket->buf_in, l_len )) ) {
                    log_it( L_WARNING, "Input: not a valid header '%.*s'", (int)l_len, a_esocket->buf_in );
                }else if ( l_ret == 1 ) {
                    log_it( L_INFO, "Input: HTTP headers are over" );

                    if ( l_http_client->proc->access_callback )
                    {
                        int isOk = true;
                        l_http_client->proc->access_callback( l_http_client, &isOk );
                        if ( !isOk )
                        {
                            log_it( L_NOTICE, "Access restricted" );
                            s_report_error_and_restart( a_esocket, l_http_client, Http_Status_Unauthorized );
                        }
                    }

                    pthread_rwlock_rdlock(&l_http_client->proc->cache_rwlock);

                    if ( l_http_client->proc->cache == NULL &&  l_http_client->proc->headers_read_callback )
                    {
                        pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                        l_http_client->proc->headers_read_callback( l_http_client, NULL );
                    }else {
                        pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                        debug_if (s_debug_http, L_DEBUG, "Cache is present, don't call underlying callbacks");
                    }

                    // If no headers callback we go to the DATA processing
                    if( l_http_client->in_content_length ) {
                        debug_if (s_debug_http, L_DEBUG, "headers -> DAP_HTTP_CLIENT_STATE_DATA" );
                        l_http_client->state_read = DAP_HTTP_CLIENT_STATE_DATA;
                    } else if (l_http_client->proc->cache)
                        // No data, its over
                        dap_http_client_write(l_http_client);
                    else {
                        a_esocket->buf_in_size = 0;
                        break;
                    }
                }
                dap_events_socket_shrink_buf_in(a_esocket, l_len);         /* Shrink input buffer over whole HTTP header */
            } break;

            case DAP_HTTP_CLIENT_STATE_DATA:{
                debug_if (s_debug_http, L_DEBUG, "dap_http_client_read: DAP_HTTP_CLIENT_STATE_DATA");

                pthread_rwlock_rdlock(&l_http_client->proc->cache_rwlock);
                if ( l_http_client->proc->cache == NULL && l_http_client->proc->data_read_callback ) {
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    l_http_client->proc->data_read_callback( l_http_client, &l_len );
                    dap_events_socket_shrink_buf_in( a_esocket, l_len );
                } else {
                    pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
                    a_esocket->buf_in_size = 0;
                    dap_http_client_write(l_http_client);
                }
            } break;
            case DAP_HTTP_CLIENT_STATE_NONE: {
                a_esocket->buf_in_size = 0;
            } break;
        } // switch
        if (++l_iter_count > 1000) {
            log_it(L_ERROR, "Indefinite loop in DAP HTTP client read");
            s_report_error_and_restart( a_esocket, l_http_client, Http_Status_LoopDetected );
            break;
        }
    } while (a_esocket->buf_in_size && l_len);
}

/**
 * @brief dap_http_client_write Process write event
 * @param a_esocket HTTP Client instance's esocket
 * @param a_arg Additional argument (usualy not used)
 */
void dap_http_client_write(dap_http_client_t *a_http_client)
{
    if ( a_http_client->proc ) {
        // We check out_headers because if they are - we send only cached headers and don't call headers_write_callback at all
        if (!a_http_client->out_headers) {
            bool l_generate_default_headers = a_http_client->proc->headers_write_callback && a_http_client->state_read != DAP_HTTP_CLIENT_STATE_NONE ?
                        !a_http_client->proc->headers_write_callback(a_http_client, a_http_client->esocket->callbacks.arg) : true;
            if (l_generate_default_headers)
                dap_http_client_out_header_generate( a_http_client );
        } else {
            a_http_client->reply_status_code = Http_Status_OK; // Cached data are always OK... for now.
            //TODO: make cached reply status code
        }
    }
    log_it( L_INFO," HTTP response with %u status code", a_http_client->reply_status_code );
    a_http_client->esocket->buf_out_size += snprintf((char *) a_http_client->esocket->buf_out + a_http_client->esocket->buf_out_size,
                                                         a_http_client->esocket->buf_out_size_max - a_http_client->esocket->buf_out_size,
                    "HTTP/1.1 %u %s" CRLF,
                    a_http_client->reply_status_code, a_http_client->reply_reason_phrase[0] ?
                    a_http_client->reply_reason_phrase : http_status_reason_phrase(a_http_client->reply_status_code) );
    /* Write HTTP headres */
    char l_buf[DAP_TIME_STR_SIZE];
    dap_time_to_str_rfc822( l_buf, DAP_TIME_STR_SIZE, time( NULL ) );
    dap_http_header_add( &a_http_client->out_headers, "Date", l_buf );

    for ( dap_http_header_t *hdr = a_http_client->out_headers; hdr; hdr = a_http_client->out_headers ) {
        a_http_client->esocket->buf_out_size += snprintf((char *) a_http_client->esocket->buf_out + a_http_client->esocket->buf_out_size,
                                                            a_http_client->esocket->buf_out_size_max - a_http_client->esocket->buf_out_size,
                                                            "%s: %s" CRLF, hdr->name, hdr->value);
        dap_http_header_remove( &a_http_client->out_headers, hdr );
    }
    dap_events_socket_write_unsafe(a_http_client->esocket, CRLF, 2); /* Add final CRLF - HTTP's End-Of-Header */
}

bool dap_http_client_write_callback(dap_events_socket_t *a_esocket, void *a_arg)
{
    dap_return_val_if_fail(a_esocket, false);
    dap_http_client_t *l_http_client = DAP_HTTP_CLIENT(a_esocket);
    if (!l_http_client)
        return false;
    if (l_http_client->state_read == DAP_HTTP_CLIENT_STATE_NONE) {
        // No write data if error code set
        l_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
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
            l_ret = l_http_client->proc->data_write_callback(l_http_client, a_arg);
        } else {
            size_t l_sent = dap_events_socket_write_unsafe(l_http_client->esocket,
                                           l_http_client->proc->cache->body + l_http_client->out_cache_position,
                                           l_http_client->proc->cache->body_size-l_http_client->out_cache_position);
            if (!l_sent || l_http_client->out_cache_position + l_sent >= l_http_client->proc->cache->body_size) { // All is sent
                if (!l_sent)
                    debug_if(s_debug_http, L_ERROR, "Can't send data to socket");
                else
                    debug_if(s_debug_http, L_DEBUG, "Out %"DAP_FORMAT_SOCKET" All cached data over, signal to close connection",
                             l_http_client->esocket->socket);
                l_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
            } else
                l_http_client->out_cache_position += l_sent;
            pthread_rwlock_unlock(&l_http_client->proc->cache_rwlock);
        }
    } else {
        log_it(L_WARNING, "No http proc, nothing to write");
        l_http_client->esocket->flags |= DAP_SOCK_SIGNAL_CLOSE;
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
            log_it(L_DEBUG, "Out headers generate for sock %"DAP_FORMAT_SOCKET, a_http_client->socket_num);
        if ( a_http_client->out_last_modified ) {
            dap_time_to_str_rfc822( buf, sizeof(buf), a_http_client->out_last_modified );
            dap_http_header_add( &a_http_client->out_headers, "Last-Modified", buf );
        }
        if ( a_http_client->out_content_type[0] ) {
            dap_http_header_add(&a_http_client->out_headers,"Content-Type",a_http_client->out_content_type);
            log_it(L_DEBUG,"Output: Content-Type = '%s'",a_http_client->out_content_type);
        }
        if ( a_http_client->out_content_length ) {
            snprintf(buf,sizeof(buf),"%zu",a_http_client->out_content_length);
            dap_http_header_add(&a_http_client->out_headers,"Content-Length",buf);
            log_it(L_DEBUG,"Output: Content-Length = %zu",a_http_client->out_content_length);
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
