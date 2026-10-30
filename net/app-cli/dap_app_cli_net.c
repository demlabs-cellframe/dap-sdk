/*
 * Authors:
 * Dmitriy A. Gerasimov <kahovski@gmail.com>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://github.com/demlabsinc
 * Copyright  (c) 2017-2019
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

 DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 DAP is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
 */

//#include <dap_client.h>

#include <stdlib.h>
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>
#include <io.h>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#endif

#include "dap_common.h"
#include "dap_net.h"
#include "dap_string.h"
#include "dap_strfuncs.h"
#include "dap_cli_server.h" // for UNIX_SOCKET_FILE
#include "dap_app_cli.h"
#include "dap_app_cli_net.h"
#include "dap_enc_base64.h"

#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"

#define CLI_SERVER_DEFAULT_PORT 12345

int dap_app_cli_http_read(dap_app_cli_connect_param_t socket, dap_app_cli_cmd_state_t *l_cmd, int a_status)
{
    ssize_t l_recv_len = recv(socket, l_cmd->cmd_res + l_cmd->cmd_res_cur, DAP_CLI_HTTP_RESPONSE_SIZE_MAX, 0);
    switch (l_recv_len) {
    case 0: return DAP_CLI_ERROR_INCOMPLETE;
    case -1:
#ifdef DAP_OS_WINDOWS
        _set_errno(WSAGetLastError());
#endif
        return errno == EAGAIN || errno == EWOULDBLOCK ? DAP_CLI_ERROR_TIMEOUT : DAP_CLI_ERROR_SOCKET;
    default: 
        break;
    }
    l_cmd->cmd_res_cur += l_recv_len;
    switch (a_status) {
    case 1: {   // Find content length
        static const char l_content_len_str[] = "Content-Length: ";
        char *l_len_token = strstr(l_cmd->cmd_res, l_content_len_str);
        if (!l_len_token || !strpbrk(l_len_token, "\r\n"))
            break;
        char *l_len_value = l_len_token + sizeof(l_content_len_str) - 1;
        // A zero length is a valid frame: the overload gate answers 429 with
        // "Content-Length: 0" and no body. Only a non-numeric value is a
        // format error - treating 0 as one made a busy server look like a
        // corrupted reply ("Response format error!" on heavy commands like
        // "ledger list balance" under load).
        if (*l_len_value < '0' || *l_len_value > '9')
            return DAP_CLI_ERROR_FORMAT;
        errno = 0;
        l_cmd->cmd_res_len = (size_t)strtoull(l_len_value, NULL, 10);
        if (errno == ERANGE)
            return DAP_CLI_ERROR_FORMAT;
        ++a_status;
    }
    case 2: {   // Find header end and throw out header
        static const char l_head_end_str[] = "\r\n\r\n";
        char *l_hdr_end_token = strstr(l_cmd->cmd_res, l_head_end_str);
        if (!l_hdr_end_token)
            break;
        l_hdr_end_token += ( sizeof(l_head_end_str) - 1 );
        l_cmd->hdr_len = l_hdr_end_token - l_cmd->cmd_res;
        if (l_cmd->cmd_res_len + l_cmd->hdr_len > l_cmd->cmd_res_cur) {
            char *l_res = DAP_REALLOC(l_cmd->cmd_res, l_cmd->cmd_res_len + l_cmd->hdr_len + 1);
            if (!l_res)
                return printf("Error: out of memory!"), DAP_CLI_ERROR_INCOMPLETE;
            l_cmd->cmd_res = l_res;
        }
        ++a_status;
    }
    case 3:
    default:
        if (l_cmd->cmd_res_len + l_cmd->hdr_len <= l_cmd->cmd_res_cur) {
            *(l_cmd->cmd_res + l_cmd->cmd_res_cur) = '\0';
            a_status = 0;
        }
        break;
    }
    return a_status;
}

// HTTP status of the reply head ("HTTP/1.1 429 Too Many Requests"), 0 when
// the line is missing or unparsable.
static int s_cli_http_status_get(const char *a_hdr)
{
    if (!a_hdr || strncmp(a_hdr, "HTTP/", 5))
        return 0;
    const char *l_code = strchr(a_hdr, ' ');
    int l_status = l_code ? atoi(l_code + 1) : 0;
    return l_status >= 100 && l_status < 600 ? l_status : 0;
}

