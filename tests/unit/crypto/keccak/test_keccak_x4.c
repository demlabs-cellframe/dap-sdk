/**
 * @file test_keccak_x4.c
 * @brief Correctness + benchmark for 4-way parallel Keccak-p[1600] and SHAKE x4
 *
 * Tests:
 *   1. x4 permutation: ref vs AVX2 (bit-exact)
 *   2. SHAKE128 x4: output matches 4× sequential SHAKE128
 *   3. SHAKE256 x4: output matches 4× sequential SHAKE256
 *   4. Benchmark: x4 dispatch vs 4× sequential permutation throughput
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "dap_common.h"
#include "dap_hash_keccak.h"
#include "dap_hash_keccak_x4.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dap_hash_shake_x4.h"
#include "dap_cpu_detect.h"

#define LOG_TAG "test_keccak_x4"

static int g_pass = 0, g_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { printf("  \033[31mFAIL: %s\033[0m\n", msg); g_fail++; return; } \
} while(0)
#define TEST_PASS(msg) do { printf("  \033[32mPASS: %s\033[0m\n", msg); g_pass++; } while(0)

/* Deterministic seed for reproducibility */
static void s_fill_random(uint8_t *buf, size_t len, uint32_t seed)
{
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245u + 12345u;
        buf[i] = (uint8_t)(seed >> 16);
    }
}

// ============================================================================
// Test 1: x4 permutation correctness (ref vs SIMD)
// ============================================================================

static void s_test_x4_permute_correctness(void)
{
    printf("\n[Keccak x4 permutation correctness]\n");

    dap_keccak_x4_state_t l_ref, l_simd;
    uint8_t l_data[4][200];

    for (int trial = 0; trial < 5; trial++) {
        /* Fill 4 different "states" with pseudo-random data */
        for (int j = 0; j < 4; j++)
            s_fill_random(l_data[j], 200, 0xDEAD0000u + (uint32_t)(trial * 4 + j));

        /* Load into x4 state via XOR (state is zeroed first) */
        dap_keccak_x4_init(&l_ref);
        for (int j = 0; j < 4; j++)
            dap_keccak_x4_xor_bytes(&l_ref, (unsigned)j, l_data[j], 200);
        memcpy(&l_simd, &l_ref, sizeof(l_ref));

        /* Reference x4 (4× sequential) */
        dap_keccak_x4_permute_ref(&l_ref);

        /* Dispatch x4 (SIMD if available) */
        dap_keccak_x4_permute(&l_simd);

        int l_match = memcmp(&l_ref, &l_simd, sizeof(l_ref));
        char l_msg[80];
        snprintf(l_msg, sizeof(l_msg), "x4 permute trial %d: dispatch matches ref", trial);
        TEST_ASSERT(l_match == 0, l_msg);
        TEST_PASS(l_msg);
    }

    /* Verify _opt (4× single-state dispatch) matches ref */
    {
        dap_keccak_x4_state_t l_a, l_b;
        dap_keccak_x4_init(&l_a);
        for (int j = 0; j < 4; j++) {
            s_fill_random(l_data[j], 200, 0xBEEF0000u + (uint32_t)j);
            dap_keccak_x4_xor_bytes(&l_a, (unsigned)j, l_data[j], 200);
        }
        memcpy(&l_b, &l_a, sizeof(l_a));
        dap_keccak_x4_permute_ref(&l_a);
        dap_keccak_x4_permute_opt(&l_b);
        TEST_ASSERT(memcmp(&l_a, &l_b, sizeof(l_a)) == 0,
                    "x4 permute: _opt matches ref");
        TEST_PASS("x4 permute: _opt matches ref");
    }

#if DAP_CPU_DETECT_X86
    {
        dap_cpu_features_t l_f = dap_cpu_detect_features();
        if (l_f.has_avx2) {
            dap_keccak_x4_state_t l_a, l_b;
            dap_keccak_x4_init(&l_a);
            for (int j = 0; j < 4; j++) {
                s_fill_random(l_data[j], 200, 0xCAFE0000u + (uint32_t)j);
                dap_keccak_x4_xor_bytes(&l_a, (unsigned)j, l_data[j], 200);
            }
            memcpy(&l_b, &l_a, sizeof(l_a));
            dap_keccak_x4_permute_ref(&l_a);
            dap_keccak_x4_permute_avx2(&l_b);
            TEST_ASSERT(memcmp(&l_a, &l_b, sizeof(l_a)) == 0,
                        "x4 permute: AVX2 matches ref");
            TEST_PASS("x4 permute: AVX2 matches ref");
        }
    }
#endif
}

// ============================================================================
// Test 2: SHAKE128 x4 vs 4× sequential
// ============================================================================

static void s_test_shake128_x4(void)
{
    printf("\n[SHAKE128 x4 correctness]\n");

    uint8_t l_in[4][34]; /* typical: 32-byte seed + 2-byte nonce */
    for (int j = 0; j < 4; j++)
        s_fill_random(l_in[j], 34, 0xABCD0000u + (uint32_t)j);

    /* Sequential: 4× individual SHAKE128 */
    size_t l_nblocks = 5;
    size_t l_outlen = l_nblocks * DAP_KECCAK_SHAKE128_RATE;
    uint8_t *l_seq[4], *l_par[4];
    for (int j = 0; j < 4; j++) {
        l_seq[j] = (uint8_t *)calloc(1, l_outlen);
        l_par[j] = (uint8_t *)calloc(1, l_outlen);

        uint64_t l_state[25] = {0};
        dap_hash_shake128_absorb(l_state, l_in[j], 34);
        dap_hash_shake128_squeezeblocks(l_seq[j], l_nblocks, l_state);
    }

    /* Parallel: SHAKE128 x4 */
    dap_keccak_x4_state_t l_x4;
    dap_hash_shake128_x4_absorb(&l_x4, l_in[0], l_in[1], l_in[2], l_in[3], 34);
    dap_hash_shake128_x4_squeezeblocks(l_par[0], l_par[1], l_par[2], l_par[3],
                                        l_nblocks, &l_x4);

    for (int j = 0; j < 4; j++) {
        char l_msg[64];
        snprintf(l_msg, sizeof(l_msg), "SHAKE128 x4 instance %d matches sequential", j);
        TEST_ASSERT(memcmp(l_seq[j], l_par[j], l_outlen) == 0, l_msg);
        TEST_PASS(l_msg);
        free(l_seq[j]);
        free(l_par[j]);
    }
}

