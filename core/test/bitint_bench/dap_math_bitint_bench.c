/*
 * Unified benchmark: dap_math_ops.h vs C23 unsigned _BitInt(256/512).
 * No intrinsics / SSE / inline asm in this translation unit.
 * Standalone: defines extern constants expected by dap_math_ops.h here.
 *
 * Build: CMake -DBUILD_DAP_MATH_BITINT_BENCH=ON (dap-sdk/core → test/bitint_bench), or manually, e.g. from this dir:
 *   gcc -std=gnu23 -O3 -I../../include -I../../../3rdparty -I../../../3rdparty/uthash/src \
 *       dap_math_bitint_bench.c -o dap_math_bitint_bench
 */
#define _POSIX_C_SOURCE 199309L

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

#include "dap_math_ops.h"

/* ---- Symbols declared extern in dap_math_ops.h (standalone TU) ---- */
const uint128_t uint128_0 = 0;
const uint128_t uint128_1 = 1;
const uint128_t uint128_max = ((uint128_t)((int128_t)-1L));
const uint256_t uint256_0 = {};
const uint256_t uint256_1 = { .hi = 0, .lo = 1 };
const uint256_t uint256_max = { .hi = uint128_max, .lo = uint128_max };
const uint512_t uint512_0 = {};

#ifndef DAP_GLOBAL_IS_INT128
#error "This benchmark expects __int128 (GCC/Clang) so dap_math_ops.h matches _BitInt interop."
#endif

typedef unsigned _BitInt(256) b256_t;
typedef unsigned _BitInt(512) b512_t;

#define ITERS_MAIN    100000000
#define ITERS_PRIM    50000000
#define ITERS_MD      10000000
#define POOL_SIZE     1024
#define MASK          (POOL_SIZE - 1)

static _Atomic(unsigned long long) g_bench_sink;

static inline void bench_touch_u256(const uint256_t *p)
{
    unsigned long long x = (unsigned long long)(uint64_t)p->lo
        ^ (unsigned long long)(uint64_t)(p->lo >> 64)
        ^ (unsigned long long)(uint64_t)p->hi
        ^ (unsigned long long)(uint64_t)(p->hi >> 64);
    (void)atomic_fetch_xor_explicit(&g_bench_sink, x, memory_order_relaxed);
}

static inline void bench_touch_b256(const b256_t *p)
{
    b256_t v = *p;
    uint256_t u = { .lo = (uint128_t)v, .hi = (uint128_t)(v >> 128) };
    bench_touch_u256(&u);
}

static inline b256_t u256_to_b256(const uint256_t *m)
{
    return ((b256_t)m->hi << 128) | (b256_t)m->lo;
}

static inline uint256_t b256_to_u256(b256_t b)
{
    uint256_t r;
    r.lo = (uint128_t)b;
    r.hi = (uint128_t)(b >> 128);
    return r;
}

static inline b512_t u512_to_b512(const uint512_t *w)
{
    return ((b512_t)u256_to_b256(&w->hi) << 256) | (b512_t)u256_to_b256(&w->lo);
}

static inline bool equal512(const uint512_t *a, const uint512_t *b)
{
    return EQUAL_256(a->lo, b->lo) && EQUAL_256(a->hi, b->hi);
}

/* Unsigned add/sub with carry/borrow; avoids <stdckdint.h> for older libcs. */
static inline bool b256_add(b256_t a, b256_t b, b256_t *c)
{
    *c = a + b;
    return *c < a;
}

static inline bool b256_sub(b256_t a, b256_t b, b256_t *c)
{
    *c = a - b;
    return a < b;
}

static inline int b256_cmp(b256_t a, b256_t b)
{
    return (a > b) - (a < b);
}

