#include "dap_enc_falcon.h"
#include "falcon.h"

#define LOG_TAG "dap_enc_sig_falcon"

static falcon_sign_degree_t s_falcon_sign_degree = FALCON_512;
static falcon_kind_t s_falcon_kind = FALCON_COMPRESSED;
static falcon_sign_type_t s_falcon_type = FALCON_DYNAMIC;


void dap_enc_sig_falcon_set_degree(falcon_sign_degree_t a_falcon_sign_degree)
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


void dap_enc_sig_falcon_key_new(dap_enc_key_t *a_key) {
    a_key->type = DAP_ENC_KEY_TYPE_SIG_FALCON;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_falcon_get_sign;
    a_key->sign_verify = dap_enc_sig_falcon_verify_sign;
}

void dap_enc_sig_falcon_key_new_generate(dap_enc_key_t *key, const void *kex_buf, size_t kex_size,
        const void* seed, size_t seed_size, size_t key_size) {

    dap_enc_sig_falcon_key_new(key);

    int retcode = 0;
    unsigned int logn = s_falcon_sign_degree;
    size_t tmp[FALCON_TMPSIZE_KEYGEN(logn)];

    key->pub_key_data_size = sizeof(falcon_public_key_t);
    key->priv_key_data_size = sizeof(falcon_private_key_t);
    key->pub_key_data = malloc(key->pub_key_data_size);
    if (!key->pub_key_data) {
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }
    key->priv_key_data = malloc(key->priv_key_data_size);
    if (!key->priv_key_data) {
        log_it(L_CRITICAL, "Memory allocation error");
        return;
    }

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

int dap_enc_sig_falcon_get_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void* a_sig, const size_t a_signature_size)
{

    if (a_signature_size != sizeof(falcon_signature_t)) {
        log_it(L_ERROR, "Invalid falcon signature size");
        return -10;
    }

    if (a_key->priv_key_data_size != sizeof(falcon_private_key_t)) {
        log_it(L_ERROR, "Invalid falcon key");
        return -11;
    }

    int l_ret = 0;
    //todo: do we need to use shared shake256 context?
    shake256_context l_rng;
    if ((l_ret = shake256_init_prng_from_system(&l_rng))) {
        log_it(L_ERROR, "Failed to initialize PRNG");
        return l_ret;
    }

    falcon_private_key_t *privateKey = a_key->priv_key_data;

    size_t tmpsize = privateKey->type == FALCON_DYNAMIC ?
                FALCON_TMPSIZE_SIGNDYN(privateKey->degree) :
                FALCON_TMPSIZE_SIGNTREE(privateKey->degree);

    uint8_t tmp[tmpsize];

    falcon_signature_t *l_sig = a_sig;
    l_sig->degree = privateKey->degree;
    l_sig->kind = privateKey->kind;
    l_sig->type = privateKey->type;
    size_t l_sig_len = 0;
    switch (privateKey->kind) {
        case FALCON_COMPRESSED:
            l_sig_len = FALCON_SIG_COMPRESSED_MAXSIZE(privateKey->degree);
            break;
        case FALCON_PADDED:
            l_sig_len = FALCON_SIG_PADDED_SIZE(privateKey->degree);
            break;
        case FALCON_CT:
            l_sig_len = FALCON_SIG_CT_SIZE(privateKey->degree);
        default:
            break;
    }

    if (l_sig_len)
        DAP_NEW_Z_SIZE_RET_VAL(l_sig->sig_data, byte_t, l_sig_len, -1, NULL);

    l_ret = falcon_sign_dyn(
            &l_rng,
            l_sig->sig_data, &l_sig_len, privateKey->kind,
            privateKey->data, FALCON_PRIVKEY_SIZE(privateKey->degree),
            a_msg, a_msg_size,
            tmp, tmpsize
            );
    l_sig->sig_len = l_sig_len;

    if (l_ret)
        log_it(L_ERROR, "Failed to sign message");
    return l_ret;
}

int dap_enc_sig_falcon_verify_sign(dap_enc_key_t *a_key, const void *a_msg, const size_t a_msg_size, void *a_sig,
                                      const size_t a_sig_size)
{

    if (a_key->pub_key_data_size != sizeof(falcon_private_key_t)) {
        log_it(L_ERROR, "Invalid falcon key");
        return -11;
    }
    falcon_public_key_t *l_pkey = a_key->pub_key_data;
    int l_logn = l_pkey->degree;

    uint8_t l_tmp[FALCON_TMPSIZE_VERIFY(l_logn)];

    falcon_signature_t *l_sig = a_sig;
    if (sizeof(falcon_signature_t) != a_sig_size ||
            l_sig->degree != l_pkey->degree ||
            l_sig->kind != l_pkey->kind ||
            l_sig->type != l_pkey->type)
        return -1;

    int l_ret = falcon_verify(
            l_sig->sig_data, l_sig->sig_len, l_pkey->kind,
            l_pkey->data, FALCON_PUBKEY_SIZE(l_pkey->degree),
            a_msg, a_msg_size,
            l_tmp, FALCON_TMPSIZE_VERIFY(l_logn)
            );
    if (l_ret)
        log_it(L_ERROR, "Failed to verify signature");
    return l_ret;
}

