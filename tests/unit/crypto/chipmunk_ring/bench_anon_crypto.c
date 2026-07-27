/*
 * bench_anon_crypto.c — Performance benchmarks for anonymous crypto primitives.
 *
 * Measures: SNARK prove/verify, Pedersen commit/verify, range proof,
 * key image generation, HOTS sign/verify, aggregation.
 */

#include <dap_common.h>
#include <dap_hash_sha3.h>
#include <dap_rand.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include "sig/chipmunk/chipmunk_snark.h"
#include "sig/chipmunk/chipmunk_pedersen.h"
#include "sig/chipmunk/chipmunk_range_proof.h"
#include "sig/chipmunk/chipmunk_hots.h"
#include "sig/chipmunk/chipmunk_ring.h"
#include "sig/lotrs/lotrs_params.h"

#define LOG_TAG "bench_anon_crypto"
#define BENCH_ITERS 10

static double time_ms(struct timespec *a_start, struct timespec *a_end)
{
    return (double)(a_end->tv_sec - a_start->tv_sec) * 1000.0 +
           (double)(a_end->tv_nsec - a_start->tv_nsec) / 1000000.0;
}

static void bench_snark_prove_verify(void)
{
    chipmunk_snark_ctx_t l_ctx;
    chipmunk_snark_init(&l_ctx);

    chipmunk_lrs_public_key_t l_ring[8];
    memset(l_ring, 0, sizeof(l_ring));

    chipmunk_snark_statement_t l_stmt;
    memset(&l_stmt, 0, sizeof(l_stmt));
    l_stmt.ring = l_ring;
    l_stmt.ring_size = 8;
    const uint8_t l_msg[] = "bench";
    l_stmt.message = l_msg;
    l_stmt.message_size = 5;

    chipmunk_snark_witness_t l_witness;
    memset(&l_witness, 0, sizeof(l_witness));
    l_witness.signer_index = 3;
    l_witness.indicator.coeffs[3] = 1;

    struct timespec l_start, l_end;
    double l_prove_total = 0, l_verify_total = 0;

    for (int i = 0; i < BENCH_ITERS; ++i) {
        chipmunk_snark_proof_t l_proof;
        memset(&l_proof, 0, sizeof(l_proof));

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_snark_prove(&l_proof, &l_ctx, &l_stmt, &l_witness);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_prove_total += time_ms(&l_start, &l_end);

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_snark_verify(&l_proof, &l_ctx, &l_stmt);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_verify_total += time_ms(&l_start, &l_end);

        chipmunk_snark_proof_free(&l_proof);
    }

    log_it(L_INFO, "SNARK prove:   %.2f ms/iter (N=8)", l_prove_total / BENCH_ITERS);
    log_it(L_INFO, "SNARK verify:  %.2f ms/iter (N=8)", l_verify_total / BENCH_ITERS);
    chipmunk_snark_ctx_free(&l_ctx);
}

static void bench_pedersen_commit(void)
{
    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42;
    chipmunk_pedersen_init(&l_params, l_seed);

    struct timespec l_start, l_end;
    double l_total = 0;

    for (int i = 0; i < BENCH_ITERS; ++i) {
        chipmunk_pedersen_commit_t l_commit;
        uint8_t l_rand[32], l_value[CHIPMUNK_PEDERSEN_VALUE_BYTES];
        dap_random_bytes(l_rand, 32);
        memset(l_value, 0, sizeof(l_value));
        { uint64_t v = 1000000 + (uint64_t)i; memcpy(l_value, &v, sizeof(v)); }

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_pedersen_commit(&l_commit, &l_params, l_value, l_rand);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_total += time_ms(&l_start, &l_end);
    }

    log_it(L_INFO, "Pedersen commit: %.2f ms/iter", l_total / BENCH_ITERS);
}

static void bench_range_proof(void)
{
    chipmunk_pedersen_params_t l_params;
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42;
    chipmunk_pedersen_init(&l_params, l_seed);

    chipmunk_pedersen_commit_t l_commit;
    uint8_t l_rand[32], l_value[CHIPMUNK_PEDERSEN_VALUE_BYTES];
    for (int i = 0; i < 32; ++i) l_rand[i] = 0xAA;
    memset(l_value, 0, sizeof(l_value));
    { uint64_t v = 1000000; memcpy(l_value, &v, sizeof(v)); }
    chipmunk_pedersen_commit(&l_commit, &l_params, l_value, l_rand);

    struct timespec l_start, l_end;
    double l_prove_total = 0, l_verify_total = 0;

    for (int i = 0; i < BENCH_ITERS; ++i) {
        chipmunk_range_proof_t l_proof;
        memset(&l_proof, 0, sizeof(l_proof));

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_range_proof_prove(&l_proof, &l_params, &l_commit, l_value, l_rand);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_prove_total += time_ms(&l_start, &l_end);

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_range_proof_verify(&l_proof, &l_params, &l_commit);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_verify_total += time_ms(&l_start, &l_end);

        chipmunk_range_proof_free(&l_proof);
    }

    log_it(L_INFO, "Range proof prove:  %.2f ms/iter", l_prove_total / BENCH_ITERS);
    log_it(L_INFO, "Range proof verify: %.2f ms/iter", l_verify_total / BENCH_ITERS);
}