static unsigned s_cli_http_retry_after_get(const char *a_hdr)
{
    static const char l_key[] = "Retry-After: ";
    const char *l_p = a_hdr ? strstr(a_hdr, l_key) : NULL;
    return l_p ? (unsigned)strtoul(l_p + sizeof(l_key) - 1, NULL, 10) : 0;
}

/**
 * @brief dap_app_cli_connect
 * @details Connect to node unix socket server
 * @param a_socket_path
 * @return if connect established, else NULL
 */
dap_app_cli_connect_param_t dap_app_cli_connect()
{
    SOCKET l_socket = ~0;
    int l_arg_len = 0;
    uint16_t l_array_count;
    struct sockaddr_storage l_saddr = { };
    char *l_addr = NULL;
    if (( l_addr = dap_config_get_item_str_path_default(g_config, "cli-server", DAP_CFG_PARAM_SOCK_PATH, NULL) )) {
#if defined(DAP_OS_WINDOWS) || defined(DAP_OS_ANDROID)
#else
        if ( -1 == (l_socket = socket(AF_UNIX, SOCK_STREAM, 0)) )
            return printf ("socket() error %d: \"%s\"\r\n", errno, dap_strerror(errno)), ~0;
        struct sockaddr_un l_saddr_un = { .sun_family = AF_UNIX };
        strncpy(l_saddr_un.sun_path, l_addr, sizeof(l_saddr_un.sun_path) - 1);
        l_arg_len = SUN_LEN(&l_saddr_un);
        memcpy(&l_saddr, &l_saddr_un, l_arg_len);
        DAP_DELETE(l_addr);
#endif
    } else if (( l_addr = (char*)dap_config_get_item_str(g_config, "cli-server", DAP_CFG_PARAM_LISTEN_ADDRS) )) {
        if ( -1 == (l_socket = socket(AF_INET, SOCK_STREAM, 0)) ) {
#ifdef DAP_OS_WINDOWS
            _set_errno( WSAGetLastError() );
#endif
            return printf ("socket() error %d: \"%s\"\r\n", errno, dap_strerror(errno)), ~0;
        }
        char l_ip[INET6_ADDRSTRLEN] = { '\0' }; uint16_t l_port = 0;
        if ( 0 > (l_arg_len = dap_net_parse_config_address(l_addr, l_ip, &l_port, &l_saddr, NULL)) ) {
            printf ("Incorrect address \"%s\" format\n", l_addr);
            return ~0;
        }
    } else
        return printf("CLI server is not set, check config"), ~0;
    
    if ( connect(l_socket, (struct sockaddr*)&l_saddr, l_arg_len) == SOCKET_ERROR ) {
#ifdef DAP_OS_WINDOWS
            _set_errno(WSAGetLastError());
#endif
        printf("connect() error %d: \"%s\"\n", errno, dap_strerror(errno));
        closesocket(l_socket);
        return ~0;
    }
    return (dap_app_cli_connect_param_t)l_socket;
}

/* if cli command argument contains one of the following symbol
 argument is going to be encoded to base64 */


DAP_STATIC_INLINE bool s_dap_app_cli_cmd_contains_forbidden_symbol(const char * a_cmd_param) {
    static const char* s_dap_app_cli_forbidden_symbols = ";\r\n";
    return !!strpbrk(a_cmd_param, s_dap_app_cli_forbidden_symbols);
}

char *dap_app_cli_form_command(dap_app_cli_cmd_state_t *a_cmd) {
    dap_string_t *l_cmd_data = dap_string_new(a_cmd->cmd_name);
    if (a_cmd->cmd_param) {
        for (int i = 0; i < a_cmd->cmd_param_count; i++) {
            if (a_cmd->cmd_param[i]) {
                dap_string_append(l_cmd_data, ";");
                if(s_dap_app_cli_cmd_contains_forbidden_symbol(a_cmd->cmd_param[i])){
                    char * l_cmd_param_base64 = dap_enc_strdup_to_base64(a_cmd->cmd_param[i]);
                    dap_string_append(l_cmd_data, l_cmd_param_base64);
                    DAP_DELETE(l_cmd_param_base64);
                }else{
                    dap_string_append(l_cmd_data, a_cmd->cmd_param[i]);
                }
            }
        }
    }
    char *ret = l_cmd_data->str;
    dap_string_free(l_cmd_data, false);
    return ret;
}