void dap_enc_sig_falcon_key_delete(dap_enc_key_t *key) {

    falcon_private_and_public_keys_delete((falcon_private_key_t *)key->priv_key_data, (falcon_public_key_t *)key->pub_key_data);

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
uint8_t *dap_enc_falcon_write_public_key(const void *a_public_key, size_t *a_buflen_out) {
    //Serialized key have format:
    // 8 first bytes - size of overall serialized key
    // 4 bytes - degree of key
    // 4 bytes - kind of key
    // 4 bytes - type of key
    // n bytes - public key data
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_public_key, NULL);
// func work
    falcon_public_key_t *l_public_key = (falcon_public_key_t *)a_public_key;
    uint64_t l_buflen =
            sizeof(uint64_t) +
            sizeof(uint32_t) * 3 +
            FALCON_PUBKEY_SIZE(l_public_key->degree
    );
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 10,
        &l_buflen, sizeof(uint64_t),
        &l_public_key->degree, sizeof(uint32_t),
        &l_public_key->kind, sizeof(uint32_t),
        &l_public_key->type, sizeof(uint32_t),
        l_public_key->data, FALCON_PUBKEY_SIZE(l_public_key->degree)
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

uint8_t *dap_enc_falcon_write_private_key(const void *a_private_key, size_t *a_buflen_out) {
    //Serialized key have format:
    // 8 first bytes - size of overall serialized key
    // 4 bytes - degree of key
    // 4 bytes - kind of key
    // 4 bytes - type of key
    // n bytes - private key data
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_private_key, NULL);
// func work
    falcon_private_key_t *l_private_key = (falcon_private_key_t *)a_private_key;
    uint64_t l_buflen =
            sizeof(uint64_t) +
            sizeof(uint32_t) * 3 +
            FALCON_PRIVKEY_SIZE(l_private_key->degree);
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 10,
        &l_buflen, sizeof(uint64_t),
        &l_private_key->degree, sizeof(uint32_t),
        &l_private_key->kind, sizeof(uint32_t),
        &l_private_key->type, sizeof(uint32_t),
        l_private_key->data, FALCON_PRIVKEY_SIZE(l_private_key->degree)
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

falcon_private_key_t* dap_enc_falcon_read_private_key(const uint8_t *a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_private_key() a_buf is NULL");
        return NULL;
    }

    if (a_buflen < sizeof(uint32_t) * 3) {
        log_it(L_ERROR, "::read_private_key() a_buflen %"DAP_UINT64_FORMAT_U" is smaller than first four fields(%zu)", a_buflen, sizeof(uint32_t) * 3);
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint32_t l_degree = 0;
    uint32_t l_kind = 0;
    uint32_t l_type = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen < l_buflen) {
        log_it(L_ERROR, "::read_private_key() a_buflen %"DAP_UINT64_FORMAT_U" is less than l_buflen %"DAP_UINT64_FORMAT_U, a_buflen, l_buflen);
        return NULL;
    }

    l_degree = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_degree != FALCON_512 && l_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_private_key() degree %ul is not supported", l_degree);
        return NULL;
    }
    if (l_buflen != (sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PRIVKEY_SIZE(l_degree))) {
        log_it(L_ERROR, "::read_private_key() buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
               a_buflen, sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PRIVKEY_SIZE(l_degree));
        return NULL;
    }

    l_kind = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_kind != FALCON_COMPRESSED && l_kind != FALCON_PADDED && l_kind != FALCON_CT) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_private_key() kind %ul is not supported", l_kind);
        return NULL;
    }

    l_type = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_type != FALCON_DYNAMIC && l_type != FALCON_TREE) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_private_key() type %ul is not supported", l_type);
        return NULL;
    }

    falcon_private_key_t* l_private_key = DAP_NEW_Z(falcon_private_key_t);
    if (!l_private_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_private_key->degree = l_degree;
    l_private_key->kind = l_kind;
    l_private_key->type = l_type;
    l_private_key->data = DAP_NEW_Z_SIZE(uint8_t, FALCON_PRIVKEY_SIZE(l_degree));
    if (!l_private_key->data) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_private_key);
        return NULL;
    }
    memcpy(l_private_key->data, l_ptr, FALCON_PRIVKEY_SIZE(l_degree));
    assert(l_ptr + FALCON_PRIVKEY_SIZE(l_degree) - a_buf == (int64_t)l_buflen);

    return l_private_key;
}

