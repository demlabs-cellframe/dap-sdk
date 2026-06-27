/*
 * bench_chipmunk_mring.c — MRNG sign/verify benchmark with competitor comparison.
 *
 * Compares:
 *   - MRNG: log-N compressed threshold ring signature (N=2,4,8,16)
 *   - LRS:  1-of-N linkable ring signature (N=2,4,8,16)
 *   - ML-DSA-65 (Dilithium3): NIST standard regular signature (liboqs, optional)
 *
 * Build:
 *   cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -DBUILD_DAP_SDK_TESTS=ON ..
 *   cmake --build . --target bench_chipmunk_mring
 *
 * For ML-DSA comparison, first run ./download_competitors.sh
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"
#include "sig/lotrs/lotrs.h"
#include "sig/lotrs/lotrs_params.h"
#include "sig/lotrs/lotrs_ring.h"
#include "sig/lotrs/lotrs_wire.h"

#ifdef HAVE_LIBOQS
#include <oqs/oqs.h>
#endif

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define WARMUP_SIGN  4
#define ITERS_SIGN   20
#define WARMUP_VER   8
#define ITERS_VER    50

#define BARRIER() __asm__ volatile("" ::: "memory")

static void s_fill_seed(uint8_t *a_out, size_t a_len, uint8_t a_salt)
{
    for (size_t i = 0u; i < a_len; ++i) {
        a_out[i] = (uint8_t)(0x42u ^ (uint8_t)i ^ a_salt);
    }
}

static void s_make_keypair(chipmunk_lrs_public_key_t *a_pk,
                           chipmunk_lrs_secret_key_t *a_sk,
                           uint8_t a_salt)
{
    uint8_t sk_seed[CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(sk_seed, sizeof(sk_seed), a_salt);
    chipmunk_lrs_keypair_from_seeds(a_pk, a_sk, sk_seed);
}

/* =========================================================================
 * MRNG benchmark
 * ======================================================================= */

static void s_bench_mring(uint32_t a_N)
{
    const uint32_t T = a_N / 2u;
    if (T < 1u) return;

    chipmunk_lrs_public_key_t *ring =
        calloc(a_N, sizeof(chipmunk_lrs_public_key_t));
    chipmunk_lrs_secret_key_t *sks =
        calloc(T, sizeof(chipmunk_lrs_secret_key_t));
    const chipmunk_lrs_secret_key_t **ptrs =
        calloc(T, sizeof(chipmunk_lrs_secret_key_t *));

    for (uint32_t i = 0u; i < a_N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x10u + i));
    }
    for (uint32_t i = 0u; i < T; ++i) {
        s_make_keypair(&ring[i], &sks[i], (uint8_t)(0xA0u + i));
        ptrs[i] = &sks[i];
    }

    const size_t seed_bytes = T * CHIPMUNK_LRS_SEED_BYTES;
    uint8_t *seeds = malloc(seed_bytes);
    s_fill_seed(seeds, seed_bytes, 0x99u);

    const uint8_t msg[] = "bench-mring";
    const uint32_t depth = chipmunk_mring_fold_depth_for(a_N);
    const uint32_t wire = chipmunk_mring_wire_size(depth);

    printf("  MRNG  N=%-4u t=%-2u  wire=%6u B  ", a_N, T, wire);

    /* Sign gate. */
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;
    chipmunk_ring_error_t s_rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, a_N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    if (s_rc != CHIPMUNK_RING_OK) {
        printf("SKIP (sign rc=%d)\n", (int)s_rc);
        free(sig); free(seeds); free(ptrs); free(sks); free(ring);
        return;
    }
    free(sig); sig = NULL;

    for (int w = 0; w < WARMUP_SIGN - 1; w++) {
        chipmunk_ring_sign_to_bytes(
            &sig, &sig_sz, ptrs, T, ring, a_N, T,
            msg, sizeof(msg) - 1u, NULL, 0u, seeds);
        free(sig); sig = NULL;
    }

    uint64_t t0 = now_ns();
    for (int i = 0; i < ITERS_SIGN; i++) {
        BARRIER();
        chipmunk_ring_sign_to_bytes(
            &sig, &sig_sz, ptrs, T, ring, a_N, T,
            msg, sizeof(msg) - 1u, NULL, 0u, seeds);
        BARRIER();
        free(sig); sig = NULL;
    }
    uint64_t dt_sign = now_ns() - t0;

    chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, a_N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);

    for (int w = 0; w < WARMUP_VER; w++) {
        chipmunk_ring_verify_from_bytes(
            sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS_VER; i++) {
        BARRIER();
        chipmunk_ring_verify_from_bytes(
            sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);
        BARRIER();
    }
    uint64_t dt_ver = now_ns() - t0;

    chipmunk_ring_error_t v_rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);

    printf("sign=%7.3f ms  verify=%6.3f ms  %s\n",
           (double)dt_sign / (ITERS_SIGN * 1000000.0),
           (double)dt_ver / (ITERS_VER * 1000000.0),
           v_rc == CHIPMUNK_RING_OK ? "OK" : "FAIL");

    free(sig); free(seeds); free(ptrs); free(sks); free(ring);
}

