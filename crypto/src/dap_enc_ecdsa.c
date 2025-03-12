#include <assert.h>
#include <inttypes.h>
#include <string.h>
#include "dap_enc_ecdsa.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "hash_impl.h"
#include "dap_list.h"

#define LOG_TAG "dap_enc_sig_ecdsa"

static enum DAP_ECDSA_SIGN_SECURITY _ecdsa_type = ECDSA_MIN_SIZE; // by default
static _Thread_local ecdsa_context_t *s_context = NULL;  // local connection

static void s_context_destructor(UNUSED_ARG void *a_context) {
    secp256k1_context_destroy(s_context);
    log_it(L_DEBUG, "ECDSA context is destroyed @%p", s_context);
    s_context = NULL;
}

//void dap_enc_sig_ecdsa_set_type(enum DAP_ECDSA_SIGN_SECURITY type)
//{
//    _ecdsa_type = type;
//}

static ecdsa_context_t *s_context_create() 
{
     if (!s_context) {
        s_context = secp256k1_context_create(SECP256K1_CONTEXT_NONE);
        if (!s_context) {
            log_it(L_CRITICAL, "%s", c_error_memory_alloc);
            return NULL;
        }
        pthread_key_t s_context_destructor_key;
        pthread_key_create(&s_context_destructor_key, s_context_destructor);
        pthread_setspecific(s_context_destructor_key, (const void *)s_context);
        log_it(L_DEBUG, "ECDSA context is created @%p", s_context);
    }
    unsigned char l_random_seed[32];
    randombytes(l_random_seed, sizeof(l_random_seed));
    if (secp256k1_context_randomize(s_context, l_random_seed) != 1) {
        log_it(L_ERROR, "Error creating ECDSA context");
        secp256k1_context_destroy(s_context);
        s_context = NULL;
    }
    return s_context;
}

bool dap_enc_sig_ecdsa_hash_fast(const unsigned char *a_data, size_t a_data_size, dap_hash_fast_t *a_out)
{
    secp256k1_sha256 l_hasher;
    secp256k1_sha256_initialize(&l_hasher);
    secp256k1_sha256_write(&l_hasher, a_data, a_data_size);
    secp256k1_sha256_finalize(&l_hasher, (unsigned char *)a_out);
    return true;
}

void dap_enc_sig_ecdsa_key_new(dap_enc_key_t *a_key) {
    *a_key = (dap_enc_key_t) {
        .type       = DAP_ENC_KEY_TYPE_SIG_ECDSA,
        .sign_get   = dap_enc_sig_ecdsa_get_sign,
        .sign_verify= dap_enc_sig_ecdsa_verify_sign
    };
}

void dap_enc_sig_ecdsa_deinit() {
    if (s_context) {
        s_context_destructor(NULL);
    }
}



void dap_enc_sig_ecdsa_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, const void *a_seed, size_t a_seed_size,
        UNUSED_ARG size_t key_size)
{
// sanity check
    dap_return_if_pass(!a_key);
// memory alloc
    a_key->priv_key_data = DAP_NEW_Z_RET_IF_FAIL(ecdsa_private_key_t);
    a_key->pub_key_data = DAP_NEW_Z_RET_IF_FAIL(ecdsa_public_key_t, a_key->priv_key_data);
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx) {
        log_it(L_ERROR, "Error creating ECDSA context in generating key pair");
        DAP_DEL_Z(a_key->priv_key_data);
        DAP_DEL_Z(a_key->pub_key_data);
        return;
    }
// keypair generate
    if(a_seed && a_seed_size > 0) {
        dap_enc_sig_ecdsa_hash_fast((const unsigned char *)a_seed, a_seed_size, (dap_hash_fast_t *)a_key->priv_key_data);
        if (!secp256k1_ec_seckey_verify(l_ctx, (const unsigned char*)a_key->priv_key_data)) {
            log_it(L_ERROR, "Error verify ECDSA private key");
            DAP_DEL_Z(a_key->priv_key_data);
            DAP_DEL_Z(a_key->pub_key_data);
            goto clean_and_ret;
        }
    } else {
        do {
            randombytes(a_key->priv_key_data, sizeof(ecdsa_private_key_t));
        } while ( !secp256k1_ec_seckey_verify(l_ctx, (const unsigned char*)a_key->priv_key_data) );
    }

    //not sure we need this for ECDSA
    //dap_enc_sig_ecdsa_set_type(ECDSA_MAX_SPEED)
   
    if(secp256k1_ec_pubkey_create(l_ctx, (ecdsa_public_key_t*)a_key->pub_key_data, (const unsigned char*)a_key->priv_key_data) != 1) {
        log_it(L_CRITICAL, "Error generating ECDSA key pair");
        DAP_DEL_Z(a_key->priv_key_data);
        DAP_DEL_Z(a_key->pub_key_data);
        goto clean_and_ret;
    }
    a_key->priv_key_data_size = sizeof(ecdsa_private_key_t);
    a_key->pub_key_data_size  = sizeof(ecdsa_public_key_t);
clean_and_ret:
    return;
}

int dap_enc_sig_ecdsa_get_sign(struct dap_enc_key *l_key, const void *a_msg, const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
// sanity check
    dap_return_val_if_pass(!l_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(ecdsa_signature_t), -2, "Invalid ecdsa signature size");
    dap_return_val_if_pass_err(l_key->priv_key_data_size != sizeof(ecdsa_private_key_t), -3, "Invalid ecdsa private key size");
// msg hashing
    byte_t l_msghash[32] = { '\0' };
    dap_enc_sig_ecdsa_hash_fast(a_msg, a_msg_size, (dap_hash_fast_t *)l_msghash);
// context create
    int l_ret = 0;
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx || secp256k1_ecdsa_sign(l_ctx, (ecdsa_signature_t *)a_sig, l_msghash, l_key->priv_key_data, NULL, NULL) != 1) {
        log_it(L_ERROR, "Failed to sign message");
        l_ret = -4;
    }
    return l_ret;
}

