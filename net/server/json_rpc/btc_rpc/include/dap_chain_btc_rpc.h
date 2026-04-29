#pragma once

#include "dap_chain_btc_rpc_handlers.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Deprecated compatibility target for SDK consumers that still include
 * dap_chain_btc_rpc.h or link dap_chain_btc_rpc. BTC JSON-RPC handlers are no
 * longer registered by this module.
 */
int dap_chain_btc_rpc_init(void);
void dap_chain_btc_rpc_deinit(void);

#ifdef __cplusplus
}
#endif
