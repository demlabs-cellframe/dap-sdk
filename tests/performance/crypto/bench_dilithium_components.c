/**
 * ML-DSA (Dilithium) component-level profiling — identifies bottlenecks
 * in the verify path by timing individual operations (RDTSC / CNTVCT / clock).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "bench_perf_ticks.h"
#include "dap_common.h"
#include "dap_cpu_arch.h"
#include "dilithium_params.h"
#include "dilithium_poly.h"
#include "dilithium_polyvec.h"
#include "dilithium_sign.h"
#include "dilithium_packing.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"

#define BARRIER() __asm__ volatile("" ::: "memory")
#define ITERS    10000
#define WARMUP   500
#define CRYSTALS_QINV_VAL 58728449

static int32_t crystals_mont_reduce(int64_t a) {
    int32_t t = (int32_t)((uint32_t)(int32_t)a * (uint32_t)CRYSTALS_QINV_VAL);
    return (int32_t)((a - (int64_t)t * Q) >> 32);
}

#define BENCH_CYC(label, setup, call)                                      \
do {                                                                       \
    setup;                                                                 \
    for (int _w = 0; _w < WARMUP; _w++) { call; BARRIER(); }              \
    uint64_t _t0 = bench_perf_ticks();                                      \
    for (int _i = 0; _i < ITERS; _i++) { call; BARRIER(); }               \
    uint64_t _dc = bench_perf_ticks() - _t0;                              \
    printf("  %-42s %8.0f cyc  %7.1f ns\n",                               \
           label, (double)_dc / ITERS, (double)_dc / ITERS * ns_cyc);     \
} while(0)

int main(void) {
    dap_common_init("bench_dil", NULL);

    double ns_cyc;
    {
        struct timespec t0, t1;
        uint64_t c0, c1;
        clock_gettime(CLOCK_MONOTONIC, &t0); c0 = bench_perf_ticks();
        volatile int sink = 0;
        for (int i = 0; i < 2000000; i++) sink += i;
        clock_gettime(CLOCK_MONOTONIC, &t1); c1 = bench_perf_ticks();
        uint64_t dt = (t1.tv_sec - t0.tv_sec) * 1000000000ULL + (t1.tv_nsec - t0.tv_nsec);
        ns_cyc = (double)dt / (c1 - c0);
    }

    printf("=== ML-DSA Component Profiling (%d iters) ===\n", ITERS);
    printf("CPU: %.2f GHz (%.3f ns/cyc)\n\n", 1.0 / ns_cyc, ns_cyc);

    dilithium_param_t p;
    dilithium_params_init(&p, MODE_1);
    printf("MODE_1 (ML-DSA-44): K=%u L=%u\n\n", p.PARAM_K, p.PARAM_L);

    unsigned char rho[SEEDBYTES], mu[CRHBYTES];
    memset(rho, 0xAB, SEEDBYTES);
    memset(mu, 0xCD, CRHBYTES);

    polyvecl mat[p.PARAM_K], z;
    polyveck t1, w1, h, tmp1, tmp2;
    poly c_val, chat, cp;

    for (unsigned i = 0; i < NN; i++) {
        z.vec[0].coeffs[i] = i * 31u % Q;
        z.vec[1].coeffs[i] = i * 47u % Q;
        z.vec[2].coeffs[i] = i * 59u % Q;
        t1.vec[0].coeffs[i] = i * 71u % Q;
        t1.vec[1].coeffs[i] = i * 83u % Q;
        t1.vec[2].coeffs[i] = i * 97u % Q;
        t1.vec[3].coeffs[i] = i * 101u % Q;
        c_val.coeffs[i] = 0;
        h.vec[0].coeffs[i] = (i % 20 == 0) ? 1 : 0;
        h.vec[1].coeffs[i] = 0;
        h.vec[2].coeffs[i] = 0;
        h.vec[3].coeffs[i] = 0;
    }
    c_val.coeffs[200] = 1; c_val.coeffs[210] = Q - 1;

    unsigned char shake_out[5 * 168];
    dap_hash_shake256(shake_out, sizeof(shake_out), rho, SEEDBYTES);

    printf("--- AVX2 vs AVX512 NTT variant comparison ---\n");

/* x86 ASM benchmarks - System V ABI only, not available on Windows */
#if DAP_PLATFORM_X86 && !defined(_WIN32)
    {
        extern void dap_dilithium_ntt_forward_avx2(int32_t [256]);
        extern void dap_dilithium_ntt_inverse_avx2(int32_t [256]);
        extern void dap_dilithium_pointwise_mont_avx2(int32_t *, const int32_t *, const int32_t *);
        extern void dap_dilithium_ntt_forward_avx512(int32_t [256]);
        extern void dap_dilithium_ntt_inverse_avx512(int32_t [256]);
        extern void dap_dilithium_pointwise_mont_avx512(int32_t *, const int32_t *, const int32_t *);
        extern void dap_dilithium_ntt_forward_avx2_asm(int32_t [256]);
        extern void dap_dilithium_ntt_inverse_avx2_asm(int32_t [256]);
        extern void dap_dilithium_pointwise_mont_avx2_asm(int32_t *, const int32_t *, const int32_t *);
        extern void dap_dilithium_ntt_fwd_fused_avx2(int32_t [256]);
        extern void dap_dilithium_invntt_fused_avx2(int32_t [256]);
        extern void dap_dilithium_nttunpack_avx2(int32_t [256]);

        int32_t tmp_a[256] __attribute__((aligned(64)));
        int32_t tmp_b[256] __attribute__((aligned(64)));
        int32_t tmp_c[256] __attribute__((aligned(64)));
        int32_t ref_ntt[256] __attribute__((aligned(64)));
        for (int i = 0; i < 256; i++) {
            tmp_a[i] = (int32_t)(i * 31u % Q);
            tmp_b[i] = (int32_t)(i * 47u % Q);
        }

        /* CRYSTALS reference scalar Montgomery reduce + NTT for verification */
        {
            static const int32_t crystals_zetas[256] = {
                0, 25847, -2608894, -518909, 237124, -777960, -876248, 466468,
                1826347, 2353451, -359251, -2091905, 3119733, -2884855, 3111497, 2680103,
                2725464, 1024112, -1079900, 3585928, -549488, -1119584, 2619752, -2108549,
                -2118186, -3859737, -1399561, -3277672, 1757237, -19422, 4010497, 280005,
                2706023, 95776, 3077325, 3530437, -1661693, -3592148, -2537516, 3915439,
                -3861115, -3043716, 3574422, -2867647, 3539968, -300467, 2348700, -539299,
                -1699267, -1643818, 3505694, -3821735, 3507263, -2140649, -1600420, 3699596,
                811944, 531354, 954230, 3881043, 3900724, -2556880, 2071892, -2797779,
                -3930395, -1528703, -3677745, -3041255, -1452451, 3475950, 2176455, -1585221,
                -1257611, 1939314, -4083598, -1000202, -3190144, -3157330, -3632928, 126922,
                3412210, -983419, 2147896, 2715295, -2967645, -3693493, -411027, -2477047,
                -671102, -1228525, -22981, -1308169, -381987, 1349076, 1852771, -1430430,
                -3343383, 264944, 508951, 3097992, 44288, -1100098, 904516, 3958618,
                -3724342, -8578, 1653064, -3249728, 2389356, -210977, 759969, -1316856,
                189548, -3553272, 3159746, -1851402, -2409325, -177440, 1315589, 1341330,
                1285669, -1584928, -812732, -1439742, -3019102, -3881060, -3628969, 3839961,
                2091667, 3407706, 2316500, 3817976, -3342478, 2244091, -2446433, -3562462,
                266997, 2434439, -1235728, 3513181, -3520352, -3759364, -1197226, -3193378,
                900702, 1859098, 909542, 819034, 495491, -1613174, -43260, -522500,
                -655327, -3122442, 2031748, 3207046, -3556995, -525098, -768622, -3595838,
                342297, 286988, -2437823, 4108315, 3437287, -3342277, 1735879, 203044,
                2842341, 2691481, -2590150, 1265009, 4055324, 1247620, 2486353, 1595974,
                -3767016, 1250494, 2635921, -3548272, -2994039, 1869119, 1903435, -1050970,
                -1333058, 1237275, -3318210, -1430225, -451100, 1312455, 3306115, -1962642,
                -1279661, 1917081, -2546312, -1374803, 1500165, 777191, 2235880, 3406031,
                -542412, -2831860, -1671176, -1846953, -2584293, -3724270, 594136, -3776993,
                -2013608, 2432395, 2454455, -164721, 1957272, 3369112, 185531, -1207385,
                -3183426, 162844, 1616392, 3014001, 810149, 1652634, -3694233, -1799107,
                -3038916, 3523897, 3866901, 269760, 2213111, -975884, 1717735, 472078,
                -426683, 1723600, -1803090, 1910376, -1667432, -1104333, -260646, -3833893,
                -2939036, -2235985, -420899, -2286327, 183443, -976891, 1612842, -3545687,
                -554416, 3919660, -48306, -1362209, 3937738, 1400424, -846154, 1976782
            };
            /* crystals_mont_reduce defined at file scope above */

            int32_t crystals_ref[256];
            memcpy(crystals_ref, tmp_a, 1024);

            unsigned int len, start, j, k = 0;
            for (len = 128; len >= 1; len >>= 1) {
                for (start = 0; start < 256; start = j + len) {
                    int32_t zeta = crystals_zetas[++k];
                    for (j = start; j < start + len; ++j) {
                        int32_t t = crystals_mont_reduce((int64_t)zeta * crystals_ref[j + len]);
                        crystals_ref[j + len] = crystals_ref[j] - t;
                        crystals_ref[j] = crystals_ref[j] + t;
                    }
                }
            }

            memcpy(tmp_c, tmp_a, 1024);
            dap_dilithium_ntt_fwd_fused_avx2(tmp_c);

            /* Fwd NTT outputs CRYSTALS shuffled format: 8x8 transpose per quarter.
             * Apply the same permutation to scalar reference for comparison. */
            int32_t shuffled_ref[256] __attribute__((aligned(64)));
            for (int q = 0; q < 4; q++) {
                for (int row = 0; row < 8; row++)
                    for (int col = 0; col < 8; col++)
                        shuffled_ref[q*64 + col*8 + row] = crystals_ref[q*64 + row*8 + col];
            }

            int ntt_ok = 1, ntt_modq_ok = 1, ntt_modq_fail_cnt = 0;
            for (int i = 0; i < 256; i++) {
                if (shuffled_ref[i] != tmp_c[i]) {
                    int64_t diff = (int64_t)shuffled_ref[i] - (int64_t)tmp_c[i];
                    int32_t r = (int32_t)(((diff % Q) + Q) % Q);
                    ntt_ok = 0;
                    if (r != 0) {
                        ntt_modq_ok = 0;
                        ntt_modq_fail_cnt++;
                        if (ntt_modq_fail_cnt <= 4)
                            printf("  CRYSTALS FAIL[%d]: ref=%d fused=%d diff=%lld\n",
                                   i, shuffled_ref[i], tmp_c[i], (long long)diff);
                    }
                }
            }
            printf("  Fused vs CRYSTALS ref (shuffled): %s\n", ntt_ok ? "exact OK" : "exact FAIL, values differ by bounded rep");
            printf("  Fused vs CRYSTALS ref mod-Q:      %s (%d failures)\n\n",
                   ntt_modq_ok ? "OK" : "FAIL", ntt_modq_fail_cnt);

            if (!ntt_modq_ok) {
                printf("  Failing ranges: ");
                int in_range = 0, range_start = 0;
                for (int i = 0; i <= 256; i++) {
                    int fail = 0;
                    if (i < 256) {
                        int64_t diff = (int64_t)shuffled_ref[i] - (int64_t)tmp_c[i];
                        int32_t r = (int32_t)(((diff % Q) + Q) % Q);
                        fail = (r != 0);
                    }
                    if (fail && !in_range) { range_start = i; in_range = 1; }
                    if (!fail && in_range) {
                        printf("[%d-%d] ", range_start, i-1);
                        in_range = 0;
                    }
                }
                printf("\n");
            }

            /* Round-trip test: fwd_fused → inv_fused
             * Result = orig * f (mod Q) where f is the Montgomery scaling factor.
             * Check proportional consistency: rt[i]*orig[j] ≡ rt[j]*orig[i] (mod Q). */
            {
                int32_t rt[256] __attribute__((aligned(64)));
                memcpy(rt, tmp_a, 1024);
                dap_dilithium_ntt_fwd_fused_avx2(rt);
                dap_dilithium_invntt_fused_avx2(rt);

                int rt_ok = 1, rt_first_fail = -1;
                int ref_idx = -1;
                for (int i = 0; i < 256; i++) {
                    if (tmp_a[i] != 0) { ref_idx = i; break; }
                }

                for (int i = 0; i < 256; i++) {
                    if (tmp_a[i] == 0) {
                        int64_t r = ((int64_t)rt[i]) % Q;
                        if (r < 0) r += Q;
                        if (r != 0) {
                            if (rt_ok) { rt_first_fail = i; printf("  RT fail[%d]: orig=0, rt=%d (expected 0 mod Q)\n", i, rt[i]); }
                            rt_ok = 0;
                        }
                    } else if (ref_idx >= 0) {
                        int64_t lhs = ((int64_t)rt[i] * (int64_t)tmp_a[ref_idx]) % Q;
                        int64_t rhs = ((int64_t)rt[ref_idx] * (int64_t)tmp_a[i]) % Q;
                        if (lhs < 0) lhs += Q;
                        if (rhs < 0) rhs += Q;
                        if (lhs != rhs) {
                            if (rt_ok) { rt_first_fail = i; printf("  RT fail[%d]: proportional mismatch\n", i); }
                            rt_ok = 0;
                        }
                    }
                }
                printf("  Round-trip (fwd+inv fused): %s", rt_ok ? "OK" : "FAIL");
                if (rt_ok && ref_idx >= 0) {
                    int64_t f = ((int64_t)rt[ref_idx] * 100) / tmp_a[ref_idx];
                    printf(" (scale ~ %lld/100)", (long long)f);
                }
                printf("\n");
            }
        }

        /* Full NTT pipeline test: NTT(a) * NTT(b) → invNTT → should give a*b mod poly */
        {
            int32_t a_poly[256] __attribute__((aligned(64)));
            int32_t b_poly[256] __attribute__((aligned(64)));
            int32_t ntt_result[256] __attribute__((aligned(64)));
            int32_t scalar_result[256] __attribute__((aligned(64)));

            memset(a_poly, 0, sizeof(a_poly));
            memset(b_poly, 0, sizeof(b_poly));
            a_poly[0] = 1; a_poly[1] = 2; a_poly[2] = 3;
            b_poly[0] = 4; b_poly[1] = 5;

            memset(scalar_result, 0, sizeof(scalar_result));
            for (int ia = 0; ia < 256; ia++) {
                if (a_poly[ia] == 0) continue;
                for (int ib = 0; ib < 256; ib++) {
                    if (b_poly[ib] == 0) continue;
                    int idx = ia + ib;
                    int64_t val = (int64_t)a_poly[ia] * b_poly[ib];
                    if (idx < 256) {
                        scalar_result[idx] = (int32_t)((scalar_result[idx] + val) % Q);
                    } else {
                        scalar_result[idx - 256] = (int32_t)((scalar_result[idx - 256] - val) % Q);
                    }
                }
            }
            for (int i = 0; i < 256; i++) {
                if (scalar_result[i] < 0) scalar_result[i] += Q;
            }

            int32_t a_ntt[256] __attribute__((aligned(64)));
            int32_t b_ntt[256] __attribute__((aligned(64)));
            memcpy(a_ntt, a_poly, 1024);
            memcpy(b_ntt, b_poly, 1024);
            dap_dilithium_ntt_fwd_fused_avx2(a_ntt);
            dap_dilithium_ntt_fwd_fused_avx2(b_ntt);
            poly pa, pb, pc;
            memcpy(pa.coeffs, a_ntt, 1024);
            memcpy(pb.coeffs, b_ntt, 1024);
            poly_pointwise_invmontgomery(&pc, &pa, &pb);
            memcpy(ntt_result, pc.coeffs, 1024);
            dap_dilithium_invntt_fused_avx2(ntt_result);

            int pipe_ok = 1;
            for (int i = 0; i < 256; i++) {
                int32_t got = ((int64_t)ntt_result[i] % Q + Q) % Q;
                int32_t want = scalar_result[i];
                if (got != want) {
                    if (pipe_ok)
                        printf("  Pipeline fail[%d]: got=%d want=%d\n", i, got, want);
                    pipe_ok = 0;
                }
            }
            printf("  Full pipeline (NTT*pointwise*invNTT) simple: %s\n", pipe_ok ? "OK" : "FAIL");
        }

        /* Full pipeline with RANDOM data */
        {
            int32_t a_poly[256] __attribute__((aligned(64)));
            int32_t b_poly[256] __attribute__((aligned(64)));
            int32_t ntt_result[256] __attribute__((aligned(64)));
            int32_t scalar_result[256] __attribute__((aligned(64)));

            srand(42);
            for (int i = 0; i < 256; i++) {
                a_poly[i] = rand() % Q;
                b_poly[i] = rand() % Q;
            }

            memset(scalar_result, 0, sizeof(scalar_result));
            for (int ia = 0; ia < 256; ia++) {
                for (int ib = 0; ib < 256; ib++) {
                    int idx = ia + ib;
                    int64_t val = (int64_t)a_poly[ia] * b_poly[ib];
                    if (idx < 256) {
                        scalar_result[idx] = (int32_t)(((int64_t)scalar_result[idx] + val) % Q);
                    } else {
                        scalar_result[idx - 256] = (int32_t)(((int64_t)scalar_result[idx - 256] - val) % Q);
                    }
                }
            }
            for (int i = 0; i < 256; i++) {
                if (scalar_result[i] < 0) scalar_result[i] += Q;
            }

            int32_t a_ntt[256] __attribute__((aligned(64)));
            int32_t b_ntt[256] __attribute__((aligned(64)));
            memcpy(a_ntt, a_poly, 1024);
            memcpy(b_ntt, b_poly, 1024);
            dap_dilithium_ntt_fwd_fused_avx2(a_ntt);
            dap_dilithium_ntt_fwd_fused_avx2(b_ntt);
            poly pa2, pb2, pc2;
            memcpy(pa2.coeffs, a_ntt, 1024);
            memcpy(pb2.coeffs, b_ntt, 1024);
            poly_pointwise_invmontgomery(&pc2, &pa2, &pb2);
            memcpy(ntt_result, pc2.coeffs, 1024);
            dap_dilithium_invntt_fused_avx2(ntt_result);

            int pipe2_ok = 1;
            int fail_cnt = 0;
            for (int i = 0; i < 256; i++) {
                int32_t got = ((int64_t)ntt_result[i] % Q + Q) % Q;
                int32_t want = scalar_result[i];
                if (got != want) {
                    if (fail_cnt < 3)
                        printf("  Pipeline-rand fail[%d]: got=%d want=%d\n", i, got, want);
                    pipe2_ok = 0;
                    fail_cnt++;
                }
            }
            printf("  Full pipeline (NTT*pointwise*invNTT) random: %s (%d/%d match)\n",
                   pipe2_ok ? "OK" : "FAIL", 256 - fail_cnt, 256);
        }

        /* Pipeline test with inputs near Q (like polyeta_unpack produces) */
        {
            int32_t a_near_q[256] __attribute__((aligned(64)));
            int32_t b_near_q[256] __attribute__((aligned(64)));
            int32_t ntt_nearq[256] __attribute__((aligned(64)));

            memset(a_near_q, 0, sizeof(a_near_q));
            memset(b_near_q, 0, sizeof(b_near_q));
            a_near_q[0] = Q + 2; a_near_q[1] = Q + 1; a_near_q[2] = Q; a_near_q[3] = Q - 1; a_near_q[4] = Q - 2;
            b_near_q[0] = Q + 1; b_near_q[1] = Q - 1;

            int32_t a_small[256], b_small[256];
            memset(a_small, 0, sizeof(a_small));
            memset(b_small, 0, sizeof(b_small));
            a_small[0] = 2; a_small[1] = 1; a_small[2] = 0; a_small[3] = -1; a_small[4] = -2;
            b_small[0] = 1; b_small[1] = -1;

            int32_t scalar_r[256];
            memset(scalar_r, 0, sizeof(scalar_r));
            for (int ia = 0; ia < 256; ia++) {
                if (a_small[ia] == 0) continue;
                for (int ib = 0; ib < 256; ib++) {
                    if (b_small[ib] == 0) continue;
                    int idx = ia + ib;
                    int64_t val = (int64_t)a_small[ia] * b_small[ib];
                    if (idx < 256) scalar_r[idx] = (int32_t)((scalar_r[idx] + val) % Q);
                    else scalar_r[idx - 256] = (int32_t)((scalar_r[idx - 256] - val) % Q);
                }
            }
            for (int i = 0; i < 256; i++)
                if (scalar_r[i] < 0) scalar_r[i] += Q;

            int32_t a_ntt_q[256] __attribute__((aligned(64)));
            int32_t b_ntt_q[256] __attribute__((aligned(64)));
            memcpy(a_ntt_q, a_near_q, 1024);
            memcpy(b_ntt_q, b_near_q, 1024);
            dap_dilithium_ntt_fwd_fused_avx2(a_ntt_q);
            dap_dilithium_ntt_fwd_fused_avx2(b_ntt_q);
            {
                poly pa_q, pb_q, pc_q;
                memcpy(pa_q.coeffs, a_ntt_q, 1024);
                memcpy(pb_q.coeffs, b_ntt_q, 1024);
                poly_pointwise_invmontgomery(&pc_q, &pa_q, &pb_q);
                memcpy(ntt_nearq, pc_q.coeffs, 1024);
            }
            dap_dilithium_invntt_fused_avx2(ntt_nearq);

            int nearq_ok = 1, nearq_fails = 0;
            for (int i = 0; i < 256; i++) {
                int32_t got = ((int64_t)ntt_nearq[i] % Q + Q) % Q;
                int32_t want = scalar_r[i];
                if (got != want) {
                    if (nearq_fails < 5)
                        printf("  near-Q fail[%d]: got=%d want=%d\n", i, got, want);
                    nearq_ok = 0;
                    nearq_fails++;
                }
            }
            printf("  Pipeline with near-Q inputs: %s (%d/256 match)\n",
                   nearq_ok ? "OK" : "FAIL", 256 - nearq_fails);
        }

        /* Compare OLD NTT pipeline vs FUSED NTT pipeline for mat*y (sign-like flow) */
        {
            int32_t m_std[256] __attribute__((aligned(64)));
            int32_t y_orig[256] __attribute__((aligned(64)));

            srand(777);
            for (int i = 0; i < 256; i++) {
                m_std[i] = rand() % Q;
                y_orig[i] = (rand() % (2*131072)) - 131072;
            }

            int32_t y_ntt_old[256] __attribute__((aligned(64)));
            int32_t pw_old[256] __attribute__((aligned(64)));
            int32_t w_old[256] __attribute__((aligned(64)));
            memcpy(y_ntt_old, y_orig, 1024);
            dap_dilithium_ntt_forward_avx512(y_ntt_old);
            {
                poly pa_o, pb_o, pc_o;
                memcpy(pa_o.coeffs, m_std, 1024);
                memcpy(pb_o.coeffs, y_ntt_old, 1024);
                poly_pointwise_invmontgomery(&pc_o, &pa_o, &pb_o);
                memcpy(pw_old, pc_o.coeffs, 1024);
            }
            memcpy(w_old, pw_old, 1024);
            dap_dilithium_ntt_inverse_avx512(w_old);

            int32_t m_shuf[256] __attribute__((aligned(64)));
            int32_t y_ntt_fused[256] __attribute__((aligned(64)));
            int32_t pw_fused[256] __attribute__((aligned(64)));
            int32_t w_fused[256] __attribute__((aligned(64)));
            memcpy(m_shuf, m_std, 1024);
            dap_dilithium_nttunpack_avx2(m_shuf);
            memcpy(y_ntt_fused, y_orig, 1024);
            dap_dilithium_ntt_fwd_fused_avx2(y_ntt_fused);
            {
                poly pa_f, pb_f, pc_f;
                memcpy(pa_f.coeffs, m_shuf, 1024);
                memcpy(pb_f.coeffs, y_ntt_fused, 1024);
                poly_pointwise_invmontgomery(&pc_f, &pa_f, &pb_f);
                memcpy(pw_fused, pc_f.coeffs, 1024);
            }
            memcpy(w_fused, pw_fused, 1024);
            dap_dilithium_invntt_fused_avx2(w_fused);

            int cmp_ok = 1, cmp_fails = 0;
            for (int i = 0; i < 256; i++) {
                int32_t a_val = ((int64_t)w_old[i] % Q + Q) % Q;
                int32_t b_val = ((int64_t)w_fused[i] % Q + Q) % Q;
                if (a_val != b_val) {
                    if (cmp_fails < 5)
                        printf("  old_vs_fused[%d]: old=%d fused=%d\n", i, a_val, b_val);
                    cmp_ok = 0;
                    cmp_fails++;
                }
            }
            printf("  Old NTT vs Fused+nttunpack pipeline: %s (%d/256 match)\n",
                   cmp_ok ? "OK" : "FAIL", 256 - cmp_fails);
        }

        /* Test polyvecl_pointwise_acc_invmontgomery with fused NTT */
        {
            polyvecl u, v;
            poly w_acc, w_ref;
            poly t_ref;

            srand(123);
            for (unsigned k = 0; k < 3; k++) {
                for (int i = 0; i < 256; i++) {
                    u.vec[k].coeffs[i] = rand() % Q;
                    v.vec[k].coeffs[i] = rand() % Q;
                }
                dap_dilithium_ntt_fwd_fused_avx2((int32_t*)u.vec[k].coeffs);
                dap_dilithium_ntt_fwd_fused_avx2((int32_t*)v.vec[k].coeffs);
            }

            polyvecl_pointwise_acc_invmontgomery(&w_acc, &u, &v, &p);

            poly_pointwise_invmontgomery(&w_ref, u.vec + 0, v.vec + 0);
            for (unsigned k = 1; k < 3; k++) {
                poly_pointwise_invmontgomery(&t_ref, u.vec + k, v.vec + k);
                dilithium_poly_add(&w_ref, &w_ref, &t_ref);
            }

            int acc_ok = 1;
            for (int i = 0; i < 256; i++) {
                int32_t a_val = ((int64_t)w_acc.coeffs[i] % Q + Q) % Q;
                int32_t r_val = ((int64_t)w_ref.coeffs[i] % Q + Q) % Q;
                if (a_val != r_val) {
                    if (acc_ok)
                        printf("  pw_acc diff[%d]: acc=%d ref=%d\n", i, a_val, r_val);
                    acc_ok = 0;
                }
            }
            printf("  polyvecl_pw_acc vs manual: %s\n", acc_ok ? "OK" : "FAIL");
        }

        BENCH_CYC("ntt_forward AVX512",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_forward_avx512(tmp_a));
        BENCH_CYC("ntt_forward AVX2",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_forward_avx2(tmp_a));
        BENCH_CYC("ntt_forward AVX512 (2nd)",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_forward_avx512(tmp_a));
        BENCH_CYC("ntt_forward FUSED AVX2",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_fwd_fused_avx2(tmp_a));

        BENCH_CYC("ntt_inverse AVX512",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_inverse_avx512(tmp_a));
        BENCH_CYC("ntt_inverse AVX2",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_inverse_avx2(tmp_a));
        BENCH_CYC("ntt_inverse AVX512 (2nd)",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_inverse_avx512(tmp_a));

        dap_dilithium_ntt_forward_avx2(tmp_a);
        dap_dilithium_ntt_forward_avx2(tmp_b);

        BENCH_CYC("pointwise_mont AVX512",
                  (void)0,
                  dap_dilithium_pointwise_mont_avx512(tmp_c, tmp_a, tmp_b));
        BENCH_CYC("pointwise_mont AVX2",
                  (void)0,
                  dap_dilithium_pointwise_mont_avx2(tmp_c, tmp_a, tmp_b));
        BENCH_CYC("pointwise_mont AVX512 (2nd)",
                  (void)0,
                  dap_dilithium_pointwise_mont_avx512(tmp_c, tmp_a, tmp_b));

        printf("  --- ASM (pre-computed zeta_qinv) ---\n");
        BENCH_CYC("ntt_forward AVX2 ASM",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_forward_avx2_asm(tmp_a));
        BENCH_CYC("ntt_inverse AVX2 ASM",
                  memcpy(tmp_a, z.vec[0].coeffs, 1024),
                  dap_dilithium_ntt_inverse_avx2_asm(tmp_a));
        BENCH_CYC("pointwise_mont AVX2 ASM",
                  (void)0,
                  dap_dilithium_pointwise_mont_avx2_asm(tmp_c, tmp_a, tmp_b));
    }
    printf("\n");
