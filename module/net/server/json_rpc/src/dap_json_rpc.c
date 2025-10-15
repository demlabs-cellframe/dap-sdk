#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_json_rpc_response_handler.h"
#include "dap_json_rpc_request.h"
#include "dap_json_rpc_response.h"
#include "dap_json_rpc_errors.h"
#include "dap_json_rpc_params.h"
#include "dap_http_server.h"
#include "dap_http_simple.h"
#include "dap_pkey.h"
#include "dap_config.h"
#include "dap_enc_http.h"
#include "dap_enc_msrln.h"
#include "dap_enc_ks.h"
#include "dap_enc_key.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "/exec_cmd"

#define KEX_KEY_STR_SIZE 128

static bool exec_cmd_module = false;

// Method handlers registry (hash table by method name)
typedef struct dap_json_rpc_method_handler_item {
    char *method_name;
    dap_json_rpc_method_handler_t handler;
    void *user_data;
    UT_hash_handle hh;
} dap_json_rpc_method_handler_item_t;

static dap_json_rpc_method_handler_item_t *s_method_handlers = NULL;
static pthread_rwlock_t s_method_handlers_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// URL handlers registry (hash table by URL)
typedef struct dap_json_rpc_url_handler_item {
    char *url;
    dap_json_rpc_url_handler_t handler;
    void *user_data;
    UT_hash_handle hh;
} dap_json_rpc_url_handler_item_t;

static dap_json_rpc_url_handler_item_t *s_url_handlers = NULL;
static pthread_rwlock_t s_url_handlers_rwlock = PTHREAD_RWLOCK_INITIALIZER;
typedef struct dap_exec_cmd_pkey {
    dap_hash_fast_t pkey;
    UT_hash_handle hh;
} dap_exec_cmd_pkey_t;
static dap_exec_cmd_pkey_t *s_exec_cmd_map;
static pthread_rwlock_t s_exec_cmd_rwlock;

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
        DAP_DELETE(l_pkey);
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

dap_http_client_callback_error_t * dap_json_rpc_error_callback() {
    return NULL;
}

bool dap_json_rpc_exec_cmd_inited(){
    return exec_cmd_module;
}

void dap_json_rpc_deinit()
{
    dap_json_rpc_map_deinit();
    
    // Cleanup method handlers
    pthread_rwlock_wrlock(&s_method_handlers_rwlock);
    dap_json_rpc_method_handler_item_t *method_item, *method_tmp;
    HASH_ITER(hh, s_method_handlers, method_item, method_tmp) {
        HASH_DEL(s_method_handlers, method_item);
        DAP_DELETE(method_item->method_name);
        DAP_DELETE(method_item);
    }
    pthread_rwlock_unlock(&s_method_handlers_rwlock);
    
    // Cleanup URL handlers
    pthread_rwlock_wrlock(&s_url_handlers_rwlock);
    dap_json_rpc_url_handler_item_t *url_item, *url_tmp;
    HASH_ITER(hh, s_url_handlers, url_item, url_tmp) {
        HASH_DEL(s_url_handlers, url_item);
        DAP_DELETE(url_item->url);
        DAP_DELETE(url_item);
    }
    pthread_rwlock_unlock(&s_url_handlers_rwlock);
}

/**
 * @brief Register method handler for specific RPC method
 * @param a_method_name Method name (e.g., "wallet_balance")
 * @param a_handler Handler function
 * @param a_user_data User data to pass to handler
 * @return 0 on success, negative on error
 */
int dap_json_rpc_register_method_handler(const char *a_method_name, dap_json_rpc_method_handler_t a_handler, void *a_user_data) {
    if (!a_method_name || !a_handler) {
        log_it(L_ERROR, "JSON-RPC: invalid arguments for method registration");
        return -EINVAL;
    }
    
    pthread_rwlock_wrlock(&s_method_handlers_rwlock);
    
    // Check if already exists
    dap_json_rpc_method_handler_item_t *existing = NULL;
    HASH_FIND_STR(s_method_handlers, a_method_name, existing);
    if (existing) {
        log_it(L_WARNING, "JSON-RPC: replacing existing handler for method '%s'", a_method_name);
        existing->handler = a_handler;
        existing->user_data = a_user_data;
        pthread_rwlock_unlock(&s_method_handlers_rwlock);
        return 0;
    }
    
    // Create new handler item
    dap_json_rpc_method_handler_item_t *item = DAP_NEW_Z(dap_json_rpc_method_handler_item_t);
    if (!item) {
        pthread_rwlock_unlock(&s_method_handlers_rwlock);
        log_it(L_ERROR, "JSON-RPC: memory allocation failed");
        return -ENOMEM;
    }
    
    item->method_name = dap_strdup(a_method_name);
    item->handler = a_handler;
    item->user_data = a_user_data;
    
    HASH_ADD_STR(s_method_handlers, method_name, item);
    pthread_rwlock_unlock(&s_method_handlers_rwlock);
    
    log_it(L_INFO, "JSON-RPC: registered method handler for '%s'", a_method_name);
    return 0;
}