/* =========================================================================
 * LRS benchmark (1-of-N linkable ring signature)
 * ======================================================================= */

static void s_bench_lrs(uint32_t a_N)
{
    chipmunk_lrs_public_key_t *ring =
        calloc(a_N, sizeof(chipmunk_lrs_public_key_t));
    chipmunk_lrs_secret_key_t sk;

    for (uint32_t i = 0u; i < a_N; ++i) {
        chipmunk_lrs_secret_key_t tmp;
        s_make_keypair(&ring[i], &tmp, (uint8_t)(0x20u + i));
    }
    s_make_keypair(&ring[0], &sk, 0xB0u);

    const uint8_t msg[] = "bench-lrs";
    uint8_t randomness[CHIPMUNK_LRS_SEED_BYTES];
    s_fill_seed(randomness, sizeof(randomness), 0x77u);

    const size_t sig_size = chipmunk_lrs_signature_size(a_N);
    printf("  LRS   N=%-4u              wire=%6zu B  ", a_N, sig_size);

    uint8_t *sig = malloc(sig_size);

    int rc = chipmunk_lrs_sign(sig, sig_size, &sk, ring, a_N,
                               msg, sizeof(msg) - 1u, randomness);
    if (rc != 0) {
        printf("SKIP (sign rc=%d)\n", rc);
        free(sig); free(ring);
        return;
    }

    for (int w = 0; w < WARMUP_SIGN - 1; w++) {
        chipmunk_lrs_sign(sig, sig_size, &sk, ring, a_N,
                          msg, sizeof(msg) - 1u, randomness);
    }

    uint64_t t0 = now_ns();
    for (int i = 0; i < ITERS_SIGN; i++) {
        BARRIER();
        chipmunk_lrs_sign(sig, sig_size, &sk, ring, a_N,
                          msg, sizeof(msg) - 1u, randomness);
        BARRIER();
    }
    uint64_t dt_sign = now_ns() - t0;

    chipmunk_lrs_sign(sig, sig_size, &sk, ring, a_N,
                      msg, sizeof(msg) - 1u, randomness);

    for (int w = 0; w < WARMUP_VER; w++) {
        chipmunk_lrs_verify(sig, sig_size, ring, a_N,
                            msg, sizeof(msg) - 1u);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS_VER; i++) {
        BARRIER();
        chipmunk_lrs_verify(sig, sig_size, ring, a_N,
                            msg, sizeof(msg) - 1u);
        BARRIER();
    }
    uint64_t dt_ver = now_ns() - t0;

    int v_rc = chipmunk_lrs_verify(sig, sig_size, ring, a_N,
                                   msg, sizeof(msg) - 1u);

    printf("sign=%7.3f ms  verify=%6.3f ms  %s\n",
           (double)dt_sign / (ITERS_SIGN * 1000000.0),
           (double)dt_ver / (ITERS_VER * 1000000.0),
           v_rc == 0 ? "OK" : "FAIL");

    free(sig); free(ring);
}

