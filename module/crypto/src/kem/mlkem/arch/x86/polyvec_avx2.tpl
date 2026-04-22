{{! ================================================================ }}
{{! AVX2 implementations of ML-KEM polyvec compress/decompress      }}
{{! + basemul_acc_cached with deferred Montgomery.                   }}
{{! Included by dap_mlkem_polyvec_simd.c.tpl via IMPL_FILE          }}
{{! Expects: immintrin.h, MLKEM_Q, MLKEM_QINV, MLKEM_N, string.h   }}
{{! ================================================================ }}

/* ============================================================================
 * polyvec_compress_d11: one polynomial → 352 bytes (11 bits per coeff)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_polyvec_compress_d11_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i v8 = _mm256_slli_epi16(v, 3);
    const __m256i off = _mm256_set1_epi16(36);
    const __m256i shift1 = _mm256_set1_epi16(1 << 13);
    const __m256i mask = _mm256_set1_epi16(2047);
    const __m256i shift2 = _mm256_set1_epi64x((2048LL << 48) + (1LL << 32) + (2048 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(10);
    const __m256i srlvqidx = _mm256_set_epi64x(30, 10, 30, 10);
    const __m256i shufbidx = _mm256_set_epi8(
        4,3,2,1,0, 0,-1,-1,-1,-1, 10,9,8,7,6,5,
        -1,-1,-1,-1,-1, 10,9,8,7,6, 5,4,3,2,1,0);

    for (unsigned i = 0; i < MLKEM_N / 16 - 1; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 16 * i));
        __m256i f1 = _mm256_mullo_epi16(f0, v8);
        __m256i f2 = _mm256_add_epi16(f0, off);
        f0 = _mm256_slli_epi16(f0, 3);
        f0 = _mm256_mulhi_epi16(f0, v);
        f2 = _mm256_sub_epi16(f1, f2);
        f1 = _mm256_andnot_si256(f1, f2);
        f1 = _mm256_srli_epi16(f1, 15);
        f0 = _mm256_sub_epi16(f0, f1);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f0 = _mm256_madd_epi16(f0, shift2);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f1 = _mm256_bsrli_epi128(f0, 8);
        f0 = _mm256_srlv_epi64(f0, srlvqidx);
        f1 = _mm256_slli_epi64(f1, 34);
        f0 = _mm256_add_epi64(f0, f1);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blendv_epi8(t0, t1, _mm256_castsi256_si128(shufbidx));
        _mm_storeu_si128((__m128i *)(a_r + 22 * i), t0);
        _mm_storel_epi64((__m128i *)(a_r + 22 * i + 16), t1);
    }
    {
        const unsigned i = MLKEM_N / 16 - 1;
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 16 * i));
        __m256i f1 = _mm256_mullo_epi16(f0, v8);
        __m256i f2 = _mm256_add_epi16(f0, off);
        f0 = _mm256_slli_epi16(f0, 3);
        f0 = _mm256_mulhi_epi16(f0, v);
        f2 = _mm256_sub_epi16(f1, f2);
        f1 = _mm256_andnot_si256(f1, f2);
        f1 = _mm256_srli_epi16(f1, 15);
        f0 = _mm256_sub_epi16(f0, f1);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f0 = _mm256_madd_epi16(f0, shift2);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f1 = _mm256_bsrli_epi128(f0, 8);
        f0 = _mm256_srlv_epi64(f0, srlvqidx);
        f1 = _mm256_slli_epi64(f1, 34);
        f0 = _mm256_add_epi64(f0, f1);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blendv_epi8(t0, t1, _mm256_castsi256_si128(shufbidx));
        _mm_storeu_si128((__m128i *)(a_r + 22 * i), t0);
        memcpy(a_r + 22 * i + 16, &t1, 6);
    }
}

/* ============================================================================
 * polyvec_compress_d10: one polynomial → 320 bytes (10 bits per coeff)
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_polyvec_compress_d10_{{ARCH_LOWER}}(uint8_t *a_r, const int16_t *a_coeffs)
{
    const __m256i v = _mm256_set1_epi16(20159);
    const __m256i v8 = _mm256_slli_epi16(v, 3);
    const __m256i off = _mm256_set1_epi16(15);
    const __m256i shift1 = _mm256_set1_epi16(1 << 12);
    const __m256i mask = _mm256_set1_epi16(1023);
    const __m256i shift2 = _mm256_set1_epi64x((1024LL << 48) + (1LL << 32) + (1024 << 16) + 1);
    const __m256i sllvdidx = _mm256_set1_epi64x(12);
    const __m256i shufbidx = _mm256_set_epi8(
        8, 4,3,2,1,0, -1,-1,-1,-1,-1,-1, 12,11,10,9,
        -1,-1,-1,-1,-1,-1, 12,11,10,9,8, 4,3,2,1,0);

    for (unsigned i = 0; i < MLKEM_N / 16; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(a_coeffs + 16 * i));
        __m256i f1 = _mm256_mullo_epi16(f0, v8);
        __m256i f2 = _mm256_add_epi16(f0, off);
        f0 = _mm256_slli_epi16(f0, 3);
        f0 = _mm256_mulhi_epi16(f0, v);
        f2 = _mm256_sub_epi16(f1, f2);
        f1 = _mm256_andnot_si256(f1, f2);
        f1 = _mm256_srli_epi16(f1, 15);
        f0 = _mm256_sub_epi16(f0, f1);
        f0 = _mm256_mulhrs_epi16(f0, shift1);
        f0 = _mm256_and_si256(f0, mask);
        f0 = _mm256_madd_epi16(f0, shift2);
        f0 = _mm256_sllv_epi32(f0, sllvdidx);
        f0 = _mm256_srli_epi64(f0, 12);
        f0 = _mm256_shuffle_epi8(f0, shufbidx);
        __m128i t0 = _mm256_castsi256_si128(f0);
        __m128i t1 = _mm256_extracti128_si256(f0, 1);
        t0 = _mm_blend_epi16(t0, t1, 0xE0);
        _mm_storeu_si128((__m128i *)(a_r + 20 * i), t0);
        memcpy(a_r + 20 * i + 16, &t1, 4);
    }
}

/* ============================================================================
 * polyvec_decompress_d11: 352 bytes → one polynomial
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_polyvec_decompress_d11_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi16(MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        13,12,12,11, 10,9,9,8, 8,7,6,5, 5,4,4,3,
        10,9,9,8, 7,6,6,5, 5,4,3,2, 2,1,1,0);
    const __m256i srlvdidx = _mm256_set_epi32(0, 0, 1, 0, 0, 0, 1, 0);
    const __m256i srlvqidx = _mm256_set_epi64x(2, 0, 2, 0);
    const __m256i shift = _mm256_set_epi16(
        4,32,1,8, 32,1,4,32, 4,32,1,8, 32,1,4,32);
    const __m256i mask = _mm256_set1_epi16(32752);

    for (unsigned i = 0; i < MLKEM_N / 16 - 1; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)(a_a + 22 * i));
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_srlv_epi32(f, srlvdidx);
        f = _mm256_srlv_epi64(f, srlvqidx);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
    {
        const unsigned i = MLKEM_N / 16 - 1;
        __m256i f;
        memcpy(&f, a_a + 22 * i, 22);
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_srlv_epi32(f, srlvdidx);
        f = _mm256_srlv_epi64(f, srlvqidx);
        f = _mm256_mullo_epi16(f, shift);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}

/* ============================================================================
 * polyvec_decompress_d10: 320 bytes → one polynomial
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_polyvec_decompress_d10_{{ARCH_LOWER}}(int16_t *a_r, const uint8_t *a_a)
{
    const __m256i q = _mm256_set1_epi32((MLKEM_Q << 16) + 4 * MLKEM_Q);
    const __m256i shufbidx = _mm256_set_epi8(
        11,10,10,9, 9,8,8,7, 6,5,5,4, 4,3,3,2,
        9,8,8,7, 7,6,6,5, 4,3,3,2, 2,1,1,0);
    const __m256i sllvdidx = _mm256_set1_epi64x(4);
    const __m256i mask = _mm256_set1_epi32((32736 << 16) + 8184);

    for (unsigned i = 0; i < MLKEM_N / 16 - 1; i++) {
        __m256i f = _mm256_loadu_si256((const __m256i *)(a_a + 20 * i));
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_sllv_epi32(f, sllvdidx);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
    {
        const unsigned i = MLKEM_N / 16 - 1;
        __m256i f;
        memcpy(&f, a_a + 20 * i, 20);
        f = _mm256_permute4x64_epi64(f, 0x94);
        f = _mm256_shuffle_epi8(f, shufbidx);
        f = _mm256_sllv_epi32(f, sllvdidx);
        f = _mm256_srli_epi16(f, 1);
        f = _mm256_and_si256(f, mask);
        f = _mm256_mulhrs_epi16(f, q);
        _mm256_storeu_si256((__m256i *)(a_r + 16 * i), f);
    }
}

/* ============================================================================
 * basemul_acc_cached: K-way fused basemul + accumulate + deferred Montgomery
 * ============================================================================ */

