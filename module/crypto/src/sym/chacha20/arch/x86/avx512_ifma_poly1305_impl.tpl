/* Poly1305 AVX-512 IFMA 8-block parallel implementation.
 * Uses vpmadd52lo/vpmadd52hi for radix-2^44 arithmetic.
 * Included by dap_poly1305_simd.c.tpl — do not compile standalone. */

#if defined(__SIZEOF_INT128__)

static inline void s_donna44_mul(uint64_t out[3], const uint64_t a[3], const uint64_t b[3])
{
    uint64_t bs1 = 20 * b[1], bs2 = 20 * b[2];
    unsigned __int128 d0 = (unsigned __int128)a[0] * b[0]
                         + (unsigned __int128)a[1] * bs2
                         + (unsigned __int128)a[2] * bs1;
    unsigned __int128 d1 = (unsigned __int128)a[0] * b[1]
                         + (unsigned __int128)a[1] * b[0]
                         + (unsigned __int128)a[2] * bs2;
    unsigned __int128 d2 = (unsigned __int128)a[0] * b[2]
                         + (unsigned __int128)a[1] * b[1]
                         + (unsigned __int128)a[2] * b[0];
    uint64_t c;
    c = (uint64_t)(d0 >> 44); out[0] = (uint64_t)d0 & 0xFFFFFFFFFFF;
    d1 += c;
    c = (uint64_t)(d1 >> 44); out[1] = (uint64_t)d1 & 0xFFFFFFFFFFF;
    d2 += c;
    c = (uint64_t)(d2 >> 42); out[2] = (uint64_t)d2 & 0x3FFFFFFFFFF;
    out[0] += c * 5;
    c = out[0] >> 44; out[0] &= 0xFFFFFFFFFFF;
    out[1] += c;
}

{{TARGET_ATTR}}
void dap_poly1305_blocks_{{ARCH_LOWER}}(s_poly1305_state_t *st,
    const uint8_t *msg, size_t nblocks)
{
    uint64_t r44[3] = {st->r0, st->r1, st->r2};
    uint64_t r2[3], r4[3], r8[3];
    s_donna44_mul(r2, r44, r44);
    s_donna44_mul(r4, r2, r2);
    s_donna44_mul(r8, r4, r4);

    __m512i yr0  = _mm512_set1_epi64((long long)r8[0]);
    __m512i yr1  = _mm512_set1_epi64((long long)r8[1]);
    __m512i yr2  = _mm512_set1_epi64((long long)r8[2]);
    __m512i yss1 = _mm512_set1_epi64((long long)(20 * r8[1]));
    __m512i yss2 = _mm512_set1_epi64((long long)(20 * r8[2]));
    __m512i mask44 = _mm512_set1_epi64(0xFFFFFFFFFFF);
    __m512i mask42 = _mm512_set1_epi64(0x3FFFFFFFFFF);
    __m512i hibit  = _mm512_set1_epi64((long long)((uint64_t)1 << 40));
    __m512i zero   = _mm512_setzero_si512();

    __m512i idx_lo = _mm512_setr_epi64(0, 2, 4, 6, 8, 10, 12, 14);
    __m512i idx_hi = _mm512_setr_epi64(1, 3, 5, 7, 9, 11, 13, 15);

    __m512i h0v = _mm512_set_epi64(0,0,0,0,0,0,0, (long long)st->h0);
    __m512i h1v = _mm512_set_epi64(0,0,0,0,0,0,0, (long long)st->h1);
    __m512i h2v = _mm512_set_epi64(0,0,0,0,0,0,0, (long long)st->h2);

    size_t G = nblocks >> 3;

#define IFMA_LOAD_8BLK() do {                                                   \
    __m512i ck0 = _mm512_loadu_si512(msg);                                      \
    __m512i ck1 = _mm512_loadu_si512(msg + 64);                                 \
    __m512i lo = _mm512_permutex2var_epi64(ck0, idx_lo, ck1);                   \
    __m512i hi = _mm512_permutex2var_epi64(ck0, idx_hi, ck1);                   \
    h0v = _mm512_add_epi64(h0v, _mm512_and_si512(lo, mask44));                  \
    h1v = _mm512_add_epi64(h1v, _mm512_and_si512(                              \
        _mm512_or_si512(_mm512_srli_epi64(lo, 44),                              \
                        _mm512_slli_epi64(hi, 20)), mask44));                   \
    h2v = _mm512_add_epi64(h2v,                                                 \
        _mm512_or_si512(_mm512_srli_epi64(hi, 24), hibit));                     \
    msg += 128;                                                                  \
} while (0)

