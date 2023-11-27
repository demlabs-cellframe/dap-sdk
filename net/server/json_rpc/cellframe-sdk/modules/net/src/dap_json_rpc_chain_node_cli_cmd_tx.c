#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>


#include "dap_chain_wallet.h"
#include "dap_common.h"
#include "dap_enc_base58.h"
#include "dap_strfuncs.h"
#include "dap_string.h"
#include "dap_list.h"
#include "dap_hash.h"
#include "dap_time.h"

#include "dap_chain_cell.h"
#include "dap_chain_net_tx.h"
#include "dap_chain_mempool.h"

#include "dap_json_rpc_chain_datum.h"
#include "dap_json_rpc_chain_datum_tx.h"
#include "dap_json_rpc_chain_datum_token.h"
#include "dap_json_rpc_chain_datum_decree.h"
#include "dap_json_rpc_chain_datum_tx_items.h"
#include "dap_json_rpc_chain_node_cli_cmd_tx.h"
#include "dap_json_rpc_errors.h"
#include "json.h"

#define LOG_TAG "dap_json_rpc_chain_node_cli_cmd_tx"

json_object * dap_db_tx_history_to_json_rpc(dap_chain_hash_fast_t* a_tx_hash,
                                        dap_hash_fast_t * l_atom_hash,
                                        dap_chain_datum_tx_t * l_tx,
                                        dap_chain_t * a_chain, 
                                        const char *a_hash_out_type, 
                                        dap_chain_net_t * l_net,
                                        int l_ret_code,
                                        bool *accepted_tx)
{
    const char *l_tx_token_ticker = NULL;
    json_object* json_obj_datum = json_object_new_object();
    if (!json_obj_datum) {
        return NULL;
    }

    dap_ledger_t *l_ledger = dap_chain_net_by_id(a_chain->net_id)->pub.ledger;
    l_tx_token_ticker = dap_chain_ledger_tx_get_token_ticker_by_hash(l_ledger, a_tx_hash);
    if (l_tx_token_ticker) {
        json_object_object_add(json_obj_datum, "status", json_object_new_string("ACCEPTED"));
        *accepted_tx = true;
    } else {
        json_object_object_add(json_obj_datum, "status", json_object_new_string("DECLINED"));
        *accepted_tx = false;
    }

    if (l_atom_hash) {
        char *l_atom_hash_str = dap_strcmp(a_hash_out_type, "hex")
                            ? dap_enc_base58_encode_hash_to_str(l_atom_hash)
                            : dap_chain_hash_fast_to_str_new(l_atom_hash);
        json_object_object_add(json_obj_datum, "atom_hash", json_object_new_string(l_atom_hash_str));
    }

    char *l_hash_str = dap_strcmp(a_hash_out_type, "hex")
                        ? dap_enc_base58_encode_hash_to_str(a_tx_hash)
                        : dap_chain_hash_fast_to_str_new(a_tx_hash);
    json_object_object_add(json_obj_datum, "hash", json_object_new_string(l_hash_str));

    json_object_object_add(json_obj_datum, "token_ticker", l_tx_token_ticker ? json_object_new_string(l_tx_token_ticker) 
                                                                             : json_object_new_null());

    json_object_object_add(json_obj_datum, "ret_code", json_object_new_int(l_ret_code));
    json_object_object_add(json_obj_datum, "ret_code_str", json_object_new_string(dap_chain_ledger_tx_check_err_str(l_ret_code)));

    char l_time_str[32];
    if (l_tx->header.ts_created) {
        uint64_t l_ts = l_tx->header.ts_created;
        dap_ctime_r(&l_ts, l_time_str);                             /* Convert ts to  "Sat May 17 01:17:08 2014\n" */
        l_time_str[strlen(l_time_str)-1] = '\0';                    /* Remove "\n"*/
    }
    json_object *l_obj_ts_created = json_object_new_string(l_time_str);
    json_object_object_add(json_obj_datum, "tx_created", l_obj_ts_created);

    json_object* datum_tx = dap_chain_datum_dump_tx_to_json(l_tx, a_hash_out_type);
    json_object_object_add(json_obj_datum, "items", datum_tx);

    return json_obj_datum;
}