int dap_enc_sig_ecdsa_verify_sign(struct dap_enc_key *l_key, const void *a_msg, const size_t a_msg_size, void *a_sig, const size_t a_sig_size)
{
// sanity check
    dap_return_val_if_pass(!l_key, -1);
    dap_return_val_if_pass_err(a_sig_size != sizeof(ecdsa_signature_t), -2, "Invalid ecdsa signature size");
    dap_return_val_if_pass_err(l_key->pub_key_data_size != sizeof(ecdsa_public_key_t), -3, "Invalid ecdsa public key size");
// msg hashing
    byte_t l_msghash[32] = { '\0' };
    dap_enc_sig_ecdsa_hash_fast(a_msg, a_msg_size, (dap_hash_fast_t *)l_msghash);
// context create
    int l_ret = 0;
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx || secp256k1_ecdsa_verify(l_ctx, (const ecdsa_signature_t*)a_sig, l_msghash, (ecdsa_public_key_t *)l_key->pub_key_data) != 1) {
        log_it(L_ERROR, "Failed to verify signature");
        l_ret = -4;
    }
    return l_ret;
}

uint8_t *dap_enc_sig_ecdsa_write_public_key(const void *a_public_key, size_t *a_buflen_out)
{
    dap_return_val_if_pass(!a_public_key, NULL);
    byte_t *l_buf = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, ECDSA_PKEY_SERIALIZED_SIZE, NULL);

    ecdsa_context_t *l_ctx = s_context_create();

    size_t l_len = ECDSA_PKEY_SERIALIZED_SIZE;
    if (
        !l_ctx ||
        secp256k1_ec_pubkey_serialize( l_ctx, l_buf, &l_len, (const ecdsa_public_key_t*)a_public_key, SECP256K1_EC_UNCOMPRESSED) != 1 ||
        l_len != ECDSA_PKEY_SERIALIZED_SIZE
    ) {
        log_it(L_CRITICAL, "Failed to serialize pkey");
        DAP_DEL_Z(l_buf);
        goto clean_and_ret;
    }
    if (a_buflen_out)
        *a_buflen_out = ECDSA_PKEY_SERIALIZED_SIZE;
clean_and_ret:
    return l_buf;
}

void *dap_enc_sig_ecdsa_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
// sanity check
    dap_return_val_if_pass(!a_buf || a_buflen != ECDSA_PKEY_SERIALIZED_SIZE, NULL);
// memory alloc
    ecdsa_public_key_t *l_public_key = DAP_NEW_Z_RET_VAL_IF_FAIL(ecdsa_public_key_t, NULL);
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx || secp256k1_ec_pubkey_parse(l_ctx, l_public_key, a_buf, a_buflen ) != 1) {
        log_it(L_CRITICAL, "Failed to deserialize pkey");
        DAP_DELETE(l_public_key);
    }
    return l_public_key;
}

uint8_t *dap_enc_sig_ecdsa_write_signature(const void *a_sign, size_t *a_sign_len)
{
    dap_return_val_if_pass(!a_sign || !a_sign_len, NULL);
    byte_t *l_ret = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, sizeof(ecdsa_signature_t), NULL);
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx || secp256k1_ecdsa_signature_serialize_compact(l_ctx, l_ret, (const ecdsa_signature_t*)a_sign) != 1) {
        log_it(L_ERROR, "Failed to serialize sign");
        DAP_DEL_Z(l_ret);  
    }
    return l_ret;
}

void *dap_enc_sig_ecdsa_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    dap_return_val_if_pass(!a_buf || a_buflen != sizeof(ecdsa_signature_t), NULL);
    ecdsa_signature_t *l_ret = DAP_NEW_Z_RET_VAL_IF_FAIL(ecdsa_signature_t, NULL);
    ecdsa_context_t *l_ctx = s_context_create();
    if (!l_ctx || secp256k1_ecdsa_signature_parse_compact(l_ctx, l_ret, a_buf) != 1) {
        log_it(L_ERROR, "Failed to deserialize sign");
        DAP_DEL_Z(l_ret);
    }
    return l_ret;
}

void dap_enc_sig_ecdsa_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    memset_safe(((ecdsa_signature_t *)a_sig)->data, 0, ECDSA_SIG_SIZE);
}

void dap_enc_sig_ecdsa_private_key_delete(void *a_private_key) {
    dap_return_if_pass(!a_private_key);
    memset_safe( ((ecdsa_private_key_t*)a_private_key)->data, 0, ECDSA_PRIVATE_KEY_SIZE);
    DAP_DELETE(a_private_key);
}

void dap_enc_sig_ecdsa_public_key_delete(void *a_public_key) {
    dap_return_if_pass(!a_public_key);
    memset_safe( ((ecdsa_public_key_t*)a_public_key)->data, 0, ECDSA_PUBLIC_KEY_SIZE);
    DAP_DELETE(a_public_key);
}

void dap_enc_sig_ecdsa_private_and_public_keys_delete(dap_enc_key_t* a_key) {
        dap_enc_sig_ecdsa_private_key_delete(a_key->priv_key_data);
        dap_enc_sig_ecdsa_public_key_delete(a_key->pub_key_data);
        a_key->pub_key_data = NULL;
        a_key->priv_key_data = NULL;
        a_key->pub_key_data_size = 0;
        a_key->priv_key_data_size = 0;
}
