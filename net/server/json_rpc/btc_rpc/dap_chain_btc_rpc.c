#include "dap_chain_btc_rpc.h"

#include "dap_common.h"

#define LOG_TAG "dap_chain_btc_rpc"

int dap_chain_btc_rpc_init(void)
{
    dap_chain_btc_rpc_registration_handlers();
    return 0;
}

void dap_chain_btc_rpc_deinit(void)
{
    dap_chain_btc_rpc_unregistration_handlers();
}
