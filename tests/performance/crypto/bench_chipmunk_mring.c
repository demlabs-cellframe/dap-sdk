/*
 * bench_chipmunk_mring.c — MRNG sign/verify microbenchmark.
 *
 * M7.3: measures sign and verify latency at N ∈ {2, 4, 8, 16}, t = N/2.
 * Uses clock_gettime(CLOCK_MONOTONIC) for nanosecond timing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "chipmunk/chipmunk_ring.h"
#include "sig/chipmunk/chipmunk_mring.h"
#include "chipmunk/chipmunk_lrs.h"

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

static void s_bench_ring(uint32_t a_N)
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

    printf("=== MRNG N=%u t=%u (fold_depth=%u, wire=%u B) ===\n",
           a_N, T, depth, wire);

    /* --- Sign benchmark --- */
    uint8_t *sig = NULL;
    size_t sig_sz = 0u;

    /* Warmup + correctness gate. */
    chipmunk_ring_error_t s_rc = chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, a_N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);
    if (s_rc != CHIPMUNK_RING_OK) {
        printf("  SKIPPED: sign returned rc=%d\n\n", (int)s_rc);
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

    /* Timed sign. */
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
    printf("  %-12s %8d iters  %9.3f ms/op\n",
           "sign", ITERS_SIGN, (double)dt_sign / (ITERS_SIGN * 1000000.0));

    /* Produce one signature for verify benchmark. */
    chipmunk_ring_sign_to_bytes(
        &sig, &sig_sz, ptrs, T, ring, a_N, T,
        msg, sizeof(msg) - 1u, NULL, 0u, seeds);

    /* Warmup verify. */
    for (int w = 0; w < WARMUP_VER; w++) {
        chipmunk_ring_verify_from_bytes(
            sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);
    }

    /* Timed verify. */
    t0 = now_ns();
    for (int i = 0; i < ITERS_VER; i++) {
        BARRIER();
        chipmunk_ring_verify_from_bytes(
            sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);
        BARRIER();
    }
    uint64_t dt_ver = now_ns() - t0;
    printf("  %-12s %8d iters  %9.3f ms/op\n",
           "verify", ITERS_VER, (double)dt_ver / (ITERS_VER * 1000000.0));

    /* Correctness check. */
    chipmunk_ring_error_t v_rc = chipmunk_ring_verify_from_bytes(
        sig, sig_sz, ring, a_N, msg, sizeof(msg) - 1u, NULL, 0u);
    printf("  Correctness: %s\n\n",
           v_rc == CHIPMUNK_RING_OK ? "OK" : "FAIL");

    free(sig);
    free(seeds);
    free(ptrs);
    free(sks);
    free(ring);
}

int main(void)
{
    s_bench_ring(2u);
    s_bench_ring(4u);
    s_bench_ring(8u);
    s_bench_ring(16u);
    return 0;
}
