#include "dap_json_rpc_chain_node_cli_cmd.h"
#include "json.h"

#include "uthash.h"
#include "utlist.h"
#include "dap_string.h"
#include "dap_hash.h"
#include "dap_chain_common.h"
#include "dap_strfuncs.h"
#include "dap_list.h"
#include "dap_string.h"
#include "dap_cert.h"
#include "dap_cert_file.h"
#include "dap_file_utils.h"
#include "dap_enc_base58.h"
#include "dap_chain_wallet.h"
#include "dap_chain_wallet_internal.h"
#include "dap_chain_node.h"
#include "dap_global_db.h"
#include "dap_global_db_driver.h"
#include "dap_chain_node_client.h"
#include "dap_chain_node_cli_cmd.h"
#include "dap_chain_node_cli_cmd_tx.h"
#include "dap_chain_node_ping.h"
#include "dap_chain_net_srv.h"
#include "dap_chain_net_tx.h"
#include "dap_chain_block.h"
#include "dap_chain_cs_blocks.h"

#ifndef _WIN32
#include "dap_chain_net_news.h"
#endif
#include "dap_chain_cell.h"


#include "dap_enc_base64.h"
#include "json.h"
#ifdef DAP_OS_UNIX
#include <dirent.h>
#endif

#include "dap_chain_common.h"
#include "dap_chain_datum.h"
#include "dap_chain_datum_token.h"
#include "dap_chain_datum_tx_items.h"
#include "dap_chain_ledger.h"
#include "dap_chain_mempool.h"
#include "dap_chain_node_cli_cmd.h"
#include "dap_global_db.h"
#include "dap_global_db_remote.h"

#include "dap_stream_ch_chain_net.h"
#include "dap_stream_ch_chain.h"
#include "dap_stream_ch_chain_pkt.h"
#include "dap_stream_ch_chain_net_pkt.h"
#include "dap_enc_base64.h"
#include "dap_chain_net_srv_stake_pos_delegate.h"
#include "dap_chain_net_node_list.h"

#include "dap_json_rpc_chain_datum.h"
#include "dap_json_rpc_chain_node_cli_cmd_tx.h"
#include "dap_json_rpc_errors.h"

#define LOG_TAG "dap_json_rpc_chain_node_cli_cmd"

typedef enum dap_chain_node_cli_cmd_values_parse_net_chain_err_to_json {
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_INTERNAL_COMMAND_PROCESSING = 101,
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_STR_IS_NUL = 102,
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_NOT_FOUND = 103,
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CHAIN_NOT_FOUND = 104,
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CHAIN_STR_IS_NULL = 105,
    DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CONFIG_DEFAULT_DATUM = 106
} dap_chain_node_cli_cmd_values_parse_net_chain_err_to_json;
int dap_chain_node_cli_cmd_values_parse_net_chain_for_json(int *a_arg_index, int a_argc,
                                                           char **a_argv,
                                                           dap_chain_t **a_chain, dap_chain_net_t **a_net) {
    const char * l_chain_str = NULL;
    const char * l_net_str = NULL;

    // Net name
    if(a_net)
        dap_cli_server_cmd_find_option_val(a_argv, *a_arg_index, a_argc, "-net", &l_net_str);
    else {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_INTERNAL_COMMAND_PROCESSING,
                               "Error in internal command processing.");
        return DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_INTERNAL_COMMAND_PROCESSING;
    }

    // Select network
    if(!l_net_str) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_STR_IS_NUL, "%s requires parameter '-net'", a_argv[0]);
        return DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_STR_IS_NUL;
    }

    if((*a_net = dap_chain_net_by_name(l_net_str)) == NULL) { // Can't find such network
        char l_str_to_reply_chain[500] = {0};
        char *l_str_to_reply = NULL;
        sprintf(l_str_to_reply_chain, "%s can't find network \"%s\"\n", a_argv[0], l_net_str);
        l_str_to_reply = dap_strcat2(l_str_to_reply,l_str_to_reply_chain);
        dap_string_t* l_net_str = dap_cli_list_net();
        l_str_to_reply = dap_strcat2(l_str_to_reply,l_net_str->str);
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_NOT_FOUND, "%s can't find network \"%s\"\n%s", a_argv[0], l_net_str, l_str_to_reply);
        return DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_NET_NOT_FOUND;
    }

    // Chain name
    if(a_chain) {
        dap_cli_server_cmd_find_option_val(a_argv, *a_arg_index, a_argc, "-chain", &l_chain_str);

        // Select chain
        if(l_chain_str) {
            if ((*a_chain = dap_chain_net_get_chain_by_name(*a_net, l_chain_str)) == NULL) { // Can't find such chain
                char l_str_to_reply_chain[500] = {0};
                char *l_str_to_reply = NULL;
                sprintf(l_str_to_reply_chain, "%s requires parameter '-chain' to be valid chain name in chain net %s. Current chain %s is not valid\n",
                        a_argv[0], l_net_str, l_chain_str);
                l_str_to_reply = dap_strcat2(l_str_to_reply,l_str_to_reply_chain);
                dap_chain_t * l_chain;
                dap_chain_net_t * l_chain_net = *a_net;
                l_str_to_reply = dap_strcat2(l_str_to_reply,"\nAvailable chains:\n");
                DL_FOREACH(l_chain_net->pub.chains, l_chain) {
                    l_str_to_reply = dap_strcat2(l_str_to_reply,"\t");
                    l_str_to_reply = dap_strcat2(l_str_to_reply,l_chain->name);
                    l_str_to_reply = dap_strcat2(l_str_to_reply,"\n");
                }
                dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CHAIN_NOT_FOUND, l_str_to_reply);
                return DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CHAIN_NOT_FOUND;
            }
        }
        else if (!strcmp(a_argv[0], "token_decl")  || !strcmp(a_argv[0], "token_decl_sign")) {
            if (	(*a_chain = dap_chain_net_get_default_chain_by_chain_type(*a_net, CHAIN_TYPE_TOKEN)) == NULL )
            {
                dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CONFIG_DEFAULT_DATUM, "%s requires parameter '-chain' or set default datum "
                                             "type in chain configuration file");
                return DAP_CHAIN_NODE_CLI_CMD_VALUES_PARSE_NET_CHAIN_ERR_CONFIG_DEFAULT_DATUM;
            }
        }
    }
    return 0;
}


