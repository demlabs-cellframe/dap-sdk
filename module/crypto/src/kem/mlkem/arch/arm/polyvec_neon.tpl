{{! ================================================================ }}
{{! ARM NEON implementations of ML-KEM polyvec operations            }}
{{! Included by dap_mlkem_polyvec_simd.c.tpl via IMPL_FILE           }}
{{! Full NEON intrinsics — 128-bit (8 x int16) per iteration          }}
{{! ================================================================ }}

/* Deferred-accumulator Montgomery (int32 → int16): match dap_mlkem_montgomery_reduce. */
static inline int16_t s_mlkem_montgomery_from_acc(int32_t a)
{
    int16_t u = (int16_t)((uint32_t)a * (uint32_t)(uint16_t)MLKEM_QINV);
    return (int16_t)((a - (int32_t)u * MLKEM_Q) >> 16);
}

static inline int16x8_t s_neon_mulhi_s16(int16x8_t a, int16x8_t b) {
    int32x4_t p_lo = vmull_s16(vget_low_s16(a), vget_low_s16(b));
    int32x4_t p_hi = vmull_s16(vget_high_s16(a), vget_high_s16(b));
    return vcombine_s16(vshrn_n_s32(p_lo, 16), vshrn_n_s32(p_hi, 16));
}

/* ============================================================================
 * polyvec_compress_d11: one polynomial → 352 bytes (11 bits per coeff)
 *
 * round(x * 2048 / q) & 2047 via Barrett, then pack 8 × 11-bit → 11 bytes.
 * NEON: vectorize Barrett, byte-pack in groups of 8 coefficients.
 * ============================================================================ */

void dap_mlkem_polyvec_compress_d11_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const int16x8_t l_v = vdupq_n_s16(20159);
    const int16x8_t l_v8 = vshlq_n_s16(l_v, 3);
    const int16x8_t l_off = vdupq_n_s16(36);
    const int16x8_t l_shift1 = vdupq_n_s16(1 << 13);
    const int16x8_t l_mask11 = vdupq_n_s16(2047);

    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16x8_t f = vld1q_s16(a_coeffs + 8 * i);

        int16x8_t f1 = vmulq_s16(f, l_v8);
        int16x8_t f2 = vaddq_s16(f, l_off);
        int16x8_t fh = s_neon_mulhi_s16(vshlq_n_s16(f, 3), l_v);
        f2 = vsubq_s16(f1, f2);
        f1 = vandq_s16(vmvnq_s16(f1), f2);
        uint16x8_t f1u = vshrq_n_u16(vreinterpretq_u16_s16(f1), 15);
        fh = vsubq_s16(fh, vreinterpretq_s16_u16(f1u));
        fh = vqrdmulhq_s16(fh, l_shift1);
        fh = vandq_s16(fh, l_mask11);

        uint16_t c[8];
        vst1q_u16(c, vreinterpretq_u16_s16(fh));

        a_r[ 0] = (uint8_t)(c[0]);
        a_r[ 1] = (uint8_t)((c[0] >> 8) | (c[1] << 3));
        a_r[ 2] = (uint8_t)((c[1] >> 5) | (c[2] << 6));
        a_r[ 3] = (uint8_t)(c[2] >> 2);
        a_r[ 4] = (uint8_t)((c[2] >> 10) | (c[3] << 1));
        a_r[ 5] = (uint8_t)((c[3] >> 7) | (c[4] << 4));
        a_r[ 6] = (uint8_t)((c[4] >> 4) | (c[5] << 7));
        a_r[ 7] = (uint8_t)(c[5] >> 1);
        a_r[ 8] = (uint8_t)((c[5] >> 9) | (c[6] << 2));
        a_r[ 9] = (uint8_t)((c[6] >> 6) | (c[7] << 5));
        a_r[10] = (uint8_t)(c[7] >> 3);
        a_r += 11;
    }
}

/* ============================================================================
 * polyvec_compress_d10: one polynomial → 320 bytes (10 bits per coeff)
 *
 * round(x * 1024 / q) & 1023 via Barrett, then pack 4 × 10-bit → 5 bytes.
 * ============================================================================ */

