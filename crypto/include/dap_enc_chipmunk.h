#ifndef DAP_ENC_CHIPMUNK_H
#define DAP_ENC_CHIPMUNK_H

#include <stddef.h>
#include <stdint.h>
#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_enc_chipmunk_key {
    dap_enc_key_t key;
    uint8_t *private_key;
    size_t private_key_size;
    uint8_t *public_key;
    size_t public_key_size;
} dap_enc_chipmunk_key_t;

// Key initialization
int dap_enc_chipmunk_key_new(dap_enc_key_t *a_key);

// Key pair generation
int dap_enc_chipmunk_key_generate(dap_enc_key_t *a_key, size_t a_size);

// Message signing
int dap_enc_chipmunk_sign(dap_enc_key_t *a_key, const void *a_msg, 
                          size_t a_msg_size, void *a_sign, const size_t a_sign_size);

// Signature verification
int dap_enc_chipmunk_verify(dap_enc_key_t *a_key, const void *a_msg, 
                           size_t a_msg_size, void *a_sign, const size_t a_sign_size);

// Calculate signature size
size_t dap_enc_chipmunk_calc_signature_size(void);

// Resource deallocation
void dap_enc_chipmunk_key_delete(dap_enc_key_t *a_key);

#ifdef __cplusplus
}
#endif

#endif // DAP_ENC_CHIPMUNK_H 