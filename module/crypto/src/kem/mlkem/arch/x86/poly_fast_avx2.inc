/* AVX2 static fast-function bodies for ML-KEM poly ops.
 * Included by generated dap_mlkem_poly_simd.h inside #pragma GCC target("avx2") scope. */

__attribute__((target("avx2")))
static void s_mlkem_poly_csubq_avx2(int16_t *a)
{
    const __m256i q = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
        v = _mm256_sub_epi16(v, q);
        __m256i mask = _mm256_srai_epi16(v, 15);
        v = _mm256_add_epi16(v, _mm256_and_si256(mask, q));
        _mm256_storeu_si256((__m256i *)(a + i), v);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_reduce_avx2(int16_t *a)
{
    const __m256i bv = _mm256_set1_epi16(20159);
    const __m256i q  = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i t = _mm256_mulhi_epi16(v, bv);
        t = _mm256_srai_epi16(t, 10);
        v = _mm256_sub_epi16(v, _mm256_mullo_epi16(t, q));
        __m256i sign = _mm256_srai_epi16(v, 15);
        v = _mm256_add_epi16(v, _mm256_and_si256(sign, q));
        _mm256_storeu_si256((__m256i *)(a + i), v);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_tomont_avx2(int16_t *a)
{
    const __m256i f    = _mm256_set1_epi16((int16_t)((1ULL << 32) % 3329));
    const __m256i qinv = _mm256_set1_epi16((int16_t)-3327);
    const __m256i q    = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v  = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i lo = _mm256_mullo_epi16(v, f);
        __m256i hi = _mm256_mulhi_epi16(v, f);
        __m256i u  = _mm256_mullo_epi16(lo, qinv);
        __m256i uq = _mm256_mulhi_epi16(u, q);
        _mm256_storeu_si256((__m256i *)(a + i), _mm256_sub_epi16(hi, uq));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_add_avx2(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        _mm256_storeu_si256((__m256i *)(r + i), _mm256_add_epi16(va, vb));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_poly_sub_avx2(int16_t *r, const int16_t *a, const int16_t *b)
{
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i va = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i vb = _mm256_loadu_si256((const __m256i *)(b + i));
        _mm256_storeu_si256((__m256i *)(r + i), _mm256_sub_epi16(va, vb));
    }
}

__attribute__((target("avx2")))
static void s_mlkem_cbd2_avx2(int16_t *r, const uint8_t *buf)
{
    const __m256i mask55 = _mm256_set1_epi32(0x55555555);
    const __m256i mask33 = _mm256_set1_epi32(0x33333333);
    const __m256i mask03 = _mm256_set1_epi32(0x03030303);
    const __m256i mask0F = _mm256_set1_epi32(0x0F0F0F0F);

    for (unsigned i = 0; i < 256 / 64; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(buf + 32 * i));

        __m256i f1 = _mm256_srli_epi16(f0, 1);
        f0 = _mm256_and_si256(mask55, f0);
        f1 = _mm256_and_si256(mask55, f1);
        f0 = _mm256_add_epi8(f0, f1);

        f1 = _mm256_srli_epi16(f0, 2);
        f0 = _mm256_and_si256(mask33, f0);
        f1 = _mm256_and_si256(mask33, f1);
        f0 = _mm256_add_epi8(f0, mask33);
        f0 = _mm256_sub_epi8(f0, f1);

        f1 = _mm256_srli_epi16(f0, 4);
        f0 = _mm256_and_si256(mask0F, f0);
        f1 = _mm256_and_si256(mask0F, f1);
        f0 = _mm256_sub_epi8(f0, mask03);
        f1 = _mm256_sub_epi8(f1, mask03);

        __m256i f2 = _mm256_unpacklo_epi8(f0, f1);
        __m256i f3 = _mm256_unpackhi_epi8(f0, f1);

        f0 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(f2));
        f1 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(f2, 1));
        f2 = _mm256_cvtepi8_epi16(_mm256_castsi256_si128(f3));
        f3 = _mm256_cvtepi8_epi16(_mm256_extracti128_si256(f3, 1));

        _mm256_storeu_si256((__m256i *)(r + 64 * i +  0), f0);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 16), f2);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 32), f1);
        _mm256_storeu_si256((__m256i *)(r + 64 * i + 48), f3);
    }
}

__attribute__((target("avx2")))
static void s_mlkem_cbd3_avx2(int16_t *r, const uint8_t *buf)
{
    const __m256i mask249 = _mm256_set1_epi32(0x249249);
    const __m256i mask6DB = _mm256_set1_epi32(0x6DB6DB);
    const __m256i mask07  = _mm256_set1_epi32(7);
    const __m256i mask70  = _mm256_set1_epi32(7 << 16);
    const __m256i mask3   = _mm256_set1_epi16(3);
    const __m256i shufb   = _mm256_set_epi8(
        -1,15,14,13, -1,12,11,10, -1, 9, 8, 7, -1, 6, 5, 4,
        -1,11,10, 9, -1, 8, 7, 6, -1, 5, 4, 3, -1, 2, 1, 0);

    for (unsigned i = 0; i < 256 / 32; i++) {
        __m256i f0 = _mm256_loadu_si256((const __m256i *)(buf + 24 * i));
        f0 = _mm256_permute4x64_epi64(f0, 0x94);
        f0 = _mm256_shuffle_epi8(f0, shufb);

        __m256i f1 = _mm256_srli_epi32(f0, 1);
        __m256i f2 = _mm256_srli_epi32(f0, 2);
        f0 = _mm256_and_si256(mask249, f0);
        f1 = _mm256_and_si256(mask249, f1);
        f2 = _mm256_and_si256(mask249, f2);
        f0 = _mm256_add_epi32(f0, f1);
        f0 = _mm256_add_epi32(f0, f2);

        f1 = _mm256_srli_epi32(f0, 3);
        f0 = _mm256_add_epi32(f0, mask6DB);
        f0 = _mm256_sub_epi32(f0, f1);

        f1 = _mm256_slli_epi32(f0, 10);
        f2 = _mm256_srli_epi32(f0, 12);
        __m256i f3 = _mm256_srli_epi32(f0, 2);
        f0 = _mm256_and_si256(f0, mask07);
        f1 = _mm256_and_si256(f1, mask70);
        f2 = _mm256_and_si256(f2, mask07);
        f3 = _mm256_and_si256(f3, mask70);
        f0 = _mm256_add_epi16(f0, f1);
        f1 = _mm256_add_epi16(f2, f3);
        f0 = _mm256_sub_epi16(f0, mask3);
        f1 = _mm256_sub_epi16(f1, mask3);

        f2 = _mm256_unpacklo_epi32(f0, f1);
        f3 = _mm256_unpackhi_epi32(f0, f1);
        f0 = _mm256_permute2x128_si256(f2, f3, 0x20);
        f1 = _mm256_permute2x128_si256(f2, f3, 0x31);

        _mm256_storeu_si256((__m256i *)(r + 32 * i +  0), f0);
        _mm256_storeu_si256((__m256i *)(r + 32 * i + 16), f1);
    }
}
