/**
 * Microbenchmark: Keccak 1x permutation, x4 permutation, SHA3-256 on various sizes.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include "dap_hash_keccak.h"
#include "dap_hash_keccak_x4.h"
#include "dap_hash_sha3.h"

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BARRIER() __asm__ volatile("" ::: "memory")

#define BENCH(label, N, call) do {                                \
    for (int _w = 0; _w < (N)/10; _w++) { call; BARRIER(); }     \
    uint64_t _t0 = now_ns();                                      \
    for (int _i = 0; _i < (N); _i++) { call; BARRIER(); }        \
    uint64_t _dt = now_ns() - _t0;                                \
    printf("  %-40s %8d iters  %8.1f ns/op  (%7.3f us/op)\n",    \
           label, (N), (double)_dt / (N), (double)_dt / ((N) * 1000.0)); \
} while(0)

int main(void) {
    printf("=== Keccak permutation microbenchmark ===\n\n");

    /* 1x permutation */
    {
        dap_hash_keccak_state_t state;
        memset(&state, 0x42, sizeof(state));
        BENCH("Keccak-f[1600] 1x permute", 1000000,
              dap_hash_keccak_permute(&state));
    }

    /* x4 permutation */
    {
        dap_keccak_x4_state_t st4;
        memset(&st4, 0x42, sizeof(st4));
        BENCH("Keccak-f[1600] x4 permute", 1000000,
              dap_keccak_x4_permute(&st4));
    }

    printf("\n=== SHA3-256 (hash_h) on various sizes ===\n\n");

    /* SHA3-256 on small and large inputs */
    {
        uint8_t out[32];
        uint8_t data32[32]; memset(data32, 0xAB, 32);
        BENCH("SHA3-256(32 bytes)", 500000,
              dap_hash_sha3_256_raw(out, data32, 32));

        uint8_t data64[64]; memset(data64, 0xAB, 64);
        BENCH("SHA3-256(64 bytes)", 500000,
              dap_hash_sha3_256_raw(out, data64, 64));

        uint8_t data136[136]; memset(data136, 0xAB, 136);
        BENCH("SHA3-256(136 bytes = 1 block)", 500000,
              dap_hash_sha3_256_raw(out, data136, 136));

        uint8_t data1184[1184]; memset(data1184, 0xAB, 1184);
        BENCH("SHA3-256(1184 bytes = ML-KEM-768 pk)", 200000,
              dap_hash_sha3_256_raw(out, data1184, 1184));
    }

    printf("\n=== SHAKE128 x4 absorb+squeeze (ML-KEM gen_matrix style) ===\n\n");
    {
        dap_keccak_x4_state_t st;
        uint8_t seed[34]; memset(seed, 0xCD, 34);
        uint8_t in0[34], in1[34], in2[34], in3[34];
        memcpy(in0, seed, 34); in0[32] = 0; in0[33] = 0;
        memcpy(in1, seed, 34); in1[32] = 1; in1[33] = 0;
        memcpy(in2, seed, 34); in2[32] = 2; in2[33] = 0;
        memcpy(in3, seed, 34); in3[32] = 0; in3[33] = 1;
        uint8_t out0[504], out1[504], out2[504], out3[504];

        BENCH("SHAKE128 x4: absorb(34) + squeeze(3 blocks)", 200000, ({
            dap_keccak_x4_init(&st);
            dap_keccak_x4_xor_bytes_all(&st, in0, in1, in2, in3, 34);
            /* pad */
            for (unsigned j = 0; j < 4; j++) {
                size_t lane = 34 / 8, byte = 34 % 8;
                ((uint8_t *)st.lanes)[(lane * 4 + j) * 8 + byte] ^= 0x1F;
                size_t last = 167;
                lane = last / 8; byte = last % 8;
                ((uint8_t *)st.lanes)[(lane * 4 + j) * 8 + byte] ^= 0x80;
            }
            dap_keccak_x4_permute(&st);
            dap_keccak_x4_extract_bytes_all(&st, out0, out1, out2, out3, 168);
            dap_keccak_x4_permute(&st);
            dap_keccak_x4_extract_bytes_all(&st, out0+168, out1+168, out2+168, out3+168, 168);
            dap_keccak_x4_permute(&st);
            dap_keccak_x4_extract_bytes_all(&st, out0+336, out1+336, out2+336, out3+336, 168);
        }));
    }

    return 0;
}
