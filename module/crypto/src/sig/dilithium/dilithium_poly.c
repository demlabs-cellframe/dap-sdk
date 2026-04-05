#include <stdint.h>
#include "dilithium_poly.h"
#include "dilithium_rounding_reduce.h"
#include "dap_ntt.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake128.h"
#include "dap_hash_shake256.h"
#include "dap_hash_shake_x4.h"
#include "dap_cpu_arch.h"
#include "dap_cpu_detect.h"
#include "dap_arch_dispatch.h"

extern const dap_ntt_params_t g_dilithium_ntt_params;

/* ===== External SIMD implementations ===== */

#if DAP_PLATFORM_X86
extern void dap_dilithium_ntt_forward_avx2(int32_t coeffs[256]);
extern void dap_dilithium_ntt_inverse_avx2(int32_t coeffs[256]);
extern void dap_dilithium_pointwise_mont_avx2(int32_t *c, const int32_t *a, const int32_t *b);
extern void dap_dilithium_ntt_forward_avx2_512vl(int32_t coeffs[256]);
extern void dap_dilithium_ntt_inverse_avx2_512vl(int32_t coeffs[256]);
extern void dap_dilithium_pointwise_mont_avx2_512vl(int32_t *c, const int32_t *a, const int32_t *b);
extern void dap_dilithium_ntt_forward_avx512(int32_t coeffs[256]);
extern void dap_dilithium_ntt_inverse_avx512(int32_t coeffs[256]);
extern void dap_dilithium_pointwise_mont_avx512(int32_t *c, const int32_t *a, const int32_t *b);

extern void dap_dilithium_ntt_forward_avx2_asm(int32_t coeffs[256]);
extern void dap_dilithium_ntt_inverse_avx2_asm(int32_t coeffs[256]);
extern void dap_dilithium_pointwise_mont_avx2_asm(int32_t *c, const int32_t *a, const int32_t *b);

extern void dap_dilithium_ntt_fwd_fused_avx2(int32_t coeffs[256]);
extern void dap_dilithium_invntt_fused_avx2(int32_t coeffs[256]);
extern void dap_dilithium_nttunpack_avx2(int32_t coeffs[256]);

