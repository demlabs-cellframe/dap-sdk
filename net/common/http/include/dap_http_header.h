/*
 * Authors:
 * Dmitriy A. Gearasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Ltd.   https://demlabs.net
 * Copyright  (c) 2021-2024
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

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Structure for holding HTTP header in the bidirectional list
#define     DAP_HTTP$SZ_FIELD_NAME  256                             /* Length of the HTTP's header field name */
#define     DAP_HTTP$SZ_FIELD_VALUE 1024                            /* -- // -- value string */

typedef enum dap_http_method {
    HTTP_GET,
    HTTP_POST,
    // Add more
    HTTP_INVALID = 0xf
} dap_http_method_t;

typedef struct dap_http_header{
    char    name[DAP_HTTP$SZ_FIELD_NAME],
            value[DAP_HTTP$SZ_FIELD_VALUE];

    size_t  namesz, valuesz;                                        /* Dimension of corresponding field */

    struct dap_http_header *next, *prev;                            /* List's element links */
} dap_http_header_t;

// Core header functions - implementation independent
dap_http_header_t *dap_http_header_add(dap_http_header_t **a_top, const char *a_name, const char *a_value);
dap_http_header_t *dap_http_header_find(dap_http_header_t *a_top, const char *a_name);
void dap_http_header_remove(dap_http_header_t **a_top, dap_http_header_t *a_hdr);
dap_http_header_t *dap_http_headers_dup(dap_http_header_t *a_top);

// Simple universal header parser
int dap_http_header_parse_line(const char *a_line, size_t a_line_len, 
                               char *a_name_out, size_t a_name_max,
                               char *a_value_out, size_t a_value_max);

// For debug output
void dap_http_header_print(dap_http_header_t *a_headers); 

static inline dap_http_method_t dap_http_method_from_str(const char *a_method) {
    if ( !a_method )
        return HTTP_INVALID;
    if ( !strcmp(a_method, "GET") )
        return HTTP_GET;
    else if ( !strcmp(a_method, "POST") )
        return HTTP_POST;
    else return HTTP_INVALID;
    // Add more
}

static inline const char * dap_http_method_to_str(dap_http_method_t a_method) {
    static const char * l_methods[] = {
        [HTTP_GET] = "GET",
        [HTTP_POST] = "POST"
    };
    return l_methods[a_method < HTTP_INVALID ? a_method : HTTP_INVALID];
    // Add more
}

#ifdef __cplusplus
}
#endif 