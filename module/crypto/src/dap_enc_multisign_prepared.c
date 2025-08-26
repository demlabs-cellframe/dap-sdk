/*
 * Authors:
 * Pavel Uhanov <uhanov.pavel@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net    https:/gitlab.com/demlabs
 * Kelvin Project https://github.com/kelvinblockchain
 * Copyright  (c) 2017-2024
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <dap_enc_multisign_prepared.h>

#define LOG_TAG "dap_enc_multisign_prepared"

#ifdef DAP_ECDSA
/*--------------------------ECDSA+DILITHIUM block--------------------------*/
void dap_enc_sig_multisign_ecdsa_dilithium_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM;
    a_key->sign_get = dap_enc_sig_multisign_get_sign;
    a_key->sign_verify = dap_enc_sig_multisign_verify_sign;
}

void dap_enc_sig_multisign_ecdsa_dilithium_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf, UNUSED_ARG size_t a_kex_size,
        const void *a_seed, size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(a_key->type != DAP_ENC_KEY_TYPE_SIG_MULTI_ECDSA_DILITHIUM);
    const dap_enc_key_type_t l_key_types[2] = {DAP_ENC_KEY_TYPE_SIG_ECDSA, DAP_ENC_KEY_TYPE_SIG_DILITHIUM};
    dap_enc_sig_multisign_key_new_generate(a_key, l_key_types, 2, a_seed, a_seed_size, 0);
}
/*--------------------------ECDSA+DILITHIUM block end-----------------------*/
#endif