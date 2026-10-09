#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_json_rpc_response_handler.h"
#include "dap_http_server.h"
#include "dap_pkey.h"
#include "dap_config.h"
#include "dap_enc_http.h"
#include "dap_cli_server.h"
#include "dap_enc_msrln.h"
#include "dap_stream_session.h"
#include "dap_stream.h"
#include "dap_enc_ks.h"

#define LOG_TAG "dap_json_rpc_rpc"

static bool s_debug_more = false;
#define DAP_EXEC_CMD_URL "/exec_cmd"

#define KEX_KEY_STR_SIZE 128

static bool exec_cmd_module = false;
typedef struct dap_exec_cmd_pkey {
    dap_hash_fast_t pkey;
    UT_hash_handle hh;
} dap_exec_cmd_pkey_t;
static dap_exec_cmd_pkey_t *s_exec_cmd_map;
static pthread_rwlock_t s_exec_cmd_rwlock = PTHREAD_RWLOCK_INITIALIZER;

static int dap_json_rpc_map_init(dap_config_t *a_config) {
    s_exec_cmd_map = NULL;
    uint16_t  l_array_length = 0;
    const char ** l_pkeys = dap_config_get_array_str(a_config, "server", "exec_cmd", &l_array_length);
    for (size_t i = 0; i < l_array_length; i++) {
        dap_hash_fast_t l_pkey = {0};
        dap_chain_hash_fast_from_str(l_pkeys[i], &l_pkey);
        dap_exec_cmd_pkey_t* l_exec_cmd_pkey = DAP_NEW_Z(dap_exec_cmd_pkey_t);
        l_exec_cmd_pkey->pkey = l_pkey;
        HASH_ADD(hh, s_exec_cmd_map, pkey, sizeof(dap_exec_cmd_pkey_t), l_exec_cmd_pkey);
    }
    return 0;
}

static int dap_json_rpc_map_deinit() {
    dap_exec_cmd_pkey_t* l_pkey = NULL, *tmp = NULL;
    HASH_ITER(hh, s_exec_cmd_map, l_pkey, tmp) {
        HASH_DEL(s_exec_cmd_map, l_pkey);
        DAP_DEL_Z(l_pkey);
    }
    return 0;
}

bool dap_check_node_pkey_in_map(dap_hash_fast_t *a_pkey){
    dap_exec_cmd_pkey_t* l_exec_cmd_pkey = NULL, *tmp = NULL;
    HASH_ITER(hh, s_exec_cmd_map, l_exec_cmd_pkey, tmp) {
        if (dap_hash_fast_compare(&l_exec_cmd_pkey->pkey, a_pkey))
            return true;
    }
    return false;
}

dap_client_http_callback_error_t * dap_json_rpc_error_callback() {
    return NULL;
}

int dap_json_rpc_init(dap_server_t* a_http_server, dap_config_t *a_config)
{
    exec_cmd_module = true;
    if (!a_http_server) {
        log_it(L_ERROR, "Can't find server for %s", DAP_EXEC_CMD_URL);
        return -1;
    }

    dap_http_server_t * l_http = DAP_HTTP_SERVER(a_http_server);
    if(!l_http){
        log_it(L_ERROR, "Can't find http server for %s", DAP_EXEC_CMD_URL);
        return -2;
    }

    dap_json_rpc_map_init(a_config);
    dap_http_simple_proc_add(l_http, "/exec_cmd", 24000, dap_json_rpc_http_proc);
    return 0;
}

bool dap_json_rpc_exec_cmd_inited(){
    return exec_cmd_module;
}

void dap_json_rpc_deinit()
{
    dap_json_rpc_map_deinit();
}

/* *** Standalone plain-HTTP JSON-RPC service ([rpc-server]) *** */

