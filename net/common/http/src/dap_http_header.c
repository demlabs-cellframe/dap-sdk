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

#define DAP_HTTP_HEADER_NAME_LEN_MASK 5
#define DAP_HTTP_HEADER_VALUE_LEN_MASK 16
typedef struct dap_http_header {
    struct dap_http_header *next, *prev;
    uint32_t name_len : DAP_HTTP_HEADER_NAME_LEN_MASK;
    uint32_t value_len : DAP_HTTP_HEADER_VALUE_LEN_MASK;
    uint32_t padding : 32 - DAP_HTTP_HEADER_NAME_LEN_MASK - DAP_HTTP_HEADER_VALUE_LEN_MASK; // reserved
    char data[];
} dap_http_header_t;

static inline void dap_http_header_to_printable(dap_http_header_t *a_header) {
    a_header->data[a_header->name_len] = ':';
    a_header->data[a_header->name_len + 1] = ' ';
    a_header->data[a_header->name_len + 2 + a_header->value_len] = '\r';
    a_header->data[a_header->name_len + 2 + a_header->value_len + 1] = '\n';
}

static inline void dap_http_header_from_printable(dap_http_header_t *a_header) {
    a_header->data[a_header->name_len] = a_header->data[a_header->name_len + 1] =
    a_header->data[a_header->name_len + 2 + a_header->value_len] = a_header->data[a_header->name_len + 2 + a_header->value_len + 1] = '\0';
}

/**
 * @brief Validate HTTP header name according to RFC 7230
 * @param a_name Header name to validate
 * @param a_len Length of name
 * @return true if valid, false otherwise
 */
static inline bool dap_http_header_name_is_valid(const char *a_name, size_t a_len)
{
    if (!a_name || a_len == 0 || a_len >= ( 1 << DAP_HTTP_HEADER_NAME_LEN_MASK ))
        return false;
        
    for (size_t i = 0; i < a_len; i++) {
        if (!isprint(a_name[i]) || a_name[i] == ':') {
            return false;
        }
    }
    return true;
}

static inline dap_http_header_t *dap_http_header_create(const char *a_name, const char *a_value, uint16_t a_name_len, uint16_t a_value_len, uint32_t *a_size) {
    if ( !dap_http_header_name_is_valid(a_name, a_name_len) ) {
        log_it(L_ERROR, "Invalid header name");
        return NULL;
    }
    dap_http_header_t *l_header = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(dap_http_header_t, a_name_len + a_value_len + sizeof(": \r\n"), NULL);
    memcpy(l_header->data, a_name, a_name_len);
    memcpy(l_header->data + a_name_len + 2, a_value, a_value_len);
    l_header->name_len = a_name_len;
    l_header->value_len = a_value_len;
    if ( a_size )
        *a_size = a_name_len + a_value_len + sizeof(": \r\n");
    return l_header;
}

/**
 * @brief dap_http_header_add - Add HTTP header to list
 * @param a_top Pointer to top of list
 * @param a_name Header name
 * @param a_value Header value
 * @return New header or NULL on error
 */
dap_http_header_t *dap_http_header_add(dap_http_header_t **a_top, const char *a_name, const char *a_value, uint32_t *a_size)
{
    if (!a_top || !a_name || !a_value)
        return NULL;
    dap_http_header_t *l_header = dap_http_header_create(a_name, a_value, strlen(a_name), strlen(a_value), a_size);
    DL_APPEND(*a_top, l_header);
    return l_header;
}

/**
 * @brief dap_http_header_find - Find header by name
 * @param a_top Top of list
 * @param a_name Name to find
 * @return Header or NULL if not found
 */
char *dap_http_header_find(dap_http_header_t *a_top, const char *a_name, uint32_t *a_len)
{
    if(!a_top || !a_name)
        return NULL;
        
    dap_http_header_t *l_hdr = NULL;
    DL_FOREACH(a_top, l_hdr) {
        if ( l_hdr->name_len == strlen(a_name) && !strncmp(l_hdr->data, a_name, l_hdr->name_len) )
        {
            if ( a_len )
                *a_len = l_hdr->value_len;
            return dap_strdup(l_hdr->data + l_hdr->name_len + 2);
        }
    }
    return NULL;
}

