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
typedef enum dap_http_method {
    HTTP_GET = 0,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_CONNECT,
    HTTP_TRACE,
    HTTP_METHOD_COUNT,  // Must be last - used for array bounds
    HTTP_INVALID = 0xf
} dap_http_method_t;

typedef struct dap_http_header dap_http_header_t;

// Core header functions - implementation independent
dap_http_header_t *dap_http_header_add(dap_http_header_t **a_top, const char *a_name, const char *a_value, uint16_t *a_size);
dap_http_header_t *dap_http_header_add_from_line(dap_http_header_t **a_top, const char *a_line, size_t a_line_len, uint16_t *a_size);

char *dap_http_header_find(dap_http_header_t *a_top, const char *a_name, uint16_t *a_len);
void dap_http_header_remove(dap_http_header_t **a_top, const char *a_name, uint16_t *a_size);
void dap_http_headers_remove_all(dap_http_header_t **a_top);
dap_http_header_t *dap_http_headers_dup(dap_http_header_t *a_top);

// Simple universal header parser
int dap_http_header_parse_line(const char *a_line, size_t a_line_len, 
                               char *a_name_out, size_t a_name_max,
                               char *a_value_out, size_t a_value_max);

// For debug output
void dap_http_headers_dump(dap_http_header_t *a_headers);
size_t dap_http_headers_print(dap_http_header_t *a_headers, char *a_str, size_t a_size);

static inline dap_http_method_t dap_http_method_from_str(const char *a_method) {
    if (!a_method)
        return HTTP_INVALID;
    
    // Optimized: check first character for early filtering
    switch (a_method[0]) {
        case 'G':
            if (strcmp(a_method, "GET") == 0) return HTTP_GET;
            break;
        case 'P':
            if (strcmp(a_method, "POST") == 0) return HTTP_POST;
            if (strcmp(a_method, "PUT") == 0) return HTTP_PUT;
            if (strcmp(a_method, "PATCH") == 0) return HTTP_PATCH;
            break;
        case 'D':
            if (strcmp(a_method, "DELETE") == 0) return HTTP_DELETE;
            break;
        case 'H':
            if (strcmp(a_method, "HEAD") == 0) return HTTP_HEAD;
            break;
        case 'O':
            if (strcmp(a_method, "OPTIONS") == 0) return HTTP_OPTIONS;
            break;
        case 'C':
            if (strcmp(a_method, "CONNECT") == 0) return HTTP_CONNECT;
            break;
        case 'T':
            if (strcmp(a_method, "TRACE") == 0) return HTTP_TRACE;
            break;
    }
    
    return HTTP_INVALID;
}

static inline const char * dap_http_method_to_str(dap_http_method_t a_method) {
    static const char * const l_methods[] = {
        [HTTP_GET]     = "GET",
        [HTTP_POST]    = "POST", 
        [HTTP_PUT]     = "PUT",
        [HTTP_DELETE]  = "DELETE",
        [HTTP_HEAD]    = "HEAD",
        [HTTP_OPTIONS] = "OPTIONS",
        [HTTP_PATCH]   = "PATCH",
        [HTTP_CONNECT] = "CONNECT",
        [HTTP_TRACE]   = "TRACE"
    };
    return (a_method < HTTP_METHOD_COUNT) ? l_methods[a_method] : NULL;
}

#ifdef __cplusplus
}
#endif 