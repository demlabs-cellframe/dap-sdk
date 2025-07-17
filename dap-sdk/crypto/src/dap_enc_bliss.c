#include <stdint.h>
#include <assert.h>
#include <inttypes.h>
#include <string.h>

#include "dap_enc_bliss.h"
#include "dap_common.h"
#include "rand/dap_rand.h"
#include "SimpleFIPS202.h"

#define LOG_TAG "dap_enc_sig_bliss"

static enum DAP_BLISS_SIGN_SECURITY _bliss_type = MAX_SECURITY; // by default
bliss_kind_t _bliss_kind = BLISS_B_4; // same as previous as I expect

void dap_enc_sig_bliss_set_type(enum DAP_BLISS_SIGN_SECURITY type)
{
    _bliss_type = type;
}

void dap_enc_sig_bliss_key_new(dap_enc_key_t *a_key) {

    a_key->type = DAP_ENC_KEY_TYPE_SIG_BLISS;
    a_key->enc = NULL;
    a_key->sign_get = dap_enc_sig_bliss_get_sign;
    a_key->sign_verify = dap_enc_sig_bliss_verify_sign;
}

/**
 * @brief dap_enc_sig_bliss_key_pub_output
 * @param l_key
 * @param l_output
 * @return
 */
int dap_enc_sig_bliss_key_pub_output(dap_enc_key_t *l_key, void * l_output)
{
    int32_t retcode;

    retcode = bliss_b_public_key_extract((bliss_public_key_t *) l_output,
            (const bliss_private_key_t *) l_key->priv_key_data);
    if(retcode != BLISS_B_NO_ERROR) {
        log_it(L_CRITICAL, "Can't extract public key from the private one");
        return -1;
    }
    return 0;
}

// generation key pair for sign Alice
// OUTPUT:
// a_key->data  --- Alice's public key
// alice_priv  ---  Alice's private key
// alice_msg_len --- Alice's private key length
void dap_enc_sig_bliss_key_new_generate(dap_enc_key_t *a_key, UNUSED_ARG const void *kex_buf,
        UNUSED_ARG size_t kex_size, UNUSED_ARG const void *seed, 
        UNUSED_ARG size_t seed_size, UNUSED_ARG size_t key_size)
{
    int32_t l_retcode = 0;

    dap_enc_sig_bliss_key_new(a_key);

    uint8_t seed_tmp[SHA3_512_DIGEST_LENGTH];
    entropy_t entropy;
    if(seed && seed_size>0){
        assert(SHA3_512_DIGEST_LENGTH==64);
        SHA3_512((unsigned char *)seed_tmp, (const unsigned char *)seed, seed_size);
    }
    else
        randombytes(&seed_tmp, 64);
    entropy_init(&entropy, seed_tmp);

    /* type is a param of sign-security
     * type = 0 - "toy" version                (< 60 bits)
     * type = 1 - max speed                    (128 bits)
     * type = 2 - min size                     (128 bits)
     * type = 3 - good speed and good security (160 bits)
     * type = 4 - max securiry                 (192 bits)
     */
    //int32_t type = 4;
    a_key->priv_key_data_size = sizeof(bliss_private_key_t);
    a_key->priv_key_data = DAP_NEW_SIZE(void, a_key->priv_key_data_size);
    l_retcode = bliss_b_private_key_gen((bliss_private_key_t *) a_key->priv_key_data, _bliss_kind, &entropy);
    if(l_retcode != BLISS_B_NO_ERROR) {
        bliss_b_private_key_delete(a_key->priv_key_data);
        a_key->priv_key_data = NULL;
        a_key->priv_key_data_size = 0;
        log_it(L_CRITICAL, "Error");
        return;
    }

    a_key->pub_key_size = sizeof(bliss_public_key_t);
    a_key->pub_key_data = DAP_NEW_SIZE(void, a_key->pub_key_size);
    l_retcode = bliss_b_public_key_extract((bliss_public_key_t *) a_key->pub_key_data,
            (const bliss_private_key_t *) a_key->priv_key_data);
    if(l_retcode != BLISS_B_NO_ERROR) {
        bliss_b_private_key_delete(a_key->priv_key_data);
        bliss_b_public_key_delete(a_key->pub_key_data);
        log_it(L_CRITICAL, "Error");
        return;
    }
}

