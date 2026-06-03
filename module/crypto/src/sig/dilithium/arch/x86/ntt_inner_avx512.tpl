/*
 * Dilithium NTT AVX-512 inner-layer primitives (VEC_LANES=16, HVEC_LANES=8).
 *
 * Provides DIL_NTT_FWD_INNER / DIL_NTT_INV_INNER — per-block shuffle-based
 * butterflies for inner layers (len=8/4/2/1 forward, len=1/2/4/8 inverse).
 *
 * Uses AVX-512 intrinsics: _mm512_shuffle_epi32, _mm512_shuffle_i32x4,
 * _mm512_mask_blend_epi32, _mm512_setr_epi32, __mmask16.
 * The enclosing template supplies: s_mont_reduce_mul_vec, s_mont_reduce_mul_hvec,
 * and the VEC_* / HVEC_* abstract primitives.
 *
 * Included by dap_dilithium_ntt_simd.c.tpl as NTT_INNER_FILE — do not compile standalone.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define DIL_HAS_NTT_INNER 1

#define DIL_NTT_FWD_INNER(v, zetas, k_base, blk, qinv_v, q_v) do {           \
    unsigned _k8  = (k_base);                                                  \
    unsigned _k4  = (k_base) + DIL_N / 16;                                     \
    unsigned _k2  = (k_base) + DIL_N / 16 + DIL_N / 8;                         \
    unsigned _k1  = (k_base) + DIL_N / 16 + DIL_N / 8 + DIL_N / 4;             \
    _k8 += (blk); _k4 += 2 * (blk); _k2 += 4 * (blk); _k1 += 8 * (blk);     \
    HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                         \
    HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                      \
    /* len=8: butterfly [0..7] <-> [8..15] via 256-bit halves */               \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _t = s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas)[_k8]),        \
                                            _hi, _hqinv, _hq);                \
        v = VEC_FROM_HALVES(HVEC_ADD32(_lo, _t), HVEC_SUB32(_lo, _t));        \
    }                                                                          \
    /* len=4: butterfly lanes 0<->1, 2<->3 via 128-bit shuffle */             \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(2,2,0,0));         \
        VEC_T _hi = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(3,3,1,1));         \
        VEC_T _zv = VEC_FROM_HALVES(                                           \
            HVEC_SET1_32((zetas)[_k4]),                                        \
            HVEC_SET1_32((zetas)[_k4 + 1]));                                    \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xF0F0,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
    /* len=2: butterfly within each 128-bit lane, stride=2 */                  \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas)[_k2],   (zetas)[_k2],   (zetas)[_k2],   (zetas)[_k2],     \
            (zetas)[_k2+1], (zetas)[_k2+1], (zetas)[_k2+1], (zetas)[_k2+1],   \
            (zetas)[_k2+2], (zetas)[_k2+2], (zetas)[_k2+2], (zetas)[_k2+2],   \
            (zetas)[_k2+3], (zetas)[_k2+3], (zetas)[_k2+3], (zetas)[_k2+3]);  \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xCCCC,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
    /* len=1: butterfly within each pair */                                    \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas)[_k1],   (zetas)[_k1],   (zetas)[_k1+1], (zetas)[_k1+1],   \
            (zetas)[_k1+2], (zetas)[_k1+2], (zetas)[_k1+3], (zetas)[_k1+3],   \
            (zetas)[_k1+4], (zetas)[_k1+4], (zetas)[_k1+5], (zetas)[_k1+5],   \
            (zetas)[_k1+6], (zetas)[_k1+6], (zetas)[_k1+7], (zetas)[_k1+7]);  \
        VEC_T _t = s_mont_reduce_mul_vec(_zv, _hi, (qinv_v), (q_v));          \
        v = _mm512_mask_blend_epi32((__mmask16)0xAAAA,                         \
            VEC_ADD32(_lo, _t), VEC_SUB32(_lo, _t));                           \
    }                                                                          \
} while (0)

