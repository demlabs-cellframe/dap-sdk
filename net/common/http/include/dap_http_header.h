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

// Structure for holding HTTP header in the bidirectional list
#define     DAP_HTTP$SZ_FIELD_NAME  256                             /* Length of the HTTP's header field name */
#define     DAP_HTTP$SZ_FIELD_VALUE 1024                            /* -- // -- value string */

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