#pragma once

#include "dap_list.h"
#include "dap_hash.h"
#include "dap_time.h"
#include "dap_string.h"
#include "uthash.h"
#ifdef DAP_OS_WINDOWS
#include <winsock2.h>
#include <in6addr.h>
#include <ws2tcpip.h>
#endif
#ifdef DAP_OS_UNIX
#include <arpa/inet.h>
#endif

int dap_http_ban_list_client_init();
void dap_http_ban_list_client_deinit();

bool dap_http_ban_list_client_check(const char *a_addr, dap_hash_fast_t *a_decree_hash, dap_time_t *a_ts);
int dap_http_ban_list_client_add(const char *a_addr, dap_hash_fast_t a_decree_hash, dap_time_t a_ts);
int dap_http_ban_list_client_remove(const char *a_addr);
char *dap_http_ban_list_client_dump(const char *a_addr);

/**
 * @brief dap_http_ban_list_client_enum
 * walks the whole ban list under its read lock; a non-zero callback return
 * stops the walk (the remaining entries are not visited)
 * @param a_cb per-entry callback, may be NULL to just count the entries
 * @return number of entries visited
 */
typedef int (*dap_http_ban_list_client_enum_cb_t)(const char *a_addr, dap_hash_fast_t a_decree_hash,
                                                  dap_time_t a_ts, void *a_arg);
size_t dap_http_ban_list_client_enum(dap_http_ban_list_client_enum_cb_t a_cb, void *a_arg);

