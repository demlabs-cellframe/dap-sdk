#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DAP_SHA1_DIGEST_LENGTH 20

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} dap_sha1_ctx_t;

static inline uint32_t s_sha1_rol(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static inline void s_sha1_transform(uint32_t s[5], const uint8_t b[64]) {
    uint32_t w[80], a = s[0], bb = s[1], c = s[2], d = s[3], e = s[4], t;
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)b[i*4] << 24 | (uint32_t)b[i*4+1] << 16 |
               (uint32_t)b[i*4+2] << 8 | b[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = s_sha1_rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    for (int i = 0; i < 80; i++) {
        if (i < 20)      t = s_sha1_rol(a,5) + ((bb&c)|((~bb)&d)) + e + w[i] + 0x5A827999;
        else if (i < 40) t = s_sha1_rol(a,5) + (bb^c^d) + e + w[i] + 0x6ED9EBA1;
        else if (i < 60) t = s_sha1_rol(a,5) + ((bb&c)|(bb&d)|(c&d)) + e + w[i] + 0x8F1BBCDC;
        else              t = s_sha1_rol(a,5) + (bb^c^d) + e + w[i] + 0xCA62C1D6;
        e = d; d = c; c = s_sha1_rol(bb, 30); bb = a; a = t;
    }
    s[0] += a; s[1] += bb; s[2] += c; s[3] += d; s[4] += e;
}

static inline void dap_hash_sha1(uint8_t *a_output, const uint8_t *a_input, size_t a_len) {
    dap_sha1_ctx_t ctx = { .state = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, .count = 0 };
    size_t i;
    for (i = 0; i + 64 <= a_len; i += 64)
        s_sha1_transform(ctx.state, a_input + i);
    uint8_t last[128];
    size_t rem = a_len - i;
    memcpy(last, a_input + i, rem);
    last[rem++] = 0x80;
    if (rem > 56) {
        memset(last + rem, 0, 64 - rem);
        s_sha1_transform(ctx.state, last);
        rem = 0;
        memset(last, 0, 64);
    } else {
        memset(last + rem, 0, 56 - rem);
    }
    uint64_t bits = (uint64_t)a_len * 8;
    for (int j = 7; j >= 0; j--)
        last[56 + (7 - j)] = (uint8_t)(bits >> (j * 8));
    s_sha1_transform(ctx.state, last);
    for (int j = 0; j < 5; j++) {
        a_output[j*4]   = (uint8_t)(ctx.state[j] >> 24);
        a_output[j*4+1] = (uint8_t)(ctx.state[j] >> 16);
        a_output[j*4+2] = (uint8_t)(ctx.state[j] >> 8);
        a_output[j*4+3] = (uint8_t)(ctx.state[j]);
    }
}

#define SHA_DIGEST_LENGTH DAP_SHA1_DIGEST_LENGTH
#define SHA1(input, len, output) dap_hash_sha1((output), (input), (len))

#ifdef __cplusplus
}
#endif