int dap_enc_sig_bliss_get_sign(dap_enc_key_t * key, const void * msg,
        const size_t msg_size, void * signature, const size_t signature_size)
{
    if(signature_size < sizeof(bliss_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }
    uint8_t seed_tmp[SHA3_512_DIGEST_LENGTH];
    entropy_t entropy;
    randombytes(&seed_tmp, 64);
    entropy_init(&entropy, seed_tmp);

    return bliss_b_sign((bliss_signature_t *) signature,
            (const bliss_private_key_t *) key->priv_key_data,
            (const uint8_t *) msg,
            msg_size,
            &entropy);
}

int dap_enc_sig_bliss_verify_sign(dap_enc_key_t * key, const void * msg,
        const size_t msg_size, void * signature, const size_t signature_size)
{
    if(signature_size < sizeof(bliss_signature_t)) {
        log_it(L_ERROR, "bad signature size");
        return -1;
    }
    return bliss_b_verify(signature, key->pub_key_data, msg, msg_size);
}

void dap_enc_sig_bliss_key_delete(dap_enc_key_t *key)
{
    dap_return_if_pass(!key);
    if(key->priv_key_data) {
        bliss_b_private_key_delete(key->priv_key_data);
        key->priv_key_data = NULL;
    }
    if(key->pub_key_data) {
        bliss_b_public_key_delete(key->pub_key_data);
        key->pub_key_data = NULL;
    }
    key->priv_key_data_size = 0;
    key->pub_key_size = 0;
}

/* Serialize a signature */
uint8_t *dap_enc_sig_bliss_write_signature(const void *a_sign, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    bliss_signature_t *l_sign = (bliss_signature_t *)a_sign;
    bliss_param_t p;
    dap_return_val_if_pass(!bliss_params_init(&p, l_sign->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_bliss_ser_sig_size(a_sign);
    uint32_t l_kind = l_sign->kind;
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen,  (uint64_t)sizeof(uint64_t),
        &l_kind,    (uint64_t)sizeof(uint32_t),
        l_sign->z1, (uint64_t)(p.n * sizeof(int32_t)),
        l_sign->z2, (uint64_t)(p.n * sizeof(int32_t)),
        l_sign->c,  (uint64_t)(p.kappa * sizeof(int32_t))
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Deserialize a signature */
void *dap_enc_sig_bliss_read_signature(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(uint64_t) + sizeof(uint32_t)))
        return NULL ;
    bliss_kind_t kind;
    uint64_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    memcpy(&kind, a_buf + sizeof(uint64_t), sizeof(uint32_t));
    if(l_buflen != a_buflen)
        return NULL ;
    bliss_param_t p;
    if(!bliss_params_init(&p, kind))
        return NULL ;

    bliss_signature_t* l_sign = DAP_NEW(bliss_signature_t);
    if (!l_sign) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_sign->kind = kind;
    l_sign->z1 = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    l_sign->z2 = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    l_sign->c = DAP_NEW_SIZE(unsigned int, p.kappa * sizeof(unsigned int));
    uint64_t l_shift_mem = sizeof(uint64_t) + sizeof(uint32_t);
    memcpy(l_sign->z1, a_buf + l_shift_mem, p.n * sizeof(int32_t));
    l_shift_mem += p.n * sizeof(int32_t);
    memcpy(l_sign->z2, a_buf + l_shift_mem, p.n * sizeof(int32_t));
    l_shift_mem += p.n * sizeof(int32_t);
    memcpy(l_sign->c, a_buf + l_shift_mem, p.kappa * sizeof(int32_t));
    return l_sign;
}

/* Serialize a private key. */
uint8_t *dap_enc_sig_bliss_write_private_key(const void *a_private_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    bliss_private_key_t *l_private_key = (bliss_private_key_t *)a_private_key;
    bliss_param_t p;
    dap_return_val_if_pass(!l_private_key || !bliss_params_init(&p, l_private_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_bliss_ser_private_key_size(a_private_key);
    uint32_t l_kind = l_private_key->kind;
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_private_key->s1, (uint64_t)(p.n * sizeof(int32_t)),
        l_private_key->s2, (uint64_t)(p.n * sizeof(int32_t)),
        l_private_key->a, (uint64_t)(p.n * sizeof(int32_t))
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Serialize a public key. */
uint8_t *dap_enc_sig_bliss_write_public_key(const void *a_public_key, size_t *a_buflen_out)
{
// in work
    a_buflen_out ? *a_buflen_out = 0 : 0;
    bliss_public_key_t *l_public_key = (bliss_public_key_t *)a_public_key;
    bliss_param_t p;
    dap_return_val_if_pass(!bliss_params_init(&p, l_public_key->kind), NULL);
// func work
    uint64_t l_buflen = dap_enc_sig_bliss_ser_public_key_size(a_public_key);
    uint32_t l_kind = l_public_key->kind;
    uint8_t *l_buf = DAP_VA_SERIALIZE_NEW(l_buflen,
        &l_buflen, (uint64_t)sizeof(uint64_t),
        &l_kind, (uint64_t)sizeof(uint32_t),
        l_public_key->a, p.n * (uint64_t)sizeof(int32_t)
    );
// out work
    (a_buflen_out  && l_buf) ? *a_buflen_out = (size_t)l_buflen : 0;
    return l_buf;
}

/* Deserialize a private key. */
void *dap_enc_sig_bliss_read_private_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(uint64_t) + sizeof(uint32_t)))
        return NULL;
    bliss_kind_t kind;
    uint64_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    memcpy(&kind, a_buf + sizeof(uint64_t), sizeof(uint32_t));
    if(l_buflen != a_buflen)
        return NULL;
    bliss_param_t p;
    if(!bliss_params_init(&p, kind))
        return NULL;

    bliss_private_key_t* l_private_key = DAP_NEW(bliss_private_key_t);
    if (!l_private_key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_private_key->kind = kind;

    l_private_key->s1 = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    l_private_key->s2 = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    l_private_key->a  = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    uint64_t l_shift_mem = sizeof(uint64_t) + sizeof(uint32_t);
    memcpy(l_private_key->s1, a_buf + l_shift_mem, p.n * sizeof(int32_t));
    l_shift_mem += p.n * sizeof(int32_t);
    memcpy(l_private_key->s2, a_buf + l_shift_mem, p.n * sizeof(int32_t));
    l_shift_mem += p.n * sizeof(int32_t);
    memcpy(l_private_key->a, a_buf + l_shift_mem, p.n * sizeof(int32_t));
    l_shift_mem += p.n * sizeof(int32_t);
    return l_private_key;
}

/* Deserialize a public key. */
void *dap_enc_sig_bliss_read_public_key(const uint8_t *a_buf, size_t a_buflen)
{
    if(!a_buf || a_buflen < (sizeof(uint64_t) + sizeof(uint32_t)))
        return NULL;
    bliss_kind_t kind;
    uint64_t l_buflen = 0;
    memcpy(&l_buflen, a_buf, sizeof(uint64_t));
    memcpy(&kind, a_buf + sizeof(uint64_t), sizeof(uint32_t));
    if(l_buflen != a_buflen)
        return NULL;
    bliss_param_t p;
    if(!bliss_params_init(&p, kind))
        return NULL;
    bliss_public_key_t* l_public_key = DAP_NEW(bliss_public_key_t);
    if (!l_public_key) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        return NULL;
    }
    l_public_key->kind = kind;

    l_public_key->a = DAP_NEW_SIZE(int32_t, p.n * sizeof(int32_t));
    if (!l_public_key->a) {
        log_it(L_CRITICAL, "%s", c_error_memory_alloc);
        DAP_DEL_Z(l_public_key);
        return NULL;
    }
    memcpy(l_public_key->a, a_buf + sizeof(uint64_t) + sizeof(uint32_t), p.n * sizeof(int32_t));
    return l_public_key;
}
