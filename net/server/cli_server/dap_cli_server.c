/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * Alexander Lysikov <alexander.lysikov@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Cellframe  https://cellframe.net
 * Copyright  (c) 2019-2021
 * All rights reserved.

 This file is part of Cellframe SDK

 Cellframe SDK is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 Cellframe SDK is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with any Cellframe SDK based project.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>
#ifndef DAP_OS_WINDOWS
#include <poll.h>
#endif

#include "dap_common.h"
#include "dap_strfuncs.h"
#include "dap_file_utils.h"
#include "dap_list.h"
#include "dap_net.h"
#include "dap_cli_server.h"
#include "dap_proc_thread.h"
#include "dap_context.h"
#include "dap_server.h"

#include "dap_json_rpc_errors.h"
#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"
#include "dap_cli_http_docs.h"

#define LOG_TAG "dap_cli_server"

#define MAX_CONSOLE_CLIENTS 16

static dap_server_t *s_cli_server = NULL;
static bool s_debug_cli = false;
static int s_cli_version = 1;

// Bounded concurrency guard for the per-request detached thread model: not a
// full executor yet, just a hard cap turning runaway parallel requests into
// an immediate 429 instead of unbounded pthread/heap growth. Commands flagged
// DAP_CLI_CMD_FLAG_HEAVY by their own service (see dap_cli_server_cmd_flags_set)
// get a tighter cap of their own: the dispatcher has no built-in notion of
// which service is "heavy" — any service's command can end up being the one
// a crawler hits hard enough to exhaust node resources, so the classification
// lives with the service that registered the command, not here.
static _Atomic int s_cli_inflight = 0;
static _Atomic int s_cli_inflight_heavy = 0;
static int s_cli_max_inflight = 32;
static int s_cli_max_inflight_heavy = 4;

// Per-source rate limiting: a crawler is rarely a single address. Production
// incidents were driven by many hosts inside the same /16 (see
// cellframe_node_rpc_overload_research_2026_09, sec. 12), so limiting by
// exact IPv4 address alone does nothing — the bucket key is the /16 prefix.
// A plain token bucket refilled continuously (fractional tokens tracked as
// nanotime-scaled units) keeps the check O(1) and lock-scope tiny.
typedef struct dap_cli_rate_bucket {
    uint32_t subnet_key;      // top 16 bits of the source IPv4 address
    int64_t tokens_scaled;    // current tokens * DAP_NSEC_PER_SEC, may exceed burst only via cap below
    dap_nanotime_t ts_last;   // last refill timestamp
    UT_hash_handle hh;
} dap_cli_rate_bucket_t;

static dap_cli_rate_bucket_t *s_cli_rate_buckets = NULL;
static pthread_mutex_t s_cli_rate_lock = PTHREAD_MUTEX_INITIALIZER;
static bool s_cli_rate_limit_enabled = false;
static int s_cli_rate_limit_rps = 20;     // sustained requests/sec per /16
static int s_cli_rate_limit_burst = 40;   // burst allowance per /16
#define DAP_CLI_RATE_BUCKETS_MAX 65536    // hard cap: one entry per possible /16, never grows past it

// Dead-client registry: tracks esocket uuids that still have a live
// connection, so the detached command thread can check — right before doing
// any real work — whether the requesting client is still there. Without
// this, a client that times out or disconnects while a command is queued
// still gets its command executed to completion; for a mutating command
// (tx_create_json et al.) that means the transaction lands in mempool with
// nobody left to receive the "hash" reply — exactly the production report
// "API call timed out, but the transaction was still sent". Entries are
// added when a command is scheduled and removed exactly once, by the
// framework's delete_callback (s_cli_cmd_delete), which fires when the
// esocket is actually torn down — never earlier, never twice.
typedef struct dap_cli_live_client {
    dap_events_socket_uuid_t es_uuid;
    UT_hash_handle hh;
} dap_cli_live_client_t;
static dap_cli_live_client_t *s_cli_live_clients = NULL;
static pthread_mutex_t s_cli_live_clients_lock = PTHREAD_MUTEX_INITIALIZER;

static void s_cli_live_client_add(dap_events_socket_uuid_t a_uuid) {
    dap_cli_live_client_t *l_c = DAP_NEW_Z(dap_cli_live_client_t);
    l_c->es_uuid = a_uuid;
    pthread_mutex_lock(&s_cli_live_clients_lock);
    HASH_ADD(hh, s_cli_live_clients, es_uuid, sizeof(a_uuid), l_c);
    pthread_mutex_unlock(&s_cli_live_clients_lock);
}

static void s_cli_live_client_remove(dap_events_socket_uuid_t a_uuid) {
    pthread_mutex_lock(&s_cli_live_clients_lock);
    dap_cli_live_client_t *l_c = NULL;
    HASH_FIND(hh, s_cli_live_clients, &a_uuid, sizeof(a_uuid), l_c);
    if (l_c) {
        HASH_DEL(s_cli_live_clients, l_c);
        DAP_DELETE(l_c);
    }
    pthread_mutex_unlock(&s_cli_live_clients_lock);
}