void s_com_mempool_list_print_for_chain_rpc(dap_chain_net_t * a_net, dap_chain_t * a_chain, const char * a_add, json_object *a_json_obj, const char *a_hash_out_type, bool a_fast){
    char * l_gdb_group_mempool = dap_chain_net_get_gdb_group_mempool_new(a_chain);
    if(!l_gdb_group_mempool){
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_MEMPOOL_LIST_CAN_NOT_GET_MEMPOOL_GROUP,
                               "%s.%s: chain not found\n", a_net->pub.name, a_chain->name);
    } else {
        int l_removed = 0;
        json_object *l_obj_chain = json_object_new_object();
        if (!l_obj_chain) {
            dap_json_rpc_allocated_error;
            return;
        }
        json_object *l_obj_chain_name  = json_object_new_string(a_chain->name);
        if (!l_obj_chain_name) {
            json_object_put(l_obj_chain);
            dap_json_rpc_allocated_error;
            return;
        }
        json_object_object_add(l_obj_chain, "name", l_obj_chain_name);
        dap_chain_mempool_filter(a_chain, &l_removed);
        json_object *l_jobj_removed = json_object_new_int(l_removed);
        if (!l_jobj_removed) {
            json_object_put(l_obj_chain);
            dap_json_rpc_allocated_error;
            return;
        }
        json_object_object_add(l_obj_chain, "removed", l_jobj_removed);
        size_t l_objs_size = 0;
        dap_global_db_obj_t * l_objs = dap_global_db_get_all_sync(l_gdb_group_mempool, &l_objs_size);
        size_t l_objs_addr = 0;
        json_object  *l_jobj_datums;
        if (l_objs_size == 0) {
            l_jobj_datums = json_object_new_null();
        } else {
            l_jobj_datums = json_object_new_array();
            if (!l_jobj_datums) {
                json_object_put(l_obj_chain);
                dap_json_rpc_allocated_error;
                return;
            }
        }
        for(size_t i = 0; i < l_objs_size; i++) {
            dap_chain_datum_t *l_datum = (dap_chain_datum_t *)l_objs[i].value;
            dap_time_t l_ts_create = (dap_time_t) l_datum->header.ts_create;
            if (!l_datum->header.data_size || (l_datum->header.data_size > l_objs[i].value_len)) {
                log_it(L_ERROR, "Trash datum in GDB %s.%s, key: %s data_size:%u, value_len:%zu",
                        a_net->pub.name, a_chain->name, l_objs[i].key, l_datum->header.data_size, l_objs[i].value_len);
                dap_global_db_del_sync(l_gdb_group_mempool, l_objs[i].key);
                continue;
            }
            json_object *l_jobj_datum = dap_chain_datum_to_json(l_datum);
            if (!l_jobj_datum){
                json_object_put(l_jobj_datums);
                json_object_put(l_obj_chain);
                dap_global_db_objs_delete(l_objs, l_objs_size);
                dap_json_rpc_error_add(DAP_JSON_RPC_ERR_CODE_SERIALIZATION_DATUM_TO_JSON,
                                       "An error occurred while serializing a datum to JSON.");
                return;
            }
            json_object *l_jobj_warning = NULL;
            if(a_add)
            {
                size_t l_emisssion_size = l_datum->header.data_size;
                dap_chain_datum_token_emission_t *l_emission = dap_chain_datum_emission_read(l_datum->data, &l_emisssion_size);
                if (!l_emission) {
                    json_object_put(l_jobj_datum);
                    json_object_put(l_jobj_datums);
                    json_object_put(l_obj_chain);
                    dap_global_db_objs_delete(l_objs, l_objs_size);
                    dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_MEMPOOL_LIST_CAN_NOT_READ_EMISSION,
                                           "Failed to read the emission.");
                    return;
                }
                dap_chain_datum_tx_t *l_tx = (dap_chain_datum_tx_t *)l_datum->data;

                uint32_t l_tx_items_count = 0;
                uint32_t l_tx_items_size = l_tx->header.tx_items_size;
                bool l_f_found = false;

                dap_chain_addr_t *l_addr = dap_chain_addr_from_str(a_add);
                if (!l_addr) {
                    json_object_put(l_obj_chain);
                    json_object_put(l_jobj_datum);
                    json_object_put(l_jobj_datums);
                    dap_global_db_objs_delete(l_objs, l_objs_size);
                    dap_json_rpc_allocated_error;
                    return;
                }
                switch (l_datum->header.type_id) {
                case DAP_CHAIN_DATUM_TX:
                    while (l_tx_items_count < l_tx_items_size)
                    {
                        uint8_t *item = l_tx->tx_items + l_tx_items_count;
                        size_t l_item_tx_size = dap_chain_datum_item_tx_get_size(item);
                        dap_chain_hash_fast_t l_hash;
                        bool t =false;
                        if(!memcmp(l_addr, &((dap_chain_tx_out_old_t*)item)->addr, sizeof(dap_chain_addr_t))||
                            !memcmp(l_addr, &((dap_chain_tx_out_t*)item)->addr, sizeof(dap_chain_addr_t))||
                            !memcmp(l_addr, &((dap_chain_tx_out_ext_t*)item)->addr, sizeof(dap_chain_addr_t)))
                        {
                            l_f_found = true;                            
                            break;
                        }
                        l_hash = ((dap_chain_tx_in_t*)item)->header.tx_prev_hash;
                        // if(dap_chain_mempool_find_addr_ledger(a_net->pub.ledger,&l_hash,l_addr)){l_f_found=true;break;}
                        // l_hash = ((dap_chain_tx_in_cond_t*)item)->header.tx_prev_hash;
                        // if(dap_chain_mempool_find_addr_ledger(a_net->pub.ledger,&l_hash,l_addr)){l_f_found=true;break;}
                        // l_hash = ((dap_chain_tx_in_ems_t*)item)->header.token_emission_hash;
                        // if(dap_chain_mempool_find_addr_ledger(a_net->pub.ledger,&l_hash,l_addr)){l_f_found=true;break;}
                        // l_hash = ((dap_chain_tx_in_ems_ext_t*)item)->header.ext_tx_hash;
                        // if(dap_chain_mempool_find_addr_ledger(a_net->pub.ledger,&l_hash,l_addr)){l_f_found=true;break;}

                        l_tx_items_count += l_item_tx_size;
                    }
                    if(l_f_found)
                        l_objs_addr++;
                    break;
                case DAP_CHAIN_DATUM_TOKEN_EMISSION:
                    if(!memcmp(l_addr, &l_emission->hdr.address, sizeof(dap_chain_addr_t)))
                    {
                        l_objs_addr++;
                        l_f_found = true;
                    }
                    break;
                case DAP_CHAIN_DATUM_DECREE:

                    break;
                default:
                    //continue;
                    break;
                }
                DAP_DELETE(l_emission);
                DAP_DELETE(l_addr);
                if(!l_f_found)
                    continue;
            }
            char buf[50] = {[0]='\0'};
            dap_hash_fast_t l_data_hash;
            char l_data_hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE] = {[0]='\0'};
            dap_hash_fast(l_datum->data,l_datum->header.data_size,&l_data_hash);
            dap_hash_fast_to_str(&l_data_hash,l_data_hash_str,DAP_CHAIN_HASH_FAST_STR_SIZE);
            if (strcmp(l_data_hash_str, l_objs[i].key)){
                char *l_wgn = dap_strdup_printf("Key field in DB %s does not match datum's hash %s\n",
                                                l_objs[i].key, l_data_hash_str);
                if (!l_wgn) {
                    dap_global_db_objs_delete(l_objs, l_objs_size);
                    json_object_put(l_obj_chain);
                    json_object_put(l_jobj_datum);
                    json_object_put(l_jobj_datums);
                    dap_json_rpc_allocated_error;
                    return;
                }
                l_jobj_warning = json_object_new_string(l_wgn);
                DAP_DELETE(l_wgn);
                if (!l_jobj_warning) {
                    dap_global_db_objs_delete(l_objs, l_objs_size);
                    json_object_put(l_obj_chain);
                    json_object_put(l_jobj_datum);
                    json_object_put(l_jobj_datums);
                    dap_json_rpc_allocated_error;
                    return;
                }
            }
            const char *l_type = NULL;
            DAP_DATUM_TYPE_STR(l_datum->header.type_id, l_type)
            const char *l_token_ticker = NULL;
            bool l_is_unchained = false;
            if (l_datum->header.type_id == DAP_CHAIN_DATUM_TX) { // TODO rewrite it for support of multichannel & conditional transactions
                dap_chain_tx_in_ems_t *obj_token = (dap_chain_tx_in_ems_t*)dap_chain_datum_tx_item_get((dap_chain_datum_tx_t*)l_datum->data, NULL, TX_ITEM_TYPE_IN_EMS, NULL);
                if (obj_token) {
                    l_token_ticker = obj_token->header.ticker;
                } else {
                    if (!a_fast) {
                        l_token_ticker = s_tx_get_main_ticker((dap_chain_datum_tx_t*)l_datum->data, a_net, &l_is_unchained);
                    }
                }
                if (l_token_ticker) {
                    json_object *l_main_ticker = json_object_new_string(l_token_ticker);
                    if (!l_main_ticker) {
                        dap_global_db_objs_delete(l_objs, l_objs_size);
                        json_object_put(l_obj_chain);
                        json_object_put(l_jobj_datum);
                        json_object_put(l_jobj_datums);
                        dap_json_rpc_allocated_error;
                        return;
                    }
                    json_object_object_add(l_jobj_datum, "main_ticker", l_main_ticker);
                }
            }
            json_object_array_add(l_jobj_datums, l_jobj_datum);
        }
        dap_global_db_objs_delete(l_objs, l_objs_size);
        json_object_object_add(l_obj_chain, "datums", l_jobj_datums);
        if (a_add) {
            json_object *l_ev_addr = json_object_new_int64(l_objs_size);
            if (!l_ev_addr) {
                json_object_put(l_obj_chain);
                dap_json_rpc_allocated_error;
                return;
            }
            json_object_object_add(l_obj_chain, "Number_elements_per_address", l_ev_addr);
        }
        json_object_array_add(a_json_obj, l_obj_chain);
        DAP_DELETE(l_gdb_group_mempool);
    }
}

