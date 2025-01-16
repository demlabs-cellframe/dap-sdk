#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_json_rpc_response_handler.h"
#include "dap_http_server.h"
#include "dap_pkey.h"
#include "dap_config.h"
#include "dap_enc_http.h"
#include "dap_enc_msrln.h"
#include "dap_stream_session.h"
#include "dap_stream.h"
#include "dap_enc_ks.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "/exec_cmd"

#define KEX_KEY_STR_SIZE 128

static bool exec_cmd_module = false;
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

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    log_it(L_DEBUG,"Proc enc http exec_cmd request");
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
