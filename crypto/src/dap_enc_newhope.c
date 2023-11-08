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
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "newhope/newhope_cpapke.h"
#include "newhope/newhope_params.h"


#define LOG_TAG "dap_enc_newhope_pke"

DAP_NEWHOPE_SIGN_SECURITY _newhope_type = NEWHOPE_1024; // by default


void dap_enc_newhope_pke_set_type(DAP_NEWHOPE_SIGN_SECURITY type)
{
    _newhope_type = type;
}

void dap_enc_newhope_kem_key_new(dap_enc_key_t *key) {

    key->type = DAP_ENC_KEY_TYPE_RLWE_NEWHOPE_CPA_KEM;
    key->enc = NULL;
    key->enc_na = NULL;
    key->dec_na = NULL;
    key->gen_bob_shared_key= dap_enc_newhope_gen_bob_shared_key;
    key->gen_alice_shared_key = dap_enc_newhope_prk_dec;
    key->priv_key_data  = NULL;
    key->pub_key_data   = NULL;

}

void dap_enc_newhope_kem_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *a_kex_buf,
        UNUSED_ARG size_t a_kex_size, UNUSED_ARG const void *a_seed, UNUSED_ARG size_t a_seed_size,
        UNUSED_ARG size_t a_key_size)
{
// income check
    dap_return_if_pass(!a_key);
// work prepare
    DAP_NEWHOPE_SIGN_SECURITY newhope_type = NEWHOPE_1024;
    dap_enc_newhope_pke_set_type(newhope_type);
// memory alloc
    DAP_NEW_Z_SIZE_RET(a_key->_inheritor, uint8_t, NEWHOPE_CPAPKE_SECRETKEYBYTES, NULL);
    DAP_NEW_Z_SIZE_RET(a_key->pub_key_data, uint8_t, NEWHOPE_CPAPKE_PUBLICKEYBYTES, a_key->_inheritor);
// crypto calc
    cpapke_keypair(a_key->pub_key_data, a_key->_inheritor);
// post func work
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
    uint8_t *l_shared_key, *l_cypher_msg;
    DAP_NEW_Z_SIZE_RET_VAL(l_shared_key, uint8_t, NEWHOPE_SYMBYTES, 0, NULL);
    DAP_NEW_Z_SIZE_RET_VAL(l_cypher_msg, uint8_t, NEWHOPE_CPAKEM_CIPHERTEXTBYTES, 0, l_shared_key);
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

size_t dap_enc_newhope_prk_dec(dap_enc_key_t *a_key, const void *a_priv,
                               size_t a_sendb_size, unsigned char *a_sendb)
{
// sanity check
    dap_return_val_if_pass(!a_key || a_sendb_size != NEWHOPE_CPAKEM_CIPHERTEXTBYTES, 0);
// memory alloc
    a_key->shared_key_size = 0;
    DAP_DEL_Z(a_key->shared_key);
    DAP_NEW_Z_SIZE_RET_VAL(a_key->shared_key, uint8_t, NEWHOPE_SYMBYTES, 0, NULL);
// crypto calc
    crypto_kem_dec(a_key->priv_key_data, a_sendb, a_key->_inheritor);
// post func work
    a_key->shared_key_size = NEWHOPE_SYMBYTES;
    return a_key->shared_key_size;
}

void dap_enc_newhope_kem_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_DEL_Z(a_key->_inheritor);
    a_key->_inheritor_size= 0;
}