falcon_public_key_t* dap_enc_falcon_read_public_key(const uint8_t* a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_public_key() a_buf is NULL");
        return NULL;
    }

    if (a_buflen < sizeof(uint32_t) * 3) {
        log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is smaller than first four fields(%zu)", a_buflen, sizeof(uint32_t) * 3);
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint32_t l_degree = 0;
    uint32_t l_kind = 0;
    uint32_t l_type = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen < l_buflen) {
        log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is less than l_buflen %"DAP_UINT64_FORMAT_U, a_buflen, l_buflen);
        return NULL;
    }

    l_degree = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_degree != FALCON_512 && l_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_public_key() l_degree %ul is not supported", l_degree);
        return NULL;
    }
    if (l_buflen != (sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PUBKEY_SIZE(l_degree))) {
        log_it(L_ERROR, "::read_public_key() a_buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
                        a_buflen, (sizeof(uint64_t) + sizeof(uint32_t) * 3 + FALCON_PUBKEY_SIZE(l_degree)));
        return NULL;
    }

    l_kind = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_kind != FALCON_COMPRESSED && l_kind != FALCON_PADDED && l_kind != FALCON_CT) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_public_key() l_kind %ul is not supported", l_kind);
        return NULL;
    }

    l_type = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_type != FALCON_DYNAMIC && l_type != FALCON_TREE) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_public_key() l_type %ul is not supported", l_type);
        return NULL;
    }

    falcon_public_key_t* l_public_key = DAP_NEW_Z(falcon_public_key_t);
    if (!l_public_key) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }
    l_public_key->degree = l_degree;
    l_public_key->kind = l_kind;
    l_public_key->type = l_type;
    l_public_key->data = DAP_NEW_Z_SIZE(uint8_t, FALCON_PUBKEY_SIZE(l_degree));
    if (!l_public_key->data) {
        log_it(L_CRITICAL, "Memory allocation error");
        DAP_DEL_Z(l_public_key);
        return NULL;
    }
    memcpy(l_public_key->data, l_ptr, FALCON_PUBKEY_SIZE(l_degree));
    assert(l_ptr + FALCON_PUBKEY_SIZE(l_degree) - a_buf == (int64_t)l_buflen);

    return l_public_key;
}

uint8_t *dap_enc_falcon_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    if (!a_sign) {
        log_it(L_ERROR, "::write_signature() a_sign is NULL");
        return NULL;
    }
    falcon_signature_t *l_sign = (falcon_signature_t*)a_sign;
// func work
    size_t l_buflen = sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sign->sig_len;
    uint8_t *l_buf = dap_serialize_multy(NULL, l_buflen, 12,
        &l_buflen, sizeof(uint64_t),
        &l_sign->degree, sizeof(uint32_t),
        &l_sign->kind, sizeof(uint32_t),
        &l_sign->type, sizeof(uint32_t),
        &l_sign->sig_len, sizeof(uint64_t),
        l_sign->sig_data, l_sign->sig_len
    );
// out work
    a_buflen_out ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}
falcon_signature_t* dap_enc_falcon_read_signature(const uint8_t* a_buf, size_t a_buflen) {
    if (!a_buf) {
        log_it(L_ERROR, "::read_signature() a_buf is NULL");
        return NULL;
    }

    uint64_t l_buflen = 0;
    uint32_t l_degree = 0;
    uint32_t l_kind = 0;
    uint32_t l_type = 0;
    uint64_t l_sig_len = 0;
    uint8_t *l_ptr = (uint8_t *)a_buf;

    l_buflen = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (a_buflen != l_buflen) {
        log_it(L_ERROR, "::read_signature() a_buflen %zu is not equal to sign size (%"DAP_UINT64_FORMAT_U")",
                        a_buflen, l_buflen);
        return NULL;
    }

    l_degree = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_degree != FALCON_512 && l_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "::read_signature() l_degree %ul is not supported", l_degree);
        return NULL;
    }

    l_kind = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_kind != FALCON_COMPRESSED && l_kind != FALCON_PADDED && l_kind != FALCON_CT) { // we are now supporting only compressed, padded and ct signatures
        log_it(L_ERROR, "::read_signature() l_kind %ul is not supported", l_kind);
        return NULL;
    }

    l_type = *(uint32_t *)l_ptr; l_ptr += sizeof(uint32_t);
    if (l_type != FALCON_DYNAMIC && l_type != FALCON_TREE) { // we are now supporting only sign and sign open signatures
        log_it(L_ERROR, "::read_signature() l_type %ul is not supported", l_type);
        return NULL;
    }

    l_sig_len = *(uint64_t *)l_ptr; l_ptr += sizeof(uint64_t);
    if (l_buflen != sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len) {
        log_it(L_ERROR, "::read_signature() l_buflen %"DAP_UINT64_FORMAT_U" is not equal to expected size %zu",
               l_buflen, sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3 + l_sig_len);
        return NULL;
    }

    falcon_signature_t *l_sign = DAP_NEW(falcon_signature_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "Memory allocation error");
        return NULL;
    }

    l_sign->degree = l_degree;
    l_sign->kind = l_kind;
    l_sign->type = l_type;
    l_sign->sig_len = l_sig_len;
    l_sign->sig_data = DAP_NEW_SIZE(uint8_t, l_sig_len);
    memcpy(l_sign->sig_data, l_ptr, l_sig_len);
    assert(l_ptr + l_sig_len - a_buf == (int64_t)l_buflen);

    return l_sign;
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

void falcon_signature_delete(falcon_signature_t *a_sig){
    assert(a_sig);
    DAP_DEL_Z(a_sig->sig_data);
    a_sig->sig_len = 0;
}

