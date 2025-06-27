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

#include <string.h>
#include <ctype.h>
#include <utlist.h>
#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_http_header.h"

#define LOG_TAG "http_header"

/**
 * @brief dap_http_header_add - Add HTTP header to list
 * @param a_top Pointer to top of list
 * @param a_name Header name
 * @param a_value Header value
 * @return New header or NULL on error
 */
dap_http_header_t *dap_http_header_add(dap_http_header_t **a_top, const char *a_name, const char *a_value)
{
    if(!a_top || !a_name || !a_value)
        return NULL;
        
    dap_http_header_t *l_new_header = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_http_header_t, NULL);

    l_new_header->namesz = strnlen(a_name, DAP_HTTP$SZ_FIELD_NAME - 1);
    memcpy(l_new_header->name, a_name, l_new_header->namesz);
    l_new_header->name[l_new_header->namesz] = '\0';

    l_new_header->valuesz = strnlen(a_value, DAP_HTTP$SZ_FIELD_VALUE - 1);
    memcpy(l_new_header->value, a_value, l_new_header->valuesz);
    l_new_header->value[l_new_header->valuesz] = '\0';

    DL_APPEND(*a_top, l_new_header);

    return l_new_header;
}

/**
 * @brief dap_http_header_find - Find header by name
 * @param a_top Top of list
 * @param a_name Name to find
 * @return Header or NULL if not found
 */
dap_http_header_t *dap_http_header_find(dap_http_header_t *a_top, const char *a_name)
{
    if(!a_top || !a_name)
        return NULL;
        
    size_t l_name_len = strlen(a_name);
    
    for(dap_http_header_t *l_hdr = a_top; l_hdr; l_hdr = l_hdr->next) {
        if(l_hdr->namesz == l_name_len && 
           strncasecmp(l_hdr->name, a_name, l_name_len) == 0)
            return l_hdr;
    }

    return NULL;
}

/**
 * @brief dap_http_header_remove - Remove header from list
 * @param a_top Pointer to top of list
 * @param a_hdr Header to remove
 */
void dap_http_header_remove(dap_http_header_t **a_top, dap_http_header_t *a_hdr)
{
    if (!a_top || !a_hdr)
        return;
        
    DL_DELETE(*a_top, a_hdr);
    DAP_DELETE(a_hdr);
}

/**
 * @brief dap_http_headers_dup - Duplicate headers list
 * @param a_top Top of list to duplicate
 * @return New list or NULL
 */
dap_http_header_t *dap_http_headers_dup(dap_http_header_t *a_top)
{
    dap_http_header_t *l_hdr = NULL, *l_ret = NULL;

    DL_FOREACH(a_top, l_hdr) {
        dap_http_header_t *l_hdr_copy = DAP_NEW_Z_RET_VAL_IF_FAIL(dap_http_header_t, l_ret);
        memcpy(l_hdr_copy, l_hdr, sizeof(dap_http_header_t));
        l_hdr_copy->next = l_hdr_copy->prev = NULL;
        DL_APPEND(l_ret, l_hdr_copy);
    }

    return l_ret;
}

/**
 * @brief dap_http_header_parse_line - Parse single HTTP header line
 * @param a_line Header line to parse
 * @param a_line_len Length of line
 * @param a_name_out Buffer for name output
 * @param a_name_max Maximum name buffer size
 * @param a_value_out Buffer for value output  
 * @param a_value_max Maximum value buffer size
 * @return 0 on success, -1 on error, 1 if end-of-headers (empty line)
 */
int dap_http_header_parse_line(const char *a_line, size_t a_line_len, 
                               char *a_name_out, size_t a_name_max,
                               char *a_value_out, size_t a_value_max)
{
    if(!a_line || !a_name_out || !a_value_out)
        return -1;
        
    // Check for end-of-headers marker
    if(a_line_len == 2 && a_line[0] == '\r' && a_line[1] == '\n')
        return 1;
        
    // Find separator
    const char *l_sep = memchr(a_line, ':', a_line_len);
    if(!l_sep || l_sep == a_line)
        return -1;
        
    // Extract name
    size_t l_name_len = l_sep - a_line;
    if(l_name_len >= a_name_max)
        l_name_len = a_name_max - 1;
    memcpy(a_name_out, a_line, l_name_len);
    a_name_out[l_name_len] = '\0';
    
    // Extract value
    const char *l_value = l_sep + 1;
    size_t l_value_len = a_line_len - (l_value - a_line);
    
    // Skip leading whitespace
    while(l_value_len > 0 && isspace(*l_value)) {
        l_value++;
        l_value_len--;
    }
    
    // Remove trailing CRLF
    if(l_value_len >= 2 && l_value[l_value_len-2] == '\r' && l_value[l_value_len-1] == '\n')
        l_value_len -= 2;
        
    // Remove trailing whitespace
    while(l_value_len > 0 && isspace(l_value[l_value_len-1]))
        l_value_len--;
        
    if(l_value_len >= a_value_max)
        l_value_len = a_value_max - 1;
    memcpy(a_value_out, l_value, l_value_len);
    a_value_out[l_value_len] = '\0';
    
    return 0;
}

/**
 * @brief dap_http_header_print - Print headers for debug
 * @param a_headers Headers list
 */
void dap_http_header_print(dap_http_header_t *a_headers)
{
    log_it(L_DEBUG, "HTTP headers:");
    
    for(dap_http_header_t *l_hdr = a_headers; l_hdr; l_hdr = l_hdr->next) {
        log_it(L_DEBUG, "  %s: %s", l_hdr->name, l_hdr->value);
    }
} 