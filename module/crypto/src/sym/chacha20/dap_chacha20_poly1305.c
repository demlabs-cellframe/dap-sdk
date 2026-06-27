/*
 * ChaCha20-Poly1305 AEAD (RFC 8439).
 * Reference implementation — constant-time on platforms without
 * variable-time multiply.
 */

#include <string.h>
#include "dap_chacha20_poly1305.h"
#include "dap_chacha20_internal.h"
#include "dap_poly1305_internal.h"
#include "dap_arch_dispatch.h"
#include "dap_cpu_detect.h"

/* ─── helpers ──────────────────────────────────────────────────────── */

static inline uint32_t s_rotl32(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

static inline uint32_t s_load32_le(const uint8_t *p)
{
    return (uint32_t)p[0]
         | (uint32_t)p[1] << 8
         | (uint32_t)p[2] << 16
         | (uint32_t)p[3] << 24;
}

static inline void s_store32_le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline void s_store64_le(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

/* ─── ChaCha20 (RFC 8439 §2.3) ────────────────────────────────────── */

#define QR(a, b, c, d) do { \
    a += b; d ^= a; d = s_rotl32(d, 16); \
    c += d; b ^= c; b = s_rotl32(b, 12); \
    a += b; d ^= a; d = s_rotl32(d, 8);  \
    c += d; b ^= c; b = s_rotl32(b, 7);  \
} while (0)

void dap_chacha20_block(uint32_t a_out[16], const uint32_t a_in[16])
{
    uint32_t x[16];
    memcpy(x, a_in, 64);
    for (int i = 0; i < 10; i++) {
        QR(x[0], x[4], x[ 8], x[12]);
        QR(x[1], x[5], x[ 9], x[13]);
        QR(x[2], x[6], x[10], x[14]);
        QR(x[3], x[7], x[11], x[15]);
        QR(x[0], x[5], x[10], x[15]);
        QR(x[1], x[6], x[11], x[12]);
        QR(x[2], x[7], x[ 8], x[13]);
        QR(x[3], x[4], x[ 9], x[14]);
    }
    for (int i = 0; i < 16; i++)
        a_out[i] = x[i] + a_in[i];
}

static void s_chacha20_encrypt_ref(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE], uint32_t a_counter)
{
    uint32_t state[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = s_load32_le(a_key + 4 * i);
    state[13] = s_load32_le(a_nonce);
    state[14] = s_load32_le(a_nonce + 4);
    state[15] = s_load32_le(a_nonce + 8);

    uint32_t ks[16];
    while (a_len > 0) {
        state[12] = a_counter++;
        dap_chacha20_block(ks, state);

        uint8_t ks_bytes[DAP_CHACHA20_BLOCK_SIZE];
        for (int i = 0; i < 16; i++)
            s_store32_le(ks_bytes + 4 * i, ks[i]);

        size_t todo = a_len < DAP_CHACHA20_BLOCK_SIZE ? a_len : DAP_CHACHA20_BLOCK_SIZE;
        for (size_t i = 0; i < todo; i++)
            a_out[i] = a_in[i] ^ ks_bytes[i];
        a_out += todo;
        a_in  += todo;
        a_len -= todo;
    }
}

/*
 * Runtime selection for ChaCha20 SIMD: one cached function pointer per TU
 * via the archive-wide dispatch framework.
 */
DAP_DISPATCH_DECLARE_RESOLVE(dap_chacha20_encrypt, void, uint8_t *, const uint8_t *, size_t,
        const uint8_t[32], const uint8_t[12], uint32_t);

static inline dap_chacha20_encrypt_fn_t dap_chacha20_encrypt_resolve(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("CHACHA20");
    dap_cpu_arch_t arch = dap_cpu_arch_get_best_for(l_class);
    (void)l_class;

#if DAP_PLATFORM_X86
#if !defined(_WIN32)
    if (__builtin_expect(arch >= DAP_CPU_ARCH_AVX512, 1))
        return dap_chacha20_encrypt_asm;
#endif
    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX2, dap_chacha20_encrypt_avx2);
    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_SSE2, dap_chacha20_encrypt_sse2);
