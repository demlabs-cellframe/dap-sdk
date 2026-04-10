{{! ================================================================ }}
{{! Dilithium/ML-DSA SIMD polynomial operations                     }}
{{! ================================================================ }}
{{! neg, shiftl, decompose, power2round, make_hint, use_hint,       }}
{{! chknorm — vectorized for any VEC_LANES width.                   }}
{{! Included by dap_dilithium_poly_simd.c.tpl.                      }}
{{! Expects all primitives from reduce.tpl PLUS:                    }}
{{!   VEC_CMPEQ_32, VEC_CMPGT_32, VEC_BLENDV_32, VEC_ANY_TRUE_32   }}
{{! ================================================================ }}

#ifndef DIL_Q
#define DIL_N       256
#define DIL_Q       8380417
#endif

#define DIL_D       14
#define DIL_GAMMA1  ((DIL_Q - 1U) / 16U)
#define DIL_GAMMA2  (DIL_GAMMA1 / 2U)
#define DIL_ALPHA   (2U * DIL_GAMMA2)

{{TARGET_ATTR}}
static inline void s_poly_neg_vec(int32_t *a_coeffs)
{
    const VEC_T l_q = VEC_SET1_32(DIL_Q);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SUB32(l_q, v));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_shiftl_vec(int32_t *a_coeffs, unsigned a_k)
{
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_STORE(a_coeffs + i, VEC_SLLI32(v, a_k));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_decompose_vec(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    const VEC_T l_mask19   = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha    = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1  = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1  = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q        = VEC_SET1_32(DIL_Q);
    const VEC_T l_one      = VEC_SET1_32(1);
    const VEC_T l_0xf      = VEC_SET1_32(0xF);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T t  = VEC_AND(va, l_mask19);
        t = VEC_ADD32(t, VEC_SLLI32(VEC_SRLI32(va, 19), 9));
        t = VEC_SUB32(t, l_half_p1);
        VEC_T tm = VEC_SRAI32(t, 31);
        t = VEC_ADD32(t, VEC_AND(tm, l_alpha));
        t = VEC_SUB32(t, l_half_m1);

        VEC_T a_val = VEC_SUB32(va, t);
        VEC_T u = VEC_SRAI32(VEC_SUB32(a_val, l_one), 31);
        a_val = VEC_SUB32(VEC_ADD32(VEC_SRLI32(a_val, 19), l_one),
                          VEC_AND(u, l_one));

        VEC_STORE(a_a0 + i,
            VEC_SUB32(VEC_ADD32(l_q, t), VEC_SRLI32(a_val, 4)));
        VEC_STORE(a_a1 + i, VEC_AND(a_val, l_0xf));
    }
}

{{TARGET_ATTR}}
static inline void s_poly_power2round_vec(int32_t * restrict a_a1,
    int32_t * restrict a_a0, const int32_t * restrict a_a)
{
    const VEC_T l_d_mask    = VEC_SET1_32((1 << DIL_D) - 1);
    const VEC_T l_d_half_p1 = VEC_SET1_32((1 << (DIL_D - 1)) + 1);
    const VEC_T l_d_full    = VEC_SET1_32(1 << DIL_D);
    const VEC_T l_d_half_m1 = VEC_SET1_32((1 << (DIL_D - 1)) - 1);
    const VEC_T l_q         = VEC_SET1_32(DIL_Q);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T t  = VEC_AND(va, l_d_mask);
        t = VEC_SUB32(t, l_d_half_p1);
        VEC_T tm = VEC_SRAI32(t, 31);
        t = VEC_ADD32(t, VEC_AND(tm, l_d_full));
        t = VEC_SUB32(t, l_d_half_m1);

        VEC_STORE(a_a0 + i, VEC_ADD32(l_q, t));
        VEC_STORE(a_a1 + i, VEC_SRLI32(VEC_SUB32(va, t), DIL_D));
    }
}