static double elapsed_ms(struct timespec t0, struct timespec t1)
{
    return (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

static uint64_t xorshift64(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void report2(const char *dap_label, double dap_ms, const char *b_label, double b_ms)
{
    printf("  %-14s %8.1f ms\n  %-14s %8.1f ms", dap_label, dap_ms, b_label, b_ms);
    if (b_ms > 0.0)
        printf("  (dap/_BitInt: %.2fx)\n", dap_ms / b_ms);
    else
        printf("\n");
}

int main(void)
{
    uint256_t mp[POOL_SIZE];
    b256_t bp[POOL_SIZE];
    uint256_t ms[POOL_SIZE];
    b256_t bs[POOL_SIZE];
    uint256_t md[POOL_SIZE];
    b256_t bd[POOL_SIZE];
    uint256_t mb[POOL_SIZE];
    b256_t bmp[POOL_SIZE];
    uint256_t mw[POOL_SIZE];
    b256_t bmw[POOL_SIZE];

    uint64_t seed = 0xC0FFEE42DEADBEEFULL;
    for (int i = 0; i < POOL_SIZE; i++) {
        mp[i].lo = (uint128_t)xorshift64(&seed) | ((uint128_t)xorshift64(&seed) << 64);
        mp[i].hi = (uint128_t)xorshift64(&seed) | ((uint128_t)xorshift64(&seed) << 64);
        bp[i] = u256_to_b256(&mp[i]);

        ms[i] = GET_256_FROM_64(xorshift64(&seed) & 0xFFFFFFFFu);
        bs[i] = u256_to_b256(&ms[i]);

        uint64_t dv = (xorshift64(&seed) & 0xFFFFFFFFu) | 1u;
        md[i] = GET_256_FROM_64(dv);
        bd[i] = (b256_t)dv;

        mb[i].lo = (uint128_t)xorshift64(&seed) | ((uint128_t)xorshift64(&seed) << 64);
        mb[i].hi = 0;
        if (!mb[i].lo)
            mb[i].lo = 1;
        bmp[i] = u256_to_b256(&mb[i]);

        mw[i].lo = (uint128_t)xorshift64(&seed) | ((uint128_t)xorshift64(&seed) << 64);
        mw[i].hi = (uint128_t)xorshift64(&seed) | ((uint128_t)xorshift64(&seed) << 64);
        if (IS_ZERO_256(mw[i]))
            mw[i] = uint256_1;
        bmw[i] = u256_to_b256(&mw[i]);
    }

    struct timespec t0, t1;
    double ms_dap, ms_b;
    int fail = 0;

    printf("=== Correctness (dap_math_ops vs _BitInt) ===\n");
    {
        int ok = 1;
        for (int i = 0; i < POOL_SIZE; i++) {
            int j = (i + 1) & MASK;
            uint256_t dq, dr, qm, rm;

            DIV_256(mp[i], md[j], &dq);
            divmod_impl_256(mp[i], md[j], &qm, &rm);
            ok &= EQUAL_256(dq, qm);

            divmod_impl_256(mp[i], md[j], &dq, &dr);
            b256_t bq = bp[i] / bd[j];
            b256_t br = bp[i] % bd[j];
            ok &= u256_to_b256(&dq) == bq && u256_to_b256(&dr) == br;

            divmod_impl_256(mp[i], mb[j], &dq, &dr);
            bq = bp[i] / bmp[j];
            br = bp[i] % bmp[j];
            ok &= u256_to_b256(&dq) == bq && u256_to_b256(&dr) == br;

            divmod_impl_256(mp[i], mw[j], &dq, &dr);
            bq = bp[i] / bmw[j];
            br = bp[i] % bmw[j];
            ok &= u256_to_b256(&dq) == bq && u256_to_b256(&dr) == br;

            uint256_t plo;
            int ovd = MULT_256_256(ms[i], ms[j], &plo);
            b512_t p512 = (b512_t)bs[i] * (b512_t)bs[j];
            bool bit_ov = (p512 >> 256) != 0;
            ok &= (ovd != 0) == bit_ov;
            if (!ovd)
                ok &= EQUAL_256(plo, b256_to_u256((b256_t)p512));

            uint512_t wd, wf;
            MULT_256_512(mp[i], ms[j], &wd);
            b512_t wb = (b512_t)bp[i] * (b512_t)bs[j];
            ok &= u512_to_b512(&wd) == wb;

            dap_mult256_u64_to_512(mp[i], (uint64_t)ms[j].lo, &wf);
            MULT_256_512(mp[i], GET_256_FROM_64((uint64_t)ms[j].lo), &wd);
            ok &= equal512(&wd, &wf);
            ok &= u512_to_b512(&wf) == ((b512_t)bp[i] * (b512_t)(uint64_t)ms[j].lo);

            uint256_t c_dap;
            int oc_dap = _MULT_256_COIN(ms[i], ms[j], &c_dap, false);
            b512_t wcoin = (b512_t)bs[i] * (b512_t)bs[j];
            b512_t ten18 = (b512_t)1000000000000000000ULL;
            b512_t qcoin = wcoin / ten18;
            bool oc_bit = ((b512_t)(b256_t)qcoin) != qcoin;
            ok &= (oc_dap != 0) == oc_bit;
            if (!oc_bit) {
                b256_t expect = (b256_t)qcoin;
                ok &= u256_to_b256(&c_dap) == expect;
            }
        }
        {
            uint256_t t2 = GET_256_FROM_64(2), lo_d;
            int omd = MULT_256_256(uint256_max, t2, &lo_d);
            b256_t bmax = u256_to_b256(&uint256_max);
            b512_t p512 = (b512_t)bmax * (b512_t)2;
            bool obit = (p512 >> 256) != 0;
            ok &= (omd != 0) == obit;
            if (!omd)
                ok &= EQUAL_256(lo_d, b256_to_u256((b256_t)p512));
        }
        printf("  pool (div/mod/mul/wide/u64/coin): %s\n", ok ? "PASS" : "FAIL");
        fail |= !ok;

        int ok2 = 1;
        for (int i = 0; i < POOL_SIZE; i++) {
            int j = (i + 1) & MASK;
            uint256_t dc;
            DIV_256_COIN(mp[i], md[j], &dc);
            b512_t w = (b512_t)bp[i] * (b512_t)1000000000000000000ULL;
            b256_t qc = (b256_t)(w / (b512_t)bd[j]);
            ok2 &= u256_to_b256(&dc) == qc;
        }
        printf("  DIV_256_COIN (pool): %s\n", ok2 ? "PASS" : "FAIL");
        fail |= !ok2;

        {
            uint256_t r;
            DIV_256_COIN(mp[0], uint256_0, &r);
            int zok = IS_ZERO_256(r);
            DIV_256_COIN(uint256_0, md[0], &r);
            zok &= IS_ZERO_256(r);
            printf("  DIV_256_COIN zero cases: %s\n", zok ? "PASS" : "FAIL");
            fail |= !zok;
        }

        {
            int okc = 1;
            b512_t ten18b = (b512_t)1000000000000000000ULL;
            for (int i = 0; i < POOL_SIZE; i++) {
                int j = (i + 11) & MASK;
                b512_t w = (b512_t)bp[i] * (b512_t)bp[j];
                b512_t q = w / ten18b;
                bool ref_ov = ((b512_t)(b256_t)q) != q;
                uint256_t fc;
                int dap_ov = _MULT_256_COIN(mp[i], mp[j], &fc, false);
                okc &= (dap_ov != 0) == ref_ov;
                if (!ref_ov) {
                    b256_t expect = (b256_t)q;
                    okc &= u256_to_b256(&fc) == expect;
                }
            }
            printf("  MULT_256_COIN overflow vs _BitInt: %s\n", okc ? "PASS" : "FAIL");
            fail |= !okc;
        }

        {
            uint256_t dc, d_sub;
            b256_t bc;
            int spot_ok = 1;
            SUM_256_256(mp[0], mp[1], &dc);
            (void)b256_add(bp[0], bp[1], &bc);
            spot_ok &= u256_to_b256(&dc) == bc;

            SUBTRACT_256_256(mp[0], mp[1], &d_sub);
            (void)b256_sub(bp[0], bp[1], &bc);
            spot_ok &= u256_to_b256(&d_sub) == bc;
            spot_ok &= compare256(mp[0], mp[2]) == b256_cmp(bp[0], bp[2]);
            spot_ok &= EQUAL_256(mp[0], mp[1]) == (bp[0] == bp[1]);
            printf("  add/sub/cmp/eq spot: %s\n", spot_ok ? "PASS" : "FAIL");
            fail |= !spot_ok;
        }
    }

    printf("\n=== SUM_256_256 chain (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        uint256_t a = mp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            (void)SUM_256_256(a, mp[(i + 1) & MASK], &a);
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t ba = bp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            (void)b256_add(ba, bp[(i + 1) & MASK], &ba);
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap SUM_256_256", ms_dap, "_BitInt +", ms_b);
    }

    printf("\n=== SUBTRACT_256_256 chain (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        uint256_t a = mp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            (void)SUBTRACT_256_256(a, mp[(i + 1) & MASK], &a);
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t ba = bp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            (void)b256_sub(ba, bp[(i + 1) & MASK], &ba);
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap SUBTRACT", ms_dap, "_BitInt -", ms_b);
    }

    printf("\n=== compare256 (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        volatile int sink = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++)
            sink += compare256(mp[i & MASK], mp[(i + 1) & MASK]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++)
            sink += b256_cmp(bp[i & MASK], bp[(i + 1) & MASK]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        printf("  sink=%d\n", sink);
        report2("dap compare256", ms_dap, "_BitInt cmp", ms_b);
    }

    printf("\n=== IS_ZERO_256 (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        volatile int sink = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++)
            sink += IS_ZERO_256(mp[i & MASK]);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++)
            sink += (bp[i & MASK] == 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        printf("  sink=%d\n", sink);
        report2("dap IS_ZERO_256", ms_dap, "_BitInt ==0", ms_b);
    }

    printf("\n=== LEFT_SHIFT_256 (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        uint256_t a = mp[0], r;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            int sh = (int)((uint64_t)mp[(i + 1) & MASK].lo & 0xFF);
            if (sh > 255)
                sh = 255;
            LEFT_SHIFT_256(a, &r, sh);
            a = r;
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t ba = bp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            int sh = (int)((uint64_t)mp[(i + 1) & MASK].lo & 0xFF);
            if (sh > 255)
                sh = 255;
            ba <<= sh;
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap LEFT_SHIFT", ms_dap, "_BitInt <<", ms_b);
    }

    printf("\n=== RIGHT_SHIFT_256 (%dM) ===\n", ITERS_PRIM / 1000000);
    {
        uint256_t a = mp[0], r;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            int sh = (int)((uint64_t)mp[(i + 1) & MASK].lo & 0xFF);
            if (sh > 255)
                sh = 255;
            RIGHT_SHIFT_256(a, &r, sh);
            a = r;
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t ba = bp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_PRIM; i++) {
            int sh = (int)((uint64_t)mp[(i + 1) & MASK].lo & 0xFF);
            if (sh > 255)
                sh = 255;
            ba >>= sh;
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap RIGHT_SHIFT", ms_dap, "_BitInt >>", ms_b);
    }

    printf("\n=== Add chain (%dM) ===\n", ITERS_MAIN / 1000000);
    {
        uint256_t a = mp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            (void)SUM_256_256(a, mp[(i + 1) & MASK], &a);
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);
        b256_t ba = bp[0];
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            (void)b256_add(ba, bp[(i + 1) & MASK], &ba);
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== Ledger sim (%dM) ===\n", ITERS_MAIN / 1000000);
    {
        volatile int of_dap = 0, of_b = 0;
        uint256_t bal = { .hi = 0, .lo = 1000000000ULL };
        uint256_t sup = bal;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            uint256_t d = mp[i & MASK];
            d.hi = 0;
            d.lo &= 0xFFFFFFFFu;
            if (compare256(bal, d) >= 0) {
                (void)SUBTRACT_256_256(bal, d, &bal);
                (void)SUM_256_256(sup, d, &sup);
            } else {
                (void)SUM_256_256(bal, d, &bal);
                if (SUBTRACT_256_256(sup, d, &sup))
                    of_dap++;
            }
            bench_touch_u256(&bal);
            bench_touch_u256(&sup);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t bit_bal = (b256_t)1000000000ULL, bit_sup = bit_bal;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            b256_t d = bp[i & MASK] & 0xFFFFFFFFu;
            if (bit_bal >= d) {
                (void)b256_sub(bit_bal, d, &bit_bal);
                (void)b256_add(bit_sup, d, &bit_sup);
            } else {
                (void)b256_add(bit_bal, d, &bit_bal);
                if (b256_sub(bit_sup, d, &bit_sup))
                    of_b++;
            }
            bench_touch_b256(&bit_bal);
            bench_touch_b256(&bit_sup);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        printf("  underflow dap/_BitInt: %d %d\n", of_dap, of_b);
        report2("dap", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== UTXO scan (%dK outer) ===\n", ITERS_MAIN / POOL_SIZE / 1000);
    {
        uint256_t thr = { .hi = 0, .lo = (uint128_t)1 << 62 };
        b256_t bthr = (b256_t)1 << 62;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < ITERS_MAIN / POOL_SIZE; r++) {
            uint256_t s = uint256_0;
            for (int i = 0; i < POOL_SIZE; i++) {
                uint256_t v = mp[i];
                v.hi = 0;
                v.lo &= 0xFFFFFFFFFFFFu;
                (void)SUM_256_256(s, v, &s);
                if (compare256(s, thr) >= 0)
                    break;
            }
            bench_touch_u256(&s);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < ITERS_MAIN / POOL_SIZE; r++) {
            b256_t s = 0;
            for (int i = 0; i < POOL_SIZE; i++) {
                b256_t v = bp[i] & 0xFFFFFFFFFFFFu;
                (void)b256_add(s, v, &s);
                if (b256_cmp(s, bthr) >= 0)
                    break;
            }
            bench_touch_b256(&s);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== Conservation eq (%dM) ===\n", ITERS_MAIN / 4 / 1000000);
    {
        volatile uint64_t eq_d = 0, eq_b = 0;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < ITERS_MAIN / 4; r++) {
            int i = r & MASK;
            uint256_t si, so;
            SUM_256_256(mp[i], mp[(i + 1) & MASK], &si);
            SUM_256_256(mp[(i + 2) & MASK], mp[(i + 3) & MASK], &so);
            if (EQUAL_256(si, so))
                eq_d++;
            bench_touch_u256(&si);
            bench_touch_u256(&so);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int r = 0; r < ITERS_MAIN / 4; r++) {
            int i = r & MASK;
            b256_t si, so;
            (void)b256_add(bp[i], bp[(i + 1) & MASK], &si);
            (void)b256_add(bp[(i + 2) & MASK], bp[(i + 3) & MASK], &so);
            if (si == so)
                eq_b++;
            bench_touch_b256(&si);
            bench_touch_b256(&so);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        printf("  eq_matches dap/_BitInt: %llu %llu %s\n",
            (unsigned long long)eq_d, (unsigned long long)eq_b,
            (eq_d == eq_b) ? "OK" : "MISMATCH");
        report2("dap", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== Mul via repeated add (%dM) ===\n", ITERS_MAIN / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            uint256_t v = mp[i & MASK];
            v.hi = 0;
            uint256_t a = uint256_0;
            int k = (int)(v.lo & 7) + 2;
            for (int j = 0; j < k; j++)
                (void)SUM_256_256(a, v, &a);
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MAIN; i++) {
            b256_t v = bp[i & MASK] & ((((b256_t)1 << 128) - 1));
            b256_t a = 0;
            int k = (int)(v & 7) + 2;
            for (int j = 0; j < k; j++)
                (void)b256_add(a, v, &a);
            bench_touch_b256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== Mul chain (%dM) ===\n", ITERS_MD / 1000000);
    {
        uint256_t a = uint256_1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            (void)MULT_256_256(a, ms[i & MASK], &a);
            bench_touch_u256(&a);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b256_t ba = 1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            ba *= bs[i & MASK];
            bench_touch_b256(&ba);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap MULT_256_256", ms_dap, "_BitInt *", ms_b);
    }

    printf("\n=== Div 256 / 32-bit limb (%dM) ===\n", ITERS_MD / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            uint256_t dc;
            DIV_256(mp[i & MASK], md[(i + 1) & MASK], &dc);
            bench_touch_u256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            b256_t dc = bp[i & MASK] / bd[(i + 1) & MASK];
            bench_touch_b256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap DIV_256", ms_dap, "_BitInt /", ms_b);
    }

    printf("\n=== Div 256 / low-128 divisor (%dM) ===\n", ITERS_MD / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            uint256_t dc;
            DIV_256(mp[i & MASK], mb[(i + 1) & MASK], &dc);
            bench_touch_u256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            b256_t dc = bp[i & MASK] / bmp[(i + 1) & MASK];
            bench_touch_b256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap DIV_256", ms_dap, "_BitInt /", ms_b);
    }

    printf("\n=== Div 256 / full divisor (%dM) ===\n", ITERS_MD / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            uint256_t dc;
            DIV_256(mp[i & MASK], mw[(i + 1) & MASK], &dc);
            bench_touch_u256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            b256_t dc = bp[i & MASK] / bmw[(i + 1) & MASK];
            bench_touch_b256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap DIV_256", ms_dap, "_BitInt /", ms_b);
    }

    printf("\n=== MULT_256_COIN (%dM) ===\n", ITERS_MD / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            uint256_t dc;
            (void)_MULT_256_COIN(mp[i & MASK], ms[(i + 1) & MASK], &dc, false);
            bench_touch_u256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b512_t ten18 = (b512_t)1000000000000000000ULL;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            b512_t w = (b512_t)bp[i & MASK] * (b512_t)bs[(i + 1) & MASK];
            b256_t dc = (b256_t)(w / ten18);
            bench_touch_b256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap _MULT_256_COIN", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== DIV_256_COIN (%dM) ===\n", ITERS_MD / 1000000);
    {
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            uint256_t dc;
            DIV_256_COIN(mp[i & MASK], md[(i + 1) & MASK], &dc);
            bench_touch_u256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_dap = elapsed_ms(t0, t1);

        b512_t ten18b = (b512_t)1000000000000000000ULL;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < ITERS_MD; i++) {
            b512_t w = (b512_t)bp[i & MASK] * ten18b;
            b256_t dc = (b256_t)(w / (b512_t)bd[(i + 1) & MASK]);
            bench_touch_b256(&dc);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms_b = elapsed_ms(t0, t1);
        report2("dap DIV_256_COIN", ms_dap, "_BitInt", ms_b);
    }

    printf("\n=== Done. %s ===\n", fail ? "FAIL" : "PASS");
    return fail ? 1 : 0;
}
