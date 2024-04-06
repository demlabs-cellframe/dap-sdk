#ifndef _DAP_ENC_SHIPOVNIK_H_
#define _DAP_ENC_SHIPOVNIK_H_

#include "sig_shipovnik/shipovnik.h"
#include "sig_shipovnik/shipovnik_params.h"
#include "dap_enc_key.h"


enum DAP_SHIPOVNIK_SIGN_SECURITY {
    SHIPOVNIK_TOY = 0, SHIPOVNIK_MAX_SPEED, SHIPOVNIK_MIN_SIZE, SHIPOVNIK_MAX_SECURITY
};

void dap_enc_sig_shipovnik_set_type(enum DAP_SHIPOVNIK_SIGN_SECURITY type);

void dap_enc_sig_shipovnik_key_new(dap_enc_key_t *a_key);

void dap_enc_sig_shipovnik_key_new_generate(dap_enc_key_t *key, const void *kex_buf,
                                    size_t kex_size, const void *seed, size_t seed_size,
                                    size_t key_size);
void *dap_enc_sig_shipovnik_key_delete(dap_enc_key_t *a_key);

int dap_enc_sig_shipovnik_get_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size);

int dap_enc_sig_shipovnik_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature,
                                      const size_t signature_size);

uint8_t *dap_enc_sig_shipovnik_write_signature(const uint8_t* a_sign, size_t *a_sign_out);
uint8_t *dap_enc_sig_shipovnik_write_private_key(const uint8_t* a_private_key, size_t *a_buflen_out);
uint8_t *dap_enc_sig_shipovnik_write_public_key(const uint8_t* a_public_key, size_t *a_buflen_out);
uint8_t* dap_enc_sig_shipovnik_read_signature(const uint8_t *a_buf, size_t a_buflen);
uint8_t* dap_enc_sig_shipovnik_read_private_key(const uint8_t *a_buf, size_t a_buflen);
uint8_t *dap_enc_sig_shipovnik_read_public_key(const uint8_t *a_buf, size_t a_buflen);


void dap_enc_sig_shipovnik_signature_delete(void *a_sig);
void dap_enc_sig_shipovnik_private_key_delete(uint8_t* privateKey);
void dap_enc_sig_shipovnik_public_key_delete(uint8_t* publicKey);
void dap_enc_sig_shipovnik_private_and_public_keys_delete(uint8_t* privateKey, uint8_t* publicKey);


#endif