int com_mempool_list_rpc(int a_argc, char **a_argv, json_object **a_json_reply)
{
    int arg_index = 1;
    dap_chain_t * l_chain = NULL;
    dap_chain_net_t * l_net = NULL;
    const char *l_addr_base58 = NULL;
    bool l_fast = false;

    const char * l_hash_out_type = "hex";
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-H", &l_hash_out_type);
    dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index, a_argc, a_argv, &l_chain, &l_net);
    if (dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-addr", &l_addr_base58) && !l_addr_base58) {
        dap_json_rpc_error_add(-2, "Parameter '-addr' require <addr>");
        return -2;
    }
    l_fast = (dap_cli_server_cmd_check_option(a_argv, arg_index, a_argc, "-fast") != -1) ? true : false;
    if(!l_net)
        return -1;
    json_object *l_ret = json_object_new_object();
    if (!l_ret) {
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object *l_ret_net = json_object_new_string(l_net->pub.name);
    if (!l_ret_net) {
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object_object_add(l_ret, "net", l_ret_net);
    json_object *l_ret_chains = json_object_new_array();
    if (!l_ret_chains){
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    if(l_chain)
        s_com_mempool_list_print_for_chain(l_net, l_chain, l_addr_base58, l_ret_chains, l_hash_out_type, l_fast);
    else
        DL_FOREACH(l_net->pub.chains, l_chain)
            s_com_mempool_list_print_for_chain(l_net, l_chain, l_addr_base58, l_ret_chains, l_hash_out_type, l_fast);

    json_object_object_add(l_ret, "chains", l_ret_chains);
    json_object_array_add(*a_json_reply, l_ret);
    return 0;
}

typedef enum com_mempool_delete_err_list{
    COM_MEMPOOL_DELETE_ERR_DATUM_NOT_FOUND_IN_ARGUMENT = DAP_JSON_RPC_ERR_CODE_METHOD_ERR_START,
    COM_MEMPOOL_DELETE_ERR_DATUM_NOT_FOUND
}com_mempool_delete_err_list_t;
/**
 * @brief com_mempool_delete
 * @param argc
 * @param argv
 * @param arg_func
 * @param a_str_reply
 * @return
 */
int com_mempool_delete_rpc(int a_argc, char **a_argv, json_object **a_json_reply)
{
    int arg_index = 1;
    dap_chain_t * l_chain = NULL;
    dap_chain_net_t * l_net = NULL;

    if(dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index, a_argc, a_argv, &l_chain, &l_net) != 0) {
        return -1;
    }
    const char * l_datum_hash_str = NULL;
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-datum", &l_datum_hash_str);
    if (l_datum_hash_str) {
        char *l_datum_hash_hex_str = (char *)l_datum_hash_str;
        // datum hash may be in hex or base58 format
        if(dap_strncmp(l_datum_hash_str, "0x", 2) && dap_strncmp(l_datum_hash_str, "0X", 2))
            l_datum_hash_hex_str = dap_enc_base58_to_hex_str_from_str(l_datum_hash_str);

        char * l_gdb_group_mempool = dap_chain_net_get_gdb_group_mempool_new(l_chain);
        if (!l_gdb_group_mempool) {
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        uint8_t *l_data_tmp = dap_global_db_get_sync(l_gdb_group_mempool, l_datum_hash_hex_str ? l_datum_hash_hex_str : l_datum_hash_str,
                                                     NULL, NULL, NULL);
        if(l_data_tmp && dap_global_db_del_sync(l_gdb_group_mempool, l_datum_hash_hex_str) == 0) {
            char *l_msg_str = dap_strdup_printf("Datum %s deleted", l_datum_hash_str);
            if (!l_msg_str) {
                dap_json_rpc_allocated_error;
                return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
            }
            json_object *l_msg = json_object_new_string(l_msg_str);
            DAP_DELETE(l_msg_str);
            if (!l_msg) {
                dap_json_rpc_allocated_error;
                return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
            }
            json_object_array_add(*a_json_reply, l_msg);
            return 0;
        } else {
            char *l_msg_str = dap_strdup_printf("Error! Can't find datum %s", l_datum_hash_str);
            if (!l_msg_str) {
               dap_json_rpc_allocated_error;
                return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
            }
            json_object *l_msg = json_object_new_string(l_msg_str);
            DAP_DELETE(l_msg_str);
            if (!l_msg) {
                dap_json_rpc_allocated_error;
                return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
            }
            json_object_array_add(*a_json_reply, l_msg);
            return COM_MEMPOOL_DELETE_ERR_DATUM_NOT_FOUND;
        }
        DAP_DELETE(l_gdb_group_mempool);
        DAP_DELETE(l_data_tmp);
        if (l_datum_hash_hex_str != l_datum_hash_str)
            DAP_DELETE(l_datum_hash_hex_str);
    } else {
        dap_json_rpc_error_add(COM_MEMPOOL_DELETE_ERR_DATUM_NOT_FOUND_IN_ARGUMENT, "Error! %s requires -datum <datum hash> option", a_argv[0]);
        return COM_MEMPOOL_DELETE_ERR_DATUM_NOT_FOUND_IN_ARGUMENT;
    }
}


typedef enum com_mempool_check_err_list {
    COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_CHAIN = DAP_JSON_RPC_ERR_CODE_METHOD_ERR_START,
    COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_NET,
    COM_MEMPOOL_CHECK_ERR_REQUIRES_DATUM_HASH,
    COM_MEMPOOL_CHECK_ERR_INCORRECT_HASH_STR,
    COM_MEMPOOL_CHECK_ERR_DATUM_NOT_FIND
}com_mempool_check_err_list_t;
/**
 * @brief com_mempool_check
 * @param argc
 * @param argv
 * @param arg_func
 * @param a_str_reply
 * @return
 */
int com_mempool_check_rpc(int a_argc, char **a_argv, json_object ** a_json_reply)
{
    int arg_index = 1;
    dap_chain_t * l_chain = NULL;
    dap_chain_net_t * l_net = NULL;

    if (dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index, a_argc, a_argv, NULL, &l_net))
        return -1;

    const char *l_chain_str = NULL;
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-chain", &l_chain_str);
    if (l_chain_str) {
        l_chain = dap_chain_net_get_chain_by_name(l_net, l_chain_str);
        if (!l_chain) {
            dap_json_rpc_error_add(COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_CHAIN, "%s requires parameter '-chain' to be valid chain name in chain net %s. Current chain %s is not valid",
                                   a_argv[0], l_net->pub.name, l_chain_str);
            return COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_CHAIN;
        }
    }

    if (l_net) {
        const char *l_datum_hash_str = NULL;
        dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-datum", &l_datum_hash_str);
        if (l_datum_hash_str) {
            char *l_datum_hash_hex_str = NULL;
            char *l_hash_out_type = "hex";
            // datum hash may be in hex or base58 format
            if (dap_strncmp(l_datum_hash_str, "0x", 2) && dap_strncmp(l_datum_hash_str, "0X", 2)) {
                l_hash_out_type = "base58";
                l_datum_hash_hex_str = dap_enc_base58_to_hex_str_from_str(l_datum_hash_str);
            } else
                l_datum_hash_hex_str = dap_strdup(l_datum_hash_str);
            dap_chain_datum_t *l_datum = NULL;
            char *l_chain_name = l_chain ? l_chain->name : NULL;
            bool l_found_in_chains = false;
            int l_ret_code = 0;
            dap_hash_fast_t l_atom_hash = {};
            if (l_chain)
                l_datum = s_com_mempool_check_datum_in_chain(l_chain, l_datum_hash_hex_str);
            else {
                dap_chain_t *it = NULL;
                DL_FOREACH(l_net->pub.chains, it) {
                    l_datum = s_com_mempool_check_datum_in_chain(it, l_datum_hash_hex_str);
                    if (l_datum) {
                        l_chain_name = it->name;
                        break;
                    }
                }
            }
            if (!l_datum) {
                l_found_in_chains = true;
                dap_hash_fast_t l_datum_hash;
                if (dap_chain_hash_fast_from_hex_str(l_datum_hash_hex_str, &l_datum_hash)) {
                    dap_json_rpc_error_add(COM_MEMPOOL_CHECK_ERR_INCORRECT_HASH_STR,
                                           "Incorrect hash string %s", l_datum_hash_str);
                    return COM_MEMPOOL_CHECK_ERR_INCORRECT_HASH_STR;
                }
                if (l_chain)
                    l_datum = l_chain->callback_datum_find_by_hash(l_chain, &l_datum_hash, &l_atom_hash, &l_ret_code);
                else {
                    dap_chain_t *it = NULL;
                    DL_FOREACH(l_net->pub.chains, it) {
                        l_datum = it->callback_datum_find_by_hash(it, &l_datum_hash, &l_atom_hash, &l_ret_code);
                        if (l_datum) {
                            l_chain_name = it->name;
                            break;
                        }
                    }
                }
            }
            DAP_DELETE(l_datum_hash_hex_str);
            json_object *l_jobj_datum = json_object_new_object();
            json_object *l_datum_hash = json_object_new_string(l_datum_hash_str);
            json_object *l_net_obj = json_object_new_string(l_net->pub.name);
            if (!l_jobj_datum || !l_datum_hash || !l_net_obj){
                json_object_put(l_jobj_datum);
                json_object_put(l_datum_hash);
                json_object_put(l_net_obj);
                dap_json_rpc_allocated_error;
                return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
            }
            json_object *l_chain_obj;
            if(l_chain_name) {
                l_chain_obj = json_object_new_string(l_chain_name);
                if (!l_chain_obj) {
                    json_object_put(l_jobj_datum);
                    json_object_put(l_datum_hash);
                    json_object_put(l_net_obj);
                    dap_json_rpc_allocated_error;
                    return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                }
            } else
                l_chain_obj = json_object_new_null();
            json_object_object_add(l_jobj_datum, "hash", l_datum_hash);
            json_object_object_add(l_jobj_datum, "net", l_net_obj);
            json_object_object_add(l_jobj_datum, "chain", l_chain_obj);
            json_object *l_find_bool;
            if (l_datum) {
                l_find_bool = json_object_new_boolean(TRUE);
                json_object *l_find_chain_or_mempool = json_object_new_string(l_found_in_chains ? "chain" : "mempool");
                if (!l_find_chain_or_mempool || !l_find_bool) {
                    json_object_put(l_find_chain_or_mempool);
                    json_object_put(l_find_bool);
                    json_object_put(l_jobj_datum);
                    dap_json_rpc_allocated_error;
                    return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                }
                json_object_object_add(l_jobj_datum, "find", l_find_bool);
                json_object_object_add(l_jobj_datum, "source", l_find_chain_or_mempool);
                if (l_found_in_chains) {
                    char l_atom_hash_str[DAP_CHAIN_HASH_FAST_STR_SIZE];
                    dap_chain_hash_fast_to_str(&l_atom_hash, l_atom_hash_str, DAP_CHAIN_HASH_FAST_STR_SIZE);
                    json_object *l_obj_atom = json_object_new_object();
                    json_object *l_jobj_atom_hash = json_object_new_string(l_atom_hash_str);
                    json_object *l_jobj_atom_err = json_object_new_string(dap_chain_ledger_tx_check_err_str(l_ret_code));
                    if (!l_obj_atom || !l_jobj_atom_hash || !l_jobj_atom_err) {
                        json_object_put(l_jobj_datum);
                        json_object_put(l_obj_atom);
                        json_object_put(l_jobj_atom_hash);
                        json_object_put(l_jobj_atom_err);
                        dap_json_rpc_allocated_error;
                        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                    }
                    json_object_object_add(l_obj_atom, "hash", l_jobj_atom_hash);
                    json_object_object_add(l_obj_atom, "err", l_jobj_atom_err);
                    json_object_object_add(l_jobj_datum, "atom", l_obj_atom);
                }
                json_object *l_datum_obj_inf = dap_chain_datum_to_json(l_datum);
                if (!l_datum_obj_inf) {
                    if (!l_found_in_chains)
                        DAP_DELETE(l_datum);
                    json_object_put(l_jobj_datum);
                    dap_json_rpc_error_add(DAP_JSON_RPC_ERR_CODE_SERIALIZATION_DATUM_TO_JSON,
                                           "Failed to serialize datum to JSON.");
                    return DAP_JSON_RPC_ERR_CODE_SERIALIZATION_DATUM_TO_JSON;
                }
                if (!l_found_in_chains)
                    DAP_DELETE(l_datum);
                json_object_array_add(*a_json_reply, l_jobj_datum);
                return 0;
            } else {
               l_find_bool = json_object_new_boolean(TRUE);
               if (!l_find_bool) {
                   json_object_put(l_jobj_datum);
                   dap_json_rpc_allocated_error;
                   return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
               }
                json_object_object_add(l_jobj_datum, "find", l_find_bool);
                json_object_array_add(*a_json_reply, l_jobj_datum);
                return COM_MEMPOOL_CHECK_ERR_DATUM_NOT_FIND;
            }
        } else {
            dap_json_rpc_error_add(COM_MEMPOOL_CHECK_ERR_REQUIRES_DATUM_HASH,
                                   "Error! %s requires -datum <datum hash> option", a_argv[0]);
            return COM_MEMPOOL_CHECK_ERR_REQUIRES_DATUM_HASH;
        }
    } else {
        dap_json_rpc_error_add(COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_NET, "Error! Need both -net <network name> param");
        return COM_MEMPOOL_CHECK_ERR_CAN_NOT_FIND_NET;
    }
}

typedef enum com_mempool_proc_list_error{
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_NODE_ROLE_NOT_FULL = DAP_JSON_RPC_ERR_CODE_METHOD_ERR_START,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_GET_DATUM_HASH_FROM_STR,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_DATUM_CORRUPT_SIZE_DATUM_NOT_EQUALS_SIZE_RECORD,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_GROUP_NAME,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_FIND_DATUM,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_CONVERT_DATUM_HASH_TO_DIGITAL_FORM,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_REAL_HASH_DATUM_DOES_NOT_MATCH_HASH_DATA_STRING,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_FALSE_VERIFY,
    DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_MOVE_TO_NO_CONCENSUS_FROM_MEMPOOL

}com_mempool_proc_list_error_t;
/**
 * @brief com_mempool_proc
 * process mempool datums
 * @param argc
 * @param argv
 * @param arg_func
 * @param a_str_reply
 * @return
 */
int com_mempool_proc_rpc(int a_argc, char **a_argv, json_object **a_json_reply)
{
    int arg_index = 1;
    dap_chain_t * l_chain = NULL;
    dap_chain_net_t * l_net = NULL;
    dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index, a_argc, a_argv, &l_chain, &l_net);
    if (!l_net || !l_chain)
        return -1;

//    if(*a_str_reply) {
//        DAP_DELETE(*a_str_reply);COM_MEMPOOL_CHECK_ERR_DATUM_NOT_FIND
//        *a_str_reply = NULL;
//    }

    // If full or light it doesnt work
    if(dap_chain_net_get_role(l_net).enums>= NODE_ROLE_FULL){
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_NODE_ROLE_NOT_FULL,
                               "Need master node role or higher for network %s to process this command", l_net->pub.name);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_NODE_ROLE_NOT_FULL;
    }

    const char * l_datum_hash_str = NULL;
    int ret = 0;
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-datum", &l_datum_hash_str);
    if (!l_datum_hash_str) {
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_GET_DATUM_HASH_FROM_STR,
                               "Error! %s requires -datum <datum hash> option", a_argv[0]);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_GET_DATUM_HASH_FROM_STR;
    }
    char *l_gdb_group_mempool = dap_chain_net_get_gdb_group_mempool_new(l_chain);
    if (!l_gdb_group_mempool){
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_GROUP_NAME,
                               "Failed to get mempool group name on network %s", l_net->pub.name);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_GROUP_NAME;
    }
    size_t l_datum_size=0;

    char *l_datum_hash_hex_str;
    // datum hash may be in hex or base58 format
    if(dap_strncmp(l_datum_hash_str, "0x", 2) && dap_strncmp(l_datum_hash_str, "0X", 2))
        l_datum_hash_hex_str = dap_enc_base58_to_hex_str_from_str(l_datum_hash_str);
    else
        l_datum_hash_hex_str = dap_strdup(l_datum_hash_str);

    dap_chain_datum_t * l_datum = l_datum_hash_hex_str ? (dap_chain_datum_t*) dap_global_db_get_sync(l_gdb_group_mempool, l_datum_hash_hex_str,
                                                                                   &l_datum_size, NULL, NULL ) : NULL;
    size_t l_datum_size2 = l_datum? dap_chain_datum_size( l_datum): 0;
    if (l_datum_size != l_datum_size2) {
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_DATUM_CORRUPT_SIZE_DATUM_NOT_EQUALS_SIZE_RECORD, "Error! Corrupted datum %s, size by datum headers is %zd when in mempool is only %zd bytes",
                                            l_datum_hash_hex_str, l_datum_size2, l_datum_size);
        DAP_DELETE(l_datum_hash_hex_str);
        DAP_DELETE(l_gdb_group_mempool);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_DATUM_CORRUPT_SIZE_DATUM_NOT_EQUALS_SIZE_RECORD;
    }
    if (!l_datum) {
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_FIND_DATUM,
                               "Error! Can't find datum %s", l_datum_hash_str);
        DAP_DELETE(l_datum_hash_hex_str);
        DAP_DELETE(l_gdb_group_mempool);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_FIND_DATUM;
    }
    dap_hash_fast_t l_datum_hash, l_real_hash;
    if (dap_chain_hash_fast_from_hex_str(l_datum_hash_hex_str, &l_datum_hash)) {
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_CONVERT_DATUM_HASH_TO_DIGITAL_FORM,
                               "Error! Can't convert datum hash string %s to digital form",
                               l_datum_hash_hex_str);
        DAP_DELETE(l_datum_hash_hex_str);
        DAP_DELETE(l_gdb_group_mempool);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_CONVERT_DATUM_HASH_TO_DIGITAL_FORM;
    }
    dap_hash_fast(l_datum->data, l_datum->header.data_size, &l_real_hash);
    if (!dap_hash_fast_compare(&l_datum_hash, &l_real_hash)) {
        dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_REAL_HASH_DATUM_DOES_NOT_MATCH_HASH_DATA_STRING,
                               "Error! Datum's real hash doesn't match datum's hash string %s",
                                            l_datum_hash_hex_str);
        DAP_DELETE(l_datum_hash_hex_str);
        DAP_DELETE(l_gdb_group_mempool);
        return DAP_COM_MEMPOOL_PROC_LIST_ERROR_REAL_HASH_DATUM_DOES_NOT_MATCH_HASH_DATA_STRING;
    }
    char buf[50];
    dap_time_t l_ts_create = (dap_time_t)l_datum->header.ts_create;
    const char *l_type = NULL;
    DAP_DATUM_TYPE_STR(l_datum->header.type_id, l_type);
    json_object *l_jobj_res = json_object_new_object();
    json_object *l_jobj_datum = json_object_new_object();
    json_object *l_jobj_hash = json_object_new_string(l_datum_hash_str);
    json_object *l_jobj_type = json_object_new_string(l_type);
    json_object *l_jobj_ts_created = json_object_new_object();
    json_object *l_jobj_ts_created_time_stamp = json_object_new_uint64(l_ts_create);
    char *l_ts_created_str = dap_ctime_r(&l_ts_create, buf);
    if (!l_ts_created_str || !l_jobj_ts_created || !l_jobj_ts_created_time_stamp || !l_jobj_type ||
        !l_jobj_hash || !l_jobj_datum || !l_jobj_res) {
        json_object_put(l_jobj_res);
        json_object_put(l_jobj_datum);
        json_object_put(l_jobj_hash);
        json_object_put(l_jobj_type);
        json_object_put(l_jobj_ts_created);
        json_object_put(l_jobj_ts_created_time_stamp);
        DAP_DEL_Z(l_ts_created_str);
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object *l_jobj_ts_created_str = json_object_new_string(l_ts_created_str);
    DAP_DEL_Z(l_ts_created_str);
    json_object *l_jobj_data_size = json_object_new_uint64(l_datum->header.data_size);
    if (!l_jobj_ts_created_str || !l_jobj_data_size) {
        json_object_put(l_jobj_res);
        json_object_put(l_jobj_datum);
        json_object_put(l_jobj_hash);
        json_object_put(l_jobj_type);
        json_object_put(l_jobj_ts_created);
        json_object_put(l_jobj_ts_created_time_stamp);
        json_object_put(l_jobj_ts_created_str);
        json_object_put(l_jobj_data_size);
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object_object_add(l_jobj_datum, "hash", l_jobj_hash);
    json_object_object_add(l_jobj_datum, "type", l_jobj_type);
    json_object_object_add(l_jobj_ts_created, "time_stamp", l_jobj_ts_created_time_stamp);
    json_object_object_add(l_jobj_ts_created, "str", l_jobj_ts_created_str);
    json_object_object_add(l_jobj_datum, "ts_created", l_jobj_ts_created);
    json_object_object_add(l_jobj_datum, "data_size", l_jobj_data_size);
    json_object_object_add(l_jobj_res, "datum", l_jobj_datum);
    json_object *l_jobj_verify = json_object_new_object();
    if (!l_jobj_verify) {
        json_object_put(l_jobj_res);
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    int l_verify_datum = dap_chain_net_verify_datum_for_add(l_chain, l_datum, &l_datum_hash);
    if (l_verify_datum){
        json_object *l_jobj_verify_err = json_object_new_string(dap_chain_net_verify_datum_err_code_to_str(l_datum, l_verify_datum));
        json_object *l_jobj_verify_status = json_object_new_boolean(FALSE);
        if (!l_jobj_verify_status || !l_jobj_verify_err) {
            json_object_put(l_jobj_verify_status);
            json_object_put(l_jobj_verify_err);
            json_object_put(l_jobj_verify);
            json_object_put(l_jobj_res);
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object_object_add(l_jobj_verify, "isProcessed", l_jobj_verify_status);
        json_object_object_add(l_jobj_verify, "error", l_jobj_verify_err);
        ret = DAP_COM_MEMPOOL_PROC_LIST_ERROR_FALSE_VERIFY;
    } else {
        if (l_chain->callback_add_datums) {
            if (l_chain->callback_add_datums(l_chain, &l_datum, 1) == 0) {
                json_object *l_jobj_verify_status = json_object_new_boolean(FALSE);
                if (!l_jobj_verify_status) {
                    json_object_put(l_jobj_verify_status);
                    json_object_put(l_jobj_verify);
                    json_object_put(l_jobj_res);
                    dap_json_rpc_allocated_error;
                    return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                }
                json_object_object_add(l_jobj_verify, "isProcessed", l_jobj_verify_status);
                ret = DAP_COM_MEMPOOL_PROC_LIST_ERROR_FALSE_VERIFY;
            } else {
                json_object *l_jobj_verify_status = json_object_new_boolean(TRUE);
                if (!l_jobj_verify_status) {
                    json_object_put(l_jobj_verify);
                    json_object_put(l_jobj_res);
                    dap_json_rpc_allocated_error;
                    return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                }
                json_object_object_add(l_jobj_verify, "isProcessed", l_jobj_verify_status);
                if (dap_global_db_del_sync(l_gdb_group_mempool, l_datum_hash_hex_str)){
                    json_object *l_jobj_wrn_text = json_object_new_string("Can't delete datum from mempool!");
                    if (!l_jobj_wrn_text) {
                        json_object_put(l_jobj_verify);
                        json_object_put(l_jobj_res);
                        dap_json_rpc_allocated_error;
                        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                    }
                    json_object_object_add(l_jobj_verify, "warning", l_jobj_wrn_text);
                } else {
                    json_object *l_jobj_text = json_object_new_string("Removed datum from mempool.");
                    if (!l_jobj_text) {
                        json_object_put(l_jobj_verify);
                        json_object_put(l_jobj_res);
                        dap_json_rpc_allocated_error;
                        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
                    }
                    json_object_object_add(l_jobj_verify, "notice", l_jobj_text);
                }
            }
        } else {
            dap_json_rpc_error_add(DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_MOVE_TO_NO_CONCENSUS_FROM_MEMPOOL, "Error! Can't move to no-concensus chains from mempool");
            ret = DAP_COM_MEMPOOL_PROC_LIST_ERROR_CAN_NOT_MOVE_TO_NO_CONCENSUS_FROM_MEMPOOL;
        }
    }
    DAP_DELETE(l_gdb_group_mempool);
    DAP_DELETE(l_datum_hash_hex_str);
    json_object_object_add(l_jobj_res, "verify", l_jobj_verify);
    json_object_array_add(*a_json_reply, l_jobj_res);
    return ret;
}

/**
 * @breif com_mempool_proc_all
 * @param argc
 * @param argv
 * @param a_str_reply
 * @return
 */
int com_mempool_proc_all_rpc(int argc, char ** argv, json_object **a_json_reply) {
    dap_chain_net_t *l_net = NULL;
    dap_chain_t *l_chain = NULL;
    int arg_index = 1;

    dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index, argc, argv, &l_chain, &l_net);
    if (!l_net || !l_chain)
        return -1;

    json_object *l_ret = json_object_new_object();
    if (!l_ret){
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    if(!dap_chain_net_by_id(l_chain->net_id)) {
        char *l_warn_str = dap_strdup_printf("%s.%s: chain not found\n", l_net->pub.name,
                                             l_chain->name);
        if (!l_warn_str) {
            json_object_put(l_ret);
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object *l_warn_obj = json_object_new_string(l_warn_str);
        DAP_DELETE(l_warn_str);
        if (!l_warn_obj){
            json_object_put(l_ret);
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object_object_add(l_ret, "warning", l_warn_obj);
    }

#ifdef DAP_TPS_TEST
    dap_chain_ledger_set_tps_start_time(l_net->pub.ledger);
#endif
    dap_chain_node_mempool_process_all(l_chain, true);
    char *l_str_result = dap_strdup_printf("The entire mempool has been processed in %s.%s.",
                                           l_net->pub.name, l_chain->name);
    if (!l_str_result) {
        json_object_put(l_ret);
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object *l_obj_result = json_object_new_string(l_str_result);
    DAP_DEL_Z(l_str_result);
    if (!l_obj_result) {
        json_object_put(l_ret);
        dap_json_rpc_allocated_error;
        return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
    }
    json_object_object_add(l_ret, "result", l_obj_result);
    json_object_array_add(*a_json_reply, l_obj_result);
    return 0;
}


typedef enum com_mempool_add_ca_error_list{
    COM_MEMPOOL_ADD_CA_ERROR_NET_NOT_FOUND = DAP_JSON_RPC_ERR_CODE_METHOD_ERR_START,
    COM_MEMPOOL_ADD_CA_ERROR_NO_CAINS_FOR_CA_DATUM_IN_NET,
    COM_MEMPOOL_ADD_CA_ERROR_REQUIRES_PARAMETER_CA_NAME,
    COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_FIND_CERTIFICATE,
    COM_MEMPOOL_ADD_CA_ERROR_CORRUPTED_CERTIFICATE_WITHOUT_KEYS,
    COM_MEMPOOL_ADD_CA_ERROR_CERTIFICATE_HAS_PRIVATE_KEY_DATA,
    COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_SERIALIZE,
    COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_PLACE_CERTIFICATE
}com_mempool_add_ca_error_list_t;
/**
 * @brief com_mempool_add_ca
 * @details Place public CA into the mempool
 * @param a_argc
 * @param a_argv
 * @param a_str_reply
 * @return
 */
int com_mempool_add_ca_rpc(int a_argc,  char ** a_argv, json_object ** a_json_reply)
{
    int arg_index = 1;

    // Read params
    const char * l_ca_name = NULL;
    dap_chain_net_t * l_net = NULL;
    dap_chain_t * l_chain = NULL;

    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-ca_name", &l_ca_name);
    dap_chain_node_cli_cmd_values_parse_net_chain_for_json(&arg_index,a_argc, a_argv, &l_chain, &l_net);
    if ( l_net == NULL ){
        return COM_MEMPOOL_ADD_CA_ERROR_NET_NOT_FOUND;
    } else if (a_json_reply && *a_json_reply) {
        DAP_DELETE(*a_json_reply);
        *a_json_reply = NULL;
    }

    // Chech for chain if was set or not
    if ( l_chain == NULL){
       // If wasn't set - trying to auto detect
        l_chain = dap_chain_net_get_chain_by_chain_type( l_net, CHAIN_TYPE_CA );
        if (l_chain == NULL) { // If can't auto detect
            // clean previous error code
            dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_NO_CAINS_FOR_CA_DATUM_IN_NET,
                                   "No chains for CA datum in network \"%s\"", l_net->pub.name);
            return COM_MEMPOOL_ADD_CA_ERROR_NO_CAINS_FOR_CA_DATUM_IN_NET;
        }
    }
    // Check if '-ca_name' wasn't specified
    if (l_ca_name == NULL){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_REQUIRES_PARAMETER_CA_NAME,
                               "mempool_add_ca_public requires parameter '-ca_name' to specify the certificate name");
        return COM_MEMPOOL_ADD_CA_ERROR_REQUIRES_PARAMETER_CA_NAME;
    }

    // Find certificate with specified key
    dap_cert_t * l_cert = dap_cert_find_by_name( l_ca_name );
    if( l_cert == NULL ){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_FIND_CERTIFICATE,
                               "Can't find \"%s\" certificate", l_ca_name);
        return COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_FIND_CERTIFICATE;
    }
    if( l_cert->enc_key == NULL ){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_CORRUPTED_CERTIFICATE_WITHOUT_KEYS,
                               "Corrupted certificate \"%s\" without keys certificate", l_ca_name);
        return COM_MEMPOOL_ADD_CA_ERROR_CORRUPTED_CERTIFICATE_WITHOUT_KEYS;
    }

    if ( l_cert->enc_key->priv_key_data_size || l_cert->enc_key->priv_key_data){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_CERTIFICATE_HAS_PRIVATE_KEY_DATA,
                               "Certificate \"%s\" has private key data. Please export public only key certificate without private keys", l_ca_name);
        return COM_MEMPOOL_ADD_CA_ERROR_CERTIFICATE_HAS_PRIVATE_KEY_DATA;
    }

    // Serialize certificate into memory
    uint32_t l_cert_serialized_size = 0;
    byte_t * l_cert_serialized = dap_cert_mem_save( l_cert, &l_cert_serialized_size );
    if( l_cert_serialized == NULL){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_SERIALIZE,
                               "Can't serialize in memory certificate \"%s\"", l_ca_name);
        return COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_SERIALIZE;
    }
    // Now all the chechs passed, forming datum for mempool
    dap_chain_datum_t * l_datum = dap_chain_datum_create( DAP_CHAIN_DATUM_CA, l_cert_serialized , l_cert_serialized_size);
    DAP_DELETE( l_cert_serialized);
    if( l_datum == NULL){
        dap_json_rpc_error_add(COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_SERIALIZE,
                               "Can't produce datum from certificate \"%s\"", l_ca_name);
        return COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_SERIALIZE;
    }

    // Finaly add datum to mempool
    char *l_hash_str = dap_chain_mempool_datum_add(l_datum, l_chain, "hex");
    DAP_DELETE(l_datum);
    if (l_hash_str) {
        char *l_msg = dap_strdup_printf("Datum %s was successfully placed to mempool", l_hash_str);
        if (!l_msg) {
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object *l_obj_message = json_object_new_string(l_msg);
        DAP_DELETE(l_msg);
        DAP_DELETE(l_hash_str);
        if (!l_obj_message) {
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object_array_add(*a_json_reply, l_obj_message);
        return 0;
    } else {
        char *l_msg = dap_strdup_printf("Can't place certificate \"%s\" to mempool", l_ca_name);
        if (!l_msg) {
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object *l_obj_msg = json_object_new_string(l_msg);
        DAP_DELETE(l_msg);
        if (!l_obj_msg) {
            dap_json_rpc_allocated_error;
            return DAP_JSON_RPC_ERR_CODE_MEMORY_ALLOCATED;
        }
        json_object_array_add(*a_json_reply, l_obj_msg);
        return COM_MEMPOOL_ADD_CA_ERROR_CAN_NOT_PLACE_CERTIFICATE;
    }
}

/**
 * @brief com_chain_ca_copy
 * @details copy public CA into the mempool
 * @param a_argc
 * @param a_argv
 * @param a_arg_func
 * @param a_str_reply
 * @return
 */
int com_chain_ca_copy_rpc( int a_argc,  char ** a_argv, json_object ** a_json_reply)
{
    return com_mempool_add_ca_rpc(a_argc, a_argv, a_json_reply);
}

static const char* s_json_get_text(struct json_object *a_json, const char *a_key)
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


static bool s_json_get_int64(struct json_object *a_json, const char *a_key, int64_t *a_out)
{
    if(!a_json || !a_key || !a_out)
        return false;
    struct json_object *l_json = json_object_object_get(a_json, a_key);
    if(l_json) {
        if(json_object_is_type(l_json, json_type_int)) {
            // Read number
            *a_out = json_object_get_int64(l_json);
            return true;
        }
    }
    return false;
}

static bool s_json_get_unit(struct json_object *a_json, const char *a_key, dap_chain_net_srv_price_unit_uid_t *a_out)
{
    const char *l_unit_str = s_json_get_text(a_json, a_key);
    if(!l_unit_str || !a_out)
        return false;
    dap_chain_net_srv_price_unit_uid_t l_unit = dap_chain_net_srv_price_unit_uid_from_str(l_unit_str);
    if(l_unit.enm == SERV_UNIT_UNDEFINED)
        return false;
    a_out->enm = l_unit.enm;
    return true;
}

static bool s_json_get_uint256(struct json_object *a_json, const char *a_key, uint256_t *a_out)
{
    const char *l_uint256_str = s_json_get_text(a_json, a_key);
    if(!a_out || !l_uint256_str)
        return false;
    uint256_t l_value = dap_chain_balance_scan(l_uint256_str);
    if(!IS_ZERO_256(l_value)) {
        memcpy(a_out, &l_value, sizeof(uint256_t));
        return true;
    }
    return false;
}

// service names: srv_stake, srv_vpn, srv_xchange
static bool s_json_get_srv_uid(struct json_object *a_json, const char *a_key_service_id, const char *a_key_service, uint64_t *a_out)
{
    uint64_t l_srv_id;
    if(!a_out)
        return false;
    // Read service id
    if(s_json_get_int64(a_json, a_key_service_id, (int64_t*) &l_srv_id)) {
        *a_out = l_srv_id;
        return true;
    }
    else {
        // Read service as name
        const char *l_service = s_json_get_text(a_json, a_key_service);
        if(l_service) {
            dap_chain_net_srv_t *l_srv = dap_chain_net_srv_get_by_name(l_service);
            if(!l_srv)
                return false;
            *a_out = l_srv->uid.uint64;
            return true;
        }
    }
    return false;
}

static dap_chain_wallet_t* s_json_get_wallet(struct json_object *a_json, const char *a_key)
{
    // From wallet
    const char *l_wallet_str = s_json_get_text(a_json, a_key);
    if(l_wallet_str) {
        dap_chain_wallet_t *l_wallet = dap_chain_wallet_open(l_wallet_str, dap_config_get_item_str_default(g_config, "resources", "wallets_path", NULL));
        return l_wallet;
    }
    return NULL;
}

static const dap_cert_t* s_json_get_cert(struct json_object *a_json, const char *a_key)
{
    const char *l_cert_name = s_json_get_text(a_json, a_key);
    if(l_cert_name) {
        dap_cert_t *l_cert = dap_cert_find_by_name(l_cert_name);
        return l_cert;
    }
    return NULL;
}

// Read pkey from wallet or cert
static dap_pkey_t* s_json_get_pkey(struct json_object *a_json)
{
    dap_pkey_t *l_pub_key = NULL;
    // From wallet
    dap_chain_wallet_t *l_wallet = s_json_get_wallet(a_json, "wallet");
    if(l_wallet) {
        l_pub_key = dap_chain_wallet_get_pkey(l_wallet, 0);
        dap_chain_wallet_close(l_wallet);
        if(l_pub_key) {
            return l_pub_key;
        }
    }
    // From cert
    const dap_cert_t *l_cert = s_json_get_cert(a_json, "cert");
    if(l_cert) {
        l_pub_key = dap_pkey_from_enc_key(l_cert->enc_key);
    }
    return l_pub_key;
}



/**
 * @brief Create transaction from json file
 * com_tx_create command
 * @param argc
 * @param argv
 * @param arg_func
 * @param str_reply
 * @return int
 */
int com_tx_create_json(int a_argc, char ** a_argv, char **a_str_reply)
{
    int l_arg_index = 1;
    int l_err_code = 0;
    const char *l_net_name = NULL; // optional parameter
    const char *l_chain_name = NULL; // optional parameter
    const char *l_json_file_path = NULL;
    const char *l_native_token = NULL;
    const char *l_main_token = NULL;

    dap_cli_server_cmd_find_option_val(a_argv, l_arg_index, a_argc, "-net", &l_net_name); // optional parameter
    dap_cli_server_cmd_find_option_val(a_argv, l_arg_index, a_argc, "-chain", &l_chain_name); // optional parameter
    dap_cli_server_cmd_find_option_val(a_argv, l_arg_index, a_argc, "-json", &l_json_file_path);

    if(!l_json_file_path) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Command requires one of parameters '-json <json file path>'");
        return -1;
    }
    // Open json file
    struct json_object *l_json = json_object_from_file(l_json_file_path);
    if(!l_json) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Can't open json file: %s", json_util_get_last_err());
        return -2;
    }
    if(!json_object_is_type(l_json, json_type_object)) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Wrong json format");
        json_object_put(l_json);
        return -3;
    }

    
    // Read network from json file
    if(!l_net_name) {
        struct json_object *l_json_net = json_object_object_get(l_json, "net");
        if(l_json_net && json_object_is_type(l_json_net, json_type_string)) {
            l_net_name = json_object_get_string(l_json_net);
        }
        if(!l_net_name) {
            dap_cli_server_cmd_set_reply_text(a_str_reply, "Command requires parameter '-net' or set net in the json file");
            json_object_put(l_json);
            return -11;
        }
    }
    dap_chain_net_t *l_net = dap_chain_net_by_name(l_net_name);
    l_native_token = l_net->pub.native_ticker;
    if(!l_net) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Not found net by name '%s'", l_net_name);
        json_object_put(l_json);
        return -12;
    }

    // Read chain from json file
    if(!l_chain_name) {
        struct json_object *l_json_chain = json_object_object_get(l_json, "chain");
        if(l_json_chain && json_object_is_type(l_json_chain, json_type_string)) {
            l_chain_name = json_object_get_string(l_json_chain);
        }
    }
    dap_chain_t *l_chain = dap_chain_net_get_chain_by_name(l_net, l_chain_name);
    if(!l_chain) {
        l_chain = dap_chain_net_get_chain_by_chain_type(l_net, CHAIN_TYPE_TX);
    }
    if(!l_chain) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Chain name '%s' not found, try use parameter '-chain' or set chain in the json file", l_chain_name);
        json_object_put(l_json);
        return -13;
    }


    // Read items from json file
    struct json_object *l_json_items = json_object_object_get(l_json, "items");
    size_t l_items_count = json_object_array_length(l_json_items);
    bool a = (l_items_count = json_object_array_length(l_json_items));
    if(!l_json_items || !json_object_is_type(l_json_items, json_type_array) || !(l_items_count = json_object_array_length(l_json_items))) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Wrong json format: not found array 'items' or array is empty");
        json_object_put(l_json);
        return -15;
    }

    log_it(L_ERROR, "Json TX: found %lu items", l_items_count);
    // Create transaction
    dap_chain_datum_tx_t *l_tx = DAP_NEW_Z_SIZE(dap_chain_datum_tx_t, sizeof(dap_chain_datum_tx_t));
    if(!l_tx) {
        log_it(L_CRITICAL, "Memory allocation error");
        return -16;
    }
    l_tx->header.ts_created = time(NULL);
    size_t l_items_ready = 0;
    size_t l_receipt_count = 0;
    dap_list_t *l_sign_list = NULL;// list 'sing' items
    dap_list_t *l_in_list = NULL;// list 'in' items
    dap_list_t *l_tsd_list = NULL;// list tsd sections
    uint256_t l_value_need = { };// how many tokens are needed in the 'out' item
    uint256_t l_value_need_fee = {};
    dap_string_t *l_err_str = dap_string_new("Errors: \n");
    // Creating and adding items to the transaction
    for(size_t i = 0; i < l_items_count; ++i) {
        struct json_object *l_json_item_obj = json_object_array_get_idx(l_json_items, i);
        if(!l_json_item_obj || !json_object_is_type(l_json_item_obj, json_type_object)) {
            continue;
        }
        struct json_object *l_json_item_type = json_object_object_get(l_json_item_obj, "type");
        if(!l_json_item_type && json_object_is_type(l_json_item_type, json_type_string)) {
            log_it(L_WARNING, "Item %zu without type", i);
            continue;
        }
        const char *l_item_type_str = json_object_get_string(l_json_item_type);
        dap_chain_tx_item_type_t l_item_type = dap_chain_datum_tx_item_str_to_type(l_item_type_str);
        if(l_item_type == TX_ITEM_TYPE_UNKNOWN) {
            log_it(L_WARNING, "Item %zu has invalid type '%s'", i, l_item_type_str);
            continue;
        }

        log_it(L_DEBUG, "Json TX: process item %s", json_object_get_string(l_json_item_type));
        // Create an item depending on its type
        const uint8_t *l_item = NULL;
        switch (l_item_type) {
        case TX_ITEM_TYPE_IN: {
            // Save item obj for in
            l_in_list = dap_list_append(l_in_list, l_json_item_obj);
        }
            break;

        case TX_ITEM_TYPE_OUT:
        case TX_ITEM_TYPE_OUT_EXT: {
            // Read address and value
            uint256_t l_value = { };
            const char *l_json_item_addr_str = s_json_get_text(l_json_item_obj, "addr");
            bool l_is_value = s_json_get_uint256(l_json_item_obj, "value", &l_value);
            if(l_is_value && l_json_item_addr_str) {
                dap_chain_addr_t *l_addr = dap_chain_addr_from_str(l_json_item_addr_str);
                if(l_addr && !IS_ZERO_256(l_value)) {
                    if(l_item_type == TX_ITEM_TYPE_OUT) {
                        // Create OUT item
                        dap_chain_tx_out_t *l_out_item = dap_chain_datum_tx_item_out_create(l_addr, l_value);
                        if (!l_out_item) {
                            dap_string_append_printf(l_err_str, "Failed to create transaction out. "
                                                                "There may not be enough funds in the wallet.\n");
                        }
                        l_item = (const uint8_t*) l_out_item;
                    }
                    else if(l_item_type == TX_ITEM_TYPE_OUT_EXT) {
                        // Read address and value
                        const char *l_token = s_json_get_text(l_json_item_obj, "token");
                        l_main_token = l_token;
                        if(l_token) {
                            // Create OUT_EXT item
                            dap_chain_tx_out_ext_t *l_out_ext_item = dap_chain_datum_tx_item_out_ext_create(l_addr, l_value, l_token);
                            if (!l_out_ext_item) {
                                dap_string_append_printf(l_err_str, "Failed to create a out ext"
                                                                    "for a transaction. There may not be enough funds "
                                                                    "on the wallet or the wrong ticker token "
                                                                    "is indicated.\n");
                            }
                            l_item = (const uint8_t*) l_out_ext_item;
                        }
                        else {
                            log_it(L_WARNING, "Invalid 'out_ext' item %zu", i);
                            continue;
                        }
                    }
                    // Save value for using in In item
                    if(l_item) {
                        SUM_256_256(l_value_need, l_value, &l_value_need);
                    }
                } else {
                    if(l_item_type == TX_ITEM_TYPE_OUT) {
                        log_it(L_WARNING, "Invalid 'out' item %zu", i);
                    }
                    else if(l_item_type == TX_ITEM_TYPE_OUT_EXT) {
                        log_it(L_WARNING, "Invalid 'out_ext' item %zu", i);
                    }
                    dap_string_append_printf(l_err_str, "For item %zu of type 'out' or 'out_ext' the "
                                                        "string representation of the address could not be converted, "
                                                        "or the size of the output sum is 0.\n", i);
                    continue;
                }
            }
        }
            break;
        case TX_ITEM_TYPE_OUT_COND: {
            // Read subtype of item
            const char *l_subtype_str = s_json_get_text(l_json_item_obj, "subtype");
            dap_chain_tx_out_cond_subtype_t l_subtype = dap_chain_tx_out_cond_subtype_from_str(l_subtype_str);
            switch (l_subtype) {

            case DAP_CHAIN_TX_OUT_COND_SUBTYPE_SRV_PAY:{
                uint256_t l_value = { };
                bool l_is_value = s_json_get_uint256(l_json_item_obj, "value", &l_value);
                if(!l_is_value || IS_ZERO_256(l_value)) {
                    log_it(L_ERROR, "Json TX: bad value in OUT_COND_SUBTYPE_SRV_PAY");
                    break;
                }
                uint256_t l_value_max_per_unit = { };
                l_is_value = s_json_get_uint256(l_json_item_obj, "value_max_per_unit", &l_value_max_per_unit);
                if(!l_is_value || IS_ZERO_256(l_value_max_per_unit)) {
                    log_it(L_ERROR, "Json TX: bad value_max_per_unit in OUT_COND_SUBTYPE_SRV_PAY");
                    break;
                }
                dap_chain_net_srv_price_unit_uid_t l_price_unit;
                if(!s_json_get_unit(l_json_item_obj, "price_unit", &l_price_unit)) {
                    log_it(L_ERROR, "Json TX: bad price_unit in OUT_COND_SUBTYPE_SRV_PAY");
                    break;
                }
                dap_chain_net_srv_uid_t l_srv_uid;
                if(!s_json_get_srv_uid(l_json_item_obj, "service_id", "service", &l_srv_uid.uint64)){
                    // Default service DAP_CHAIN_NET_SRV_VPN_ID
                    l_srv_uid.uint64 = 0x0000000000000001;
                }

                // From "wallet" or "cert"
                dap_pkey_t *l_pkey = s_json_get_pkey(l_json_item_obj);
                if(!l_pkey) {
                    log_it(L_ERROR, "Json TX: bad pkey in OUT_COND_SUBTYPE_SRV_PAY");
                    break;
                }
                const char *l_params_str = s_json_get_text(l_json_item_obj, "params");
                size_t l_params_size = dap_strlen(l_params_str);
                dap_chain_tx_out_cond_t *l_out_cond_item = dap_chain_datum_tx_item_out_cond_create_srv_pay(l_pkey, l_srv_uid, l_value, l_value_max_per_unit,
                        l_price_unit, l_params_str, l_params_size);
                l_item = (const uint8_t*) l_out_cond_item;
                // Save value for using in In item
                if(l_item) {
                    SUM_256_256(l_value_need, l_value, &l_value_need);
                } else {
                    dap_string_append_printf(l_err_str, "Unable to create conditional out for transaction "
                                                        "can of type %s described in item %zu.\n", l_subtype_str, i);
                }
                DAP_DELETE(l_pkey);
            }
                break;
            case DAP_CHAIN_TX_OUT_COND_SUBTYPE_SRV_XCHANGE: {

                dap_chain_net_srv_uid_t l_srv_uid;
                if(!s_json_get_srv_uid(l_json_item_obj, "service_id", "service", &l_srv_uid.uint64)) {
                    // Default service DAP_CHAIN_NET_SRV_XCHANGE_ID
                    l_srv_uid.uint64 = 0x2;
                }
                dap_chain_net_t *l_net = dap_chain_net_by_name(s_json_get_text(l_json_item_obj, "net"));
                if(!l_net) {
                    log_it(L_ERROR, "Json TX: bad net in OUT_COND_SUBTYPE_SRV_XCHANGE");
                    break;
                }
                const char *l_token = s_json_get_text(l_json_item_obj, "token");
                if(!l_token) {
                    log_it(L_ERROR, "Json TX: bad token in OUT_COND_SUBTYPE_SRV_XCHANGE");
                    break;
                }
                uint256_t l_value = { };
                if(!s_json_get_uint256(l_json_item_obj, "value", &l_value) || IS_ZERO_256(l_value)) {
                    log_it(L_ERROR, "Json TX: bad value in OUT_COND_SUBTYPE_SRV_XCHANGE");
                    break;
                }
                //const char *l_params_str = s_json_get_text(l_json_item_obj, "params");
                //size_t l_params_size = dap_strlen(l_params_str);
                dap_chain_tx_out_cond_t *l_out_cond_item = NULL; //dap_chain_datum_tx_item_out_cond_create_srv_xchange(l_srv_uid, l_net->pub.id, l_token, l_value, l_params_str, l_params_size);
                l_item = (const uint8_t*) l_out_cond_item;
                // Save value for using in In item
                if(l_item) {
                    SUM_256_256(l_value_need, l_value, &l_value_need);
                } else {
                    dap_string_append_printf(l_err_str, "Unable to create conditional out for transaction "
                                                        "can of type %s described in item %zu.\n", l_subtype_str, i);
                }
            }
                break;
            case DAP_CHAIN_TX_OUT_COND_SUBTYPE_SRV_STAKE_POS_DELEGATE:{
                dap_chain_net_srv_uid_t l_srv_uid;
                if(!s_json_get_srv_uid(l_json_item_obj, "service_id", "service", &l_srv_uid.uint64)) {
                    // Default service DAP_CHAIN_NET_SRV_STAKE_ID
                    l_srv_uid.uint64 = 0x13;
                }
                uint256_t l_value = { };
                if(!s_json_get_uint256(l_json_item_obj, "value", &l_value) || IS_ZERO_256(l_value)) {
                    log_it(L_ERROR, "Json TX: bad value in OUT_COND_SUBTYPE_SRV_STAKE_POS_DELEGATE");
                    break;
                }
                uint256_t l_fee_value = { };
                if(!s_json_get_uint256(l_json_item_obj, "fee", &l_fee_value) || IS_ZERO_256(l_fee_value)) {
                    break;
                }
                
                const char *l_signing_addr_str = s_json_get_text(l_json_item_obj, "signing_addr");
                dap_chain_addr_t *l_signing_addr = dap_chain_addr_from_str(l_signing_addr_str);
                if(!l_signing_addr) {
                {
                    log_it(L_ERROR, "Json TX: bad signing_addr in OUT_COND_SUBTYPE_SRV_STAKE_POS_DELEGATE");
                    break;
                }
                dap_chain_node_addr_t l_signer_node_addr;
                const char *l_node_addr_str = s_json_get_text(l_json_item_obj, "node_addr");
                if(!l_node_addr_str || dap_chain_node_addr_from_str(&l_signer_node_addr, l_node_addr_str)) {
                    log_it(L_ERROR, "Json TX: bad node_addr in OUT_COND_SUBTYPE_SRV_STAKE_POS_DELEGATE");
                    break;
                }
                dap_chain_tx_out_cond_t *l_out_cond_item = dap_chain_datum_tx_item_out_cond_create_srv_stake(l_srv_uid, l_value, l_signing_addr, &l_signer_node_addr);
                l_item = (const uint8_t*) l_out_cond_item;
                // Save value for using in In item
                if(l_item) {
                    SUM_256_256(l_value_need, l_value, &l_value_need);
                } else {
                    dap_string_append_printf(l_err_str, "Unable to create conditional out for transaction "
                                                        "can of type %s described in item %zu.\n", l_subtype_str, i);
                }
                }
            }
                break;
            case DAP_CHAIN_TX_OUT_COND_SUBTYPE_FEE: {
                uint256_t l_value = { };
                bool l_is_value = s_json_get_uint256(l_json_item_obj, "value", &l_value);
                if(!IS_ZERO_256(l_value)) {
                    dap_chain_tx_out_cond_t *l_out_cond_item = dap_chain_datum_tx_item_out_cond_create_fee(l_value);
                    l_item = (const uint8_t*) l_out_cond_item;
                    // Save value for using in In item
                    if(l_item) {
                        SUM_256_256(l_value_need_fee, l_value, &l_value_need_fee);
                    } else {
                        dap_string_append_printf(l_err_str, "Unable to create conditional out for transaction "
                                                            "can of type %s described in item %zu.\n", l_subtype_str, i);
                    }
                }
                else
                    log_it(L_ERROR, "Json TX: zero value in OUT_COND_SUBTYPE_FEE");
            }
                break;
            case DAP_CHAIN_TX_OUT_COND_SUBTYPE_UNDEFINED:
                log_it(L_WARNING, "Undefined subtype: '%s' of 'out_cond' item %zu ", l_subtype_str, i);
                    dap_string_append_printf(l_err_str, "Specified unknown sub type %s of conditional out "
                                                        "on item %zu.\n", l_subtype_str, i);
                break;
            }
        }

            break;
        case TX_ITEM_TYPE_SIG:{
            // Save item obj for sign
            l_sign_list = dap_list_append(l_sign_list,l_json_item_obj);
        }
            break;
        case TX_ITEM_TYPE_RECEIPT: {
            dap_chain_net_srv_uid_t l_srv_uid;
            if(!s_json_get_srv_uid(l_json_item_obj, "service_id", "service", &l_srv_uid.uint64)) {
                log_it(L_ERROR, "Json TX: bad service_id in TYPE_RECEIPT");
                break;
            }
            dap_chain_net_srv_price_unit_uid_t l_price_unit;
            if(!s_json_get_unit(l_json_item_obj, "price_unit", &l_price_unit)) {
                log_it(L_ERROR, "Json TX: bad price_unit in TYPE_RECEIPT");
                break;
            }
            int64_t l_units;
            if(!s_json_get_int64(l_json_item_obj, "units", &l_units)) {
                log_it(L_ERROR, "Json TX: bad units in TYPE_RECEIPT");
                break;
            }
            uint256_t l_value = { };
            if(!s_json_get_uint256(l_json_item_obj, "value", &l_value) || IS_ZERO_256(l_value)) {
                log_it(L_ERROR, "Json TX: bad value in TYPE_RECEIPT");
                break;
            }
            const char *l_params_str = s_json_get_text(l_json_item_obj, "params");
            size_t l_params_size = dap_strlen(l_params_str);
            dap_chain_datum_tx_receipt_t *l_receipt = dap_chain_datum_tx_receipt_create(l_srv_uid, l_price_unit, l_units, l_value, l_params_str, l_params_size);
            l_item = (const uint8_t*) l_receipt;
            if(l_item)
                l_receipt_count++;
            else {
                dap_string_append_printf(l_err_str, "Unable to create receipt out for transaction "
                                                    "described by item %zu.\n", i);
            }
        }
            break;
        case TX_ITEM_TYPE_TSD: {
            int64_t l_tsd_type;
            if(!s_json_get_int64(l_json_item_obj, "type_tsd", &l_tsd_type)) {
                log_it(L_ERROR, "Json TX: bad type_tsd in TYPE_TSD");
                break;
            }
            const char *l_tsd_data = s_json_get_text(l_json_item_obj, "data");
            if (!l_tsd_data) {
                log_it(L_ERROR, "Json TX: bad data in TYPE_TSD");
                break;
            }
            size_t l_data_size = dap_strlen(l_tsd_data);
            dap_chain_tx_tsd_t *l_tsd = dap_chain_datum_tx_item_tsd_create((void*)l_tsd_data, (int)l_tsd_type, l_data_size);
            l_tsd_list = dap_list_append(l_tsd_list, l_tsd);
        }
            break;
            //case TX_ITEM_TYPE_PKEY:
                //break;
            //case TX_ITEM_TYPE_IN_EMS:
                //break;
            //case TX_ITEM_TYPE_IN_EMS_EXT:
                //break;
        }
        // Add item to transaction
        if(l_item) {
            dap_chain_datum_tx_add_item(&l_tx, (const uint8_t*) l_item);
            l_items_ready++;
            DAP_DELETE(l_item);
        }
    }
    
    dap_list_t *l_list;
    // Add In items
    l_list = l_in_list;
    while(l_list) {
        struct json_object *l_json_item_obj = (struct json_object*) l_list->data;
        // Read prev_hash and out_prev_idx
        const char *l_prev_hash_str = s_json_get_text(l_json_item_obj, "prev_hash");
        int64_t l_out_prev_idx;
        bool l_is_out_prev_idx = s_json_get_int64(l_json_item_obj, "out_prev_idx", &l_out_prev_idx);
        // If prev_hash and out_prev_idx were read
        if(l_prev_hash_str && l_is_out_prev_idx) {
            dap_chain_hash_fast_t l_tx_prev_hash;
            if(!dap_chain_hash_fast_from_str(l_prev_hash_str, &l_tx_prev_hash)) {
                // Create IN item
                dap_chain_tx_in_t *l_in_item = dap_chain_datum_tx_item_in_create(&l_tx_prev_hash, (uint32_t) l_out_prev_idx);
                if (!l_in_item) {
                    dap_string_append_printf(l_err_str, "Unable to create in for transaction.\n");
                }
            } else {
                log_it(L_WARNING, "Invalid 'in' item, bad prev_hash %s", l_prev_hash_str);
                dap_string_append_printf(l_err_str, "Unable to create in for transaction. Invalid 'in' item, "
                                                    "bad prev_hash %s\n", l_prev_hash_str);
                // Go to the next item
                l_list = dap_list_next(l_list);
                continue;
            }
        }
        // Read addr_from
        else {
            const char *l_json_item_addr_str = s_json_get_text(l_json_item_obj, "addr_from");
            const char *l_json_item_token = s_json_get_text(l_json_item_obj, "token");
            l_main_token = l_json_item_token;
            dap_chain_addr_t *l_addr_from = NULL;
            if(l_json_item_addr_str) {
                l_addr_from = dap_chain_addr_from_str(l_json_item_addr_str);
                if (!l_addr_from) {
                    log_it(L_WARNING, "Invalid element 'in', unable to convert string representation of addr_from: '%s' "
                                      "to binary.", l_json_item_addr_str);
                    dap_string_append_printf(l_err_str, "Invalid element 'to', unable to convert string representation "
                                                        "of addr_from: '%s' to binary.\n", l_json_item_addr_str);
                    // Go to the next item
                    l_list = dap_list_next(l_list);
                    continue;
                }
            }
            else {
                log_it(L_WARNING, "Invalid 'in' item, incorrect addr_from: '%s'", l_json_item_addr_str ? l_json_item_addr_str : "[null]");
                dap_string_append_printf(l_err_str, "Invalid 'in' item, incorrect addr_from: '%s'\n",
                                         l_json_item_addr_str ? l_json_item_addr_str : "[null]");
                // Go to the next item
                l_list = dap_list_next(l_list);
                continue;
            }
            if(!l_json_item_token) {
                log_it(L_WARNING, "Invalid 'in' item, not found token name");
                dap_string_append_printf(l_err_str, "Invalid 'in' item, not found token name\n");
                // Go to the next item
                l_list = dap_list_next(l_list);
                continue;
            }
            if(IS_ZERO_256(l_value_need)) {
                log_it(L_WARNING, "Invalid 'in' item, not found value in out items");
                dap_string_append_printf(l_err_str, "Invalid 'in' item, not found value in out items\n");
                // Go to the next item
                l_list = dap_list_next(l_list);
                continue;
            }
            if(l_addr_from)
            {
                // find the transactions from which to take away coins
                dap_list_t *l_list_used_out = NULL;
                dap_list_t *l_list_used_out_fee = NULL;
                uint256_t l_value_transfer = { }; // how many coins to transfer
                uint256_t l_value_transfer_fee = { }; // how many coins to transfer
                //SUM_256_256(a_value, a_value_fee, &l_value_need);
                uint256_t l_value_need_check = {};
                if (!dap_strcmp(l_native_token, l_main_token)) {
                    SUM_256_256(l_value_need_check, l_value_need, &l_value_need_check);
                    SUM_256_256(l_value_need_check, l_value_need_fee, &l_value_need_check);
                    l_list_used_out = dap_chain_ledger_get_list_tx_outs_with_val(l_net->pub.ledger, l_json_item_token,
                                                                                             l_addr_from, l_value_need_check, &l_value_transfer);
                    if(!l_list_used_out) {
                        log_it(L_WARNING, "Not enough funds in previous tx to transfer");
                        dap_string_append_printf(l_err_str, "Can't create in transaction. Not enough funds in previous tx "
                                                            "to transfer\n");
                        // Go to the next item
                        l_list = dap_list_next(l_list);
                        continue;
                    }
                } else {
                    //CHECK value need
                    l_list_used_out = dap_chain_ledger_get_list_tx_outs_with_val(l_net->pub.ledger, l_json_item_token,
                                                                                             l_addr_from, l_value_need, &l_value_transfer);
                    if(!l_list_used_out) {
                        log_it(L_WARNING, "Not enough funds in previous tx to transfer");
                        dap_string_append_printf(l_err_str, "Can't create in transaction. Not enough funds in previous tx "
                                                            "to transfer\n");
                        // Go to the next item
                        l_list = dap_list_next(l_list);
                        continue;
                    }
                    //CHECK value fee
                    l_list_used_out_fee = dap_chain_ledger_get_list_tx_outs_with_val(l_net->pub.ledger, l_native_token,
                                                                                     l_addr_from, l_value_need_fee, &l_value_transfer_fee);
                    if(!l_list_used_out_fee) {
                        log_it(L_WARNING, "Not enough funds in previous tx to transfer");
                        dap_string_append_printf(l_err_str, "Can't create in transaction. Not enough funds in previous tx "
                                                            "to transfer\n");
                        // Go to the next item
                        l_list = dap_list_next(l_list);
                        continue;
                    }
                }
                // add 'in' items
                uint256_t l_value_got = dap_chain_datum_tx_add_in_item_list(&l_tx, l_list_used_out);
                assert(EQUAL_256(l_value_got, l_value_transfer));
                if (l_list_used_out_fee) {
                    uint256_t l_value_got_fee = dap_chain_datum_tx_add_in_item_list(&l_tx, l_list_used_out_fee);
                    assert(EQUAL_256(l_value_got_fee, l_value_transfer_fee));
                    dap_list_free_full(l_list_used_out_fee, free);
                    // add 'out' item for coin fee back
                    uint256_t  l_value_back;
                    SUBTRACT_256_256(l_value_got_fee, l_value_need_fee, &l_value_back);
                    if (!IS_ZERO_256(l_value_back)) {
                        dap_chain_datum_tx_add_out_ext_item(&l_tx, l_addr_from, l_value_back, l_native_token);
                    }
                } else {
                    SUM_256_256(l_value_need, l_value_need_fee, &l_value_need);
                }
                dap_list_free_full(l_list_used_out, free);
                if(!IS_ZERO_256(l_value_got)) {
                    l_items_ready++;

                    // add 'out' item for coin back
                    uint256_t l_value_back;
                    SUBTRACT_256_256(l_value_got, l_value_need, &l_value_back);
                    if(!IS_ZERO_256(l_value_back)) {
                        dap_chain_datum_tx_add_out_item(&l_tx, l_addr_from, l_value_back);
                    }
                }
            }
        }
        // Go to the next 'in' item
        l_list = dap_list_next(l_list);
    }
    dap_list_free(l_in_list);


    
    // Add TSD section
    l_list = l_tsd_list;
    while(l_list) {
        dap_chain_datum_tx_add_item(&l_tx, l_list->data);
        l_items_ready++;
        l_list = dap_list_next(l_list);
    }
    dap_list_free(l_tsd_list);


    // Add signs
    l_list = l_sign_list;
    while(l_list) {

        struct json_object *l_json_item_obj = (struct json_object*) l_list->data;

        dap_enc_key_t * l_enc_key  = NULL;
        
        //get wallet or cert
        dap_chain_wallet_t *l_wallet = s_json_get_wallet(l_json_item_obj, "wallet");
        const dap_cert_t *l_cert = s_json_get_cert(l_json_item_obj, "cert");

        //wallet goes first
        if (l_wallet) {
            l_enc_key = dap_chain_wallet_get_key(l_wallet, 0);

        } else if (l_cert && l_cert->enc_key) {
            l_enc_key = l_cert->enc_key;
        }
        else{
		dap_string_append_printf(l_err_str, "Can't create sign for transactions.\n");
            log_it(L_ERROR, "Json TX: Item sign has no wallet or cert of they are invalid ");
            l_list = dap_list_next(l_list);
            continue;
        }

        if(l_enc_key && dap_chain_datum_tx_add_sign_item(&l_tx, l_enc_key) > 0) {
            l_items_ready++;
        } else {
            log_it(L_ERROR, "Json TX: Item sign has invalid enc_key.");
            l_list = dap_list_next(l_list);
            continue;
        }

        if (l_wallet)
            dap_chain_wallet_close(l_wallet);    

    
        l_list = dap_list_next(l_list);
    }

    dap_list_free(l_sign_list);
    json_object_put(l_json);

    if(l_items_ready<l_items_count) {
        if(!l_items_ready)
            dap_cli_server_cmd_set_reply_text(a_str_reply, "No valid items found to create a transaction");
        else
            dap_cli_server_cmd_set_reply_text(a_str_reply, "Can't create transaction, because only %zu items out of %zu are valid",l_items_ready,l_items_count);
        DAP_DELETE(l_tx);
        return -30;
    }

    // Pack transaction into the datum
    dap_chain_datum_t *l_datum_tx = dap_chain_datum_create(DAP_CHAIN_DATUM_TX, l_tx, dap_chain_datum_tx_get_size(l_tx));
    size_t l_datum_tx_size = dap_chain_datum_size(l_datum_tx);
    DAP_DELETE(l_tx);

    // Add transaction to mempool
    char *l_gdb_group_mempool_base_tx = dap_chain_net_get_gdb_group_mempool_new(l_chain);// get group name for mempool
    char *l_tx_hash_str;
    dap_get_data_hash_str_static(l_datum_tx->data, l_datum_tx->header.data_size, l_tx_hash_str);
    bool l_placed = !dap_global_db_set(l_gdb_group_mempool_base_tx,l_tx_hash_str, l_datum_tx, l_datum_tx_size, false, NULL, NULL);

    DAP_DEL_Z(l_datum_tx);
    DAP_DELETE(l_gdb_group_mempool_base_tx);
    if(!l_placed) {
        dap_cli_server_cmd_set_reply_text(a_str_reply, "Can't add transaction to mempool");
        return -90;
    }
    // Completed successfully
    dap_cli_server_cmd_set_reply_text(a_str_reply, "Transaction %s with %zu items created and added to mempool successfully", l_tx_hash_str, l_items_ready);
    return l_err_code;
}


