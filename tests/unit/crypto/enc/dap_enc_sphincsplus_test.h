#ifndef _DAP_ENC_SPHINCSPLUS_TEST_H_
#define _DAP_ENC_SPHINCSPLUS_TEST_H_

#include "dap_enc_sphincsplus.h"

#ifdef __cplusplus
extern "C" {
#endif

// Test-only functions for sphincsplus configuration
// These functions allow tests to modify the default configuration
void dap_enc_sig_sphincsplus_set_default_config(sphincsplus_config_t a_new_config);
int dap_enc_sig_sphincsplus_get_configs_count(void);

#ifdef __cplusplus
}
#endif

#endif //_DAP_ENC_SPHINCSPLUS_TEST_H_