static dap_server_t *s_rpc_service_server = NULL;
static char *s_rpc_service_index_cache = NULL;          /* GET / answer, built once */
static char **s_rpc_allowed_cmds_owned = NULL;          /* ownership of the strings below */
static const char **s_rpc_allowed_cmds = NULL;          /* NULL-terminated public command list */
static bool s_rpc_service_restricted = true;            /* allowed_cmd_control */
static size_t s_rpc_max_request_size = 1024 * 1024;     /* max_request_size */

bool dap_json_rpc_method_is_public(const char *a_method, const char **a_allowed_cmds)
{
    return a_method && *a_method && a_allowed_cmds && *a_allowed_cmds &&
           !!dap_str_find(a_allowed_cmds, a_method);
}

// Small JSON body for replies that carry no command result (errors, the index, /health).
static void s_rpc_service_reply_str(dap_http_simple_t *a_http_simple, const char *a_str)
{
    dap_http_simple_reply(a_http_simple, (void *)a_str, strlen(a_str));
}

static void s_rpc_service_reply_error(dap_http_simple_t *a_http_simple,
                                      http_status_code_t *a_return_code, http_status_code_t a_status,
                                      const char *a_message)
{
    *a_return_code = a_status;
    json_object *l_arr = json_object_new_array();
    if (l_arr) {
        dap_json_rpc_error_add(l_arr, -1, "%s", a_message);
        char *l_str = dap_strdup(json_object_to_json_string(l_arr));
        if (l_str) {
            s_rpc_service_reply_str(a_http_simple, l_str);
            DAP_DELETE(l_str);
        }
        json_object_put(l_arr);
    }
}

void dap_json_rpc_http_plain_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    dap_return_if_fail(a_http_simple);
    http_status_code_t *l_return_code = (http_status_code_t *)a_arg;
    if (!l_return_code)                                   /* proc registered by the service itself */
        return;
    // Per-source budget, shared with the CLI port and the signed /exec_cmd path: a caller must not
    // get an unthrottled stream just because it found the plain RPC port. Loopback is exempt.
    if (a_http_simple->http_client && a_http_simple->http_client->esocket &&
        !dap_cli_server_rate_limit_check_addr(&a_http_simple->http_client->esocket->addr_storage)) {
        *l_return_code = Http_Status_TooManyRequests;
        return;
    }

    // No body: the command index (GET) - nothing to execute, so no backpressure slot is taken.
    // The command set is registered at startup and static afterwards: build the index once and
    // reuse it for the lifetime of the service.
    if (!a_http_simple->request_size || !a_http_simple->request_str) {
        if (!s_rpc_service_index_cache) {
            bool l_all_public = !s_rpc_service_restricted;
            s_rpc_service_index_cache =
                dap_cli_server_cmd_list_json(l_all_public ? NULL : s_rpc_allowed_cmds, l_all_public);
        }
        if (!s_rpc_service_index_cache) {
            *l_return_code = Http_Status_InternalServerError;
            return;
        }
        *l_return_code = Http_Status_OK;
        s_rpc_service_reply_str(a_http_simple, s_rpc_service_index_cache);
        return;
    }
    if (a_http_simple->request_size > s_rpc_max_request_size) {
        *l_return_code = Http_Status_PayloadTooLarge;
        return;
    }

    // The body is parsed exactly once: the same tree drives the access check, the heavy
    // classification and the execution (the executor consumes it).
    enum json_tokener_error l_jterr;
    json_object *l_req = json_tokener_parse_verbose(a_http_simple->request_str, &l_jterr);
    if (l_jterr != json_tokener_success || !l_req ||
            !json_object_is_type(l_req, json_type_object)) {
        json_object_put(l_req);
        s_rpc_service_reply_error(a_http_simple, l_return_code, Http_Status_BadRequest,
                                  "Wrong request");
        return;
    }

    // Access control for the plain endpoint: a non-loopback caller may only run the commands the
    // configuration made public. Loopback tooling keeps the full command set, exactly like the
    // CLI port. With allowed_cmd_control=false the whole set is public - an explicit opt-in.
    bool l_loopback = dap_cli_server_addr_is_loopback(
            a_http_simple->http_client && a_http_simple->http_client->esocket
                ? &a_http_simple->http_client->esocket->addr_storage : NULL);
    if (!l_loopback && s_rpc_service_restricted) {
        json_object *l_jobj_method = NULL;
        const char *l_method = json_object_object_get_ex(l_req, "method", &l_jobj_method) &&
                               json_object_is_type(l_jobj_method, json_type_string)
                                   ? json_object_get_string(l_jobj_method) : NULL;
        if (!dap_json_rpc_method_is_public(l_method, s_rpc_allowed_cmds)) {
            log_it(l_method ? L_WARNING : L_ERROR,
                   "HTTP RPC: refused %s for a non-loopback caller",
                   l_method ? l_method : "a request without a method");
            json_object_put(l_req);
            s_rpc_service_reply_error(a_http_simple, l_return_code, Http_Status_Forbidden,
                                      "Command is not exposed on this endpoint");
            return;
        }
    }

    // Same acquire/release gate as the CLI port and the signed path: a heavy command must not
    // starve the shared proc-thread pool (GlobalDB I/O runs there too).
    bool l_is_heavy = false;
    if (!dap_cli_server_backpressure_acquire_json(l_req, &l_is_heavy)) {
        json_object_put(l_req);
        dap_http_header_add(&a_http_simple->ext_headers, "Retry-After", "1");
        s_rpc_service_reply_error(a_http_simple, l_return_code, Http_Status_ServiceUnavailable,
                                  "Node is busy, try again later");
        return;
    }
    char *l_response = dap_cli_cmd_exec_json(l_req);   // consumes l_req
    dap_cli_server_backpressure_release(l_is_heavy);
    if (!l_response) {
        s_rpc_service_reply_error(a_http_simple, l_return_code, Http_Status_BadRequest,
                                  "Wrong request");
        return;
    }
    *l_return_code = Http_Status_OK;
    s_rpc_service_reply_str(a_http_simple, l_response);
    DAP_DELETE(l_response);
}

