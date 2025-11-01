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

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Include common header definitions (structure and common functions)
#include "dap_http_header.h"

#include "dap_http_client.h"

// Server-specific functions
int dap_http_header_server_init(); // Init module
void dap_http_header_server_deinit(); // Deinit module

// Server-specific parser that fills dap_http_client_t fields
int dap_http_header_server_parse(dap_http_client_t *cl_ht, const char *ht_line, size_t ht_line_len);

// Convenience function for adding output headers
static inline struct dap_http_header* dap_http_out_header_add(dap_http_client_t *ht, const char *name, const char *value)
{
    return dap_http_header_add(&ht->out_headers, name, value);
}

DAP_PRINTF_ATTR(3, 4) dap_http_header_t *dap_http_header_server_out_header_add_f(dap_http_client_t *ht, const char *name, const char *value, ...);

// Backward compatibility - use dap_http_header_print instead
#define print_dap_http_headers dap_http_header_print

/*
 * Server-specific HTTP constants
 */
#define HTTP$SZ_METHOD      16                                              /* POST, GET, HEAD ... */

// Server-specific HTTP field codes
enum    {
    HTTP_FLD$K_CONNECTION = 0,                                              /* Connection: Keep-Alive */
    HTTP_FLD$K_CONTENT_TYPE,                                                /* Content-Type: application/x-www-form-urlencoded */
    HTTP_FLD$K_CONTENT_LEN,                                                 /* Content-Length: 348 */
    HTTP_FLD$K_COOKIE,                                                      /* Cookie: $Version=1; Skin=new; */
    HTTP_FLD$K_EOL                                                          /* End-Of-List marker, mast be last element here */
};

#ifdef __cplusplus
}
#endif
