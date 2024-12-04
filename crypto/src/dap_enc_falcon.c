#include "dap_enc_falcon.h"
#include "falcon.h"

#define LOG_TAG "dap_enc_sig_falcon"

static falcon_sign_degree_t s_falcon_sign_degree = FALCON_512;
static falcon_kind_t s_falcon_kind = FALCON_COMPRESSED;
static falcon_sign_type_t s_falcon_type = FALCON_DYNAMIC;


static int s_deserialised_sign_check(
    uint64_t a_buflen,
    uint64_t a_des_buflen, 
    falcon_sign_degree_t a_degree, 
    falcon_kind_t a_kind, 
    falcon_sign_type_t a_type)
{
    if (a_buflen != a_des_buflen) {
        log_it(L_ERROR, "Buflen  %"DAP_UINT64_FORMAT_U" is not equal to sign size ( %"DAP_UINT64_FORMAT_U")",
                        a_buflen, a_des_buflen);
        return -1;
    }
    if (a_degree != FALCON_512 && a_degree != FALCON_1024) { // we are now supporting only 512 and 1024 degrees
        log_it(L_ERROR, "Degree %u is not supported", a_degree);
        return -2;
    }
    if (a_kind != FALCON_COMPRESSED && a_kind != FALCON_PADDED && a_kind != FALCON_CT) { // we are now supporting only compressed, padded and ct signatures
        log_it(L_ERROR, "Kind %ul is not supported", a_kind);
        return -3;
    }
    if (a_type != FALCON_DYNAMIC && a_type != FALCON_TREE) { // we are now supporting only sign and sign open signatures
        log_it(L_ERROR, "Type %ul is not supported", a_type);
        return -4;
    }
    return 0;
}

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

void dap_enc_sig_falcon_key_new_generate(dap_enc_key_t *a_key, const void *kex_buf, size_t kex_size,
        const void* seed, size_t seed_size, size_t key_size) {

    dap_enc_sig_falcon_key_new(a_key);

    int l_ret = 0;
    unsigned int l_logn = s_falcon_sign_degree;
    size_t l_tmp[FALCON_TMPSIZE_KEYGEN(l_logn)];
    falcon_private_key_t *l_skey = NULL;
    falcon_public_key_t *l_pkey = NULL;

    a_key->pub_key_data_size = sizeof(falcon_public_key_t);
    a_key->priv_key_data_size = sizeof(falcon_private_key_t);

    l_skey = DAP_NEW_Z_SIZE_RET_IF_FAIL(falcon_private_key_t, a_key->priv_key_data_size);
    l_pkey = DAP_NEW_Z_SIZE_RET_IF_FAIL(falcon_public_key_t, a_key->pub_key_data_size, l_skey);
    l_skey->data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, FALCON_PRIVKEY_SIZE(l_logn), l_skey, l_pkey);
    l_pkey->data = DAP_NEW_Z_SIZE_RET_IF_FAIL(uint8_t, FALCON_PUBKEY_SIZE(l_logn), l_skey->data, l_skey, l_pkey);

    l_skey->degree = s_falcon_sign_degree;
    l_skey->kind = s_falcon_kind;
    l_skey->type = s_falcon_type;
    
    l_pkey->degree = s_falcon_sign_degree;
    l_pkey->kind = s_falcon_kind;
    l_pkey->type = s_falcon_type;

    shake256_context rng;
    if(!seed || !seed_size) {
        if ((l_ret = shake256_init_prng_from_system(&rng))) {
            log_it(L_ERROR, "Failed to initialize PRNG");
            DAP_DEL_MULTY(l_skey->data, l_skey, l_pkey->data, l_pkey);
            return;
        }
    } else {
        shake256_init_prng_from_seed(&rng, seed, seed_size);
    }
    l_ret = falcon_keygen_make(
            &rng, l_logn,
            l_skey->data, FALCON_PRIVKEY_SIZE(l_logn),
            l_pkey->data, FALCON_PUBKEY_SIZE(l_logn),
            l_tmp, FALCON_TMPSIZE_KEYGEN(l_logn)
            );
    if (l_ret != 0) {
        log_it(L_ERROR, "Failed to generate falcon key");
        DAP_DEL_MULTY(l_skey->data, l_skey, l_pkey->data, l_pkey);
        return;
    }
    a_key->priv_key_data = l_skey;
    a_key->pub_key_data = l_pkey;
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
        l_sig->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(byte_t, l_sig_len, -1);

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

