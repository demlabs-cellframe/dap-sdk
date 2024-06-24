#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "dap_enc_ecdsa.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "hash_impl.h"

#define LOG_TAG "dap_enc_sig_ecdsa"



static enum DAP_ECDSA_SIGN_SECURITY _ecdsa_type = ECDSA_MIN_SIZE; // by default


//void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type)
//{
//    _ecdsa_type = type;
//}

void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key) {
    *a_key = (dap_enc_key_t) {
        .type       = DAP_ENC_KEY_TYPE_SIG_ECDSA,
        .sign_get   = dap_enc_sig_ecdsa_get_sign,
        .sign_verify= dap_enc_sig_ecdsa_verify_sign
    };
}



void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, const void *a_seed, size_t a_seed_size,
        UNUSED_ARG size_t key_size)
{
// sanity check
    dap_return_if_pass(!a_key);
// memory alloc
    DAP_NEW_Z_RET(a_key->priv_key_data, ecdsa_private_key_t, NULL);
    DAP_NEW_Z_RET(a_key->pub_key_data, ecdsa_public_key_t, a_key->priv_key_data);
    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
// keypair generate
    if(a_seed && a_seed_size > 0) {
        SHA3_256((unsigned char *)a_key->priv_key_data, (const unsigned char *)a_seed, a_seed_size);
    } else {
        do {
            randombytes(a_key->priv_key_data, sizeof(ecdsa_private_key_t));
        } while ( !secp256k1_ec_seckey_verify(l_ctx, (const unsigned char*)a_key->priv_key_data) );
    }

    //not sure we need this for ECDSA
    //dap_enc_sig_ecdsa_set_type(ECDSA_MAX_SPEED)
   
    if(
        secp256k1_context_randomize(l_ctx, a_key->priv_key_data) != 1 ||
        secp256k1_ec_pubkey_create(l_ctx, (ecdsa_public_key_t*)a_key->pub_key_data, (const unsigned char*)a_key->priv_key_data) != 1
    ) {
        log_it(L_CRITICAL, "Error generating ECDSA key pair");
        DAP_DEL_MULTY(a_key->priv_key_data, a_key->pub_key_data);
        goto clean_and_ret;
    }
    a_key->priv_key_data_size = sizeof(ecdsa_private_key_t);
    a_key->pub_key_data_size  = sizeof(ecdsa_public_key_t);
clean_and_ret:
    secp256k1_context_destroy(l_ctx);
}

int dap_enc_sig_ecdsa_get_sign(struct dap_enc_key *l_key, const void *a_msg, const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
// sanity check
    dap_return_val_if_pass(!l_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(ecdsa_signature_t), -2, "Invalid ecdsa signature size");
    dap_return_val_if_pass_err(l_key->priv_key_data_size != sizeof(ecdsa_private_key_t), -3, "Invalid ecdsa private key size");
// msg hashing
    secp256k1_sha256 l_hasher;
    byte_t l_msghash[32] = { '\0' };
    secp256k1_sha256_initialize(&l_hasher);
    secp256k1_sha256_write(&l_hasher, a_msg, a_msg_size);
    secp256k1_sha256_finalize(&l_hasher, l_msghash);
// context create
    int l_ret = 0;
    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_SIGN);
    if ( 
        !l_ctx || 
        secp256k1_context_randomize(l_ctx, l_key->priv_key_data) != 1 ||
        secp256k1_ecdsa_sign(l_ctx, (ecdsa_signature_t *)a_sig, l_msghash, l_key->priv_key_data, NULL, NULL) != 1
    ) {
        log_it(L_ERROR, "Failed to sign message");
        l_ret = -4;
    }
    secp256k1_context_destroy(l_ctx);
    return l_ret;
}

int dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key *l_key, const void *a_msg, const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
// sanity check
    dap_return_val_if_pass(!l_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(ecdsa_signature_t), -2, "Invalid ecdsa signature size");
    dap_return_val_if_pass_err(l_key->pub_key_data_size != sizeof(ecdsa_public_key_t), -3, "Invalid ecdsa public key size");
// msg hashing
    secp256k1_sha256 l_hasher;
    byte_t l_msghash[32] = { '\0' };
    secp256k1_sha256_initialize(&l_hasher);
    secp256k1_sha256_write(&l_hasher, a_msg, a_msg_size);
    secp256k1_sha256_finalize(&l_hasher, l_msghash);
// context create
    int l_ret = 0;
    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_VERIFY);
    if (
        !l_ctx ||
        secp256k1_context_randomize(l_ctx, l_key->priv_key_data) != 1 ||
        secp256k1_ecdsa_verify(l_ctx, (const ecdsa_signature_t*)a_sig, l_msghash, (ecdsa_public_key_t *)l_key->pub_key_data) != 1
    ) {
        log_it(L_ERROR, "Failed to verify signature");
        l_ret = -4;
    }
    secp256k1_context_destroy(l_ctx);
    return l_ret;
}

