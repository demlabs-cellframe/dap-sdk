/**
 * ML-DSA (Dilithium) component-level profiling — identifies bottlenecks
 * in the verify path by timing individual operations with rdtsc.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "dap_common.h"
#include "dilithium_params.h"
#include "dilithium_poly.h"
#include "dilithium_polyvec.h"
#include "dilithium_sign.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ITERS    10000
#define WARMUP   500

#define BENCH_CYC(label, setup, call)                                      \
do {                                                                       \
    setup;                                                                 \
    for (int _w = 0; _w < WARMUP; _w++) { call; BARRIER(); }              \
    uint64_t _t0 = rdtsc();                                                \
    for (int _i = 0; _i < ITERS; _i++) { call; BARRIER(); }               \
    uint64_t _dc = rdtsc() - _t0;                                         \
    printf("  %-42s %8.0f cyc  %7.1f ns\n",                               \
           label, (double)_dc / ITERS, (double)_dc / ITERS * ns_cyc);     \
} while(0)

int main(void) {
    dap_common_init("bench_dil", NULL);

    double ns_cyc;
    {
        struct timespec t0, t1;
        uint64_t c0, c1;
        clock_gettime(CLOCK_MONOTONIC, &t0); c0 = rdtsc();
        volatile int sink = 0;
        for (int i = 0; i < 2000000; i++) sink += i;
        clock_gettime(CLOCK_MONOTONIC, &t1); c1 = rdtsc();
        uint64_t dt = (t1.tv_sec - t0.tv_sec) * 1000000000ULL + (t1.tv_nsec - t0.tv_nsec);
        ns_cyc = (double)dt / (c1 - c0);
    }

    printf("=== ML-DSA Component Profiling (%d iters) ===\n", ITERS);
    printf("CPU: %.2f GHz (%.3f ns/cyc)\n\n", 1.0 / ns_cyc, ns_cyc);

    dilithium_param_t p;
    dilithium_params_init(&p, MODE_1);
    printf("MODE_1 (ML-DSA-44): K=%u L=%u\n\n", p.PARAM_K, p.PARAM_L);

    unsigned char rho[SEEDBYTES], mu[CRHBYTES];
    memset(rho, 0xAB, SEEDBYTES);
    memset(mu, 0xCD, CRHBYTES);

    polyvecl mat[p.PARAM_K], z;
    polyveck t1, w1, h, tmp1, tmp2;
    poly c_val, chat, cp;

    for (unsigned i = 0; i < NN; i++) {
        z.vec[0].coeffs[i] = i * 31u % Q;
        z.vec[1].coeffs[i] = i * 47u % Q;
        z.vec[2].coeffs[i] = i * 59u % Q;
        t1.vec[0].coeffs[i] = i * 71u % Q;
        t1.vec[1].coeffs[i] = i * 83u % Q;
        t1.vec[2].coeffs[i] = i * 97u % Q;
        t1.vec[3].coeffs[i] = i * 101u % Q;
        c_val.coeffs[i] = 0;
        h.vec[0].coeffs[i] = (i % 20 == 0) ? 1 : 0;
        h.vec[1].coeffs[i] = 0;
        h.vec[2].coeffs[i] = 0;
        h.vec[3].coeffs[i] = 0;
    }
    c_val.coeffs[200] = 1; c_val.coeffs[210] = Q - 1;

    unsigned char shake_out[5 * 168];
    dap_hash_shake256(shake_out, sizeof(shake_out), rho, SEEDBYTES);

    printf("--- Individual components ---\n");

    BENCH_CYC("expand_mat (K*L SHAKE128 x4 + rej)",
              (void)0,
              expand_mat(mat, rho, &p));

    BENCH_CYC("dilithium_poly_uniform (rej_sample)",
              (void)0,
              dilithium_poly_uniform(&z.vec[0], shake_out));

    BENCH_CYC("dilithium_poly_ntt (1 poly)",
              (void)0,
              dilithium_poly_ntt(&z.vec[0]));

    BENCH_CYC("poly_invntt_montgomery (1 poly)",
              (void)0,
              poly_invntt_montgomery(&z.vec[0]));

    BENCH_CYC("poly_pointwise_invmontgomery",
              (void)0,
              poly_pointwise_invmontgomery(&cp, &z.vec[0], &z.vec[1]));

    BENCH_CYC("polyvecl_pw_acc_invmont (L=3)",
              (void)0,
              polyvecl_pointwise_acc_invmontgomery(&cp, &mat[0], &z, &p));

    BENCH_CYC("polyvecl_ntt (L=3 NTTs)",
              (void)0,
              polyvecl_ntt(&z, &p));

    BENCH_CYC("polyveck_ntt (K=4 NTTs)",
              (void)0,
              polyveck_ntt(&t1, &p));

    BENCH_CYC("polyveck_invntt_montgomery (K=4)",
              (void)0,
              polyveck_invntt_montgomery(&t1, &p));

    BENCH_CYC("poly_reduce",
              (void)0,
              poly_reduce(&z.vec[0]));

    BENCH_CYC("poly_csubq",
              (void)0,
              poly_csubq(&z.vec[0]));

    BENCH_CYC("poly_decompose",
              (void)0,
              poly_decompose(&w1.vec[0], &tmp1.vec[0], &t1.vec[0]));

    BENCH_CYC("poly_use_hint",
              (void)0,
              poly_use_hint(&w1.vec[0], &t1.vec[0], &h.vec[0]));

    BENCH_CYC("polyw1_pack",
              (void)0,
              polyw1_pack((unsigned char *)shake_out, &w1.vec[0]));

    BENCH_CYC("poly_shiftl(D=14)",
              (void)0,
              poly_shiftl(&t1.vec[0], D));

    BENCH_CYC("dilithium_poly_add",
              (void)0,
              dilithium_poly_add(&cp, &z.vec[0], &z.vec[1]));

    BENCH_CYC("polyveck_reduce (K=4)",
              (void)0,
              polyveck_reduce(&t1, &p));

    BENCH_CYC("polyveck_csubq (K=4)",
              (void)0,
              polyveck_csubq(&t1, &p));

    BENCH_CYC("polyveck_use_hint (K=4)",
              (void)0,
              polyveck_use_hint(&w1, &t1, &h, &p));

    BENCH_CYC("challenge (SHAKE256 + sample)",
              (void)0,
              challenge(&cp, mu, &w1, &p));

    printf("\n--- Verify hot path simulation ---\n");
    BENCH_CYC("full_verify_core",
              (void)0,
              ({
                  expand_mat(mat, rho, &p);
                  polyvecl_ntt(&z, &p);
                  for (unsigned _j = 0; _j < p.PARAM_K; _j++)
                      polyvecl_pointwise_acc_invmontgomery(tmp1.vec + _j, mat + _j, &z, &p);
                  chat = c_val;
                  dilithium_poly_ntt(&chat);
                  polyveck_ntt(&t1, &p);
                  for (unsigned _j = 0; _j < p.PARAM_K; _j++)
                      poly_pointwise_invmontgomery(tmp2.vec + _j, &chat, t1.vec + _j);
                  polyveck_sub(&tmp1, &tmp1, &tmp2, &p);
                  polyveck_reduce(&tmp1, &p);
                  polyveck_invntt_montgomery(&tmp1, &p);
                  polyveck_csubq(&tmp1, &p);
                  polyveck_use_hint(&w1, &tmp1, &h, &p);
                  challenge(&cp, mu, &w1, &p);
              }));

    printf("\n=== Done ===\n");
    return 0;
}
