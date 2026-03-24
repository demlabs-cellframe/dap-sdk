/**
 * Raw ML-KEM microbenchmark — measures keygen/encaps/decaps via dap_kem.h
 * generic API. Benchmarks both stateless and persistent-context paths.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "dap_kem.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define WARMUP 500
#define ITERS  10000

#define BARRIER() __asm__ volatile("" ::: "memory")

#define BENCH(label, call) do {                                   \
    for (int _w = 0; _w < WARMUP; _w++) { call; BARRIER(); }     \
    uint64_t _t0 = now_ns();                                      \
    for (int _i = 0; _i < ITERS; _i++) { call; BARRIER(); }      \
    uint64_t _dt = now_ns() - _t0;                                \
    printf("  %-12s %8d iters  %9.3f us/op\n",                   \
           label, ITERS, (double)_dt / (ITERS * 1000.0));        \
} while(0)

static void s_bench_variant(dap_kem_alg_t a_alg)
{
    const char *name = dap_kem_alg_name(a_alg);
    size_t pk_sz = dap_kem_publickey_size(a_alg);
    size_t sk_sz = dap_kem_secretkey_size(a_alg);
    size_t ct_sz = dap_kem_ciphertext_size(a_alg);
    size_t ss_sz = dap_kem_sharedsecret_size(a_alg);

    uint8_t *pk = malloc(pk_sz), *sk = malloc(sk_sz);
    uint8_t *ct = malloc(ct_sz), *ss1 = malloc(ss_sz), *ss2 = malloc(ss_sz);

    printf("=== %s raw ===\n", name);
    BENCH("keygen", dap_kem_keypair(a_alg, pk, sk));
    dap_kem_keypair(a_alg, pk, sk);
    BENCH("encaps", dap_kem_encaps(a_alg, ct, ss1, pk));
    dap_kem_encaps(a_alg, ct, ss1, pk);
    BENCH("decaps", dap_kem_decaps(a_alg, ss2, ct, sk));
    if (memcmp(ss1, ss2, ss_sz) != 0)
        printf("  !!! CORRECTNESS FAILURE: ss1 != ss2 !!!\n");
    else
        printf("  Correctness: OK\n");

    printf("\n=== %s ctx ===\n", name);
    dap_kem_ctx_t ctx;
    dap_kem_keypair(a_alg, pk, sk);
    dap_kem_ctx_create(&ctx, a_alg, pk, sk);
    BENCH("enc_ctx", dap_kem_ctx_encaps(ct, ss1, &ctx));
    dap_kem_ctx_encaps(ct, ss1, &ctx);
    BENCH("dec_ctx", dap_kem_ctx_decaps(ss2, ct, &ctx));
    if (memcmp(ss1, ss2, ss_sz) != 0)
        printf("  !!! CORRECTNESS FAILURE: ss1 != ss2 !!!\n");
    else
        printf("  Correctness: OK\n");
    dap_kem_ctx_destroy(&ctx);

    free(pk); free(sk); free(ct); free(ss1); free(ss2);
    printf("\n");
}

int main(void) {
    s_bench_variant(DAP_KEM_ALG_ML_KEM_512);
    s_bench_variant(DAP_KEM_ALG_ML_KEM_768);
    s_bench_variant(DAP_KEM_ALG_ML_KEM_1024);
    return 0;
}