void dap_mlkem_polyvec_compress_d10_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const int16x8_t l_v = vdupq_n_s16(20159);
    const int16x8_t l_v8 = vshlq_n_s16(l_v, 3);
    const int16x8_t l_off = vdupq_n_s16(15);
    const int16x8_t l_shift1 = vdupq_n_s16(1 << 12);
    const int16x8_t l_mask10 = vdupq_n_s16(1023);

    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16x8_t f = vld1q_s16(a_coeffs + 8 * i);

        int16x8_t f1 = vmulq_s16(f, l_v8);
        int16x8_t f2 = vaddq_s16(f, l_off);
        int16x8_t fh = s_neon_mulhi_s16(vshlq_n_s16(f, 3), l_v);
        f2 = vsubq_s16(f1, f2);
        f1 = vandq_s16(vmvnq_s16(f1), f2);
        uint16x8_t f1u = vshrq_n_u16(vreinterpretq_u16_s16(f1), 15);
        fh = vsubq_s16(fh, vreinterpretq_s16_u16(f1u));
        fh = vqrdmulhq_s16(fh, l_shift1);
        fh = vandq_s16(fh, l_mask10);

        uint16_t c[8];
        vst1q_u16(c, vreinterpretq_u16_s16(fh));

        a_r[0] = (uint8_t)(c[0]);
        a_r[1] = (uint8_t)((c[0] >> 8) | (c[1] << 2));
        a_r[2] = (uint8_t)((c[1] >> 6) | (c[2] << 4));
        a_r[3] = (uint8_t)((c[2] >> 4) | (c[3] << 6));
        a_r[4] = (uint8_t)(c[3] >> 2);
        a_r[5] = (uint8_t)(c[4]);
        a_r[6] = (uint8_t)((c[4] >> 8) | (c[5] << 2));
        a_r[7] = (uint8_t)((c[5] >> 6) | (c[6] << 4));
        a_r[8] = (uint8_t)((c[6] >> 4) | (c[7] << 6));
        a_r[9] = (uint8_t)(c[7] >> 2);
        a_r += 10;
    }
}

/* ============================================================================
 * polyvec_decompress_d11: 352 bytes → one polynomial
 *
 * decompress(x) = round(x * q / 2048) = ((x & 0x7FF) * q + 1024) >> 11
 * ============================================================================ */

void dap_mlkem_polyvec_decompress_d11_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const int32x4_t l_q32 = vdupq_n_s32(MLKEM_Q);
    const int32x4_t l_half = vdupq_n_s32(1024);

    for (unsigned j = 0; j < MLKEM_N / 8; j++) {
        uint16_t t[8];
        t[0] = (a_a[0])       | ((uint16_t)a_a[1] << 8);
        t[1] = (a_a[1] >> 3)  | ((uint16_t)a_a[2] << 5);
        t[2] = (a_a[2] >> 6)  | ((uint16_t)a_a[3] << 2) | ((uint16_t)a_a[4] << 10);
        t[3] = (a_a[4] >> 1)  | ((uint16_t)a_a[5] << 7);
        t[4] = (a_a[5] >> 4)  | ((uint16_t)a_a[6] << 4);
        t[5] = (a_a[6] >> 7)  | ((uint16_t)a_a[7] << 1) | ((uint16_t)a_a[8] << 9);
        t[6] = (a_a[8] >> 2)  | ((uint16_t)a_a[9] << 6);
        t[7] = (a_a[9] >> 5)  | ((uint16_t)a_a[10] << 3);
        a_a += 11;

        uint16x8_t v = vandq_u16(vld1q_u16(t), vdupq_n_u16(0x7FF));
        uint32x4_t lo = vmull_u16(vget_low_u16(v), vget_low_u16(vdupq_n_u16(MLKEM_Q)));
        uint32x4_t hi = vmull_u16(vget_high_u16(v), vget_high_u16(vdupq_n_u16(MLKEM_Q)));
        lo = vaddq_u32(lo, vreinterpretq_u32_s32(l_half));
        hi = vaddq_u32(hi, vreinterpretq_u32_s32(l_half));
        int16x4_t r_lo = vmovn_s32(vreinterpretq_s32_u32(vshrq_n_u32(lo, 11)));
        int16x4_t r_hi = vmovn_s32(vreinterpretq_s32_u32(vshrq_n_u32(hi, 11)));
        vst1q_s16(a_r + 8 * j, vcombine_s16(r_lo, r_hi));
    }
}

/* ============================================================================
 * polyvec_decompress_d10: 320 bytes → one polynomial
 *
 * decompress(x) = round(x * q / 1024) = ((x & 0x3FF) * q + 512) >> 10
 * ============================================================================ */