/**
 * @brief com_tx_history
 * tx_history command
 * Transaction history for an address
 * @param a_argc
 * @param a_argv
 * @param a_str_reply
 * @return int
 */
int com_tx_history_rpc(int a_argc, char ** a_argv, json_object** json_arr_reply)
{
    int arg_index = 1;
    const char *l_addr_base58 = NULL;
    const char *l_wallet_name = NULL;
    const char *l_net_str = NULL;
    const char *l_chain_str = NULL;
    const char *l_tx_hash_str = NULL;

    dap_chain_t * l_chain = NULL;
    dap_chain_net_t * l_net = NULL;

    const char * l_hash_out_type = NULL;
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-H", &l_hash_out_type);
    if(!l_hash_out_type)
        l_hash_out_type = "hex";
    if(dap_strcmp(l_hash_out_type,"hex") && dap_strcmp(l_hash_out_type,"base58")) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR,
                                "Invalid parameter -H, valid values: -H <hex | base58>");
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR;

    }

    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-addr", &l_addr_base58);
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-w", &l_wallet_name);
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-net", &l_net_str);
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-chain", &l_chain_str);
    dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-tx", &l_tx_hash_str);

    bool l_is_tx_all = dap_cli_server_cmd_find_option_val(a_argv, arg_index, a_argc, "-all", NULL);

    if (!l_addr_base58 && !l_wallet_name && !l_tx_hash_str && !l_is_tx_all) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR,
                                "tx_history requires parameter '-addr' or '-w' or '-tx'");
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR;
    }

    if (!l_net_str && !l_addr_base58&& !l_is_tx_all) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR,
                                "tx_history requires parameter '-net' or '-addr'");
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_PARAM_ERR;
    }

    dap_chain_hash_fast_t l_tx_hash;
    if (l_tx_hash_str && dap_chain_hash_fast_from_str(l_tx_hash_str, &l_tx_hash) < 0) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_HASH_REC_ERR, "tx hash not recognized");
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_HASH_REC_ERR;
    }
    // Select chain network
    if (l_net_str) {
        l_net = dap_chain_net_by_name(l_net_str);
        if (!l_net) { // Can't find such network
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_NET_PARAM_ERR,
                                    "tx_history requires parameter '-net' to be valid chain network name");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_NET_PARAM_ERR;
        }
    }
    // Get chain address
    dap_chain_addr_t *l_addr = NULL;
    if (l_addr_base58) {
        if (l_tx_hash_str) {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_INCOMPATIBLE_PARAMS_ERR,
                                                        "Incompatible params '-addr' & '-tx'");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_INCOMPATIBLE_PARAMS_ERR;
        }
        l_addr = dap_chain_addr_from_str(l_addr_base58);
        if (!l_addr) {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_WALLET_ADDR_ERR,
                                                        "Wallet address not recognized");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_WALLET_ADDR_ERR;
        }
        if (l_net) {
            if (l_net->pub.id.uint64 != l_addr->net_id.uint64) {
                dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_ID_NET_ADDR_DIF_ERR,
                                        "Network ID with '-net' param and network ID with '-addr' param are different");
                DAP_DELETE(l_addr);
                return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_ID_NET_ADDR_DIF_ERR;
            }
        } else
            l_net = dap_chain_net_by_id(l_addr->net_id);
    }
    const char* l_sign_str = "";
    if (l_wallet_name) {
        const char *c_wallets_path = dap_chain_wallet_get_path(g_config);
        dap_chain_wallet_t *l_wallet = dap_chain_wallet_open(l_wallet_name, c_wallets_path);
        if (l_wallet) {
            l_sign_str = dap_chain_wallet_check_bliss_sign(l_wallet);
            dap_chain_addr_t *l_addr_tmp = dap_chain_wallet_get_addr(l_wallet, l_net->pub.id);
            if (l_addr) {
                if (!dap_chain_addr_compare(l_addr, l_addr_tmp)) {
                    dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_ADDR_WALLET_DIF_ERR,
                                            "Address with '-addr' param and address with '-w' param are different");
                    DAP_DELETE(l_addr);
                    DAP_DELETE(l_addr_tmp);
                    return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_ADDR_WALLET_DIF_ERR;
                }
                DAP_DELETE(l_addr_tmp);
            } else
                l_addr = l_addr_tmp;
            dap_chain_wallet_close(l_wallet);
        } else {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_WALLET_ERR,
                                    "The wallet %s is not activated or it doesn't exist", l_wallet_name);
            DAP_DELETE(l_addr);
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_WALLET_ERR;
        }
    }
    // Select chain, if any
    if (!l_net) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_NET_ERR, "Could not determine the network from which to "
                                                       "extract data for the tx_history command to work.");
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_NET_ERR;
    }
    if (l_chain_str)
        l_chain = dap_chain_net_get_chain_by_name(l_net, l_chain_str);
    else
        l_chain = dap_chain_net_get_default_chain_by_chain_type(l_net, CHAIN_TYPE_TX);

    if(!l_chain) {
        dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_CHAIN_PARAM_ERR,
                                "tx_history requires parameter '-chain' to be valid chain name in chain net %s."
                                " You can set default datum type in chain configuration file", l_net_str);
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_CHAIN_PARAM_ERR;
    }
    // response
    json_object * json_obj_out = NULL;
    if (l_tx_hash_str) {
         // history tx hash
        json_obj_out = dap_db_history_tx_rpc(&l_tx_hash, l_chain, l_hash_out_type, l_net);
        if (!json_obj_out) {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_TX_ERR,
                                    "something went wrong in tx_history");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_TX_ERR;
        }
    } else if (l_addr) {
        // history addr and wallet
        char *l_addr_str = dap_chain_addr_to_str(l_addr);
        json_obj_out = dap_db_history_addr_rpc(l_addr, l_chain, l_hash_out_type, l_addr_str);
        if (!json_obj_out) {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_ADDR_ERR,
                                    "something went wrong in tx_history");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_ADDR_ERR;
        }
    } else if (l_is_tx_all) {
        // history all
        json_object * json_obj_summary = json_object_new_object();
        if (!json_obj_summary) {
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_MEMORY_ERR;
        }

        json_object* json_arr_history_all = dap_db_history_tx_all_rpc(l_chain, l_net, l_hash_out_type, json_obj_summary);
        if (!json_arr_history_all) {
            dap_json_rpc_error_add(DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_ALL_ERR,
                                    "something went wrong in tx_history");
            return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_DAP_DB_HISTORY_ALL_ERR;
        }

        json_object_array_add(*json_arr_reply, json_arr_history_all);
        json_object_array_add(*json_arr_reply, json_obj_summary);
        return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_OK;
    }

    if (json_obj_out) {
        *json_arr_reply = json_object_get(json_obj_out);
    } else {
        json_object_array_add(*json_arr_reply, json_object_new_string("empty"));
    }

    return DAP_CHAIN_NODE_CLI_COM_TX_HISTORY_OK;
}

