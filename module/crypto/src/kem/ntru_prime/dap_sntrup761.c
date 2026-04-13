/*
 * Streamlined NTRU Prime 761 — reference implementation.
 * Based on the NTRU Prime specification (Bernstein, Chuengsatiansup,
 * Lange, van Vredendaal).
 *
 * Ring: R = Z[x]/(x^p - x - 1), R/q = (Z/qZ)[x]/(x^p - x - 1)
 * p = 761, q = 4591, w = 286.
 */

#include <string.h>
#include "dap_sntrup761.h"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_rand.h"

#define P SNTRUP761_P
#define Q SNTRUP761_Q
#define W SNTRUP761_W

#define SMALL_BYTES ((P + 3) / 4)

/* ─── Fq arithmetic: integers mod q ∈ (-q/2, q/2] ─────────────────── */

typedef int16_t fq_t;

static inline fq_t s_fq_freeze(int32_t a)
{
    a %= Q;
    if (a > (Q - 1) / 2) a -= Q;
    if (a < -(Q - 1) / 2) a += Q;
    return (fq_t)a;
}

static inline fq_t s_fq_recip(fq_t a)
{
    int32_t r = 1, x = a;
    int e = Q - 2;
    while (e > 0) {
        if (e & 1) r = s_fq_freeze(r * x);
        x = s_fq_freeze(x * x);
        e >>= 1;
    }
    return (fq_t)r;
}

/* ─── Small / F3 arithmetic ────────────────────────────────────────── */

typedef int8_t small_t;

static inline small_t s_f3_freeze(int32_t a)
{
    a = ((a % 3) + 3) % 3;
    if (a == 2) a = -1;
    return (small_t)a;
}

/* ─── Polynomial multiplication ────────────────────────────────────── */

static void s_rq_mult_small(fq_t *out, const fq_t *a, const small_t *b)
{
    int32_t fg[P + P - 1];
    memset(fg, 0, sizeof(fg));
    for (int i = 0; i < P; i++)
        for (int j = 0; j < P; j++)
            fg[i + j] += (int32_t)a[i] * b[j];
    for (int i = P + P - 2; i >= P; i--) {
        fg[i - P + 1] += fg[i];
        fg[i - P]     += fg[i];
    }
    for (int i = 0; i < P; i++)
        out[i] = s_fq_freeze(fg[i]);
}

static void s_r3_mult(small_t *out, const small_t *a, const small_t *b)
{
    int32_t fg[P + P - 1];
    memset(fg, 0, sizeof(fg));
    for (int i = 0; i < P; i++)
        for (int j = 0; j < P; j++)
            fg[i + j] += (int32_t)a[i] * b[j];
    for (int i = P + P - 2; i >= P; i--) {
        fg[i - P + 1] += fg[i];
        fg[i - P]     += fg[i];
    }
    for (int i = 0; i < P; i++)
        out[i] = s_f3_freeze(fg[i]);
}

/*
 * Rq_recip3: compute out such that 3 * a * out ≡ 1 (mod q) in R/q.
 * Divstep GCD directly in Fq, following the NTRU Prime reference.
 */
static int s_rq_recip3(fq_t *out, const small_t *a)
{
    fq_t f[P + 1], g[P + 1], v[P + 1], r[P + 1];
    int i, delta = 1;

    memset(v, 0, sizeof(v));
    memset(r, 0, sizeof(r));
    r[0] = s_fq_recip(3);

    memset(f, 0, sizeof(f));
    f[0] = 1;
    f[P - 1] = -1;
    f[P] = -1;

    for (i = 0; i < P; i++) g[P - 1 - i] = (fq_t)a[i];
    g[P] = 0;

    for (int loop = 0; loop < 2 * P - 1; loop++) {
        for (i = P; i > 0; i--) v[i] = v[i - 1];
        v[0] = 0;

        if (delta > 0 && g[0] != 0) {
            fq_t tmp;
            for (i = 0; i <= P; i++) {
                tmp = f[i]; f[i] = g[i]; g[i] = tmp;
                tmp = v[i]; v[i] = r[i]; r[i] = tmp;
            }
            delta = -delta;
        }
        delta++;

        int32_t f0 = (int32_t)f[0], g0 = (int32_t)g[0];
        for (i = 0; i <= P; i++) {
            g[i] = s_fq_freeze(f0 * (int32_t)g[i] - g0 * (int32_t)f[i]);
            r[i] = s_fq_freeze(f0 * (int32_t)r[i] - g0 * (int32_t)v[i]);
        }

        for (i = 0; i < P; i++) g[i] = g[i + 1];
        g[P] = 0;
    }

    if (f[0] == 0) return -1;

    fq_t scale = s_fq_recip(f[0]);
    for (i = 0; i < P; i++)
        out[i] = s_fq_freeze((int32_t)scale * v[P - 1 - i]);

    return 0;
}