void dap_mlkem_polyvec_decompress_d10_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const int32x4_t l_half = vdupq_n_s32(512);

    for (unsigned j = 0; j < MLKEM_N / 4; j++) {
        uint16_t t[4];
        t[0] = (a_a[0])       | ((uint16_t)a_a[1] << 8);
        t[1] = (a_a[1] >> 2)  | ((uint16_t)a_a[2] << 6);
        t[2] = (a_a[2] >> 4)  | ((uint16_t)a_a[3] << 4);
        t[3] = (a_a[3] >> 6)  | ((uint16_t)a_a[4] << 2);
        a_a += 5;

        uint16x4_t v = vand_u16(vld1_u16(t), vdup_n_u16(0x3FF));
        uint32x4_t prod = vmull_u16(v, vdup_n_u16(MLKEM_Q));
        prod = vaddq_u32(prod, vreinterpretq_u32_s32(l_half));
        int16x4_t r = vmovn_s32(vreinterpretq_s32_u32(vshrq_n_u32(prod, 10)));
        vst1_s16(a_r + 4 * j, r);
    }
}

/* ============================================================================
 * basemul_acc_cached: K-way fused basemul + accumulate + deferred Montgomery
 *
 * Accumulates in int32 (via vmlal) then Montgomery-reduces once per block.
 * ============================================================================ */

void dap_mlkem_polyvec_basemul_acc_cached_{{ARCH_LOWER}}(
    int16_t * restrict a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    const int16_t * const *a_caches,
    unsigned a_count)
{
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        for (unsigned l_h = 0; l_h < 2; l_h++) {
            unsigned l_off = 32 * l_p + 8 * l_h;

            int32x4_t l_diag_lo  = vdupq_n_s32(0);
            int32x4_t l_diag_hi  = vdupq_n_s32(0);
            int32x4_t l_cross_lo = vdupq_n_s32(0);
            int32x4_t l_cross_hi = vdupq_n_s32(0);

            for (unsigned k = 0; k < a_count; k++) {
                int16x8_t l_ae = vld1q_s16(a_polys_a[k] + l_off);
                int16x8_t l_ao = vld1q_s16(a_polys_a[k] + l_off + 16);
                int16x8_t l_ce = vld1q_s16(a_caches[k] + l_off);
                int16x8_t l_co = vld1q_s16(a_caches[k] + l_off + 16);
                int16x8_t l_bo = vld1q_s16(a_polys_b[k] + l_off + 16);

                l_diag_lo = vmlal_s16(l_diag_lo, vget_low_s16(l_ae), vget_low_s16(l_ce));
                l_diag_lo = vmlal_s16(l_diag_lo, vget_low_s16(l_ao), vget_low_s16(l_co));
                l_diag_hi = vmlal_s16(l_diag_hi, vget_high_s16(l_ae), vget_high_s16(l_ce));
                l_diag_hi = vmlal_s16(l_diag_hi, vget_high_s16(l_ao), vget_high_s16(l_co));

                l_cross_lo = vmlal_s16(l_cross_lo, vget_low_s16(l_ae), vget_low_s16(l_bo));
                l_cross_lo = vmlal_s16(l_cross_lo, vget_low_s16(l_ao), vget_low_s16(l_ce));
                l_cross_hi = vmlal_s16(l_cross_hi, vget_high_s16(l_ae), vget_high_s16(l_bo));
                l_cross_hi = vmlal_s16(l_cross_hi, vget_high_s16(l_ao), vget_high_s16(l_ce));
            }

            int16_t re_lo_a[4], re_hi_a[4], ro_lo_a[4], ro_hi_a[4];
            for (int li = 0; li < 4; li++) {
                re_lo_a[li]  = s_mlkem_montgomery_from_acc(vgetq_lane_s32(l_diag_lo, li));
                re_hi_a[li]  = s_mlkem_montgomery_from_acc(vgetq_lane_s32(l_diag_hi, li));
                ro_lo_a[li] = s_mlkem_montgomery_from_acc(vgetq_lane_s32(l_cross_lo, li));
                ro_hi_a[li] = s_mlkem_montgomery_from_acc(vgetq_lane_s32(l_cross_hi, li));
            }
            int16x4_t re_lo = vld1_s16(re_lo_a);
            int16x4_t re_hi = vld1_s16(re_hi_a);
            int16x4_t ro_lo = vld1_s16(ro_lo_a);
            int16x4_t ro_hi = vld1_s16(ro_hi_a);

            vst1q_s16(a_r + l_off, vcombine_s16(re_lo, re_hi));
            vst1q_s16(a_r + l_off + 16, vcombine_s16(ro_lo, ro_hi));
        }
    }
}