static void s_rpc_service_health_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    http_status_code_t *l_return_code = (http_status_code_t *)a_arg;
    if (!l_return_code)
        return;
    char l_reason[256] = { 0 };
    bool l_ready = dap_cli_server_ready_check(l_reason, sizeof(l_reason));
    *l_return_code = l_ready ? Http_Status_OK : Http_Status_ServiceUnavailable;
    char *l_body = NULL;
    if (l_ready)
        l_body = dap_strdup("{\"status\":\"ok\"}");
    else {
        if (!l_reason[0])
            dap_strncpy(l_reason, "not ready", sizeof(l_reason) - 1);
        dap_http_header_add(&a_http_simple->ext_headers, "Retry-After", "5");
        l_body = dap_strdup_printf("{\"status\":\"unavailable\",\"reason\":\"%s\"}", l_reason);
    }
    if (l_body) {
        s_rpc_service_reply_str(a_http_simple, l_body);
        DAP_DELETE(l_body);
    }
}

static void s_rpc_service_allowed_cmds_free(void)
{
    if (s_rpc_allowed_cmds_owned) {
        for (char **l_it = s_rpc_allowed_cmds_owned; *l_it; ++l_it)
            DAP_DELETE(*l_it);
        DAP_DELETE(s_rpc_allowed_cmds_owned);
    }
    s_rpc_allowed_cmds_owned = NULL;
    s_rpc_allowed_cmds = NULL;
}

