/**
 * Benchmark comparing current 1x Keccak permutation vs optimized scalar.
 * Tests whether a scalar (GP register) approach can beat the YMM-based approach.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#ifdef __BMI2__
#include <immintrin.h>
#define ROL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#else
#define ROL64(x, n) (((x) << (n)) | ((x) >> (64 - (n))))
#endif

void dap_hash_keccak_permute_avx512vl_asm(uint64_t state[25]);

static const uint64_t s_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808aULL, 0x8000000080008000ULL,
    0x000000000000808bULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008aULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000aULL,
    0x000000008000808bULL, 0x800000000000008bULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800aULL, 0x800000008000000aULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int s_rho[25] = {
     0,  1, 62, 28, 27,
    36, 44,  6, 55, 20,
     3, 10, 43, 25, 39,
    41, 45, 15, 21,  8,
    18,  2, 61, 56, 14
};

static const int s_pi[25] = {
     0, 10, 20,  5, 15,
    16,  1, 11, 21,  6,
     7, 17,  2, 12, 22,
    23,  8, 18,  3, 13,
    14, 24,  9, 19,  4
};

__attribute__((noinline, optimize("O3,unroll-loops")))
static void keccak_scalar_opt(uint64_t A[25])
{
    for (int round = 0; round < 24; round++) {
        /* Theta */
        uint64_t C[5];
        C[0] = A[0] ^ A[5] ^ A[10] ^ A[15] ^ A[20];
        C[1] = A[1] ^ A[6] ^ A[11] ^ A[16] ^ A[21];
        C[2] = A[2] ^ A[7] ^ A[12] ^ A[17] ^ A[22];
        C[3] = A[3] ^ A[8] ^ A[13] ^ A[18] ^ A[23];
        C[4] = A[4] ^ A[9] ^ A[14] ^ A[19] ^ A[24];

        uint64_t D[5];
        D[0] = C[4] ^ ROL64(C[1], 1);
        D[1] = C[0] ^ ROL64(C[2], 1);
        D[2] = C[1] ^ ROL64(C[3], 1);
        D[3] = C[2] ^ ROL64(C[4], 1);
        D[4] = C[3] ^ ROL64(C[0], 1);

        for (int i = 0; i < 25; i++) A[i] ^= D[i % 5];

        /* Rho + Pi */
        uint64_t B[25];
        for (int i = 0; i < 25; i++)
            B[s_pi[i]] = ROL64(A[i], s_rho[i]);

        /* Chi */
        for (int y = 0; y < 5; y++) {
            int b = y * 5;
            A[b+0] = B[b+0] ^ (~B[b+1] & B[b+2]);
            A[b+1] = B[b+1] ^ (~B[b+2] & B[b+3]);
            A[b+2] = B[b+2] ^ (~B[b+3] & B[b+4]);
            A[b+3] = B[b+3] ^ (~B[b+4] & B[b+0]);
            A[b+4] = B[b+4] ^ (~B[b+0] & B[b+1]);
        }

        /* Iota */
        A[0] ^= s_rc[round];
    }
}

static inline uint64_t rdtsc(void) {
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ITERS 200000

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

    uint64_t state[25] __attribute__((aligned(64)));
    memset(state, 0xab, sizeof(state));

    /* Verify correctness */
    {
        uint64_t s1[25], s2[25];
        memcpy(s1, state, sizeof(s1));
        memcpy(s2, state, sizeof(s2));
        dap_hash_keccak_permute_avx512vl_asm(s1);
        keccak_scalar_opt(s2);
        if (memcmp(s1, s2, sizeof(s1)) == 0)
            printf("Correctness: OK (scalar matches SIMD)\n\n");
        else {
            printf("Correctness: FAIL!\n");
            return 1;
        }
    }

    printf("=== Keccak-f1600 Permutation (%d iters) ===\n", ITERS);

    /* Current: 1x AVX-512VL ASM */
    for (int w = 0; w < 5000; w++) { dap_hash_keccak_permute_avx512vl_asm(state); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { dap_hash_keccak_permute_avx512vl_asm(state); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  1x SIMD (avx512vl ASM): %7.0f cycles  %6.1f ns\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* Scalar optimized (compiler-generated) */
    for (int w = 0; w < 5000; w++) { keccak_scalar_opt(state); BARRIER(); }
    {
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { keccak_scalar_opt(state); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("  Scalar (gcc -O3):       %7.0f cycles  %6.1f ns\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    return 0;
}
