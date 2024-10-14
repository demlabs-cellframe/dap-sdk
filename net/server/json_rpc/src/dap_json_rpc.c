#include "dap_json_rpc.h"
#include "dap_json_rpc_request_handler.h"
#include "dap_json_rpc_response_handler.h"
#include "dap_http_server.h"
#include "dap_pkey.h"
#include "dap_config.h"
#include "dap_cli_server.h"
#include "dap_chain_net.h"

#define LOG_TAG "dap_json_rpc_rpc"
#define DAP_EXEC_CMD_URL "exec_cmd"

static int com_exec_cmd(int argc, char **argv, void **reply);

static bool init_module = false;
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

}

int dap_json_rpc_init(dap_server_t* a_http_server, dap_config_t *a_config)
{
    init_module = true;
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
    dap_json_rpc_request_init(DAP_EXEC_CMD_URL);
    dap_cli_server_cmd_add ("exec_cmd", com_exec_cmd, "Exec cmd\n", "Exec cmd\n");
    dap_http_simple_proc_add(l_http, DAP_EXEC_CMD_URL, 24000, dap_json_rpc_http_proc);
    return 0;
}

void dap_json_rpc_deinit()
{
    dap_json_rpc_map_deinit();
}

void dap_json_rpc_http_proc(dap_http_simple_t *a_http_simple, void *a_arg)
{
    log_it(L_INFO, "Proc exec_cmd request");
    http_status_code_t *l_return_code = (http_status_code_t*)a_arg;
    *l_return_code = Http_Status_OK;
    strcpy(a_http_simple->reply_mime, "application/json");

    // const char* l_query = a_http_simple->http_client->in_query_string;
    // uint32_t l_query_length = a_http_simple->http_client->in_query_string_len;
    *l_return_code = Http_Status_OK;
    if (!a_http_simple->request){
        *l_return_code = Http_Status_NoContent;
        dap_http_simple_reply_f(a_http_simple, "JSON-RPC request was not formed. "
                                          "JSON-RPC must represent a method containing objects from an object, "
                                          "this is a string the name of the method, id is a number and params "
                                          "is an array that can contain strings, numbers and boolean values.");
    }
    dap_json_rpc_request_handler(a_http_simple->request, a_http_simple);
}

static int com_exec_cmd(int argc, char **argv, void **reply) {
    json_object ** a_json_arr_reply = (json_object **) reply;
    const char * l_cmd_arg_str = NULL, * l_ip_str = NULL, * l_port_str = NULL, * l_net_str = NULL;
    int arg_index = 1;
    dap_cli_server_cmd_find_option_val(argv, arg_index, argc, "-cmd", &l_cmd_arg_str);
    dap_cli_server_cmd_find_option_val(argv, arg_index, argc, "-ip", &l_ip_str);
    dap_cli_server_cmd_find_option_val(argv, arg_index, argc, "-port", &l_port_str);
    dap_cli_server_cmd_find_option_val(argv, arg_index, argc, "-net", &l_net_str);

    if (!l_cmd_arg_str || ! l_ip_str || !l_net_str) {
        dap_json_rpc_error_add(-1, "Need args -cmd, -ip, -net");
    }
    dap_chain_net_t* l_net = NULL;
    l_net = dap_chain_net_by_name(l_net_str);

    dap_json_rpc_params_t * params = dap_json_rpc_params_create();
    char *l_cmd_str = dap_strdup(l_cmd_arg_str);
    for(int i = 0; l_cmd_str[i] != '\0'; i++) {
        if (l_cmd_str[i] == ',')
            l_cmd_str[i] = ';';
    }
    dap_json_rpc_params_add_data(params, l_cmd_str, TYPE_PARAM_STRING);
    uint64_t l_id_response = dap_json_rpc_response_get_new_id();
    char ** l_cmd_arr_str = dap_strsplit(l_cmd_str, ";", -1);
    dap_json_rpc_request_t *a_request = dap_json_rpc_request_creation(l_cmd_arr_str[0], params, l_id_response);
    dap_strfreev(l_cmd_arr_str);
    // char * request_str = dap_json_rpc_request_to_json_string(a_request);
    dap_chain_node_addr_t l_node_addr;
    dap_chain_node_addr_from_str(&l_node_addr, l_ip_str);
    dap_chain_node_info_t *l_remote = dap_chain_node_info_read(l_net, &l_node_addr);
    DAP_DEL_Z(l_cmd_str);
    if (!dap_json_rpc_request_send(a_request, dap_json_rpc_response_accepted, l_remote->ext_host, l_remote->ext_port, dap_json_rpc_error_callback))
        log_it(L_INFO, "com_exec sent request to %s:%d", l_remote->ext_host, l_remote->ext_port);

    json_object_array_add(*a_json_arr_reply, json_object_new_string("DONE"));
    return 0;
}
