#pragma once

#include "dap_enc_key.h"

#ifdef __cplusplus
extern "C" {
#endif

void dap_enc_ntru_prime_key_new(dap_enc_key_t *a_key);
void dap_enc_ntru_prime_key_generate(dap_enc_key_t *a_key, const void *kex_buf,
        size_t kex_size, const void *seed, size_t seed_size, size_t key_size);
void dap_enc_ntru_prime_key_new_from_data_public(dap_enc_key_t *a_key,
        const void *a_in, size_t a_in_size);
void dap_enc_ntru_prime_key_delete(dap_enc_key_t *a_key);
size_t dap_enc_ntru_prime_gen_bob_shared_key(dap_enc_key_t *a_bob_key,
        const void *a_alice_pub, size_t a_alice_pub_size, void **a_cypher_msg);
size_t dap_enc_ntru_prime_gen_alice_shared_key(dap_enc_key_t *a_alice_key,
        const void *a_alice_priv, size_t a_cypher_msg_size, uint8_t *a_cypher_msg);

#ifdef __cplusplus
}
#endif
