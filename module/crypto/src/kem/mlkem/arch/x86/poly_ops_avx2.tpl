{{! ================================================================ }}
{{! AVX2 implementations of ML-KEM poly I/O + mulcache              }}
{{! Included by dap_mlkem_poly_ops_simd.c.tpl via IMPL_FILE         }}
{{! Expects: immintrin.h, MLKEM_Q, MLKEM_QINV, MLKEM_N, string.h   }}
{{! ================================================================ }}

/* ============================================================================
 * compress_d4: polynomial → 128 bytes (4 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_compress_d4_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i shift1 = _mm256_set1_epi16(1 << 9);
    const __m256i mask = _mm256_set1_epi16(15);
    const __m256i shift2 = _mm256_set1_epi16((16 << 8) + 1);
    const __m256i permdidx = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

    for (unsigned i = 0; i < MLKEM_N / 64; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i));
        __m256i f1 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 16));
        __m256i f2 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 32));
        __m256i f3 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 64 * i + 48));
        f0 = _mm256_mulhi_epi16(f0, v);
        f1 = _mm256_mulhi_epi16(f1, v);
        f2 = _mm256_mulhi_epi16(f2, v);
        f3 = _mm256_mulhi_epi16(f3, v);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f1 = _mm256_mulhrs_epi16(f1, shift1);
        f2 = _mm256_mulhrs_epi16(f2, shift1);
        f3 = _mm256_mulhrs_epi16(f3, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f1 = _mm256_and_si256(f1, mask);
        f2 = _mm256_and_si256(f2, mask);
        f3 = _mm256_and_si256(f3, mask);
        f0 = _mm256_packus_epi16(f0, f1);
        f2 = _mm256_packus_epi16(f2, f3);
        f0 = _mm256_maddubs_epi16(f0, shift2);
        f2 = _mm256_maddubs_epi16(f2, shift2);
        f0 = _mm256_packus_epi16(f0, f2);
        f0 = _mm256_permutevar8x32_epi32(f0, permdidx);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * i), f0);
    }
}

/* ============================================================================
 * compress_d5: polynomial → 160 bytes (5 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_compress_d5_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i shift1 = _mm256_set1_epi16(1 << 10);
    const __m256i mask = _mm256_set1_epi16(31);
    const __m256i shift2 = _mm256_set1_epi16((32 << 8) + 1);
    const __m256i shift3 = _mm256_set1_epi32((1024 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(12);
    const __m256i shufbidx = _mm256_set_epi8(
        8, -1, -1, -1, -1, -1, 4, 3, 2, 1, 0, -1, 12, 11, 10, 9,
        -1, 12, 11, 10, 9, 8, -1, -1, -1, -1, -1, 4, 3, 2, 1, 0);

    for (unsigned i = 0; i < MLKEM_N / 32; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * i));
        __m256i f1 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 32 * i + 16));
        f0 = _mm256_mulhi_epi16(f0, v);
        f1 = _mm256_mulhi_epi16(f1, v);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f1 = _mm256_mulhrs_epi16(f1, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f1 = _mm256_and_si256(f1, mask);
        f0 = _mm256_packus_epi16(f0, f1);
        f0 = _mm256_maddubs_epi16(f0, shift2);
        f0 = _mm256_madd_epi16(f0, shift3);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f0 = _mm256_srlv_epi64(f0, sllvdidx);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blendv_epi8(t0, t1, _mm256_castsi256_si128(shufbidx));
        _mm_storeu_si128((__m128i *)(a_r + 20 * i), t0);
        memcpy(a_r + 20 * i + 16, &t1, 4);
    }
}

/* ============================================================================
 * decompress_d4: 128 bytes → polynomial (4 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_decompress_d4_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        7,7,7,7, 6,6,6,6, 5,5,5,5, 4,4,4,4,
        3,3,3,3, 2,2,2,2, 1,1,1,1, 0,0,0,0);
    const __m256i mask = _mm256_set1_epi32(0x00F0000F);
    const __m256i shift = _mm256_set1_epi32((128 << 16) + 2048);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m128i t = _mm_loadl_epi64((const __m128i *)(a_a + 8 * i));
        __m256i f = _mm256_broadcastsi128_si256(t);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}

/* ============================================================================
 * decompress_d5: 160 bytes → polynomial (5 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_decompress_d5_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        9,9,9,8, 8,8,8,7, 7,6,6,6, 6,5,5,5,
        4,4,4,3, 3,3,3,2, 2,1,1,1, 1,0,0,0);
    const __m256i mask = _mm256_set_epi16(
        248, 1984, 62, 496, 3968, 124, 992, 31,
        248, 1984, 62, 496, 3968, 124, 992, 31);
    const __m256i shift = _mm256_set_epi16(
        128, 16, 512, 64, 8, 256, 32, 1024,
        128, 16, 512, 64, 8, 256, 32, 1024);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m128i t = _mm_loadl_epi64((const __m128i *)(a_a + 10 * i));
        int16_t ti;
        memcpy(&ti, a_a + 10 * i + 8, 2);
        t = _mm_insert_epi16(t, ti, 4);
        __m256i f = _mm256_broadcastsi128_si256(t);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}

/* ============================================================================
 * tobytes: polynomial → 384 bytes (12 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_tobytes_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v_mix = _mm256_set1_epi32(0x10000001);
    const __m256i v_shuf = _mm256_setr_epi8(
        0,1,2, 4,5,6, 8,9,10, 12,13,14, -1,-1,-1,-1,
        0,1,2, 4,5,6, 8,9,10, 12,13,14, -1,-1,-1,-1);

    for (int i = 0; i < 16; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)&a_coeffs[16 * i]);
        __m256i t = _mm256_madd_epi16(f, v_mix);
        t = _mm256_shuffle_epi8(t, v_shuf);
        uint8_t *out = a_r + 24 * i;
        __m128i lo = _mm256_castsi256_si128(t);
        __m128i hi = _mm256_extracti128_si256(t, 1);
        _mm_storel_epi64((__m128i *)out, lo);
        *(uint32_t *)(out + 8) = (uint32_t)_mm_extract_epi32(lo, 2);
        _mm_storel_epi64((__m128i *)(out + 12), hi);
        *(uint32_t *)(out + 20) = (uint32_t)_mm_extract_epi32(hi, 2);
    }
}

/* ============================================================================
 * frombytes: 384 bytes → polynomial (12 bits per coefficient)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_frombytes_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i v_shuf = _mm256_setr_epi8(
        0,1, 1,2, 3,4, 4,5, 6,7, 7,8, 9,10, 10,11,
        0,1, 1,2, 3,4, 4,5, 6,7, 7,8, 9,10, 10,11);
    const __m256i v_mask = _mm256_set1_epi16(0x0FFF);

    for (int i = 0; i < 15; i++) {
        const uint8_t *p = a_a + 24 * i;
        __m128i lo_raw = _mm_loadu_si128((const __m128i *)p);
        __m128i hi_raw = _mm_loadu_si128((const __m128i *)(p + 12));
        __m256i raw = _mm256_inserti128_si256(_mm256_castsi128_si256(lo_raw), hi_raw, 1);
        __m256i arranged = _mm256_shuffle_epi8(raw, v_shuf);
        __m256i masked  = _mm256_and_si256(arranged, v_mask);
        __m256i shifted = _mm256_srli_epi16(arranged, 4);
        __m256i result  = _mm256_blend_epi16(masked, shifted, 0xAA);
        _mm256_storeu_si256((__m256i *)&a_r[16 * i], result);
    }
    {
        const uint8_t *p = a_a + 24 * 15;
        __m128i lo_raw = _mm_loadu_si128((const __m128i *)p);
        uint8_t tmp[16] __attribute__((aligned(16))) = {0};
        memcpy(tmp, p + 12, 12);
        __m128i hi_raw = _mm_load_si128((const __m128i *)tmp);
        __m256i raw = _mm256_inserti128_si256(_mm256_castsi128_si256(lo_raw), hi_raw, 1);
        __m256i arranged = _mm256_shuffle_epi8(raw, v_shuf);
        __m256i masked  = _mm256_and_si256(arranged, v_mask);
        __m256i shifted = _mm256_srli_epi16(arranged, 4);
        __m256i result  = _mm256_blend_epi16(masked, shifted, 0xAA);
        _mm256_storeu_si256((__m256i *)&a_r[16 * 15], result);
    }
}

/* ============================================================================
 * frommsg: 32-byte message → polynomial (1 bit → (q+1)/2 or 0)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_frommsg_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_msg)
{
    const __m256i v_half_q = _mm256_set1_epi16((MLKEM_Q + 1) / 2);
    const __m128i v_bits = _mm_setr_epi16(1, 2, 4, 8, 16, 32, 64, 128);

    for (int i = 0; i < 16; i++) {
        __m128i b0 = _mm_set1_epi16(a_msg[2 * i]);
        __m128i b1 = _mm_set1_epi16(a_msg[2 * i + 1]);
        b0 = _mm_and_si128(b0, v_bits);
        b1 = _mm_and_si128(b1, v_bits);
        __m256i nz = _mm256_inserti128_si256(_mm256_castsi128_si256(b0), b1, 1);
        __m256i cmp = _mm256_cmpeq_epi16(nz, _mm256_setzero_si256());
        __m256i result = _mm256_andnot_si256(cmp, v_half_q);
        _mm256_storeu_si256((__m256i *)&a_r[16 * i], result);
    }
}

/* ============================================================================
 * tomsg: polynomial → 32-byte message (coefficient range check → bit)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_tomsg_{{ARCH_LOWER}}(uint8_t *a_msg, const int16_t *a_coeffs)
{
    const __m256i v_lo = _mm256_set1_epi16(832);
    const __m256i v_hi = _mm256_set1_epi16(2497);

    for (int i = 0; i < 16; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)&a_coeffs[16 * i]);
        __m256i c_lo = _mm256_cmpgt_epi16(f, v_lo);
        __m256i c_hi = _mm256_cmpgt_epi16(v_hi, f);
        __m256i valid = _mm256_and_si256(c_lo, c_hi);
        __m256i packed = _mm256_packs_epi16(valid, _mm256_setzero_si256());
        packed = _mm256_permute4x64_epi64(packed, 0xD8);
        uint32_t mask = (uint32_t)_mm256_movemask_epi8(packed);
        a_msg[2 * i]     = (uint8_t)(mask & 0xFF);
        a_msg[2 * i + 1] = (uint8_t)((mask >> 8) & 0xFF);
    }
}

/* ============================================================================
 * mulcache_compute: precompute b_odd * zeta for cached basemul
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_poly_mulcache_compute_{{ARCH_LOWER}}(int16_t * restrict a_cache,
                                                      const int16_t * restrict a_b)
{
    const __m256i l_qinv = _mm256_set1_epi16((int16_t)MLKEM_QINV);
    const __m256i l_q    = _mm256_set1_epi16(MLKEM_Q);
    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_be = _mm256_loadu_si256((const __m256i *)(a_b + 32 * l_p));
        __m256i l_bo = _mm256_loadu_si256((const __m256i *)(a_b + 32 * l_p + 16));
        __m256i l_z  = _mm256_load_si256((const __m256i *)(s_basemul_zetas_nttpack + 16 * l_p));
        __m256i lo = _mm256_mullo_epi16(l_bo, l_z);
        __m256i hi = _mm256_mulhi_epi16(l_bo, l_z);
        __m256i u  = _mm256_mullo_epi16(lo, l_qinv);
        __m256i uq = _mm256_mulhi_epi16(u, l_q);
        __m256i l_boz = _mm256_sub_epi16(hi, uq);
        _mm256_storeu_si256((__m256i *)(a_cache + 32 * l_p), l_be);
        _mm256_storeu_si256((__m256i *)(a_cache + 32 * l_p + 16), l_boz);
    }
}
