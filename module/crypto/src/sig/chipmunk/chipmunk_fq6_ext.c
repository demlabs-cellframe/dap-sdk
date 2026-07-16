/*
 * CR-11.G Phase 7.7 — MRNG ring-extension arithmetic (G3.1 §9.2).
 * See chipmunk_fq6_ext.h for the algebra and rationale.
 */

#include <errno.h>
#include <string.h>

#include "chipmunk_fq6_ext.h"
#include "chipmunk_poly.h"
#include "dap_hash_shake256.h"

/* g(Y) = Φ₉(Y) = Y⁶ + Y³ + 1 over F_q.  Reduction rule used below:
 *     Y⁶ ≡ −Y³ − 1   (mod g). */

/* ------------------------------------------------------------------ */
/*  F_q scalar arithmetic                                              */
/* ------------------------------------------------------------------ */

static inline int32_t s_fq_norm(int64_t a_v)
{
    int32_t l_r = (int32_t)(a_v % (int64_t)CHIPMUNK_Q);
    if (l_r < 0) { l_r += (int32_t)CHIPMUNK_Q; }
    return l_r;
}

static inline int32_t s_fq_mul(int32_t a_a, int32_t a_b)
{
    return s_fq_norm((int64_t)a_a * (int64_t)a_b);
}

/* Modular inverse mod q via extended Euclid; q is prime so every
 * nonzero residue is invertible.  Returns -1 for a ≡ 0. */
static int32_t s_fq_inv(int32_t a_a)
{
    int32_t l_a = s_fq_norm(a_a);
    if (l_a == 0) { return -1; }
    int64_t l_t = 0, l_newt = 1;
    int64_t l_r = (int64_t)CHIPMUNK_Q, l_newr = l_a;
    while (l_newr != 0) {
        int64_t l_quot = l_r / l_newr;
        int64_t l_tmp;
        l_tmp = l_t - l_quot * l_newt; l_t = l_newt; l_newt = l_tmp;
        l_tmp = l_r - l_quot * l_newr; l_r = l_newr; l_newr = l_tmp;
    }
    if (l_r != 1) { return -1; } /* not coprime — impossible for prime q */
    if (l_t < 0) { l_t += (int64_t)CHIPMUNK_Q; }
    return (int32_t)l_t;
}

/* ------------------------------------------------------------------ */
/*  F_q[Y] polynomial arithmetic for inversion mod Φ₉                  */
/* ------------------------------------------------------------------ */

#define FQX_MAX 16  /* enough to hold intermediate products (deg ≤ ~11) */

static int s_fqx_deg(const int32_t a_p[FQX_MAX])
{
    for (int i = FQX_MAX - 1; i >= 0; --i) {
        if (a_p[i] != 0) { return i; }
    }
    return -1; /* zero polynomial */
}

/* Invert in[0..5] modulo Φ₉ over F_q; on success out[0..5] holds the
 * inverse (deg < 6).  Returns 0, or -EDOM if non-invertible. */