void dap_enc_sig_falcon_key_delete(dap_enc_key_t *key)
{
    dap_return_if_pass(!key);
    falcon_private_and_public_keys_delete((falcon_private_key_t *)key->priv_key_data, (falcon_public_key_t *)key->pub_key_data);
    key->priv_key_data = NULL;
    key->pub_key_data = NULL;
    key->priv_key_data_size = 0;
    key->pub_key_data_size = 0;
}

// Serialize a public key into a buffer.
uint8_t *dap_enc_sig_falcon_write_public_key(const void *a_public_key, size_t *a_buflen_out) {
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
    uint64_t l_buflen = dap_enc_sig_falcon_ser_public_key_size(a_public_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_public_key->degree, (uint64_t)sizeof(uint32_t),
        &l_public_key->kind, (uint64_t)sizeof(uint32_t),
        &l_public_key->type, (uint64_t)sizeof(uint32_t),
        l_public_key->data, (uint64_t)FALCON_PUBKEY_SIZE(l_public_key->degree)
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

uint8_t *dap_enc_sig_falcon_write_private_key(const void *a_private_key, size_t *a_buflen_out) {
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
    uint64_t l_buflen = dap_enc_sig_falcon_ser_private_key_size(a_private_key);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_private_key->degree, (uint64_t)sizeof(uint32_t),
        &l_private_key->kind, (uint64_t)sizeof(uint32_t),
        &l_private_key->type, (uint64_t)sizeof(uint32_t),
        l_private_key->data, (uint64_t)FALCON_PRIVKEY_SIZE(l_private_key->degree)
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

void *dap_enc_sig_falcon_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
// in work
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(uint32_t) * 3, NULL);
// func work
    uint64_t l_buflen = 0;
    uint64_t l_skey_len = a_buflen - sizeof(uint64_t) - sizeof(uint32_t) * 3;

    falcon_private_key_t *l_skey = DAP_NEW_Z_RET_VAL_IF_FAIL(falcon_private_key_t, NULL);
    l_skey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_skey_len, NULL, l_skey);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen, 
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_skey->degree, (uint64_t)sizeof(uint32_t),
        &l_skey->kind, (uint64_t)sizeof(uint32_t),
        &l_skey->type, (uint64_t)sizeof(uint32_t),
        l_skey->data, (uint64_t)l_skey_len
    );
// out work
    int l_res_check = s_deserialised_sign_check(a_buflen, l_buflen, l_skey->degree, l_skey->kind, l_skey->type);
    if (l_skey_len != FALCON_PRIVKEY_SIZE(l_skey->degree)) {
        log_it(L_ERROR,"::read_private_key() l_skey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %u", l_skey_len, FALCON_PRIVKEY_SIZE(l_skey->degree));
        DAP_DEL_MULTY(l_skey->data, l_skey);
        return NULL;
    }
    if (l_res_des || l_res_check) {
        log_it(L_ERROR,"::read_private_key() deserialise private, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_skey->data, l_skey);
        return NULL;
    }
    return l_skey;
}

void *dap_enc_sig_falcon_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
// in work
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) + sizeof(uint32_t) * 3, NULL);
// func work
    uint64_t l_buflen = 0;
    uint64_t l_pkey_len = a_buflen - sizeof(uint64_t) - sizeof(uint32_t) * 3;

    falcon_public_key_t *l_pkey = DAP_NEW_Z_RET_VAL_IF_FAIL(falcon_public_key_t, NULL);
    l_pkey->data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_pkey_len, NULL, l_pkey);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen, 
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_pkey->degree, (uint64_t)sizeof(uint32_t),
        &l_pkey->kind, (uint64_t)sizeof(uint32_t),
        &l_pkey->type, (uint64_t)sizeof(uint32_t),
        l_pkey->data, (uint64_t)l_pkey_len
    );
