#pragma once

#include "dap_list.h"
#include "dap_hash.h"
#include "dap_time.h"
#include "dap_string.h"
#include "uthash.h"
#ifdef WIN32
#include <winsock2.h>
#include <in6addr.h>
#include <ws2tcpip.h>
#endif
#ifdef DAP_OS_UNIX
#include <arpa/inet.h>
#endif

typedef struct dap_enc_http_ban_list_client_record{
    dap_hash_fast_t decree_hash;
    dap_time_t ts_created;
    union {
        struct in_addr ip_v4;
        struct in6_addr ip_v6;
    };
    UT_hash_handle hh;
}dap_enc_http_ban_list_client_record_t;

int dap_enc_http_ban_list_client_init();
void dap_enc_http_ban_list_client_deinit();

bool dap_enc_http_ban_list_client_check_ipv4(struct in_addr);
void dap_enc_http_ban_list_client_add_ipv4(struct in_addr, dap_hash_fast_t, dap_time_t);
void dap_enc_http_ban_list_client_remove_ipv4(struct in_addr);
void dap_enc_http_ban_list_client_ipv4_print(dap_string_t *a_str_out);

bool dap_enc_http_ban_list_client_check_ipv6(struct in6_addr);
void dap_enc_http_ban_list_client_add_ipv6(struct in6_addr, dap_hash_fast_t, dap_time_t);
void dap_enc_http_ban_list_client_remove_ipv6(struct in6_addr);
void dap_enc_http_ban_list_client_ipv6_print(dap_string_t *a_str_out);
