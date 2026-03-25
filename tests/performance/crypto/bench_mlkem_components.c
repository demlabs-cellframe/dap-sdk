/**
 * ML-KEM component-level profiling — measures individual hot-path
 * operations in cycles to identify bottlenecks vs mlkem-native/liboqs.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ITERS 50000
#define WARMUP 2000

#define BENCH_CYC(label, setup, call)                                          \
do {                                                                           \
    setup;                                                                     \
    for (int _w = 0; _w < WARMUP; _w++) { call; BARRIER(); }                  \
    uint64_t _t0 = rdtsc();                                                    \
    for (int _i = 0; _i < ITERS; _i++) { call; BARRIER(); }                   \
    uint64_t _dc = rdtsc() - _t0;                                             \
    printf("  %-42s %7.0f cyc  %6.1f ns\n",                                   \
           label, (double)_dc / ITERS, (double)_dc / ITERS * ns_per_cycle);   \
} while(0)

typedef struct { int16_t coeffs[256]; } poly_t;
typedef struct { int16_t coeffs[256]; } poly_mulcache_t;

/* Extern declarations for mlkem768 internals (K=3) */
void dap_mlkem768_ntt(int16_t r[256]);
void dap_mlkem768_invntt(int16_t r[256]);
void dap_mlkem768_basemul(int16_t r[2], const int16_t a[2], const int16_t b[2], int16_t zeta);
void dap_mlkem768_poly_ntt(poly_t *r);
void dap_mlkem768_poly_invntt_tomont(poly_t *r);
void dap_mlkem768_poly_basemul_montgomery(poly_t *r, const poly_t *a, const poly_t *b);
void dap_mlkem768_poly_tomont(poly_t *r);
void dap_mlkem768_poly_reduce(poly_t *r);
void dap_mlkem768_poly_compress(uint8_t r[128], const poly_t *a);
void dap_mlkem768_poly_decompress(poly_t *r, const uint8_t a[128]);
void dap_mlkem768_poly_tobytes(uint8_t r[384], const poly_t *a);
void dap_mlkem768_poly_frombytes(poly_t *r, const uint8_t a[384]);
void dap_mlkem768_poly_frommsg(poly_t *r, const uint8_t msg[32]);
void dap_mlkem768_poly_tomsg(uint8_t msg[32], const poly_t *r);
void dap_mlkem768_poly_mulcache_compute(poly_mulcache_t *r, const poly_t *a);
void dap_mlkem768_poly_getnoise_eta1(poly_t *r, const uint8_t seed[32], uint8_t nonce);
void dap_mlkem768_poly_getnoise_eta2(poly_t *r, const uint8_t seed[32], uint8_t nonce);
void dap_mlkem768_poly_getnoise_eta1_x4(poly_t *r0, poly_t *r1, poly_t *r2, poly_t *r3,
        const uint8_t seed[32], uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3);
void dap_mlkem768_poly_getnoise_eta2_x4(poly_t *r0, poly_t *r1, poly_t *r2, poly_t *r3,
        const uint8_t seed[32], uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3);

typedef struct { poly_t vec[3]; } polyvec_t;
typedef struct { poly_mulcache_t vec[3]; } polyvec_mulcache_t;

void dap_mlkem768_polyvec_ntt(polyvec_t *r);
void dap_mlkem768_polyvec_invntt_tomont(polyvec_t *r);
void dap_mlkem768_polyvec_compress(uint8_t *r, const polyvec_t *a);
void dap_mlkem768_polyvec_frombytes(polyvec_t *r, const uint8_t *a);
void dap_mlkem768_polyvec_tobytes(uint8_t *r, const polyvec_t *a);
void dap_mlkem768_polyvec_mulcache_compute(polyvec_mulcache_t *r, const polyvec_t *a);
void dap_mlkem768_polyvec_basemul_acc_montgomery_cached(poly_t *r, const polyvec_t *a,
        const polyvec_t *b, const polyvec_mulcache_t *bc);
void dap_mlkem768_polyvec_add(polyvec_t *r, const polyvec_t *b);
void dap_mlkem768_polyvec_reduce(polyvec_t *r);

void dap_mlkem768_indcpa_keypair(uint8_t *pk, uint8_t *sk);
void dap_mlkem768_indcpa_enc(uint8_t *c, const uint8_t *m, const uint8_t *pk, const uint8_t *coins);
void dap_mlkem768_indcpa_dec(uint8_t *m, const uint8_t *c, const uint8_t *sk);

int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* SHAKE/Keccak x4 */
#include "dap_hash_keccak.h"
#include "dap_hash_keccak_x4.h"
#include "dap_hash_shake_x4.h"
#include "dap_mlkem_symmetric.h"
#include "dap_common.h"

