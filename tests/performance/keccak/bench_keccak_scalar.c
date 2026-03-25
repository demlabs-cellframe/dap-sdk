#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "dap_hash_keccak.h"
#include "dap_common.h"

extern void dap_hash_keccak_permute_scalar_bmi2(dap_hash_keccak_state_t *state);
extern void dap_hash_keccak_permute_avx512vl_asm(dap_hash_keccak_state_t *state);

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define WARMUP 10000
#define ITERS  100000

static void bench_permute(const char *name, void (*fn)(dap_hash_keccak_state_t *)) {
    dap_hash_keccak_state_t st;
    memset(&st, 0x42, sizeof(st));

    for (int i = 0; i < WARMUP; i++)
        fn(&st);

    uint64_t best = UINT64_MAX;
    for (int run = 0; run < 5; run++) {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++)
            fn(&st);
        uint64_t t1 = rdtsc();
        uint64_t cyc = (t1 - t0) / ITERS;
        if (cyc < best) best = cyc;
    }
    printf("  %-30s  %4lu cycles/permute (best of 5)\n", name, best);
}

static void bench_absorb_136(const char *name,
    void (*fn)(uint64_t *, const uint8_t *, size_t, uint8_t))
{
    uint8_t data[32];
    memset(data, 0xAB, sizeof(data));
    uint64_t st[25];

    for (int i = 0; i < WARMUP; i++)
        fn(st, data, 32, 0x06);

    uint64_t best = UINT64_MAX;
    for (int run = 0; run < 5; run++) {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++)
            fn(st, data, 32, 0x06);
        uint64_t t1 = rdtsc();
        uint64_t cyc = (t1 - t0) / ITERS;
        if (cyc < best) best = cyc;
    }
    printf("  %-30s  %4lu cycles (32B absorb, best of 5)\n", name, best);
}

int main(void) {
    dap_common_init("bench_keccak_scalar", NULL);

    printf("=== Keccak-f[1600] Scalar BMI2 vs AVX-512VL ===\n\n");
    printf("--- Permute (24 rounds, 1x state) ---\n");

    bench_permute("scalar_bmi2", dap_hash_keccak_permute_scalar_bmi2);
    bench_permute("avx512vl_asm (1x)", dap_hash_keccak_permute_avx512vl_asm);

    printf("\n--- Fused absorb (SHA3-256 rate=136, 32B input) ---\n");

    bench_absorb_136("scalar_bmi2 absorb_136",
                     dap_keccak_absorb_136_scalar_bmi2);
    bench_absorb_136("avx512vl_asm absorb_136",
                     dap_keccak_absorb_136_avx512vl_asm);

    dap_common_deinit();
    return 0;
}