/* Inline decompose helper for make_hint/use_hint: returns (a1, a0_for_hint) */
#define S_DECOMPOSE_HINT(va, l_mask19, l_alpha, l_half_p1, l_half_m1, \
                         l_q, l_one, l_0xf, out_a1, out_a0) do {      \
    VEC_T _t = VEC_AND(va, l_mask19);                                  \
    _t = VEC_ADD32(_t, VEC_SLLI32(VEC_SRLI32(va, 19), 9));            \
    _t = VEC_SUB32(_t, l_half_p1);                                     \
    _t = VEC_ADD32(_t, VEC_AND(VEC_SRAI32(_t, 31), l_alpha));         \
    _t = VEC_SUB32(_t, l_half_m1);                                     \
    VEC_T _av = VEC_SUB32(va, _t);                                     \
    VEC_T _u = VEC_SRAI32(VEC_SUB32(_av, l_one), 31);                 \
    _av = VEC_SUB32(VEC_ADD32(VEC_SRLI32(_av, 19), l_one),            \
                    VEC_AND(_u, l_one));                               \
    (out_a1) = VEC_AND(_av, l_0xf);                                    \
    (out_a0) = VEC_SUB32(VEC_ADD32(l_q, _t), VEC_SRLI32(_av, 4));     \
} while (0)

{{TARGET_ATTR}}
static inline unsigned s_poly_make_hint_vec(int32_t * restrict a_h,
    const int32_t * restrict a_a, const int32_t * restrict a_b)
{
    unsigned s = 0;
    const VEC_T l_mask19  = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha   = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1 = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1 = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q       = VEC_SET1_32(DIL_Q);
    const VEC_T l_one     = VEC_SET1_32(1);
    const VEC_T l_0xf     = VEC_SET1_32(0xF);

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T va = VEC_LOAD(a_a + i);
        VEC_T vb = VEC_LOAD(a_b + i);

        VEC_T a1a, a0a, a1b, a0b;
        S_DECOMPOSE_HINT(va, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1a, a0a);
        S_DECOMPOSE_HINT(vb, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1b, a0b);

        VEC_T cmp  = VEC_CMPEQ_32(a1a, a1b);
        VEC_T hint = VEC_ANDNOT(cmp, l_one);
        VEC_STORE(a_h + i, hint);

        int32_t buf[VEC_LANES];
        VEC_STORE(buf, hint);
        for (int j = 0; j < VEC_LANES; j++) s += (unsigned)buf[j];
    }
    return s;
}

{{TARGET_ATTR}}
static inline void s_poly_use_hint_vec(int32_t * restrict a_r,
    const int32_t * restrict a_b, const int32_t * restrict a_h)
{
    const VEC_T l_mask19  = VEC_SET1_32(0x7FFFF);
    const VEC_T l_alpha   = VEC_SET1_32((int)DIL_ALPHA);
    const VEC_T l_half_p1 = VEC_SET1_32((int)(DIL_ALPHA / 2 + 1));
    const VEC_T l_half_m1 = VEC_SET1_32((int)(DIL_ALPHA / 2 - 1));
    const VEC_T l_q       = VEC_SET1_32(DIL_Q);
    const VEC_T l_one     = VEC_SET1_32(1);
    const VEC_T l_0xf     = VEC_SET1_32(0xF);
    const VEC_T l_zero    = VEC_ZERO();

    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T vb = VEC_LOAD(a_b + i);
        VEC_T vh = VEC_LOAD(a_h + i);

        VEC_T a1, a0;
        S_DECOMPOSE_HINT(vb, l_mask19, l_alpha, l_half_p1, l_half_m1,
                         l_q, l_one, l_0xf, a1, a0);

        VEC_T hint_is_zero = VEC_CMPEQ_32(vh, l_zero);
        VEC_T a0_gt_q      = VEC_CMPGT_32(a0, l_q);
        VEC_T plus1  = VEC_AND(VEC_ADD32(a1, l_one), l_0xf);
        VEC_T minus1 = VEC_AND(VEC_SUB32(a1, l_one), l_0xf);
        VEC_T hint_result = VEC_BLENDV_32(a0_gt_q, plus1, minus1);
        VEC_T result = VEC_BLENDV_32(hint_is_zero, a1, hint_result);
        VEC_STORE(a_r + i, result);
    }
}

{{TARGET_ATTR}}
static inline int s_poly_chknorm_vec(const int32_t *a_coeffs, int32_t a_bound)
{
    const VEC_T l_half  = VEC_SET1_32((DIL_Q - 1) / 2);
    const VEC_T l_bm1   = VEC_SET1_32(a_bound - 1);
    for (unsigned i = 0; i < DIL_N; i += VEC_LANES) {
        VEC_T v = VEC_LOAD(a_coeffs + i);
        VEC_T t = VEC_SUB32(l_half, v);
        VEC_T m = VEC_SRAI32(t, 31);
        t = VEC_XOR(t, m);
        t = VEC_SUB32(l_half, t);
        if (VEC_ANY_TRUE_32(VEC_CMPGT_32(t, l_bm1)))
            return 1;
    }
    return 0;
}