/**
 * Send request to node
 *
 * return 0 if OK, else error code
 */
int dap_app_cli_post_command( dap_app_cli_connect_param_t a_socket, dap_app_cli_cmd_state_t *a_cmd )
{
    if(a_socket == (dap_app_cli_connect_param_t)~0 || !a_cmd || !a_cmd->cmd_name) {
        assert(0);
        return -1;
    }
    a_cmd->cmd_res_cur = 0;
    a_cmd->http_status = 0;
    a_cmd->retry_after = 0;
    dap_json_rpc_params_t *params = dap_json_rpc_params_create();
    char *l_cmd_str = dap_app_cli_form_command(a_cmd);
    dap_json_rpc_params_add_data(params, l_cmd_str, TYPE_PARAM_STRING);
    DAP_DELETE(l_cmd_str);
    uint64_t l_id_response = dap_json_rpc_response_get_new_id();
    dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(
        a_cmd->cmd_name, params, l_id_response,
        dap_config_get_item_int32_default(g_config, "cli-server", "version", 1));

    char *request_str = dap_json_rpc_request_to_json_string(a_request);

    dap_string_t *l_post_data = dap_string_new("");
    dap_string_printf(l_post_data, "POST /connect HTTP/1.1\r\n"
                                   "Host: localhost\r\n"
                                   "Content-Type: text/text\r\n"
                                   "Content-Length: %zu\r\n"
                                   "\r\n"
                                   "%s", strlen(request_str), request_str);
    DAP_DELETE(request_str);
    size_t res = send(a_socket, l_post_data->str, l_post_data->len, 0);
    if (res != l_post_data->len) {
        dap_json_rpc_request_free(a_request);
        dap_string_free(l_post_data, true);
        printf("Error sending to server");
        return -1;
    }

    //wait for command execution
    time_t l_start_time = time(NULL);
    int l_status = 1;
    a_cmd->cmd_res = DAP_NEW_Z_SIZE(char, DAP_CLI_HTTP_RESPONSE_SIZE_MAX);
    while (l_status > 0) {
        l_status = dap_app_cli_http_read(a_socket, a_cmd, l_status);
        if ((time(NULL) - l_start_time > DAP_CLI_HTTP_TIMEOUT)&&!a_cmd->cmd_res)
            l_status = DAP_CLI_ERROR_TIMEOUT;
    }
    // process result
    if (!l_status && a_cmd->cmd_res) {
        a_cmd->http_status = s_cli_http_status_get(a_cmd->cmd_res);
        a_cmd->retry_after = s_cli_http_retry_after_get(a_cmd->cmd_res);
        size_t l_body_size = a_cmd->cmd_res_cur > a_cmd->hdr_len ? a_cmd->cmd_res_cur - a_cmd->hdr_len : 0;
        if (!l_body_size) {
            // The overload gate answers 429 with an empty body: parsing it as
            // JSON made a busy node look like a corrupted reply. Report it as
            // busy so the caller retries instead of printing a format error.
            if (a_cmd->http_status && a_cmd->http_status != 200) {
                l_status = DAP_CLI_ERROR_BUSY;
            } else {
                printf("Empty response from server\n");
                l_status = DAP_CLI_ERROR_FORMAT;
            }
        } else {
            dap_json_rpc_response_t* response = dap_json_rpc_response_from_string(a_cmd->cmd_res + a_cmd->hdr_len);
            if (!response) {
                printf("Malformed response from server\n");
                l_status = DAP_CLI_ERROR_FORMAT;
            } else {
                if (l_id_response != response->id) {
                    printf("Wrong response from server\n");
                    l_status = DAP_CLI_ERROR_FORMAT;
                } else if (dap_json_rpc_response_printf_result(response, a_cmd->cmd_name, a_cmd->cmd_param, a_cmd->cmd_param_count) != 0) {
                    printf("Something wrong with response\n");
                }
                dap_json_rpc_response_free(response);
            }
        }
    }
    DAP_DELETE(a_cmd->cmd_res);
    dap_json_rpc_request_free(a_request);
    dap_string_free(l_post_data, true);
    return l_status;
}


int dap_app_cli_disconnect(dap_app_cli_connect_param_t a_socket)
{
    closesocket(a_socket);
    return 0;
}