#define IFMA_MUL_R8() do {                                                       \
    __m512i d0l, d0h, d1l, d1h, d2l, d2h, cv;                                   \
    d0l = _mm512_madd52lo_epu64(zero, h0v, yr0);                                \
    d0l = _mm512_madd52lo_epu64(d0l, h1v, yss2);                                \
    d0l = _mm512_madd52lo_epu64(d0l, h2v, yss1);                                \
    d0h = _mm512_madd52hi_epu64(zero, h0v, yr0);                                \
    d0h = _mm512_madd52hi_epu64(d0h, h1v, yss2);                                \
    d0h = _mm512_madd52hi_epu64(d0h, h2v, yss1);                                \
    d1l = _mm512_madd52lo_epu64(zero, h0v, yr1);                                \
    d1l = _mm512_madd52lo_epu64(d1l, h1v, yr0);                                 \
    d1l = _mm512_madd52lo_epu64(d1l, h2v, yss2);                                \
    d1h = _mm512_madd52hi_epu64(zero, h0v, yr1);                                \
    d1h = _mm512_madd52hi_epu64(d1h, h1v, yr0);                                 \
    d1h = _mm512_madd52hi_epu64(d1h, h2v, yss2);                                \
    d2l = _mm512_madd52lo_epu64(zero, h0v, yr2);                                \
    d2l = _mm512_madd52lo_epu64(d2l, h1v, yr1);                                 \
    d2l = _mm512_madd52lo_epu64(d2l, h2v, yr0);                                 \
    d2h = _mm512_madd52hi_epu64(zero, h0v, yr2);                                \
    d2h = _mm512_madd52hi_epu64(d2h, h1v, yr1);                                 \
    d2h = _mm512_madd52hi_epu64(d2h, h2v, yr0);                                 \
    h0v = _mm512_and_si512(d0l, mask44);                                         \
    cv = _mm512_add_epi64(_mm512_srli_epi64(d0l, 44), _mm512_slli_epi64(d0h, 8)); \
    d1l = _mm512_add_epi64(d1l, cv);                                             \
    h1v = _mm512_and_si512(d1l, mask44);                                         \
    cv = _mm512_add_epi64(_mm512_srli_epi64(d1l, 44), _mm512_slli_epi64(d1h, 8)); \
    d2l = _mm512_add_epi64(d2l, cv);                                             \
    h2v = _mm512_and_si512(d2l, mask42);                                         \
    cv = _mm512_add_epi64(_mm512_srli_epi64(d2l, 42), _mm512_slli_epi64(d2h, 10)); \
    h0v = _mm512_add_epi64(h0v, _mm512_add_epi64(cv, _mm512_slli_epi64(cv, 2))); \
    cv = _mm512_srli_epi64(h0v, 44);                                             \
    h0v = _mm512_and_si512(h0v, mask44);                                         \
    h1v = _mm512_add_epi64(h1v, cv);                                             \
} while (0)

    for (size_t k = 0; k + 1 < G; k++) {
        IFMA_LOAD_8BLK();
        IFMA_MUL_R8();
    }
    if (G) {
        IFMA_LOAD_8BLK();
    }

#undef IFMA_MUL_R8
#undef IFMA_LOAD_8BLK

    uint64_t __attribute__((aligned(64))) lb0[8], lb1[8], lb2[8];
    _mm512_store_si512(lb0, h0v);
    _mm512_store_si512(lb1, h1v);
    _mm512_store_si512(lb2, h2v);

    uint64_t rh[3] = {lb0[0], lb1[0], lb2[0]};
    for (int step = 1; step <= 7; step++) {
        s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);
        rh[0] += lb0[step];
        rh[1] += lb1[step];
        rh[2] += lb2[step];
    }
    s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);

    st->h0 = rh[0]; st->h1 = rh[1]; st->h2 = rh[2];

    nblocks &= 7;
    while (nblocks--) {
        s_poly1305_block(st, msg, 1);
        msg += 16;
    }
}

#endif /* __SIZEOF_INT128__ */
