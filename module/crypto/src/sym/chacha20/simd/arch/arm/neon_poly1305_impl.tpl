/* Poly1305 NEON 2-block parallel implementation snippet.
 * Included by dap_poly1305_simd.c.tpl — do not compile standalone.
 *
 * Uses 26-bit radix decomposition with NEON 32×32→64 multiply.
 * Processes 2 message blocks per iteration (r^2 stride).
 *
 * AArch64 NEON: vmull_u32 / vmlal_u32 for widening multiply-accumulate.
 */

#include <arm_neon.h>

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

    uint32_t r2_26[5];
    s_poly1305_pow_r26(r2_26, r26, 2);

    uint32x2_t nr0  = vdup_n_u32(r2_26[0]);
    uint32x2_t nr1  = vdup_n_u32(r2_26[1]);
    uint32x2_t nr2  = vdup_n_u32(r2_26[2]);
    uint32x2_t nr3  = vdup_n_u32(r2_26[3]);
    uint32x2_t nr4  = vdup_n_u32(r2_26[4]);
    uint32x2_t nss1 = vdup_n_u32(r2_26[1] * 5);
    uint32x2_t nss2 = vdup_n_u32(r2_26[2] * 5);
    uint32x2_t nss3 = vdup_n_u32(r2_26[3] * 5);
    uint32x2_t nss4 = vdup_n_u32(r2_26[4] * 5);
    uint64x2_t nmsk = vdupq_n_u64(0x3ffffff);
    uint64x2_t nhbt = vdupq_n_u64((uint64_t)1 << 24);

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

    uint64x2_t h0v = vcombine_u64(vcreate_u64(h26i[0]), vcreate_u64(0));
    uint64x2_t h1v = vcombine_u64(vcreate_u64(h26i[1]), vcreate_u64(0));
    uint64x2_t h2v = vcombine_u64(vcreate_u64(h26i[2]), vcreate_u64(0));
    uint64x2_t h3v = vcombine_u64(vcreate_u64(h26i[3]), vcreate_u64(0));
    uint64x2_t h4v = vcombine_u64(vcreate_u64(h26i[4]), vcreate_u64(0));

    size_t G = nblocks >> 1;

#define POLY_LOAD_2BLK() do {                                               \
    uint64x2_t lo = vld1q_u64((const uint64_t *)msg);                       \
    uint64x2_t hi = vld1q_u64((const uint64_t *)(msg + 16));                \
    uint64x2_t t0 = vuzp1q_u64(lo, hi);                                    \
    uint64x2_t t1 = vuzp2q_u64(lo, hi);                                    \
    h0v = vaddq_u64(h0v, vandq_u64(t0, nmsk));                             \
    h1v = vaddq_u64(h1v, vandq_u64(vshrq_n_u64(t0, 26), nmsk));           \
    h2v = vaddq_u64(h2v, vandq_u64(                                        \
        vorrq_u64(vshrq_n_u64(t0, 52), vshlq_n_u64(t1, 12)), nmsk));      \
    h3v = vaddq_u64(h3v, vandq_u64(vshrq_n_u64(t1, 14), nmsk));           \
    h4v = vaddq_u64(h4v, vorrq_u64(vshrq_n_u64(t1, 40), nhbt));           \
    msg += 32;                                                              \
} while (0)