/**
 * @brief Register URL handler for entire endpoint
 * @param a_url URL path (e.g., "/exec_cmd")
 * @param a_handler Handler function
 * @param a_user_data User data to pass to handler
 * @return 0 on success, negative on error
 */
int dap_json_rpc_register_url_handler(const char *a_url, dap_json_rpc_url_handler_t a_handler, void *a_user_data) {
    if (!a_url || !a_handler) {
        log_it(L_ERROR, "JSON-RPC: invalid arguments for URL registration");
        return -EINVAL;
    }
    
    pthread_rwlock_wrlock(&s_url_handlers_rwlock);
    
    // Check if already exists
    dap_json_rpc_url_handler_item_t *existing = NULL;
    HASH_FIND_STR(s_url_handlers, a_url, existing);
    if (existing) {
        log_it(L_WARNING, "JSON-RPC: replacing existing handler for URL '%s'", a_url);
        existing->handler = a_handler;
        existing->user_data = a_user_data;
        pthread_rwlock_unlock(&s_url_handlers_rwlock);
        return 0;
    }
    
    // Create new handler item
    dap_json_rpc_url_handler_item_t *item = DAP_NEW_Z(dap_json_rpc_url_handler_item_t);
    if (!item) {
        pthread_rwlock_unlock(&s_url_handlers_rwlock);
        log_it(L_ERROR, "JSON-RPC: memory allocation failed");
        return -ENOMEM;
    }
    
    item->url = dap_strdup(a_url);
    item->handler = a_handler;
    item->user_data = a_user_data;
    
    HASH_ADD_STR(s_url_handlers, url, item);
    pthread_rwlock_unlock(&s_url_handlers_rwlock);
    
    log_it(L_INFO, "JSON-RPC: registered URL handler for '%s'", a_url);
    return 0;
}

/**
 * @brief Unregister method handler
 */
void dap_json_rpc_unregister_method_handler(const char *a_method_name) {
    if (!a_method_name)
        return;
    
    pthread_rwlock_wrlock(&s_method_handlers_rwlock);
    dap_json_rpc_method_handler_item_t *item = NULL;
    HASH_FIND_STR(s_method_handlers, a_method_name, item);
    if (item) {
        HASH_DEL(s_method_handlers, item);
        DAP_DELETE(item->method_name);
        DAP_DELETE(item);
        log_it(L_INFO, "JSON-RPC: unregistered method handler for '%s'", a_method_name);
    }
    pthread_rwlock_unlock(&s_method_handlers_rwlock);
}

/**
 * @brief Unregister URL handler
 */
void dap_json_rpc_unregister_url_handler(const char *a_url) {
    if (!a_url)
        return;
    
    pthread_rwlock_wrlock(&s_url_handlers_rwlock);
    dap_json_rpc_url_handler_item_t *item = NULL;
    HASH_FIND_STR(s_url_handlers, a_url, item);
    if (item) {
        HASH_DEL(s_url_handlers, item);
        DAP_DELETE(item->url);
        DAP_DELETE(item);
        log_it(L_INFO, "JSON-RPC: unregistered URL handler for '%s'", a_url);
    }
    pthread_rwlock_unlock(&s_url_handlers_rwlock);
}

/**
 * @brief Process JSON-RPC request
 * @param a_request_str JSON-RPC request string
 * @param a_url URL path (used to find URL handler if registered)
 * @return JSON-RPC response string (caller must free)
 */
