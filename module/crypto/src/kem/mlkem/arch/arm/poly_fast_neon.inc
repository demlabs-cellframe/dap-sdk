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
    const int16x8_t q  = vdupq_n_s16(3329);
    for (unsigned i = 0; i < 256; i += 8) {
        int16x8_t v = vld1q_s16(a + i);
        int16x4_t v_lo = vget_low_s16(v), v_hi = vget_high_s16(v);
        int16x4_t b_lo = vget_low_s16(bv), b_hi = vget_high_s16(bv);
        int16x8_t t = vcombine_s16(vshrn_n_s32(vmull_s16(v_lo, b_lo), 16),
                                    vshrn_n_s32(vmull_s16(v_hi, b_hi), 16));
        t = vshrq_n_s16(t, 10);
        v = vsubq_s16(v, vmulq_s16(t, q));
        int16x8_t sign = vshrq_n_s16(v, 15);
        v = vaddq_s16(v, vandq_s16(sign, q));
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

static void s_mlkem_cbd2_neon(int16_t *r, const uint8_t *buf)
{
    const uint8x16_t mask55 = vdupq_n_u8(0x55);
    const uint8x16_t mask33 = vdupq_n_u8(0x33);
    const uint8x16_t mask03 = vdupq_n_u8(0x03);
    const uint8x16_t mask0F = vdupq_n_u8(0x0F);

    for (unsigned i = 0; i < 256 / 32; i++) {
        uint8x16_t f0 = vld1q_u8(buf + 16 * i);

        uint8x16_t f1 = vshrq_n_u8(f0, 1);
        f0 = vandq_u8(f0, mask55);
        f1 = vandq_u8(f1, mask55);
        f0 = vaddq_u8(f0, f1);

        f1 = vshrq_n_u8(f0, 2);
        f0 = vandq_u8(f0, mask33);
        f1 = vandq_u8(f1, mask33);
        f0 = vaddq_u8(f0, mask33);
        f0 = vsubq_u8(f0, f1);

        f1 = vshrq_n_u8(f0, 4);
        f0 = vandq_u8(f0, mask0F);
        f1 = vandq_u8(f1, mask0F);
        int8x16_t s0 = vsubq_s8(vreinterpretq_s8_u8(f0), vreinterpretq_s8_u8(mask03));
        int8x16_t s1 = vsubq_s8(vreinterpretq_s8_u8(f1), vreinterpretq_s8_u8(mask03));

        int8x16_t lo = vzip1q_s8(s0, s1);
        int8x16_t hi = vzip2q_s8(s0, s1);

        vst1q_s16(r + 32 * i +  0, vmovl_s8(vget_low_s8(lo)));
        vst1q_s16(r + 32 * i +  8, vmovl_s8(vget_high_s8(lo)));
        vst1q_s16(r + 32 * i + 16, vmovl_s8(vget_low_s8(hi)));
        vst1q_s16(r + 32 * i + 24, vmovl_s8(vget_high_s8(hi)));
    }
}

static void s_mlkem_cbd3_neon(int16_t *r, const uint8_t *buf)
{
    for (unsigned i = 0; i < 256 / 4; i++) {
        uint32_t t = (uint32_t)buf[3*i] | ((uint32_t)buf[3*i+1] << 8) | ((uint32_t)buf[3*i+2] << 16);
        uint32_t d = (t & 0x00249249) + ((t >> 1) & 0x00249249) + ((t >> 2) & 0x00249249);
        for (unsigned j = 0; j < 4; j++) {
            int16_t a = (int16_t)((d >> (6 * j))     & 0x7);
            int16_t b = (int16_t)((d >> (6 * j + 3)) & 0x7);
            r[4 * i + j] = a - b;
        }
    }
}