{{TARGET_ATTR}}
void dap_mlkem_polyvec_basemul_acc_cached_{{ARCH_LOWER}}(
    int16_t * restrict a_r,
    const int16_t * const *a_polys_a,
    const int16_t * const *a_polys_b,
    const int16_t * const *a_caches,
    unsigned a_count)
{
    const __m256i l_qinv16 = _mm256_set1_epi16((int16_t)MLKEM_QINV);
    const __m256i l_q32    = _mm256_set1_epi32(MLKEM_Q);

    for (unsigned l_p = 0; l_p < 8; l_p++) {
        __m256i l_diag_lo  = _mm256_setzero_si256();
        __m256i l_diag_hi  = _mm256_setzero_si256();
        __m256i l_cross_lo = _mm256_setzero_si256();
        __m256i l_cross_hi = _mm256_setzero_si256();

        for (unsigned k = 0; k < a_count; k++) {
            __m256i l_ae = _mm256_loadu_si256((const __m256i *)(a_polys_a[k] + 32 * l_p));
            __m256i l_ao = _mm256_loadu_si256((const __m256i *)(a_polys_a[k] + 32 * l_p + 16));
            __m256i l_ce = _mm256_loadu_si256((const __m256i *)(a_caches[k] + 32 * l_p));
            __m256i l_co = _mm256_loadu_si256((const __m256i *)(a_caches[k] + 32 * l_p + 16));
            __m256i l_bo = _mm256_loadu_si256((const __m256i *)(a_polys_b[k] + 32 * l_p + 16));

            __m256i l_a_lo  = _mm256_unpacklo_epi16(l_ae, l_ao);
            __m256i l_a_hi  = _mm256_unpackhi_epi16(l_ae, l_ao);
            __m256i l_cd_lo = _mm256_unpacklo_epi16(l_ce, l_co);
            __m256i l_cd_hi = _mm256_unpackhi_epi16(l_ce, l_co);
            __m256i l_bc_lo = _mm256_unpacklo_epi16(l_bo, l_ce);
            __m256i l_bc_hi = _mm256_unpackhi_epi16(l_bo, l_ce);

            l_diag_lo  = _mm256_add_epi32(l_diag_lo,  _mm256_madd_epi16(l_a_lo, l_cd_lo));
            l_diag_hi  = _mm256_add_epi32(l_diag_hi,  _mm256_madd_epi16(l_a_hi, l_cd_hi));
            l_cross_lo = _mm256_add_epi32(l_cross_lo, _mm256_madd_epi16(l_a_lo, l_bc_lo));
            l_cross_hi = _mm256_add_epi32(l_cross_hi, _mm256_madd_epi16(l_a_hi, l_bc_hi));
        }

        __m256i ud_lo  = _mm256_mullo_epi16(l_diag_lo, l_qinv16);
        __m256i tqd_lo = _mm256_madd_epi16(ud_lo, l_q32);
        l_diag_lo = _mm256_srai_epi32(_mm256_sub_epi32(l_diag_lo, tqd_lo), 16);

        __m256i ud_hi  = _mm256_mullo_epi16(l_diag_hi, l_qinv16);
        __m256i tqd_hi = _mm256_madd_epi16(ud_hi, l_q32);
        l_diag_hi = _mm256_srai_epi32(_mm256_sub_epi32(l_diag_hi, tqd_hi), 16);

        __m256i uc_lo  = _mm256_mullo_epi16(l_cross_lo, l_qinv16);
        __m256i tqc_lo = _mm256_madd_epi16(uc_lo, l_q32);
        l_cross_lo = _mm256_srai_epi32(_mm256_sub_epi32(l_cross_lo, tqc_lo), 16);

        __m256i uc_hi  = _mm256_mullo_epi16(l_cross_hi, l_qinv16);
        __m256i tqc_hi = _mm256_madd_epi16(uc_hi, l_q32);
        l_cross_hi = _mm256_srai_epi32(_mm256_sub_epi32(l_cross_hi, tqc_hi), 16);

        __m256i l_re = _mm256_packs_epi32(l_diag_lo, l_diag_hi);
        __m256i l_ro = _mm256_packs_epi32(l_cross_lo, l_cross_hi);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * l_p), l_re);
        _mm256_storeu_si256((__m256i *)(a_r + 32 * l_p + 16), l_ro);
    }
}
