/**
 * @file dap_mlkem_batch.h
 * @brief Batch ML-KEM encapsulation with GPU NTT acceleration.
 *
 * Gathers NTT operations from multiple independent KEM encapsulations
 * into contiguous arrays for GPU batch dispatch.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dap_mlkem_batch_encaps_result {
    uint8_t *ciphertext;
    size_t   ciphertext_size;
    uint8_t *shared_secret;
    size_t   shared_secret_size;
    int      error;
} dap_mlkem_batch_encaps_result_t;

/**
 * @brief Batch ML-KEM-512 Bob-side encapsulation.
 *
 * For each Alice public key, generates a Bob ephemeral keypair,
 * performs encapsulation, and derives the shared secret.
 * All NTT operations are gathered and dispatched as a single batch
 * (GPU-accelerated when available).
 *
 * @param a_alice_pks      Array of serialized Alice public keys
 * @param a_alice_pk_sizes Array of public key sizes
 * @param a_count          Number of encapsulations
 * @param a_results        [out] Array of results (caller must free ciphertexts/secrets)
 * @return 0 on success, negative on error
 */
int dap_mlkem512_batch_encaps(const uint8_t **a_alice_pks,
                              const size_t *a_alice_pk_sizes,
                              uint32_t a_count,
                              dap_mlkem_batch_encaps_result_t *a_results);

void dap_mlkem_batch_encaps_result_cleanup(dap_mlkem_batch_encaps_result_t *a_result);

#ifdef __cplusplus
}
#endif
