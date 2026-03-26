/**
 * @file dap_enc_mldsa.c
 * @brief ML-DSA (FIPS 204) wrapper over Dilithium core.
 *
 * Maps security levels to FIPS 204 parameter sets:
 *   Category 2 (ML-DSA-44) -> MLDSA_44  (k=4, l=4, eta=2)
 *   Category 3 (ML-DSA-65) -> MLDSA_65  (k=6, l=5, eta=4)
 *   Category 5 (ML-DSA-87) -> MLDSA_87  (k=8, l=7, eta=2)
 *
 * Legacy Dilithium is separate: dap_enc_dilithium.c uses MODE_0..MODE_3.
 *
 * @authors naeper
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_enc_mldsa.h"
#include "dap_enc_dilithium.h"
#include "dap_sign.h"
#include "dap_common.h"

#define LOG_TAG "dap_enc_sig_mldsa"

dilithium_kind_t dap_enc_mldsa_resolve_mode(uint8_t a_sign_params)
{
    switch (a_sign_params & DAP_SIGN_PARAMS_SECURITY_MASK) {
    case DAP_SIGN_PARAMS_SECURITY_2:
        return MLDSA_44;
    case DAP_SIGN_PARAMS_SECURITY_5:
        return MLDSA_87;
    case DAP_SIGN_PARAMS_SECURITY_3:
    case DAP_SIGN_PARAMS_DEFAULT:
    default:
        return MLDSA_65;
    }
}

void dap_enc_sig_mldsa_key_new(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    a_key->type = DAP_ENC_KEY_TYPE_SIG_ML_DSA;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_dilithium_get_sign;
    a_key->sign_verify = dap_enc_sig_dilithium_verify_sign;
}

void dap_enc_sig_mldsa_key_new_generate(dap_enc_key_t *a_key, const void *a_kex_buf,
        size_t a_kex_size, const void *a_seed, size_t a_seed_size, size_t a_key_size)
{
    (void)a_kex_buf;
    (void)a_kex_size;
    dap_return_if_pass(!a_key);

    if (a_key->priv_key_data || a_key->pub_key_data)
        dap_enc_sig_dilithium_key_delete(a_key);

    uint8_t l_sign_params = (uint8_t)(a_key_size & 0xFF);
    dilithium_kind_t l_mode = dap_enc_mldsa_resolve_mode(l_sign_params);

    a_key->priv_key_data_size = sizeof(dilithium_private_key_t);
    a_key->pub_key_size = sizeof(dilithium_public_key_t);
    a_key->priv_key_data = DAP_NEW_Z(dilithium_private_key_t);
    a_key->pub_key_data = DAP_NEW_Z(dilithium_public_key_t);
    if (!a_key->priv_key_data || !a_key->pub_key_data) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        dap_enc_sig_dilithium_key_delete(a_key);
        return;
    }

    int l_rc = dilithium_crypto_sign_keypair(
            (dilithium_public_key_t *)a_key->pub_key_data,
            (dilithium_private_key_t *)a_key->priv_key_data,
            l_mode, a_seed, a_seed_size);
    if (l_rc) {
        log_it(L_CRITICAL, "ML-DSA keygen failed (mode %d)", l_mode);
        dap_enc_sig_dilithium_key_delete(a_key);
    }
}