/* =========================================================================
 * LoTRS benchmark (threshold ring signature, TEST params)
 * ======================================================================= */

static void s_bench_lotrs(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;

    lotrs_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    s_fill_seed(l_seed, 32, 0xA0);
    lotrs_keygen(&l_kp, l_par, l_seed);

    /* Build ring with single PK. */
    lotrs_ring_pk_t l_ring = {0};
    l_ring.N = 1; l_ring.T = 1;
    l_ring.pks = calloc(1, sizeof(lotrs_pk_t));
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0u; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    const uint8_t msg[] = "bench-lotrs";
    uint32_t l_wire = lotrs_wire_size(l_par);

    printf("  LoTRS d=%u N=%u t=%u  wire=%u B  ",
           l_par->d, l_ring.N, l_ring.T, l_wire);

    /* Sign gate. */
    lotrs_signature_t l_sig = {0};
    uint8_t l_sign_seed[32];
    s_fill_seed(l_sign_seed, 32, 0xBB);
    int l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                          msg, sizeof(msg) - 1, l_sign_seed);
    if (l_rc == -2) {
        l_sign_seed[0] ^= 0xFF;
        l_rc = lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                          msg, sizeof(msg) - 1, l_sign_seed);
    }
    if (l_rc != 0) {
        printf("SKIP (sign rc=%d)\n", l_rc);
        lotrs_ring_pk_free(&l_ring); lotrs_pk_free(&l_kp.pk); lotrs_sk_free(&l_kp.sk);
        return;
    }
    lotrs_signature_free(&l_sig);

    /* Warmup. */
    for (int w = 0; w < WARMUP_SIGN; w++) {
        l_sign_seed[0] ^= (uint8_t)w;
        lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                   msg, sizeof(msg) - 1, l_sign_seed);
        lotrs_signature_free(&l_sig);
    }

    /* Timed sign. */
    uint64_t t0 = now_ns();
    for (int i = 0; i < ITERS_SIGN; i++) {
        BARRIER();
        l_sign_seed[0] ^= (uint8_t)i;
        lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
                   msg, sizeof(msg) - 1, l_sign_seed);
        BARRIER();
        lotrs_signature_free(&l_sig);
    }
    uint64_t dt_sign = now_ns() - t0;

    /* Sign one for verify. */
    l_sign_seed[0] = 0xCC;
    lotrs_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0u,
               msg, sizeof(msg) - 1, l_sign_seed);

    /* Warmup verify. */
    for (int w = 0; w < WARMUP_VER; w++) {
        lotrs_verify(&l_sig, l_par, &l_ring, msg, sizeof(msg) - 1);
    }

    /* Timed verify. */
    t0 = now_ns();
    for (int i = 0; i < ITERS_VER; i++) {
        BARRIER();
        lotrs_verify(&l_sig, l_par, &l_ring, msg, sizeof(msg) - 1);
        BARRIER();
    }
    uint64_t dt_ver = now_ns() - t0;

    int v_rc = lotrs_verify(&l_sig, l_par, &l_ring, msg, sizeof(msg) - 1);
    printf("sign=%7.3f ms  verify=%6.3f ms  %s\n",
           (double)dt_sign / (ITERS_SIGN * 1000000.0),
           (double)dt_ver / (ITERS_VER * 1000000.0),
           v_rc == 0 ? "OK" : "FAIL");

    lotrs_signature_free(&l_sig);
    lotrs_ring_pk_free(&l_ring);
    lotrs_pk_free(&l_kp.pk);
    lotrs_sk_free(&l_kp.sk);
}

