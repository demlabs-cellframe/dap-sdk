{{! ================================================================ }}
{{! ARM NEON implementations of ML-KEM poly I/O + mulcache           }}
{{! Included by dap_mlkem_poly_ops_simd.c.tpl via IMPL_FILE          }}
{{! Full NEON intrinsics — 128-bit (8 x int16) per iteration         }}
{{! ================================================================ }}

static inline int16x8_t s_neon_mulhi_s16(int16x8_t a, int16x8_t b) {
    int32x4_t p_lo = vmull_s16(vget_low_s16(a), vget_low_s16(b));
    int32x4_t p_hi = vmull_s16(vget_high_s16(a), vget_high_s16(b));
    return vcombine_s16(vshrn_n_s32(p_lo, 16), vshrn_n_s32(p_hi, 16));
}

/* vaddvq_u16 is AArch64-only; ARMv7 needs pairwise reduction. */
static inline uint16_t s_neon_vaddvq_u16(uint16x8_t a_v) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return vaddvq_u16(a_v);
#else
    uint16x4_t a_lo = vget_low_u16(a_v);
    uint16x4_t a_hi = vget_high_u16(a_v);
    a_lo = vpadd_u16(a_lo, a_hi);
    a_lo = vpadd_u16(a_lo, a_lo);
    a_lo = vpadd_u16(a_lo, a_lo);
    return vget_lane_u16(a_lo, 0);
#endif
}

/* ============================================================================
 * compress_d4: polynomial → 128 bytes (4 bits per coefficient)
 *
 * round(x * 16 / q) & 15 via Barrett: mulhi(x,20159) → mulhrs(·,512) → &15
 * Then pack pairs of nibbles into bytes.
 * ============================================================================ */

void dap_mlkem_poly_compress_d4_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const int16x8_t l_v = vdupq_n_s16(20159);
    const int16x8_t l_shift = vdupq_n_s16(1 << 9);
    const int16x8_t l_mask = vdupq_n_s16(15);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        int16x8_t f0 = vld1q_s16(a_coeffs + 16 * i);
        int16x8_t f1 = vld1q_s16(a_coeffs + 16 * i + 8);

        f0 = s_neon_mulhi_s16(f0, l_v);
        f1 = s_neon_mulhi_s16(f1, l_v);
        f0 = vqrdmulhq_s16(f0, l_shift);
        f1 = vqrdmulhq_s16(f1, l_shift);
        f0 = vandq_s16(f0, l_mask);
        f1 = vandq_s16(f1, l_mask);

        uint8x8_t n0 = vmovn_u16(vreinterpretq_u16_s16(f0));
        uint8x8_t n1 = vmovn_u16(vreinterpretq_u16_s16(f1));
        uint8x16_t wide = vcombine_u8(n0, n1);

        uint8x16x2_t de = vuzpq_u8(wide, wide);
        uint8x8_t lo_nib = vget_low_u8(de.val[0]);
        uint8x8_t hi_nib = vshl_n_u8(vget_low_u8(de.val[1]), 4);
        vst1_u8(a_r + 8 * i, vorr_u8(lo_nib, hi_nib));
    }
}

/* ============================================================================
 * compress_d5: polynomial → 160 bytes (5 bits per coefficient)
 *
 * round(x * 32 / q) & 31 via Barrett: mulhi(x,20159) → mulhrs(·,1024) → &31
 * Then pack 8 x 5-bit → 5 bytes.
 * ============================================================================ */

void dap_mlkem_poly_compress_d5_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const int16x8_t l_v = vdupq_n_s16(20159);
    const int16x8_t l_shift = vdupq_n_s16(1 << 10);
    const int16x8_t l_mask = vdupq_n_s16(31);

    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16x8_t f = vld1q_s16(a_coeffs + 8 * i);
        f = s_neon_mulhi_s16(f, l_v);
        f = vqrdmulhq_s16(f, l_shift);
        f = vandq_s16(f, l_mask);

        uint16_t c[8];
        vst1q_u16(c, vreinterpretq_u16_s16(f));

        a_r[0] = (uint8_t)( c[0]       | (c[1] << 5));
        a_r[1] = (uint8_t)((c[1] >> 3) | (c[2] << 2) | (c[3] << 7));
        a_r[2] = (uint8_t)((c[3] >> 1) | (c[4] << 4));
        a_r[3] = (uint8_t)((c[4] >> 4) | (c[5] << 1) | (c[6] << 6));
        a_r[4] = (uint8_t)((c[6] >> 2) | (c[7] << 3));
        a_r += 5;
    }
}

/* ============================================================================
 * decompress_d4: 128 bytes → polynomial
 *
 * decompress(x) = round(x * q / 16) = (x * q + 8) >> 4
 * ============================================================================ */