static int s_fqx_inv_mod_phi9(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                              const int32_t a_in[CHIPMUNK_FQ6_EXT_DEG])
{
    int32_t l_r0[FQX_MAX], l_r1[FQX_MAX], l_s0[FQX_MAX], l_s1[FQX_MAX];
    memset(l_r0, 0, sizeof(l_r0));
    memset(l_r1, 0, sizeof(l_r1));
    memset(l_s0, 0, sizeof(l_s0));
    memset(l_s1, 0, sizeof(l_s1));

    /* r0 = Φ₉, r1 = in */
    l_r0[0] = 1; l_r0[3] = 1; l_r0[6] = 1;
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        l_r1[i] = s_fq_norm(a_in[i]);
    }
    /* s0 = 0, s1 = 1 (Bézout coeff for the second operand) */
    l_s1[0] = 1;

    if (s_fqx_deg(l_r1) < 0) { return -EDOM; } /* in == 0 */

    while (s_fqx_deg(l_r1) >= 0) {
        const int l_dr0 = s_fqx_deg(l_r0);
        const int l_dr1 = s_fqx_deg(l_r1);
        /* Single Euclid step: r0 = r0 − q·r1, accumulate q into s. */
        int32_t l_q[FQX_MAX];
        memset(l_q, 0, sizeof(l_q));
        int32_t l_rem[FQX_MAX];
        memcpy(l_rem, l_r0, sizeof(l_rem));
        const int32_t l_lead_inv = s_fq_inv(l_r1[l_dr1]);
        if (l_lead_inv < 0) { return -EDOM; }
        for (int d = l_dr0; d >= l_dr1; --d) {
            if (l_rem[d] == 0) { continue; }
            const int32_t l_coef = s_fq_mul(l_rem[d], l_lead_inv);
            const int l_shift = d - l_dr1;
            l_q[l_shift] = l_coef;
            for (int i = 0; i <= l_dr1; ++i) {
                l_rem[i + l_shift] = s_fq_norm((int64_t)l_rem[i + l_shift]
                                               - (int64_t)l_coef * l_r1[i]);
            }
        }
        /* s_new = s0 − q·s1 */
        int32_t l_snew[FQX_MAX];
        memcpy(l_snew, l_s0, sizeof(l_snew));
        const int l_dq = s_fqx_deg(l_q);
        const int l_ds1 = s_fqx_deg(l_s1);
        if (l_dq >= 0 && l_ds1 >= 0) {
            for (int i = 0; i <= l_dq; ++i) {
                if (l_q[i] == 0) { continue; }
                for (int j = 0; j <= l_ds1; ++j) {
                    l_snew[i + j] = s_fq_norm((int64_t)l_snew[i + j]
                                              - (int64_t)l_q[i] * l_s1[j]);
                }
            }
        }
        /* shift: (r0,r1) = (r1,rem); (s0,s1) = (s1,snew) */
        memcpy(l_r0, l_r1,   sizeof(l_r0));
        memcpy(l_r1, l_rem,  sizeof(l_r1));
        memcpy(l_s0, l_s1,   sizeof(l_s0));
        memcpy(l_s1, l_snew, sizeof(l_s1));
    }

    /* gcd = r0; invertible iff it is a nonzero constant. */
    if (s_fqx_deg(l_r0) != 0) { return -EDOM; }
    const int32_t l_gcd_inv = s_fq_inv(l_r0[0]);
    if (l_gcd_inv < 0) { return -EDOM; }
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        a_out[i] = s_fq_mul(l_s0[i], l_gcd_inv);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  R_q multiplication (time-domain in, time-domain out)               */
/* ------------------------------------------------------------------ */

static int s_rq_mul(chipmunk_poly_t *a_out,
                    const chipmunk_poly_t *a_a,
                    const chipmunk_poly_t *a_b)
{
    chipmunk_poly_t l_a = *a_a, l_b = *a_b;
    int rc = chipmunk_poly_ntt(&l_a);
    if (rc != 0) { return rc; }
    rc = chipmunk_poly_ntt(&l_b);
    if (rc != 0) { return rc; }
    chipmunk_poly_mul_ntt(a_out, &l_a, &l_b);
    return chipmunk_poly_invntt(a_out);
}

/* ------------------------------------------------------------------ */
/*  constructors                                                       */
/* ------------------------------------------------------------------ */

void chipmunk_fq6_ext_zero(chipmunk_fq6_ext_t *a_out)
{
    if (!a_out) { return; }
    memset(a_out, 0, sizeof(*a_out));
}

void chipmunk_fq6_ext_one(chipmunk_fq6_ext_t *a_out)
{
    if (!a_out) { return; }
    memset(a_out, 0, sizeof(*a_out));
    a_out->c[0].coeffs[0] = 1;
}

void chipmunk_fq6_ext_canonicalize(chipmunk_fq6_ext_t *a)
{
    if (!a) {
        return;
    }
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            a->c[j].coeffs[k] = s_fq_norm(a->c[j].coeffs[k]);
        }
    }
}

void chipmunk_fq6_ext_embed(chipmunk_fq6_ext_t *a_out,
                              const chipmunk_poly_t *a_base)
{
    if (!a_out || !a_base) { return; }
    memset(a_out, 0, sizeof(*a_out));
    a_out->c[0] = *a_base;
}

