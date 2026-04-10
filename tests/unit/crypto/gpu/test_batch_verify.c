/**
 * test_batch_verify.c — Correctness + performance test for batch Dilithium verify
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <assert.h>

#include "dap_common.h"
#include "dap_ntt.h"
#include "dilithium_params.h"

#ifdef DAP_HAS_GPU
#include "dap_gpu.h"
#endif

static double s_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static const char *s_test_msg = "Batch verify test message for DAP SDK";

int main(void)
{
    dap_log_level_set(L_WARNING);
    dap_ntt_dispatch_init();

#ifdef DAP_HAS_GPU
    dap_gpu_init(DAP_GPU_BACKEND_NONE);
    const dap_gpu_device_info_t *info = dap_gpu_get_device_info();
    if (info)
        printf("GPU: %s (%s)\n", info->name,
               info->unified_memory ? "UMA" : "discrete");
#endif

    printf("=== Batch Dilithium Verify Test ===\n\n");

    unsigned int batch_sizes[] = { 1, 4, 10, 50, 100 };
    int nsizes = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    for (int si = 0; si < nsizes; si++) {
        unsigned int count = batch_sizes[si];
        printf("--- Batch size: %u ---\n", count);

        /* Generate keypairs and sign messages — each key/sig individually allocated */
        dilithium_public_key_t **pub_keys = calloc(count, sizeof(*pub_keys));
        dilithium_private_key_t **priv_keys = calloc(count, sizeof(*priv_keys));
        dilithium_signature_t **sigs = calloc(count, sizeof(*sigs));
        unsigned char **msgs = calloc(count, sizeof(unsigned char *));
        unsigned long long *msg_lens = calloc(count, sizeof(*msg_lens));
        int *results = calloc(count, sizeof(int));
        const dilithium_public_key_t **pub_ptrs = calloc(count, sizeof(*pub_ptrs));

        assert(pub_keys && priv_keys && sigs && msgs && msg_lens && results && pub_ptrs);

        for (unsigned int i = 0; i < count; i++) {
            pub_keys[i] = calloc(1, sizeof(dilithium_public_key_t));
            priv_keys[i] = calloc(1, sizeof(dilithium_private_key_t));
            sigs[i] = calloc(1, sizeof(dilithium_signature_t));

            unsigned char seed[32];
            for (int j = 0; j < 32; j++)
                seed[j] = (unsigned char)(rand() & 0xff);

            int rc = dilithium_crypto_sign_keypair(pub_keys[i], priv_keys[i],
                                                    MODE_0, seed, 32);
            assert(rc == 0);

            size_t mlen = strlen(s_test_msg);
            msgs[i] = (unsigned char *)s_test_msg;
            msg_lens[i] = mlen;

            rc = dilithium_crypto_sign(sigs[i], (const unsigned char *)s_test_msg,
                                        mlen, priv_keys[i]);
            assert(rc == 0);

            pub_ptrs[i] = pub_keys[i];
        }

        /* Individual verify for correctness baseline */
        int *single_results = calloc(count, sizeof(int));
        double t0 = s_time_ms();
        int single_ok = 0;
        for (unsigned int i = 0; i < count; i++) {
            single_results[i] = dilithium_crypto_sign_open(
                msgs[i], msg_lens[i], sigs[i], pub_keys[i]);
            if (single_results[i] == 0) single_ok++;
        }
        double single_ms = s_time_ms() - t0;
        printf("  Single verify: %d/%u OK in %.3f ms (%.0f verify/s)\n",
               single_ok, count, single_ms,
               count > 0 ? count / (single_ms / 1000.0) : 0);

        /* Batch verify */
        t0 = s_time_ms();
        int batch_ok = dilithium_crypto_sign_open_batch(
            msgs, msg_lens, sigs, pub_ptrs, count, results);
        double batch_ms = s_time_ms() - t0;
        printf("  Batch  verify: %d/%u OK in %.3f ms (%.0f verify/s)\n",
               batch_ok, count, batch_ms,
               count > 0 ? count / (batch_ms / 1000.0) : 0);

        /* Verify consistency per-signature */
        int mismatches = 0;
        for (unsigned int i = 0; i < count; i++) {
            int s_ok = (single_results[i] == 0);
            int b_ok = (results[i] == 0);
            if (s_ok != b_ok) {
                printf("  MISMATCH sig %u: single=%d batch=%d\n",
                       i, single_results[i], results[i]);
                mismatches++;
            }
        }
        free(single_results);
        if (mismatches) {
            printf("  ERROR: %d mismatches\n", mismatches);
            return 1;
        }
        printf("  Consistency: OK\n");

        /* Test with one corrupted signature */
        if (count >= 2) {
            sigs[0]->sig_data[0] ^= 0xFF;
            t0 = s_time_ms();
            int batch_ok2 = dilithium_crypto_sign_open_batch(
                msgs, msg_lens, sigs, pub_ptrs, count, results);
            double batch_ms2 = s_time_ms() - t0;
            printf("  Corrupted[0]: %d/%u OK in %.3f ms (expected %u)\n",
                   batch_ok2, count, batch_ms2, count - 1);
            assert(batch_ok2 == (int)(count - 1) || results[0] != 0);
            sigs[0]->sig_data[0] ^= 0xFF;
        }

        printf("\n");

        /* Cleanup */
        for (unsigned int i = 0; i < count; i++) {
            dilithium_signature_delete(sigs[i]);
            dilithium_private_key_delete(priv_keys[i]);
            dilithium_public_key_delete(pub_keys[i]);
        }
        free(pub_keys); free(priv_keys); free(sigs);
        free(msgs); free(msg_lens); free(results);
        free(pub_ptrs);
    }

    printf("=== ALL TESTS PASSED ===\n");

#ifdef DAP_HAS_GPU
    dap_gpu_deinit();
#endif
    return 0;
}