/* =========================================================================
 * ML-DSA-65 (Dilithium3) benchmark via liboqs
 * ======================================================================= */

#ifdef HAVE_LIBOQS
static void s_bench_mldsa(void)
{
    OQS_SIG *sig_obj = OQS_SIG_new(OQS_SIG_alg_ml_dsa_65);
    if (!sig_obj) {
        printf("  ML-DSA-65: not available\n");
        return;
    }

    const size_t pk_sz = sig_obj->length_public_key;
    const size_t sk_sz = sig_obj->length_secret_key;
    const size_t sig_sz = sig_obj->length_signature;

    printf("  ML-DSA-65           pk=%4zu B  sk=%4zu B  sig=%5zu B  ",
           pk_sz, sk_sz, sig_sz);

    uint8_t *pk = malloc(pk_sz);
    uint8_t *sk = malloc(sk_sz);
    uint8_t *sig = malloc(sig_sz);
    const uint8_t msg[] = "bench-mldsa";
    size_t sig_len = 0;

    OQS_SIG_keypair(sig_obj, pk, sk);

    for (int w = 0; w < WARMUP_SIGN; w++) {
        OQS_SIG_sign(sig_obj, sig, &sig_len, msg, sizeof(msg) - 1, sk);
    }

    uint64_t t0 = now_ns();
    for (int i = 0; i < ITERS_SIGN; i++) {
        BARRIER();
        OQS_SIG_sign(sig_obj, sig, &sig_len, msg, sizeof(msg) - 1, sk);
        BARRIER();
    }
    uint64_t dt_sign = now_ns() - t0;

    OQS_SIG_sign(sig_obj, sig, &sig_len, msg, sizeof(msg) - 1, sk);

    for (int w = 0; w < WARMUP_VER; w++) {
        OQS_SIG_verify(sig_obj, msg, sizeof(msg) - 1, sig, sig_len, pk);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS_VER; i++) {
        BARRIER();
        OQS_SIG_verify(sig_obj, msg, sizeof(msg) - 1, sig, sig_len, pk);
        BARRIER();
    }
    uint64_t dt_ver = now_ns() - t0;

    OQS_STATUS v_rc = OQS_SIG_verify(sig_obj, msg, sizeof(msg) - 1,
                                      sig, sig_len, pk);

    printf("sign=%7.3f ms  verify=%6.3f ms  %s\n",
           (double)dt_sign / (ITERS_SIGN * 1000000.0),
           (double)dt_ver / (ITERS_VER * 1000000.0),
           v_rc == OQS_SUCCESS ? "OK" : "FAIL");

    free(pk); free(sk); free(sig);
    OQS_SIG_free(sig_obj);
}
#endif

/* =========================================================================
 * Size comparison table
 * ======================================================================= */