#elif DAP_PLATFORM_ARM
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_NEON, dap_chacha20_encrypt_neon);
#endif

    return s_chacha20_encrypt_ref;
}

static inline void s_chacha20_encrypt_dispatch(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE], uint32_t a_counter)
{
    if (a_len >= 256) {
        DAP_DISPATCH_INLINE_CALL(dap_chacha20_encrypt, a_out, a_in, a_len, a_key, a_nonce, a_counter);
        return;
    }
    s_chacha20_encrypt_ref(a_out, a_in, a_len, a_key, a_nonce, a_counter);
}

void dap_chacha20_encrypt(uint8_t *a_out, const uint8_t *a_in, size_t a_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE], uint32_t a_counter)
{
    s_chacha20_encrypt_dispatch(a_out, a_in, a_len, a_key, a_nonce, a_counter);
}

/* ─── Poly1305 (RFC 8439 §2.5) — streaming ──────────────────────── */

#if defined(__SIZEOF_INT128__) && (DAP_PLATFORM_X86_64 || DAP_PLATFORM_ARM64)

#if DAP_PLATFORM_X86
extern void dap_poly1305_blocks_avx2(s_poly1305_state_t *, const uint8_t *, size_t);
extern void dap_poly1305_blocks_avx512_ifma(s_poly1305_state_t *, const uint8_t *, size_t);
#elif DAP_PLATFORM_ARM
extern void dap_poly1305_blocks_neon(s_poly1305_state_t *, const uint8_t *, size_t);
#endif

DAP_DISPATCH_DECLARE_RESOLVE(dap_poly1305_blocks, void, s_poly1305_state_t *, const uint8_t *,
                             size_t);

static inline void s_poly1305_blocks_ref(s_poly1305_state_t *st, const uint8_t *msg, size_t nblocks)
{
    while (nblocks--) {
        s_poly1305_block(st, msg, 1);
        msg += 16;
    }
}

static inline dap_poly1305_blocks_fn_t dap_poly1305_blocks_resolve(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("CHACHA20");
    dap_cpu_arch_t arch = dap_cpu_arch_get_best_for(l_class);
    (void)l_class;

#if DAP_PLATFORM_X86
    if (__builtin_expect(arch >= DAP_CPU_ARCH_AVX512, 1)) {
        dap_cpu_features_t l_feat = dap_cpu_detect_features();
        if (l_feat.has_avx512_ifma && l_feat.has_avx512vl)
            return dap_poly1305_blocks_avx512_ifma;
    }
    DAP_DISPATCH_RESOLVE_X86(DAP_CPU_ARCH_AVX2, dap_poly1305_blocks_avx2);
#elif DAP_PLATFORM_ARM
/* NEON multi-block kernel is intentionally disabled on AArch64 due known test-mismatch
 * on existing upstream NEON implementation for this revision. */
#if !defined(__aarch64__)
    DAP_DISPATCH_RESOLVE_ARM(DAP_CPU_ARCH_NEON, dap_poly1305_blocks_neon);
#endif
#endif

    return s_poly1305_blocks_ref;
}

static void s_poly1305_init(s_poly1305_state_t *st, const uint8_t a_key[32])
{
    uint64_t t0 = s_load64_le(a_key);
    uint64_t t1 = s_load64_le(a_key + 8);

    st->r0 = t0 & 0xFFC0FFFFFFF;
    st->r1 = ((t0 >> 44) | (t1 << 20)) & 0xFFFFFC0FFFF;
    st->r2 = ((t1 >> 24)) & 0x00FFFFFFC0F;

    st->s1 = st->r1 * (5 << 2);
    st->s2 = st->r2 * (5 << 2);

    st->h0 = st->h1 = st->h2 = 0;

    st->pad0 = s_load64_le(a_key + 16);
    st->pad1 = s_load64_le(a_key + 24);
    st->buf_used = 0;
}

