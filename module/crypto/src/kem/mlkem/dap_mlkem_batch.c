/**
 * @file dap_mlkem_batch.c
 * @brief Batch ML-KEM-512 encapsulation.
 *
 * Currently calls individual kem_enc per item.
 * kem_enc only needs Alice's PK — no Bob keypair required.
 *
 * The API is designed for future GPU NTT batching: gather all
 * forward NTTs (2N) and inverse NTTs (3N) into contiguous arrays
 * for a single GPU dispatch.
 *
 * With K=2 (ML-KEM-512), each encaps needs:
 *   K = 2 forward + (K+1) = 3 inverse NTTs = 5 NTTs total.
 */

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_mlkem_batch.h"

#define LOG_TAG "mlkem_batch"

extern int dap_mlkem512_kem_enc(uint8_t *a_ct, uint8_t *a_ss, const uint8_t *a_pk);

#define MLKEM512_PUBLICKEYBYTES   800
#define MLKEM512_CIPHERTEXTBYTES  768
#define MLKEM512_SSBYTES          32

int dap_mlkem512_batch_encaps(const uint8_t **a_alice_pks,
                              const size_t *a_alice_pk_sizes,
                              uint32_t a_count,
                              dap_mlkem_batch_encaps_result_t *a_results)
{
    if (!a_alice_pks || !a_alice_pk_sizes || !a_results || a_count == 0)
        return -1;

    memset(a_results, 0, a_count * sizeof(dap_mlkem_batch_encaps_result_t));

    int l_success = 0;
    for (uint32_t i = 0; i < a_count; i++) {
        if (a_alice_pk_sizes[i] < MLKEM512_PUBLICKEYBYTES) {
            a_results[i].error = -2;
            continue;
        }

        a_results[i].ciphertext = DAP_NEW_Z_SIZE(uint8_t, MLKEM512_CIPHERTEXTBYTES);
        a_results[i].shared_secret = DAP_NEW_Z_SIZE(uint8_t, MLKEM512_SSBYTES);
        if (!a_results[i].ciphertext || !a_results[i].shared_secret) {
            DAP_DEL_Z(a_results[i].ciphertext);
            DAP_DEL_Z(a_results[i].shared_secret);
            a_results[i].error = -3;
            continue;
        }

        if (dap_mlkem512_kem_enc(a_results[i].ciphertext,
                                  a_results[i].shared_secret,
                                  a_alice_pks[i]) != 0) {
            DAP_DELETE(a_results[i].ciphertext); a_results[i].ciphertext = NULL;
            DAP_DELETE(a_results[i].shared_secret); a_results[i].shared_secret = NULL;
            a_results[i].error = -4;
            continue;
        }

        a_results[i].ciphertext_size = MLKEM512_CIPHERTEXTBYTES;
        a_results[i].shared_secret_size = MLKEM512_SSBYTES;
        l_success++;
    }

    log_it(L_INFO, "ML-KEM-512 batch encaps: %d/%u succeeded", l_success, a_count);
    return 0;
}

void dap_mlkem_batch_encaps_result_cleanup(dap_mlkem_batch_encaps_result_t *a_result)
{
    if (!a_result) return;
    DAP_DEL_Z(a_result->ciphertext);
    if (a_result->shared_secret) {
        memset(a_result->shared_secret, 0, a_result->shared_secret_size);
        DAP_DEL_Z(a_result->shared_secret);
    }
}
