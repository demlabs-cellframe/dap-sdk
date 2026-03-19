/**
 * @file dap_enc_mlkem.c
 * @brief ML-KEM (FIPS 203) wrapper for the DAP enc_key framework.
 *
 * Supports ML-KEM-512 (K=2), ML-KEM-768 (K=3), ML-KEM-1024 (K=4).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_common.h"
#include "dap_memwipe.h"
#include "dap_enc_mlkem.h"
#include "dap_sign.h"

#define LOG_TAG "dap_enc_mlkem"

/* ---- ML-KEM-512 ---- */

#define MLKEM512_PUBLICKEYBYTES   800
#define MLKEM512_SECRETKEYBYTES  1632
#define MLKEM512_CIPHERTEXTBYTES  768
#define MLKEM512_BYTES             32

int dap_mlkem512_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem512_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem512_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ---- ML-KEM-768 ---- */

#define MLKEM768_PUBLICKEYBYTES  1184
#define MLKEM768_SECRETKEYBYTES  2400
#define MLKEM768_CIPHERTEXTBYTES 1088
#define MLKEM768_BYTES             32

int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ---- ML-KEM-1024 ---- */

#define MLKEM1024_PUBLICKEYBYTES  1568
#define MLKEM1024_SECRETKEYBYTES  3168
#define MLKEM1024_CIPHERTEXTBYTES 1568
#define MLKEM1024_BYTES             32

int dap_mlkem1024_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem1024_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem1024_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ---- level helpers ---- */

typedef struct {
    int    (*keypair)(uint8_t *, uint8_t *);
    int    (*enc)(uint8_t *, uint8_t *, const uint8_t *);
    int    (*dec)(uint8_t *, const uint8_t *, const uint8_t *);
    size_t pk_bytes;
    size_t sk_bytes;
    size_t ct_bytes;
    size_t ss_bytes;
} mlkem_variant_t;

static const mlkem_variant_t s_mlkem512 = {
    .keypair  = dap_mlkem512_kem_keypair,
    .enc      = dap_mlkem512_kem_enc,
    .dec      = dap_mlkem512_kem_dec,
    .pk_bytes = MLKEM512_PUBLICKEYBYTES,
    .sk_bytes = MLKEM512_SECRETKEYBYTES,
    .ct_bytes = MLKEM512_CIPHERTEXTBYTES,
    .ss_bytes = MLKEM512_BYTES,
};

static const mlkem_variant_t s_mlkem768 = {
    .keypair  = dap_mlkem768_kem_keypair,
    .enc      = dap_mlkem768_kem_enc,
    .dec      = dap_mlkem768_kem_dec,
    .pk_bytes = MLKEM768_PUBLICKEYBYTES,
    .sk_bytes = MLKEM768_SECRETKEYBYTES,
    .ct_bytes = MLKEM768_CIPHERTEXTBYTES,
    .ss_bytes = MLKEM768_BYTES,
};

static const mlkem_variant_t s_mlkem1024 = {
    .keypair  = dap_mlkem1024_kem_keypair,
    .enc      = dap_mlkem1024_kem_enc,
    .dec      = dap_mlkem1024_kem_dec,
    .pk_bytes = MLKEM1024_PUBLICKEYBYTES,
    .sk_bytes = MLKEM1024_SECRETKEYBYTES,
    .ct_bytes = MLKEM1024_CIPHERTEXTBYTES,
    .ss_bytes = MLKEM1024_BYTES,
};

static const mlkem_variant_t *s_get_variant(uint8_t a_level)
{
    switch (a_level) {
    case DAP_SIGN_PARAMS_SECURITY_5:
        return &s_mlkem1024;
    case DAP_SIGN_PARAMS_SECURITY_3:
        return &s_mlkem768;
    default:
        return &s_mlkem512;
    }
}

static const mlkem_variant_t *s_get_variant_by_sk_size(size_t a_sk_size)
{
    if (a_sk_size == MLKEM1024_SECRETKEYBYTES)
        return &s_mlkem1024;
    if (a_sk_size == MLKEM768_SECRETKEYBYTES)
        return &s_mlkem768;
    return &s_mlkem512;
}