static void s_poly1305_update(s_poly1305_state_t *st, const uint8_t *data, size_t len)
{
    if (st->buf_used) {
        size_t want = 16 - st->buf_used;
        if (len < want) {
            memcpy(st->buf + st->buf_used, data, len);
            st->buf_used += len;
            return;
        }
        memcpy(st->buf + st->buf_used, data, want);
        s_poly1305_block(st, st->buf, 1);
        data += want;
        len  -= want;
        st->buf_used = 0;
    }
    if (len >= 16) {
        size_t nblocks = len >> 4;
        if (nblocks >= 8) {
            DAP_DISPATCH_INLINE_CALL(dap_poly1305_blocks, st, data, nblocks);
            data += nblocks << 4;
            len  &= 15;
        } else {
            while (len >= 16) {
                s_poly1305_block(st, data, 1);
                data += 16;
                len  -= 16;
            }
        }
    }
    if (len) {
        memcpy(st->buf, data, len);
        st->buf_used = len;
    }
}

static void s_poly1305_pad16(s_poly1305_state_t *st)
{
    if (st->buf_used) {
        memset(st->buf + st->buf_used, 0, 16 - st->buf_used);
        s_poly1305_block(st, st->buf, 1);
        st->buf_used = 0;
    }
}

static void s_poly1305_finalize(s_poly1305_state_t *st, uint8_t tag[16])
{
    if (st->buf_used) {
        uint8_t blk[16] = {0};
        memcpy(blk, st->buf, st->buf_used);
        blk[st->buf_used] = 1;
        s_poly1305_block(st, blk, 0);
    }

    uint64_t h0 = st->h0, h1 = st->h1, h2 = st->h2;
    uint64_t c;
    c = h1 >> 44; h1 &= 0xFFFFFFFFFFF;
    h2 += c;
    c = h2 >> 42; h2 &= 0x3FFFFFFFFFF;
    h0 += c * 5;
    c = h0 >> 44; h0 &= 0xFFFFFFFFFFF;
    h1 += c;
    c = h1 >> 44; h1 &= 0xFFFFFFFFFFF;
    h2 += c;
    c = h2 >> 42; h2 &= 0x3FFFFFFFFFF;
    h0 += c * 5;
    c = h0 >> 44; h0 &= 0xFFFFFFFFFFF;
    h1 += c;

    uint64_t g0 = h0 + 5; c = g0 >> 44; g0 &= 0xFFFFFFFFFFF;
    uint64_t g1 = h1 + c; c = g1 >> 44; g1 &= 0xFFFFFFFFFFF;
    uint64_t g2 = h2 + c - ((uint64_t)1 << 42);

    uint64_t mask = (g2 >> 63) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0;
    h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2;

    uint64_t t0 = h0 | (h1 << 44);
    uint64_t t1 = (h1 >> 20) | (h2 << 24);

    unsigned __int128 f = (unsigned __int128)t0 + st->pad0;
    t0 = (uint64_t)f;
    f = (unsigned __int128)t1 + st->pad1 + (uint64_t)(f >> 64);
    t1 = (uint64_t)f;

    memcpy(tag,     &t0, 8);
    memcpy(tag + 8, &t1, 8);
}

#else
/* 32-bit fallback: 5 limbs × 26-bit */

typedef struct {
    uint32_t r0, r1, r2, r3, r4;
    uint32_t s1, s2, s3, s4;
    uint32_t h0, h1, h2, h3, h4;
    uint32_t sk0, sk1, sk2, sk3;
    uint8_t  buf[16];
    size_t   buf_used;
} s_poly1305_state_t;