static void __attribute__((noinline)) s_bench_full_kem(void)
{
    double ns_per_cycle;
    {
        struct timespec t0_ts, t1_ts;
        uint64_t c0, c1;
        clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
        volatile int sink = 0;
        for (int i = 0; i < 1000000; i++) sink += i;
        clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
        uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
        ns_per_cycle = (double)dt_ns / (c1 - c0);
    }
    printf("\n--- Full KEM (includes gen_matrix + hashes) ---\n");
    uint8_t pk[1184], sk[2400], ct[1088], ss1[32], ss2[32];
    dap_mlkem768_kem_keypair(pk, sk);
    BENCH_CYC("kem_keypair",
               (void)0,
               dap_mlkem768_kem_keypair(pk, sk));
    BENCH_CYC("kem_enc (encaps)",
               (void)0,
               dap_mlkem768_kem_enc(ct, ss1, pk));
    BENCH_CYC("kem_dec (decaps)",
               dap_mlkem768_kem_enc(ct, ss1, pk),
               dap_mlkem768_kem_dec(ss2, ct, sk));
}

int main(void) {
    dap_common_init("bench_components", NULL);

    printf("=== ML-KEM-768 Component Profiling (%d iters) ===\n\n", ITERS);

    struct timespec t0_ts, t1_ts;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
    uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
    double ns_per_cycle = (double)dt_ns / (c1 - c0);
    printf("CPU: %.2f GHz (%.3f ns/cyc)\n\n", 1.0 / ns_per_cycle, ns_per_cycle);

    poly_t p __attribute__((aligned(32))), q __attribute__((aligned(32))), r __attribute__((aligned(32)));
    poly_mulcache_t pc __attribute__((aligned(32)));
    polyvec_t pv __attribute__((aligned(32))), qv __attribute__((aligned(32)));
    polyvec_mulcache_t pvc __attribute__((aligned(32)));
    uint8_t buf[3200] __attribute__((aligned(32)));
    uint8_t seed[32];
    memset(seed, 0xAB, 32);

    /* Initialize with pseudo-random data */
    for (int i = 0; i < 256; i++) {
        p.coeffs[i] = (int16_t)(i * 31 % 3329);
        q.coeffs[i] = (int16_t)(i * 47 % 3329);
    }
    memcpy(&pv.vec[0], &p, sizeof(p));
    memcpy(&pv.vec[1], &q, sizeof(q));
    memcpy(&pv.vec[2], &p, sizeof(p));
    memcpy(&qv, &pv, sizeof(pv));

    printf("--- NTT / arithmetic core ---\n");
    BENCH_CYC("ntt (256 coeffs)",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_ntt(r.coeffs));
    BENCH_CYC("invntt (256 coeffs)",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_invntt(r.coeffs));
    BENCH_CYC("poly_ntt",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_poly_ntt(&r));
    BENCH_CYC("poly_invntt_tomont",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_poly_invntt_tomont(&r));
    BENCH_CYC("poly_basemul_montgomery",
              (void)0,
              dap_mlkem768_poly_basemul_montgomery(&r, &p, &q));
    BENCH_CYC("poly_mulcache_compute",
              (void)0,
              dap_mlkem768_poly_mulcache_compute(&pc, &p));
    BENCH_CYC("poly_tomont",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_poly_tomont(&r));
    BENCH_CYC("poly_reduce",
              memcpy(&r, &p, sizeof(p)),
              dap_mlkem768_poly_reduce(&r));

    printf("\n--- Serialize / compress ---\n");
    BENCH_CYC("poly_tobytes",
              (void)0,
              dap_mlkem768_poly_tobytes(buf, &p));
    BENCH_CYC("poly_frombytes",
              (void)0,
              dap_mlkem768_poly_frombytes(&r, buf));
    BENCH_CYC("poly_compress (d=4)",
              (void)0,
              dap_mlkem768_poly_compress(buf, &p));
    BENCH_CYC("poly_decompress (d=4)",
              (void)0,
              dap_mlkem768_poly_decompress(&r, buf));
    BENCH_CYC("poly_frommsg",
              (void)0,
              dap_mlkem768_poly_frommsg(&r, seed));
    BENCH_CYC("poly_tomsg",
              (void)0,
              dap_mlkem768_poly_tomsg(buf, &p));

    printf("\n--- Noise sampling ---\n");
    BENCH_CYC("poly_getnoise_eta1 (eta=2)",
              (void)0,
              dap_mlkem768_poly_getnoise_eta1(&r, seed, 0));
    BENCH_CYC("poly_getnoise_eta2 (eta=2)",
              (void)0,
              dap_mlkem768_poly_getnoise_eta2(&r, seed, 0));

    printf("\n--- Keccak internals ---\n");
    {
        dap_hash_keccak_state_t kst;
        memset(&kst, 0x42, sizeof(kst));
        BENCH_CYC("keccak_permute_1x (dispatch)",
                   (void)0,
                   dap_hash_keccak_permute(&kst));
#if defined(__x86_64__)
        BENCH_CYC("keccak_permute_avx512vl_asm",
                   (void)0,
                   dap_hash_keccak_permute_avx512vl_asm(&kst));
        BENCH_CYC("keccak_permute_avx2",
                   (void)0,
                   dap_hash_keccak_permute_avx2(&kst));
        BENCH_CYC("keccak_permute_sse2",
                   (void)0,
                   dap_hash_keccak_permute_sse2(&kst));
        /* skip scalar_bmi2 — may crash on some inputs */
#endif
        BENCH_CYC("keccak_permute_ref",
                   (void)0,
                   dap_hash_keccak_permute_ref(&kst));

        dap_keccak_x4_state_t x4st;
        memset(&x4st, 0x42, sizeof(x4st));
        BENCH_CYC("keccak_x4_permute (dispatch)",
                   (void)0,
                   dap_keccak_x4_permute(&x4st));
#if defined(__x86_64__)
        BENCH_CYC("keccak_x4_permute_avx512vl_asm",
                   (void)0,
                   dap_keccak_x4_permute_avx512vl_asm(&x4st));
        BENCH_CYC("keccak_x4_permute_avx2",
                   (void)0,
                   dap_keccak_x4_permute_avx2(&x4st));
#endif

        uint8_t prf_out[4][128];
        BENCH_CYC("prf_x4 (SHAKE256, 33->128, 4 inst)",
                   (void)0,
                   dap_mlkem_prf_x4(prf_out[0], prf_out[1], prf_out[2], prf_out[3],
                                     128, seed, 0, 1, 2, 3));

        uint64_t prf_st[25];
        uint8_t extkey[33]; memcpy(extkey, seed, 32); extkey[32] = 0;
        uint8_t prf_single[128];
        BENCH_CYC("absorb_136_avx512vl (1x sponge)",
                   (void)0,
                   dap_keccak_absorb_136_avx512vl_asm(prf_st, extkey, 33, 0x1F));
        BENCH_CYC("prf_single (SHAKE256, 33->128)",
                   (void)0,
                   ({ dap_keccak_absorb_136_avx512vl_asm(prf_st, extkey, 33, 0x1F);
                      memcpy(prf_single, prf_st, 128); }));
    }

    printf("\n--- Noise x4 (amortized per 4 polys) ---\n");
    {
        poly_t np[4];
        BENCH_CYC("getnoise_eta1_x4 (4 polys)",
                   (void)0,
                   dap_mlkem768_poly_getnoise_eta1_x4(&np[0], &np[1], &np[2], &np[3],
                                                        seed, 0, 1, 2, 3));
        BENCH_CYC("getnoise_eta2_x4 (4 polys)",
                   (void)0,
                   dap_mlkem768_poly_getnoise_eta2_x4(&np[0], &np[1], &np[2], &np[3],
                                                        seed, 0, 1, 2, 3));
    }

    printf("\n--- Polyvec (K=3) ---\n");
    BENCH_CYC("polyvec_ntt (3x256)",
              memcpy(&pv, &qv, sizeof(pv)),
              dap_mlkem768_polyvec_ntt(&pv));
    BENCH_CYC("polyvec_invntt_tomont (3x256)",
              memcpy(&pv, &qv, sizeof(pv)),
              dap_mlkem768_polyvec_invntt_tomont(&pv));
    BENCH_CYC("polyvec_mulcache_compute",
              (void)0,
              dap_mlkem768_polyvec_mulcache_compute(&pvc, &pv));
    BENCH_CYC("polyvec_basemul_acc_cached",
              (void)0,
              dap_mlkem768_polyvec_basemul_acc_montgomery_cached(&r, &pv, &qv, &pvc));
    BENCH_CYC("polyvec_compress (d=10)",
              (void)0,
              dap_mlkem768_polyvec_compress(buf, &pv));
    BENCH_CYC("polyvec_tobytes",
              (void)0,
              dap_mlkem768_polyvec_tobytes(buf, &pv));
    BENCH_CYC("polyvec_frombytes",
              (void)0,
              dap_mlkem768_polyvec_frombytes(&pv, buf));
    BENCH_CYC("polyvec_add",
              (void)0,
              dap_mlkem768_polyvec_add(&pv, &qv));
    BENCH_CYC("polyvec_reduce",
              (void)0,
              dap_mlkem768_polyvec_reduce(&pv));

    s_bench_full_kem();

    dap_common_deinit();
    return 0;
}