json_object * dap_db_history_tx_rpc(dap_chain_hash_fast_t* a_tx_hash, 
                      dap_chain_t * a_chain, 
                      const char *a_hash_out_type,
                      dap_chain_net_t * l_net)

{
    if (!a_chain->callback_datum_find_by_hash) {
        log_it(L_WARNING, "Not defined callback_datum_find_by_hash for chain \"%s\"", a_chain->name);
        return NULL;
    }

    int l_ret_code = 0;
    bool accepted_tx;
    dap_hash_fast_t l_atom_hash = {};
    //search tx
    dap_chain_datum_t *l_datum = a_chain->callback_datum_find_by_hash(a_chain, a_tx_hash, &l_atom_hash, &l_ret_code);
    dap_chain_datum_tx_t *l_tx = l_datum  && l_datum->header.type_id == DAP_CHAIN_DATUM_TX ?
                                 (dap_chain_datum_tx_t *)l_datum->data : NULL;

    if (l_tx) {
        return dap_db_tx_history_to_json_rpc(a_tx_hash, &l_atom_hash,l_tx, a_chain, a_hash_out_type, l_net, l_ret_code, &accepted_tx);
    } else {
        char *l_tx_hash_str = dap_strcmp(a_hash_out_type, "hex")
                ? dap_enc_base58_encode_hash_to_str(a_tx_hash)
                : dap_chain_hash_fast_to_str_new(a_tx_hash);
        dap_json_rpc_error_add(-1, "TX hash %s not founds in chains", l_tx_hash_str);
        DAP_DELETE(l_tx_hash_str);
        return NULL;
    }
}


static void s_tx_header_print_rpc(json_object* json_obj_datum, dap_chain_tx_hash_processed_ht_t **a_tx_data_ht,
                              dap_chain_datum_tx_t *a_tx, dap_hash_fast_t *a_atom_hash,
                              const char *a_hash_out_type, dap_ledger_t *a_ledger,
                              dap_chain_hash_fast_t *a_tx_hash, int a_ret_code)
{
    bool l_declined = false;
    // transaction time
    char l_time_str[32] = "unknown";                                /* Prefill string */
    if (a_tx->header.ts_created) {
        uint64_t l_ts = a_tx->header.ts_created;
        dap_ctime_r(&l_ts, l_time_str);                             /* Convert ts to  "Sat May 17 01:17:08 2014\n" */
        l_time_str[strlen(l_time_str)-1] = '\0';                    /* Remove "\n"*/
    }
    dap_chain_tx_hash_processed_ht_t *l_tx_data = NULL;
    HASH_FIND(hh, *a_tx_data_ht, a_tx_hash, sizeof(*a_tx_hash), l_tx_data);
    if (l_tx_data)  // this tx already present in ledger (double)
        l_declined = true;
    else {
        l_tx_data = DAP_NEW_Z(dap_chain_tx_hash_processed_ht_t);
        if (!l_tx_data) {
            log_it(L_CRITICAL, "Memory allocation error");
            return;
        }
        l_tx_data->hash = *a_tx_hash;
        HASH_ADD(hh, *a_tx_data_ht, hash, sizeof(*a_tx_hash), l_tx_data);
        const char *l_token_ticker = dap_chain_ledger_tx_get_token_ticker_by_hash(a_ledger, a_tx_hash);
        if (!l_token_ticker)
            l_declined = true;
    }
    char *l_tx_hash_str, *l_atom_hash_str;
    if (!dap_strcmp(a_hash_out_type, "hex")) {
        l_tx_hash_str = dap_chain_hash_fast_to_str_new(a_tx_hash);
        l_atom_hash_str = dap_chain_hash_fast_to_str_new(a_atom_hash);
    } else {
        l_tx_hash_str = dap_enc_base58_encode_hash_to_str(a_tx_hash);
        l_atom_hash_str = dap_enc_base58_encode_hash_to_str(a_atom_hash);
    }
    json_object_object_add(json_obj_datum, "status", json_object_new_string(l_declined ? "DECLINED" : "ACCEPTED"));
    json_object_object_add(json_obj_datum, "hash", json_object_new_string(l_tx_hash_str));
    json_object_object_add(json_obj_datum, "atom_hash", json_object_new_string(l_atom_hash_str));
    json_object_object_add(json_obj_datum, "ret_code", json_object_new_int(a_ret_code));
    json_object_object_add(json_obj_datum, "ret_code", json_object_new_string(dap_chain_ledger_tx_check_err_str(a_ret_code)));
    json_object_object_add(json_obj_datum, "tx_created", json_object_new_string(l_time_str));

    DAP_DELETE(l_tx_hash_str);
    DAP_DELETE(l_atom_hash_str);
}

