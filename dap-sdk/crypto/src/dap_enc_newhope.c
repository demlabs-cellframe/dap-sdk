/*
 * Authors:
 * Dmitriy A. Gearasimov <naeper@demlabs.net>
 * Demlabs Limited   https://demlabs.net
 * Sources community https://gitlab.demlabs.net/cellframe/cellframe-sdk/dap-sdk
 * Copyright  (c) 2017-2020
 * All rights reserved.

 This file is part of DAP SDK the open source project

    DAP SDK is free software: you can redistribute it and/or modify
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

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>

#include "dap_enc_newhope.h"
#include "rand/dap_rand.h"
#include "newhope/newhope_cpapke.h"
#include "newhope/newhope_params.h"


#define LOG_TAG "dap_enc_newhope_pke"

DAP_NEWHOPE_SIGN_SECURITY _newhope_type = NEWHOPE_1024; // by default


void dap_enc_newhope_pke_set_type(DAP_NEWHOPE_SIGN_SECURITY type)
{
    _newhope_type = type;
}

void dap_enc_newhope_kem_key_new(dap_enc_key_t *a_key) {

    a_key->type = DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM;
    a_key->enc = NULL;
    a_key->enc_na = NULL;
    a_key->dec_na = NULL;
    a_key->gen_bob_shared_key= dap_enc_newhope_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_newhope_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;

}

void dap_enc_newhope_kem_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
                                    UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed, 
                                    UNUSED_ARG size_t a_seed_size, UNUSED_ARG size_t a_key_size)
{
    dap_return_if_pass(!a_key);
    DAP_NEWHOPE_SIGN_SECURITY newhope_type = NEWHOPE_1024;
    dap_enc_newhope_pke_set_type(newhope_type);
    uint8_t *l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, NEWHOPE_CPAPKE_SECRETKEYBYTES),
            *l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, NEWHOPE_CPAPKE_PUBLICKEYBYTES, l_skey);
    cpapke_keypair(l_pkey, l_skey);
    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->_inheritor_size = NEWHOPE_CPAPKE_SECRETKEYBYTES;
    a_key->pub_key_data_size = NEWHOPE_CPAPKE_PUBLICKEYBYTES;
    return;
}

/**
 * @brief Сheck whether a memory address is writable or not
 * @param a_p Pointer
 * @param a_len Checked memory size
 * @return True if the memory is writable, otherwise false
 */
bool is_writeable_memory(void *a_p, size_t a_len)
{
    int l_fd = open("/dev/zero", O_RDONLY);
    bool l_writeable;
    if (l_fd < 0)
        return FALSE; // Should not happen
    l_writeable = read(l_fd, a_p, a_len) == (ssize_t)a_len;
    close(l_fd);
    return l_writeable;
}

size_t dap_enc_newhope_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub, size_t a_alice_pub_size, void **a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg || a_alice_pub_size < NEWHOPE_CPAPKE_PUBLICKEYBYTES, 0)
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, NEWHOPE_SYMBYTES, false),
            *l_cypher_msg =  DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, NEWHOPE_CPAKEM_CIPHERTEXTBYTES, false, l_shared_key);
// crypto calc
    if (crypto_kem_enc(l_cypher_msg, l_shared_key, a_alice_pub)) {
        DAP_DEL_MULTY(l_cypher_msg, l_shared_key);
        return 0;
    }
// post func work, change in args only after all pass
    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_cypher_msg;
    a_bob_key->shared_key = l_shared_key;
    a_bob_key->shared_key_size = NEWHOPE_SYMBYTES;
    return NEWHOPE_CPAKEM_CIPHERTEXTBYTES;
}

size_t dap_enc_newhope_gen_alice_shared_key(dap_enc_key_t *a_alice_key, UNUSED_ARG const void *a_alice_priv,
                               size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
// sanity check
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg || a_cypher_msg_size < NEWHOPE_CPAKEM_CIPHERTEXTBYTES, 0);
// memory alloc
    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, NEWHOPE_SYMBYTES, 0);
// crypto calc
    if (crypto_kem_dec(l_shared_key, a_cypher_msg, a_alice_key->_inheritor)) {
        DAP_DELETE(l_shared_key);
        return 0;
    }
// post func work
    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared_key;
    a_alice_key->shared_key_size = NEWHOPE_SYMBYTES;
    return a_alice_key->shared_key_size;
}

void dap_enc_newhope_kem_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->priv_key_data);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_DEL_Z(a_key->_inheritor);
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

