/* Poly1305 AVX2 4-block parallel implementation snippet.
 * Included by dap_poly1305_simd.c.tpl — do not compile standalone. */

{{TARGET_ATTR}}
void dap_poly1305_blocks_{{ARCH_LOWER}}(s_poly1305_state_t *st,
    const uint8_t *msg, size_t nblocks)
{
    uint32_t r26[5];
    {
        unsigned __int128 rv = (unsigned __int128)st->r0
                             | ((unsigned __int128)st->r1 << 44)
                             | ((unsigned __int128)st->r2 << 88);
        r26[0] = (uint32_t)(rv)        & 0x3ffffff;
        r26[1] = (uint32_t)(rv >> 26)  & 0x3ffffff;
        r26[2] = (uint32_t)(rv >> 52)  & 0x3ffffff;
        r26[3] = (uint32_t)(rv >> 78)  & 0x3ffffff;
        r26[4] = (uint32_t)(rv >> 104) & 0x3ffffff;
    }

    uint32_t r4_26[5];
    s_poly1305_pow_r26(r4_26, r26, 4);

    __m256i yr0  = _mm256_set1_epi64x(r4_26[0]);
    __m256i yr1  = _mm256_set1_epi64x(r4_26[1]);
    __m256i yr2  = _mm256_set1_epi64x(r4_26[2]);
    __m256i yr3  = _mm256_set1_epi64x(r4_26[3]);
    __m256i yr4  = _mm256_set1_epi64x(r4_26[4]);
    __m256i yss1 = _mm256_set1_epi64x((uint64_t)r4_26[1] * 5);
    __m256i yss2 = _mm256_set1_epi64x((uint64_t)r4_26[2] * 5);
    __m256i yss3 = _mm256_set1_epi64x((uint64_t)r4_26[3] * 5);
    __m256i yss4 = _mm256_set1_epi64x((uint64_t)r4_26[4] * 5);
    __m256i ymsk = _mm256_set1_epi64x(0x3ffffff);
    __m256i yhbt = _mm256_set1_epi64x((uint64_t)1 << 24);

    uint32_t h26i[5];
    {
        unsigned __int128 hv = (unsigned __int128)st->h0
                             | ((unsigned __int128)st->h1 << 44)
                             | ((unsigned __int128)st->h2 << 88);
        h26i[0] = (uint32_t)(hv)        & 0x3ffffff;
        h26i[1] = (uint32_t)(hv >> 26)  & 0x3ffffff;
        h26i[2] = (uint32_t)(hv >> 52)  & 0x3ffffff;
        h26i[3] = (uint32_t)(hv >> 78)  & 0x3ffffff;
        h26i[4] = (uint32_t)(hv >> 104) & 0x3ffffff;
    }

    __m256i h0v = _mm256_set_epi64x(0, 0, 0, h26i[0]);
    __m256i h1v = _mm256_set_epi64x(0, 0, 0, h26i[1]);
    __m256i h2v = _mm256_set_epi64x(0, 0, 0, h26i[2]);
    __m256i h3v = _mm256_set_epi64x(0, 0, 0, h26i[3]);
    __m256i h4v = _mm256_set_epi64x(0, 0, 0, h26i[4]);

    size_t G = nblocks >> 2;

#define POLY_LOAD_4BLK() do {                                              \
    __m256i ck0 = _mm256_loadu_si256((const __m256i *)(msg));              \
    __m256i ck1 = _mm256_loadu_si256((const __m256i *)(msg + 32));         \
    __m256i lo = _mm256_permute4x64_epi64(                                 \
        _mm256_unpacklo_epi64(ck0, ck1), _MM_SHUFFLE(3,1,2,0));           \
    __m256i hi = _mm256_permute4x64_epi64(                                 \
        _mm256_unpackhi_epi64(ck0, ck1), _MM_SHUFFLE(3,1,2,0));           \
    h0v = _mm256_add_epi64(h0v, _mm256_and_si256(lo, ymsk));              \
    h1v = _mm256_add_epi64(h1v,                                            \
        _mm256_and_si256(_mm256_srli_epi64(lo, 26), ymsk));               \
    h2v = _mm256_add_epi64(h2v, _mm256_and_si256(                         \
        _mm256_or_si256(_mm256_srli_epi64(lo, 52),                        \
                        _mm256_slli_epi64(hi, 12)), ymsk));               \
    h3v = _mm256_add_epi64(h3v,                                            \
        _mm256_and_si256(_mm256_srli_epi64(hi, 14), ymsk));               \
    h4v = _mm256_add_epi64(h4v,                                            \
        _mm256_or_si256(_mm256_srli_epi64(hi, 40), yhbt));                \
    msg += 64;                                                             \
} while (0)