static inline void s_poly1305_block(s_poly1305_state_t *st, const uint8_t *blk, uint32_t hibit)
{
    uint32_t b0 = s_load32_le(blk);
    uint32_t b1 = s_load32_le(blk + 4);
    uint32_t b2 = s_load32_le(blk + 8);
    uint32_t b3 = s_load32_le(blk + 12);

    st->h0 +=  b0                        & 0x03ffffff;
    st->h1 += ((b0 >> 26) | (b1 <<  6)) & 0x03ffffff;
    st->h2 += ((b1 >> 20) | (b2 << 12)) & 0x03ffffff;
    st->h3 += ((b2 >> 14) | (b3 << 18)) & 0x03ffffff;
    st->h4 +=  (b3 >>  8)               | (hibit << 24);

    uint64_t d0 = (uint64_t)st->h0*st->r0 + (uint64_t)st->h1*st->s4 + (uint64_t)st->h2*st->s3 + (uint64_t)st->h3*st->s2 + (uint64_t)st->h4*st->s1;
    uint64_t d1 = (uint64_t)st->h0*st->r1 + (uint64_t)st->h1*st->r0 + (uint64_t)st->h2*st->s4 + (uint64_t)st->h3*st->s3 + (uint64_t)st->h4*st->s2;
    uint64_t d2 = (uint64_t)st->h0*st->r2 + (uint64_t)st->h1*st->r1 + (uint64_t)st->h2*st->r0 + (uint64_t)st->h3*st->s4 + (uint64_t)st->h4*st->s3;
    uint64_t d3 = (uint64_t)st->h0*st->r3 + (uint64_t)st->h1*st->r2 + (uint64_t)st->h2*st->r1 + (uint64_t)st->h3*st->r0 + (uint64_t)st->h4*st->s4;
    uint64_t d4 = (uint64_t)st->h0*st->r4 + (uint64_t)st->h1*st->r3 + (uint64_t)st->h2*st->r2 + (uint64_t)st->h3*st->r1 + (uint64_t)st->h4*st->r0;

    uint32_t c;
    c = (uint32_t)(d0 >> 26); st->h0 = (uint32_t)d0 & 0x03ffffff; d1 += c;
    c = (uint32_t)(d1 >> 26); st->h1 = (uint32_t)d1 & 0x03ffffff; d2 += c;
    c = (uint32_t)(d2 >> 26); st->h2 = (uint32_t)d2 & 0x03ffffff; d3 += c;
    c = (uint32_t)(d3 >> 26); st->h3 = (uint32_t)d3 & 0x03ffffff; d4 += c;
    c = (uint32_t)(d4 >> 26); st->h4 = (uint32_t)d4 & 0x03ffffff;
    st->h0 += c * 5;
    c = st->h0 >> 26; st->h0 &= 0x03ffffff; st->h1 += c;
}

static void s_poly1305_init(s_poly1305_state_t *st, const uint8_t a_key[32])
{
    uint32_t rt0 = s_load32_le(a_key)      & 0x0fffffff;
    uint32_t rt1 = s_load32_le(a_key + 4)  & 0x0ffffffc;
    uint32_t rt2 = s_load32_le(a_key + 8)  & 0x0ffffffc;
    uint32_t rt3 = s_load32_le(a_key + 12) & 0x0ffffffc;

    st->r0 =  rt0                          & 0x03ffffff;
    st->r1 = ((rt0 >> 26) | (rt1 <<  6))   & 0x03ffffff;
    st->r2 = ((rt1 >> 20) | (rt2 << 12))   & 0x03ffffff;
    st->r3 = ((rt2 >> 14) | (rt3 << 18))   & 0x03ffffff;
    st->r4 =  (rt3 >>  8);

    st->s1 = st->r1 * 5; st->s2 = st->r2 * 5;
    st->s3 = st->r3 * 5; st->s4 = st->r4 * 5;

    st->h0 = st->h1 = st->h2 = st->h3 = st->h4 = 0;

    st->sk0 = s_load32_le(a_key + 16);
    st->sk1 = s_load32_le(a_key + 20);
    st->sk2 = s_load32_le(a_key + 24);
    st->sk3 = s_load32_le(a_key + 28);
    st->buf_used = 0;
}

