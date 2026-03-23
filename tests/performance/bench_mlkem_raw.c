/**
 * Raw ML-KEM microbenchmark — measures keygen/encaps/decaps without
 * any allocation overhead, for direct comparison with liboqs speed_kem.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* ML-KEM-768 */
#define PK_BYTES  1184
#define SK_BYTES  2400
#define CT_BYTES  1088
#define SS_BYTES  32

int dap_mlkem768_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem768_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem768_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ML-KEM-512 */
#define PK512  800
#define SK512  1632
#define CT512  768

int dap_mlkem512_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem512_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem512_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

/* ML-KEM-1024 */
#define PK1024  1568
#define SK1024  3168
#define CT1024  1568

int dap_mlkem1024_kem_keypair(uint8_t *pk, uint8_t *sk);
int dap_mlkem1024_kem_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk);
int dap_mlkem1024_kem_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define WARMUP 500
#define ITERS  10000

#define BARRIER() __asm__ volatile("" ::: "memory")

#define BENCH(label, call) do {                                   \
    for (int _w = 0; _w < WARMUP; _w++) { call; BARRIER(); }     \
    uint64_t _t0 = now_ns();                                      \
    for (int _i = 0; _i < ITERS; _i++) { call; BARRIER(); }      \
    uint64_t _dt = now_ns() - _t0;                                \
    printf("  %-12s %8d iters  %9.3f us/op\n",                   \
           label, ITERS, (double)_dt / (ITERS * 1000.0));        \
} while(0)

int main(void) {

    printf("=== ML-KEM-512 raw ===\n");
    {
        uint8_t pk[PK512], sk[SK512], ct[CT512], ss1[SS_BYTES], ss2[SS_BYTES];
        BENCH("keygen", dap_mlkem512_kem_keypair(pk, sk));
        dap_mlkem512_kem_keypair(pk, sk);
        BENCH("encaps", dap_mlkem512_kem_enc(ct, ss1, pk));
        dap_mlkem512_kem_enc(ct, ss1, pk);
        BENCH("decaps", dap_mlkem512_kem_dec(ss2, ct, sk));
    }
    printf("\n=== ML-KEM-768 raw ===\n");
    {
        uint8_t pk[PK_BYTES], sk[SK_BYTES], ct[CT_BYTES], ss1[SS_BYTES], ss2[SS_BYTES];
        BENCH("keygen", dap_mlkem768_kem_keypair(pk, sk));
        dap_mlkem768_kem_keypair(pk, sk);
        BENCH("encaps", dap_mlkem768_kem_enc(ct, ss1, pk));
        dap_mlkem768_kem_enc(ct, ss1, pk);
        BENCH("decaps", dap_mlkem768_kem_dec(ss2, ct, sk));
        if (memcmp(ss1, ss2, SS_BYTES) != 0)
            printf("  !!! CORRECTNESS FAILURE: ss1 != ss2 !!!\n");
        else
            printf("  Correctness: OK (shared secrets match)\n");
    }
    printf("\n=== ML-KEM-1024 raw ===\n");
    {
        uint8_t pk[PK1024], sk[SK1024], ct[CT1024], ss1[SS_BYTES], ss2[SS_BYTES];
        BENCH("keygen", dap_mlkem1024_kem_keypair(pk, sk));
        dap_mlkem1024_kem_keypair(pk, sk);
        BENCH("encaps", dap_mlkem1024_kem_enc(ct, ss1, pk));
        dap_mlkem1024_kem_enc(ct, ss1, pk);
        BENCH("decaps", dap_mlkem1024_kem_dec(ss2, ct, sk));
    }
    return 0;
}