/**
 * @brief dap_db_history_addr
 * Get data according the history log
 *
 * return history string
 * @param a_addr
 * @param a_chain
 * @param a_hash_out_type
 * @return char*
 */
json_object* dap_db_history_addr_rpc(dap_chain_addr_t *a_addr, dap_chain_t *a_chain, 
                                 const char *a_hash_out_type, const char * l_addr_str)
{
    struct token_addr {
        const char token[DAP_CHAIN_TICKER_SIZE_MAX];
        dap_chain_addr_t addr;
    };

    json_object* json_obj_datum = json_object_new_array();
    if (!json_obj_datum){
        log_it(L_CRITICAL, "Memory allocation error");
        dap_json_rpc_error_add(-44, "Memory allocation error");
        return NULL;
    }

    // add address
    json_object * json_obj_addr = json_object_new_object();
    json_object_object_add(json_obj_addr, "address", json_object_new_string(l_addr_str));
    json_object_array_add(json_obj_datum, json_obj_addr);

    dap_chain_tx_hash_processed_ht_t *l_tx_data_ht = NULL;
    dap_chain_net_t *l_net = dap_chain_net_by_id(a_chain->net_id);
    if (!l_net) {
        log_it(L_WARNING, "Can't find net by specified chain %s", a_chain->name);
        dap_json_rpc_error_add(-1, "Can't find net by specified chain %s", a_chain->name);
        json_object_put(json_obj_datum);
        return NULL;
    }
    dap_ledger_t *l_ledger = l_net->pub.ledger;
    const char *l_native_ticker = l_net->pub.native_ticker;
    if (!a_chain->callback_datum_iter_create) {
        log_it(L_WARNING, "Not defined callback_datum_iter_create for chain \"%s\"", a_chain->name);
        dap_json_rpc_error_add(-1, "Not defined callback_datum_iter_create for chain \"%s\"", a_chain->name);
        json_object_put(json_obj_datum);
        return NULL;
    }
    // load transactions
    dap_chain_datum_iter_t *l_datum_iter = a_chain->callback_datum_iter_create(a_chain);

    for (dap_chain_datum_t *l_datum = a_chain->callback_datum_iter_get_first(l_datum_iter);
                            l_datum;
                            l_datum = a_chain->callback_datum_iter_get_next(l_datum_iter))
    {
        json_object* json_obj_tx = json_object_new_object();
        if (l_datum->header.type_id != DAP_CHAIN_DATUM_TX)
            // go to next datum
            continue;
        // it's a transaction
        dap_chain_datum_tx_t *l_tx = (dap_chain_datum_tx_t *)l_datum->data;
        dap_list_t *l_list_in_items = dap_chain_datum_tx_items_get(l_tx, TX_ITEM_TYPE_IN_ALL, NULL);
        if (!l_list_in_items) // a bad tx
            continue;
        // all in items should be from the same address
        dap_chain_addr_t *l_src_addr = NULL;
        bool l_base_tx = false;
        const char *l_noaddr_token = NULL;

        dap_hash_fast_t l_tx_hash;
        dap_hash_fast(l_tx, dap_chain_datum_tx_get_size(l_tx), &l_tx_hash);
        const char *l_src_token = dap_chain_ledger_tx_get_token_ticker_by_hash(l_ledger, &l_tx_hash);

        int l_src_subtype = DAP_CHAIN_TX_OUT_COND_SUBTYPE_UNDEFINED;
        for (dap_list_t *it = l_list_in_items; it; it = it->next) {
            dap_chain_hash_fast_t *l_tx_prev_hash;
            int l_tx_prev_out_idx;
            dap_chain_datum_tx_t *l_tx_prev = NULL;
            switch (*(byte_t *)it->data) {
            case TX_ITEM_TYPE_IN: {
                dap_chain_tx_in_t *l_tx_in = (dap_chain_tx_in_t *)it->data;
                l_tx_prev_hash = &l_tx_in->header.tx_prev_hash;
                l_tx_prev_out_idx = l_tx_in->header.tx_out_prev_idx;
            } break;
            case TX_ITEM_TYPE_IN_COND: {
                dap_chain_tx_in_cond_t *l_tx_in_cond = (dap_chain_tx_in_cond_t *)it->data;
                l_tx_prev_hash = &l_tx_in_cond->header.tx_prev_hash;
                l_tx_prev_out_idx = l_tx_in_cond->header.tx_out_prev_idx;
            } break;
            case TX_ITEM_TYPE_IN_EMS: {
                dap_chain_tx_in_ems_t *l_in_ems = (dap_chain_tx_in_ems_t *)it->data;
                l_base_tx = true;
                l_noaddr_token = l_in_ems->header.ticker;
            }
            default:
                continue;
            }

            dap_chain_datum_t *l_datum = a_chain->callback_datum_find_by_hash(a_chain, l_tx_prev_hash, NULL, NULL);
            l_tx_prev = l_datum  && l_datum->header.type_id == DAP_CHAIN_DATUM_TX ? (dap_chain_datum_tx_t *)l_datum->data : NULL;
            if (l_tx_prev) {
                uint8_t *l_prev_out_union = dap_chain_datum_tx_item_get_nth(l_tx_prev, TX_ITEM_TYPE_OUT_ALL, l_tx_prev_out_idx);
                if (!l_prev_out_union)
                    continue;
                switch (*l_prev_out_union) {
                case TX_ITEM_TYPE_OUT:
                    l_src_addr = &((dap_chain_tx_out_t *)l_prev_out_union)->addr;
                    break;
                case TX_ITEM_TYPE_OUT_EXT:
                    l_src_addr = &((dap_chain_tx_out_ext_t *)l_prev_out_union)->addr;
                    break;
                case TX_ITEM_TYPE_OUT_COND:
                    l_src_subtype = ((dap_chain_tx_out_cond_t *)l_prev_out_union)->header.subtype;
                    if (l_src_subtype == DAP_CHAIN_TX_OUT_COND_SUBTYPE_FEE)
                        l_noaddr_token = l_native_ticker;
                    else
                        l_noaddr_token = l_src_token;
                default:
                    break;
                }
            }
            if (l_src_addr && !dap_chain_addr_compare(l_src_addr, a_addr))
                break;  //it's not our addr
        }
        dap_list_free(l_list_in_items);

        // find OUT items
        bool l_header_printed = false;
        uint256_t l_fee_sum = {};
        dap_list_t *l_list_out_items = dap_chain_datum_tx_items_get(l_tx, TX_ITEM_TYPE_OUT_ALL, NULL);
        for (dap_list_t *it = l_list_out_items; it; it = it->next) {
            dap_chain_addr_t *l_dst_addr = NULL;
            uint8_t l_type = *(uint8_t *)it->data;
            uint256_t l_value;
            const char *l_dst_token = NULL;
            switch (l_type) {
            case TX_ITEM_TYPE_OUT:
                l_dst_addr = &((dap_chain_tx_out_t *)it->data)->addr;
                l_value = ((dap_chain_tx_out_t *)it->data)->header.value;
                l_dst_token = l_src_token;
                break;
            case TX_ITEM_TYPE_OUT_EXT:
                l_dst_addr = &((dap_chain_tx_out_ext_t *)it->data)->addr;
                l_value = ((dap_chain_tx_out_ext_t *)it->data)->header.value;
                l_dst_token = ((dap_chain_tx_out_ext_t *)it->data)->token;
                break;
            case TX_ITEM_TYPE_OUT_COND:
                l_value = ((dap_chain_tx_out_cond_t *)it->data)->header.value;
                if (((dap_chain_tx_out_cond_t *)it->data)->header.subtype == DAP_CHAIN_TX_OUT_COND_SUBTYPE_FEE) {
                    SUM_256_256(l_fee_sum, ((dap_chain_tx_out_cond_t *)it->data)->header.value, &l_fee_sum);
                    l_dst_token = l_native_ticker;
                } else
                    l_dst_token = l_src_token;
            default:
                break;
            }
            if (l_src_addr && l_dst_addr && dap_chain_addr_compare(l_dst_addr, l_src_addr) &&
                    dap_strcmp(l_dst_token, l_noaddr_token))
                continue;   // sent to self (coinback)
            if (l_src_addr && dap_chain_addr_compare(l_src_addr, a_addr) &&
                    dap_strcmp(l_dst_token, l_noaddr_token)) {
                if (!l_header_printed) {
                    s_tx_header_print_rpc(json_obj_tx, &l_tx_data_ht, l_tx, l_datum_iter->cur_atom_hash,
                                      a_hash_out_type, l_ledger, &l_tx_hash, l_datum_iter->ret_code);
                    l_header_printed = true;
                }
                const char *l_dst_addr_str = l_dst_addr ? dap_chain_addr_to_str(l_dst_addr)
                                                        : dap_chain_tx_out_cond_subtype_to_str(
                                                              ((dap_chain_tx_out_cond_t *)it->data)->header.subtype);
                char *l_value_str = dap_chain_balance_print(l_value);
                char *l_coins_str = dap_chain_balance_to_coins(l_value);
                json_object_object_add(json_obj_tx, "tx_type", json_object_new_string("send"));
                json_object_object_add(json_obj_tx, "send_coins", json_object_new_string(l_coins_str));
                json_object_object_add(json_obj_tx, "send_datoshi", json_object_new_string(l_value_str));
                json_object_object_add(json_obj_tx, "token", l_dst_token ? json_object_new_string(l_dst_token) 
                                                                            : json_object_new_string("UNKNOWN"));
                json_object_object_add(json_obj_tx, "destination_address", json_object_new_string(l_dst_addr_str));
                if (l_dst_addr)
                    DAP_DELETE(l_dst_addr_str);
                DAP_DELETE(l_value_str);
                DAP_DELETE(l_coins_str);
            }
            if (l_dst_addr && dap_chain_addr_compare(l_dst_addr, a_addr)) {
                if (!l_header_printed) {
                    s_tx_header_print_rpc(json_obj_tx, &l_tx_data_ht, l_tx, l_datum_iter->cur_atom_hash,
                                      a_hash_out_type, l_ledger, &l_tx_hash, l_datum_iter->ret_code);
                    l_header_printed = true;
                }
                const char *l_src_addr_str = NULL, *l_src_str;
                if (l_base_tx)
                    l_src_str = "emission";
                else if (l_src_addr && dap_strcmp(l_dst_token, l_noaddr_token))
                    l_src_str = l_src_addr_str = dap_chain_addr_to_str(l_src_addr);
                else
                    l_src_str = dap_chain_tx_out_cond_subtype_to_str(l_src_subtype);
                char *l_value_str = dap_chain_balance_print(l_value);
                char *l_coins_str = dap_chain_balance_to_coins(l_value);
                json_object_object_add(json_obj_tx, "tx_type", json_object_new_string("recv"));
                json_object_object_add(json_obj_tx, "recv_coins", json_object_new_string(l_coins_str));
                json_object_object_add(json_obj_tx, "recv_datoshi", json_object_new_string(l_value_str));
                json_object_object_add(json_obj_tx, "token", l_dst_token ? json_object_new_string(l_dst_token) 
                                                                            : json_object_new_string("UNKNOWN"));
                json_object_object_add(json_obj_tx, "source_address", json_object_new_string(l_src_str));
                DAP_DEL_Z(l_src_addr_str);
                DAP_DELETE(l_value_str);
                DAP_DELETE(l_coins_str);
            }
        }
        dap_list_free(l_list_out_items);
        // fee for base TX in native token
        if (l_header_printed && l_base_tx && !dap_strcmp(l_native_ticker, l_src_token)) {
            char *l_fee_value_str = dap_chain_balance_print(l_fee_sum);
            char *l_fee_coins_str = dap_chain_balance_to_coins(l_fee_sum);
            json_object_object_add(json_obj_tx, "fee", json_object_new_string(l_fee_coins_str));;
            json_object_object_add(json_obj_tx, "fee_datoshi", json_object_new_string(l_fee_value_str));
            DAP_DELETE(l_fee_value_str);
            DAP_DELETE(l_fee_coins_str);
        }
        if (json_object_object_length(json_obj_tx) > 0) {
            json_object_array_add(json_obj_datum, json_obj_tx);
        }
    }
    a_chain->callback_datum_iter_delete(l_datum_iter);
    // delete hashes
    s_dap_chain_tx_hash_processed_ht_free(&l_tx_data_ht);
    // if no history
    if (json_object_array_length(json_obj_datum) == 1) {
        json_object * json_empty_tx = json_object_new_object();
        json_object_object_add(json_empty_tx, "status", json_object_new_string("empty"));
        json_object_array_add(json_obj_datum, json_empty_tx);
    }
    return json_obj_datum;
}

