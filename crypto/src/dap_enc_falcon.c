
#include "dilithium_params.h"
#include "dap_enc_falcon.h"

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
        const void* seed, size_t seed_size, size_t key_size)
{
    (void) kex_buf;
    (void) kex_size;
    (void) seed;
    (void) seed_size;
    (void) key_size;

    key->type = DAP_ENC_KEY_TYPE_SIG_FALCON;
    key->enc = NULL;
    key->enc_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_get_sign;
    key->dec_na = (dap_enc_callback_dataop_na_t) dap_enc_sig_falcon_verify_sign;

    falcon_private_key_t *private_key = NULL;
    falcon_public_key_t *public_key = NULL;

    if (s_falcon_sign_degree == FALCON_512) {
        private_key = falcon512_privkey_new();
        public_key = falcon512_pubkey_new();
    } else if (s_falcon_sign_degree == FALCON_1024) {
        private_key = falcon1024_privkey_new();
        public_key = falcon1024_pubkey_new();
    } else {
        log_it(L_ERROR, "Unknown falcon sign degree");
        return;
    }

    if (private_key == NULL || public_key == NULL) {
        log_it(L_ERROR, "Can't allocate memory for falcon key");
        return;
    }

    if (falcon_keygen(private_key, public_key) != 0) {
        log_it(L_ERROR, "Can't generate falcon key");
        return;
    }

    key->data = private_key;
    key->pub_key_data = public_key;
}