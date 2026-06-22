/*
 * bench_chipmunk_ring.c — Ring sign/verify benchmark.
 *
 * Measures non-interactive lattice ring signature (chipmunk_ring)
 * sign and verify for N=1,2,4.
 *
 * Build:
 *   cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON ..
 *   cmake --build . --target bench_chipmunk_ring
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include "sig/chipmunk/chipmunk_ring.h"
#include "sig/lotrs/lotrs_params.h"

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define WARMUP_SIGN  2
#define ITERS_SIGN   10
#define WARMUP_VER   4
#define ITERS_VER    20

#define BARRIER() __asm__ volatile("" ::: "memory")

static void s_fill_seed(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i)
        a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
}

static int s_sign_retry(chipmunk_ring_sig_t *a_sig,
                        const lotrs_params_t *a_par,
                        const chipmunk_ring_table_t *a_ring,
                        const chipmunk_ring_sk_t *a_sk,
                        uint32_t a_signer_idx,
                        const uint8_t *a_msg, size_t a_msg_len,
                        const uint8_t a_seed[32])
{
    uint8_t l_seed[32];
    memcpy(l_seed, a_seed, 32u);
    for (int i = 0; i < 64; ++i) {
        int rc = chipmunk_ring_sign(a_sig, a_par, a_ring, a_sk, a_signer_idx,
                                    a_msg, a_msg_len, l_seed);
        if (rc == 0) return 0;
        if (rc != -EAGAIN) return rc;
        l_seed[0] ^= (uint8_t)(i + 1);
    }
    return -EAGAIN;
}

static void bench_ring_n(const lotrs_params_t *a_par, uint32_t a_N)
{
    printf("\n=== Ring N=%u (d=%u, q=%lu, k=%u, l=%u) ===\n",
           a_N, a_par->d, (unsigned long)a_par->q, a_par->k, a_par->l);

    /* Keygen. */
    chipmunk_ring_keypair_t *l_kps = calloc(a_N, sizeof(chipmunk_ring_keypair_t));
    for (uint32_t i = 0u; i < a_N; ++i) {
        uint8_t seed[32];
        s_fill_seed(seed, sizeof(seed), (uint8_t)i);
        int rc = chipmunk_ring_keygen(&l_kps[i], a_par, seed);
        if (rc != 0) {
            fprintf(stderr, "keygen[%u] failed: %d\n", i, rc);
            free(l_kps);
            return;
        }
    }

    /* Build ring. */
    chipmunk_ring_table_t l_ring = { .N = a_N };
    l_ring.pks = calloc(a_N, sizeof(chipmunk_ring_pk_t));
    for (uint32_t i = 0u; i < a_N; ++i) {
        l_ring.pks[i].a_hat = lotrs_polyvec_alloc(a_par, a_par->k);
        for (uint32_t j = 0u; j < a_par->k; ++j)
            lotrs_poly_copy(l_ring.pks[i].a_hat.polys[j],
                            l_kps[i].pk.a_hat.polys[j], a_par);
    }

    const uint8_t msg[] = "bench-ring-message";
    uint8_t sign_seed[32];
    s_fill_seed(sign_seed, sizeof(sign_seed), 0xBB);

    /* --- Sign benchmark --- */
    chipmunk_ring_sig_t l_sig = {0};
    int rc = s_sign_retry(&l_sig, a_par, &l_ring, &l_kps[0].sk, 0,
                          msg, sizeof(msg) - 1, sign_seed);
    if (rc != 0) {
        fprintf(stderr, "sign failed: %d\n", rc);
        goto cleanup;
    }
    chipmunk_ring_sig_free(&l_sig);

    /* Warmup. */
    for (int i = 0; i < WARMUP_SIGN; ++i) {
        chipmunk_ring_sig_t s = {0};
        s_sign_retry(&s, a_par, &l_ring, &l_kps[0].sk, 0,
                     msg, sizeof(msg) - 1, sign_seed);
        chipmunk_ring_sig_free(&s);
    }

    /* Timed sign. */
    uint64_t t0 = now_ns();
    for (int i = 0; i < ITERS_SIGN; ++i) {
        BARRIER();
        chipmunk_ring_sig_t s = {0};
        s_sign_retry(&s, a_par, &l_ring, &l_kps[0].sk, 0,
                     msg, sizeof(msg) - 1, sign_seed);
        BARRIER();
        chipmunk_ring_sig_free(&s);
    }
    uint64_t t_sign = now_ns() - t0;

    /* --- Verify benchmark --- */
    rc = s_sign_retry(&l_sig, a_par, &l_ring, &l_kps[0].sk, 0,
                      msg, sizeof(msg) - 1, sign_seed);
    if (rc != 0) goto cleanup;

    /* Warmup. */
    for (int i = 0; i < WARMUP_VER; ++i) {
        BARRIER();
        chipmunk_ring_verify(&l_sig, a_par, &l_ring, msg, sizeof(msg) - 1);
        BARRIER();
    }

    /* Timed verify. */
    uint64_t t1 = now_ns();
    for (int i = 0; i < ITERS_VER; ++i) {
        BARRIER();
        chipmunk_ring_verify(&l_sig, a_par, &l_ring, msg, sizeof(msg) - 1);
        BARRIER();
    }
    uint64_t t_ver = now_ns() - t1;

    printf("  sig_bytes : %zu\n", l_sig.len);
    printf("  sign      : %.2f ms/op  (%d iters)\n",
           (double)t_sign / (double)ITERS_SIGN / 1e6, ITERS_SIGN);
    printf("  verify    : %.2f ms/op  (%d iters)\n",
           (double)t_ver / (double)ITERS_VER / 1e6, ITERS_VER);

    chipmunk_ring_sig_free(&l_sig);