void chipmunk_fq6_ext_project(chipmunk_poly_t *a_out,
                                const chipmunk_fq6_ext_t *a)
{
    if (!a_out || !a) { return; }
    *a_out = a->c[0];
}

bool chipmunk_fq6_ext_is_in_base(const chipmunk_fq6_ext_t *a)
{
    if (!a) { return false; }
    for (int j = 1; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            if (s_fq_norm(a->c[j].coeffs[i]) != 0) { return false; }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  ring operations                                                    */
/* ------------------------------------------------------------------ */

int chipmunk_fq6_ext_add(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_add(&a_out->c[j], &a->c[j], &b->c[j]);
        if (rc != 0) { return rc; }
    }
    return 0;
}

int chipmunk_fq6_ext_sub(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_sub(&a_out->c[j], &a->c[j], &b->c[j]);
        if (rc != 0) { return rc; }
    }
    return 0;
}

/* Reduce a raw degree-≤(2e-2) array of R_q coefficients modulo Φ₉,
 * using Yᵏ (k≥6) = −Y^{k−3} − Y^{k−6}.  Process high→low so that
 * contributions pushed into degrees 6..9 are themselves reduced.
 * On return l_p[e..2e-2] are zeroed and l_p[0..e-1] hold the result. */
static int s_reduce_phi9(chipmunk_poly_t a_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1])
{
    for (int k = 2 * CHIPMUNK_FQ6_EXT_DEG - 2; k >= CHIPMUNK_FQ6_EXT_DEG; --k) {
        int rc = chipmunk_poly_sub(&a_p[k - 3], &a_p[k - 3], &a_p[k]);
        if (rc != 0) { return rc; }
        rc = chipmunk_poly_sub(&a_p[k - 6], &a_p[k - 6], &a_p[k]);
        if (rc != 0) { return rc; }
        memset(&a_p[k], 0, sizeof(a_p[k]));
    }
    return 0;
}

int chipmunk_fq6_ext_mul(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    if (a_out == a || a_out == b) { return -EINVAL; } /* no aliasing */

    /* Raw schoolbook product p[0..10] = Σ a.c[i]·b.c[j] over R_q. */
    chipmunk_poly_t l_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1];
    memset(l_p, 0, sizeof(l_p));
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            chipmunk_poly_t l_term;
            int rc = s_rq_mul(&l_term, &a->c[i], &b->c[j]);
            if (rc != 0) { return rc; }
            rc = chipmunk_poly_add(&l_p[i + j], &l_p[i + j], &l_term);
            if (rc != 0) { return rc; }
        }
    }

    int rc = s_reduce_phi9(l_p);
    if (rc != 0) { return rc; }

    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        a_out->c[j] = l_p[j];
    }
    return 0;
}

int chipmunk_fq6_ext_frobenius(chipmunk_fq6_ext_t *a_out,
                                 const chipmunk_fq6_ext_t *a)
{
    if (!a_out || !a) { return -EINVAL; }
    /* σ(w) = w(Y²) mod Φ₉: place c[j] at degree 2j (a pure shift — no R_q
     * multiplication), then reduce.  Reading all of `a` into l_p first
     * makes out==a aliasing safe. */
    chipmunk_poly_t l_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1];
    memset(l_p, 0, sizeof(l_p));
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        l_p[2 * j] = a->c[j];
    }
    int rc = s_reduce_phi9(l_p);
    if (rc != 0) { return rc; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        a_out->c[j] = l_p[j];
    }
    return 0;
}

int chipmunk_fq6_ext_trace(chipmunk_poly_t *a_out,
                             const chipmunk_fq6_ext_t *a)
{
    if (!a_out || !a) { return -EINVAL; }
    chipmunk_fq6_ext_t l_acc = *a;   /* σ⁰(a) */
    chipmunk_fq6_ext_t l_cur = *a;
    for (int i = 1; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        int rc = chipmunk_fq6_ext_frobenius(&l_cur, &l_cur); /* σⁱ(a) */
        if (rc != 0) { return rc; }
        rc = chipmunk_fq6_ext_add(&l_acc, &l_acc, &l_cur);
        if (rc != 0) { return rc; }
    }
    /* The sum is σ-fixed ⇒ lies in the base ring; project to R_q. */
    *a_out = l_acc.c[0];
    return 0;
}

