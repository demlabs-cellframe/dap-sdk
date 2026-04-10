/**
 * @file dap_poly1305_internal.h
 * @brief Poly1305 internal state and scalar helpers shared between
 *        the main implementation and architecture-specific SIMD backends.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <string.h>
#include "dap_cpu_arch.h"

#if defined(__SIZEOF_INT128__) && (DAP_PLATFORM_X86_64 || DAP_PLATFORM_ARM64)

/* ─── 64-bit donna: 3 limbs (44-44-42), 9 muls via __uint128_t ─── */

static inline uint64_t s_load64_le(const uint8_t *p)
{
    uint64_t r;
    memcpy(&r, p, 8);
    return r;
}

typedef struct {
    uint64_t r0, r1, r2;
    uint64_t s1, s2;
    uint64_t h0, h1, h2;
    uint64_t pad0, pad1;
    uint8_t  buf[16];
    size_t   buf_used;
} s_poly1305_state_t;

static inline void s_poly1305_block(s_poly1305_state_t *st, const uint8_t *blk, uint64_t hibit)
{
    uint64_t t0 = s_load64_le(blk);
    uint64_t t1 = s_load64_le(blk + 8);

    st->h0 += t0 & 0xFFFFFFFFFFF;
    st->h1 += ((t0 >> 44) | (t1 << 20)) & 0xFFFFFFFFFFF;
    st->h2 += ((t1 >> 24)) | (hibit << 40);

    unsigned __int128 d0 = (unsigned __int128)st->h0 * st->r0 +
                           (unsigned __int128)st->h1 * st->s2 +
                           (unsigned __int128)st->h2 * st->s1;
    unsigned __int128 d1 = (unsigned __int128)st->h0 * st->r1 +
                           (unsigned __int128)st->h1 * st->r0 +
                           (unsigned __int128)st->h2 * st->s2;
    unsigned __int128 d2 = (unsigned __int128)st->h0 * st->r2 +
                           (unsigned __int128)st->h1 * st->r1 +
                           (unsigned __int128)st->h2 * st->r0;

    uint64_t c;
    c = (uint64_t)(d0 >> 44); st->h0 = (uint64_t)d0 & 0xFFFFFFFFFFF;
    d1 += c;
    c = (uint64_t)(d1 >> 44); st->h1 = (uint64_t)d1 & 0xFFFFFFFFFFF;
    d2 += c;
    c = (uint64_t)(d2 >> 42); st->h2 = (uint64_t)d2 & 0x3FFFFFFFFFF;
    st->h0 += c * 5;
    c = st->h0 >> 44; st->h0 &= 0xFFFFFFFFFFF;
    st->h1 += c;
}

static inline void s_poly1305_pow_r26(uint32_t out[5], const uint32_t base[5], unsigned n)
{
    uint64_t h[5] = {base[0], base[1], base[2], base[3], base[4]};
    const uint64_t b0 = base[0], b1 = base[1], b2 = base[2], b3 = base[3], b4 = base[4];
    const uint64_t ss1 = b1*5, ss2 = b2*5, ss3 = b3*5, ss4 = b4*5;
    for (unsigned i = 1; i < n; i++) {
        uint64_t d0 = h[0]*b0 + h[1]*ss4 + h[2]*ss3 + h[3]*ss2 + h[4]*ss1;
        uint64_t d1 = h[0]*b1 + h[1]*b0  + h[2]*ss4 + h[3]*ss3 + h[4]*ss2;
        uint64_t d2 = h[0]*b2 + h[1]*b1  + h[2]*b0  + h[3]*ss4 + h[4]*ss3;
        uint64_t d3 = h[0]*b3 + h[1]*b2  + h[2]*b1  + h[3]*b0  + h[4]*ss4;
        uint64_t d4 = h[0]*b4 + h[1]*b3  + h[2]*b2  + h[3]*b1  + h[4]*b0;
        uint64_t c;
        c = d0 >> 26; h[0] = d0 & 0x3ffffff; d1 += c;
        c = d1 >> 26; h[1] = d1 & 0x3ffffff; d2 += c;
        c = d2 >> 26; h[2] = d2 & 0x3ffffff; d3 += c;
        c = d3 >> 26; h[3] = d3 & 0x3ffffff; d4 += c;
        c = d4 >> 26; h[4] = d4 & 0x3ffffff;
        h[0] += c * 5; c = h[0] >> 26; h[0] &= 0x3ffffff; h[1] += c;
    }
    for (int j = 0; j < 5; j++) out[j] = (uint32_t)h[j];
}

static inline void s_donna_mul_r(uint64_t h[3],
    uint64_t rr0, uint64_t rr1, uint64_t rr2, uint64_t ss1, uint64_t ss2)
{
    unsigned __int128 d0 = (unsigned __int128)h[0] * rr0
                         + (unsigned __int128)h[1] * ss2
                         + (unsigned __int128)h[2] * ss1;
    unsigned __int128 d1 = (unsigned __int128)h[0] * rr1
                         + (unsigned __int128)h[1] * rr0
                         + (unsigned __int128)h[2] * ss2;
    unsigned __int128 d2 = (unsigned __int128)h[0] * rr2
                         + (unsigned __int128)h[1] * rr1
                         + (unsigned __int128)h[2] * rr0;
    uint64_t c;
    c = (uint64_t)(d0 >> 44); h[0] = (uint64_t)d0 & 0xFFFFFFFFFFF;
    d1 += c;
    c = (uint64_t)(d1 >> 44); h[1] = (uint64_t)d1 & 0xFFFFFFFFFFF;
    d2 += c;
    c = (uint64_t)(d2 >> 42); h[2] = (uint64_t)d2 & 0x3FFFFFFFFFF;
    h[0] += c * 5;
    c = h[0] >> 44; h[0] &= 0xFFFFFFFFFFF;
    h[1] += c;
}

#endif /* __SIZEOF_INT128__ && 64-bit */
