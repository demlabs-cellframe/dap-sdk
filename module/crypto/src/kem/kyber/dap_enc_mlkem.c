/*
 * ML-KEM (FIPS 203) wrapper over the Kyber implementation.
 *
 * Security level is selected via key_size parameter:
 *   DAP_SIGN_PARAMS_SECURITY_2 -> ML-KEM-512  (Kyber512, KYBER_K=2)
 *   DAP_SIGN_PARAMS_SECURITY_3 -> ML-KEM-768  (Kyber768, KYBER_K=3)  [NIST recommended]
 *   DAP_SIGN_PARAMS_SECURITY_5 -> ML-KEM-1024 (Kyber1024, KYBER_K=4)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "dap_common.h"
#include "dap_enc_mlkem.h"
#include "dap_sign.h"

#define LOG_TAG "dap_enc_mlkem"

/* ---- Kyber512 (always available) ---- */

#define KYBER512_PUBLICKEYBYTES   800
#define KYBER512_SECRETKEYBYTES  1632
#define KYBER512_CIPHERTEXTBYTES  768
#define KYBER512_BYTES             32

int pqcrystals_kyber512_ref_keypair(unsigned char *pk, unsigned char *sk);
int pqcrystals_kyber512_ref_enc(unsigned char *ct, unsigned char *ss, const unsigned char *pk);
int pqcrystals_kyber512_ref_dec(unsigned char *ss, const unsigned char *ct, const unsigned char *sk);

/* ---- Kyber768 (optional) ---- */

#ifdef DAP_KYBER768
#define KYBER768_PUBLICKEYBYTES  1184
#define KYBER768_SECRETKEYBYTES  2400
#define KYBER768_CIPHERTEXTBYTES 1088
#define KYBER768_BYTES             32

int pqcrystals_kyber768_ref_keypair(unsigned char *pk, unsigned char *sk);
int pqcrystals_kyber768_ref_enc(unsigned char *ct, unsigned char *ss, const unsigned char *pk);
int pqcrystals_kyber768_ref_dec(unsigned char *ss, const unsigned char *ct, const unsigned char *sk);
#endif

/* ---- Kyber1024 (optional) ---- */

#ifdef DAP_KYBER1024
#define KYBER1024_PUBLICKEYBYTES  1568
#define KYBER1024_SECRETKEYBYTES  3168
#define KYBER1024_CIPHERTEXTBYTES 1568
#define KYBER1024_BYTES             32

int pqcrystals_kyber1024_ref_keypair(unsigned char *pk, unsigned char *sk);
int pqcrystals_kyber1024_ref_enc(unsigned char *ct, unsigned char *ss, const unsigned char *pk);
int pqcrystals_kyber1024_ref_dec(unsigned char *ss, const unsigned char *ct, const unsigned char *sk);
#endif

/* ---- level helpers ---- */

typedef struct {
    int    (*keypair)(unsigned char *, unsigned char *);
    int    (*enc)(unsigned char *, unsigned char *, const unsigned char *);
    int    (*dec)(unsigned char *, const unsigned char *, const unsigned char *);
    size_t pk_bytes;
    size_t sk_bytes;
    size_t ct_bytes;
    size_t ss_bytes;
} mlkem_variant_t;

static const mlkem_variant_t s_kyber512 = {
    .keypair  = pqcrystals_kyber512_ref_keypair,
    .enc      = pqcrystals_kyber512_ref_enc,
    .dec      = pqcrystals_kyber512_ref_dec,
    .pk_bytes = KYBER512_PUBLICKEYBYTES,
    .sk_bytes = KYBER512_SECRETKEYBYTES,
    .ct_bytes = KYBER512_CIPHERTEXTBYTES,
    .ss_bytes = KYBER512_BYTES,
};

#ifdef DAP_KYBER768
static const mlkem_variant_t s_kyber768 = {
    .keypair  = pqcrystals_kyber768_ref_keypair,
    .enc      = pqcrystals_kyber768_ref_enc,
    .dec      = pqcrystals_kyber768_ref_dec,
    .pk_bytes = KYBER768_PUBLICKEYBYTES,
    .sk_bytes = KYBER768_SECRETKEYBYTES,
    .ct_bytes = KYBER768_CIPHERTEXTBYTES,
    .ss_bytes = KYBER768_BYTES,
};
#endif

#ifdef DAP_KYBER1024
static const mlkem_variant_t s_kyber1024 = {
    .keypair  = pqcrystals_kyber1024_ref_keypair,
    .enc      = pqcrystals_kyber1024_ref_enc,
    .dec      = pqcrystals_kyber1024_ref_dec,
    .pk_bytes = KYBER1024_PUBLICKEYBYTES,
    .sk_bytes = KYBER1024_SECRETKEYBYTES,
    .ct_bytes = KYBER1024_CIPHERTEXTBYTES,
    .ss_bytes = KYBER1024_BYTES,
};
#endif

static const mlkem_variant_t *s_get_variant(uint8_t a_level)
{
    switch (a_level) {
#ifdef DAP_KYBER1024
    case DAP_SIGN_PARAMS_SECURITY_5:
        return &s_kyber1024;
#endif
#ifdef DAP_KYBER768
    case DAP_SIGN_PARAMS_SECURITY_3:
        return &s_kyber768;
#endif
    default:
        return &s_kyber512;
    }
}

static const mlkem_variant_t *s_get_variant_by_sk_size(size_t a_sk_size)
{
#ifdef DAP_KYBER1024
    if (a_sk_size == KYBER1024_SECRETKEYBYTES)
        return &s_kyber1024;
#endif
#ifdef DAP_KYBER768
    if (a_sk_size == KYBER768_SECRETKEYBYTES)
        return &s_kyber768;
#endif
    return &s_kyber512;
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
    DAP_DEL_Z(a_key->shared_key);
    DAP_DEL_Z(a_key->pub_key_data);
    DAP_DEL_Z(a_key->_inheritor);
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