#define POLY_MUL_R4() do {                                                 \
    __m256i d0, d1, d2, d3, d4, cv;                                        \
    d0 = _mm256_mul_epu32(h0v, yr0);                                       \
    d0 = _mm256_add_epi64(d0, _mm256_mul_epu32(h1v, yss4));               \
    d0 = _mm256_add_epi64(d0, _mm256_mul_epu32(h2v, yss3));               \
    d0 = _mm256_add_epi64(d0, _mm256_mul_epu32(h3v, yss2));               \
    d0 = _mm256_add_epi64(d0, _mm256_mul_epu32(h4v, yss1));               \
    d1 = _mm256_mul_epu32(h0v, yr1);                                       \
    d1 = _mm256_add_epi64(d1, _mm256_mul_epu32(h1v, yr0));                \
    d1 = _mm256_add_epi64(d1, _mm256_mul_epu32(h2v, yss4));               \
    d1 = _mm256_add_epi64(d1, _mm256_mul_epu32(h3v, yss3));               \
    d1 = _mm256_add_epi64(d1, _mm256_mul_epu32(h4v, yss2));               \
    d2 = _mm256_mul_epu32(h0v, yr2);                                       \
    d2 = _mm256_add_epi64(d2, _mm256_mul_epu32(h1v, yr1));                \
    d2 = _mm256_add_epi64(d2, _mm256_mul_epu32(h2v, yr0));                \
    d2 = _mm256_add_epi64(d2, _mm256_mul_epu32(h3v, yss4));               \
    d2 = _mm256_add_epi64(d2, _mm256_mul_epu32(h4v, yss3));               \
    d3 = _mm256_mul_epu32(h0v, yr3);                                       \
    d3 = _mm256_add_epi64(d3, _mm256_mul_epu32(h1v, yr2));                \
    d3 = _mm256_add_epi64(d3, _mm256_mul_epu32(h2v, yr1));                \
    d3 = _mm256_add_epi64(d3, _mm256_mul_epu32(h3v, yr0));                \
    d3 = _mm256_add_epi64(d3, _mm256_mul_epu32(h4v, yss4));               \
    d4 = _mm256_mul_epu32(h0v, yr4);                                       \
    d4 = _mm256_add_epi64(d4, _mm256_mul_epu32(h1v, yr3));                \
    d4 = _mm256_add_epi64(d4, _mm256_mul_epu32(h2v, yr2));                \
    d4 = _mm256_add_epi64(d4, _mm256_mul_epu32(h3v, yr1));                \
    d4 = _mm256_add_epi64(d4, _mm256_mul_epu32(h4v, yr0));                \
    cv = _mm256_srli_epi64(d0, 26); h0v = _mm256_and_si256(d0, ymsk);     \
    d1 = _mm256_add_epi64(d1, cv);                                         \
    cv = _mm256_srli_epi64(d1, 26); h1v = _mm256_and_si256(d1, ymsk);     \
    d2 = _mm256_add_epi64(d2, cv);                                         \
    cv = _mm256_srli_epi64(d2, 26); h2v = _mm256_and_si256(d2, ymsk);     \
    d3 = _mm256_add_epi64(d3, cv);                                         \
    cv = _mm256_srli_epi64(d3, 26); h3v = _mm256_and_si256(d3, ymsk);     \
    d4 = _mm256_add_epi64(d4, cv);                                         \
    cv = _mm256_srli_epi64(d4, 26); h4v = _mm256_and_si256(d4, ymsk);     \
    h0v = _mm256_add_epi64(h0v,                                            \
        _mm256_add_epi64(cv, _mm256_slli_epi64(cv, 2)));                  \
    cv = _mm256_srli_epi64(h0v, 26);                                       \
    h0v = _mm256_and_si256(h0v, ymsk);                                     \
    h1v = _mm256_add_epi64(h1v, cv);                                       \
} while (0)

    for (size_t k = 0; k + 1 < G; k++) {
        POLY_LOAD_4BLK();
        POLY_MUL_R4();
    }
    if (G) {
        POLY_LOAD_4BLK();
    }

#undef POLY_MUL_R4
#undef POLY_LOAD_4BLK

    uint64_t __attribute__((aligned(32))) lb0[4], lb1[4], lb2[4], lb3[4], lb4[4];
    _mm256_store_si256((__m256i *)lb0, h0v);
    _mm256_store_si256((__m256i *)lb1, h1v);
    _mm256_store_si256((__m256i *)lb2, h2v);
    _mm256_store_si256((__m256i *)lb3, h3v);
    _mm256_store_si256((__m256i *)lb4, h4v);

    uint64_t rh[3];
    {
        unsigned __int128 v = (unsigned __int128)lb0[0]
            | ((unsigned __int128)lb1[0] << 26) | ((unsigned __int128)lb2[0] << 52)
            | ((unsigned __int128)lb3[0] << 78) | ((unsigned __int128)lb4[0] << 104);
        rh[0] = (uint64_t)(v)       & 0xFFFFFFFFFFF;
        rh[1] = (uint64_t)(v >> 44) & 0xFFFFFFFFFFF;
        rh[2] = (uint64_t)(v >> 88) & 0x3FFFFFFFFFF;
    }

    for (int step = 1; step <= 3; step++) {
        s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);
        unsigned __int128 v = (unsigned __int128)lb0[step]
            | ((unsigned __int128)lb1[step] << 26) | ((unsigned __int128)lb2[step] << 52)
            | ((unsigned __int128)lb3[step] << 78) | ((unsigned __int128)lb4[step] << 104);
        rh[0] += (uint64_t)(v)       & 0xFFFFFFFFFFF;
        rh[1] += (uint64_t)(v >> 44) & 0xFFFFFFFFFFF;
        rh[2] += (uint64_t)(v >> 88) & 0x3FFFFFFFFFF;
    }
    s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);

    st->h0 = rh[0]; st->h1 = rh[1]; st->h2 = rh[2];

    nblocks &= 3;
    while (nblocks--) {
        s_poly1305_block(st, msg, 1);
        msg += 16;
    }
}