#endif

    printf("--- Individual components (dispatched) ---\n");

    BENCH_CYC("expand_mat (K*L SHAKE128 x4 + rej)",
              (void)0,
              expand_mat(mat, rho, &p));

    BENCH_CYC("dilithium_poly_uniform (rej_sample)",
              (void)0,
              dilithium_poly_uniform(&z.vec[0], shake_out));

    BENCH_CYC("dilithium_poly_ntt (1 poly, dispatched)",
              (void)0,
              dilithium_poly_ntt(&z.vec[0]));

    BENCH_CYC("poly_invntt_montgomery (1 poly, dispatched)",
              (void)0,
              poly_invntt_montgomery(&z.vec[0]));

    BENCH_CYC("poly_pointwise_invmontgomery (dispatched)",
              (void)0,
              poly_pointwise_invmontgomery(&cp, &z.vec[0], &z.vec[1]));

    BENCH_CYC("polyvecl_pw_acc_invmont (L=3)",
              (void)0,
              polyvecl_pointwise_acc_invmontgomery(&cp, &mat[0], &z, &p));

    BENCH_CYC("polyvecl_ntt (L=3 NTTs)",
              (void)0,
              polyvecl_ntt(&z, &p));

    BENCH_CYC("polyveck_ntt (K=4 NTTs)",
              (void)0,
              polyveck_ntt(&t1, &p));

    BENCH_CYC("polyveck_invntt_montgomery (K=4)",
              (void)0,
              polyveck_invntt_montgomery(&t1, &p));

    BENCH_CYC("poly_reduce",
              (void)0,
              poly_reduce(&z.vec[0]));

    BENCH_CYC("poly_csubq",
              (void)0,
              poly_csubq(&z.vec[0]));

    BENCH_CYC("poly_decompose",
              (void)0,
              poly_decompose(&w1.vec[0], &tmp1.vec[0], &t1.vec[0]));

    BENCH_CYC("poly_use_hint",
              (void)0,
              poly_use_hint(&w1.vec[0], &t1.vec[0], &h.vec[0]));

    BENCH_CYC("polyw1_pack",
              (void)0,
              polyw1_pack((unsigned char *)shake_out, &w1.vec[0]));

    BENCH_CYC("poly_shiftl(D=14)",
              (void)0,
              poly_shiftl(&t1.vec[0], D));

    BENCH_CYC("dilithium_poly_add",
              (void)0,
              dilithium_poly_add(&cp, &z.vec[0], &z.vec[1]));

    BENCH_CYC("polyveck_reduce (K=4)",
              (void)0,
              polyveck_reduce(&t1, &p));

    BENCH_CYC("polyveck_csubq (K=4)",
              (void)0,
              polyveck_csubq(&t1, &p));

    BENCH_CYC("polyveck_use_hint (K=4)",
              (void)0,
              polyveck_use_hint(&w1, &t1, &h, &p));

    BENCH_CYC("challenge (SHAKE256 + sample)",
              (void)0,
              challenge(&cp, mu, &w1, &p));

    printf("\n--- FIPS 204 specific components ---\n");

    dilithium_param_t fips_p;
    dilithium_params_init(&fips_p, MLDSA_44);
    printf("FIPS 204 ML-DSA-44: K=%u L=%u gamma1=2^%u gamma2=%u\n",
           fips_p.PARAM_K, fips_p.PARAM_L, dil_gamma1_bits(&fips_p), dil_gamma2(&fips_p));

    unsigned char packed_z18[576], packed_z20[640];
    memset(packed_z18, 0x42, sizeof(packed_z18));
    memset(packed_z20, 0x42, sizeof(packed_z20));

    poly unp;
    BENCH_CYC("polyz_unpack_p (g17/18-bit, dispatched)",
              (void)0,
              polyz_unpack_p(&unp, packed_z18, &fips_p));

    dilithium_param_t fips65;
    dilithium_params_init(&fips65, MLDSA_65);
    BENCH_CYC("polyz_unpack_p (g19/20-bit, dispatched)",
              (void)0,
              polyz_unpack_p(&unp, packed_z20, &fips65));

    BENCH_CYC("poly_use_hint_p (g88, dispatched)",
              (void)0,
              poly_use_hint_p(&w1.vec[0], &t1.vec[0], &h.vec[0], &fips_p));

    BENCH_CYC("poly_use_hint_p (g32, dispatched)",
              (void)0,
              poly_use_hint_p(&w1.vec[0], &t1.vec[0], &h.vec[0], &fips65));

    unsigned char w1pack_buf[256];
    BENCH_CYC("polyw1_pack_p (g88, dispatched)",
              (void)0,
              polyw1_pack_p(w1pack_buf, &w1.vec[0], &fips_p));

    BENCH_CYC("polyw1_pack_p (g32, scalar)",
              (void)0,
              polyw1_pack_p(w1pack_buf, &w1.vec[0], &fips65));

    unsigned char c_tilde[32];
    memset(c_tilde, 0xDE, sizeof(c_tilde));
    BENCH_CYC("mldsa_sample_in_ball (tau=39)",
              (void)0,
              mldsa_sample_in_ball(&c_val, c_tilde, &fips_p));

    printf("\n--- Verify hot path simulation ---\n");
    BENCH_CYC("full_verify_core (legacy)",
              (void)0,
              ({
                  expand_mat(mat, rho, &p);
                  polyvecl_ntt(&z, &p);
                  for (unsigned _j = 0; _j < p.PARAM_K; _j++)
                      polyvecl_pointwise_acc_invmontgomery(tmp1.vec + _j, mat + _j, &z, &p);
                  chat = c_val;
                  dilithium_poly_ntt(&chat);
                  polyveck_ntt(&t1, &p);
                  for (unsigned _j = 0; _j < p.PARAM_K; _j++)
                      poly_pointwise_invmontgomery(tmp2.vec + _j, &chat, t1.vec + _j);
                  polyveck_sub(&tmp1, &tmp1, &tmp2, &p);
                  polyveck_reduce(&tmp1, &p);
                  polyveck_invntt_montgomery(&tmp1, &p);
                  polyveck_csubq(&tmp1, &p);
                  polyveck_use_hint(&w1, &tmp1, &h, &p);
                  challenge(&cp, mu, &w1, &p);
              }));

    BENCH_CYC("full_verify_core (FIPS204, K=4 L=4)",
              (void)0,
              ({
                  expand_mat(mat, rho, &fips_p);
                  polyvecl_ntt(&z, &fips_p);
                  for (unsigned _j = 0; _j < fips_p.PARAM_K; _j++)
                      polyvecl_pointwise_acc_invmontgomery(tmp1.vec + _j, mat + _j, &z, &fips_p);
                  chat = c_val;
                  dilithium_poly_ntt(&chat);
                  polyveck_ntt(&t1, &fips_p);
                  for (unsigned _j = 0; _j < fips_p.PARAM_K; _j++)
                      poly_pointwise_invmontgomery(tmp2.vec + _j, &chat, t1.vec + _j);
                  polyveck_sub(&tmp1, &tmp1, &tmp2, &fips_p);
                  polyveck_reduce(&tmp1, &fips_p);
                  polyveck_invntt_montgomery(&tmp1, &fips_p);
                  polyveck_csubq(&tmp1, &fips_p);
                  polyveck_use_hint_p(&w1, &tmp1, &h, &fips_p);
              }));

    printf("\n=== Done ===\n");
    return 0;
}
