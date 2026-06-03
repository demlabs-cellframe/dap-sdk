/* AVX-512 VBMI2 rejection sampling for ML-KEM gen_matrix.
 * Replaces AVX2 LUT-based compress-store with native vpcompressw
 * (1 μop on Intel Ice Lake+, microcoded on AMD Zen4/5).
 * Tune rules should cap MLKEM_REJ at AVX2 on AMD Zen4/5.
 * Included by dap_mlkem_rej_uniform_simd.c.tpl via SAMPLE_BODY.
 * Expects: immintrin.h, MLKEM_Q defined.
 */

__attribute__((target("avx2,avx512bw,avx512vl,avx512vbmi2,popcnt")))
static unsigned s_rej_uniform_impl(int16_t * restrict a_r, unsigned a_len,
                                    const uint8_t *a_buf, unsigned a_buflen)
{
    unsigned l_ctr = 0, l_pos = 0;
    const __m256i l_bound = _mm256_set1_epi16((int16_t)MLKEM_Q);
    const __m256i l_mask  = _mm256_set1_epi16(0xFFF);
    const __m256i l_idx8  = _mm256_set_epi8(
        15,14,14,13,12,11,11,10, 9, 8, 8, 7, 6, 5, 5, 4,
        11,10,10, 9, 8, 7, 7, 6, 5, 4, 4, 3, 2, 1, 1, 0);

    while (l_ctr + 32 <= a_len && l_pos + 48 <= a_buflen) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)&a_buf[l_pos]);
        __m256i f1 = _mm256_loadu_si256((const __m256i *)&a_buf[l_pos + 24]);
        f0 = _mm256_permute4x64_epi64(f0, 0x94);
        f1 = _mm256_permute4x64_epi64(f1, 0x94);
        f0 = _mm256_shuffle_epi8(f0, l_idx8);
        f1 = _mm256_shuffle_epi8(f1, l_idx8);
        __m256i g0 = _mm256_srli_epi16(f0, 4);
        __m256i g1 = _mm256_srli_epi16(f1, 4);
        f0 = _mm256_blend_epi16(f0, g0, 0xAA);
        f1 = _mm256_blend_epi16(f1, g1, 0xAA);
        f0 = _mm256_and_si256(f0, l_mask);
        f1 = _mm256_and_si256(f1, l_mask);
        l_pos += 48;

        __mmask16 m0 = _mm256_cmp_epi16_mask(f0, l_bound, _MM_CMPINT_LT);
        __mmask16 m1 = _mm256_cmp_epi16_mask(f1, l_bound, _MM_CMPINT_LT);

        _mm256_mask_compressstoreu_epi16(&a_r[l_ctr], m0, f0);
        l_ctr += _mm_popcnt_u32((uint32_t)m0);
        _mm256_mask_compressstoreu_epi16(&a_r[l_ctr], m1, f1);
        l_ctr += _mm_popcnt_u32((uint32_t)m1);
    }

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
