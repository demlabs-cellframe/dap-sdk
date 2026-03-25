/**
 * Keccak x4 permutation micro-benchmark.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

typedef struct { uint64_t lanes[100]; } __attribute__((aligned(32))) keccak_x4_t;

void dap_keccak_x4_permute_avx512vl_asm(keccak_x4_t *state);
void dap_keccak_x4_permute_avx2(keccak_x4_t *state);
void dap_hash_keccak_permute_avx512vl_asm(uint64_t state[25]);
int dap_common_init(const char *, const char **, const char *);

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define WARMUP 5000
#define ITERS 200000

int main(void) {
    dap_common_init("bench", NULL, NULL);

    struct timespec t0_ts, t1_ts;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
    uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
    double ns_per_cycle = (double)dt_ns / (c1 - c0);
    printf("CPU: %.2f GHz\n\n", 1.0 / ns_per_cycle);

    keccak_x4_t state;
    memset(&state, 0xab, sizeof(state));

    uint64_t state1x[25] __attribute__((aligned(32)));
    memset(state1x, 0xab, sizeof(state1x));

    printf("=== Keccak Permutation (%d iters) ===\n", ITERS);

    /* 1x AVX-512VL ASM */
    for (int w = 0; w < WARMUP; w++) { dap_hash_keccak_permute_avx512vl_asm(state1x); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { dap_hash_keccak_permute_avx512vl_asm(state1x); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  1x ASM avx512vl: %7.0f cycles  %6.1f ns\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* x4 ASM avx512vl */
    for (int w = 0; w < WARMUP; w++) { dap_keccak_x4_permute_avx512vl_asm(&state); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { dap_keccak_x4_permute_avx512vl_asm(&state); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  x4 ASM avx512vl: %7.0f cycles  %6.1f ns  (%5.1f cycles/state)\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle,
               (double)dc / ITERS / 4);
    }

    /* x4 C intrinsics avx2 (comparison) */
    for (int w = 0; w < WARMUP; w++) { dap_keccak_x4_permute_avx2(&state); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { dap_keccak_x4_permute_avx2(&state); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  x4 C avx2:       %7.0f cycles  %6.1f ns  (%5.1f cycles/state)\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle,
               (double)dc / ITERS / 4);
    }

    printf("\n  SHAKE128 gen_matrix equivalent (9 states, ~4 perms each):\n");
    printf("  Estimated: 3 batches × ~4 x4 perms = 12 x4 perms\n");

    return 0;
}