extern void dap_dilithium_poly_reduce_avx2(int32_t[256]);
extern void dap_dilithium_poly_reduce_avx512(int32_t[256]);
extern void dap_dilithium_poly_csubq_avx2(int32_t[256]);
extern void dap_dilithium_poly_csubq_avx512(int32_t[256]);
extern void dap_dilithium_poly_freeze_avx2(int32_t[256]);
extern void dap_dilithium_poly_freeze_avx512(int32_t[256]);
extern void dap_dilithium_poly_add_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_add_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_sub_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_sub_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_neg_avx2(int32_t[256]);
extern void dap_dilithium_poly_neg_avx512(int32_t[256]);
extern void dap_dilithium_poly_shiftl_avx2(int32_t[256], unsigned);
extern void dap_dilithium_poly_shiftl_avx512(int32_t[256], unsigned);
extern void dap_dilithium_poly_decompose_avx2(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_decompose_avx512(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_power2round_avx2(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_power2round_avx512(int32_t *, int32_t *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_avx2(int32_t *, const int32_t *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_use_hint_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_use_hint_avx512(int32_t *, const int32_t *, const int32_t *);
extern int dap_dilithium_poly_chknorm_avx2(const int32_t *, int32_t);
extern int dap_dilithium_poly_chknorm_avx512(const int32_t *, int32_t);
extern void dap_dilithium_rej_uniform_avx2(uint32_t[256], const uint8_t *);

extern void dap_dilithium_poly_use_hint_g32_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_use_hint_g88_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_decompose_g32_avx2(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_decompose_g88_avx2(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_polyw1_pack_g88_avx2(unsigned char *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_g32_avx2(int32_t *, const int32_t *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_g88_avx2(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_polyz_unpack_g17_avx2(int32_t *, const uint8_t *);
extern void dap_dilithium_polyz_unpack_g19_avx2(int32_t *, const uint8_t *);

extern void dap_dilithium_poly_use_hint_g32_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_use_hint_g88_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_decompose_g32_avx512(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_decompose_g88_avx512(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_polyw1_pack_g88_avx512(unsigned char *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_g32_avx512(int32_t *, const int32_t *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_g88_avx512(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_polyz_unpack_g17_avx512(int32_t *, const uint8_t *);
extern void dap_dilithium_polyz_unpack_g19_avx512(int32_t *, const uint8_t *);
#endif

#if DAP_PLATFORM_ARM
extern void dap_dilithium_ntt_forward_neon(int32_t coeffs[256]);
extern void dap_dilithium_ntt_inverse_neon(int32_t coeffs[256]);
extern void dap_dilithium_pointwise_mont_neon(int32_t *c, const int32_t *a, const int32_t *b);

extern void dap_dilithium_poly_reduce_neon(int32_t[256]);
extern void dap_dilithium_poly_csubq_neon(int32_t[256]);
extern void dap_dilithium_poly_freeze_neon(int32_t[256]);
extern void dap_dilithium_poly_add_neon(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_sub_neon(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_neg_neon(int32_t[256]);
extern void dap_dilithium_poly_shiftl_neon(int32_t[256], unsigned);
extern void dap_dilithium_poly_decompose_neon(int32_t *, int32_t *, const int32_t *);
extern void dap_dilithium_poly_power2round_neon(int32_t *, int32_t *, const int32_t *);
extern unsigned dap_dilithium_poly_make_hint_neon(int32_t *, const int32_t *, const int32_t *);
extern void dap_dilithium_poly_use_hint_neon(int32_t *, const int32_t *, const int32_t *);
extern int dap_dilithium_poly_chknorm_neon(const int32_t *, int32_t);
extern void dap_dilithium_rej_uniform_neon(uint32_t[256], const uint8_t *);
#endif

/* ===== Dispatch pointer declarations ===== */

DAP_DISPATCH_LOCAL(s_dil_ntt_fwd,      void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_ntt_inv,      void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_nttunpack,    void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_pw_mont,      void, int32_t *, const int32_t *, const int32_t *);

DAP_DISPATCH_LOCAL(s_dil_reduce,       void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_csubq,        void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_freeze,       void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_add,          void, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_sub,          void, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_neg,          void, int32_t *);
DAP_DISPATCH_LOCAL(s_dil_shiftl,       void, int32_t *, unsigned);
DAP_DISPATCH_LOCAL(s_dil_decompose,    void, int32_t *, int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_p2r,          void, int32_t *, int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_make_hint,    unsigned, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_use_hint,     void, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_chknorm,      int, const int32_t *, int32_t);
DAP_DISPATCH_LOCAL(s_dil_rej_uniform,  void, uint32_t *, const uint8_t *);

DAP_DISPATCH_LOCAL(s_dil_use_hint_g32,   void, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_use_hint_g88,   void, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_decompose_g32,  void, int32_t *, int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_decompose_g88,  void, int32_t *, int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_w1pack_g88,     void, unsigned char *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_make_hint_g32,  unsigned, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_make_hint_g88,  unsigned, int32_t *, const int32_t *, const int32_t *);
DAP_DISPATCH_LOCAL(s_dil_zunpack_g17,    void, int32_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_dil_zunpack_g19,    void, int32_t *, const uint8_t *);
DAP_DISPATCH_LOCAL(s_dil_w1pack_g32,     void, unsigned char *, const int32_t *);

/* ===== Scalar reference implementations ===== */

static void s_dil_ntt_fwd_ref(int32_t *pp)
{
    dap_ntt_forward_mont(pp, &g_dilithium_ntt_params);
}

static void s_dil_ntt_inv_ref(int32_t *pp)
{
    dap_ntt_inverse_mont(pp, &g_dilithium_ntt_params);
    const int32_t f = (int32_t)((((uint64_t)MONT * MONT % Q) * (Q - 1) % Q) * ((Q - 1) >> 8) % Q);
    for (unsigned j = 0; j < NN; j++)
        pp[j] = (int32_t)dap_ntt_montgomery_reduce((int64_t)f * pp[j], &g_dilithium_ntt_params);
}

static void s_dil_pw_mont_ref(int32_t *c, const int32_t *a, const int32_t *b)
{
    dap_ntt_pointwise_montgomery(c, a, b, &g_dilithium_ntt_params);
}

static void s_dil_reduce_ref(int32_t *c)
{
    for (unsigned i = 0; i < NN; ++i)
        c[i] = (int32_t)reduce32((uint32_t)c[i]);
}

static void s_dil_csubq_ref(int32_t *c)
{
    for (unsigned i = 0; i < NN; ++i)
        c[i] = (int32_t)csubq((uint32_t)c[i]);
}

static void s_dil_freeze_ref(int32_t *c)
{
    for (unsigned i = 0; i < NN; ++i)
        c[i] = (int32_t)freeze((uint32_t)c[i]);
}

static void s_dil_add_ref(int32_t *r, const int32_t *a, const int32_t *b)
{
    for (unsigned i = 0; i < NN; ++i) r[i] = a[i] + b[i];
}

static void s_dil_sub_ref(int32_t *r, const int32_t *a, const int32_t *b)
{
    for (unsigned i = 0; i < NN; ++i) r[i] = a[i] + 2 * (int32_t)Q - b[i];
}

static void s_dil_neg_ref(int32_t *c)
{
    for (unsigned i = 0; i < NN; ++i) c[i] = (int32_t)Q - c[i];
}

static void s_dil_shiftl_ref(int32_t *c, unsigned k)
{
    for (unsigned i = 0; i < NN; ++i) c[i] <<= k;
}

static void s_dil_decompose_ref(int32_t *a1, int32_t *a0, const int32_t *a)
{
    for (unsigned i = 0; i < NN; ++i)
        a1[i] = (int32_t)decompose((uint32_t)a[i], (uint32_t *)&a0[i]);
}

static void s_dil_p2r_ref(int32_t *a1, int32_t *a0, const int32_t *a)
{
    for (unsigned i = 0; i < NN; ++i)
        a1[i] = (int32_t)power2round((uint32_t)a[i], (uint32_t *)&a0[i]);
}

static unsigned s_dil_make_hint_ref(int32_t *h, const int32_t *a, const int32_t *b)
{
    unsigned s = 0;
    for (unsigned i = 0; i < NN; ++i) {
        h[i] = (int32_t)make_hint((uint32_t)a[i], (uint32_t)b[i]);
        s += (unsigned)h[i];
    }
    return s;
}

static void s_dil_use_hint_ref(int32_t *r, const int32_t *b, const int32_t *h)
{
    for (unsigned i = 0; i < NN; ++i)
        r[i] = (int32_t)use_hint((uint32_t)b[i], (unsigned)h[i]);
}

static int s_dil_chknorm_ref(const int32_t *c, int32_t B)
{
    for (unsigned i = 0; i < NN; ++i) {
        int32_t t = (int32_t)(Q - 1) / 2 - c[i];
        t ^= (t >> 31);
        t = (int32_t)(Q - 1) / 2 - t;
        if ((uint32_t)t >= (uint32_t)B) return 1;
    }
    return 0;
}

static void s_dil_rej_uniform_ref(uint32_t *a, const uint8_t *buf)
{
    unsigned ctr = 0, pos = 0;
    while (ctr < NN) {
        uint32_t t = (uint32_t)buf[pos] | ((uint32_t)buf[pos+1] << 8)
                     | ((uint32_t)buf[pos+2] << 16);
        t &= 0x7FFFFF;
        pos += 3;
        if (t < Q) a[ctr++] = t;
    }
}

static void s_dil_use_hint_g32_ref(int32_t *r, const int32_t *b, const int32_t *h)
{
    for (unsigned i = 0; i < NN; ++i) {
        uint32_t val = (uint32_t)b[i];
        int32_t a1 = ((int32_t)val + 127) >> 7;
        a1 = (a1 * 1025 + (1 << 21)) >> 22;
        a1 &= 15;
        int32_t r0 = (int32_t)val - a1 * (int32_t)(2 * 261888);
        r0 -= (((int32_t)(Q - 1) / 2 - r0) >> 31) & (int32_t)Q;
        if (h[i] == 0) r[i] = a1;
        else if (r0 > 0) r[i] = (a1 + 1) & 15;
        else r[i] = (a1 - 1) & 15;
    }
}

static void s_dil_use_hint_g88_ref(int32_t *r, const int32_t *b, const int32_t *h)
{
    for (unsigned i = 0; i < NN; ++i) {
        uint32_t val = (uint32_t)b[i];
        int32_t a1 = ((int32_t)val + 127) >> 7;
        a1 = (a1 * 11275 + (1 << 23)) >> 24;
        a1 ^= ((43 - a1) >> 31) & a1;
        int32_t r0 = (int32_t)val - a1 * (int32_t)(2 * 95232);
        r0 -= (((int32_t)(Q - 1) / 2 - r0) >> 31) & (int32_t)Q;
        if (h[i] == 0) r[i] = a1;
        else if (r0 > 0) r[i] = (a1 == 43) ? 0 : a1 + 1;
        else r[i] = (a1 == 0) ? 43 : a1 - 1;
    }
}

static void s_dil_decompose_g32_ref(int32_t *a1, int32_t *a0, const int32_t *a)
{
    for (unsigned i = 0; i < NN; ++i) {
        int32_t v = ((int32_t)a[i] + 127) >> 7;
        v = (v * 1025 + (1 << 21)) >> 22;
        v &= 15;
        a1[i] = v;
        int32_t r0 = (int32_t)a[i] - v * (int32_t)(2 * 261888);
        r0 -= (((int32_t)(Q - 1) / 2 - r0) >> 31) & (int32_t)Q;
        a0[i] = (int32_t)Q + r0;
    }
}

static void s_dil_decompose_g88_ref(int32_t *a1, int32_t *a0, const int32_t *a)
{
    for (unsigned i = 0; i < NN; ++i) {
        int32_t v = ((int32_t)a[i] + 127) >> 7;
        v = (v * 11275 + (1 << 23)) >> 24;
        v ^= ((43 - v) >> 31) & v;
        a1[i] = v;
        int32_t r0 = (int32_t)a[i] - v * (int32_t)(2 * 95232);
        r0 -= (((int32_t)(Q - 1) / 2 - r0) >> 31) & (int32_t)Q;
        a0[i] = (int32_t)Q + r0;
    }
}

static unsigned s_dil_make_hint_g32_ref(int32_t *h, const int32_t *a, const int32_t *b)
{
    unsigned s = 0;
    for (unsigned i = 0; i < NN; ++i) {
        int32_t va = ((int32_t)a[i] + 127) >> 7;
        va = (va * 1025 + (1 << 21)) >> 22;
        va &= 15;
        int32_t vb = ((int32_t)b[i] + 127) >> 7;
        vb = (vb * 1025 + (1 << 21)) >> 22;
        vb &= 15;
        h[i] = (va != vb) ? 1 : 0;
        s += (unsigned)h[i];
    }
    return s;
}

static unsigned s_dil_make_hint_g88_ref(int32_t *h, const int32_t *a, const int32_t *b)
{
    unsigned s = 0;
    for (unsigned i = 0; i < NN; ++i) {
        int32_t va = ((int32_t)a[i] + 127) >> 7;
        va = (va * 11275 + (1 << 23)) >> 24;
        va ^= ((43 - va) >> 31) & va;
        int32_t vb = ((int32_t)b[i] + 127) >> 7;
        vb = (vb * 11275 + (1 << 23)) >> 24;
        vb ^= ((43 - vb) >> 31) & vb;
        h[i] = (va != vb) ? 1 : 0;
        s += (unsigned)h[i];
    }
    return s;
}

static void s_dil_w1pack_g88_ref(unsigned char *r, const int32_t *coeffs)
{
    for (unsigned i = 0; i < NN / 4; ++i) {
        uint32_t c0 = (uint32_t)coeffs[4*i+0];
        uint32_t c1 = (uint32_t)coeffs[4*i+1];
        uint32_t c2 = (uint32_t)coeffs[4*i+2];
        uint32_t c3 = (uint32_t)coeffs[4*i+3];
        r[3*i+0] = (uint8_t)(c0 | (c1 << 6));
        r[3*i+1] = (uint8_t)((c1 >> 2) | (c2 << 4));
        r[3*i+2] = (uint8_t)((c2 >> 4) | (c3 << 2));
    }
}

static void s_dil_w1pack_g32_ref(unsigned char *r, const int32_t *coeffs)
{
    for (unsigned i = 0; i < NN / 2; ++i)
        r[i] = (uint8_t)((uint32_t)coeffs[2*i+0] | ((uint32_t)coeffs[2*i+1] << 4));
}

static void s_dil_zunpack_g17_ref(int32_t *r, const uint8_t *a)
{
    for (unsigned i = 0; i < NN / 4; ++i) {
        uint32_t c0  = a[9*i+0];
        c0 |= (uint32_t)a[9*i+1] << 8;
        c0 |= (uint32_t)(a[9*i+2] & 0x03) << 16;
        uint32_t c1  = a[9*i+2] >> 2;
        c1 |= (uint32_t)a[9*i+3] << 6;
        c1 |= (uint32_t)(a[9*i+4] & 0x0F) << 14;
        uint32_t c2  = a[9*i+4] >> 4;
        c2 |= (uint32_t)a[9*i+5] << 4;
        c2 |= (uint32_t)(a[9*i+6] & 0x3F) << 12;
        uint32_t c3  = a[9*i+6] >> 6;
        c3 |= (uint32_t)a[9*i+7] << 2;
        c3 |= (uint32_t)a[9*i+8] << 10;
        r[4*i+0] = (int32_t)(0x1FFFF - c0) + (((int32_t)(0x1FFFF - c0) >> 31) & Q);
        r[4*i+1] = (int32_t)(0x1FFFF - c1) + (((int32_t)(0x1FFFF - c1) >> 31) & Q);
        r[4*i+2] = (int32_t)(0x1FFFF - c2) + (((int32_t)(0x1FFFF - c2) >> 31) & Q);
        r[4*i+3] = (int32_t)(0x1FFFF - c3) + (((int32_t)(0x1FFFF - c3) >> 31) & Q);
    }
}

static void s_dil_zunpack_g19_ref(int32_t *r, const uint8_t *a)
{
    for (unsigned i = 0; i < NN / 2; ++i) {
        uint32_t c0  = a[5*i+0];
        c0 |= (uint32_t)a[5*i+1] << 8;
        c0 |= (uint32_t)(a[5*i+2] & 0x0F) << 16;
        uint32_t c1  = a[5*i+2] >> 4;
        c1 |= (uint32_t)a[5*i+3] << 4;
        c1 |= (uint32_t)a[5*i+4] << 12;
        r[2*i+0] = (int32_t)(0x7FFFF - c0) + (((int32_t)(0x7FFFF - c0) >> 31) & Q);
        r[2*i+1] = (int32_t)(0x7FFFF - c1) + (((int32_t)(0x7FFFF - c1) >> 31) & Q);
    }
}

/* ===== Unified dispatch init ===== */

static void s_dil_dispatch_init(void)
{
    dap_algo_class_t l_dil = dap_algo_class_register("DIL_POLY");

    DAP_DISPATCH_DEFAULT(s_dil_ntt_fwd,      s_dil_ntt_fwd_ref);
    DAP_DISPATCH_DEFAULT(s_dil_ntt_inv,      s_dil_ntt_inv_ref);
    DAP_DISPATCH_DEFAULT(s_dil_nttunpack,    NULL);
    DAP_DISPATCH_DEFAULT(s_dil_pw_mont,      s_dil_pw_mont_ref);
    DAP_DISPATCH_DEFAULT(s_dil_reduce,       s_dil_reduce_ref);
    DAP_DISPATCH_DEFAULT(s_dil_csubq,        s_dil_csubq_ref);
    DAP_DISPATCH_DEFAULT(s_dil_freeze,       s_dil_freeze_ref);
    DAP_DISPATCH_DEFAULT(s_dil_add,          s_dil_add_ref);
    DAP_DISPATCH_DEFAULT(s_dil_sub,          s_dil_sub_ref);
    DAP_DISPATCH_DEFAULT(s_dil_neg,          s_dil_neg_ref);
    DAP_DISPATCH_DEFAULT(s_dil_shiftl,       s_dil_shiftl_ref);
    DAP_DISPATCH_DEFAULT(s_dil_decompose,    s_dil_decompose_ref);
    DAP_DISPATCH_DEFAULT(s_dil_p2r,          s_dil_p2r_ref);
    DAP_DISPATCH_DEFAULT(s_dil_make_hint,    s_dil_make_hint_ref);
    DAP_DISPATCH_DEFAULT(s_dil_use_hint,     s_dil_use_hint_ref);
    DAP_DISPATCH_DEFAULT(s_dil_chknorm,      s_dil_chknorm_ref);
    DAP_DISPATCH_DEFAULT(s_dil_rej_uniform,  s_dil_rej_uniform_ref);
    DAP_DISPATCH_DEFAULT(s_dil_use_hint_g32,  s_dil_use_hint_g32_ref);
    DAP_DISPATCH_DEFAULT(s_dil_use_hint_g88,  s_dil_use_hint_g88_ref);
    DAP_DISPATCH_DEFAULT(s_dil_decompose_g32, s_dil_decompose_g32_ref);
    DAP_DISPATCH_DEFAULT(s_dil_decompose_g88, s_dil_decompose_g88_ref);
    DAP_DISPATCH_DEFAULT(s_dil_w1pack_g88,    s_dil_w1pack_g88_ref);
    DAP_DISPATCH_DEFAULT(s_dil_make_hint_g32, s_dil_make_hint_g32_ref);
    DAP_DISPATCH_DEFAULT(s_dil_make_hint_g88, s_dil_make_hint_g88_ref);
    DAP_DISPATCH_DEFAULT(s_dil_zunpack_g17,  s_dil_zunpack_g17_ref);
    DAP_DISPATCH_DEFAULT(s_dil_zunpack_g19,  s_dil_zunpack_g19_ref);
    DAP_DISPATCH_DEFAULT(s_dil_w1pack_g32,   s_dil_w1pack_g32_ref);

    DAP_DISPATCH_ARCH_SELECT_FOR(l_dil);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_ntt_fwd,      dap_dilithium_ntt_forward_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_ntt_inv,      dap_dilithium_ntt_inverse_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_pw_mont,      dap_dilithium_pointwise_mont_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_ntt_fwd,      dap_dilithium_ntt_forward_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_ntt_inv,      dap_dilithium_ntt_inverse_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_pw_mont,      dap_dilithium_pointwise_mont_avx512);

    /* Hand-tuned ASM with pre-computed zeta*QINV. Inverse NTT stays with the
       per-block approach; pointwise is NOT overridden. */
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_ntt_inv,      dap_dilithium_ntt_inverse_avx2_asm);

    /* CRYSTALS-style register-resident fused forward NTT (~420 cyc target).
       Temporarily disabled for regression testing. */
    //DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_ntt_fwd,      dap_dilithium_ntt_fwd_fused_avx2);
    //DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_ntt_inv,      dap_dilithium_invntt_fused_avx2);
    //DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_nttunpack,    dap_dilithium_nttunpack_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_reduce,       dap_dilithium_poly_reduce_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_reduce,       dap_dilithium_poly_reduce_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_csubq,        dap_dilithium_poly_csubq_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_csubq,        dap_dilithium_poly_csubq_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_freeze,       dap_dilithium_poly_freeze_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_freeze,       dap_dilithium_poly_freeze_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_add,          dap_dilithium_poly_add_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_add,          dap_dilithium_poly_add_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_sub,          dap_dilithium_poly_sub_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_sub,          dap_dilithium_poly_sub_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_neg,          dap_dilithium_poly_neg_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_neg,          dap_dilithium_poly_neg_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_shiftl,       dap_dilithium_poly_shiftl_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_shiftl,       dap_dilithium_poly_shiftl_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_decompose,    dap_dilithium_poly_decompose_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_decompose,    dap_dilithium_poly_decompose_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_p2r,          dap_dilithium_poly_power2round_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_p2r,          dap_dilithium_poly_power2round_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_make_hint,    dap_dilithium_poly_make_hint_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_make_hint,    dap_dilithium_poly_make_hint_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_use_hint,     dap_dilithium_poly_use_hint_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_use_hint,     dap_dilithium_poly_use_hint_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_chknorm,      dap_dilithium_poly_chknorm_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_chknorm,      dap_dilithium_poly_chknorm_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_rej_uniform,  dap_dilithium_rej_uniform_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_use_hint_g32,  dap_dilithium_poly_use_hint_g32_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_use_hint_g88,  dap_dilithium_poly_use_hint_g88_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_decompose_g32, dap_dilithium_poly_decompose_g32_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_decompose_g88, dap_dilithium_poly_decompose_g88_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_w1pack_g88,    dap_dilithium_polyw1_pack_g88_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_make_hint_g32, dap_dilithium_poly_make_hint_g32_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_make_hint_g88, dap_dilithium_poly_make_hint_g88_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_zunpack_g17,   dap_dilithium_polyz_unpack_g17_avx2);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2,   s_dil_zunpack_g19,   dap_dilithium_polyz_unpack_g19_avx2);

    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_use_hint_g32,  dap_dilithium_poly_use_hint_g32_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_use_hint_g88,  dap_dilithium_poly_use_hint_g88_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_decompose_g32, dap_dilithium_poly_decompose_g32_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_decompose_g88, dap_dilithium_poly_decompose_g88_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_w1pack_g88,    dap_dilithium_polyw1_pack_g88_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_make_hint_g32, dap_dilithium_poly_make_hint_g32_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_make_hint_g88, dap_dilithium_poly_make_hint_g88_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_zunpack_g17,   dap_dilithium_polyz_unpack_g17_avx512);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX512, s_dil_zunpack_g19,   dap_dilithium_polyz_unpack_g19_avx512);

    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_ntt_fwd,      dap_dilithium_ntt_forward_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_ntt_inv,      dap_dilithium_ntt_inverse_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_pw_mont,      dap_dilithium_pointwise_mont_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_reduce,       dap_dilithium_poly_reduce_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_csubq,        dap_dilithium_poly_csubq_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_freeze,       dap_dilithium_poly_freeze_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_add,          dap_dilithium_poly_add_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_sub,          dap_dilithium_poly_sub_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_neg,          dap_dilithium_poly_neg_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_shiftl,       dap_dilithium_poly_shiftl_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_decompose,    dap_dilithium_poly_decompose_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_p2r,          dap_dilithium_poly_power2round_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_make_hint,    dap_dilithium_poly_make_hint_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_use_hint,     dap_dilithium_poly_use_hint_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_chknorm,      dap_dilithium_poly_chknorm_neon);
    DAP_DISPATCH_ARM(DAP_CPU_ARCH_NEON,   s_dil_rej_uniform,  dap_dilithium_rej_uniform_neon);
}

/* ===== Public API — thin dispatch wrappers ===== */

#define C(p)  ((int32_t *)(p)->coeffs)
#define CC(p) ((const int32_t *)(p)->coeffs)

void poly_reduce(poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_reduce, s_dil_dispatch_init);
    s_dil_reduce_ptr(C(a));
}
void poly_csubq(poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_csubq, s_dil_dispatch_init);
    s_dil_csubq_ptr(C(a));
}
void poly_freeze(poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_freeze, s_dil_dispatch_init);
    s_dil_freeze_ptr(C(a));
}
void dilithium_poly_add(poly *c, const poly *a, const poly *b) {
    DAP_DISPATCH_ENSURE(s_dil_add, s_dil_dispatch_init);
    s_dil_add_ptr(C(c), CC(a), CC(b));
}
void dilithium_poly_sub(poly *c, const poly *a, const poly *b) {
    DAP_DISPATCH_ENSURE(s_dil_sub, s_dil_dispatch_init);
    s_dil_sub_ptr(C(c), CC(a), CC(b));
}
void poly_neg(poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_neg, s_dil_dispatch_init);
    s_dil_neg_ptr(C(a));
}
void poly_shiftl(poly *a, unsigned int k) {
    DAP_DISPATCH_ENSURE(s_dil_shiftl, s_dil_dispatch_init);
    s_dil_shiftl_ptr(C(a), k);
}
void poly_decompose(poly *a1, poly *a0, const poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_decompose, s_dil_dispatch_init);
    s_dil_decompose_ptr(C(a1), C(a0), CC(a));
}
void poly_power2round(poly *a1, poly *a0, const poly *a) {
    DAP_DISPATCH_ENSURE(s_dil_p2r, s_dil_dispatch_init);
    s_dil_p2r_ptr(C(a1), C(a0), CC(a));
}
unsigned int poly_make_hint(poly *h, const poly *a, const poly *b) {
    DAP_DISPATCH_ENSURE(s_dil_make_hint, s_dil_dispatch_init);
    return s_dil_make_hint_ptr(C(h), CC(a), CC(b));
}
void poly_use_hint(poly *a, const poly *b, const poly *h) {
    DAP_DISPATCH_ENSURE(s_dil_use_hint, s_dil_dispatch_init);
    s_dil_use_hint_ptr(C(a), CC(b), CC(h));
}
int poly_chknorm(const poly *a, uint32_t B) {
    DAP_DISPATCH_ENSURE(s_dil_chknorm, s_dil_dispatch_init);
    return s_dil_chknorm_ptr(CC(a), (int32_t)B);
}

#undef C
#undef CC

/*************************************************/
void dilithium_poly_ntt(poly *a) {
  dilithium_ntt(a->coeffs);
}

/*************************************************/
void poly_invntt_montgomery(poly *a) {
  invntt_frominvmont(a->coeffs);
}

/*************************************************/
void poly_pointwise_invmontgomery(poly *c, const poly *a, const poly *b) {
    DAP_DISPATCH_ENSURE(s_dil_pw_mont, s_dil_dispatch_init);
    s_dil_pw_mont_ptr((int32_t *)c->coeffs, (const int32_t *)a->coeffs,
                      (const int32_t *)b->coeffs);
}

/*************************************************/
void dilithium_poly_uniform(poly *a, const unsigned char *buf) {
    DAP_DISPATCH_ENSURE(s_dil_rej_uniform, s_dil_dispatch_init);
    s_dil_rej_uniform_ptr(a->coeffs, (const uint8_t *)buf);
}

/*************************************************/
static unsigned int rej_eta(uint32_t *a, unsigned int len, const unsigned char *buf,
                            unsigned int buflen, dilithium_param_t *p)
{
#if ETA > 7
#error "rej_eta() assumes ETA <= 7"
#endif
  unsigned int ctr, pos;
  unsigned char t0, t1;

  ctr = pos = 0;
  while(ctr < len && pos < buflen) {
#if ETA <= 3
    t0 = buf[pos] & 0x07;
    t1 = buf[pos++] >> 5;
#else
    t0 = buf[pos] & 0x0F;
    t1 = buf[pos++] >> 4;
#endif

    if(t0 <= 2 * p->PARAM_ETA)
      a[ctr++] = Q + p->PARAM_ETA - t0;
    if(t1 <= 2 * p->PARAM_ETA && ctr < len)
      a[ctr++] = Q + p->PARAM_ETA - t1;
  }
  return ctr;
}

/*************************************************/
void poly_uniform_eta(poly *a, const unsigned char seed[SEEDBYTES], unsigned char nonce, dilithium_param_t *p)
{
    unsigned int i, ctr;
    unsigned char inbuf[SEEDBYTES + 1];

    unsigned char outbuf[2*DAP_SHAKE256_RATE];
    uint64_t state[25] = {0};

    for(i= 0; i < SEEDBYTES; ++i)
        inbuf[i] = seed[i];
    inbuf[SEEDBYTES] = nonce;

    dap_hash_shake256_absorb(state, inbuf, SEEDBYTES + 1);
    dap_hash_shake256_squeezeblocks(outbuf, 2, state);  

    ctr = rej_eta(a->coeffs, NN, outbuf, 2*DAP_SHAKE256_RATE, p);
    if(ctr < NN) {
        dap_hash_shake256_squeezeblocks(outbuf, 1, state);
        rej_eta(a->coeffs + ctr, NN - ctr, outbuf, DAP_SHAKE256_RATE, p);
    }
}

/*************************************************/
void poly_uniform_eta_x4(poly *a0, poly *a1, poly *a2, poly *a3,
                          const unsigned char seed[SEEDBYTES],
                          unsigned char n0, unsigned char n1,
                          unsigned char n2, unsigned char n3,
                          dilithium_param_t *p)
{
    unsigned char inbuf[4][SEEDBYTES + 1];
    unsigned char outbuf[4][2 * DAP_SHAKE256_RATE];

    for (int k = 0; k < 4; k++)
        memcpy(inbuf[k], seed, SEEDBYTES);
    inbuf[0][SEEDBYTES] = n0;
    inbuf[1][SEEDBYTES] = n1;
    inbuf[2][SEEDBYTES] = n2;
    inbuf[3][SEEDBYTES] = n3;

    dap_keccak_x4_state_t l_state;
    dap_hash_shake256_x4_absorb(&l_state, inbuf[0], inbuf[1], inbuf[2], inbuf[3],
                                 SEEDBYTES + 1);
    dap_hash_shake256_x4_squeezeblocks(outbuf[0], outbuf[1], outbuf[2], outbuf[3],
                                        2, &l_state);

    poly *l_polys[4] = {a0, a1, a2, a3};
    unsigned int l_ctr[4];
    int l_need_more = 0;
    for (int k = 0; k < 4; k++) {
        l_ctr[k] = rej_eta(l_polys[k]->coeffs, NN, outbuf[k], 2 * DAP_SHAKE256_RATE, p);
        if (l_ctr[k] < NN)
            l_need_more = 1;
    }
    if (l_need_more) {
        unsigned char l_extra[4][DAP_SHAKE256_RATE];
        dap_hash_shake256_x4_squeezeblocks(l_extra[0], l_extra[1], l_extra[2], l_extra[3],
                                            1, &l_state);
        for (int k = 0; k < 4; k++)
            if (l_ctr[k] < NN)
                rej_eta(l_polys[k]->coeffs + l_ctr[k], NN - l_ctr[k],
                        l_extra[k], DAP_SHAKE256_RATE, p);
    }
}

/*************************************************/
static unsigned int rej_gamma1m1(uint32_t *a, unsigned int len, const unsigned char *buf, unsigned int buflen)
{
#if GAMMA1 > (1 << 19)
#error "rej_gamma1m1() assumes GAMMA1 - 1 fits in 19 bits"
#endif
  unsigned int ctr, pos;
  uint32_t t0, t1;

  ctr = pos = 0;
  while(ctr < len && pos + 5 <= buflen) {
    t0  = buf[pos];
    t0 |= (uint32_t)buf[pos + 1] << 8;
    t0 |= (uint32_t)buf[pos + 2] << 16;
    t0 &= 0xFFFFF;

    t1  = buf[pos + 2] >> 4;
    t1 |= (uint32_t)buf[pos + 3] << 4;
    t1 |= (uint32_t)buf[pos + 4] << 12;

    pos += 5;

    if(t0 <= 2*GAMMA1 - 2)
      a[ctr++] = Q + GAMMA1 - 1 - t0;
    if(t1 <= 2*GAMMA1 - 2 && ctr < len)
      a[ctr++] = Q + GAMMA1 - 1 - t1;
  }
  return ctr;
}

/*************************************************/
void poly_uniform_gamma1m1(poly *a, const unsigned char seed[SEEDBYTES + CRHBYTES], uint16_t nonce)
{
    unsigned int i, ctr;
    unsigned char inbuf[SEEDBYTES + CRHBYTES + 2];

    unsigned char outbuf[5*DAP_SHAKE256_RATE];
    uint64_t state[25] = {0};

    for(i = 0; i < SEEDBYTES + CRHBYTES; ++i)
        inbuf[i] = seed[i];
    inbuf[SEEDBYTES + CRHBYTES] = nonce & 0xFF;
    inbuf[SEEDBYTES + CRHBYTES + 1] = nonce >> 8;

    dap_hash_shake256_absorb(state, inbuf, SEEDBYTES + CRHBYTES + 2);
    dap_hash_shake256_squeezeblocks(outbuf, 5, state);

    ctr = rej_gamma1m1(a->coeffs, NN, outbuf, 5*DAP_SHAKE256_RATE);
    if(ctr < NN) {
        dap_hash_shake256_squeezeblocks(outbuf, 1, state);
        rej_gamma1m1(a->coeffs + ctr, NN - ctr, outbuf, DAP_SHAKE256_RATE);
    }
}

/*************************************************/
void poly_uniform_gamma1m1_x4(poly *a0, poly *a1, poly *a2, poly *a3,
                               const unsigned char seed[SEEDBYTES + CRHBYTES],
                               uint16_t n0, uint16_t n1, uint16_t n2, uint16_t n3)
{
    unsigned char inbuf[4][SEEDBYTES + CRHBYTES + 2];
    unsigned char outbuf[4][5 * DAP_SHAKE256_RATE];

    for (int k = 0; k < 4; k++)
        memcpy(inbuf[k], seed, SEEDBYTES + CRHBYTES);

    const uint16_t l_nonces[4] = {n0, n1, n2, n3};
    for (int k = 0; k < 4; k++) {
        inbuf[k][SEEDBYTES + CRHBYTES]     = l_nonces[k] & 0xFF;
        inbuf[k][SEEDBYTES + CRHBYTES + 1] = l_nonces[k] >> 8;
    }

    dap_keccak_x4_state_t l_state;
    dap_hash_shake256_x4_absorb(&l_state, inbuf[0], inbuf[1], inbuf[2], inbuf[3],
                                 SEEDBYTES + CRHBYTES + 2);
    dap_hash_shake256_x4_squeezeblocks(outbuf[0], outbuf[1], outbuf[2], outbuf[3],
                                        5, &l_state);

    poly *l_polys[4] = {a0, a1, a2, a3};
    unsigned int l_ctr[4];
    int l_need_more = 0;
    for (int k = 0; k < 4; k++) {
        l_ctr[k] = rej_gamma1m1(l_polys[k]->coeffs, NN, outbuf[k], 5 * DAP_SHAKE256_RATE);
        if (l_ctr[k] < NN)
            l_need_more = 1;
    }
    if (l_need_more) {
        unsigned char l_extra[4][DAP_SHAKE256_RATE];
        dap_hash_shake256_x4_squeezeblocks(l_extra[0], l_extra[1], l_extra[2], l_extra[3],
                                            1, &l_state);
        for (int k = 0; k < 4; k++)
            if (l_ctr[k] < NN)
                rej_gamma1m1(l_polys[k]->coeffs + l_ctr[k], NN - l_ctr[k],
                             l_extra[k], DAP_SHAKE256_RATE);
    }
}

/*************************************************/
void polyeta_pack(unsigned char *r, const poly *a, dilithium_param_t *p)
{
    if (p->PARAM_ETA > 7)
    {
        printf("polyeta_pack() assumes ETA <= 7");
        return;
    }

    unsigned int i;
    unsigned char t[8];

    if (p->PARAM_ETA <= 3)
    {
        for(i = 0; i < NN/8; ++i)
        {
            t[0] = Q + p->PARAM_ETA - a->coeffs[8*i+0];
            t[1] = Q + p->PARAM_ETA - a->coeffs[8*i+1];
            t[2] = Q + p->PARAM_ETA - a->coeffs[8*i+2];
            t[3] = Q + p->PARAM_ETA - a->coeffs[8*i+3];
            t[4] = Q + p->PARAM_ETA - a->coeffs[8*i+4];
            t[5] = Q + p->PARAM_ETA - a->coeffs[8*i+5];
            t[6] = Q + p->PARAM_ETA - a->coeffs[8*i+6];
            t[7] = Q + p->PARAM_ETA - a->coeffs[8*i+7];

            r[3*i+0]  = t[0];
            r[3*i+0] |= t[1] << 3;
            r[3*i+0] |= t[2] << 6;
            r[3*i+1]  = t[2] >> 2;
            r[3*i+1] |= t[3] << 1;
            r[3*i+1] |= t[4] << 4;
            r[3*i+1] |= t[5] << 7;
            r[3*i+2]  = t[5] >> 1;
            r[3*i+2] |= t[6] << 2;
            r[3*i+2] |= t[7] << 5;
        }
    }
    else
    {
        for(i = 0; i < NN/2; ++i)
        {
            t[0] = Q + p->PARAM_ETA - a->coeffs[2*i+0];
            t[1] = Q + p->PARAM_ETA - a->coeffs[2*i+1];
            r[i] = t[0] | (t[1] << 4);
        }
    }
}

/*************************************************/
void polyeta_unpack(poly *r, const unsigned char *a, dilithium_param_t *p)
{
    unsigned int i;

    if (p->PARAM_ETA <= 3)
    {
        for(i = 0; i < NN/8; ++i)
        {
            r->coeffs[8*i+0] = a[3*i+0] & 0x07;
            r->coeffs[8*i+1] = (a[3*i+0] >> 3) & 0x07;
            r->coeffs[8*i+2] = (a[3*i+0] >> 6) | ((a[3*i+1] & 0x01) << 2);
            r->coeffs[8*i+3] = (a[3*i+1] >> 1) & 0x07;
            r->coeffs[8*i+4] = (a[3*i+1] >> 4) & 0x07;
            r->coeffs[8*i+5] = (a[3*i+1] >> 7) | ((a[3*i+2] & 0x03) << 1);
            r->coeffs[8*i+6] = (a[3*i+2] >> 2) & 0x07;
            r->coeffs[8*i+7] = (a[3*i+2] >> 5);

            r->coeffs[8*i+0] = Q + p->PARAM_ETA - r->coeffs[8*i+0];
            r->coeffs[8*i+1] = Q + p->PARAM_ETA - r->coeffs[8*i+1];
            r->coeffs[8*i+2] = Q + p->PARAM_ETA - r->coeffs[8*i+2];
            r->coeffs[8*i+3] = Q + p->PARAM_ETA - r->coeffs[8*i+3];
            r->coeffs[8*i+4] = Q + p->PARAM_ETA - r->coeffs[8*i+4];
            r->coeffs[8*i+5] = Q + p->PARAM_ETA - r->coeffs[8*i+5];
            r->coeffs[8*i+6] = Q + p->PARAM_ETA - r->coeffs[8*i+6];
            r->coeffs[8*i+7] = Q + p->PARAM_ETA - r->coeffs[8*i+7];
        }
    }
    else
    {
        for(i = 0; i < NN/2; ++i)
        {
            r->coeffs[2*i+0] = a[i] & 0x0F;
            r->coeffs[2*i+1] = a[i] >> 4;
            r->coeffs[2*i+0] = Q + p->PARAM_ETA - r->coeffs[2*i+0];
            r->coeffs[2*i+1] = Q + p->PARAM_ETA - r->coeffs[2*i+1];
        }
    }
}

/*************************************************/
void polyt1_pack_p(unsigned char *r, const poly *a, const dilithium_param_t *p) {
    uint32_t d_val = dil_d(p);
    uint32_t bits = 23 - d_val;  /* bitlen(q-1) - d */

    if (bits == 10) {
        /* d=13: 4 coefficients → 5 bytes (10 bits each) */
        for (unsigned i = 0; i < NN / 4; ++i) {
            r[5*i+0]  = (uint8_t)(a->coeffs[4*i+0]);
            r[5*i+1]  = (uint8_t)((a->coeffs[4*i+0] >> 8) | (a->coeffs[4*i+1] << 2));
            r[5*i+2]  = (uint8_t)((a->coeffs[4*i+1] >> 6) | (a->coeffs[4*i+2] << 4));
            r[5*i+3]  = (uint8_t)((a->coeffs[4*i+2] >> 4) | (a->coeffs[4*i+3] << 6));
            r[5*i+4]  = (uint8_t)(a->coeffs[4*i+3] >> 2);
        }
    } else {
        polyt1_pack(r, a);
    }
}

void polyt1_unpack_p(poly *r, const unsigned char *a, const dilithium_param_t *p) {
    uint32_t d_val = dil_d(p);
    uint32_t bits = 23 - d_val;

    if (bits == 10) {
        /* d=13: 5 bytes → 4 coefficients (10 bits each) */
        for (unsigned i = 0; i < NN / 4; ++i) {
            r->coeffs[4*i+0]  = (a[5*i+0]      | ((uint32_t)(a[5*i+1] & 0x03) << 8));
            r->coeffs[4*i+1]  = (a[5*i+1] >> 2) | ((uint32_t)(a[5*i+2] & 0x0F) << 6);
            r->coeffs[4*i+2]  = (a[5*i+2] >> 4) | ((uint32_t)(a[5*i+3] & 0x3F) << 4);
            r->coeffs[4*i+3]  = (a[5*i+3] >> 6) | ((uint32_t)(a[5*i+4])        << 2);
        }
    } else {
        polyt1_unpack(r, a);
    }
}

void polyt1_pack(unsigned char *r, const poly *a) {
#if D != 14
#error "polyt1_pack() assumes D == 14"
#endif
  unsigned int i;

  for(i = 0; i < NN/8; ++i) {
    r[9*i+0]  =  a->coeffs[8*i+0] & 0xFF;
    r[9*i+1]  = (a->coeffs[8*i+0] >> 8) | ((a->coeffs[8*i+1] & 0x7F) << 1);
    r[9*i+2]  = (a->coeffs[8*i+1] >> 7) | ((a->coeffs[8*i+2] & 0x3F) << 2);
    r[9*i+3]  = (a->coeffs[8*i+2] >> 6) | ((a->coeffs[8*i+3] & 0x1F) << 3);
    r[9*i+4]  = (a->coeffs[8*i+3] >> 5) | ((a->coeffs[8*i+4] & 0x0F) << 4);
    r[9*i+5]  = (a->coeffs[8*i+4] >> 4) | ((a->coeffs[8*i+5] & 0x07) << 5);
    r[9*i+6]  = (a->coeffs[8*i+5] >> 3) | ((a->coeffs[8*i+6] & 0x03) << 6);
    r[9*i+7]  = (a->coeffs[8*i+6] >> 2) | ((a->coeffs[8*i+7] & 0x01) << 7);
    r[9*i+8]  =  a->coeffs[8*i+7] >> 1;
  }
}

/*************************************************/
void polyt1_unpack(poly *r, const unsigned char *a) {
  unsigned int i;

  for(i = 0; i < NN/8; ++i) {
    r->coeffs[8*i+0] =  a[9*i+0]       | ((uint32_t)(a[9*i+1] & 0x01) << 8);
    r->coeffs[8*i+1] = (a[9*i+1] >> 1) | ((uint32_t)(a[9*i+2] & 0x03) << 7);
    r->coeffs[8*i+2] = (a[9*i+2] >> 2) | ((uint32_t)(a[9*i+3] & 0x07) << 6);
    r->coeffs[8*i+3] = (a[9*i+3] >> 3) | ((uint32_t)(a[9*i+4] & 0x0F) << 5);
    r->coeffs[8*i+4] = (a[9*i+4] >> 4) | ((uint32_t)(a[9*i+5] & 0x1F) << 4);
    r->coeffs[8*i+5] = (a[9*i+5] >> 5) | ((uint32_t)(a[9*i+6] & 0x3F) << 3);
    r->coeffs[8*i+6] = (a[9*i+6] >> 6) | ((uint32_t)(a[9*i+7] & 0x7F) << 2);
    r->coeffs[8*i+7] = (a[9*i+7] >> 7) | ((uint32_t)(a[9*i+8] & 0xFF) << 1);
  }
}

/*************************************************/
void polyt0_pack(unsigned char *r, const poly *a) {
  unsigned int i;
  uint32_t t[4];

  for(i = 0; i < NN/4; ++i) {
    t[0] = Q + (1 << (D-1)) - a->coeffs[4*i+0];
    t[1] = Q + (1 << (D-1)) - a->coeffs[4*i+1];
    t[2] = Q + (1 << (D-1)) - a->coeffs[4*i+2];
    t[3] = Q + (1 << (D-1)) - a->coeffs[4*i+3];

    r[7*i+0]  =  t[0];
    r[7*i+1]  =  t[0] >> 8;
    r[7*i+1] |=  t[1] << 6;
    r[7*i+2]  =  t[1] >> 2;
    r[7*i+3]  =  t[1] >> 10;
    r[7*i+3] |=  t[2] << 4;
    r[7*i+4]  =  t[2] >> 4;
    r[7*i+5]  =  t[2] >> 12;
    r[7*i+5] |=  t[3] << 2;
    r[7*i+6]  =  t[3] >> 6;
  }
}

/*************************************************/
void polyt0_unpack(poly *r, const unsigned char *a) {
  unsigned int i;

  for(i = 0; i < NN/4; ++i) {
    r->coeffs[4*i+0]  = a[7*i+0];
    r->coeffs[4*i+0] |= (uint32_t)(a[7*i+1] & 0x3F) << 8;

    r->coeffs[4*i+1]  = a[7*i+1] >> 6;
    r->coeffs[4*i+1] |= (uint32_t)a[7*i+2] << 2;
    r->coeffs[4*i+1] |= (uint32_t)(a[7*i+3] & 0x0F) << 10;

    r->coeffs[4*i+2]  = a[7*i+3] >> 4;
    r->coeffs[4*i+2] |= (uint32_t)a[7*i+4] << 4;
    r->coeffs[4*i+2] |= (uint32_t)(a[7*i+5] & 0x03) << 12;

    r->coeffs[4*i+3]  = a[7*i+5] >> 2;
    r->coeffs[4*i+3] |= (uint32_t)a[7*i+6] << 6;

    r->coeffs[4*i+0] = Q + (1 << (D-1)) - r->coeffs[4*i+0];
    r->coeffs[4*i+1] = Q + (1 << (D-1)) - r->coeffs[4*i+1];
    r->coeffs[4*i+2] = Q + (1 << (D-1)) - r->coeffs[4*i+2];
    r->coeffs[4*i+3] = Q + (1 << (D-1)) - r->coeffs[4*i+3];
  }
}

/*************************************************/
void polyz_pack(unsigned char *r, const poly *a) {
#if GAMMA1 > (1 << 19)
#error "polyz_pack() assumes GAMMA1 <= 2^{19}"
#endif
  unsigned int i;
  uint32_t t[2];

  for(i = 0; i < NN/2; ++i) {    
    t[0] = GAMMA1 - 1 - a->coeffs[2*i+0];
    t[0] += ((int32_t)t[0] >> 31) & Q;
    t[1] = GAMMA1 - 1 - a->coeffs[2*i+1];
    t[1] += ((int32_t)t[1] >> 31) & Q;

    r[5*i+0]  = t[0];
    r[5*i+1]  = t[0] >> 8;
    r[5*i+2]  = t[0] >> 16;
    r[5*i+2] |= t[1] << 4;
    r[5*i+3]  = t[1] >> 4;
    r[5*i+4]  = t[1] >> 12;
  }
}

/*************************************************/
void polyz_unpack(poly *r, const unsigned char *a) {
  unsigned int i;

  for(i = 0; i < NN/2; ++i) {
    r->coeffs[2*i+0]  = a[5*i+0];
    r->coeffs[2*i+0] |= (uint32_t)a[5*i+1] << 8;
    r->coeffs[2*i+0] |= (uint32_t)(a[5*i+2] & 0x0F) << 16;

    r->coeffs[2*i+1]  = a[5*i+2] >> 4;
    r->coeffs[2*i+1] |= (uint32_t)a[5*i+3] << 4;
    r->coeffs[2*i+1] |= (uint32_t)a[5*i+4] << 12;

    r->coeffs[2*i+0] = GAMMA1 - 1 - r->coeffs[2*i+0];
    r->coeffs[2*i+0] += ((int32_t)r->coeffs[2*i+0] >> 31) & Q;
    r->coeffs[2*i+1] = GAMMA1 - 1 - r->coeffs[2*i+1];
    r->coeffs[2*i+1] += ((int32_t)r->coeffs[2*i+1] >> 31) & Q;
  }
}

/*************************************************/
void polyw1_pack(unsigned char *r, const poly *a) {
  unsigned int i;

  for(i = 0; i < NN/2; ++i)
    r[i] = a->coeffs[2*i+0] | (a->coeffs[2*i+1] << 4);
}

/**************************************************/
static const uint32_t zetas[NN] = {0, 25847, 5771523, 7861508, 237124, 7602457, 7504169, 466468,
                        1826347, 2353451, 8021166, 6288512, 3119733, 5495562, 3111497, 2680103,
                        2725464, 1024112, 7300517, 3585928, 7830929, 7260833, 2619752, 6271868,
                        6262231, 4520680, 6980856, 5102745, 1757237, 8360995, 4010497,  280005,
                        2706023,   95776, 3077325, 3530437, 6718724, 4788269, 5842901, 3915439,
                        4519302, 5336701, 3574422, 5512770, 3539968, 8079950, 2348700, 7841118,
                        6681150, 6736599, 3505694, 4558682, 3507263, 6239768, 6779997, 3699596,
                         811944,  531354,  954230, 3881043, 3900724, 5823537, 2071892, 5582638,
                        4450022, 6851714, 4702672, 5339162, 6927966, 3475950, 2176455, 6795196,
                        7122806, 1939314, 4296819, 7380215, 5190273, 5223087, 4747489,  126922,
                        3412210, 7396998, 2147896, 2715295, 5412772, 4686924, 7969390, 5903370,
                        7709315, 7151892, 8357436, 7072248, 7998430, 1349076, 1852771, 6949987,
                        5037034,  264944,  508951, 3097992,   44288, 7280319,  904516, 3958618,
                        4656075, 8371839, 1653064, 5130689, 2389356, 8169440,  759969, 7063561,
                         189548, 4827145, 3159746, 6529015, 5971092, 8202977, 1315589, 1341330,
                        1285669, 6795489, 7567685, 6940675, 5361315, 4499357, 4751448, 3839961,
                        2091667, 3407706, 2316500, 3817976, 5037939, 2244091, 5933984, 4817955,
                         266997, 2434439, 7144689, 3513181, 4860065, 4621053, 7183191, 5187039,
                         900702, 1859098,  909542,  819034,  495491, 6767243, 8337157, 7857917,
                        7725090, 5257975, 2031748, 3207046, 4823422, 7855319, 7611795, 4784579,
                         342297,  286988, 5942594, 4108315, 3437287, 5038140, 1735879,  203044,
                        2842341, 2691481, 5790267, 1265009, 4055324, 1247620, 2486353, 1595974,
                        4613401, 1250494, 2635921, 4832145, 5386378, 1869119, 1903435, 7329447,
                        7047359, 1237275, 5062207, 6950192, 7929317, 1312455, 3306115, 6417775,
                        7100756, 1917081, 5834105, 7005614, 1500165,  777191, 2235880, 3406031,
                        7838005, 5548557, 6709241, 6533464, 5796124, 4656147,  594136, 4603424,
                        6366809, 2432395, 2454455, 8215696, 1957272, 3369112,  185531, 7173032,
                        5196991,  162844, 1616392, 3014001,  810149, 1652634, 4686184, 6581310,
                        5341501, 3523897, 3866901,  269760, 2213111, 7404533, 1717735,  472078,
                        7953734, 1723600, 6577327, 1910376, 6712985, 7276084, 8119771, 4546524,
                        5441381, 6144432, 7959518, 6094090,  183443, 7403526, 1612842, 4834730,
                        7826001, 3919660, 8332111, 7018208, 3937738, 1400424, 7534263, 1976782};

static const uint32_t zetas_inv[NN] =
                       {6403635,  846154, 6979993, 4442679, 1362209,   48306, 4460757,  554416,
                        3545687, 6767575,  976891, 8196974, 2286327,  420899, 2235985, 2939036,
                        3833893,  260646, 1104333, 1667432, 6470041, 1803090, 6656817,  426683,
                        7908339, 6662682,  975884, 6167306, 8110657, 4513516, 4856520, 3038916,
                        1799107, 3694233, 6727783, 7570268, 5366416, 6764025, 8217573, 3183426,
                        1207385, 8194886, 5011305, 6423145,  164721, 5925962, 5948022, 2013608,
                        3776993, 7786281, 3724270, 2584293, 1846953, 1671176, 2831860,  542412,
                        4974386, 6144537, 7603226, 6880252, 1374803, 2546312, 6463336, 1279661,
                        1962642, 5074302, 7067962,  451100, 1430225, 3318210, 7143142, 1333058,
                        1050970, 6476982, 6511298, 2994039, 3548272, 5744496, 7129923, 3767016,
                        6784443, 5894064, 7132797, 4325093, 7115408, 2590150, 5688936, 5538076,
                        8177373, 6644538, 3342277, 4943130, 4272102, 2437823, 8093429, 8038120,
                        3595838,  768622,  525098, 3556995, 5173371, 6348669, 3122442,  655327,
                         522500,   43260, 1613174, 7884926, 7561383, 7470875, 6521319, 7479715,
                        3193378, 1197226, 3759364, 3520352, 4867236, 1235728, 5945978, 8113420,
                        3562462, 2446433, 6136326, 3342478, 4562441, 6063917, 4972711, 6288750,
                        4540456, 3628969, 3881060, 3019102, 1439742,  812732, 1584928, 7094748,
                        7039087, 7064828,  177440, 2409325, 1851402, 5220671, 3553272, 8190869,
                        1316856, 7620448,  210977, 5991061, 3249728, 6727353,    8578, 3724342,
                        4421799, 7475901, 1100098, 8336129, 5282425, 7871466, 8115473, 3343383,
                        1430430, 6527646, 7031341,  381987, 1308169,   22981, 1228525,  671102,
                        2477047,  411027, 3693493, 2967645, 5665122, 6232521,  983419, 4968207,
                        8253495, 3632928, 3157330, 3190144, 1000202, 4083598, 6441103, 1257611,
                        1585221, 6203962, 4904467, 1452451, 3041255, 3677745, 1528703, 3930395,
                        2797779, 6308525, 2556880, 4479693, 4499374, 7426187, 7849063, 7568473,
                        4680821, 1600420, 2140649, 4873154, 3821735, 4874723, 1643818, 1699267,
                         539299, 6031717,  300467, 4840449, 2867647, 4805995, 3043716, 3861115,
                        4464978, 2537516, 3592148, 1661693, 4849980, 5303092, 8284641, 5674394,
                        8100412, 4369920,   19422, 6623180, 3277672, 1399561, 3859737, 2118186,
                        2108549, 5760665, 1119584,  549488, 4794489, 1079900, 7356305, 5654953,
                        5700314, 5268920, 2884855, 5260684, 2091905,  359251, 6026966, 6554070,
                        7913949,  876248,  777960, 8143293,  518909, 2608894, 8354570};

const dap_ntt_params_t g_dilithium_ntt_params = {
    .n            = NN,
    .q            = Q,
    .qinv         = 4236238847U,
    .mont_r_bits  = 32,
    .mont_r_mask  = 0xFFFFFFFF,
    .one_over_n   = 8347681,
    .zetas        = (const int32_t *)zetas,
    .zetas_inv    = (const int32_t *)zetas_inv,
    .zetas_len    = NN,
};

/*************************************************/
void dilithium_ntt(uint32_t pp[NN])
{
    DAP_DISPATCH_ENSURE(s_dil_ntt_fwd, s_dil_dispatch_init);
    s_dil_ntt_fwd_ptr((int32_t *)pp);
}

/*************************************************/
void invntt_frominvmont(uint32_t pp[NN])
{
    DAP_DISPATCH_ENSURE(s_dil_ntt_inv, s_dil_dispatch_init);
    s_dil_ntt_inv_ptr((int32_t *)pp);
}

/*************************************************/
void dilithium_poly_nttunpack(int32_t coeffs[NN])
{
    DAP_DISPATCH_ENSURE(s_dil_nttunpack, s_dil_dispatch_init);
    if (s_dil_nttunpack_ptr)
        s_dil_nttunpack_ptr(coeffs);
}

/*
 * =========================================================================
 * Parameterized poly operations for FIPS 204 (generic scalar, all platforms)
 * =========================================================================
 */

void poly_power2round_p(poly *a1, poly *a0, const poly *a, const dilithium_param_t *p)
{
    for (unsigned i = 0; i < NN; ++i)
        a1->coeffs[i] = power2round_p(a->coeffs[i], a0->coeffs + i, p);
}

void poly_decompose_p(poly *a1, poly *a0, const poly *a, const dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_dil_decompose_g88, s_dil_dispatch_init);
    if (dil_gamma2(p) == (Q - 1) / 88)
        s_dil_decompose_g88_ptr((int32_t *)a1->coeffs, (int32_t *)a0->coeffs, (const int32_t *)a->coeffs);
    else
        s_dil_decompose_g32_ptr((int32_t *)a1->coeffs, (int32_t *)a0->coeffs, (const int32_t *)a->coeffs);
}

unsigned int poly_make_hint_p(poly *h, const poly *a, const poly *b, const dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_dil_make_hint_g88, s_dil_dispatch_init);
    if (dil_gamma2(p) == (Q - 1) / 88)
        return s_dil_make_hint_g88_ptr((int32_t *)h->coeffs, (const int32_t *)a->coeffs, (const int32_t *)b->coeffs);
    else
        return s_dil_make_hint_g32_ptr((int32_t *)h->coeffs, (const int32_t *)a->coeffs, (const int32_t *)b->coeffs);
}

void poly_use_hint_p(poly *a, const poly *b, const poly *h, const dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_dil_use_hint_g88, s_dil_dispatch_init);
    if (dil_gamma2(p) == (Q - 1) / 88)
        s_dil_use_hint_g88_ptr((int32_t *)a->coeffs, (const int32_t *)b->coeffs, (const int32_t *)h->coeffs);
    else
        s_dil_use_hint_g32_ptr((int32_t *)a->coeffs, (const int32_t *)b->coeffs, (const int32_t *)h->coeffs);
}

/*************************************************/
void polyz_pack_p(unsigned char *r, const poly *a, const dilithium_param_t *p)
{
    uint32_t gamma1 = dil_gamma1(p);
    unsigned gbits = dil_gamma1_bits(p) + 1;  /* bits per coefficient */

    if (gbits == 18) {
        /* gamma1 = 2^17: pack 18 bits per coefficient, 4 coefficients in 9 bytes */
        for (unsigned i = 0; i < NN / 4; ++i) {
            uint32_t t[4];
            for (int j = 0; j < 4; j++) {
                t[j] = gamma1 - 1 - a->coeffs[4*i+j];
                t[j] += ((int32_t)t[j] >> 31) & Q;
            }
            r[9*i+0]  = (uint8_t)t[0];
            r[9*i+1]  = (uint8_t)(t[0] >> 8);
            r[9*i+2]  = (uint8_t)((t[0] >> 16) | (t[1] << 2));
            r[9*i+3]  = (uint8_t)(t[1] >> 6);
            r[9*i+4]  = (uint8_t)((t[1] >> 14) | (t[2] << 4));
            r[9*i+5]  = (uint8_t)(t[2] >> 4);
            r[9*i+6]  = (uint8_t)((t[2] >> 12) | (t[3] << 6));
            r[9*i+7]  = (uint8_t)(t[3] >> 2);
            r[9*i+8]  = (uint8_t)(t[3] >> 10);
        }
    } else {
        /* gamma1 = 2^19: pack 20 bits per coefficient, 2 coefficients in 5 bytes */
        for (unsigned i = 0; i < NN / 2; ++i) {
            uint32_t t[2];
            t[0] = gamma1 - 1 - a->coeffs[2*i+0];
            t[0] += ((int32_t)t[0] >> 31) & Q;
            t[1] = gamma1 - 1 - a->coeffs[2*i+1];
            t[1] += ((int32_t)t[1] >> 31) & Q;
            r[5*i+0]  = (uint8_t)t[0];
            r[5*i+1]  = (uint8_t)(t[0] >> 8);
            r[5*i+2]  = (uint8_t)((t[0] >> 16) | (t[1] << 4));
            r[5*i+3]  = (uint8_t)(t[1] >> 4);
            r[5*i+4]  = (uint8_t)(t[1] >> 12);
        }
    }
}

/*************************************************/
void polyz_unpack_p(poly *r, const unsigned char *a, const dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_dil_zunpack_g17, s_dil_dispatch_init);
    unsigned gbits = dil_gamma1_bits(p) + 1;
    if (gbits == 18)
        s_dil_zunpack_g17_ptr((int32_t *)r->coeffs, a);
    else
        s_dil_zunpack_g19_ptr((int32_t *)r->coeffs, a);
}

/*************************************************/
void polyw1_pack_p(unsigned char *r, const poly *a, const dilithium_param_t *p)
{
    DAP_DISPATCH_ENSURE(s_dil_w1pack_g88, s_dil_dispatch_init);
    if (dil_gamma2(p) == (Q - 1) / 88)
        s_dil_w1pack_g88_ptr(r, (const int32_t *)a->coeffs);
    else
        s_dil_w1pack_g32_ptr(r, (const int32_t *)a->coeffs);
}

/*************************************************/
void polyt0_pack_p(unsigned char *r, const poly *a, const dilithium_param_t *p)
{
    uint32_t d_val = dil_d(p);
    uint32_t half = 1U << (d_val - 1);

    if (d_val == 13) {
        /* d=13: 8 coefficients → 13 bytes */
        for (unsigned i = 0; i < NN / 8; ++i) {
            uint32_t t[8];
            for (int j = 0; j < 8; j++)
                t[j] = (Q + half - a->coeffs[8*i+j]) & 0x1FFF;

            r[13*i+ 0]  = (uint8_t)(t[0]);
            r[13*i+ 1]  = (uint8_t)((t[0] >> 8) | (t[1] << 5));
            r[13*i+ 2]  = (uint8_t)(t[1] >> 3);
            r[13*i+ 3]  = (uint8_t)((t[1] >> 11) | (t[2] << 2));
            r[13*i+ 4]  = (uint8_t)((t[2] >> 6) | (t[3] << 7));
            r[13*i+ 5]  = (uint8_t)(t[3] >> 1);
            r[13*i+ 6]  = (uint8_t)((t[3] >> 9) | (t[4] << 4));
            r[13*i+ 7]  = (uint8_t)(t[4] >> 4);
            r[13*i+ 8]  = (uint8_t)((t[4] >> 12) | (t[5] << 1));
            r[13*i+ 9]  = (uint8_t)((t[5] >> 7) | (t[6] << 6));
            r[13*i+10]  = (uint8_t)(t[6] >> 2);
            r[13*i+11]  = (uint8_t)((t[6] >> 10) | (t[7] << 3));
            r[13*i+12]  = (uint8_t)(t[7] >> 5);
        }
    } else {
        /* d=14: 4 coefficients → 7 bytes (legacy layout) */
        for (unsigned i = 0; i < NN / 4; ++i) {
            uint32_t t[4];
            for (int j = 0; j < 4; j++)
                t[j] = Q + half - a->coeffs[4*i+j];

            r[7*i+0]  = (uint8_t)t[0];
            r[7*i+1]  = (uint8_t)((t[0] >> 8) | (t[1] << 6));
            r[7*i+2]  = (uint8_t)(t[1] >> 2);
            r[7*i+3]  = (uint8_t)((t[1] >> 10) | (t[2] << 4));
            r[7*i+4]  = (uint8_t)(t[2] >> 4);
            r[7*i+5]  = (uint8_t)((t[2] >> 12) | (t[3] << 2));
            r[7*i+6]  = (uint8_t)(t[3] >> 6);
        }
    }
}

/*************************************************/
void polyt0_unpack_p(poly *r, const unsigned char *a, const dilithium_param_t *p)
{
    uint32_t d_val = dil_d(p);
    uint32_t half = 1U << (d_val - 1);

    if (d_val == 13) {
        /* d=13: 13 bytes → 8 coefficients */
        for (unsigned i = 0; i < NN / 8; ++i) {
            r->coeffs[8*i+0]  = a[13*i+0];
            r->coeffs[8*i+0] |= (uint32_t)(a[13*i+1] & 0x1F) << 8;

            r->coeffs[8*i+1]  = a[13*i+1] >> 5;
            r->coeffs[8*i+1] |= (uint32_t)a[13*i+2] << 3;
            r->coeffs[8*i+1] |= (uint32_t)(a[13*i+3] & 0x03) << 11;

            r->coeffs[8*i+2]  = a[13*i+3] >> 2;
            r->coeffs[8*i+2] |= (uint32_t)(a[13*i+4] & 0x7F) << 6;

            r->coeffs[8*i+3]  = a[13*i+4] >> 7;
            r->coeffs[8*i+3] |= (uint32_t)a[13*i+5] << 1;
            r->coeffs[8*i+3] |= (uint32_t)(a[13*i+6] & 0x0F) << 9;

            r->coeffs[8*i+4]  = a[13*i+6] >> 4;
            r->coeffs[8*i+4] |= (uint32_t)a[13*i+7] << 4;
            r->coeffs[8*i+4] |= (uint32_t)(a[13*i+8] & 0x01) << 12;

            r->coeffs[8*i+5]  = a[13*i+8] >> 1;
            r->coeffs[8*i+5] |= (uint32_t)(a[13*i+9] & 0x3F) << 7;

            r->coeffs[8*i+6]  = a[13*i+9] >> 6;
            r->coeffs[8*i+6] |= (uint32_t)a[13*i+10] << 2;
            r->coeffs[8*i+6] |= (uint32_t)(a[13*i+11] & 0x07) << 10;

            r->coeffs[8*i+7]  = a[13*i+11] >> 3;
            r->coeffs[8*i+7] |= (uint32_t)a[13*i+12] << 5;

            for (int j = 0; j < 8; j++)
                r->coeffs[8*i+j] = Q + half - r->coeffs[8*i+j];
        }
    } else {
        /* d=14: 7 bytes → 4 coefficients (legacy layout) */
        for (unsigned i = 0; i < NN / 4; ++i) {
            r->coeffs[4*i+0]  = a[7*i+0];
            r->coeffs[4*i+0] |= (uint32_t)(a[7*i+1] & 0x3F) << 8;

            r->coeffs[4*i+1]  = a[7*i+1] >> 6;
            r->coeffs[4*i+1] |= (uint32_t)a[7*i+2] << 2;
            r->coeffs[4*i+1] |= (uint32_t)(a[7*i+3] & 0x0F) << 10;

            r->coeffs[4*i+2]  = a[7*i+3] >> 4;
            r->coeffs[4*i+2] |= (uint32_t)a[7*i+4] << 4;
            r->coeffs[4*i+2] |= (uint32_t)(a[7*i+5] & 0x03) << 12;

            r->coeffs[4*i+3]  = a[7*i+5] >> 2;
            r->coeffs[4*i+3] |= (uint32_t)a[7*i+6] << 6;

            for (int j = 0; j < 4; j++)
                r->coeffs[4*i+j] = Q + half - r->coeffs[4*i+j];
        }
    }
}

/*************************************************/
static unsigned int rej_gamma1m1_p(uint32_t *a, unsigned int len,
                                   const unsigned char *buf, unsigned int buflen,
                                   const dilithium_param_t *p)
{
    uint32_t gamma1 = dil_gamma1(p);
    uint32_t bound = 2 * gamma1 - 2;
    unsigned gbits = dil_gamma1_bits(p) + 1;
    unsigned ctr, pos;

    ctr = pos = 0;

    if (gbits == 18) {
        /* 18-bit samples from 9-byte groups, 4 samples each */
        while (ctr < len && pos + 9 <= buflen) {
            uint32_t t[4];
            t[0]  = buf[pos];
            t[0] |= (uint32_t)buf[pos+1] << 8;
            t[0] &= 0x3FFFF;
            t[1]  = buf[pos+2] >> 2;
            t[1] |= (uint32_t)buf[pos+3] << 6;
            t[1] |= (uint32_t)(buf[pos+4] & 0x0F) << 14;
            t[2]  = buf[pos+4] >> 4;
            t[2] |= (uint32_t)buf[pos+5] << 4;
            t[2] |= (uint32_t)(buf[pos+6] & 0x3F) << 12;
            t[3]  = buf[pos+6] >> 6;
            t[3] |= (uint32_t)buf[pos+7] << 2;
            t[3] |= (uint32_t)buf[pos+8] << 10;
            pos += 9;
            for (int j = 0; j < 4 && ctr < len; j++) {
                if (t[j] <= bound)
                    a[ctr++] = Q + gamma1 - 1 - t[j];
            }
        }
    } else {
        /* 20-bit samples from 5-byte groups, 2 samples each */
        while (ctr < len && pos + 5 <= buflen) {
            uint32_t t0, t1;
            t0  = buf[pos];
            t0 |= (uint32_t)buf[pos+1] << 8;
            t0 |= (uint32_t)buf[pos+2] << 16;
            t0 &= 0xFFFFF;
            t1  = buf[pos+2] >> 4;
            t1 |= (uint32_t)buf[pos+3] << 4;
            t1 |= (uint32_t)buf[pos+4] << 12;
            pos += 5;
            if (t0 <= bound)
                a[ctr++] = Q + gamma1 - 1 - t0;
            if (t1 <= bound && ctr < len)
                a[ctr++] = Q + gamma1 - 1 - t1;
        }
    }
    return ctr;
}

/*************************************************/
void poly_uniform_gamma1m1_p(poly *a, const unsigned char *seed, uint16_t nonce,
                              const dilithium_param_t *p)
{
    unsigned int ctr;
    uint32_t crh = dil_crhbytes(p);
    unsigned char inbuf[SEEDBYTES + 66 + 2];
    unsigned char outbuf[5 * DAP_SHAKE256_RATE];
    uint64_t state[25] = {0};

    memcpy(inbuf, seed, SEEDBYTES + crh);
    inbuf[SEEDBYTES + crh]     = nonce & 0xFF;
    inbuf[SEEDBYTES + crh + 1] = nonce >> 8;

    dap_hash_shake256_absorb(state, inbuf, SEEDBYTES + crh + 2);
    dap_hash_shake256_squeezeblocks(outbuf, 5, state);

    ctr = rej_gamma1m1_p(a->coeffs, NN, outbuf, 5 * DAP_SHAKE256_RATE, p);
    if (ctr < NN) {
        dap_hash_shake256_squeezeblocks(outbuf, 1, state);
        rej_gamma1m1_p(a->coeffs + ctr, NN - ctr, outbuf, DAP_SHAKE256_RATE, p);
    }
}