static void s_print_size_table(void)
{
    printf("\n=== Signature Size Comparison ===\n\n");
    printf("%-24s  %8s  %8s  %8s  %10s\n",
           "Scheme", "PK (B)", "SK (B)", "Sig (B)", "Anonymity");
    printf("%-24s  %8s  %8s  %8s  %10s\n",
           "------------------------", "--------", "--------",
           "--------", "----------");

    /* MRNG sizes at various N. */
    static const struct { uint32_t n, t; } mring_sizes[] = {
        {2, 1}, {4, 2}, {8, 4}, {16, 8}, {32, 16}, {64, 32}, {128, 64}, {256, 128},
    };
    for (size_t i = 0; i < sizeof(mring_sizes) / sizeof(mring_sizes[0]); ++i) {
        const uint32_t N = mring_sizes[i].n;
        const uint32_t T = mring_sizes[i].t;
        const uint32_t depth = chipmunk_mring_fold_depth_for(N);
        const uint32_t wire = chipmunk_mring_wire_size(depth);
        char label[32];
        snprintf(label, sizeof(label), "MRNG N=%u t=%u", N, T);
        printf("%-24s  %8u  %8u  %8u  %10s\n",
               label, 1424u, 1456u, wire, "ring");
    }

    /* LRS sizes at various N. */
    printf("\n");
    static const uint32_t lrs_sizes[] = {2, 4, 8, 16, 32, 64};
    for (size_t i = 0; i < sizeof(lrs_sizes) / sizeof(lrs_sizes[0]); ++i) {
        const uint32_t N = lrs_sizes[i];
        const size_t sig = chipmunk_lrs_signature_size(N);
        char label[32];
        snprintf(label, sizeof(label), "LRS N=%u", N);
        printf("%-24s  %8u  %8u  %8zu  %10s\n",
               label, 1424u, 1456u, sig, "ring");
    }

    /* Competitor reference sizes. */
    printf("\n");
#ifdef HAVE_LIBOQS
    printf("%-24s  %8u  %8u  %8u  %10s\n",
           "ML-DSA-44 (Dil2)", 1312u, 2528u, 2420u, "none");
    printf("%-24s  %8u  %8u  %8u  %10s\n",
           "ML-DSA-65 (Dil3)", 1952u, 4000u, 3293u, "none");
    printf("%-24s  %8u  %8u  %8u  %10s\n",
           "ML-DSA-87 (Dil5)", 2592u, 4864u, 4595u, "none");
#else
    printf("%-24s  %8u  %8u  %8u  %10s\n",
           "ML-DSA-65 (ref)", 1952u, 4000u, 3293u, "none");
#endif

    /* LoTRS (lattice threshold ring, Rust). */
    printf("%-24s  %8s  %8s  %8u  %10s\n",
           "LoTRS N=4 t=2 (ref)", "?", "?", 877u, "ring");
    printf("%-24s  %8s  %8s  %10u  %10s\n",
           "LoTRS N=32 t=4 (ref)", "?", "?", 24464u, "ring");
    printf("%-24s  %8s  %8s  %10u  %10s\n",
           "LoTRS N=32 t=16 (ref)", "?", "?", 25580u, "ring");

    /* LoTRS TEST (our implementation). */
    {
        const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;
        uint32_t l_wire = lotrs_wire_size(l_par);
        printf("%-24s  %8u  %8u  %8u  %10s\n",
               "LoTRS d=32 N=4 t=2", 0u, 0u, l_wire, "ring");
    }

    /* RingTAIL (LWE threshold ring, Go). */
    printf("%-24s  %8s  %8s  %8s  %10s\n",
           "RingTAIL N=4 (ref)", "?", "?", "~8KB", "ring");

    /* Raptor (Falcon-based, N=50). */
    printf("%-24s  %8u  %8u  %10u  %10s\n",
           "Raptor N=50 (ref)", 897u, 4097u,
           50u * 20480u + 690u, "ring");

    printf("\n");
}

/* =========================================================================
 * main
 * ======================================================================= */

int main(void)
{
    printf("============================================================\n");
    printf("  MRNG Benchmark — Chipmunk Ring Signature Performance\n");
    printf("============================================================\n\n");

    s_print_size_table();

    printf("=== Timing Benchmarks (Release build) ===\n\n");

    printf("--- MRNG (log-N compressed threshold ring) ---\n");
    s_bench_mring(2u);
    s_bench_mring(4u);
    s_bench_mring(8u);
    s_bench_mring(16u);

    printf("\n--- LRS (1-of-N linkable ring) ---\n");
    s_bench_lrs(2u);
    s_bench_lrs(4u);
    s_bench_lrs(8u);
    s_bench_lrs(16u);

    printf("\n--- LoTRS (lattice threshold ring, TEST params) ---\n");
    s_bench_lotrs();

#ifdef HAVE_LIBOQS
    printf("\n--- ML-DSA (NIST standard, no anonymity) ---\n");
    s_bench_mldsa();
#endif

    printf("\n============================================================\n");
    return 0;
}
