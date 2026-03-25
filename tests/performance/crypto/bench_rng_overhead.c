/**
 * Microbenchmark: measure dap_random_bytes overhead vs a simple ChaCha20 DRBG.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

int dap_random_bytes(void *a_buf, unsigned int a_nbytes);

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

#define BARRIER() __asm__ volatile("" ::: "memory")

int main(void) {
    uint8_t buf[32];
    int N = 100000;

    for (int w = 0; w < 1000; w++) { dap_random_bytes(buf, 32); BARRIER(); }

    uint64_t t0 = now_ns();
    for (int i = 0; i < N; i++) { dap_random_bytes(buf, 32); BARRIER(); }
    uint64_t dt = now_ns() - t0;
    printf("dap_random_bytes(32): %.3f us/call  (%d calls)\n",
           (double)dt / (N * 1000.0), N);

    N = 100000;
    for (int w = 0; w < 1000; w++) { dap_random_bytes(buf, 1); BARRIER(); }
    t0 = now_ns();
    for (int i = 0; i < N; i++) { dap_random_bytes(buf, 1); BARRIER(); }
    dt = now_ns() - t0;
    printf("dap_random_bytes(1):  %.3f us/call  (%d calls)\n",
           (double)dt / (N * 1000.0), N);

    return 0;
}
