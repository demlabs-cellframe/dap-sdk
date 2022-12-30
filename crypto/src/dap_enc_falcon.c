
#include "dilithium_params.h"
#include "dap_enc_falcon.h"
#include "falcon.h"

#define LOG_TAG "dap_enc_sig_falcon"

static falcon_sign_degree_t s_falcon_sign_degree = FALCON_512;
static falcon_kind_t s_falcon_kind = FALCON_COMPRESSED;
static falcon_sign_type_t s_falcon_type = FALCON_DYNAMIC;


void dap_enc_sig_falcon_set_degree(enum DAP_FALCON_SIGN_DEGREE a_falcon_sign_degree)
{
    if (a_falcon_sign_degree != FALCON_512 && a_falcon_sign_degree != FALCON_1024) {
        log_it(L_ERROR, "Wrong falcon degree");
        return;
    }
    s_falcon_sign_degree = a_falcon_sign_degree;
}

void dap_enc_sig_falcon_set_kind(falcon_kind_t a_falcon_kind)
{
    if (a_falcon_kind != FALCON_COMPRESSED && a_falcon_kind != FALCON_PADDED && a_falcon_kind != FALCON_CT) {
        log_it(L_ERROR, "Wrong falcon kind");
        return;
    }
    s_falcon_kind = a_falcon_kind;
}

void dap_enc_sig_falcon_set_type(falcon_sign_type_t a_falcon_type)
{
    if (a_falcon_type != FALCON_DYNAMIC && a_falcon_type != FALCON_TREE) {
        log_it(L_ERROR, "Wrong falcon type");
        return;
    }
    s_falcon_type = a_falcon_type;
}


void dap_enc_sig_falcon_key_new(struct dap_enc_key *key) {
    key->type = DAP_ENC_KEY_TYPE_SIG_FALCON;
    key->enc = NULL;
    key->enc_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_get_sign;
    key->dec_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_verify_sign;
}

void dap_enc_sig_falcon_key_new_generate(struct dap_enc_key *key, const void *kex_buf, size_t kex_size,
        const void* seed, size_t seed_size, size_t key_size) {


    key->type = DAP_ENC_KEY_TYPE_SIG_FALCON;
    key->enc = NULL;
    key->enc_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_get_sign;
    key->dec_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_verify_sign;


    int retcode = 0;
    unsigned int logn = s_falcon_sign_degree;
    size_t tmp[FALCON_TMPSIZE_KEYGEN(logn)];

    key->pub_key_data_size = FALCON_PUBKEY_SIZE(logn);
    key->priv_key_data_size = FALCON_PRIVKEY_SIZE(logn);
    key->pub_key_data = calloc(key->pub_key_data_size, sizeof(uint8_t));
    key->priv_key_data = calloc(key->priv_key_data_size, sizeof(uint8_t));


    shake256_context rng;
    retcode = shake256_init_prng_from_system(&rng);
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to initialize PRNG");
        return;
    }

    retcode = falcon_keygen_make(
            &rng,
            logn,
            &key->priv_key_data, key->priv_key_data_size,
            &key->pub_key_data, key->pub_key_data_size,
            tmp, FALCON_TMPSIZE_KEYGEN(logn));
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to generate falcon key");
        return;
    }
}

size_t dap_enc_sig_falcon_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size) {
    //todo: need to use shared shake256 context

    int retcode;
    int logn = s_falcon_sign_degree;

    shake256_context rng;
    retcode = shake256_init_prng_from_system(&rng);
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to initialize PRNG");
        return retcode;
    }

    size_t tmpsize = (s_falcon_type == FALCON_DYNAMIC ? FALCON_TMPSIZE_SIGNDYN(logn) : FALCON_TMPSIZE_SIGNTREE(logn));
    uint8_t tmp[tmpsize];

    //TODO: get sig_type from anywhere
    retcode = falcon_sign_dyn(
            &rng,
            signature, &signature_size, s_falcon_kind,
            key->priv_key_data, key->priv_key_data_size,
            msg, msg_size,
            tmp, tmpsize
            );
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to sign message");
        return retcode;
    }
}

size_t dap_enc_sig_falcon_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature,
                                      const size_t signature_size) {
    int retcode;
    int logn = s_falcon_sign_degree;

    uint8_t tmp[FALCON_TMPSIZE_VERIFY(logn)];

    retcode = falcon_verify(
            signature, signature_size, s_falcon_kind,
            key->pub_key_data, key->pub_key_data_size,
            msg, msg_size,
            tmp, FALCON_TMPSIZE_VERIFY(logn)
            );
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to verify signature");
        return retcode;
    }
}

void dap_enc_sig_falcon_key_delete(struct dap_enc_key *key) {

    if (key->priv_key_data) {
        memset(key->priv_key_data, 0, key->priv_key_data_size);
        DAP_DEL_Z(key->priv_key_data);
    }
    if (key->pub_key_data) {
        memset(key->pub_key_data, 0, key->pub_key_data_size);
        DAP_DEL_Z(key->pub_key_data);
    }
}

// Serialize a public key into a buffer.
uint8_t* dap_enc_falcon_write_public_key(const falcon_public_key_t* a_public_key, size_t* a_buflen_out) {
    //Serialized key have format:
    // 8 first bytes - size of overall serialized key
    // 4 bytes - kind of key
    // n bytes - public key data

    size_t buflen = 8 + 4 + FALCON_PUBKEY_SIZE(a_public_key->);
}