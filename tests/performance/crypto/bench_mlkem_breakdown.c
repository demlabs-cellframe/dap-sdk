/**
 * ML-KEM-768 component breakdown benchmark.
 * Measures cycles for every major operation to find bottlenecks.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define PK_BYTES  1184
#define SK_BYTES  2400
#define CT_BYTES  1088
#define SS_BYTES  32
#define KYBER_N   256

int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

void dap_mlkem768_ntt(int16_t a[KYBER_N]);
void dap_mlkem768_invntt(int16_t a[KYBER_N]);
void dap_mlkem768_nttpack(int16_t a[KYBER_N]);
void dap_mlkem768_nttunpack(int16_t a[KYBER_N]);
void dap_mlkem768_poly_basemul_montgomery(int16_t *r, const int16_t *a, const int16_t *b);
void dap_mlkem768_poly_mulcache_compute(void *cache, const int16_t *a);
void dap_mlkem768_poly_getnoise_eta1_x4(void *p0, void *p1, void *p2, void *p3,
    const uint8_t *seed, uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3);
void dap_mlkem768_poly_getnoise_eta2_x4(void *p0, void *p1, void *p2, void *p3,
    const uint8_t *seed, uint8_t n0, uint8_t n1, uint8_t n2, uint8_t n3);
void dap_mlkem768_polyvec_compress(uint8_t *r, const void *a);
void dap_mlkem768_poly_compress(uint8_t *r, const void *a);
void dap_mlkem768_polyvec_decompress(void *r, const uint8_t *a);

typedef struct { uint64_t state[4][25]; } keccak_x4_t;
void dap_keccak_x4_init(keccak_x4_t *state);

int dap_common_init(const char *, const char **, const char *);

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define WARMUP 2000
#define ITERS  50000

#define BENCH_CYCLES(label, code) do {                               \
    for (int _w = 0; _w < WARMUP; _w++) { code; BARRIER(); }        \
    uint64_t _c0 = rdtsc();                                          \
    for (int _i = 0; _i < ITERS; _i++) { code; BARRIER(); }         \
    uint64_t _dc = rdtsc() - _c0;                                    \
    double _cyc = (double)_dc / ITERS;                               \
    printf("  %-30s %7.0f cycles  %7.1f ns\n", label, _cyc, _cyc * ns_per_cycle); \
} while(0)

int main(void) {
    dap_common_init("bench", NULL, NULL);

    /* Calibrate */
    struct timespec t0_ts, t1_ts;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
    uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
    double ns_per_cycle = (double)dt_ns / (c1 - c0);
    printf("CPU: %.2f GHz\n\n", 1.0 / ns_per_cycle);

    int16_t poly[KYBER_N] __attribute__((aligned(32)));
    int16_t poly2[KYBER_N] __attribute__((aligned(32)));
    int16_t poly_r[KYBER_N] __attribute__((aligned(32)));
    uint8_t cache[KYBER_N] __attribute__((aligned(32)));
    for (int i = 0; i < KYBER_N; i++) {
        poly[i] = (int16_t)(i * 17 % 3329);
        poly2[i] = (int16_t)(i * 31 % 3329);
    }

    printf("=== NTT Operations (%d iters) ===\n", ITERS);
    BENCH_CYCLES("NTT forward", dap_mlkem768_ntt(poly));
    BENCH_CYCLES("NTT inverse", dap_mlkem768_invntt(poly));
    BENCH_CYCLES("nttpack", dap_mlkem768_nttpack(poly));
    BENCH_CYCLES("nttunpack", dap_mlkem768_nttunpack(poly));

    printf("\n=== Polynomial Arithmetic (%d iters) ===\n", ITERS);
    dap_mlkem768_poly_mulcache_compute(cache, poly);
    BENCH_CYCLES("mulcache_compute", dap_mlkem768_poly_mulcache_compute(cache, poly));
    BENCH_CYCLES("basemul_montgomery", dap_mlkem768_poly_basemul_montgomery(poly_r, poly, poly2));

    printf("\n=== Noise Generation (%d iters) ===\n", ITERS);
    {
        int16_t p0[KYBER_N] __attribute__((aligned(32)));
        int16_t p1[KYBER_N] __attribute__((aligned(32)));
        int16_t p2b[KYBER_N] __attribute__((aligned(32)));
        int16_t p3[KYBER_N] __attribute__((aligned(32)));
        uint8_t seed[32] = {0};
        BENCH_CYCLES("getnoise_eta1_x4",
            dap_mlkem768_poly_getnoise_eta1_x4(p0, p1, p2b, p3, seed, 0, 1, 2, 3));
        BENCH_CYCLES("getnoise_eta2_x4",
            dap_mlkem768_poly_getnoise_eta2_x4(p0, p1, p2b, p3, seed, 0, 1, 2, 3));
    }

    printf("\n=== Compress/Decompress (%d iters) ===\n", ITERS);
    {
        /* polyvec compress: 3*KYBER_N polys → 3*320 = 960 bytes for d=10 */
        int16_t pvec[3 * KYBER_N] __attribute__((aligned(32)));
        uint8_t ct_buf[CT_BYTES];
        for (int i = 0; i < 3 * KYBER_N; i++) pvec[i] = (int16_t)(i % 3329);
        BENCH_CYCLES("polyvec_compress", dap_mlkem768_polyvec_compress(ct_buf, pvec));
        BENCH_CYCLES("poly_compress", dap_mlkem768_poly_compress(ct_buf + 960, poly));
        BENCH_CYCLES("polyvec_decompress", dap_mlkem768_polyvec_decompress(pvec, ct_buf));
    }

    printf("\n=== Hash Operations (%d iters) ===\n", ITERS);
    {
        uint8_t hash_in[1184], hash_out[64];
        memset(hash_in, 0x42, sizeof(hash_in));
        void dap_mlkem_hash_h_bench(uint8_t *, const uint8_t *, size_t);
        void dap_mlkem_hash_g_bench(uint8_t *, const uint8_t *, size_t);
        void dap_mlkem_kdf_bench(uint8_t *, const uint8_t *, size_t);
        BENCH_CYCLES("hash_h (32B input)", dap_mlkem_hash_h_bench(hash_out, hash_in, 32));
        BENCH_CYCLES("hash_h (1184B pk)", dap_mlkem_hash_h_bench(hash_out, hash_in, 1184));
        BENCH_CYCLES("hash_h (1088B ct)", dap_mlkem_hash_h_bench(hash_out, hash_in, 1088));
        BENCH_CYCLES("hash_g (64B input)", dap_mlkem_hash_g_bench(hash_out, hash_in, 64));
        BENCH_CYCLES("kdf (64B input)", dap_mlkem_kdf_bench(hash_out, hash_in, 64));
    }

    printf("\n=== Random Bytes (%d iters) ===\n", ITERS);
    {
        uint8_t rng_buf[32];
        void dap_random_bytes(void *, size_t);
        BENCH_CYCLES("random_bytes(32)", dap_random_bytes(rng_buf, 32));
    }

    printf("\n=== Full KEM Operations (%d iters) ===\n", ITERS);
    uint8_t pk[PK_BYTES], sk[SK_BYTES], ct[CT_BYTES], ss1[SS_BYTES], ss2[SS_BYTES];
    dap_mlkem768_kem_keypair(pk, sk);
    BENCH_CYCLES("keygen", dap_mlkem768_kem_keypair(pk, sk));
    BENCH_CYCLES("encaps", dap_mlkem768_kem_enc(ct, ss1, pk));
    dap_mlkem768_kem_enc(ct, ss1, pk);
    BENCH_CYCLES("decaps", dap_mlkem768_kem_dec(ss2, ct, sk));

    printf("\n=== Encaps Budget (K=3) ===\n");
    printf("  Per encaps, estimated call counts:\n");
    printf("  - gen_matrix: 3× x4_absorb_squeeze (9 SHAKE128 states)\n");
    printf("  - noise: 1× eta1_x4 + 1× eta2_x4 = 2× x4_PRF\n");
    printf("  - NTT fwd: 3× (polyvec_ntt sp)\n");
    printf("  - basemul: 4× polyvec_basemul_acc (3+1)\n");
    printf("  - NTT inv: 4× (3 bp + 1 v)\n");
    printf("  - compress: 1× polyvec + 1× poly\n");

    if (memcmp(ss1, ss2, SS_BYTES) != 0) printf("\n  !!! CORRECTNESS FAILURE !!!\n");
    else printf("\n  Correctness: OK\n");
    return 0;
}
