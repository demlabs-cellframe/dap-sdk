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
    const __m256i q = _mm256_set1_epi16(3329);
    for (unsigned i = 0; i < 256; i += 16) {
        __m256i v = _mm256_loadu_si256((const __m256i *)(a + i));
        __m256i t = _mm256_mulhi_epi16(v, bv);
        t = _mm256_srai_epi16(t, 10);
        v = _mm256_sub_epi16(v, _mm256_mullo_epi16(t, q));
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
