#include "dap_sha1.h"
#include <string.h>

static inline uint32_t s_dec32be(const void *src)
{
    const unsigned char *buf = (const unsigned char *)src;
    return ((uint32_t)buf[0] << 24)
         | ((uint32_t)buf[1] << 16)
         | ((uint32_t)buf[2] << 8)
         |  (uint32_t)buf[3];
}

static inline void s_enc32be(void *dst, uint32_t x)
{
    unsigned char *buf = (unsigned char *)dst;
    buf[0] = (unsigned char)(x >> 24);
    buf[1] = (unsigned char)(x >> 16);
    buf[2] = (unsigned char)(x >> 8);
    buf[3] = (unsigned char)x;
}

#define F(B, C, D)  ((((C) ^ (D)) & (B)) ^ (D))
#define G(B, C, D)  ((B) ^ (C) ^ (D))
#define H(B, C, D)  (((D) & (C)) | (((D) | (C)) & (B)))
#define ROTL(x, n)  (((x) << (n)) | ((x) >> (32 - (n))))
#define K1  ((uint32_t)0x5A827999)
#define K2  ((uint32_t)0x6ED9EBA1)
#define K3  ((uint32_t)0x8F1BBCDC)
#define K4  ((uint32_t)0xCA62C1D6)

static void s_sha1_transform(uint32_t val[5], const uint8_t buf[64])
{
    uint32_t m[80];
    uint32_t a, b, c, d, e;
    int i;

    a = val[0]; b = val[1]; c = val[2]; d = val[3]; e = val[4];
    for (i = 0; i < 16; i++)
        m[i] = s_dec32be(&buf[i << 2]);
    for (i = 16; i < 80; i++) {
        uint32_t x = m[i - 3] ^ m[i - 8] ^ m[i - 14] ^ m[i - 16];
        m[i] = ROTL(x, 1);
    }
    for (i = 0; i < 20; i += 5) {
        e += ROTL(a, 5) + F(b, c, d) + K1 + m[i + 0]; b = ROTL(b, 30);
        d += ROTL(e, 5) + F(a, b, c) + K1 + m[i + 1]; a = ROTL(a, 30);
        c += ROTL(d, 5) + F(e, a, b) + K1 + m[i + 2]; e = ROTL(e, 30);
        b += ROTL(c, 5) + F(d, e, a) + K1 + m[i + 3]; d = ROTL(d, 30);
        a += ROTL(b, 5) + F(c, d, e) + K1 + m[i + 4]; c = ROTL(c, 30);
    }
    for (i = 20; i < 40; i += 5) {
        e += ROTL(a, 5) + G(b, c, d) + K2 + m[i + 0]; b = ROTL(b, 30);
        d += ROTL(e, 5) + G(a, b, c) + K2 + m[i + 1]; a = ROTL(a, 30);
        c += ROTL(d, 5) + G(e, a, b) + K2 + m[i + 2]; e = ROTL(e, 30);
        b += ROTL(c, 5) + G(d, e, a) + K2 + m[i + 3]; d = ROTL(d, 30);
        a += ROTL(b, 5) + G(c, d, e) + K2 + m[i + 4]; c = ROTL(c, 30);
    }
    for (i = 40; i < 60; i += 5) {
        e += ROTL(a, 5) + H(b, c, d) + K3 + m[i + 0]; b = ROTL(b, 30);
        d += ROTL(e, 5) + H(a, b, c) + K3 + m[i + 1]; a = ROTL(a, 30);
        c += ROTL(d, 5) + H(e, a, b) + K3 + m[i + 2]; e = ROTL(e, 30);
        b += ROTL(c, 5) + H(d, e, a) + K3 + m[i + 3]; d = ROTL(d, 30);
        a += ROTL(b, 5) + H(c, d, e) + K3 + m[i + 4]; c = ROTL(c, 30);
    }
    for (i = 60; i < 80; i += 5) {
        e += ROTL(a, 5) + G(b, c, d) + K4 + m[i + 0]; b = ROTL(b, 30);
        d += ROTL(e, 5) + G(a, b, c) + K4 + m[i + 1]; a = ROTL(a, 30);
        c += ROTL(d, 5) + G(e, a, b) + K4 + m[i + 2]; e = ROTL(e, 30);
        b += ROTL(c, 5) + G(d, e, a) + K4 + m[i + 3]; d = ROTL(d, 30);
        a += ROTL(b, 5) + G(c, d, e) + K4 + m[i + 4]; c = ROTL(c, 30);
    }
    val[0] += a; val[1] += b; val[2] += c; val[3] += d; val[4] += e;
}

#undef F
#undef G
#undef H
#undef ROTL
#undef K1
#undef K2
#undef K3
#undef K4

int dap_sha1(uint8_t a_output[DAP_SHA1_DIGEST_SIZE], const uint8_t *a_input, size_t a_inlen)
{
    if (!a_output || (!a_input && a_inlen > 0))
        return -1;

    static const uint32_t IV[5] = {
        0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0
    };
    uint32_t val[5];
    uint8_t buf[64];
    uint64_t total = 0;
    size_t ptr = 0;

    memcpy(val, IV, sizeof(val));

    /* process full 64-byte blocks */
    const uint8_t *p = a_input;
    size_t remaining = a_inlen;
    while (remaining >= 64) {
        s_sha1_transform(val, p);
        p += 64;
        remaining -= 64;
        total += 64;
    }
    total += remaining;
    ptr = remaining;
    if (remaining > 0)
        memcpy(buf, p, remaining);

    /* padding */
    buf[ptr++] = 0x80;
    if (ptr > 56) {
        memset(buf + ptr, 0, 64 - ptr);
        s_sha1_transform(val, buf);
        ptr = 0;
    }
    memset(buf + ptr, 0, 56 - ptr);
    s_enc32be(buf + 56, (uint32_t)(total >> 29));
    s_enc32be(buf + 60, (uint32_t)(total << 3));
    s_sha1_transform(val, buf);

    /* output */
    for (int i = 0; i < 5; i++)
        s_enc32be(a_output + (i << 2), val[i]);

    return 0;
}