static void s_poly1305_update(s_poly1305_state_t *st, const uint8_t *data, size_t len)
{
    if (st->buf_used) {
        size_t want = 16 - st->buf_used;
        if (len < want) {
            memcpy(st->buf + st->buf_used, data, len);
            st->buf_used += len;
            return;
        }
        memcpy(st->buf + st->buf_used, data, want);
        s_poly1305_block(st, st->buf, 1);
        data += want;
        len  -= want;
        st->buf_used = 0;
    }
    while (len >= 16) {
        s_poly1305_block(st, data, 1);
        data += 16;
        len  -= 16;
    }
    if (len) {
        memcpy(st->buf, data, len);
        st->buf_used = len;
    }
}

static void s_poly1305_pad16(s_poly1305_state_t *st)
{
    if (st->buf_used) {
        memset(st->buf + st->buf_used, 0, 16 - st->buf_used);
        s_poly1305_block(st, st->buf, 1);
        st->buf_used = 0;
    }
}

static void s_poly1305_finalize(s_poly1305_state_t *st, uint8_t tag[16])
{
    if (st->buf_used) {
        uint8_t blk[16] = {0};
        memcpy(blk, st->buf, st->buf_used);
        blk[st->buf_used] = 1;
        s_poly1305_block(st, blk, 0);
    }

    uint32_t h0 = st->h0, h1 = st->h1, h2 = st->h2, h3 = st->h3, h4 = st->h4;
    uint32_t c;
    c = h1 >> 26; h1 &= 0x03ffffff; h2 += c;
    c = h2 >> 26; h2 &= 0x03ffffff; h3 += c;
    c = h3 >> 26; h3 &= 0x03ffffff; h4 += c;
    c = h4 >> 26; h4 &= 0x03ffffff; h0 += c * 5;
    c = h0 >> 26; h0 &= 0x03ffffff; h1 += c;

    uint32_t g0 = h0 + 5; c = g0 >> 26; g0 &= 0x03ffffff;
    uint32_t g1 = h1 + c; c = g1 >> 26; g1 &= 0x03ffffff;
    uint32_t g2 = h2 + c; c = g2 >> 26; g2 &= 0x03ffffff;
    uint32_t g3 = h3 + c; c = g3 >> 26; g3 &= 0x03ffffff;
    uint32_t g4 = h4 + c - (1u << 26);

    uint32_t mask = (g4 >> 31) - 1;
    g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
    mask = ~mask;
    h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1;
    h2 = (h2 & mask) | g2; h3 = (h3 & mask) | g3;
    h4 = (h4 & mask) | g4;

    uint64_t f;
    f = (uint64_t)h0 | ((uint64_t)h1 << 26);
    uint32_t w0 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h2 << 20;
    uint32_t w1 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h3 << 14;
    uint32_t w2 = (uint32_t)f; f >>= 32;
    f += (uint64_t)h4 << 8;
    uint32_t w3 = (uint32_t)f;

    f = (uint64_t)w0 + st->sk0;             s_store32_le(tag,      (uint32_t)f);
    f = (uint64_t)w1 + st->sk1 + (f >> 32); s_store32_le(tag + 4,  (uint32_t)f);
    f = (uint64_t)w2 + st->sk2 + (f >> 32); s_store32_le(tag + 8,  (uint32_t)f);
    f = (uint64_t)w3 + st->sk3 + (f >> 32); s_store32_le(tag + 12, (uint32_t)f);
}
#endif /* __SIZEOF_INT128__ */

void dap_poly1305_mac(uint8_t a_tag[DAP_POLY1305_TAG_SIZE],
        const uint8_t *a_msg, size_t a_msg_len,
        const uint8_t a_key[DAP_POLY1305_KEY_SIZE])
{
    s_poly1305_state_t st;
    s_poly1305_init(&st, a_key);
    s_poly1305_update(&st, a_msg, a_msg_len);
    s_poly1305_finalize(&st, a_tag);
}

