
#include "dilithium_params.h"
#include "dap_enc_falcon.h"
#include "falcon.h"

#define LOG_TAG "dap_enc_sig_falcon"

static enum DAP_FALCON_SIGN_DEGREE s_falcon_sign_degree = FALCON_512;

void dap_enc_sig_falcon_set_degree(enum DAP_FALCON_SIGN_DEGREE a_falcon_sign_degree)
{
    s_falcon_sign_degree = a_falcon_sign_degree;
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
    unsigned int logn = 0;
    if (s_falcon_sign_degree == FALCON_512) {
        logn = 9;
    } else if (s_falcon_sign_degree == FALCON_1024) {
        logn = 10;
    } else {
        log_it(L_ERROR, "Unknown falcon sign degree");
        return;
    }
    size_t tmp[FALCON_TMPSIZE_KEYGEN(logn)];


    shake256_context* rng = NULL;
    retcode = shake256_init_prng_from_system(rng);
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to initialize PRNG");
        return;
    }

    retcode = falcon_keygen_make(
            rng,
            logn,
            &key->priv_key_data, key->priv_key_data_size,
            &key->pub_key_data, key->pub_key_data_size,
            tmp, FALCON_TMPSIZE_KEYGEN(logn));
    if (retcode != 0) {
        log_it(L_ERROR, "Failed to generate falcon key");
        return;
    }
}