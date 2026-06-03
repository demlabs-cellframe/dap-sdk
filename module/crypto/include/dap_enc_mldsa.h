#pragma once

#include "dap_enc_key.h"
#include "dap_enc_dilithium.h"
#include "dap_sign.h"

#ifdef __cplusplus
extern "C" {
#endif

void dap_enc_sig_mldsa_key_new(dap_enc_key_t *a_key);
void dap_enc_sig_mldsa_key_new_generate(dap_enc_key_t *a_key, const void *kex_buf,
        size_t kex_size, const void *seed, size_t seed_size, size_t key_size);

/**
 * @brief Resolve Dilithium mode from sign_params security level.
 *        DAP_SIGN_PARAMS_DEFAULT / _SECURITY_3 -> MODE_2 (ML-DSA-65, NIST recommended)
 *        DAP_SIGN_PARAMS_SECURITY_2            -> MODE_1 (ML-DSA-44)
 *        DAP_SIGN_PARAMS_SECURITY_5            -> MODE_3 (ML-DSA-87)
 */
dilithium_kind_t dap_enc_mldsa_resolve_mode(uint8_t a_sign_params);

#ifdef __cplusplus
}
#endif