/* ------------------------------------------------------------------ */
/*  scalar (F_{q⁶}) sub-API                                            */
/* ------------------------------------------------------------------ */

void chipmunk_fq6_ext_scalar_set(chipmunk_fq6_ext_t *a_out,
                                   const int32_t a_coords[CHIPMUNK_FQ6_EXT_DEG])
{
    if (!a_out || !a_coords) { return; }
    memset(a_out, 0, sizeof(*a_out));
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        a_out->c[j].coeffs[0] = s_fq_norm(a_coords[j]);
    }
}

int chipmunk_fq6_ext_scalar_get(int32_t a_coords_out[CHIPMUNK_FQ6_EXT_DEG],
                                  const chipmunk_fq6_ext_t *a)
{
    if (!a_coords_out || !a) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (int i = 1; i < CHIPMUNK_N; ++i) {
            if (s_fq_norm(a->c[j].coeffs[i]) != 0) { return -EINVAL; }
        }
        a_coords_out[j] = s_fq_norm(a->c[j].coeffs[0]);
    }
    return 0;
}

int chipmunk_fq6_ext_scalar_invert(chipmunk_fq6_ext_t *a_out,
                                     const chipmunk_fq6_ext_t *a)
{
    if (!a_out || !a) { return -EINVAL; }
    int32_t l_coords[CHIPMUNK_FQ6_EXT_DEG];
    int rc = chipmunk_fq6_ext_scalar_get(l_coords, a);
    if (rc != 0) { return rc; }
    int32_t l_inv[CHIPMUNK_FQ6_EXT_DEG];
    rc = s_fqx_inv_mod_phi9(l_inv, l_coords);
    if (rc != 0) { return rc; }
    chipmunk_fq6_ext_scalar_set(a_out, l_inv);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Fiat-Shamir challenge sampler over the subtractive set S = F_{q⁶}   */
/* ------------------------------------------------------------------ */

#define FQ6_EXT_FS_DOMAIN "MRNG-G3.1-fold-challenge-v1"

typedef struct ext_xof_reader {
    uint64_t st[25];
    uint8_t block[DAP_SHAKE256_RATE];
    size_t pos;
    size_t avail;
} ext_xof_reader_t;

static uint8_t s_xof_u8(ext_xof_reader_t *a_r)
{
    if (a_r->pos == a_r->avail) {
        dap_hash_shake256_squeezeblocks(a_r->block, 1u, a_r->st);
        a_r->pos = 0;
        a_r->avail = sizeof(a_r->block);
    }
    return a_r->block[a_r->pos++];
}

/* One uniform F_q draw via 22-bit rejection sampling (q < 2²²). */
static int32_t s_xof_fq(ext_xof_reader_t *a_r)
{
    for (;;) {
        uint32_t v = (uint32_t)s_xof_u8(a_r);
        v |= (uint32_t)s_xof_u8(a_r) << 8;
        v |= (uint32_t)s_xof_u8(a_r) << 16;
        v &= 0x3FFFFFu; /* low 22 bits */
        if (v < (uint32_t)CHIPMUNK_Q) {
            return (int32_t)v;
        }
    }
}

int chipmunk_fq6_ext_sample_challenge(chipmunk_fq6_ext_t *a_out,
                                        const uint8_t a_fs_hash[32],
                                        uint32_t a_counter)
{
    if (!a_out || !a_fs_hash) { return -EINVAL; }

    uint8_t l_seed[sizeof(FQ6_EXT_FS_DOMAIN) - 1 + 32 + 4];
    size_t l_off = 0;
    memcpy(l_seed + l_off, FQ6_EXT_FS_DOMAIN, sizeof(FQ6_EXT_FS_DOMAIN) - 1);
    l_off += sizeof(FQ6_EXT_FS_DOMAIN) - 1;
    memcpy(l_seed + l_off, a_fs_hash, 32);
    l_off += 32;
    l_seed[l_off++] = (uint8_t)(a_counter & 0xFFu);
    l_seed[l_off++] = (uint8_t)((a_counter >> 8) & 0xFFu);
    l_seed[l_off++] = (uint8_t)((a_counter >> 16) & 0xFFu);
    l_seed[l_off++] = (uint8_t)((a_counter >> 24) & 0xFFu);

    ext_xof_reader_t l_r;
    memset(&l_r, 0, sizeof(l_r));
    dap_hash_shake256_absorb(l_r.st, l_seed, l_off);

    /* Draw six F_q coordinates; reject the all-zero scalar (keep drawing
     * from the same deterministic stream so the verifier reproduces it). */
    int32_t l_coords[CHIPMUNK_FQ6_EXT_DEG];
    for (;;) {
        bool l_all_zero = true;
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_coords[j] = s_xof_fq(&l_r);
            if (l_coords[j] != 0) { l_all_zero = false; }
        }
        if (!l_all_zero) { break; }
    }
    chipmunk_fq6_ext_scalar_set(a_out, l_coords);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  general inversion (per-slot F_{q⁶})                                */
/* ------------------------------------------------------------------ */

int chipmunk_fq6_ext_invert(chipmunk_fq6_ext_t *a_out,
                              const chipmunk_fq6_ext_t *a)
{
    if (!a_out || !a) { return -EINVAL; }

    /* NTT each Y-coefficient → 512 slots, each slot an F_{q⁶} element. */
    chipmunk_poly_t l_ntt[CHIPMUNK_FQ6_EXT_DEG];
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        l_ntt[j] = a->c[j];
        int rc = chipmunk_poly_ntt(&l_ntt[j]);
        if (rc != 0) { return rc; }
    }

    chipmunk_poly_t l_res[CHIPMUNK_FQ6_EXT_DEG];
    memset(l_res, 0, sizeof(l_res));
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_slot[CHIPMUNK_FQ6_EXT_DEG];
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_slot[j] = l_ntt[j].coeffs[i];
        }
        int32_t l_inv[CHIPMUNK_FQ6_EXT_DEG];
        int rc = s_fqx_inv_mod_phi9(l_inv, l_slot);
        if (rc != 0) { return rc; } /* -EDOM: some slot non-invertible */
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_res[j].coeffs[i] = l_inv[j];
        }
    }

    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_invntt(&l_res[j]);
        if (rc != 0) { return rc; }
        a_out->c[j] = l_res[j];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Rabin irreducibility self-check for Φ₉ over F_q                    */