int dap_json_rpc_service_init(dap_config_t *a_config)
{
    if (!dap_config_get_item_bool_default(a_config, "rpc-server", "enable", false)) {
        log_it(L_NOTICE, "HTTP RPC service is off ([rpc-server] enable)");
        return 0;
    }
    // allowed_cmd_control: true (default) = restricted, non-loopback callers run only
    // allowed_cmd; false = the whole set is public. Mirrors the CLI port's option of the same
    // name, so operators have one mental model for both entry points.
    s_rpc_service_restricted = dap_config_get_item_bool_default(a_config, "rpc-server",
                                                                "allowed_cmd_control", true);
    s_rpc_max_request_size = dap_config_get_item_uint64_default(a_config, "rpc-server",
                                                                "max_request_size", 1024 * 1024);
    uint16_t l_allowed_count = 0;
    const char **l_allowed_cfg = dap_config_get_array_str(a_config, "rpc-server", "allowed_cmd",
                                                          &l_allowed_count);
    if (l_allowed_cfg && *l_allowed_cfg) {
        // A single non-array value reports count 1 but is not an array (same handling as the CLI
        // port's allowed_cmd list).
        size_t l_n = 0;
        while (l_allowed_cfg[l_n])
            ++l_n;
        s_rpc_allowed_cmds_owned = DAP_NEW_Z_COUNT(char *, l_n + 1);
        if (!s_rpc_allowed_cmds_owned) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return -1;
        }
        for (size_t i = 0; i < l_n; ++i)
            s_rpc_allowed_cmds_owned[i] = dap_strdup(l_allowed_cfg[i]);
        s_rpc_allowed_cmds = (const char **)s_rpc_allowed_cmds_owned;
    }

    dap_server_t *l_server = dap_http_server_new("rpc-server", dap_get_appname());
    if (!l_server) {
        log_it(L_CRITICAL, "Can't create the HTTP RPC server ([rpc-server] section)");
        s_rpc_service_allowed_cmds_free();
        return -2;
    }
    dap_http_server_t *l_http = DAP_HTTP_SERVER(l_server);
    dap_http_simple_proc_add(l_http, "/", s_rpc_max_request_size, dap_json_rpc_http_plain_proc);
    dap_http_simple_proc_add(l_http, "/rpc", s_rpc_max_request_size, dap_json_rpc_http_plain_proc);
    dap_http_simple_proc_add(l_http, "/health", 512, s_rpc_service_health_proc);
    s_rpc_service_server = l_server;
    size_t l_public_count = 0;
    while (s_rpc_allowed_cmds && s_rpc_allowed_cmds[l_public_count])
        ++l_public_count;
    log_it(L_NOTICE, "HTTP RPC service started on the [rpc-server] listen-address: %s, %zu public command(s), body limit %zu bytes",
           s_rpc_service_restricted ? "restricted (loopback = all commands)" : "PUBLIC (all commands)",
           l_public_count, s_rpc_max_request_size);
    if (!s_rpc_service_restricted)
        log_it(L_WARNING, "HTTP RPC service: allowed_cmd_control=false - every command is callable by anyone");
    return 0;
}