/**
 * @brief dap_http_header_remove - Remove header from list
 * @param a_top Pointer to top of list
 * @param a_hdr Header to remove
 */
void dap_http_header_remove(dap_http_header_t **a_top, const char *a_name, uint32_t *a_size)
{
    if (!a_top || !a_name)
        return;
        
    dap_http_header_t *l_hdr = NULL;
    DL_FOREACH(*a_top, l_hdr) {
        if ( l_hdr->name_len == strlen(a_name) && !strncmp(l_hdr->data, a_name, l_hdr->name_len) ) {
            if ( a_size )
                *a_size = l_hdr->name_len + l_hdr->value_len + sizeof(": \r\n");
            DL_DELETE(*a_top, l_hdr);
            DAP_DELETE(l_hdr);
            break;
        }
    }
}

void dap_http_headers_remove_all(dap_http_header_t **a_top)
{
    if (!a_top)
        return;
    dap_http_header_t *l_hdr = NULL;
    DL_FOREACH(*a_top, l_hdr) {
        DL_DELETE(*a_top, l_hdr);
        DAP_DELETE(l_hdr);
    }
    *a_top = NULL;
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
        dap_http_header_t *l_hdr_copy = DAP_DUP_SIZE_RET_VAL_IF_FAIL(l_hdr, sizeof(dap_http_header_t) + l_hdr->name_len + l_hdr->value_len + sizeof(": \r\n"), NULL);
        l_hdr_copy->next = l_hdr_copy->prev = NULL;
        DL_APPEND(l_ret, l_hdr_copy);
    }
    return l_ret;
}

static inline dap_http_header_t *dap_http_header_from_line(const char *a_line, size_t a_line_len, uint32_t *a_size)
{
    if(!a_line || a_line_len < 4 || a_line[a_line_len - 2] != '\r' || a_line[a_line_len - 1] != '\n')
        return NULL;
    // Find separator
    const char *l_sep = memchr(a_line, ':', a_line_len);
    if (!l_sep || l_sep == a_line)
        return NULL;
    // Extract name
    size_t l_name_len = l_sep - a_line;

    // Extract value
    const char *l_value = l_sep + 1;
    size_t l_value_len = a_line_len - (l_value - a_line);

    // Skip leading whitespace
    while(l_value_len > 0 && isspace(*l_value)) {
        l_value++; l_value_len--;
    }
    // Remove trailing CRLF
    if(l_value_len >= 2 && l_value[l_value_len-2] == '\r' && l_value[l_value_len-1] == '\n')
        l_value_len -= 2;
    // Remove trailing whitespace
    while(l_value_len > 0 && isspace(l_value[l_value_len-1]))
        l_value_len--;

    dap_http_header_t *l_header = dap_http_header_create(a_line, l_value, l_name_len, l_value_len, a_size);
    return l_header;
}

dap_http_header_t *dap_http_header_add_from_line(dap_http_header_t **a_top, const char *a_line, size_t a_line_len, uint32_t *a_size)
{
    dap_http_header_t *l_header = dap_http_header_from_line(a_line, a_line_len, a_size);
    if ( l_header )
        DL_APPEND(*a_top, l_header);
    return l_header;
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
void dap_http_headers_dump(dap_http_header_t *a_headers)
{
    log_it(L_DEBUG, "HTTP headers:");
    
    dap_http_header_t *l_hdr = NULL;
    DL_FOREACH(a_headers, l_hdr) {
        log_it(L_DEBUG, "\t%s: %s", l_hdr->data, l_hdr->data + l_hdr->name_len + 2);
    }
}

size_t dap_http_headers_print(dap_http_header_t *a_headers, char *a_str, size_t a_size) {
    if(!a_headers || !a_str || !a_size)
        return 0;

    size_t l_ret = 0;
    dap_http_header_t *l_hdr = NULL;
    DL_FOREACH(a_headers, l_hdr) {
        uint32_t l_header_size = l_hdr->name_len + l_hdr->value_len + sizeof(": \r\n");
        if ( l_ret + l_header_size > a_size )
            break;
        dap_http_header_to_printable(l_hdr);
        memcpy(a_str + l_ret, l_hdr->data, l_header_size);
        dap_http_header_from_printable(l_hdr);
        l_ret += l_header_size;
    }
    return l_ret;
}