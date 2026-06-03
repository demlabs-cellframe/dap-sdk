/* NEON rejection sampling for Dilithium poly_uniform.
 * Extracts 4 candidates per iteration via NEON byte shuffle,
 * then scalar compaction (NEON lacks efficient per-lane compress).
 * Included by dap_dilithium_sample_simd.c.tpl.
 */

static void s_dil_rej_uniform_impl(uint32_t *r, const uint8_t *buf) {
    static const uint8_t s_shuf_tbl[16] = {
        0, 1, 2, 0xFF, 3, 4, 5, 0xFF, 6, 7, 8, 0xFF, 9, 10, 11, 0xFF
    };
    const uint8x16_t shuf = vld1q_u8(s_shuf_tbl);
    const uint32x4_t mask = vdupq_n_u32(0x7FFFFF);
    const uint32x4_t bound = vdupq_n_u32(8380417);

    unsigned ctr = 0, pos = 0;

    while (ctr + 4 <= 256) {
        uint8x16_t raw = vld1q_u8(buf + pos);
        uint32x4_t d = vandq_u32(vreinterpretq_u32_u8(vqtbl1q_u8(raw, shuf)), mask);

        uint32_t vals[4];
        vst1q_u32(vals, d);
        uint32x4_t lt = vcltq_u32(d, bound);
        uint32_t lt_arr[4];
        vst1q_u32(lt_arr, lt);

        for (unsigned k = 0; k < 4; k++) {
            if (lt_arr[k])
                r[ctr++] = vals[k];
        }
        pos += 12;
    }

    while (ctr < 256) {
        uint32_t t = (uint32_t)buf[pos] | ((uint32_t)buf[pos+1] << 8)
                     | ((uint32_t)buf[pos+2] << 16);
        t &= 0x7FFFFF;
        pos += 3;
        if (t < 8380417) r[ctr++] = t;
    }
}