void dap_json_rpc_service_deinit(void)
{
    if (s_rpc_service_server) {
        dap_server_delete(s_rpc_service_server);
        s_rpc_service_server = NULL;
    }
    DAP_DEL_Z(s_rpc_service_index_cache);
    s_rpc_service_allowed_cmds_free();
}

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    debug_if(s_debug_more, L_DEBUG,"Proc enc http exec_cmd request");
    http_status_code_t *return_code = (http_status_code_t *)a_arg;
    dap_stream_session_t *l_stream_session = NULL;
    bool l_new_session = false;

    enc_http_delegate_t *l_dg = enc_http_request_decode(a_http_simple);

    if(l_dg){
        size_t l_channels_str_size = sizeof(l_stream_session->active_channels);
        char l_channels_str[sizeof(l_stream_session->active_channels)];
        dap_enc_key_type_t l_enc_type = dap_stream_get_preferred_encryption_type();
        size_t l_enc_key_size = 32;
        int l_enc_headers = 0;
        bool l_is_legacy=true;
        char *l_tok_tmp;
        char *l_tok = strtok_r(l_dg->url_path, ",", &l_tok_tmp);
        while (l_tok) {
            char *l_subtok_name = l_tok;
            char *l_subtok_value = strchr(l_tok, '=');
            if (l_subtok_value && l_subtok_value != l_subtok_name) {
                *l_subtok_value++ = '\0';
                if (strcmp(l_subtok_name,"channels")==0 ){
                    strncpy(l_channels_str,l_subtok_value,sizeof (l_channels_str)-1);
                }else if(strcmp(l_subtok_name,"enc_type")==0){
                    l_enc_type = atoi(l_subtok_value);
                    // l_is_legacy = false;
                }else if(strcmp(l_subtok_name,"enc_key_size")==0){
                    l_enc_key_size = (size_t) atoi(l_subtok_value);
                    if (l_enc_key_size > l_dg->request_size )
                        l_enc_key_size = 32;
                    // l_is_legacy = false;
                }else if(strcmp(l_subtok_name,"enc_headers")==0){
                    l_enc_headers = atoi(l_subtok_value);
                }
            }
            l_tok = strtok_r(NULL, ",", &l_tok_tmp);
        }
        *return_code = Http_Status_OK;
        debug_if(s_debug_more, L_DEBUG,"Encryption type %s (enc headers %d)",dap_enc_get_type_name(l_enc_type), l_enc_headers);
        UNUSED(l_is_legacy);
        dap_http_header_t *l_hdr_key_id = dap_http_header_find(a_http_simple->http_client->in_headers, "KeyID");
        dap_enc_ks_key_t *l_ks_key = NULL;
        if (l_hdr_key_id) {
            l_ks_key = dap_enc_ks_find(l_hdr_key_id->value);
            if (!l_ks_key) {
                log_it(L_WARNING, "Key with ID %s not found", l_hdr_key_id->value);
                *return_code = Http_Status_BadRequest;
                return;
            }
        }
        char * l_res_str = dap_json_rpc_request_handler(l_dg->request, l_dg->request_size);
        if (l_res_str) {
            enc_http_reply(l_dg, l_res_str, strlen(l_res_str));
            DAP_DELETE(l_res_str);
        } else {
            json_object* l_json_obj_res = json_object_new_array();
            json_object_array_add(l_json_obj_res, json_object_new_string("Wrong request"));
            size_t l_strlen = 0;
            const char *l_json_str_res = json_object_to_json_string_length(l_json_obj_res, JSON_C_TO_STRING_SPACED, &l_strlen);
            enc_http_reply(l_dg, (char*)l_json_str_res, l_strlen);
            json_object_put(l_json_obj_res);
            log_it(L_ERROR,"Wrong request");
            *return_code = Http_Status_BadRequest;
        }
        enc_http_reply_encode(a_http_simple,l_dg);
        enc_http_delegate_delete(l_dg);
    } else {
        log_it(L_ERROR,"Wrong request");
        *return_code = Http_Status_BadRequest;
    }
}

DAP_INLINE bool dap_json_rpc_get_int64(struct json_object *a_json, const char *a_key, int64_t *a_out)
{
    struct json_object *l_json = NULL;
    dap_return_val_if_pass(!a_json || !a_key || !a_out || !(l_json = json_object_object_get(a_json, a_key)), false);
    *a_out = json_object_get_int64(l_json);
    return true;
}

DAP_INLINE bool dap_json_rpc_get_uint64(struct json_object *a_json, const char *a_key, uint64_t *a_out)
{
    struct json_object *l_json = NULL;
    dap_return_val_if_pass(!a_json || !a_key || !a_out || !(l_json = json_object_object_get(a_json, a_key)), false);
    *a_out = json_object_get_uint64(l_json);
    return true;
}

const char* dap_json_rpc_get_text(struct json_object *a_json, const char *a_key)
{
    if(!a_json || !a_key)
        return NULL;
    struct json_object *l_json = json_object_object_get(a_json, a_key);
    if(l_json && json_object_is_type(l_json, json_type_string)) {
        // Read text
        return json_object_get_string(l_json);
    }
    return NULL;
}