/**
 * ML-KEM component-level benchmark — measures individual operations
 * to identify bottlenecks vs competitors.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "dap_hash_keccak_x4.h"
#include "dap_hash_shake_x4.h"

#include "dap_common.h"

int dap_mlkem512_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem512_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem512_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

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
#define ITERS 100000
#define WARMUP 2000

int main(void) {
    dap_common_init("bench", NULL);

    printf("=== ML-KEM Component Benchmarks (cycles, %d iters) ===\n\n", ITERS);

    /* Measure CPU frequency */
    struct timespec t0_ts, t1_ts;
    uint64_t c0, c1;
    clock_gettime(CLOCK_MONOTONIC, &t0_ts); c0 = rdtsc();
    volatile int sink = 0;
    for (int i = 0; i < 2000000; i++) sink += i;
    clock_gettime(CLOCK_MONOTONIC, &t1_ts); c1 = rdtsc();
    uint64_t dt_ns = (t1_ts.tv_sec - t0_ts.tv_sec) * 1000000000ULL + (t1_ts.tv_nsec - t0_ts.tv_nsec);
    double ns_per_cycle = (double)dt_ns / (c1 - c0);
    double ghz = 1.0 / ns_per_cycle;
    printf("CPU: %.2f GHz\n\n", ghz);

    /* 1. SHAKE128 x4 absorb+squeeze (34 bytes in, 3 blocks out = gen_matrix equivalent) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t seed[4][34];
        uint8_t out[4][3 * 168];
        memset(seed, 0xab, sizeof(seed));

        for (int w = 0; w < WARMUP; w++) {
            dap_hash_shake128_x4_absorb(&state, seed[0], seed[1], seed[2], seed[3], 34);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 168);
            dap_hash_shake128_x4_squeezeblocks(out[0]+168, out[1]+168, out[2]+168, out[3]+168, 2, &state);
            BARRIER();
        }

        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_hash_shake128_x4_absorb(&state, seed[0], seed[1], seed[2], seed[3], 34);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 168);
            dap_hash_shake128_x4_squeezeblocks(out[0]+168, out[1]+168, out[2]+168, out[3]+168, 2, &state);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("1. SHAKE128 x4 absorb+3-block squeeze (gen_matrix per batch):\n");
        printf("   %7.0f cycles  %6.1f ns  (%5.0f cycles / 4 entries)\n\n",
               (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle, (double)dc / ITERS);
    }

    /* 2. SHAKE256 x4 absorb+squeeze: 33 bytes in, 192 out (eta1=3, K=2 PRF) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t key[4][33];
        uint8_t out[4][192];
        memset(key, 0xcd, sizeof(key));

        for (int w = 0; w < WARMUP; w++) {
            dap_hash_shake256_x4_absorb(&state, key[0], key[1], key[2], key[3], 33);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 136);
            dap_hash_shake256_x4_squeezeblocks(out[0]+136, out[1]+136, out[2]+136, out[3]+136, 1, &state);
            BARRIER();
        }

        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_hash_shake256_x4_absorb(&state, key[0], key[1], key[2], key[3], 33);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 136);
            dap_hash_shake256_x4_squeezeblocks(out[0]+136, out[1]+136, out[2]+136, out[3]+136, 1, &state);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("2. SHAKE256 x4 absorb+192-byte squeeze (eta1=3 PRF):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 3. SHAKE256 x4 absorb+squeeze: 33 bytes in, 128 out (eta1=2 or eta2=2 PRF) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t key[4][33];
        uint8_t out[4][136];
        memset(key, 0xef, sizeof(key));

        for (int w = 0; w < WARMUP; w++) {
            dap_hash_shake256_x4_absorb(&state, key[0], key[1], key[2], key[3], 33);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 128);
            BARRIER();
        }

        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_hash_shake256_x4_absorb(&state, key[0], key[1], key[2], key[3], 33);
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 128);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("3. SHAKE256 x4 absorb+128-byte extract (eta2/eta1=2 PRF):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 4. memset 800 bytes (x4 state init) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        for (int w = 0; w < WARMUP; w++) { memset(&state, 0, sizeof(state)); BARRIER(); }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) { memset(&state, 0, sizeof(state)); BARRIER(); }
        uint64_t dc = rdtsc() - t0;
        printf("4. memset 800 bytes (x4 state init):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 5. x4 xor_bytes_all (34 bytes) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t data[4][34];
        memset(data, 0xab, sizeof(data));
        memset(&state, 0, sizeof(state));
        for (int w = 0; w < WARMUP; w++) {
            dap_keccak_x4_xor_bytes_all(&state, data[0], data[1], data[2], data[3], 34);
            BARRIER();
        }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_keccak_x4_xor_bytes_all(&state, data[0], data[1], data[2], data[3], 34);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("5. x4 xor_bytes_all (34 bytes):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 6. x4 extract_bytes_all (168 bytes = SHAKE128 rate) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t out[4][168];
        memset(&state, 0xab, sizeof(state));
        for (int w = 0; w < WARMUP; w++) {
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 168);
            BARRIER();
        }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 168);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("6. x4 extract_bytes_all (168 bytes):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 7. x4 extract_bytes_all (136 bytes = SHAKE256 rate) */
    {
        dap_keccak_x4_state_t state __attribute__((aligned(32)));
        uint8_t out[4][136];
        memset(&state, 0xab, sizeof(state));
        for (int w = 0; w < WARMUP; w++) {
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 136);
            BARRIER();
        }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < ITERS; i++) {
            dap_keccak_x4_extract_bytes_all(&state, out[0], out[1], out[2], out[3], 136);
            BARRIER();
        }
        uint64_t dc = rdtsc() - t0;
        printf("7. x4 extract_bytes_all (136 bytes):\n");
        printf("   %7.0f cycles  %6.1f ns\n\n", (double)dc / ITERS, (double)dc / ITERS * ns_per_cycle);
    }

    /* 8. Full ML-KEM-512 enc+dec vs ML-KEM-768 */
    printf("=== Full ML-KEM Operations ===\n\n");
    {
        uint8_t pk[1568], sk[3168], ct[1568], ss1[32], ss2[32];

        dap_mlkem512_kem_keypair(pk, sk);
        for (int w = 0; w < 500; w++) {
            dap_mlkem512_kem_enc(ct, ss1, pk);
            dap_mlkem512_kem_dec(ss2, ct, sk);
            BARRIER();
        }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < 10000; i++) {
            dap_mlkem512_kem_enc(ct, ss1, pk);
            BARRIER();
        }
        uint64_t dc_enc = rdtsc() - t0;

        t0 = rdtsc();
        for (int i = 0; i < 10000; i++) {
            dap_mlkem512_kem_dec(ss2, ct, sk);
            BARRIER();
        }
        uint64_t dc_dec = rdtsc() - t0;

        printf("ML-KEM-512:\n");
        printf("  encaps: %7.0f cycles  %6.1f ns\n",
               (double)dc_enc / 10000, (double)dc_enc / 10000 * ns_per_cycle);
        printf("  decaps: %7.0f cycles  %6.1f ns\n",
               (double)dc_dec / 10000, (double)dc_dec / 10000 * ns_per_cycle);
        printf("  total:  %7.0f cycles  %6.1f ns\n\n",
               (double)(dc_enc + dc_dec) / 10000, (double)(dc_enc + dc_dec) / 10000 * ns_per_cycle);
    }
    {
        uint8_t pk[1184], sk[2400], ct[1088], ss1[32], ss2[32];

        dap_mlkem768_kem_keypair(pk, sk);
        for (int w = 0; w < 500; w++) {
            dap_mlkem768_kem_enc(ct, ss1, pk);
            dap_mlkem768_kem_dec(ss2, ct, sk);
            BARRIER();
        }
        uint64_t t0 = rdtsc();
        for (int i = 0; i < 10000; i++) {
            dap_mlkem768_kem_enc(ct, ss1, pk);
            BARRIER();
        }
        uint64_t dc_enc = rdtsc() - t0;

        t0 = rdtsc();
        for (int i = 0; i < 10000; i++) {
            dap_mlkem768_kem_dec(ss2, ct, sk);
            BARRIER();
        }
        uint64_t dc_dec = rdtsc() - t0;

        printf("ML-KEM-768:\n");
        printf("  encaps: %7.0f cycles  %6.1f ns\n",
               (double)dc_enc / 10000, (double)dc_enc / 10000 * ns_per_cycle);
        printf("  decaps: %7.0f cycles  %6.1f ns\n",
               (double)dc_dec / 10000, (double)dc_dec / 10000 * ns_per_cycle);
        printf("  total:  %7.0f cycles  %6.1f ns\n\n",
               (double)(dc_enc + dc_dec) / 10000, (double)(dc_enc + dc_dec) / 10000 * ns_per_cycle);
    }

    return 0;
}