#define DIL_NTT_INV_INNER(v, zetas_inv, k_base, blk, qinv_v, q_v) do {        \
    unsigned _k1  = (k_base);                                                  \
    unsigned _k2  = (k_base) + DIL_N / 2;                                      \
    unsigned _k4  = (k_base) + DIL_N / 2 + DIL_N / 4;                          \
    unsigned _k8  = (k_base) + DIL_N / 2 + DIL_N / 4 + DIL_N / 8;              \
    _k1 += 8 * (blk); _k2 += 4 * (blk); _k4 += 2 * (blk); _k8 += (blk);     \
    HVEC_T _hqinv = HVEC_SET1_32((int32_t)DIL_QINV);                         \
    HVEC_T _hq    = HVEC_SET1_32(DIL_Q);                                      \
    /* len=1: GS butterfly within each pair */                                 \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(2,2,0,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,3,1,1));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas_inv)[_k1],   (zetas_inv)[_k1],   (zetas_inv)[_k1+1], (zetas_inv)[_k1+1],\
            (zetas_inv)[_k1+2], (zetas_inv)[_k1+2], (zetas_inv)[_k1+3], (zetas_inv)[_k1+3],\
            (zetas_inv)[_k1+4], (zetas_inv)[_k1+4], (zetas_inv)[_k1+5], (zetas_inv)[_k1+5],\
            (zetas_inv)[_k1+6], (zetas_inv)[_k1+6], (zetas_inv)[_k1+7], (zetas_inv)[_k1+7]);\
        v = _mm512_mask_blend_epi32((__mmask16)0xAAAA, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=2: GS butterfly across 64-bit groups */                             \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_epi32(v, _MM_SHUFFLE(1,0,1,0));            \
        VEC_T _hi = _mm512_shuffle_epi32(v, _MM_SHUFFLE(3,2,3,2));            \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = _mm512_setr_epi32(                                         \
            (zetas_inv)[_k2],   (zetas_inv)[_k2],   (zetas_inv)[_k2],   (zetas_inv)[_k2],\
            (zetas_inv)[_k2+1], (zetas_inv)[_k2+1], (zetas_inv)[_k2+1], (zetas_inv)[_k2+1],\
            (zetas_inv)[_k2+2], (zetas_inv)[_k2+2], (zetas_inv)[_k2+2], (zetas_inv)[_k2+2],\
            (zetas_inv)[_k2+3], (zetas_inv)[_k2+3], (zetas_inv)[_k2+3], (zetas_inv)[_k2+3]);\
        v = _mm512_mask_blend_epi32((__mmask16)0xCCCC, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=4: GS butterfly lanes 0<->1, 2<->3 */                              \
    {                                                                          \
        VEC_T _lo = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(2,2,0,0));         \
        VEC_T _hi = _mm512_shuffle_i32x4(v, v, _MM_SHUFFLE(3,3,1,1));         \
        VEC_T _sum = VEC_ADD32(_lo, _hi);                                      \
        VEC_T _dif = VEC_SUB32(_lo, _hi);                                      \
        VEC_T _zv = VEC_FROM_HALVES(                                           \
            HVEC_SET1_32((zetas_inv)[_k4]),                                    \
            HVEC_SET1_32((zetas_inv)[_k4 + 1]));                                \
        v = _mm512_mask_blend_epi32((__mmask16)0xF0F0, _sum,                   \
            s_mont_reduce_mul_vec(_zv, _dif, (qinv_v), (q_v)));               \
    }                                                                          \
    /* len=8: GS butterfly [0..7] <-> [8..15] */                               \
    {                                                                          \
        HVEC_T _lo = VEC_LO_HALF(v), _hi = VEC_HI_HALF(v);                   \
        HVEC_T _sum = HVEC_ADD32(_lo, _hi);                                    \
        HVEC_T _dif = HVEC_SUB32(_lo, _hi);                                    \
        v = VEC_FROM_HALVES(_sum,                                              \
            s_mont_reduce_mul_hvec(HVEC_SET1_32((zetas_inv)[_k8]),             \
                                   _dif, _hqinv, _hq));                        \
    }                                                                          \
} while (0)
