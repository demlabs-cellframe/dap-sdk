/**
 * @file dap_ntru_prime_sig.c
 * @brief NTRU Prime Signature — reference implementation.
 *
 * Fiat-Shamir with Aborts on R = Z[x]/(x^p - x - 1), p=761, q=131071.
 * Verification equation: h·z - c·g ≡ h·y (mod q), where z = y + c·f.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <string.h>
#include "dap_ntru_prime_sig.h"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_rand.h"

#define P NTRU_PRIME_SIG_P
#define Q NTRU_PRIME_SIG_Q
#define W NTRU_PRIME_SIG_W
#define TAU NTRU_PRIME_SIG_TAU
#define GAMMA1 NTRU_PRIME_SIG_GAMMA1
#define BETA NTRU_PRIME_SIG_BETA

#define LOG_TAG "ntru_prime_sig"

/* ─── Fq arithmetic: integers mod q ∈ (-(q-1)/2, (q-1)/2] ──────── */

typedef int32_t fq_t;
typedef int8_t small_t;

static inline fq_t s_fq_freeze(int64_t a)
{
    int32_t r = (int32_t)(a % Q);
    if (r > (Q - 1) / 2) r -= Q;
    if (r < -(Q - 1) / 2) r += Q;
    return r;
}

static inline fq_t s_fq_recip(fq_t a)
{
    int64_t r = 1, x = a;
    int e = Q - 2;
    while (e > 0) {
        if (e & 1) r = s_fq_freeze(r * x);
        x = s_fq_freeze(x * x);
        e >>= 1;
    }
    return (fq_t)r;
}

/* ─── Polynomial multiplication in R = Z[x]/(x^p - x - 1) ──────── */

static void s_rq_mult(fq_t *out, const fq_t *a, const fq_t *b)
{
    int64_t fg[P + P - 1];
    memset(fg, 0, sizeof(fg));
    for (int i = 0; i < P; i++)
        for (int j = 0; j < P; j++)
            fg[i + j] += (int64_t)a[i] * b[j];
    /* reduce x^k for k >= P: x^P ≡ x + 1 */
    for (int i = P + P - 2; i >= P; i--) {
        fg[i - P + 1] += fg[i];
        fg[i - P]     += fg[i];
    }
    for (int i = 0; i < P; i++)
        out[i] = s_fq_freeze(fg[i]);
}

static void s_rq_mult_small(fq_t *out, const fq_t *a, const small_t *b)
{
    int64_t fg[P + P - 1];
    memset(fg, 0, sizeof(fg));
    for (int i = 0; i < P; i++)
        for (int j = 0; j < P; j++)
            fg[i + j] += (int64_t)a[i] * b[j];
    for (int i = P + P - 2; i >= P; i--) {
        fg[i - P + 1] += fg[i];
        fg[i - P]     += fg[i];
    }
    for (int i = 0; i < P; i++)
        out[i] = s_fq_freeze(fg[i]);
}

/* ─── Rq inversion via divstep GCD ──────────────────────────────── */

static int s_rq_recip(fq_t *out, const small_t *a)
{
    fq_t f[P + 1], g[P + 1], v[P + 1], r[P + 1];
    int i, delta = 1;

    memset(v, 0, sizeof(v));
    memset(r, 0, sizeof(r));
    r[0] = 1;

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

        int64_t f0 = (int64_t)f[0], g0 = (int64_t)g[0];
        for (i = 0; i <= P; i++) {
            g[i] = s_fq_freeze(f0 * g[i] - g0 * f[i]);
            r[i] = s_fq_freeze(f0 * r[i] - g0 * v[i]);
        }

        for (i = 0; i < P; i++) g[i] = g[i + 1];
        g[P] = 0;
    }

    if (f[0] == 0) return -1;

    fq_t scale = s_fq_recip(f[0]);
    for (i = 0; i < P; i++)
        out[i] = s_fq_freeze((int64_t)scale * v[P - 1 - i]);

    return 0;
}