// Returns false once the client's esocket has been destroyed. Checked once,
// right before running the command: it catches the client-already-gone case
// (early disconnect, upstream timeout) but — being a point-in-time check,
// not cooperative cancellation — does not abort a command already in
// flight if the client disconnects mid-execution. That residual window is
// unavoidable without threading a cancellation signal through every command
// handler, which is out of scope here.
static bool s_cli_live_client_check(dap_events_socket_uuid_t a_uuid) {
    pthread_mutex_lock(&s_cli_live_clients_lock);
    dap_cli_live_client_t *l_c = NULL;
    HASH_FIND(hh, s_cli_live_clients, &a_uuid, sizeof(a_uuid), l_c);
    pthread_mutex_unlock(&s_cli_live_clients_lock);
    return l_c != NULL;
}

// Cooperative cancellation: the uuid of the client a command thread is
// currently working for, one slot per thread since exactly one command runs
// per detached CLI thread. Set right before calling into the command's
// handler, cleared right after — a zero value (no live esocket ever has
// uuid 0, see dap_new_es_id()) means "not a CLI command thread" and makes
// dap_cli_server_client_is_alive() a safe no-op true from any other context
// (HTTP /exec_cmd proc thread, tests, dap_app_cli's own dap_cli_cmd_exec()
// calls).
static _Thread_local dap_events_socket_uuid_t s_cli_cmd_current_client_uuid = 0;

bool dap_cli_server_client_is_alive(void) {
    return s_cli_cmd_current_client_uuid
        ? s_cli_live_client_check(s_cli_cmd_current_client_uuid)
        : true;
}

static dap_cli_cmd_t *cli_commands = NULL;
static dap_cli_cmd_aliases_t *s_command_alias = NULL;

static inline dap_cli_cmd_t *s_cmd_add_ex(const char *a_name, dap_cli_server_cmd_callback_ex_t a_func, dap_cli_server_cmd_callback_func_json a_func_rpc,
                                            void *a_arg_func, const char *a_doc, const char *a_doc_ex);

static char *s_cli_cmd_exec_ex(char *a_req_str, bool a_restricted);
typedef struct cli_cmd_arg {
    dap_worker_t *worker;
    dap_events_socket_uuid_t es_uid;
    size_t buf_size;
    char *buf, status;
    time_t time_start;
    bool restricted;
    bool is_heavy;
} cli_cmd_arg_t;

static void* s_cli_cmd_exec(void *a_arg);

static bool s_allowed_cmd_check(char *a_buf) {
    enum json_tokener_error jterr;
    const char *l_method;
    json_object *jobj = json_tokener_parse_verbose(a_buf, &jterr),
                *jobj_method = NULL;
    if ( jterr != json_tokener_success ) 
        return log_it(L_ERROR, "Can't parse json command, error %s", json_tokener_error_desc(jterr)), false;
    if ( json_object_object_get_ex(jobj, "method", &jobj_method) )
        l_method = json_object_get_string(jobj_method);
    else {
        log_it(L_ERROR, "Invalid command request, dump it");
        json_object_put(jobj);
        return false;
    }

    bool l_allowed = !!dap_str_find( dap_config_get_array_str(g_config, "cli-server", "allowed_cmd", NULL), l_method );
    debug_if(!l_allowed, L_ERROR, "Command %s is restricted", l_method);
    json_object_put(jobj);
    return l_allowed;
}

// Look up whether the requested command was flagged HEAVY by its own service
// (dap_cli_server_cmd_flags_set). This is the only place the generic dispatcher
// consults command-specific state for backpressure purposes; it never hardcodes
// a service name.
static bool s_cmd_is_heavy(const char *a_buf) {
    enum json_tokener_error jterr;
    json_object *jobj = json_tokener_parse_verbose(a_buf, &jterr), *jobj_method = NULL;
    if (jterr != json_tokener_success)
        return false;
    bool l_heavy = false;
    if (json_object_object_get_ex(jobj, "method", &jobj_method)) {
        dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find(json_object_get_string(jobj_method));
        l_heavy = l_cmd && (l_cmd->flags & DAP_CLI_CMD_FLAG_HEAVY);
    }
    json_object_put(jobj);
    return l_heavy;
}