/*
 * R3_recip: compute out such that a * out ≡ 1 (mod 3) in R/3.
 * Divstep GCD in F_3, same structure as Rq_recip3.
 */
static int s_r3_recip(small_t *out, const small_t *a)
{
    small_t f[P + 1], g[P + 1], v[P + 1], r[P + 1];
    int i, delta = 1;

    memset(v, 0, sizeof(v));
    memset(r, 0, sizeof(r));
    r[0] = 1;

    memset(f, 0, sizeof(f));
    f[0] = 1;
    f[P - 1] = -1;
    f[P] = -1;

    for (i = 0; i < P; i++) g[P - 1 - i] = a[i];
    g[P] = 0;

    for (int loop = 0; loop < 2 * P - 1; loop++) {
        for (i = P; i > 0; i--) v[i] = v[i - 1];
        v[0] = 0;

        if (delta > 0 && g[0] != 0) {
            small_t tmp;
            for (i = 0; i <= P; i++) {
                tmp = f[i]; f[i] = g[i]; g[i] = tmp;
                tmp = v[i]; v[i] = r[i]; r[i] = tmp;
            }
            delta = -delta;
        }
        delta++;

        int32_t f0 = (int32_t)f[0], g0 = (int32_t)g[0];
        for (i = 0; i <= P; i++) {
            g[i] = s_f3_freeze(f0 * (int32_t)g[i] - g0 * (int32_t)f[i]);
            r[i] = s_f3_freeze(f0 * (int32_t)r[i] - g0 * (int32_t)v[i]);
        }

        for (i = 0; i < P; i++) g[i] = g[i + 1];
        g[P] = 0;
    }

    if (f[0] == 0) return -1;

    small_t scale = f[0];
    for (i = 0; i < P; i++)
        out[i] = s_f3_freeze((int32_t)scale * v[P - 1 - i]);

    return 0;
}

/* ─── Encoding ────────────────────────────────────────────────────── */

static void s_rq_encode(uint8_t *out, const fq_t *f)
{
    for (int i = 0; i < P; i++) {
        uint16_t v = (uint16_t)((int32_t)f[i] + (Q - 1) / 2);
        out[2 * i]     = (uint8_t)(v & 0xFF);
        out[2 * i + 1] = (uint8_t)(v >> 8);
    }
}

static void s_rq_decode(fq_t *f, const uint8_t *in)
{
    for (int i = 0; i < P; i++) {
        uint16_t v = (uint16_t)in[2 * i] | ((uint16_t)in[2 * i + 1] << 8);
        f[i] = s_fq_freeze((int32_t)v - (Q - 1) / 2);
    }
}

static void s_small_encode(uint8_t *out, const small_t *f)
{
    for (int i = 0; i < P; i += 4) {
        uint8_t byte = 0;
        for (int j = 0; j < 4 && i + j < P; j++)
            byte |= (uint8_t)((f[i + j] + 1) & 3) << (2 * j);
        out[i / 4] = byte;
    }
}

static void s_small_decode(small_t *f, const uint8_t *in)
{
    for (int i = 0; i < P; i++) {
        uint8_t bits = (in[i / 4] >> (2 * (i % 4))) & 3;
        f[i] = (small_t)((int)bits - 1);
    }
}

static void s_rounded_encode(uint8_t *out, const fq_t *f)
{
    for (int i = 0; i < P; i++) {
        uint16_t v = (uint16_t)(((int32_t)f[i] + (Q - 1) / 2) / 3);
        out[2 * i]     = (uint8_t)(v & 0xFF);
        out[2 * i + 1] = (uint8_t)(v >> 8);
    }
}

static void s_rounded_decode(fq_t *f, const uint8_t *in)
{
    for (int i = 0; i < P; i++) {
        uint16_t v = (uint16_t)in[2 * i] | ((uint16_t)in[2 * i + 1] << 8);
        f[i] = s_fq_freeze((int32_t)v * 3 - (Q - 1) / 2);
    }
}

/* ─── Random generation ───────────────────────────────────────────── */

static void s_random_small(small_t *f)
{
    uint8_t buf[P];
    dap_random_bytes(buf, P);
    for (int i = 0; i < P; i++)
        f[i] = (small_t)((buf[i] % 3) - 1);
}