static void bench_hots_sign_verify(void)
{
    chipmunk_hots_params_t l_params;
    chipmunk_hots_setup(&l_params);

    chipmunk_hots_pk_t l_pk;
    chipmunk_hots_sk_t l_sk;
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42;
    chipmunk_hots_keygen(l_seed, 0, &l_params, &l_pk, &l_sk);

    const uint8_t l_msg[] = "bench-message";

    struct timespec l_start, l_end;
    double l_sign_total = 0, l_verify_total = 0;

    for (int i = 0; i < BENCH_ITERS; ++i) {
        chipmunk_hots_signature_t l_sig;

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_hots_sign(&l_sk, l_msg, sizeof(l_msg), &l_sig);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_sign_total += time_ms(&l_start, &l_end);

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_hots_verify(&l_pk, l_msg, sizeof(l_msg), &l_sig, &l_params);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_verify_total += time_ms(&l_start, &l_end);
    }

    log_it(L_INFO, "HOTS sign:   %.2f ms/iter", l_sign_total / BENCH_ITERS);
    log_it(L_INFO, "HOTS verify: %.2f ms/iter", l_verify_total / BENCH_ITERS);
}

static void bench_ring_sign_verify(void)
{
    const lotrs_params_t *l_par = &LOTRS_PARAMS_TEST;

    chipmunk_ring_keypair_t l_kp = {0};
    uint8_t l_seed[32];
    for (int i = 0; i < 32; ++i) l_seed[i] = 0x42;
    chipmunk_ring_keygen(&l_kp, l_par, l_seed);

    chipmunk_ring_table_t l_ring = {0};
    l_ring.N = 1;
    l_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, 1);
    l_ring.pks[0].a_hat = lotrs_polyvec_alloc(l_par, l_par->k);
    for (uint32_t i = 0; i < l_par->k; ++i) {
        lotrs_poly_copy(l_ring.pks[0].a_hat.polys[i], l_kp.pk.a_hat.polys[i], l_par);
    }

    const uint8_t l_msg[] = "bench";
    uint8_t l_sign_seed[32];
    for (int i = 0; i < 32; ++i) l_sign_seed[i] = 0xBB;

    struct timespec l_start, l_end;
    double l_sign_total = 0, l_verify_total = 0;

    for (int i = 0; i < BENCH_ITERS; ++i) {
        chipmunk_ring_sig_t l_sig = {0};

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_ring_sign(&l_sig, l_par, &l_ring, &l_kp.sk, 0, l_msg, sizeof(l_msg), l_sign_seed);
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_sign_total += time_ms(&l_start, &l_end);

        clock_gettime(CLOCK_MONOTONIC, &l_start);
        chipmunk_ring_verify(&l_sig, l_par, &l_ring, l_msg, sizeof(l_msg));
        clock_gettime(CLOCK_MONOTONIC, &l_end);
        l_verify_total += time_ms(&l_start, &l_end);

        chipmunk_ring_sig_free(&l_sig);
    }

    log_it(L_INFO, "Ring sign (N=1, TEST):   %.2f ms/iter", l_sign_total / BENCH_ITERS);
    log_it(L_INFO, "Ring verify (N=1, TEST): %.2f ms/iter", l_verify_total / BENCH_ITERS);

    chipmunk_ring_keypair_free(&l_kp);
    lotrs_polyvec_free(&l_ring.pks[0].a_hat);
    DAP_DELETE(l_ring.pks);
}

int main(void)
{
    dap_set_appname("bench_anon_crypto");
    dap_common_init("bench_anon_crypto", NULL);

    log_it(L_INFO, "=== Anonymous Crypto Benchmarks (%d iterations) ===", BENCH_ITERS);

    bench_snark_prove_verify();
    bench_pedersen_commit();
    bench_range_proof();
    bench_hots_sign_verify();
    bench_ring_sign_verify();

    log_it(L_INFO, "=== Benchmarks complete ===");
    dap_common_deinit();
    return 0;
}