json_object* dap_db_history_tx_all_rpc(dap_chain_t *l_chain, dap_chain_net_t* l_net, const char *l_hash_out_type, json_object * json_obj_summary) {
        log_it(L_DEBUG, "Start getting tx from chain");
        size_t l_tx_count = 0;
        size_t l_tx_ledger_accepted = 0;
        size_t l_tx_ledger_rejected = 0;
        dap_chain_cell_t    *l_cell = NULL,
                            *l_cell_tmp = NULL;
        dap_chain_atom_iter_t *l_iter = NULL;
        json_object * json_arr_out = json_object_new_array();
        HASH_ITER(hh, l_chain->cells, l_cell, l_cell_tmp) {
            l_iter = l_chain->callback_atom_iter_create(l_chain, l_cell->id, 0);
            size_t l_atom_size = 0;
            dap_chain_atom_ptr_t l_ptr = l_chain->callback_atom_iter_get_first(l_iter, &l_atom_size);
            while (l_ptr && l_atom_size) {
                size_t l_datums_count = 0;
                dap_chain_datum_t **l_datums = l_cell->chain->callback_atom_get_datums(l_ptr, l_atom_size, &l_datums_count);
                for (size_t i = 0; i < l_datums_count; i++) {
                    if (l_datums[i]->header.type_id == DAP_CHAIN_DATUM_TX) {
                        l_tx_count++;
                        dap_chain_datum_tx_t *l_tx = (dap_chain_datum_tx_t*)l_datums[i]->data;
                        dap_hash_fast_t l_ttx_hash = {0};
                        dap_hash_fast(l_tx, l_datums[i]->header.data_size, &l_ttx_hash);
                        bool accepted_tx;
                        json_object* json_obj_datum = dap_db_tx_history_to_json_rpc(&l_ttx_hash, NULL, l_tx, l_chain, l_hash_out_type, l_net, NULL, &accepted_tx);
                        if (!json_obj_datum) {
                            log_it(L_CRITICAL, "Memory allocation error");
                            return NULL;
                        }
                        if (accepted_tx)
                            l_tx_ledger_accepted++;
                        else
                            l_tx_ledger_rejected++;
                        json_object_array_add(json_arr_out, json_obj_datum);
                        const char * debug_json_string = json_object_to_json_string(json_obj_datum);
                    }
                }
                DAP_DEL_Z(l_datums);
                l_ptr = l_chain->callback_atom_iter_get_next(l_iter, &l_atom_size);
            }
            l_cell->chain->callback_atom_iter_delete(l_iter);
        }
        log_it(L_DEBUG, "END getting tx from chain");

        json_object_object_add(json_obj_summary, "network", json_object_new_string(l_net->pub.name));
        json_object_object_add(json_obj_summary, "chain", json_object_new_string(l_chain->name));
        json_object_object_add(json_obj_summary, "tx_sum", json_object_new_int(l_tx_count));
        json_object_object_add(json_obj_summary, "accepted_tx", json_object_new_int(l_tx_ledger_accepted));
        json_object_object_add(json_obj_summary, "rejected_tx", json_object_new_int(l_tx_ledger_rejected));
        return json_arr_out;
}