static void s_random_weight_w(small_t *f)
{
    uint8_t sign_buf[W];
    dap_random_bytes(sign_buf, W);
    for (int i = 0; i < W; i++)
        f[i] = (sign_buf[i] & 1) ? 1 : -1;
    for (int i = W; i < P; i++)
        f[i] = 0;
    uint32_t rand_buf[P];
    dap_random_bytes((uint8_t *)rand_buf, sizeof(rand_buf));
    for (int i = P - 1; i > 0; i--) {
        uint32_t j = rand_buf[i] % (uint32_t)(i + 1);
        small_t tmp = f[i]; f[i] = f[j]; f[j] = tmp;
    }
}

/* ─── Round ───────────────────────────────────────────────────────── */

static void s_round(fq_t *out, const fq_t *f)
{
    for (int i = 0; i < P; i++)
        out[i] = f[i] - s_f3_freeze(f[i]);
}

/* ─── Hash ────────────────────────────────────────────────────────── */

static void s_hash_session(uint8_t *out, const uint8_t *r_hash,
        const uint8_t *ct, size_t ct_len)
{
    size_t total = 32 + ct_len;
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, total);
    if (!l_buf) return;
    memcpy(l_buf, r_hash, 32);
    memcpy(l_buf + 32, ct, ct_len);
    dap_hash_sha3_256_raw(out, l_buf, total);
    DAP_DELETE(l_buf);
}

/* ─── KEM operations ──────────────────────────────────────────────── */

int sntrup761_keypair(uint8_t *pk, uint8_t *sk)
{
    small_t f[P], g[P], ginv3[P];
    fq_t h[P], finv[P];

    for (int attempts = 0; attempts < 100; attempts++) {
        s_random_small(f);
        if (s_rq_recip3(finv, f) == 0)
            goto f_ok;
    }
    return -1;
f_ok:

    for (int attempts = 0; attempts < 100; attempts++) {
        s_random_weight_w(g);
        if (s_r3_recip(ginv3, g) == 0)
            goto g_ok;
    }
    return -1;
g_ok:

    /* h = finv * g  (finv = 1/(3*f), so h = g/(3*f) in R/q) */
    s_rq_mult_small(h, finv, g);

    s_rq_encode(pk, h);

    /* sk = SmallEncode(f) || SmallEncode(ginv3) || pk || SHA3-256(pk) */
    s_small_encode(sk, f);
    s_small_encode(sk + SMALL_BYTES, ginv3);
    memcpy(sk + 2 * SMALL_BYTES, pk, SNTRUP761_PUBLICKEYBYTES);
    uint8_t pk_hash[32];
    dap_hash_sha3_256_raw(pk_hash, pk, SNTRUP761_PUBLICKEYBYTES);
    memcpy(sk + 2 * SMALL_BYTES + SNTRUP761_PUBLICKEYBYTES, pk_hash, 32);

    return 0;
}

int sntrup761_enc(uint8_t *ct, uint8_t *ss, const uint8_t *pk)
{
    fq_t h[P], hr[P], rounded[P];
    small_t r[P];

    s_rq_decode(h, pk);
    s_random_weight_w(r);

    s_rq_mult_small(hr, h, r);
    s_round(rounded, hr);
    s_rounded_encode(ct, rounded);

    uint8_t r_hash[32];
    dap_hash_sha3_256_raw(r_hash, (const uint8_t *)r, P);
    s_hash_session(ss, r_hash, ct, SNTRUP761_CIPHERTEXTBYTES);

    return 0;
}

int sntrup761_dec(uint8_t *ss, const uint8_t *ct, const uint8_t *sk)
{
    small_t f[P], ginv3[P];
    fq_t c[P], cf[P];

    s_small_decode(f, sk);
    s_small_decode(ginv3, sk + SMALL_BYTES);
    s_rounded_decode(c, ct);

    /* e = c * (3f) in R/q: must multiply by 3f as one operation,
     * not multiply by f then scale by 3 (that zeroes out mod 3) */
    small_t threef[P];
    for (int i = 0; i < P; i++)
        threef[i] = (small_t)(3 * f[i]);
    s_rq_mult_small(cf, c, threef);

    /* Reduce each coefficient mod 3 → g*r mod 3 */
    small_t e[P];
    for (int i = 0; i < P; i++)
        e[i] = s_f3_freeze((int32_t)cf[i]);

    /* r' = ginv3 * e in R/3 */
    small_t r_candidate[P];
    s_r3_mult(r_candidate, ginv3, e);

    uint8_t r_hash[32];
    dap_hash_sha3_256_raw(r_hash, (const uint8_t *)r_candidate, P);
    s_hash_session(ss, r_hash, ct, SNTRUP761_CIPHERTEXTBYTES);

    return 0;
}
