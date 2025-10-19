/*
 * Authors:
 * Demlabs Limited
 * DeM Labs Inc.
 * Copyright  (c) 2020-2025
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
    along with any DAP SDK based project. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

/**
 * @file dap_http_status_code.h
 * @brief HTTP status codes definitions
 * 
 * Standard HTTP status codes as defined in RFC 7231 and related RFCs
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP status code enumeration
 */
typedef enum dap_http_status_code {
    // 1xx - Informational
    DAP_HTTP_STATUS_CONTINUE            = 100,  ///< Continue
    DAP_HTTP_STATUS_SWITCHING_PROTOCOLS = 101,  ///< Switching Protocols
    DAP_HTTP_STATUS_PROCESSING          = 102,  ///< Processing (WebDAV)
    DAP_HTTP_STATUS_EARLY_HINTS         = 103,  ///< Early Hints

    // 2xx - Successful
    DAP_HTTP_STATUS_OK                      = 200,  ///< OK
    DAP_HTTP_STATUS_CREATED                 = 201,  ///< Created
    DAP_HTTP_STATUS_ACCEPTED                = 202,  ///< Accepted
    DAP_HTTP_STATUS_NON_AUTHORITATIVE       = 203,  ///< Non-Authoritative Information
    DAP_HTTP_STATUS_NO_CONTENT              = 204,  ///< No Content
    DAP_HTTP_STATUS_RESET_CONTENT           = 205,  ///< Reset Content
    DAP_HTTP_STATUS_PARTIAL_CONTENT         = 206,  ///< Partial Content
    DAP_HTTP_STATUS_MULTI_STATUS            = 207,  ///< Multi-Status (WebDAV)
    DAP_HTTP_STATUS_ALREADY_REPORTED        = 208,  ///< Already Reported (WebDAV)
    DAP_HTTP_STATUS_IM_USED                 = 226,  ///< IM Used

    // 3xx - Redirection
    DAP_HTTP_STATUS_MULTIPLE_CHOICES    = 300,  ///< Multiple Choices
    DAP_HTTP_STATUS_MOVED_PERMANENTLY   = 301,  ///< Moved Permanently
    DAP_HTTP_STATUS_FOUND               = 302,  ///< Found
    DAP_HTTP_STATUS_SEE_OTHER           = 303,  ///< See Other
    DAP_HTTP_STATUS_NOT_MODIFIED        = 304,  ///< Not Modified
    DAP_HTTP_STATUS_USE_PROXY           = 305,  ///< Use Proxy (deprecated)
    DAP_HTTP_STATUS_TEMPORARY_REDIRECT  = 307,  ///< Temporary Redirect
    DAP_HTTP_STATUS_PERMANENT_REDIRECT  = 308,  ///< Permanent Redirect

    // 4xx - Client Error
    DAP_HTTP_STATUS_BAD_REQUEST                 = 400,  ///< Bad Request
    DAP_HTTP_STATUS_UNAUTHORIZED                = 401,  ///< Unauthorized
    DAP_HTTP_STATUS_PAYMENT_REQUIRED            = 402,  ///< Payment Required
    DAP_HTTP_STATUS_FORBIDDEN                   = 403,  ///< Forbidden
    DAP_HTTP_STATUS_NOT_FOUND                   = 404,  ///< Not Found
    DAP_HTTP_STATUS_METHOD_NOT_ALLOWED          = 405,  ///< Method Not Allowed
    DAP_HTTP_STATUS_NOT_ACCEPTABLE              = 406,  ///< Not Acceptable
    DAP_HTTP_STATUS_PROXY_AUTH_REQUIRED         = 407,  ///< Proxy Authentication Required
    DAP_HTTP_STATUS_REQUEST_TIMEOUT             = 408,  ///< Request Timeout
    DAP_HTTP_STATUS_CONFLICT                    = 409,  ///< Conflict
    DAP_HTTP_STATUS_GONE                        = 410,  ///< Gone
    DAP_HTTP_STATUS_LENGTH_REQUIRED             = 411,  ///< Length Required
    DAP_HTTP_STATUS_PRECONDITION_FAILED         = 412,  ///< Precondition Failed
    DAP_HTTP_STATUS_PAYLOAD_TOO_LARGE           = 413,  ///< Payload Too Large
    DAP_HTTP_STATUS_URI_TOO_LONG                = 414,  ///< URI Too Long
    DAP_HTTP_STATUS_UNSUPPORTED_MEDIA_TYPE      = 415,  ///< Unsupported Media Type
    DAP_HTTP_STATUS_RANGE_NOT_SATISFIABLE       = 416,  ///< Range Not Satisfiable
    DAP_HTTP_STATUS_EXPECTATION_FAILED          = 417,  ///< Expectation Failed
    DAP_HTTP_STATUS_IM_A_TEAPOT                 = 418,  ///< I'm a teapot (RFC 2324)
    DAP_HTTP_STATUS_UNPROCESSABLE_ENTITY        = 422,  ///< Unprocessable Entity
    DAP_HTTP_STATUS_LOCKED                      = 423,  ///< Locked (WebDAV)
    DAP_HTTP_STATUS_FAILED_DEPENDENCY           = 424,  ///< Failed Dependency (WebDAV)
    DAP_HTTP_STATUS_UPGRADE_REQUIRED            = 426,  ///< Upgrade Required
    DAP_HTTP_STATUS_PRECONDITION_REQUIRED       = 428,  ///< Precondition Required
    DAP_HTTP_STATUS_TOO_MANY_REQUESTS           = 429,  ///< Too Many Requests
    DAP_HTTP_STATUS_HEADER_FIELDS_TOO_LARGE     = 431,  ///< Request Header Fields Too Large
    DAP_HTTP_STATUS_UNAVAILABLE_LEGAL_REASONS   = 451,  ///< Unavailable For Legal Reasons

    // 5xx - Server Error
    DAP_HTTP_STATUS_INTERNAL_SERVER_ERROR   = 500,  ///< Internal Server Error
    DAP_HTTP_STATUS_NOT_IMPLEMENTED         = 501,  ///< Not Implemented
    DAP_HTTP_STATUS_BAD_GATEWAY             = 502,  ///< Bad Gateway
    DAP_HTTP_STATUS_SERVICE_UNAVAILABLE     = 503,  ///< Service Unavailable
    DAP_HTTP_STATUS_GATEWAY_TIMEOUT         = 504,  ///< Gateway Timeout
    DAP_HTTP_STATUS_VERSION_NOT_SUPPORTED   = 505,  ///< HTTP Version Not Supported
    DAP_HTTP_STATUS_VARIANT_ALSO_NEGOTIATES = 506,  ///< Variant Also Negotiates
    DAP_HTTP_STATUS_INSUFFICIENT_STORAGE    = 507,  ///< Insufficient Storage (WebDAV)
    DAP_HTTP_STATUS_LOOP_DETECTED           = 508,  ///< Loop Detected (WebDAV)
    DAP_HTTP_STATUS_NOT_EXTENDED            = 510,  ///< Not Extended
    DAP_HTTP_STATUS_NETWORK_AUTH_REQUIRED   = 511   ///< Network Authentication Required
} dap_http_status_code_t;

