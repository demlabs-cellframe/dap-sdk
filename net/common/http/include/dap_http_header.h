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
    HTTP_INVALID = -1,
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_HEAD,
    HTTP_OPTIONS,
    HTTP_PATCH,
    HTTP_CONNECT,
    HTTP_TRACE,
    HTTP_METHOD_COUNT,  // Must be last - used for array bounds
} dap_http_method_t;

#define DAP_HTTP_HEADER_NAME_LEN_MASK 6
#define DAP_HTTP_HEADER_VALUE_LEN_MASK 16
typedef struct dap_http_header {
    uint32_t name_len : DAP_HTTP_HEADER_NAME_LEN_MASK;
    uint32_t value_len : DAP_HTTP_HEADER_VALUE_LEN_MASK;
    uint32_t padding : 32 - DAP_HTTP_HEADER_NAME_LEN_MASK - DAP_HTTP_HEADER_VALUE_LEN_MASK; // reserved
    struct dap_http_header *next, *prev;

    char *name, *value; // TODO: remove after full integration

    char data[];
} dap_http_header_t;

// Core header functions - implementation independent

/**
 * @brief Add HTTP header to list
 * @param a_top Pointer to top of list
 * @param a_name Header name
 * @param a_value Header value
 * @param a_size Optional pointer to store header size
 * @return New header or NULL on error
 */
dap_http_header_t *dap_http_header_add_ex(dap_http_header_t **a_top, const char *a_name, const char *a_value, uint32_t *a_size);

/**
 * @brief Add new header or replace existing one with same name
 * @param a_top Pointer to top of list
 * @param a_name Header name
 * @param a_value Header value
 * @param a_size Optional pointer to store header size
 * @return New or updated header, NULL on error
 */
dap_http_header_t *dap_http_header_add_or_replace(dap_http_header_t **a_top, const char *a_name, const char *a_value, uint32_t *a_size);

/**
 * @brief Parse header from HTTP line and add to list
 * @param a_top Pointer to top of list
 * @param a_line HTTP header line with CRLF termination
 * @param a_line_len Length of line including CRLF
 * @param a_size Optional pointer to store header size
 * @return New header or NULL on error
 */
dap_http_header_t *dap_http_header_add_from_line(dap_http_header_t **a_top, const char *a_line, size_t a_line_len, uint32_t *a_size);

// Header validation functions

/**
 * @brief Validate HTTP header value according to RFC 7230
 * @param a_value Header value to validate
 * @param a_len Length of value
 * @return true if valid, false otherwise
 */
bool dap_http_header_value_is_valid(const char *a_value, size_t a_len);

/**
 * @brief Find header by name
 * @param a_top Top of list
 * @param a_name Name to find
 * @param a_value_len Optional pointer to store value length
 * @return Header or NULL if not found
 */
dap_http_header_t *dap_http_header_find_ex(dap_http_header_t *a_top, const char *a_name, uint32_t *a_value_len);

/**
 * @brief Get header value by name
 * @param a_top Top of headers list
 * @param a_name Header name to find (or NULL to get value from a_top directly)
 * @param a_len Optional pointer to store value length
 * @return Pointer to header value or NULL if not found
 */
const char *dap_http_header_get_value(dap_http_header_t *a_top, const char *a_name, uint32_t *a_len);

/**
 * @brief Remove header from list by name
 * @param a_top Pointer to top of list
 * @param a_name Name of header to remove
 * @param a_size Optional pointer to store removed header size
 */
void dap_http_header_remove(dap_http_header_t **a_top, const char *a_name, uint32_t *a_size);

/**
 * @brief Remove all headers from list and free memory
 * @param a_top Pointer to top of list
 */
void dap_http_headers_remove_all(dap_http_header_t **a_top);

// TODO: remove
/**
 * @brief Parse single HTTP header line into separate name and value buffers
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
                               char *a_value_out, size_t a_value_max);

// For debug output

/**
 * @brief Dump headers to debug log
 * @param a_headers Headers list
 */
void dap_http_headers_dump(dap_http_header_t *a_headers);

/**
 * @brief Print headers to string buffer in HTTP format
 * @param a_headers Headers list
 * @param a_str Output buffer
 * @param a_size Size of output buffer
 * @return Number of bytes written to buffer
 */
size_t dap_http_headers_print(dap_http_header_t *a_headers, char *a_str, size_t a_size);

/**
 * @brief Convert HTTP method string to enum
 * @param a_method Method string (e.g., "GET", "POST")
 * @return HTTP method enum or HTTP_INVALID if unknown
 */
static inline dap_http_method_t dap_http_method_from_str(const char *a_method) {
    if (!a_method)
        return HTTP_INVALID;
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

/**
 * @brief Convert HTTP method enum to string
 * @param a_method HTTP method enum
 * @return Method string or "INVALID" if unknown
 */
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
    return (a_method < HTTP_METHOD_COUNT && a_method > HTTP_INVALID) ? l_methods[a_method] : "INVALID";
}

#define dap_http_header_add(a_top, a_name, a_value) dap_http_header_add_ex(a_top, a_name, a_value, NULL)
#define dap_http_header_find(a_top, a_name) dap_http_header_find_ex(a_top, a_name, NULL)
#define dap_http_header_remove(a_top, a_name) dap_http_header_remove_ex(a_top, a_name, NULL)

#ifdef __cplusplus
}
#endif 