// ============================================================================
// Test 3: SHAKE256 x4 vs 4× sequential
// ============================================================================

static void s_test_shake256_x4(void)
{
    printf("\n[SHAKE256 x4 correctness]\n");

    uint8_t l_in[4][64];
    for (int j = 0; j < 4; j++)
        s_fill_random(l_in[j], 64, 0xFACE0000u + (uint32_t)j);

    size_t l_nblocks = 4;
    size_t l_outlen = l_nblocks * DAP_KECCAK_SHAKE256_RATE;
    uint8_t *l_seq[4], *l_par[4];
    for (int j = 0; j < 4; j++) {
        l_seq[j] = (uint8_t *)calloc(1, l_outlen);
        l_par[j] = (uint8_t *)calloc(1, l_outlen);

        uint64_t l_state[25] = {0};
        dap_hash_shake256_absorb(l_state, l_in[j], 64);
        dap_hash_shake256_squeezeblocks(l_seq[j], l_nblocks, l_state);
    }

    dap_keccak_x4_state_t l_x4;
    dap_hash_shake256_x4_absorb(&l_x4, l_in[0], l_in[1], l_in[2], l_in[3], 64);
    dap_hash_shake256_x4_squeezeblocks(l_par[0], l_par[1], l_par[2], l_par[3],
                                        l_nblocks, &l_x4);

    for (int j = 0; j < 4; j++) {
        char l_msg[64];
        snprintf(l_msg, sizeof(l_msg), "SHAKE256 x4 instance %d matches sequential", j);
        TEST_ASSERT(memcmp(l_seq[j], l_par[j], l_outlen) == 0, l_msg);
        TEST_PASS(l_msg);
        free(l_seq[j]);
        free(l_par[j]);
    }
}

// ============================================================================
// Benchmark: x4 permutation throughput
// ============================================================================

static double s_get_time_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

static void s_bench_x4_permute(void)
{
    printf("\n[Keccak x4 permutation benchmark]\n");

    dap_keccak_x4_state_t l_state;
    dap_keccak_x4_init(&l_state);
    /* Warm up */
    for (int i = 0; i < 100; i++)
        dap_keccak_x4_permute(&l_state);

    int l_iters = 10000;

    /* Benchmark: 4× sequential pure-C reference */
    {
        double l_start = s_get_time_sec();
        for (int i = 0; i < l_iters; i++)
            dap_keccak_x4_permute_ref(&l_state);
        double l_elapsed = s_get_time_sec() - l_start;
        double l_perms_per_sec = (double)l_iters * 4.0 / l_elapsed;
        printf("  ref  (4× scalar):  %.0f perms/sec (%.1f us per x4 call)\n",
               l_perms_per_sec, l_elapsed / l_iters * 1e6);
    }

    /* Benchmark: _opt (4× single-state dispatch SIMD) */
    {
        double l_start = s_get_time_sec();
        for (int i = 0; i < l_iters; i++)
            dap_keccak_x4_permute_opt(&l_state);
        double l_elapsed = s_get_time_sec() - l_start;
        double l_perms_per_sec = (double)l_iters * 4.0 / l_elapsed;
        printf("  opt  (4× 1x SIMD): %.0f perms/sec (%.1f us per x4 call)\n",
               l_perms_per_sec, l_elapsed / l_iters * 1e6);
    }

    /* Benchmark: dispatch (native x4 SIMD if available, else _opt) */
    {
        double l_start = s_get_time_sec();
        for (int i = 0; i < l_iters; i++)
            dap_keccak_x4_permute(&l_state);
        double l_elapsed = s_get_time_sec() - l_start;
        double l_perms_per_sec = (double)l_iters * 4.0 / l_elapsed;
        printf("  dispatch (x4):     %.0f perms/sec (%.1f us per x4 call)\n",
               l_perms_per_sec, l_elapsed / l_iters * 1e6);
    }

#if DAP_CPU_DETECT_X86
    {
        dap_cpu_features_t l_f = dap_cpu_detect_features();
        if (l_f.has_avx2) {
            double l_start = s_get_time_sec();
            for (int i = 0; i < l_iters; i++)
                dap_keccak_x4_permute_avx2(&l_state);
            double l_elapsed = s_get_time_sec() - l_start;
            double l_perms_per_sec = (double)l_iters * 4.0 / l_elapsed;
            printf("  AVX2 (native x4):  %.0f perms/sec (%.1f us per x4 call)\n",
                   l_perms_per_sec, l_elapsed / l_iters * 1e6);
        }
    }
#endif

    g_pass++;
    printf("  \033[32mPASS: benchmark completed\033[0m\n");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    printf("=== Keccak x4 / SHAKE x4 Tests ===\n");
    dap_cpu_print_features();

    s_test_x4_permute_correctness();
    s_test_shake128_x4();
    s_test_shake256_x4();
    s_bench_x4_permute();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