// Token-bucket check for the source /16. Returns true if the request is
// allowed and consumes one token; false if the subnet is over its budget.
// a_subnet_key == 0 (unknown/non-IPv4 family) always passes — this limiter
// only targets the IPv4 crawler pattern seen in production, not local/unix
// sockets or IPv6 traffic.
static bool s_cli_rate_limit_check(uint32_t a_subnet_key) {
    if (!s_cli_rate_limit_enabled || !a_subnet_key)
        return true;
    dap_nanotime_t l_now = dap_nanotime_now();
    bool l_allow;
    pthread_mutex_lock(&s_cli_rate_lock);
    dap_cli_rate_bucket_t *l_b = NULL;
    HASH_FIND(hh, s_cli_rate_buckets, &a_subnet_key, sizeof(a_subnet_key), l_b);
    if (!l_b) {
        // Bounded growth: with the table full, fail open rather than leak
        // memory or stall the CLI accept path on eviction bookkeeping.
        if (HASH_CNT(hh, s_cli_rate_buckets) >= DAP_CLI_RATE_BUCKETS_MAX) {
            pthread_mutex_unlock(&s_cli_rate_lock);
            return true;
        }
        l_b = DAP_NEW_Z(dap_cli_rate_bucket_t);
        l_b->subnet_key = a_subnet_key;
        l_b->tokens_scaled = (int64_t)s_cli_rate_limit_burst * DAP_NSEC_PER_SEC;
        l_b->ts_last = l_now;
        HASH_ADD(hh, s_cli_rate_buckets, subnet_key, sizeof(l_b->subnet_key), l_b);
    } else {
        dap_nanotime_t l_dt = l_now > l_b->ts_last ? l_now - l_b->ts_last : 0;
        l_b->ts_last = l_now;
        int64_t l_refill = (int64_t)l_dt * s_cli_rate_limit_rps;
        int64_t l_cap = (int64_t)s_cli_rate_limit_burst * DAP_NSEC_PER_SEC;
        l_b->tokens_scaled = dap_min(l_cap, l_b->tokens_scaled + l_refill);
    }
    l_allow = l_b->tokens_scaled >= DAP_NSEC_PER_SEC;
    if (l_allow)
        l_b->tokens_scaled -= DAP_NSEC_PER_SEC;
    pthread_mutex_unlock(&s_cli_rate_lock);
    return l_allow;
}

// Shared with dap_json_rpc.c's /exec_cmd handler: that path used to call
// dap_cli_cmd_exec() directly, bypassing every backpressure mechanism above
// (HEAVY classification, inflight caps) simply because it runs on a proc
// thread instead of going through s_cli_cmd_schedule. Both entry points now
// funnel through this single acquire/release pair so a signed /exec_cmd
// caller is subject to the exact same limits as an unauthenticated CLI-port
// caller.
bool dap_cli_server_backpressure_acquire(const char *a_req_str, bool *a_out_is_heavy) {
    bool l_heavy = a_req_str && s_cmd_is_heavy(a_req_str);
    if (a_out_is_heavy)
        *a_out_is_heavy = l_heavy;
    _Atomic int *l_counter = l_heavy ? &s_cli_inflight_heavy : &s_cli_inflight;
    int l_limit = l_heavy ? s_cli_max_inflight_heavy : s_cli_max_inflight;
    if (atomic_fetch_add(l_counter, 1) >= l_limit) {
        atomic_fetch_sub(l_counter, 1);
        return false;
    }
    return true;
}

void dap_cli_server_backpressure_release(bool a_is_heavy) {
    atomic_fetch_sub(a_is_heavy ? &s_cli_inflight_heavy : &s_cli_inflight, 1);
}

