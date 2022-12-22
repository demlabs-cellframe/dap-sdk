#ifndef CELLFRAME_SDK_DAP_ENC_FALCON_H
#define CELLFRAME_SDK_DAP_ENC_FALCON_H

#include "dap_enc_key.h"
#include "falcon/falcon_params.h"

void dap_enc_sig_falcon_key_new(struct dap_enc_key *key);

void dap_enc_sig_falcon_key_new_generate(struct dap_enc_key *key, const void *kex_buf, size_t kex_size, const void* seed, size_t seed_size, size_t key_size);

int dap_enc_sig_falcon_get_sign(struct dap_enc_key* key, const void * msg, const size_t msg_size, void* signature, const size_t signature_size);

int dap_enc_sig_falcon_verify_sign(struct dap_enc_key* key, const void* msg, const size_t msg_size, void* signature, const size_t signature_size);

void dap_enc_sig_falcon_key_delete(struct dap_enc_key *key);
size_t dap_enc_sig_falconkey_pub_output_size(struct dap_enc_key *l_key);
int dap_enc_sig_falcon_key_pub_output(struct dap_enc_key *l_key, void* l_output);

//uint8_t* dap_enc_sig_falcon_write_signature();

#endif //CELLFRAME_SDK_DAP_ENC_FALCON_H
