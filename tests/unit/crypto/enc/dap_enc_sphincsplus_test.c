#include "dap_enc_sphincsplus_test.h"
#include "dap_enc_sphincsplus.h"
#include "sphincsplus/sphincsplus_params.h"

// Access to internal default config variable (test-only)
extern _Thread_local sphincsplus_config_t s_default_config;

void dap_enc_sig_sphincsplus_set_default_config(sphincsplus_config_t a_new_config)
{
    s_default_config = a_new_config;
}

int dap_enc_sig_sphincsplus_get_configs_count(void)
{
    return SPHINCSPLUS_CONFIG_MAX_ARG - 1;
}