char *dap_json_rpc_process_request(const char *a_request_str, const char *a_url) {
    if (!a_request_str) {
        log_it(L_ERROR, "JSON-RPC: null request string");
        return dap_strdup("{\"error\":\"Invalid request\"}");
    }
    
    // First, check if there's a URL-specific handler
    if (a_url) {
        pthread_rwlock_rdlock(&s_url_handlers_rwlock);
        dap_json_rpc_url_handler_item_t *url_handler = NULL;
        HASH_FIND_STR(s_url_handlers, a_url, url_handler);
        if (url_handler) {
            dap_json_rpc_url_handler_t handler = url_handler->handler;
            void *user_data = url_handler->user_data;
            pthread_rwlock_unlock(&s_url_handlers_rwlock);
            
            log_it(L_DEBUG, "JSON-RPC: using URL handler for '%s'", a_url);
            return handler(a_request_str, user_data);
        }
        pthread_rwlock_unlock(&s_url_handlers_rwlock);
    }
    
    // No URL handler, try method-based routing
    // Parse JSON-RPC request
    int l_cli_version = 1; // Default version
    dap_json_rpc_request_t *request = dap_json_rpc_request_from_json(a_request_str, l_cli_version);
    if (!request) {
        log_it(L_ERROR, "JSON-RPC: failed to parse request");
        return dap_strdup("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}");
    }
    
    // Find method handler
    pthread_rwlock_rdlock(&s_method_handlers_rwlock);
    dap_json_rpc_method_handler_item_t *method_handler = NULL;
    HASH_FIND_STR(s_method_handlers, request->method, method_handler);
    
    if (!method_handler) {
        pthread_rwlock_unlock(&s_method_handlers_rwlock);
        log_it(L_WARNING, "JSON-RPC: no handler for method '%s'", request->method);
        
        // Create error response
        dap_json_rpc_response_t *response = dap_json_rpc_response_create(
            dap_strdup_printf("Method '%s' not found", request->method),
            TYPE_RESPONSE_STRING, request->id, request->version);
        char *response_str = dap_json_rpc_response_to_string(response);
        dap_json_rpc_response_free(response);
        dap_json_rpc_request_free(request);
        return response_str;
    }
    
    // Call method handler
    dap_json_rpc_method_handler_t handler = method_handler->handler;
    void *user_data = method_handler->user_data;
    pthread_rwlock_unlock(&s_method_handlers_rwlock);
    
    log_it(L_DEBUG, "JSON-RPC: calling method handler for '%s'", request->method);
    char *handler_result = handler(request->params, request->version, user_data);
    
    // Create JSON-RPC response
    dap_json_rpc_response_t *response = handler_result
            ? dap_json_rpc_response_create(handler_result, TYPE_RESPONSE_STRING, request->id, request->version)
            : dap_json_rpc_response_create(dap_strdup("null"), TYPE_RESPONSE_STRING, request->id, request->version);

    char *response_string = dap_json_rpc_response_to_string(response);
    
    // Cleanup
    if (handler_result)
        DAP_DELETE(handler_result);
    dap_json_rpc_response_free(response);
    dap_json_rpc_request_free(request);

    return response_string ? response_string : dap_strdup("{\"error\":\"Internal error\"}");
}

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    log_it(L_DEBUG,"Proc enc http exec_cmd request");
    http_status_code_t *return_code = (http_status_code_t *)a_arg;

    enc_http_delegate_t *l_dg = enc_http_request_decode(a_http_simple);

    if(l_dg){
        char l_channels_str[256];
        // Use default encryption type instead of calling dap_stream_get_preferred_encryption_type()
        dap_enc_key_type_t l_enc_type = DAP_ENC_KEY_TYPE_SIG_DILITHIUM;
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
                    l_is_legacy = false;
                }else if(strcmp(l_subtok_name,"enc_key_size")==0){
                    l_enc_key_size = (size_t) atoi(l_subtok_value);
                    if (l_enc_key_size > l_dg->request_size )
                        l_enc_key_size = 32;
                    l_is_legacy = false;
                }else if(strcmp(l_subtok_name,"enc_headers")==0){
                    l_enc_headers = atoi(l_subtok_value);
                }
            }
            l_tok = strtok_r(NULL, ",", &l_tok_tmp);
        }
        *return_code = Http_Status_OK;
        log_it(L_DEBUG,"Encryption type %s (enc headers %d)",dap_enc_get_type_name(l_enc_type), l_enc_headers);
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
        char *l_res_str = dap_json_rpc_request_handler(l_dg->request, l_dg->request_size);
        if (l_res_str) {
            enc_http_reply(l_dg, l_res_str, strlen(l_res_str));
            DAP_DELETE(l_res_str);
        } else {
            dap_json_t* l_json_obj_res = dap_json_array_new();
            dap_json_t* l_error_msg = dap_json_object_new_string("Wrong request");
            dap_json_array_add(l_json_obj_res, l_error_msg);
            const char *l_json_str_res = dap_json_to_string(l_json_obj_res);
            size_t l_strlen = l_json_str_res ? strlen(l_json_str_res) : 0;
            enc_http_reply(l_dg, (char*)l_json_str_res, l_strlen);
            dap_json_object_free(l_json_obj_res);
            log_it(L_ERROR, "Wrong request");
            *return_code = Http_Status_BadRequest;
        }
        *return_code = Http_Status_OK;
        enc_http_reply_encode(a_http_simple,l_dg);
        enc_http_delegate_delete(l_dg);
    } else {
        log_it(L_ERROR,"Wrong request");
        *return_code = Http_Status_BadRequest;
    }
}

bool dap_json_rpc_get_int64_uint64(dap_json_t *a_json, const char *a_key, void *a_out, bool a_is_uint64)
{
    if(!a_json || !a_key || !a_out)
        return false;
    if (a_is_uint64) {
        *(uint64_t*)a_out = dap_json_object_get_uint64(a_json, a_key);
    } else {
        *(int64_t*)a_out = dap_json_object_get_int64(a_json, a_key);
    }
    return true;
}

const char *dap_json_rpc_get_text(dap_json_t *a_json, const char *a_key)
{
    if(!a_json || !a_key)
        return NULL;
    return dap_json_is_string(a_json) ? dap_json_object_get_string(a_json, a_key) : NULL;
}