DAP_STATIC_INLINE void s_cli_cmd_schedule(dap_events_socket_t *a_es, void *a_arg) {
    cli_cmd_arg_t *l_arg = a_arg ? (cli_cmd_arg_t*)a_arg : DAP_NEW_Z(cli_cmd_arg_t);
    switch (l_arg->status) {
    case 0: {
        a_es->callbacks.arg = l_arg;
        // Registered exactly once per esocket lifetime (status only ever
        // passes through 0 once); removed exactly once by s_cli_cmd_delete
        // when the framework actually tears the esocket down.
        s_cli_live_client_add(a_es->uuid);
        ++l_arg->status;
    }
    case 1: {
        if (dap_cli_http_docs_try_get(a_es, (void **)&l_arg)) {
            a_es->buf_in_size = 0;
            a_es->callbacks.arg = NULL;
            return;
        }
        static const char l_content_len_str[] = "Content-Length: ";
        l_arg->buf = strstr((char*)a_es->buf_in, l_content_len_str);
        if ( !l_arg->buf || !strpbrk(l_arg->buf, "\r\n") )
            return;
        if (( l_arg->buf_size = (size_t)strtol(l_arg->buf + sizeof(l_content_len_str) - 1, NULL, 10) ))
            ++l_arg->status;
        else
            break;
    }
    case 2: { // Find header end and throw out header
        static const char l_head_end_str[] = "\r\n\r\n";
        char *l_hdr_end_token = strstr(l_arg->buf, l_head_end_str);
        if (!l_hdr_end_token)
            return;
        l_arg->buf = l_hdr_end_token + sizeof(l_head_end_str) - 1;
        ++l_arg->status;
    }
    case 3:
    default: {
        size_t l_hdr_len = (size_t)(l_arg->buf - (char*)a_es->buf_in);
        if ( a_es->buf_in_size < l_arg->buf_size + l_hdr_len )
            return;

        bool l_is_loopback = ((struct sockaddr_in*)&a_es->addr_storage)->sin_addr.s_addr == htonl(INADDR_LOOPBACK);
        l_arg->restricted = !l_is_loopback
#ifdef DAP_OS_UNIX
            && a_es->addr_storage.ss_family != AF_UNIX
#endif
            && !s_allowed_cmd_check(l_arg->buf);

        // Rate-limit by source /16 before doing any further work: the
        // production crawlers were spread across many hosts in a handful of
        // /16 ranges, so per-IP limiting alone would not have helped.
        // Loopback and unix-socket callers (local tooling, node-cli) bypass
        // this — they are not the threat model.
        if (!l_is_loopback && a_es->addr_storage.ss_family == AF_INET) {
            uint32_t l_subnet_key = ntohl(((struct sockaddr_in*)&a_es->addr_storage)->sin_addr.s_addr) >> 16;
            if (!s_cli_rate_limit_check(l_subnet_key)) {
                dap_events_socket_write_f_unsafe(a_es, "HTTP/1.1 429 Too Many Requests\r\n"
                                                  "Retry-After: 1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
                a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
                DAP_DELETE(l_arg);
                a_es->buf_in_size = 0;
                a_es->callbacks.arg = NULL;
                return;
            }
        }

        l_arg->buf = strndup(l_arg->buf, l_arg->buf_size);
        l_arg->worker = a_es->worker;
        l_arg->es_uid = a_es->uuid;
        l_arg->time_start = dap_nanotime_now();

        if (!dap_cli_server_backpressure_acquire(l_arg->buf, &l_arg->is_heavy)) {
            dap_events_socket_write_f_unsafe(a_es, "HTTP/1.1 429 Too Many Requests\r\n"
                                              "Retry-After: 1\r\nConnection: close\r\nContent-Length: 0\r\n\r\n");
            a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
            DAP_DEL_MULTY(l_arg->buf, l_arg);
            a_es->buf_in_size = 0;
            a_es->callbacks.arg = NULL;
            return;
        }

        pthread_t l_tid;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        int l_rc = pthread_create(&l_tid, &attr, s_cli_cmd_exec, l_arg);
        if (l_rc) {
            log_it(L_ERROR, "Can't create CLI command thread, error %d", l_rc);
            dap_cli_server_backpressure_release(l_arg->is_heavy);
            DAP_DEL_MULTY(l_arg->buf, l_arg);
        }
        a_es->buf_in_size = 0;
        a_es->callbacks.arg = NULL;
    } return;
    }

    dap_events_socket_write_f_unsafe(a_es, "HTTP/1.1 500 Internal Server Error\r\n");
    char *buf_dump = dap_dump_hex(a_es->buf_in, dap_min(a_es->buf_in_size, (size_t)65536));
    log_it(L_DEBUG, "Incomplete cmd request:\r\n%s", buf_dump);
    DAP_DELETE(buf_dump);
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

DAP_STATIC_INLINE void s_cli_cmd_delete(dap_events_socket_t *a_es, void UNUSED_ARG *a_arg) {
    s_cli_live_client_remove(a_es->uuid);
    DAP_DELETE(a_es->callbacks.arg);
}

// The CLI/RPC protocol is strictly request-response with no keep-alive
// semantics of its own; the node only ever wrote a "Connection: close"
// header without actually closing anything, so a client that does not close
// its side itself would sit in the worker's esocket table (and eventually
// CLOSE_WAIT after a peer-side close) until the 60s inactivity timeout. Once
// the full reply has been flushed to the socket, tear the connection down
// immediately instead of waiting on that timeout.
static void s_cli_client_write_finished(dap_events_socket_t *a_es, void UNUSED_ARG *a_arg) {
    a_es->flags |= DAP_SOCK_SIGNAL_CLOSE;
}

/**
 * @brief dap_cli_server_init
 * @param a_debug_more
 * @param a_socket_path_or_address
 * @param a_port
 * @param a_permissions
 * @return
 */
int dap_cli_server_init(bool a_debug_more, const char *a_cfg_section)
{
    s_debug_cli = a_debug_more;
    dap_events_socket_callbacks_t l_callbacks = { .read_callback = s_cli_cmd_schedule, .delete_callback = s_cli_cmd_delete,
                                                   .write_finished_callback = s_cli_client_write_finished };
    if (!( s_cli_server = dap_server_new(a_cfg_section, NULL, &l_callbacks) )) {
        log_it(L_ERROR, "CLI server not initialized");
        return -2;
    }
    s_cli_version = dap_config_get_item_int32_default(g_config, a_cfg_section, "version", s_cli_version);
    s_cli_max_inflight = dap_config_get_item_int32_default(g_config, a_cfg_section, "max_inflight", s_cli_max_inflight);
    s_cli_max_inflight_heavy = dap_config_get_item_int32_default(g_config, a_cfg_section, "max_inflight_heavy", s_cli_max_inflight_heavy);
    s_cli_rate_limit_enabled = dap_config_get_item_bool_default(g_config, a_cfg_section, "rate_limit", s_cli_rate_limit_enabled);
    s_cli_rate_limit_rps = dap_config_get_item_int32_default(g_config, a_cfg_section, "rate_limit_rps", s_cli_rate_limit_rps);
    s_cli_rate_limit_burst = dap_config_get_item_int32_default(g_config, a_cfg_section, "rate_limit_burst", s_cli_rate_limit_burst);
    dap_cli_http_docs_init(a_cfg_section);
    log_it(L_INFO, "CLI server initialized with protocol version %d, max_inflight %d (heavy %d), rate_limit %s (%d rps, burst %d per /16)",
           s_cli_version, s_cli_max_inflight, s_cli_max_inflight_heavy,
           s_cli_rate_limit_enabled ? "on" : "off", s_cli_rate_limit_rps, s_cli_rate_limit_burst);
    return 0;
}

/**
 * @brief dap_cli_server_deinit
 */
void dap_cli_server_deinit()
{
    dap_cli_http_docs_deinit();
    dap_server_delete(s_cli_server);
    pthread_mutex_lock(&s_cli_rate_lock);
    dap_cli_rate_bucket_t *l_b, *l_tmp;
    HASH_ITER(hh, s_cli_rate_buckets, l_b, l_tmp) {
        HASH_DEL(s_cli_rate_buckets, l_b);
        DAP_DELETE(l_b);
    }
    pthread_mutex_unlock(&s_cli_rate_lock);
    pthread_mutex_lock(&s_cli_live_clients_lock);
    dap_cli_live_client_t *l_c, *l_c_tmp;
    HASH_ITER(hh, s_cli_live_clients, l_c, l_c_tmp) {
        HASH_DEL(s_cli_live_clients, l_c);
        DAP_DELETE(l_c);
    }
    pthread_mutex_unlock(&s_cli_live_clients_lock);
}

/**
 * @brief dap_cli_server_cmd_add
 * @param a_name
 * @param a_func
 * @param a_doc
 * @param a_doc_ex
 */
dap_cli_cmd_t *dap_cli_server_cmd_add(const char * a_name, dap_cli_server_cmd_callback_t a_func, dap_cli_server_cmd_callback_func_json a_func_rpc,
                                                                                                            const char *a_doc, const char *a_doc_ex)
{
    return s_cmd_add_ex(a_name, (dap_cli_server_cmd_callback_ex_t)(void *)a_func, a_func_rpc, NULL, a_doc, a_doc_ex);
}

/**
 * @brief s_cmd_add_ex
 * @param a_name
 * @param a_func
 * @param a_arg_func
 * @param a_doc
 * @param a_doc_ex
 */
static inline dap_cli_cmd_t *s_cmd_add_ex(const char * a_name, dap_cli_server_cmd_callback_ex_t a_func, dap_cli_server_cmd_callback_func_json a_func_rpc,                                            void *a_arg_func, const char *a_doc, const char *a_doc_ex)
{
    dap_cli_cmd_t *l_cmd_item = NULL;
    HASH_FIND_STR(cli_commands, a_name, l_cmd_item);
    bool l_is_replace = l_cmd_item != NULL;
    if (!l_cmd_item) {
        l_cmd_item = DAP_NEW_Z(dap_cli_cmd_t);
        if (!l_cmd_item) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }
        snprintf(l_cmd_item->name, sizeof(l_cmd_item->name), "%s", a_name);
        HASH_ADD_STR(cli_commands, name, l_cmd_item);
    } else {
        if (l_cmd_item->doc) {
            DAP_DELETE(l_cmd_item->doc);
            l_cmd_item->doc = NULL;
        }
        if (l_cmd_item->doc_ex) {
            DAP_DELETE(l_cmd_item->doc_ex);
            l_cmd_item->doc_ex = NULL;
        }
    }
    l_cmd_item->doc = a_doc ? strdup(a_doc) : NULL;
    l_cmd_item->doc_ex = a_doc_ex ? strdup(a_doc_ex) : NULL;

    if (a_arg_func) {
        l_cmd_item->func_ex = a_func;
        l_cmd_item->arg_func = a_arg_func;
    } else {
        l_cmd_item->func = (dap_cli_server_cmd_callback_t)(void *)a_func;
        l_cmd_item->arg_func = NULL;
    }
    l_cmd_item->func_rpc = a_func_rpc;
    l_cmd_item->arg_func_rpc = NULL;
    log_it(L_DEBUG, "%s command %s", l_is_replace ? "Replaced" : "Added", l_cmd_item->name);
    return l_cmd_item;
}

bool dap_cli_server_cmd_remove(const char *a_name)
{
    if (!a_name)
        return false;
    dap_cli_cmd_t *l_cmd_item = NULL;
    HASH_FIND_STR(cli_commands, a_name, l_cmd_item);
    if (!l_cmd_item)
        return false;
    dap_cli_cmd_aliases_t *l_alias, *l_tmp;
    HASH_ITER(hh, s_command_alias, l_alias, l_tmp) {
        if (l_alias->standard_command == l_cmd_item) {
            HASH_DEL(s_command_alias, l_alias);
            DAP_DELETE(l_alias);
        }
    }
    if (l_cmd_item->doc) {
        DAP_DELETE(l_cmd_item->doc);
        l_cmd_item->doc = NULL;
    }
    if (l_cmd_item->doc_ex) {
        DAP_DELETE(l_cmd_item->doc_ex);
        l_cmd_item->doc_ex = NULL;
    }
    HASH_DEL(cli_commands, l_cmd_item);
    log_it(L_DEBUG, "Removed command %s", l_cmd_item->name);
    DAP_DELETE(l_cmd_item);
    return true;
}

int json_commands(const char * a_name) {
    static const char* long_cmd[] = {
            "tx_history",
            "wallet",
            "mempool",
            "ledger",
            "tx_create",
            "tx_create_json",
            "mempool_add",
            "tx_verify",
            "tx_sign",
            "tx_cond",
            "tx_cond_create",
            "tx_cond_remove",
            "tx_cond_unspent_find",
            "tx_cond_refill",
            "chain_ca_copy",
            "dag",
            "block",
            "dag",
            "token",
            "esbocs",
            "global_db",
            "net_srv",
            "net",
            "srv_stake",
            "poll",
            "srv_xchange",
            "emit_delegate",
            "token_decl",
            "token_update",
            "token_update_sign",
            "token_decl_sign",
            "chain_ca_pub",
            "token_emit",
            "find",
            "version",
            "remove",
            "gdb_import",
            "stats",
            "print_log",
            "stake_lock",
            "exec_cmd",
            "policy",
            "stake_ext",
            "srv_dex"
    };
    for (size_t i = 0; i < sizeof(long_cmd)/sizeof(long_cmd[0]); i++) {
        if (!strcmp(a_name, long_cmd[i])) {
            return 1;
        }
    }
    return 0;
}

/**
 * @brief dap_cli_server_cmd_set_reply_text
 * Write text to reply string
 * @param str_reply
 * @param str
 * @param ...
 */
void dap_cli_server_cmd_set_reply_text(void **a_str_reply, const char *str, ...)
{
    char **l_str_reply = (char **)a_str_reply;
    if (l_str_reply) {
        if (*l_str_reply) {
            DAP_DELETE(*l_str_reply);
            *l_str_reply = NULL;
        }
        va_list args;
        va_start(args, str);
        *l_str_reply = dap_strdup_vprintf(str, args);
        va_end(args);
    }
}

/**
 * @brief dap_cli_server_cmd_check_option
 * @param argv
 * @param arg_start
 * @param arg_end
 * @param opt_name
 * @return
 */
int dap_cli_server_cmd_check_option( char** argv, int arg_start, int arg_end, const char *opt_name)
{
    int arg_index = arg_start;
    const char *arg_string;

    while(arg_index < arg_end)
    {
        char * l_argv_cur = argv[arg_index];
        arg_string = l_argv_cur;
        // find opt_name
        if(arg_string && opt_name && arg_string[0] && opt_name[0] && !strcmp(arg_string, opt_name)) {
                return arg_index;
        }
        arg_index++;
    }
    return -1;
}


/**
 * @brief dap_cli_server_cmd_find_option_val
 * return index of string in argv, or 0 if not found
 * @param argv
 * @param arg_start
 * @param arg_end
 * @param opt_name
 * @param opt_value
 * @return int
 */
int dap_cli_server_cmd_find_option_val( char** argv, int arg_start, int arg_end, const char *opt_name, const char **opt_value)
{
    assert(argv);
    int arg_index = arg_start;
    const char *arg_string;
    int l_ret_pos = 0;

    while(arg_index < arg_end)
    {
        char * l_argv_cur = argv[arg_index];
        arg_string = l_argv_cur;
        // find opt_name
        if(arg_string && opt_name && arg_string[0] && opt_name[0] && !strcmp(arg_string, opt_name)) {
            // find opt_value
            if(opt_value) {
                arg_string = argv[++arg_index];
                if(arg_string) {
                    *opt_value = arg_string;
                    return arg_index;
                }
                // for case if opt_name exist without value
                else
                    l_ret_pos = arg_index;
            }
            else
                // need only opt_name
                return arg_index;
        }
        arg_index++;
    }
    return l_ret_pos;
}


/**
 * @brief dap_cli_server_cmd_apply_overrides
 *
 * @param a_name
 * @param a_overrides
 */
void dap_cli_server_cmd_apply_overrides(const char * a_name, const dap_cli_server_cmd_override_t a_overrides)
{
    dap_cli_cmd_t *l_cmd_item = dap_cli_server_cmd_find(a_name);
    if(l_cmd_item)
        l_cmd_item->overrides = a_overrides;
}

/**
 * @brief dap_cli_server_cmd_flags_set
 * Let a service classify its own command for dispatcher-level backpressure
 * (see DAP_CLI_CMD_FLAG_HEAVY) without the dispatcher having to know the
 * service or command name in advance.
 * @param a_name
 * @param a_flags
 */
void dap_cli_server_cmd_flags_set(const char *a_name, uint32_t a_flags)
{
    dap_cli_cmd_t *l_cmd_item = dap_cli_server_cmd_find(a_name);
    if (l_cmd_item)
        l_cmd_item->flags |= a_flags;
}

/**
 * @brief dap_cli_server_cmd_get_first
 * @return
 */
dap_cli_cmd_t* dap_cli_server_cmd_get_first()
{
    return cli_commands;
}

/**
 * @brief dap_cli_server_cmd_find
 * @param a_name
 * @return
 */
dap_cli_cmd_t* dap_cli_server_cmd_find(const char *a_name)
{
    dap_cli_cmd_t *l_cmd_item = NULL;
    HASH_FIND_STR(cli_commands,a_name,l_cmd_item);
    return l_cmd_item;
}

dap_cli_cmd_aliases_t *dap_cli_server_alias_add(dap_cli_cmd_t *a_cmd, const char *a_pre_cmd, const char *a_alias)
{
    if (!a_alias || !a_cmd)
        return NULL;
    dap_cli_cmd_aliases_t *l_alias = DAP_NEW_Z(dap_cli_cmd_aliases_t);
    size_t l_alias_size = dap_strlen(a_alias);
    memcpy(l_alias->alias, a_alias, l_alias_size);
    if (a_pre_cmd) {
        size_t l_addition_size = dap_strlen(a_pre_cmd);
        memcpy(l_alias->addition, a_pre_cmd, l_addition_size);
    }
    l_alias->standard_command = a_cmd;
    HASH_ADD_STR(s_command_alias, alias, l_alias);
    return l_alias;
}

dap_cli_cmd_t *dap_cli_server_cmd_find_by_alias(const char *a_alias, char **a_append, char **a_ncmd)
{
    dap_cli_cmd_aliases_t *l_alias = NULL;
    HASH_FIND_STR(s_command_alias, a_alias, l_alias);
    if (!l_alias)
        return NULL;
    *a_append = l_alias->addition[0] ? dap_strdup(l_alias->addition) : NULL;
    *a_ncmd = dap_strdup(l_alias->standard_command->name);
    return l_alias->standard_command;
}

static void *s_cli_cmd_exec(void *a_arg) {
    cli_cmd_arg_t *l_arg = (cli_cmd_arg_t*)a_arg;

    // Dead-client check: the request sat in the pthread-create/scheduling
    // path for some amount of time; if the caller has already disconnected
    // there is nobody left to deliver the reply to, so running the command
    // at all would be pure waste — and, for a mutating command like
    // tx_create_json, would place a transaction in mempool whose "hash"
    // reply nobody will ever receive (the exact production symptom this
    // fixes). No output is produced or sent in that case.
    if (!s_cli_live_client_check(l_arg->es_uid)) {
        debug_if(s_debug_cli, L_INFO, "Client for es "DAP_FORMAT_ESOCKET_UUID" disconnected before command execution, skipping", l_arg->es_uid);
        dap_cli_server_backpressure_release(l_arg->is_heavy);
        DAP_DEL_MULTY(l_arg->buf, l_arg);
        return NULL;
    }

    // Cooperative cancellation: publish the client uuid this thread is
    // working for, so a HEAVY command's hot loop can poll
    // dap_cli_server_client_is_alive() deep inside shared helper code (DEX
    // history/OHLCV, ledger UTXO scans, block dump/list, tx_history -all)
    // without threading a uuid parameter through every call in between.
    s_cli_cmd_current_client_uuid = l_arg->es_uid;
    char    *l_ret = s_cli_cmd_exec_ex(l_arg->buf, l_arg->restricted);
    s_cli_cmd_current_client_uuid = 0;
    char    *l_full_ret = dap_strdup_printf("HTTP/1.1 200 OK\r\n"
                                            "Content-Length: %zu\r\n"
                                            "Content-Type: application/json\r\n"
                                            "Access-Control-Allow-Origin: *\r\n"
                                            "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
                                            "Access-Control-Allow-Headers: Content-Type\r\n"
                                            "Processing-Time: %"DAP_UINT64_FORMAT_U"\r\n"
                                            "Node-Type: %s\r\n"
                                            "Node-Version: %s\r\n\r\n"
                                            "%s", 
                                            dap_strlen(l_ret), 
                                            (uint64_t)(dap_nanotime_now() - l_arg->time_start), 
                                            dap_config_get_item_bool_default(g_config, "cli-server", "allowed_cmd_control", false)
                                                ? "Public" : "Private", 
                                            "CellframeNode, " DAP_VERSION ", " BUILD_TS ", " BUILD_HASH, 
                                             l_ret);
    DAP_DELETE(l_ret);
    dap_events_socket_write_mt(l_arg->worker, l_arg->es_uid, l_full_ret, dap_strlen(l_full_ret));
    dap_cli_server_backpressure_release(l_arg->is_heavy);
    // TODO: pagination
    DAP_DEL_MULTY(l_arg->buf, /* l_full_ret, */ l_arg);
    return NULL;
}

static char *s_cli_cmd_exec_ex(char *a_req_str, bool a_restricted)
{
    dap_json_rpc_request_t *request = dap_json_rpc_request_from_json(a_req_str, s_cli_version);
    if ( !request )
        return NULL;
    int l_verbose = 0;
    // command is found
    char *cmd_name = request->method;
    dap_cli_cmd_t *l_cmd = dap_cli_server_cmd_find(cmd_name);
    bool l_finded_by_alias = false;
    char *l_append_cmd = NULL;
    char *l_ncmd = NULL;
    if (!l_cmd) {
        l_cmd = dap_cli_server_cmd_find_by_alias(cmd_name, &l_append_cmd, &l_ncmd);
        l_finded_by_alias = true;
    }
    dap_json_rpc_params_t *params = request->params;

    char *str_cmd = dap_json_rpc_params_get(params, 0);
    if (!str_cmd)
        str_cmd = cmd_name;
    int res = -1;
    char *str_reply = NULL;
    json_object* l_json_arr_reply = json_object_new_array();
    if (l_cmd && a_restricted) {
        log_it(L_WARNING,"Command \"%s\" is restricted", l_cmd->name);
        dap_json_rpc_error_add(l_json_arr_reply, -1, "Command \"%s\" is restricted", l_cmd->name);
    } else if (!l_cmd) {
        dap_json_rpc_error_add(l_json_arr_reply, -1, "can't recognize command=%s", str_cmd);
        log_it(L_ERROR,"Reply string: \"%s\"", str_reply);
    } else {
        if(l_cmd->overrides.log_cmd_call)
            l_cmd->overrides.log_cmd_call(str_cmd);
        else {
            char *l_str_cmd = dap_strdup(str_cmd);
            char *l_ptr = strstr(l_str_cmd, "-password");
            if (l_ptr) {
                l_ptr += 10;
                while(l_ptr[0] != '\0' && l_ptr[0] != ';') {
                    *l_ptr = '*';
                    l_ptr +=1;
                }
            }
            debug_if( dap_config_get_item_bool_default(g_config, "cli-server", "debug-more", false),
                      L_DEBUG, "execute command=%s", l_str_cmd );
            DAP_DELETE(l_str_cmd);
        }

        char **l_argv = dap_strsplit(str_cmd, ";", -1);
        int l_argc = 0;
        // Count argc
        while (l_argv[l_argc] != NULL)
            l_argc++;
        // Support alias
        if (l_finded_by_alias) {
            cmd_name = l_ncmd;
            DAP_FREE(l_argv[0]);
            l_argv[0] = l_ncmd;
            if (l_append_cmd) {
                l_argc++;
                char **al_argv = DAP_NEW_Z_COUNT(char*, l_argc + 1);
                al_argv[1] = l_ncmd;
                al_argv[1] = l_append_cmd;
                for (int i = 1; i < l_argc; i++)
                    al_argv[i + 1] = l_argv[i];
                DAP_DEL_Z(l_argv);
                l_argv = al_argv;
            }
        }
        // Call the command function
        if(l_cmd &&  l_argv && l_cmd->func) {
            if (json_commands(cmd_name)) {
                res = l_cmd->func(l_argc, l_argv, (void *)&l_json_arr_reply, request->version);
            } else if (l_cmd->arg_func) {
                res = l_cmd->func_ex(l_argc, l_argv, l_cmd->arg_func, (void *)&str_reply, request->version);
            } else {
                res = l_cmd->func(l_argc, l_argv, (void *)&str_reply, request->version);
            }
        } else if (l_cmd) {
            log_it(L_WARNING,"NULL arguments for input for command \"%s\"", str_cmd);
            dap_json_rpc_error_add(l_json_arr_reply, -1, "NULL arguments for input for command \"%s\"", str_cmd);
        }else {
            log_it(L_WARNING,"No function for command \"%s\" but it registred?!", str_cmd);
            dap_json_rpc_error_add(l_json_arr_reply, -1, "No function for command \"%s\" but it registred?!", str_cmd);
        }
        // find '-verbose' command
        l_verbose = dap_cli_server_cmd_find_option_val(l_argv, 1, l_argc, "-verbose", NULL);
        dap_strfreev(l_argv);
    }
    char *reply_body = NULL;
    // -verbose
    if(l_verbose) {
        if (str_reply) {
            reply_body = dap_strdup_printf("%d\r\nret_code: %d\r\n%s\r\n", res, res, str_reply);
            DAP_DELETE(str_reply);
        } else {
            json_object* json_res = json_object_new_object();
            json_object_object_add(json_res, "ret_code", json_object_new_int(res));
            json_object_array_add(l_json_arr_reply, json_res);
        }
    } else
        reply_body = str_reply;

    // create response
    dap_json_rpc_response_t* response = reply_body
            ? dap_json_rpc_response_create(reply_body, TYPE_RESPONSE_STRING, request->id, request->version)
            : dap_json_rpc_response_create(json_object_get(l_json_arr_reply), TYPE_RESPONSE_JSON, request->id, request->version);
    json_object_put(l_json_arr_reply);
    char* response_string = dap_json_rpc_response_to_string(response);
    dap_json_rpc_response_free(response);
    dap_json_rpc_request_free(request);
    return response_string ? response_string : dap_strdup("Error");
}

DAP_INLINE char *dap_cli_cmd_exec(char *a_req_str)
{
    return s_cli_cmd_exec_ex(a_req_str, false);
}

DAP_INLINE int dap_cli_server_get_version()
{
    return s_cli_version;
}
