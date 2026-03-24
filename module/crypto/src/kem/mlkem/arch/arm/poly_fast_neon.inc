/* NEON static fast-function bodies for ML-KEM poly ops.
 * Included by generated dap_mlkem_poly_simd.h inside AArch64 section. */

static void s_mlkem_poly_csubq_neon(int16_t *a)
{
    const int16x8_t q = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        v = vsubq_s16(v, q);
        int16x8_t mask = vshrq_n_s16(v, 15);
        v = vaddq_s16(v, vandq_s16(mask, q));
        vst1q_s16(a + i, v);
    }
}

static void s_mlkem_poly_reduce_neon(int16_t *a)
{
    const int16x8_t bv = vdupq_n_s16(20159);
    const int16x8_t q = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        int16x4_t v_lo = vget_low_s16(v), v_hi = vget_high_s16(v);
        int16x4_t b_lo = vget_low_s16(bv), b_hi = vget_high_s16(bv);
        int16x8_t t = vcombine_s16(vshrn_n_s32(vmull_s16(v_lo, b_lo), 16),
                                    vshrn_n_s32(vmull_s16(v_hi, b_hi), 16));
        t = vshrq_n_s16(t, 10);
        v = vsubq_s16(v, vmulq_s16(t, q));
        vst1q_s16(a + i, v);
    }
}

static void s_mlkem_poly_tomont_neon(int16_t *a)
{
    const int16x8_t f    = vdupq_n_s16((int16_t)((1ULL << 32) % 3329));
    const int16x8_t qinv = vdupq_n_s16((int16_t)-3327);
    const int16x8_t q    = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        int16x8_t lo = vmulq_s16(v, f);
        int16x4_t vl = vget_low_s16(v), vh = vget_high_s16(v);
        int16x4_t fl = vget_low_s16(f), fh = vget_high_s16(f);
        int16x8_t hi = vcombine_s16(vshrn_n_s32(vmull_s16(vl, fl), 16),
                                     vshrn_n_s32(vmull_s16(vh, fh), 16));
        int16x8_t u = vmulq_s16(lo, qinv);
        int16x4_t ul = vget_low_s16(u), uh = vget_high_s16(u);
        int16x4_t ql = vget_low_s16(q), qh = vget_high_s16(q);
        int16x8_t uq = vcombine_s16(vshrn_n_s32(vmull_s16(ul, ql), 16),
                                      vshrn_n_s32(vmull_s16(uh, qh), 16));
        vst1q_s16(a + i, vsubq_s16(hi, uq));
    }
}

static void s_mlkem_poly_add_neon(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t va = vld1q_s16(a + i);
        int16x8_t vb = vld1q_s16(b + i);
        vst1q_s16(r + i, vaddq_s16(va, vb));
    }
}

static void s_mlkem_poly_sub_neon(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t va = vld1q_s16(a + i);
        int16x8_t vb = vld1q_s16(b + i);
        vst1q_s16(r + i, vsubq_s16(va, vb));
    }
}