void dap_mlkem_poly_decompress_d4_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const int16x8_t l_q = vdupq_n_s16(MLKEM_Q);
    const int16x8_t l_half = vdupq_n_s16(8);

    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        uint8x8_t raw = vld1_u8(a_a + 4 * i);
        uint8x8_t lo_nib = vand_u8(raw, vdup_n_u8(0x0F));
        uint8x8_t hi_nib = vshr_n_u8(raw, 4);
        uint8x8x2_t zipped = vzip_u8(lo_nib, hi_nib);
        uint16x8_t vals = vmovl_u8(zipped.val[0]);
        int16x8_t v = vreinterpretq_s16_u16(vals);
        v = vaddq_s16(vmulq_s16(v, l_q), l_half);
        v = vshrq_n_s16(v, 4);
        vst1q_s16(a_r + 8 * i, v);
    }
}

/* ============================================================================
 * decompress_d5: 160 bytes → polynomial
 *
 * decompress(x) = round(x * q / 32) = ((x & 31) * q + 16) >> 5
 * ============================================================================ */

void dap_mlkem_poly_decompress_d5_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        uint16_t t[8];
        t[0] =  a_a[0]       & 31;
        t[1] = (a_a[0] >> 5) | ((a_a[1] << 3) & 31);
        t[2] = (a_a[1] >> 2) & 31;
        t[3] = (a_a[1] >> 7) | ((a_a[2] << 1) & 31);
        t[4] = (a_a[2] >> 4) | ((a_a[3] << 4) & 31);
        t[5] = (a_a[3] >> 1) & 31;
        t[6] = (a_a[3] >> 6) | ((a_a[4] << 2) & 31);
        t[7] = (a_a[4] >> 3) & 31;
        a_a += 5;

        uint16x8_t v = vld1q_u16(t);
        int16x8_t vq = vmulq_s16(vreinterpretq_s16_u16(v), vdupq_n_s16(MLKEM_Q));
        vq = vaddq_s16(vq, vdupq_n_s16(16));
        vq = vshrq_n_s16(vq, 5);
        vst1q_s16(a_r + 8 * i, vq);
    }
}

/* ============================================================================
 * tobytes: polynomial → 384 bytes (12 bits per coefficient)
 *
 * Pack pairs of 12-bit values into 3 bytes using NEON widening/narrowing.
 * ============================================================================ */

void dap_mlkem_poly_tobytes_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        int16x8_t f = vld1q_s16(a_coeffs + 8 * i);
        uint16x8_t u = vreinterpretq_u16_s16(f);

        uint16x4_t lo = vget_low_u16(u);
        uint16x4_t hi = vget_high_u16(u);

        uint8_t *p = a_r + 12 * i;
        uint16_t c[8];
        vst1q_u16(c, u);

        p[ 0] = (uint8_t)(c[0]);
        p[ 1] = (uint8_t)((c[0] >> 8) | (c[1] << 4));
        p[ 2] = (uint8_t)(c[1] >> 4);
        p[ 3] = (uint8_t)(c[2]);
        p[ 4] = (uint8_t)((c[2] >> 8) | (c[3] << 4));
        p[ 5] = (uint8_t)(c[3] >> 4);
        p[ 6] = (uint8_t)(c[4]);
        p[ 7] = (uint8_t)((c[4] >> 8) | (c[5] << 4));
        p[ 8] = (uint8_t)(c[5] >> 4);
        p[ 9] = (uint8_t)(c[6]);
        p[10] = (uint8_t)((c[6] >> 8) | (c[7] << 4));
        p[11] = (uint8_t)(c[7] >> 4);
    }
}

/* ============================================================================
 * frombytes: 384 bytes → polynomial (12 bits per coefficient)
 *
 * Unpack 3 bytes → 2 × 12-bit coefficients, vectorize with NEON.
 * ============================================================================ */

void dap_mlkem_poly_frombytes_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    for (unsigned i = 0; i < MLKEM_N / 8; i++) {
        const uint8_t *p = a_a + 12 * i;
        uint16_t c[8];
        c[0] =  (uint16_t)p[ 0]       | ((uint16_t)p[ 1] << 8);
        c[1] = ((uint16_t)p[ 1] >> 4) | ((uint16_t)p[ 2] << 4);
        c[2] =  (uint16_t)p[ 3]       | ((uint16_t)p[ 4] << 8);
        c[3] = ((uint16_t)p[ 4] >> 4) | ((uint16_t)p[ 5] << 4);
        c[4] =  (uint16_t)p[ 6]       | ((uint16_t)p[ 7] << 8);
        c[5] = ((uint16_t)p[ 7] >> 4) | ((uint16_t)p[ 8] << 4);
        c[6] =  (uint16_t)p[ 9]       | ((uint16_t)p[10] << 8);
        c[7] = ((uint16_t)p[10] >> 4) | ((uint16_t)p[11] << 4);

        uint16x8_t v = vandq_u16(vld1q_u16(c), vdupq_n_u16(0x0FFF));
        vst1q_s16(a_r + 8 * i, vreinterpretq_s16_u16(v));
    }
}

