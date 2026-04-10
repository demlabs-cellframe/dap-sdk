/**
 * High-resolution timestamps for microbenchmarks.
 * x86: LFENCE+RDTSC; AArch64: CNTVCT_EL0; otherwise CLOCK_MONOTONIC ns.
 */
#ifndef TESTS_PERF_CRYPTO_BENCH_PERF_TICKS_H
#define TESTS_PERF_CRYPTO_BENCH_PERF_TICKS_H

#include <stdint.h>
#include <time.h>

static inline uint64_t bench_perf_ticks(void)
{
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
    unsigned lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#elif defined(__aarch64__)
    uint64_t v;
    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v));
    return v;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

#endif