// out work
    int l_res_check = s_deserialised_sign_check(a_buflen, l_buflen, l_pkey->degree, l_pkey->kind, l_pkey->type);
    if (l_pkey_len != FALCON_PUBKEY_SIZE(l_pkey->degree)) {
        log_it(L_ERROR,"::read_public_key() l_pkey_len %"DAP_UINT64_FORMAT_U" is not equal to expected size %u", l_pkey_len, FALCON_PUBKEY_SIZE(l_pkey->degree));
        DAP_DEL_MULTY(l_pkey->data, l_pkey);
        return NULL;
    }
    if (l_res_des || l_res_check) {
        log_it(L_ERROR,"::read_public_key() deserialise public key, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_pkey->data, l_pkey);
        return NULL;
    }
    return l_pkey;
}

uint8_t *dap_enc_sig_falcon_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    dap_return_val_if_pass(!a_sign, NULL);
    falcon_signature_t *l_sign = (falcon_signature_t*)a_sign;
// func work
    uint64_t l_buflen = dap_enc_sig_falcon_ser_sig_size(a_sign);
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_sign->degree, (uint64_t)sizeof(uint32_t),
        &l_sign->kind, (uint64_t)sizeof(uint32_t),
        &l_sign->type, (uint64_t)sizeof(uint32_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sign->sig_len
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = l_buflen : 0;
    return l_buf;
}

void *dap_enc_sig_falcon_read_signature(const uint8_t* a_buf, size_t a_buflen)
{
// sanity
    dap_return_val_if_pass(!a_buf || a_buflen < sizeof(uint64_t) * 2 + sizeof(uint32_t) * 3, NULL);
// func work
    uint64_t l_buflen = 0;
    uint64_t l_sig_len = a_buflen - sizeof(uint64_t) * 2 - sizeof(uint32_t) * 3;

    falcon_signature_t *l_sign = DAP_NEW_Z_RET_VAL_IF_FAIL(falcon_signature_t, NULL);
    l_sign->sig_data = DAP_NEW_Z_SIZE_RET_VAL_IF_FAIL(uint8_t, l_sig_len, NULL, l_sign);

    int l_res_des = DAP_VA_DESERIALIZE(a_buf, a_buflen, 
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_sign->degree, (uint64_t)sizeof(uint32_t),
        &l_sign->kind, (uint64_t)sizeof(uint32_t),
        &l_sign->type, (uint64_t)sizeof(uint32_t),
        &l_sign->sig_len, (uint64_t)sizeof(uint64_t),
        l_sign->sig_data, (uint64_t)l_sig_len
    );
// out work
    int l_res_check = s_deserialised_sign_check(a_buflen, l_buflen, l_sign->degree, l_sign->kind, l_sign->type);
    if (l_res_des || l_res_check) {
        log_it(L_ERROR,"Error deserialise signature, err code %d", l_res_des ? l_res_des : l_res_check );
        DAP_DEL_MULTY(l_sign->sig_data, l_sign);
        return NULL;
    }
    return l_sign;
}


void falcon_private_and_public_keys_delete(falcon_private_key_t* privateKey, falcon_public_key_t* publicKey) {
    if(privateKey)
        falcon_private_key_delete(privateKey);
    if(publicKey)
        falcon_public_key_delete(publicKey);
}

void falcon_private_key_delete(void* a_skey) {
    dap_return_if_pass(!a_skey);

    falcon_private_key_t *l_skey = a_skey;
    memset(l_skey->data, 0, FALCON_PRIVKEY_SIZE(l_skey->degree));
    l_skey->degree = 0;
    l_skey->type = 0;
    l_skey->kind = 0;
    DAP_DEL_MULTY(l_skey->data, l_skey);
}

void falcon_public_key_delete(void *a_skey) {
    dap_return_if_pass(!a_skey);

    falcon_public_key_t *l_pkey = a_skey;
    memset(l_pkey->data, 0, FALCON_PUBKEY_SIZE(l_pkey->degree));
    l_pkey->degree = 0;
    l_pkey->type = 0;
    l_pkey->kind = 0;
    DAP_DEL_MULTY(l_pkey->data, l_pkey);
}

void falcon_signature_delete(void *a_sig){
    dap_return_if_pass(!a_sig);
    DAP_DEL_Z(((falcon_signature_t *)a_sig)->sig_data);
    ((falcon_signature_t *)a_sig)->sig_len = 0;
}

