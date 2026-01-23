#ifndef _DAP_ENC_SPHINCSPLUS_TEST_H_
#define _DAP_ENC_SPHINCSPLUS_TEST_H_

#include "dap_enc_sphincsplus.h"

#ifdef __cplusplus
extern "C" {
#endif

// Runtime configuration functions for sphincsplus
// These functions are always available (no conditional compilation)
// Declared in dap_enc_sphincsplus.h, defined in dap_enc_sphincsplus.c
void dap_enc_sig_sphincsplus_set_default_config(sphincsplus_config_t a_new_config);
int dap_enc_sig_sphincsplus_get_configs_count(void);

#ifdef __cplusplus
}
#endif

#endif //_DAP_ENC_SPHINCSPLUS_TEST_H_

