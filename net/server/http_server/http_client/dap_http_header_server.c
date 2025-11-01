/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2021
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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#endif

#include <pthread.h>
#include <utlist.h>
#include <ctype.h>

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_events_socket.h"
#include "dap_http_client.h"
#include "dap_http_header_server.h"

#define LOG_TAG "http_header"

extern  int s_debug_http;                                                   /* Should be declared in the dap_http_client.c */

#define $STRINI(a)  (a), sizeof((a))-1
struct ht_field {
    int     ht_field_code;                                                  /* Digital HTTP Code, see HTTP_FLD$K_* constants */
    char    name [128];                                                     /* Name of the HTTP Field */
    size_t  namelen;                                                        /* Length of the field */

} ht_fields_server [HTTP_FLD$K_EOL + 1] = {
    {HTTP_FLD$K_CONNECTION,     $STRINI("Connection")},
    {HTTP_FLD$K_CONTENT_TYPE,   $STRINI("Content-Type")},
    {HTTP_FLD$K_CONTENT_LEN,    $STRINI("Content-Length")},
    {HTTP_FLD$K_COOKIE,         $STRINI("Cookie")},

    {-1, {0}, 0},                                                           /* End-of-list marker, dont' touch!!! */
};
#undef  $STRINI



/**
 * @brief dap_http_header_server_init Init module
 * @return Zero if ok others if not
 */
int dap_http_header_server_init( )
{
    log_it( L_NOTICE, "Initialized HTTP headers module" );
    return 0;
}

/**
 * @brief dap_http_header_server_deinit Deinit module
 */
void dap_http_header_server_deinit()
{
    log_it( L_INFO, "HTTP headers module deinit" );
}


/**
 * @brief dap_http_header_server_parse Parse string with HTTP header
 * Server-specific parser that fills dap_http_client_t fields
 * @param cl_ht HTTP client instance
 * @param ht_line String to parse
 * @param ht_line_len Length of string
 * @return Zero if parsed well -1 if it wasn't HTTP header 1 if its "\r\n" string
 */
#define	CRLF    "\r\n"
#define	CR      '\r'
#define	LF      '\n'

int dap_http_header_server_parse(dap_http_client_t *cl_ht, const char *ht_line, size_t ht_line_len)
{
char l_name[DAP_HTTP$SZ_FIELD_NAME] = {0};
char l_value[DAP_HTTP$SZ_FIELD_VALUE] = {0};
size_t l_namelen, l_valuelen;
struct ht_field *l_ht;

    debug_if(s_debug_http, L_DEBUG, "Parse header string (%zu octets) : '%.*s'",  ht_line_len, (int) ht_line_len, ht_line);

    // Use common parser to extract name/value
    int l_ret = dap_http_header_parse_line(ht_line, ht_line_len, 
                                           l_name, sizeof(l_name),
                                           l_value, sizeof(l_value));
    if(l_ret != 0)
        return l_ret;
    
    l_namelen = strlen(l_name);
    l_valuelen = strlen(l_value);

    /*
     * So at this moment we known start and end of a field name, so we can try to recognize it
     * against a set of interested fields
     */
    for ( l_ht = ht_fields_server; l_ht->namelen; l_ht++)
        {
            if ( l_namelen == l_ht->namelen )
                if ( !memcmp(l_name, l_ht->name, l_namelen) )
                    break;
            }


    if ( l_ht->namelen )
        debug_if(s_debug_http, L_DEBUG, "Interested HTTP header field: '%s'", l_name);

    // Fill server-specific fields
    switch (l_ht->ht_field_code )
    {
        case    HTTP_FLD$K_CONNECTION:
            cl_ht->keep_alive = !strncasecmp(l_value, "Keep-Alive", l_valuelen);
            break;

        case    HTTP_FLD$K_CONTENT_TYPE:
            memcpy( cl_ht->in_content_type, l_value, dap_min(l_valuelen, sizeof(cl_ht->in_content_type) - 1) );
            cl_ht->in_content_type[dap_min(l_valuelen, sizeof(cl_ht->in_content_type) - 1)] = '\0';
            break;

        case    HTTP_FLD$K_CONTENT_LEN:
            cl_ht->in_content_length = atoi( l_value );
            break;

        case    HTTP_FLD$K_COOKIE:
            memcpy(cl_ht->in_cookie, l_value, dap_min(l_valuelen, sizeof(cl_ht->in_cookie) - 1) );
            cl_ht->in_cookie[dap_min(l_valuelen, sizeof(cl_ht->in_cookie) - 1)] = '\0';
            break;
    }


    /* Add header to list using common function */
    dap_http_header_add(&cl_ht->in_headers, l_name, l_value);

    return 0;
}


/**
 * @brief dap_http_header_server_out_header_add_f Add header to the output queue with format-filled string
 * @param ht HTTP client instance
 * @param name Header name
 * @param value Formatted string to header value
 * @param ... Arguments for formatted string
 * @return
 */
dap_http_header_t * dap_http_header_server_out_header_add_f(dap_http_client_t *ht, const char *name, const char *value, ...)
{
    va_list ap;
    dap_http_header_t * ret;
    char buf[1024];
    va_start(ap,value);
    vsnprintf(buf,sizeof(buf)-1,value,ap);
    ret=dap_http_out_header_add(ht,name,buf);
    va_end(ap);
    return ret;
}

// Note: Functions dap_http_header_add, dap_http_header_remove, dap_http_header_find, 
// dap_http_headers_dup and print_dap_http_headers are now provided by the common module
