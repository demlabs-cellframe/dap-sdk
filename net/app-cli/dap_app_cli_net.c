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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
#include <sys/ioctl.h>
#include <poll.h>
#include <unistd.h>
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
    case 0:
        fprintf(stderr, "[CLI-DIAG] recv: EOF (peer closed), fd=%d\n", (int)socket);
        return DAP_CLI_ERROR_INCOMPLETE;
    case -1: {
#ifdef DAP_OS_WINDOWS
        _set_errno(WSAGetLastError());
#endif
        int l_err = errno;
        if (l_err != EAGAIN && l_err != EWOULDBLOCK) {
            fprintf(stderr, "[CLI-DIAG] recv: error %d (%s), fd=%d\n", l_err, strerror(l_err), (int)socket);
            return DAP_CLI_ERROR_SOCKET;
        }
        int l_sock_err = 0;
        socklen_t l_err_len = sizeof(l_sock_err);
        getsockopt(socket, SOL_SOCKET, SO_ERROR, (char *)&l_sock_err, &l_err_len);
        if (l_sock_err)
            fprintf(stderr, "[CLI-DIAG] recv: EAGAIN + SO_ERROR=%d (%s), fd=%d\n", l_sock_err, strerror(l_sock_err), (int)socket);
        return DAP_CLI_ERROR_TIMEOUT;
    }
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
        if (( l_cmd->cmd_res_len = strtol(l_len_token + sizeof(l_content_len_str) - 1, NULL, 10) ))
            ++a_status;
        else
            return DAP_CLI_ERROR_FORMAT;
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
    fprintf(stderr, "[CLI-DIAG] connect: resolving socket path\n");
    if (( l_addr = dap_config_get_item_str_path_default(g_config, "cli-server", DAP_CFG_PARAM_SOCK_PATH, NULL) )) {
#if defined(DAP_OS_WINDOWS) || defined(DAP_OS_ANDROID)
#else
        fprintf(stderr, "[CLI-DIAG] connect: socket path=%s\n", l_addr);
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
    
    fprintf(stderr, "[CLI-DIAG] connect: calling connect() on fd=%d\n", (int)l_socket);
    if ( connect(l_socket, (struct sockaddr*)&l_saddr, l_arg_len) == SOCKET_ERROR ) {
#ifdef DAP_OS_WINDOWS
            _set_errno(WSAGetLastError());
#endif
        printf("connect() error %d: \"%s\"\n", errno, dap_strerror(errno));
        closesocket(l_socket);
        return ~0;
    }
    {
#ifdef SO_PEERCRED
        struct ucred cr;
        socklen_t cr_len = sizeof(cr);
        if (!getsockopt(l_socket, SOL_SOCKET, SO_PEERCRED, &cr, &cr_len))
            fprintf(stderr, "[CLI-DIAG] connect: OK fd=%d peer_pid=%d peer_uid=%d my_pid=%d\n",
                    (int)l_socket, (int)cr.pid, (int)cr.uid, (int)getpid());
        else
            fprintf(stderr, "[CLI-DIAG] connect: OK fd=%d SO_PEERCRED failed errno=%d\n",
                    (int)l_socket, errno);
#else
        fprintf(stderr, "[CLI-DIAG] connect: connected successfully fd=%d\n", (int)l_socket);
#endif
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
    fprintf(stderr, "[CLI-DIAG] send: %zu bytes to fd=%d\n", l_post_data->len, (int)a_socket);
    size_t res = send(a_socket, l_post_data->str, l_post_data->len, 0);
    if (res != l_post_data->len) {
        dap_json_rpc_request_free(a_request);
        printf("Error sending to server");
        return -1;
    }
    fprintf(stderr, "[CLI-DIAG] send: OK, waiting for response\n");

#ifndef _WIN32
    {
        struct pollfd pfd = { .fd = (int)a_socket, .events = POLLIN };
        int pr = poll(&pfd, 1, 2000);
        int avail = 0;
        ioctl((int)a_socket, FIONREAD, &avail);
        fprintf(stderr, "[CLI-DIAG] poll(2s): ret=%d revents=0x%x FIONREAD=%d\n", pr, pfd.revents, avail);
    }
#endif

    a_cmd->cmd_res = DAP_NEW_Z_SIZE(char, DAP_CLI_HTTP_RESPONSE_SIZE_MAX);
#ifdef _WIN32
    DWORD l_rcv_timeout_ms = 5000;
    setsockopt(a_socket, SOL_SOCKET, SO_RCVTIMEO, (const char *)&l_rcv_timeout_ms, sizeof(l_rcv_timeout_ms));
#else
    struct timeval l_rcv_timeout = { .tv_sec = 5 };
    setsockopt(a_socket, SOL_SOCKET, SO_RCVTIMEO, &l_rcv_timeout, sizeof(l_rcv_timeout));
#endif
    time_t l_start_time = time(NULL);
    int l_status = 1;
    while (l_status > 0) {
        int l_prev_status = l_status;
        l_status = dap_app_cli_http_read(a_socket, a_cmd, l_status);
        if (l_status == DAP_CLI_ERROR_TIMEOUT) {
            time_t l_elapsed = time(NULL) - l_start_time;
            int avail = 0;
            ioctl((int)a_socket, FIONREAD, &avail);
            fprintf(stderr, "[CLI-DIAG] recv: timeout cycle, elapsed=%lds FIONREAD=%d\n", (long)l_elapsed, avail);
            if (l_elapsed > DAP_CLI_HTTP_TIMEOUT)
                break;
            l_status = l_prev_status;
        }
    }
    if (l_status == DAP_CLI_ERROR_INCOMPLETE
            && a_cmd->hdr_len > 0
            && a_cmd->cmd_res_len + a_cmd->hdr_len <= a_cmd->cmd_res_cur) {
        *(a_cmd->cmd_res + a_cmd->cmd_res_cur) = '\0';
        l_status = 0;
    }
    fprintf(stderr, "[CLI-DIAG] recv: done, status=%d received=%zu\n", l_status, a_cmd->cmd_res_cur);
    if (!l_status && a_cmd->cmd_res) {
        dap_json_rpc_response_t* response = dap_json_rpc_response_from_string(a_cmd->cmd_res + a_cmd->hdr_len);
        if (!response) {
            fprintf(stderr, "[CLI-DIAG] JSON parse failed, body offset=%zu body='%.200s'\n",
                    a_cmd->hdr_len, a_cmd->cmd_res + a_cmd->hdr_len);
            printf("Error: failed to parse server response\n");
            dap_json_rpc_request_free(a_request);
            dap_string_free(l_post_data, true);
            DAP_DELETE(a_cmd->cmd_res);
            return -1;
        }
        if (l_id_response != response->id) {
            printf("Wrong response from server\n");
            dap_json_rpc_request_free(a_request);
            dap_json_rpc_response_free(response);
            DAP_DELETE(a_cmd->cmd_res);
            dap_string_free(l_post_data, true);
            return -1;
        }
        if (dap_json_rpc_response_printf_result(response, a_cmd->cmd_name, a_cmd->cmd_param, a_cmd->cmd_param_count) != 0) {
            printf("Something wrong with response\n");
        }
        dap_json_rpc_response_free(response);
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