/**
 * @brief Check if status code is informational (1xx)
 * @param a_code HTTP status code
 * @return true if informational, false otherwise
 */
static inline bool dap_http_status_is_informational(int a_code) {
    return (a_code >= 100 && a_code < 200);
}

/**
 * @brief Check if status code is successful (2xx)
 * @param a_code HTTP status code
 * @return true if successful, false otherwise
 */
static inline bool dap_http_status_is_successful(int a_code) {
    return (a_code >= 200 && a_code < 300);
}

/**
 * @brief Check if status code is redirection (3xx)
 * @param a_code HTTP status code
 * @return true if redirection, false otherwise
 */
static inline bool dap_http_status_is_redirection(int a_code) {
    return (a_code >= 300 && a_code < 400);
}

/**
 * @brief Check if status code is client error (4xx)
 * @param a_code HTTP status code
 * @return true if client error, false otherwise
 */
static inline bool dap_http_status_is_client_error(int a_code) {
    return (a_code >= 400 && a_code < 500);
}

/**
 * @brief Check if status code is server error (5xx)
 * @param a_code HTTP status code
 * @return true if server error, false otherwise
 */
static inline bool dap_http_status_is_server_error(int a_code) {
    return (a_code >= 500 && a_code < 600);
}

/**
 * @brief Check if status code is any error (4xx or 5xx)
 * @param a_code HTTP status code
 * @return true if error, false otherwise
 */
static inline bool dap_http_status_is_error(int a_code) {
    return (a_code >= 400);
}

/**
 * @brief Get standard HTTP reason phrase for status code
 * @param a_code HTTP status code
 * @return Reason phrase string or NULL if unknown
 */
static inline const char* dap_http_status_reason_phrase(int a_code) {
    switch (a_code) {
        // 1xx - Informational
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 102: return "Processing";
        case 103: return "Early Hints";

        // 2xx - Successful
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 203: return "Non-Authoritative Information";
        case 204: return "No Content";
        case 205: return "Reset Content";
        case 206: return "Partial Content";
        case 207: return "Multi-Status";
        case 208: return "Already Reported";
        case 226: return "IM Used";

        // 3xx - Redirection
        case 300: return "Multiple Choices";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 305: return "Use Proxy";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";

        // 4xx - Client Error
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 402: return "Payment Required";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 406: return "Not Acceptable";
        case 407: return "Proxy Authentication Required";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 412: return "Precondition Failed";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 416: return "Range Not Satisfiable";
        case 417: return "Expectation Failed";
        case 418: return "I'm a teapot";
        case 422: return "Unprocessable Entity";
        case 423: return "Locked";
        case 424: return "Failed Dependency";
        case 426: return "Upgrade Required";
        case 428: return "Precondition Required";
        case 429: return "Too Many Requests";
        case 431: return "Request Header Fields Too Large";
        case 451: return "Unavailable For Legal Reasons";

        // 5xx - Server Error
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        case 505: return "HTTP Version Not Supported";
        case 506: return "Variant Also Negotiates";
        case 507: return "Insufficient Storage";
        case 508: return "Loop Detected";
        case 510: return "Not Extended";
        case 511: return "Network Authentication Required";

        default:
            return NULL;
    }
}

#ifdef __cplusplus
}
#endif

