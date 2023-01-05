
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

    key->pub_key_data_size = sizeof(falcon_public_key_t);
    key->priv_key_data_size = sizeof(falcon_private_key_t);
    key->pub_key_data = malloc(key->pub_key_data_size);
    key->priv_key_data = malloc(key->priv_key_data_size);


    uint8_t* privkey = calloc(1, FALCON_PRIVKEY_SIZE(logn));
    uint8_t* pubkey = calloc(1, FALCON_PUBKEY_SIZE(logn));

    falcon_private_key_t privateKey = {s_falcon_kind, s_falcon_sign_degree, s_falcon_type, privkey};
    falcon_public_key_t publicKey = {s_falcon_kind, s_falcon_sign_degree, s_falcon_type, pubkey};

    shake256_context rng;
    retcode = shake256_init_prng_from_system(&rng);
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to initialize PRNG");
        return;
    }
    retcode = falcon_keygen_make(
            &rng,
            logn,
            privateKey.data, FALCON_PRIVKEY_SIZE(logn),
            publicKey.data, FALCON_PUBKEY_SIZE(logn),
//            key->priv_key_data, key->priv_key_data_size,
//            key->pub_key_data, key->pub_key_data_size,
            tmp, FALCON_TMPSIZE_KEYGEN(logn)
            );
    if (retcode != 0) {
        falcon_private_and_public_keys_delete(&privateKey, &publicKey);
        log_it(L_ERROR, "Failed to generate falcon key");
        return;
    }

    memcpy(key->priv_key_data, &privateKey, sizeof(falcon_private_key_t));
    memcpy(key->pub_key_data, &publicKey, sizeof(falcon_public_key_t));

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
    // 4 bytes - degree of key
    // 4 bytes - kind of key
    // 4 bytes - type of key
    // n bytes - public key data

    uint64_t l_buflen =
            sizeof(uint64_t) +
            sizeof(s_falcon_sign_degree) +
            sizeof(s_falcon_kind) +
            sizeof(s_falcon_type) +
            FALCON_PUBKEY_SIZE(a_public_key->degree);

    uint8_t* l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    falcon_kind_t l_kind = a_public_key->kind;
    falcon_sign_degree_t l_degree = a_public_key->degree;
    falcon_sign_type_t l_type = a_public_key->type;
    memcpy(l_buf, &l_buflen, sizeof(uint64_t));
    memcpy(l_buf + sizeof(uint64_t), &l_degree, sizeof(s_falcon_sign_degree));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree), &l_kind, sizeof(s_falcon_kind));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree) + sizeof(s_falcon_kind), &l_type, sizeof(s_falcon_type));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree) + sizeof(s_falcon_kind) + sizeof(s_falcon_type), a_public_key->data, FALCON_PUBKEY_SIZE(a_public_key->degree));


    if(a_buflen_out) {
        *a_buflen_out = l_buflen;
    }

    return l_buf;
}

uint8_t* dap_enc_falcon_write_private_key(const falcon_private_key_t* a_private_key, size_t* a_buflen_out) {
    //Serialized key have format:
    // 8 first bytes - size of overall serialized key
    // 4 bytes - degree of key
    // 4 bytes - kind of key
    // 4 bytes - type of key
    // n bytes - private key data

    uint64_t l_buflen =
            sizeof(uint64_t) +
            sizeof(s_falcon_sign_degree) +
            sizeof(s_falcon_kind) +
            sizeof(s_falcon_type) +
            FALCON_PRIVKEY_SIZE(a_private_key->degree);

    uint8_t* l_buf = DAP_NEW_Z_SIZE(uint8_t, l_buflen);
    falcon_kind_t l_kind = a_private_key->kind;
    falcon_sign_degree_t l_degree = a_private_key->degree;
    falcon_sign_type_t l_type = a_private_key->type;
    memcpy(l_buf, &l_buflen, sizeof(uint64_t));
    memcpy(l_buf + sizeof(uint64_t), &l_degree, sizeof(s_falcon_sign_degree));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree), &l_kind, sizeof(s_falcon_kind));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree) + sizeof(s_falcon_kind), &l_type, sizeof(s_falcon_type));
    memcpy(l_buf + sizeof(uint64_t) + sizeof(s_falcon_sign_degree) + sizeof(s_falcon_kind) + sizeof(s_falcon_type), a_private_key->data, FALCON_PRIVKEY_SIZE(a_private_key->degree));

    if(a_buflen_out) {
        *a_buflen_out = l_buflen;
    }

    return l_buf;
}

void falcon_private_and_public_keys_delete(falcon_private_key_t* privateKey, falcon_public_key_t* publicKey) {
    falcon_private_key_delete(privateKey);
    falcon_public_key_delete(publicKey);
}

void falcon_private_key_delete(falcon_private_key_t* privateKey) {
    if (privateKey) {
        memset(privateKey->data, 0, FALCON_PRIVKEY_SIZE(privateKey->degree));
        DAP_DEL_Z(privateKey->data);
        privateKey->degree = 0;
        privateKey->type = 0;
        privateKey->kind = 0;
    }
}

void falcon_public_key_delete(falcon_public_key_t* publicKey) {
    if (publicKey) {
        memset(publicKey->data, 0, FALCON_PUBKEY_SIZE(publicKey->degree));
        DAP_DEL_Z(publicKey->data);
        publicKey->degree = 0;
        publicKey->type = 0;
        publicKey->kind = 0;
    }
}