void dap_enc_mlkem_key_new(dap_enc_key_t *a_key)
{
    a_key->type = DAP_ENC_KEY_TYPE_ML_KEM;
    a_key->dec = NULL;
    a_key->enc = NULL;
    a_key->gen_bob_shared_key = dap_enc_mlkem_gen_bob_shared_key;
    a_key->gen_alice_shared_key = dap_enc_mlkem_gen_alice_shared_key;
    a_key->priv_key_data_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
    a_key->priv_key_data = NULL;
    a_key->pub_key_data = NULL;
    a_key->_inheritor = NULL;
}

void dap_enc_mlkem_key_generate(dap_enc_key_t *a_key, const void *a_kex_buf,
        size_t a_kex_size, const void *a_seed, size_t a_seed_size, size_t a_key_size)
{
    (void)a_kex_buf; (void)a_kex_size; (void)a_seed; (void)a_seed_size;
    dap_return_if_pass(!a_key);

    uint8_t l_level = (uint8_t)a_key_size & DAP_SIGN_PARAMS_SECURITY_MASK;
    const mlkem_variant_t *l_v = s_get_variant(l_level);

    uint8_t *l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_v->sk_bytes),
            *l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, l_v->pk_bytes, l_skey);
    if (l_v->keypair(l_pkey, l_skey)) {
        DAP_DEL_MULTY(l_pkey, l_skey);
        return;
    }
    DAP_DEL_MULTY(a_key->_inheritor, a_key->pub_key_data);
    a_key->_inheritor = l_skey;
    a_key->pub_key_data = l_pkey;
    a_key->_inheritor_size = l_v->sk_bytes;
    a_key->pub_key_data_size = l_v->pk_bytes;
}

void dap_enc_mlkem_key_new_from_data_public(dap_enc_key_t *a_key, const void *a_in, size_t a_in_size)
{
    dap_return_if_pass(!a_key || !a_in || !a_in_size);
    a_key->pub_key_data = DAP_DUP_SIZE((void *)a_in, a_in_size);
    a_key->pub_key_data_size = a_in_size;
}

void dap_enc_mlkem_key_delete(dap_enc_key_t *a_key)
{
    dap_return_if_pass(!a_key);
    DAP_WIPE_AND_FREE(a_key->shared_key, a_key->shared_key_size);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_WIPE_AND_FREE(a_key->_inheritor, a_key->_inheritor_size);
    a_key->shared_key_size = 0;
    a_key->pub_key_data_size = 0;
    a_key->_inheritor_size = 0;
}

size_t dap_enc_mlkem_gen_bob_shared_key(dap_enc_key_t *a_bob_key, const void *a_alice_pub,
        size_t a_alice_pub_size, void **a_cypher_msg)
{
    dap_return_val_if_pass(!a_bob_key || !a_alice_pub || !a_cypher_msg, 0);

    const mlkem_variant_t *l_v = s_get_variant_by_sk_size(a_bob_key->_inheritor_size);
    dap_return_val_if_pass(a_alice_pub_size < l_v->pk_bytes, 0);

    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_v->ss_bytes, 0),
            *l_cypher_msg = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_v->ct_bytes, 0, l_shared_key);
    if (l_v->enc(l_cypher_msg, l_shared_key, a_alice_pub)) {
        DAP_DEL_MULTY(l_cypher_msg, l_shared_key);
        return 0;
    }
    DAP_DEL_MULTY(a_bob_key->shared_key, *a_cypher_msg);
    *a_cypher_msg = l_cypher_msg;
    a_bob_key->shared_key = l_shared_key;
    a_bob_key->shared_key_size = l_v->ss_bytes;
    return l_v->ct_bytes;
}

size_t dap_enc_mlkem_gen_alice_shared_key(dap_enc_key_t *a_alice_key, const void *a_alice_priv,
        size_t a_cypher_msg_size, uint8_t *a_cypher_msg)
{
    (void)a_alice_priv;
    dap_return_val_if_pass(!a_alice_key || !a_cypher_msg, 0);

    const mlkem_variant_t *l_v = s_get_variant_by_sk_size(a_alice_key->_inheritor_size);
    dap_return_val_if_pass(a_cypher_msg_size < l_v->ct_bytes, 0);

    uint8_t *l_shared_key = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_v->ss_bytes, 0);
    if (l_v->dec(l_shared_key, a_cypher_msg, a_alice_key->_inheritor))
        return DAP_DELETE(l_shared_key), 0;
    DAP_DEL_Z(a_alice_key->shared_key);
    a_alice_key->shared_key = l_shared_key;
    a_alice_key->shared_key_size = l_v->ss_bytes;
    return a_alice_key->shared_key_size;
}