/* ─── Encoding / Decoding ──────────────────────────────────────── */

static void s_fq_encode(uint8_t *out, const fq_t *f)
{
    /* Pack P coefficients, each in [0, q-1], as 17-bit values */
    int bit_pos = 0;
    memset(out, 0, NTRU_PRIME_SIG_POLY_BYTES);
    for (int i = 0; i < P; i++) {
        uint32_t v = (uint32_t)((int32_t)f[i] + (Q - 1) / 2);
        int byte_idx = bit_pos / 8;
        int bit_off = bit_pos % 8;
        out[byte_idx]     |= (uint8_t)(v << bit_off);
        out[byte_idx + 1] |= (uint8_t)(v >> (8 - bit_off));
        if (bit_off + 17 > 16)
            out[byte_idx + 2] |= (uint8_t)(v >> (16 - bit_off));
        bit_pos += 17;
    }
}

static void s_fq_decode(fq_t *f, const uint8_t *in)
{
    int bit_pos = 0;
    for (int i = 0; i < P; i++) {
        int byte_idx = bit_pos / 8;
        int bit_off = bit_pos % 8;
        uint32_t v = (uint32_t)in[byte_idx] >> bit_off;
        v |= (uint32_t)in[byte_idx + 1] << (8 - bit_off);
        v |= (uint32_t)in[byte_idx + 2] << (16 - bit_off);
        v &= (1U << 17) - 1;
        f[i] = s_fq_freeze((int32_t)v - (Q - 1) / 2);
        bit_pos += 17;
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

/* ─── Random generation ──────────────────────────────────────────── */

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

static void s_random_mask(fq_t *y)
{
    /* Sample y uniformly from [-GAMMA1, GAMMA1]^P */
    uint8_t buf[P * 4];
    dap_random_bytes(buf, sizeof(buf));
    for (int i = 0; i < P; i++) {
        uint32_t r = ((uint32_t)buf[4*i]) | ((uint32_t)buf[4*i+1] << 8)
                   | ((uint32_t)buf[4*i+2] << 16) | ((uint32_t)buf[4*i+3] << 24);
        r %= (2 * (uint32_t)GAMMA1 + 1);
        y[i] = (fq_t)((int32_t)r - GAMMA1);
    }
}

/* ─── Challenge sampling ─────────────────────────────────────────── */

/**
 * Derive a sparse polynomial c with exactly TAU non-zero entries in {-1,+1}
 * from a 32-byte seed using rejection sampling from SHA3 SHAKE output.
 */
static void s_sample_challenge(small_t *c, const uint8_t *seed)
{
    memset(c, 0, P * sizeof(small_t));

    /* SHAKE-256 expand the seed for index+sign sampling */
    uint8_t buf[8 * TAU];
    dap_hash_sha3_256_raw(buf, seed, NTRU_PRIME_SIG_SEED_BYTES);
    /* extend with secondary hash for more bytes */
    uint8_t buf2[32];
    uint8_t seed_ext[64];
    memcpy(seed_ext, seed, 32);
    memcpy(seed_ext + 32, buf, 32);
    dap_hash_sha3_256_raw(buf2, seed_ext, 64);

    uint8_t pool[64];
    memcpy(pool, buf, 32);
    memcpy(pool + 32, buf2, 32);

    int count = 0, idx = 0;
    while (count < TAU && idx + 2 <= 64) {
        uint16_t raw = (uint16_t)pool[idx] | ((uint16_t)pool[idx + 1] << 8);
        idx += 2;
        int pos = raw % P;
        if (c[pos] != 0) continue;
        int sign = (raw >> 15) & 1;
        c[pos] = sign ? -1 : 1;
        count++;
    }

    /* if we didn't get enough, use incremental hashing */
    uint32_t ctr = 1;
    while (count < TAU) {
        uint8_t ctr_buf[36];
        memcpy(ctr_buf, seed, 32);
        ctr_buf[32] = (uint8_t)(ctr & 0xFF);
        ctr_buf[33] = (uint8_t)((ctr >> 8) & 0xFF);
        ctr_buf[34] = (uint8_t)((ctr >> 16) & 0xFF);
        ctr_buf[35] = (uint8_t)((ctr >> 24) & 0xFF);
        ctr++;
        uint8_t h[32];
        dap_hash_sha3_256_raw(h, ctr_buf, 36);
        for (int k = 0; k + 2 <= 32 && count < TAU; k += 2) {
            uint16_t raw = (uint16_t)h[k] | ((uint16_t)h[k + 1] << 8);
            int pos = raw % P;
            if (c[pos] != 0) continue;
            int sign = (raw >> 15) & 1;
            c[pos] = sign ? -1 : 1;
            count++;
        }
    }
}

/* ─── Commitment hash ────────────────────────────────────────────── */

static void s_hash_commitment(uint8_t *c_seed,
                              const uint8_t *msg, size_t msg_len,
                              const fq_t *w)
{
    uint8_t w_enc[NTRU_PRIME_SIG_POLY_BYTES];
    s_fq_encode(w_enc, w);

    size_t total = msg_len + sizeof(w_enc);
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, total);
    if (!l_buf) return;
    memcpy(l_buf, msg, msg_len);
    memcpy(l_buf + msg_len, w_enc, sizeof(w_enc));
    dap_hash_sha3_256_raw(c_seed, l_buf, total);
    DAP_DELETE(l_buf);
}

/* ─── Infinity norm check ────────────────────────────────────────── */

static int s_check_norm(const fq_t *z, int32_t bound)
{
    for (int i = 0; i < P; i++) {
        int32_t v = z[i];
        if (v < 0) v = -v;
        if (v >= bound) return -1;
    }
    return 0;
}

/* ===== Public API ================================================ */

int ntru_prime_sig_keypair(uint8_t *a_pk, uint8_t *a_sk)
{
    small_t f[P], g[P];
    fq_t finv[P], h[P];

    /* Generate invertible ternary f */
    for (int attempts = 0; attempts < 100; attempts++) {
        s_random_small(f);
        if (s_rq_recip(finv, f) == 0)
            goto f_ok;
    }
    return -1;
f_ok:

    /* Generate weight-w polynomial g */
    s_random_weight_w(g);

    /* h = finv · g in R/q, so h·f = g mod q */
    {
        fq_t g_fq[P];
        for (int i = 0; i < P; i++) g_fq[i] = (fq_t)g[i];
        s_rq_mult(h, finv, g_fq);
    }

    /* pk = h || g (encoded) */
    s_fq_encode(a_pk, h);
    {
        fq_t g_fq[P];
        for (int i = 0; i < P; i++) g_fq[i] = (fq_t)g[i];
        s_fq_encode(a_pk + NTRU_PRIME_SIG_POLY_BYTES, g_fq);
    }

    /* sk = SmallEncode(f) || pk */
    s_small_encode(a_sk, f);
    memcpy(a_sk + NTRU_PRIME_SIG_SMALL_BYTES, a_pk, NTRU_PRIME_SIG_PUBLICKEYBYTES);

    return 0;
}

int ntru_prime_sig_sign(uint8_t *a_sig, size_t *a_sig_len,
                       const uint8_t *a_msg, size_t a_msg_len,
                       const uint8_t *a_sk)
{
    small_t f[P];
    fq_t h[P], g_fq[P];

    /* Decode secret key */
    s_small_decode(f, a_sk);
    s_fq_decode(h, a_sk + NTRU_PRIME_SIG_SMALL_BYTES);
    s_fq_decode(g_fq, a_sk + NTRU_PRIME_SIG_SMALL_BYTES + NTRU_PRIME_SIG_POLY_BYTES);

    fq_t y[P], w[P], z[P];
    small_t c[P];
    uint8_t c_seed[NTRU_PRIME_SIG_SEED_BYTES];

    for (int attempt = 0; attempt < 1000; attempt++) {
        /* 1. Sample mask y uniformly from [-γ₁, γ₁] */
        s_random_mask(y);

        /* 2. Commitment: w = h·y mod q */
        {
            fq_t hy[P];
            s_rq_mult(hy, h, y);
            for (int i = 0; i < P; i++)
                w[i] = hy[i];
        }

        /* 3. Challenge hash: c_seed = H(msg || encode(w)) */
        s_hash_commitment(c_seed, a_msg, a_msg_len, w);

        /* 4. Expand challenge to sparse polynomial */
        s_sample_challenge(c, c_seed);

        /* 5. Response: z = y + c·f in R (integer arithmetic, no mod q) */
        {
            fq_t cf[P];
            s_rq_mult_small(cf, y, c);
            /* Actually: z = y + c*f. We need c*f, not c*y.
             * Let's compute c*f properly */
        }
        /* Recompute: z[i] = y[i] + (c*f)[i], where c*f is convolution in R */
        {
            int64_t cf[P + P - 1];
            memset(cf, 0, sizeof(cf));
            for (int i = 0; i < P; i++) {
                if (c[i] == 0) continue;
                for (int j = 0; j < P; j++)
                    cf[i + j] += (int64_t)c[i] * f[j];
            }
            for (int i = P + P - 2; i >= P; i--) {
                cf[i - P + 1] += cf[i];
                cf[i - P]     += cf[i];
            }
            for (int i = 0; i < P; i++)
                z[i] = (fq_t)(y[i] + (int32_t)cf[i]);
        }

        /* 6. Rejection: ||z||∞ < γ₁ - β */
        if (s_check_norm(z, GAMMA1 - BETA) != 0)
            continue;

        /* Success — encode signature */
        memcpy(a_sig, c_seed, NTRU_PRIME_SIG_SEED_BYTES);
        {
            fq_t z_red[P];
            for (int i = 0; i < P; i++)
                z_red[i] = s_fq_freeze(z[i]);
            s_fq_encode(a_sig + NTRU_PRIME_SIG_SEED_BYTES, z_red);
        }
        *a_sig_len = NTRU_PRIME_SIG_BYTES;
        return 0;
    }

    return -1;
}

int ntru_prime_sig_verify(const uint8_t *a_sig, size_t a_sig_len,
                         const uint8_t *a_msg, size_t a_msg_len,
                         const uint8_t *a_pk)
{
    if (a_sig_len != NTRU_PRIME_SIG_BYTES)
        return -1;

    fq_t h[P], g_fq[P], z[P];
    small_t c[P];

    /* Decode public key: h || g */
    s_fq_decode(h, a_pk);
    s_fq_decode(g_fq, a_pk + NTRU_PRIME_SIG_POLY_BYTES);

    /* Decode signature: c_seed || z */
    const uint8_t *c_seed = a_sig;
    s_fq_decode(z, a_sig + NTRU_PRIME_SIG_SEED_BYTES);

    /* Expand challenge */
    s_sample_challenge(c, c_seed);

    /* Check norm bound on z */
    if (s_check_norm(z, GAMMA1 - BETA) != 0)
        return -2;

    /* Reconstruct commitment: w' = h·z - c·g mod q */
    fq_t hz[P], cg[P], w_prime[P];
    s_rq_mult(hz, h, z);
    s_rq_mult_small(cg, g_fq, c);
    for (int i = 0; i < P; i++)
        w_prime[i] = s_fq_freeze((int64_t)hz[i] - cg[i]);

    /* Recompute challenge hash */
    uint8_t c_seed_check[NTRU_PRIME_SIG_SEED_BYTES];
    s_hash_commitment(c_seed_check, a_msg, a_msg_len, w_prime);

    /* Compare */
    int diff = 0;
    for (int i = 0; i < NTRU_PRIME_SIG_SEED_BYTES; i++)
        diff |= c_seed[i] ^ c_seed_check[i];

    return diff ? -3 : 0;
}
