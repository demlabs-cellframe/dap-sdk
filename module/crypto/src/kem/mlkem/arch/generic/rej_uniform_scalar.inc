/* Scalar rejection sampling for ML-KEM gen_matrix.
 * Extracts 2 candidates (12-bit) per 3 bytes.
 * Included by dap_mlkem_rej_uniform_simd.c.tpl via SAMPLE_BODY.
 * Expects: MLKEM_Q defined.
 */

static unsigned s_rej_uniform_impl(int16_t *a_r, unsigned a_len,
                                    const uint8_t *a_buf, unsigned a_buflen)
{
    unsigned l_ctr = 0, l_pos = 0;
    while (l_ctr < a_len && l_pos + 3 <= a_buflen) {
        uint16_t val0 = ((a_buf[l_pos] >> 0) | ((uint16_t)a_buf[l_pos + 1] << 8)) & 0xFFF;
        uint16_t val1 = ((a_buf[l_pos + 1] >> 4) | ((uint16_t)a_buf[l_pos + 2] << 4)) & 0xFFF;
        l_pos += 3;
        if (val0 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val0;
        if (l_ctr < a_len && val1 < MLKEM_Q)
            a_r[l_ctr++] = (int16_t)val1;
    }
    return l_ctr;
}