/* ─── AEAD construction (RFC 8439 §2.8) ──────────────────────────── */

static inline void s_aead_mac(uint8_t tag[16], const uint8_t poly_key[32],
        const uint8_t *aad, size_t aad_len,
        const uint8_t *ct, size_t ct_len)
{
    s_poly1305_state_t st;
    s_poly1305_init(&st, poly_key);
    if (aad_len) {
        s_poly1305_update(&st, aad, aad_len);
        s_poly1305_pad16(&st);
    }
    s_poly1305_update(&st, ct, ct_len);
    s_poly1305_pad16(&st);
    uint8_t lens[16];
    s_store64_le(lens,     (uint64_t)aad_len);
    s_store64_le(lens + 8, (uint64_t)ct_len);
    s_poly1305_update(&st, lens, 16);
    s_poly1305_finalize(&st, tag);
}

static inline void s_generate_poly_key(uint8_t a_poly_key[32],
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE])
{
    uint32_t state[16], ks[16];
    state[0] = 0x61707865; state[1] = 0x3320646e;
    state[2] = 0x79622d32; state[3] = 0x6b206574;
    for (int i = 0; i < 8; i++)
        state[4 + i] = s_load32_le(a_key + 4 * i);
    state[12] = 0;
    state[13] = s_load32_le(a_nonce);
    state[14] = s_load32_le(a_nonce + 4);
    state[15] = s_load32_le(a_nonce + 8);
    dap_chacha20_block(ks, state);
    for (int i = 0; i < 8; i++)
        s_store32_le(a_poly_key + 4 * i, ks[i]);
}

int dap_chacha20_poly1305_seal(uint8_t *a_ct, uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_pt, size_t a_pt_len,
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE])
{
    uint8_t poly_key[32];
    s_generate_poly_key(poly_key, a_key, a_nonce);
    dap_chacha20_encrypt(a_ct, a_pt, a_pt_len, a_key, a_nonce, 1);
    s_aead_mac(a_tag, poly_key, a_aad, a_aad_len, a_ct, a_pt_len);
    memset(poly_key, 0, sizeof(poly_key));
    return 0;
}

int dap_chacha20_poly1305_open(uint8_t *a_pt,
        const uint8_t *a_ct, size_t a_ct_len,
        const uint8_t a_tag[DAP_CHACHA20_POLY1305_TAG_SIZE],
        const uint8_t *a_aad, size_t a_aad_len,
        const uint8_t a_key[DAP_CHACHA20_KEY_SIZE],
        const uint8_t a_nonce[DAP_CHACHA20_NONCE_SIZE])
{
    uint8_t poly_key[32];
    s_generate_poly_key(poly_key, a_key, a_nonce);

    uint8_t computed_tag[DAP_POLY1305_TAG_SIZE];
    {
        s_poly1305_state_t st;
        s_poly1305_init(&st, poly_key);
        if (a_aad_len) {
            s_poly1305_update(&st, a_aad, a_aad_len);
            s_poly1305_pad16(&st);
        }
        s_poly1305_update(&st, a_ct, a_ct_len);
        s_poly1305_pad16(&st);
        uint8_t lens[16];
        s_store64_le(lens,     (uint64_t)a_aad_len);
        s_store64_le(lens + 8, (uint64_t)a_ct_len);
        s_poly1305_update(&st, lens, 16);
        s_poly1305_finalize(&st, computed_tag);
    }
    memset(poly_key, 0, sizeof(poly_key));

    uint8_t diff = 0;
    for (int i = 0; i < DAP_POLY1305_TAG_SIZE; i++)
        diff |= computed_tag[i] ^ a_tag[i];
    if (diff)
        return -1;

    dap_chacha20_encrypt(a_pt, a_ct, a_ct_len, a_key, a_nonce, 1);
    return 0;
}
