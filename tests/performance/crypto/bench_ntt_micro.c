/**
 * NTT micro-benchmark — measures individual NTT operations in isolation.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define KYBER_N 256

void dap_mlkem768_ntt(int16_t a_coeffs[KYBER_N]);
void dap_mlkem768_invntt(int16_t a_coeffs[KYBER_N]);
void dap_mlkem768_nttpack(int16_t a_coeffs[KYBER_N]);
void dap_mlkem768_nttunpack(int16_t a_coeffs[KYBER_N]);
int dap_common_init(const char *, const char **, const char *);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BARRIER() __asm__ volatile("" ::: "memory")

#define WARMUP 5000
#define ITERS  100000

int main(void) {
    dap_common_init("bench_ntt", NULL, NULL);

    int16_t coeffs[KYBER_N] __attribute__((aligned(32)));
    for (int i = 0; i < KYBER_N; i++)
        coeffs[i] = (int16_t)(i * 17 % 3329);

    printf("=== NTT Micro-benchmark (%d iters) ===\n", ITERS);

    /* NTT forward */
    for (int w = 0; w < WARMUP; w++) { dap_mlkem768_ntt(coeffs); BARRIER(); }
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) { dap_mlkem768_ntt(coeffs); BARRIER(); }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  NTT forward:   %7.1f ns\n", ns);
    }

    /* nttpack */
    for (int w = 0; w < WARMUP; w++) { dap_mlkem768_nttpack(coeffs); BARRIER(); }
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) { dap_mlkem768_nttpack(coeffs); BARRIER(); }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  nttpack:       %7.1f ns\n", ns);
    }

    /* NTT inverse */
    for (int w = 0; w < WARMUP; w++) { dap_mlkem768_invntt(coeffs); BARRIER(); }
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) { dap_mlkem768_invntt(coeffs); BARRIER(); }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  NTT inverse:   %7.1f ns\n", ns);
    }

    /* nttunpack */
    for (int w = 0; w < WARMUP; w++) { dap_mlkem768_nttunpack(coeffs); BARRIER(); }
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) { dap_mlkem768_nttunpack(coeffs); BARRIER(); }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  nttunpack:     %7.1f ns\n", ns);
    }

    /* Combined forward+pack */
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
            dap_mlkem768_ntt(coeffs);
            dap_mlkem768_nttpack(coeffs);
            BARRIER();
        }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  fwd+pack:      %7.1f ns\n", ns);
    }

    /* Combined unpack+inverse */
    {
        uint64_t t0 = now_ns();
        for (int i = 0; i < ITERS; i++) {
            dap_mlkem768_nttunpack(coeffs);
            dap_mlkem768_invntt(coeffs);
            BARRIER();
        }
        double ns = (double)(now_ns() - t0) / ITERS;
        printf("  unpack+inv:    %7.1f ns\n", ns);
    }

    return 0;
}