#define POLY_MUL_R2() do {                                                  \
    uint32x2_t hh0 = vmovn_u64(h0v), hh1 = vmovn_u64(h1v);                \
    uint32x2_t hh2 = vmovn_u64(h2v), hh3 = vmovn_u64(h3v);                \
    uint32x2_t hh4 = vmovn_u64(h4v);                                       \
    uint64x2_t d0, d1, d2, d3, d4, cv;                                     \
    d0 = vmull_u32(hh0, nr0);                                              \
    d0 = vmlal_u32(d0, hh1, nss4);                                         \
    d0 = vmlal_u32(d0, hh2, nss3);                                         \
    d0 = vmlal_u32(d0, hh3, nss2);                                         \
    d0 = vmlal_u32(d0, hh4, nss1);                                         \
    d1 = vmull_u32(hh0, nr1);                                              \
    d1 = vmlal_u32(d1, hh1, nr0);                                          \
    d1 = vmlal_u32(d1, hh2, nss4);                                         \
    d1 = vmlal_u32(d1, hh3, nss3);                                         \
    d1 = vmlal_u32(d1, hh4, nss2);                                         \
    d2 = vmull_u32(hh0, nr2);                                              \
    d2 = vmlal_u32(d2, hh1, nr1);                                          \
    d2 = vmlal_u32(d2, hh2, nr0);                                          \
    d2 = vmlal_u32(d2, hh3, nss4);                                         \
    d2 = vmlal_u32(d2, hh4, nss3);                                         \
    d3 = vmull_u32(hh0, nr3);                                              \
    d3 = vmlal_u32(d3, hh1, nr2);                                          \
    d3 = vmlal_u32(d3, hh2, nr1);                                          \
    d3 = vmlal_u32(d3, hh3, nr0);                                          \
    d3 = vmlal_u32(d3, hh4, nss4);                                         \
    d4 = vmull_u32(hh0, nr4);                                              \
    d4 = vmlal_u32(d4, hh1, nr3);                                          \
    d4 = vmlal_u32(d4, hh2, nr2);                                          \
    d4 = vmlal_u32(d4, hh3, nr1);                                          \
    d4 = vmlal_u32(d4, hh4, nr0);                                          \
    cv = vshrq_n_u64(d0, 26); h0v = vandq_u64(d0, nmsk);                  \
    d1 = vaddq_u64(d1, cv);                                                 \
    cv = vshrq_n_u64(d1, 26); h1v = vandq_u64(d1, nmsk);                  \
    d2 = vaddq_u64(d2, cv);                                                 \
    cv = vshrq_n_u64(d2, 26); h2v = vandq_u64(d2, nmsk);                  \
    d3 = vaddq_u64(d3, cv);                                                 \
    cv = vshrq_n_u64(d3, 26); h3v = vandq_u64(d3, nmsk);                  \
    d4 = vaddq_u64(d4, cv);                                                 \
    cv = vshrq_n_u64(d4, 26); h4v = vandq_u64(d4, nmsk);                  \
    h0v = vaddq_u64(h0v, vaddq_u64(cv, vshlq_n_u64(cv, 2)));             \
    cv = vshrq_n_u64(h0v, 26);                                             \
    h0v = vandq_u64(h0v, nmsk);                                            \
    h1v = vaddq_u64(h1v, cv);                                              \
} while (0)

    for (size_t k = 0; k + 1 < G; k++) {
        POLY_LOAD_2BLK();
        POLY_MUL_R2();
    }
    if (G) {
        POLY_LOAD_2BLK();
    }

#undef POLY_MUL_R2
#undef POLY_LOAD_2BLK

    uint64_t lb0[2], lb1[2], lb2[2], lb3[2], lb4[2];
    vst1q_u64(lb0, h0v);
    vst1q_u64(lb1, h1v);
    vst1q_u64(lb2, h2v);
    vst1q_u64(lb3, h3v);
    vst1q_u64(lb4, h4v);

    uint64_t rh[3];
    {
        unsigned __int128 v = (unsigned __int128)lb0[0]
            | ((unsigned __int128)lb1[0] << 26) | ((unsigned __int128)lb2[0] << 52)
            | ((unsigned __int128)lb3[0] << 78) | ((unsigned __int128)lb4[0] << 104);
        rh[0] = (uint64_t)(v)       & 0xFFFFFFFFFFF;
        rh[1] = (uint64_t)(v >> 44) & 0xFFFFFFFFFFF;
        rh[2] = (uint64_t)(v >> 88) & 0x3FFFFFFFFFF;
    }

    s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);
    {
        unsigned __int128 v = (unsigned __int128)lb0[1]
            | ((unsigned __int128)lb1[1] << 26) | ((unsigned __int128)lb2[1] << 52)
            | ((unsigned __int128)lb3[1] << 78) | ((unsigned __int128)lb4[1] << 104);
        rh[0] += (uint64_t)(v)       & 0xFFFFFFFFFFF;
        rh[1] += (uint64_t)(v >> 44) & 0xFFFFFFFFFFF;
        rh[2] += (uint64_t)(v >> 88) & 0x3FFFFFFFFFF;
    }
    s_donna_mul_r(rh, st->r0, st->r1, st->r2, st->s1, st->s2);

    st->h0 = rh[0]; st->h1 = rh[1]; st->h2 = rh[2];

    nblocks &= 1;
    while (nblocks--) {
        s_poly1305_block(st, msg, 1);
        msg += 16;
    }
}
