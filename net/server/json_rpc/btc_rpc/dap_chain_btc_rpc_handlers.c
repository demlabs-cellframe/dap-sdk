#include "dap_chain_btc_rpc_handlers.h"

#include "dap_common.h"
#include "dap_strfuncs.h"

#define LOG_TAG "dap_chain_btc_rpc_handlers"

static void s_btc_rpc_unsupported_handler(UNUSED_ARG dap_json_rpc_params_t *a_params,
                                          dap_json_rpc_response_t *a_response,
                                          const char *a_method)
{
    const char *l_method = a_method ? a_method : "unknown";
    log_it(L_WARNING, "Deprecated BTC JSON-RPC method \"%s\" is not supported", l_method);
    if (!a_response)
        return;
    json_object *l_error = json_object_new_object();
    json_object *l_data = json_object_new_object();
    if (!l_error || !l_data) {
        json_object_put(l_error);
        json_object_put(l_data);
        a_response->type = TYPE_RESPONSE_NULL;
        return;
    }
    json_object_object_add(l_error, "code", json_object_new_int(-32601));
    json_object_object_add(l_error, "message", json_object_new_string("Method not found"));
    json_object_object_add(l_data, "method", json_object_new_string(l_method));
    json_object_object_add(l_error, "data", l_data);

    /* dap_json_rpc_response_t has no top-level error field; serialization wraps JSON values in "result". */
    a_response->type = TYPE_RESPONSE_JSON;
    a_response->result_json_object = l_error;
}

void dap_chain_btc_rpc_registration_handlers(void)
{
    log_it(L_WARNING, "Deprecated BTC JSON-RPC compatibility shim: no handlers registered");
}

void dap_chain_btc_rpc_unregistration_handlers(void)
{
}

#define BTC_RPC_UNSUPPORTED_HANDLER(name) \
    void name(dap_json_rpc_params_t *a_params, dap_json_rpc_response_t *a_response, const char *a_method) \
    { \
        s_btc_rpc_unsupported_handler(a_params, a_response, a_method); \
    }

BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_addmultisigaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_addnode)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_backupwallet)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_createmultisig)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_createrawtransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_decoderawtransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_dumpprivkey)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_dumpwallet)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_encryptwallet)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getaccount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getaccountaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getaddednodeinfo)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getaddressesbyaccount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getbalance)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getbestblockhash)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getblock)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getblockcount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getblockhash)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getblocknumber)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getblocktemplate)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getconnectioncount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getdifficulty)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getgenerate)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_gethashespersec)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getinfo)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getmemorypool)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getmininginfo)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getnewaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getpeerinfo)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getrawchangeaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getrawmempool)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getrawtransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getreceivedbyaccount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getreceivedbyaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_gettransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_gettxout)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_gettxoutsetinfo)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_getwork)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_help)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_importprivkey)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_invalidateblock)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_keypoolrefill)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listaccounts)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listaddressgroupings)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listreceivedbyaccount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listreceivedbyaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listsinceblock)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listtransactions)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listunspent)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_listlockunspent)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_lockunspent)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_move)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_sendfrom)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_sendmany)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_sendrawtransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_sendtoaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_setaccount)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_setgenerate)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_settxfee)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_signmessage)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_signrawtransaction)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_stop)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_submitblock)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_validateaddress)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_verifymessage)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_walletlock)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_walletpassphrase)
BTC_RPC_UNSUPPORTED_HANDLER(dap_chain_btc_rpc_handler_walletpassphrasechange)
