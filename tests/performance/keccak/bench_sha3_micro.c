/**
 * SHA3/Keccak micro-benchmark — measures absorb at different sizes.
 * Uses the same ASM functions that ML-KEM hash_h dispatches to.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

void dap_hash_keccak_permute_avx512vl_asm(uint64_t state[25]);
void dap_keccak_x4_permute_avx512vl_asm(void *state);
void dap_keccak_absorb_136_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_168_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);
void dap_keccak_absorb_72_avx512vl_asm(uint64_t *state, const uint8_t *data, size_t len, uint8_t suffix);

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define WARMUP 5000
#define ITERS 100000

int main(void) {
    struct timespec t0_ts, t1_ts;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
    uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
    double ns_per_cycle = (double)dt_ns / (c1 - c0);
    printf("CPU: %.2f GHz\n\n", 1.0 / ns_per_cycle);

    uint8_t data[2048] __attribute__((aligned(32)));
    memset(data, 0xab, sizeof(data));
    uint64_t state[25] __attribute__((aligned(32)));

    printf("=== Keccak Primitives (%d iters) ===\n", ITERS);

    /* 1x permutation */
    memset(state, 0, sizeof(state));
    for (int w = 0; w < WARMUP; w++) { dap_hash_keccak_permute_avx512vl_asm(state); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { dap_hash_keccak_permute_avx512vl_asm(state); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  1x permute:                  %7.0f cycles  %6.1f ns\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    printf("\n=== ASM absorb_136 (SHA3-256 rate) ===\n");

#define BENCH_ABSORB(label, rate_fn, data_ptr, len, suffix) do { \
    for (int _w = 0; _w < WARMUP; _w++) { rate_fn(state, data_ptr, len, suffix); BARRIER(); } \
    uint64_t _c0 = rdtsc(); \
    for (int _i = 0; _i < ITERS; _i++) { rate_fn(state, data_ptr, len, suffix); BARRIER(); } \
    uint64_t _dc = rdtsc() - _c0; \
    double _cyc = (double)_dc / ITERS; \
    int _nperms = ((int)(len) / (rate_fn == dap_keccak_absorb_136_avx512vl_asm ? 136 : \
                   rate_fn == dap_keccak_absorb_72_avx512vl_asm ? 72 : 168)) + 1; \
    printf("  %-30s %7.0f cycles  %6.1f ns  (%d perms, %.0f cyc/perm)\n", \
           label, _cyc, _cyc * ns_per_cycle, _nperms, _cyc / _nperms); \
} while(0)

    BENCH_ABSORB("absorb_136(32 bytes)", dap_keccak_absorb_136_avx512vl_asm, data, 32, 0x06);
    BENCH_ABSORB("absorb_136(64 bytes)", dap_keccak_absorb_136_avx512vl_asm, data, 64, 0x06);
    BENCH_ABSORB("absorb_136(1088 bytes)", dap_keccak_absorb_136_avx512vl_asm, data, 1088, 0x06);
    BENCH_ABSORB("absorb_136(1184 bytes)", dap_keccak_absorb_136_avx512vl_asm, data, 1184, 0x06);

    printf("\n=== ASM absorb_72 (SHA3-512 rate) ===\n");
    BENCH_ABSORB("absorb_72(64 bytes)", dap_keccak_absorb_72_avx512vl_asm, data, 64, 0x06);

    printf("\n=== ASM absorb_168 (SHAKE128 rate) ===\n");
    BENCH_ABSORB("absorb_168(34 bytes)", dap_keccak_absorb_168_avx512vl_asm, data, 34, 0x1f);

    printf("\n=== ML-KEM-768 encaps SHA3 budget ===\n");
    printf("  hash_h(buf,32)   = absorb_136(32)\n");
    printf("  hash_h(pk,1184)  = absorb_136(1184) → 9 perms\n");
    printf("  hash_g(buf,64)   = absorb_72(64)\n");
    printf("  indcpa_enc       = gen_matrix + NTT + basemul + ...\n");
    printf("  hash_h(ct,1088)  = absorb_136(1088) → 9 perms\n");
    printf("  kdf(kr,64)       = absorb_136(64)\n");

    return 0;
}