/* ------------------------------------------------------------------ */

/* Multiply two deg-<6 polys mod Φ₉ over F_q (scalar coefficients). */
static void s_phi9_mul(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                       const int32_t a_a[CHIPMUNK_FQ6_EXT_DEG],
                       const int32_t a_b[CHIPMUNK_FQ6_EXT_DEG])
{
    int64_t l_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1] = {0};
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_p[i + j] = (l_p[i + j] + (int64_t)a_a[i] * a_b[j]) % (int64_t)CHIPMUNK_Q;
        }
    }
    for (int k = 2 * CHIPMUNK_FQ6_EXT_DEG - 2; k >= CHIPMUNK_FQ6_EXT_DEG; --k) {
        int64_t l_v = l_p[k];
        l_p[k - 3] = (l_p[k - 3] - l_v) % (int64_t)CHIPMUNK_Q;
        l_p[k - 6] = (l_p[k - 6] - l_v) % (int64_t)CHIPMUNK_Q;
        l_p[k] = 0;
    }
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        a_out[i] = s_fq_norm(l_p[i]);
    }
}

/* base^e mod Φ₉ over F_q (square-and-multiply). */
static void s_phi9_powmod(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                          const int32_t a_base[CHIPMUNK_FQ6_EXT_DEG],
                          uint64_t a_exp)
{
    int32_t l_res[CHIPMUNK_FQ6_EXT_DEG] = {0};
    l_res[0] = 1;
    int32_t l_b[CHIPMUNK_FQ6_EXT_DEG];
    memcpy(l_b, a_base, sizeof(l_b));
    while (a_exp > 0) {
        if (a_exp & 1ULL) {
            int32_t l_t[CHIPMUNK_FQ6_EXT_DEG];
            s_phi9_mul(l_t, l_res, l_b);
            memcpy(l_res, l_t, sizeof(l_res));
        }
        int32_t l_sq[CHIPMUNK_FQ6_EXT_DEG];
        s_phi9_mul(l_sq, l_b, l_b);
        memcpy(l_b, l_sq, sizeof(l_b));
        a_exp >>= 1;
    }
    memcpy(a_out, l_res, sizeof(int32_t) * CHIPMUNK_FQ6_EXT_DEG);
}