cleanup:
    chipmunk_ring_table_free(&l_ring);
    for (uint32_t i = 0u; i < a_N; ++i)
        chipmunk_ring_keypair_free(&l_kps[i]);
    free(l_kps);
}

int main(void)
{
    printf("Chipmunk Ring Benchmark\n");
    printf("=======================\n");

    printf("\n--- TEST params (d=%u, q=%lu, k=%u, l=%u) ---\n",
           LOTRS_PARAMS_TEST.d, (unsigned long)LOTRS_PARAMS_TEST.q,
           LOTRS_PARAMS_TEST.k, LOTRS_PARAMS_TEST.l);
    bench_ring_n(&LOTRS_PARAMS_TEST, 1u);
    bench_ring_n(&LOTRS_PARAMS_TEST, 2u);
    bench_ring_n(&LOTRS_PARAMS_TEST, 4u);

    printf("\n--- PRODUCTION params (d=%u, q=%lu, k=%u, l=%u) ---\n",
           LOTRS_PARAMS_RING.d, (unsigned long)LOTRS_PARAMS_RING.q,
           LOTRS_PARAMS_RING.k, LOTRS_PARAMS_RING.l);
    bench_ring_n(&LOTRS_PARAMS_RING, 1u);
    bench_ring_n(&LOTRS_PARAMS_RING, 2u);
    bench_ring_n(&LOTRS_PARAMS_RING, 4u);

    printf("\n--- OPTIMIZED params (d=%u, q=%lu, k=%u, l=%u) ---\n",
           LOTRS_PARAMS_RING_OPT.d, (unsigned long)LOTRS_PARAMS_RING_OPT.q,
           LOTRS_PARAMS_RING_OPT.k, LOTRS_PARAMS_RING_OPT.l);
    bench_ring_n(&LOTRS_PARAMS_RING_OPT, 1u);
    bench_ring_n(&LOTRS_PARAMS_RING_OPT, 2u);
    bench_ring_n(&LOTRS_PARAMS_RING_OPT, 4u);

    printf("\nDone.\n");
    return 0;
}