/* ============================================================================
 * frommsg: 32-byte message → polynomial (1 bit → (q+1)/2 or 0)
 *
 * For each bit in msg: if set → (q+1)/2, else → 0.
 * Vectorized: broadcast byte, AND with bit masks, compare != 0.
 * ============================================================================ */

void dap_mlkem_poly_frommsg_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_msg)
{
    const int16x8_t l_half_q = vdupq_n_s16((MLKEM_Q + 1) / 2);
    static const int16_t s_bits[8] = {1, 2, 4, 8, 16, 32, 64, 128};
    const int16x8_t l_bits = vld1q_s16(s_bits);

    for (unsigned i = 0; i < 32; i++) {
        int16x8_t byte_broadcast = vdupq_n_s16((int16_t)a_msg[i]);
        int16x8_t masked = vandq_s16(byte_broadcast, l_bits);
        uint16x8_t nonzero = vmvnq_u16(vceqq_s16(masked, vdupq_n_s16(0)));
        int16x8_t result = vandq_s16(vreinterpretq_s16_u16(nonzero), l_half_q);
        vst1q_s16(a_r + 8 * i, result);
    }
}

/* ============================================================================
 * tomsg: polynomial → 32-byte message (coefficient in [833,2496] → bit 1)
 *
 * Threshold: if coeff > 832 && coeff < 2497 → 1.
 * Vectorized: compare, pack bits.
 * ============================================================================ */

void dap_mlkem_poly_tomsg_{{ARCH_LOWER}}(uint8_t *a_msg, const int16_t *a_coeffs)
{
    const int16x8_t l_lo = vdupq_n_s16(832);
    const int16x8_t l_hi = vdupq_n_s16(2497);

    for (unsigned i = 0; i < 32; i++) {
        int16x8_t f = vld1q_s16(a_coeffs + 8 * i);
        uint16x8_t gt_lo = vcgtq_s16(f, l_lo);
        uint16x8_t lt_hi = vcltq_s16(f, l_hi);
        uint16x8_t valid = vandq_u16(gt_lo, lt_hi);

        uint16x8_t shifted = vshrq_n_u16(valid, 15);
        static const uint16_t s_weights[8] = {1, 2, 4, 8, 16, 32, 64, 128};
        uint16x8_t weighted = vmulq_u16(shifted, vld1q_u16(s_weights));
        a_msg[i] = (uint8_t)s_neon_vaddvq_u16(weighted);
    }
}

/* ============================================================================
 * mulcache_compute: precompute b_odd * zeta for cached basemul
 *
 * cache_even[j] = b_even[j]
 * cache_odd[j]  = montgomery_reduce(b_odd[j] * zeta[j])
 * ============================================================================ */

void dap_mlkem_poly_mulcache_compute_{{ARCH_LOWER}}(int16_t * restrict a_cache,
                                                      const int16_t * restrict a_b)
{
    const int16x8_t l_qinv = vdupq_n_s16((int16_t)MLKEM_QINV);
    const int16x8_t l_q    = vdupq_n_s16(MLKEM_Q);

    for (unsigned l_p = 0; l_p < 8; l_p++) {
        int16x8_t l_be = vld1q_s16(a_b + 32 * l_p);
        int16x8_t l_bo = vld1q_s16(a_b + 32 * l_p + 8);
        int16x8_t l_z  = vld1q_s16(s_basemul_zetas_nttpack + 8 * l_p);

        int16x8_t lo = vmulq_s16(l_bo, l_z);
        int16x8_t hi = s_neon_mulhi_s16(l_bo, l_z);
        int16x8_t u  = vmulq_s16(lo, l_qinv);
        int16x8_t uq = s_neon_mulhi_s16(u, l_q);
        int16x8_t l_boz = vsubq_s16(hi, uq);

        vst1q_s16(a_cache + 32 * l_p, l_be);
        vst1q_s16(a_cache + 32 * l_p + 8, l_boz);

        l_be = vld1q_s16(a_b + 32 * l_p + 16);
        l_bo = vld1q_s16(a_b + 32 * l_p + 24);
        l_z  = vld1q_s16(s_basemul_zetas_nttpack + 8 * l_p + 8);

        lo = vmulq_s16(l_bo, l_z);
        hi = s_neon_mulhi_s16(l_bo, l_z);
        u  = vmulq_s16(lo, l_qinv);
        uq = s_neon_mulhi_s16(u, l_q);
        l_boz = vsubq_s16(hi, uq);

        vst1q_s16(a_cache + 32 * l_p + 16, l_be);
        vst1q_s16(a_cache + 32 * l_p + 24, l_boz);
    }
}