/* gcd over F_q[Y] of two deg-<=6 polys (given in FQX_MAX buffers);
 * returns degree of gcd. */
static int s_fqx_gcd_deg(const int32_t a_a[FQX_MAX], const int32_t a_b[FQX_MAX])
{
    int32_t l_r0[FQX_MAX], l_r1[FQX_MAX];
    memcpy(l_r0, a_a, sizeof(l_r0));
    memcpy(l_r1, a_b, sizeof(l_r1));
    while (s_fqx_deg(l_r1) >= 0) {
        const int l_d0 = s_fqx_deg(l_r0);
        const int l_d1 = s_fqx_deg(l_r1);
        const int32_t l_lead_inv = s_fq_inv(l_r1[l_d1]);
        int32_t l_rem[FQX_MAX];
        memcpy(l_rem, l_r0, sizeof(l_rem));
        for (int d = l_d0; d >= l_d1; --d) {
            if (l_rem[d] == 0) { continue; }
            const int32_t l_coef = s_fq_mul(l_rem[d], l_lead_inv);
            const int l_shift = d - l_d1;
            for (int i = 0; i <= l_d1; ++i) {
                l_rem[i + l_shift] = s_fq_norm((int64_t)l_rem[i + l_shift]
                                               - (int64_t)l_coef * l_r1[i]);
            }
        }
        memcpy(l_r0, l_r1,  sizeof(l_r0));
        memcpy(l_r1, l_rem, sizeof(l_r1));
    }
    return s_fqx_deg(l_r0);
}

bool chipmunk_fq6_ext_modulus_is_irreducible(void)
{
    /* Rabin's test for g = Φ₉ (deg n = 6) over F_q:
     *   1. Y^{qⁿ} ≡ Y           (mod g)
     *   2. for every prime p | n:  gcd(Y^{q^{n/p}} − Y, g) = 1
     * n = 6, prime divisors {2, 3} → check exponents n/2 = 3, n/3 = 2. */
    const int l_n = CHIPMUNK_FQ6_EXT_DEG;
    int32_t l_y[CHIPMUNK_FQ6_EXT_DEG] = {0};
    l_y[1] = 1; /* the polynomial Y */

    /* helper: compute Y^{q^m} mod g by iterating x → x^q, m times. */
    /* Condition 1: m = n. */
    int32_t l_cur[CHIPMUNK_FQ6_EXT_DEG];
    memcpy(l_cur, l_y, sizeof(l_cur));
    for (int m = 1; m <= l_n; ++m) {
        int32_t l_nx[CHIPMUNK_FQ6_EXT_DEG];
        s_phi9_powmod(l_nx, l_cur, (uint64_t)CHIPMUNK_Q);
        memcpy(l_cur, l_nx, sizeof(l_cur));
        if (m == l_n / 2 || m == l_n / 3) {
            /* gcd(Y^{q^m} − Y, g) must be 1. */
            int32_t l_diff[FQX_MAX];
            memset(l_diff, 0, sizeof(l_diff));
            for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) { l_diff[i] = l_cur[i]; }
            l_diff[1] = s_fq_norm((int64_t)l_diff[1] - 1);
            int32_t l_g[FQX_MAX];
            memset(l_g, 0, sizeof(l_g));
            l_g[0] = 1; l_g[3] = 1; l_g[6] = 1;
            if (s_fqx_gcd_deg(l_g, l_diff) != 0) { return false; }
        }
    }
    /* Condition 1: Y^{qⁿ} ≡ Y. */
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        if (s_fq_norm(l_cur[i]) != s_fq_norm(l_y[i])) { return false; }
    }
    return true;
}