uint8_t* dap_enc_sig_ecdsa_write_public_key(const void *a_key, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_key, NULL);
    dap_enc_key_t *l_key = (dap_enc_key_t*)a_key;
    byte_t *l_buf = NULL;
    DAP_NEW_Z_SIZE_RET_VAL(l_buf, byte_t, ECDSA_PKEY_SERIALIZED_SIZE, NULL, NULL);

    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);

    size_t l_len = ECDSA_PKEY_SERIALIZED_SIZE;
    if (
        !l_ctx ||
        secp256k1_context_randomize(l_ctx, l_key->priv_key_data) != 1 ||
        secp256k1_ec_pubkey_serialize( l_ctx, l_buf, &l_len, (const ecdsa_public_key_t*)l_key->pub_key_data, SECP256K1_EC_UNCOMPRESSED) != 1 ||
        l_len != ECDSA_PKEY_SERIALIZED_SIZE
    ) {
        log_it(L_CRITICAL, "Failed to serialize pkey");
        DAP_DEL_Z(l_buf);
        goto clean_and_ret;
    }
    if (a_buflen_out)
        *a_buflen_out = ECDSA_PKEY_SERIALIZED_SIZE;
clean_and_ret:
    secp256k1_context_destroy(l_ctx);
    return l_buf;
}

void *dap_enc_sig_ecdsa_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen != ECDSA_PKEY_SERIALIZED_SIZE, NULL);
// memory alloc
    ecdsa_public_key_t *l_public_key = NULL;
    DAP_NEW_Z_RET_VAL(l_public_key, ecdsa_public_key_t, NULL, NULL);
    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);  // used only to warning fix
    if (secp256k1_ec_pubkey_parse(l_ctx, l_public_key, a_buf, a_buflen ) != 1) {
        log_it(L_CRITICAL, "Failed to deserialize pkey");
        DAP_DEL_Z(l_public_key);
    }
    secp256k1_context_destroy(l_ctx);
    return l_public_key;
}

uint8_t *dap_enc_sig_ecdsa_write_signature(const void *a_sign, dap_enc_key_t *a_key, size_t *a_sign_len)
{
    dap_return_val_if_fail_err(a_sign && a_key && a_sign_len, NULL, "Invalid args");
    byte_t *l_buf = DAP_NEW_SIZE(byte_t, sizeof(ecdsa_signature_t));
    if (!l_buf) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    int retcode = secp256k1_context_randomize(l_ctx, a_key->priv_key_data);
    assert(retcode);

    uint8_t *l_ret = secp256k1_ecdsa_signature_serialize_compact( l_ctx,
                                                        l_buf, (const ecdsa_signature_t*)a_sign ) == 1
        ? l_buf : ( DAP_DELETE(l_buf), log_it(L_ERROR, "Failed to serialize sign"), NULL );  
    secp256k1_context_destroy(l_ctx);
    return l_ret;
}

void* dap_enc_sig_ecdsa_read_signature(const uint8_t* a_buf, dap_enc_key_t *a_key, size_t a_buflen)
{
    dap_return_val_if_fail_err(a_buf && a_key && a_buflen == sizeof(ecdsa_signature_t), NULL, "Invalid args");
    ecdsa_signature_t *l_ret = DAP_NEW(ecdsa_signature_t);
    if (!l_ret) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    ecdsa_context_t *l_ctx = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
    int retcode = secp256k1_context_randomize(l_ctx, a_key->priv_key_data);
    assert(retcode);

    void *l_ret2 = secp256k1_ecdsa_signature_parse_compact(l_ctx, l_ret, a_buf) == 1
        ? l_ret : ( DAP_DELETE(l_ret), log_it(L_ERROR, "Failed to deserialize sign"), NULL );
    secp256k1_context_destroy(l_ctx);
    return l_ret2;
}

void dap_enc_sig_ecdsa_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    memset_safe(((ecdsa_signature_t *)a_sig)->data, 0, ECDSA_SIG_SIZE);
}

void dap_enc_sig_ecdsa_private_key_delete(void *a_private_key) {
    dap_return_if_pass(!a_private_key);
    memset_safe( ((ecdsa_private_key_t*)a_private_key)->data, 0, ECDSA_PRIVATE_KEY_SIZE);
}

void dap_enc_sig_ecdsa_public_key_delete(void *a_public_key) {
    dap_return_if_pass(!a_public_key);
    memset_safe( ((ecdsa_public_key_t*)a_public_key)->data, 0, ECDSA_PUBLIC_KEY_SIZE);
}

void dap_enc_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t* a_key) {
        dap_enc_sig_ecdsa_private_key_delete(a_key->priv_key_data);
        dap_enc_sig_ecdsa_public_key_delete(a_key->pub_key_data);
}