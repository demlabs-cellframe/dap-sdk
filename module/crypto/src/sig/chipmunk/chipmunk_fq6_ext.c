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

#define FQX_MAX 16  /* enough to hold intermediate products (deg ≤ ~11) */

typedef struct ext_xof_reader {
    uint64_t st[25];
    uint8_t block[DAP_SHAKE256_RATE];
    size_t pos;
    size_t avail;
} ext_xof_reader_t;

/* ------------------------------------------------------------------ */
/*  F_q scalar arithmetic                                              */
/* ------------------------------------------------------------------ */

/* All F_q arithmetic operates on an arbitrary prime q. */
static inline int32_t s_fq_norm_q(int64_t a_v, uint64_t q)
{
    int32_t l_r = (int32_t)(a_v % (int64_t)q);
    if (l_r < 0) { l_r += (int32_t)q; }
    return l_r;
}

static inline int32_t s_fq_mul_q(int32_t a_a, int32_t a_b, uint64_t q)
{
    return s_fq_norm_q((int64_t)a_a * (int64_t)a_b, q);
}

/* Modular inverse mod arbitrary prime q via extended Euclid.
 * Returns -1 for a ≡ 0. */
static int32_t s_fq_inv_q(int32_t a_a, uint64_t q)
{
    int32_t l_a = s_fq_norm_q(a_a, q);
    if (l_a == 0) { return -1; }
    int64_t l_t = 0, l_newt = 1;
    int64_t l_r = (int64_t)q, l_newr = l_a;
    while (l_newr != 0) {
        int64_t l_quot = l_r / l_newr;
        int64_t l_tmp;
        l_tmp = l_t - l_quot * l_newt; l_t = l_newt; l_newt = l_tmp;
        l_tmp = l_r - l_quot * l_newr; l_r = l_newr; l_newr = l_tmp;
    }
    if (l_r != 1) { return -1; }
    if (l_t < 0) { l_t += (int64_t)q; }
    return (int32_t)l_t;
}

/* ------------------------------------------------------------------ */
/*  F_q[Y] polynomial arithmetic for inversion mod Φ₉                  */
/* ------------------------------------------------------------------ */

static int s_fqx_deg(const int32_t a_p[FQX_MAX])
{
    for (int i = FQX_MAX - 1; i >= 0; --i) {
        if (a_p[i] != 0) { return i; }
    }
    return -1; /* zero polynomial */
}

/* Parameterized version: invert in[0..5] modulo Φ₉ over F_q. */
static int s_fqx_inv_mod_phi9_q(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                                const int32_t a_in[CHIPMUNK_FQ6_EXT_DEG],
                                uint64_t q)
{
    int32_t l_r0[FQX_MAX], l_r1[FQX_MAX], l_s0[FQX_MAX], l_s1[FQX_MAX];
    memset(l_r0, 0, sizeof(l_r0));
    memset(l_r1, 0, sizeof(l_r1));
    memset(l_s0, 0, sizeof(l_s0));
    memset(l_s1, 0, sizeof(l_s1));

    /* r0 = Φ₉, r1 = in */
    l_r0[0] = 1; l_r0[3] = 1; l_r0[6] = 1;
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        l_r1[i] = s_fq_norm_q(a_in[i], q);
    }
    /* s0 = 0, s1 = 1 (Bézout coeff for the second operand) */
    l_s1[0] = 1;

    if (s_fqx_deg(l_r1) < 0) { return -EDOM; } /* in == 0 */

    while (s_fqx_deg(l_r1) >= 0) {
        const int l_dr0 = s_fqx_deg(l_r0);
        const int l_dr1 = s_fqx_deg(l_r1);
        /* Single Euclid step: r0 = r0 − q·r1, accumulate q into s. */
        int32_t l_qcoef[FQX_MAX];
        memset(l_qcoef, 0, sizeof(l_qcoef));
        int32_t l_rem[FQX_MAX];
        memcpy(l_rem, l_r0, sizeof(l_rem));
        const int32_t l_lead_inv = s_fq_inv_q(l_r1[l_dr1], q);
        if (l_lead_inv < 0) { return -EDOM; }
        for (int d = l_dr0; d >= l_dr1; --d) {
            if (l_rem[d] == 0) { continue; }
            const int32_t l_coef = s_fq_mul_q(l_rem[d], l_lead_inv, q);
            const int l_shift = d - l_dr1;
            l_qcoef[l_shift] = l_coef;
            for (int i = 0; i <= l_dr1; ++i) {
                l_rem[i + l_shift] = s_fq_norm_q((int64_t)l_rem[i + l_shift]
                                                  - (int64_t)l_coef * l_r1[i], q);
            }
        }
        /* s_new = s0 − q·s1 */
        int32_t l_snew[FQX_MAX];
        memcpy(l_snew, l_s0, sizeof(l_snew));
        const int l_dq = s_fqx_deg(l_qcoef);
        const int l_ds1 = s_fqx_deg(l_s1);
        if (l_dq >= 0 && l_ds1 >= 0) {
            for (int i = 0; i <= l_dq; ++i) {
                if (l_qcoef[i] == 0) { continue; }
                for (int j = 0; j <= l_ds1; ++j) {
                    l_snew[i + j] = s_fq_norm_q((int64_t)l_snew[i + j]
                                                 - (int64_t)l_qcoef[i] * l_s1[j], q);
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
    const int32_t l_gcd_inv = s_fq_inv_q(l_r0[0], q);
    if (l_gcd_inv < 0) { return -EDOM; }
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        a_out[i] = s_fq_mul_q(l_s0[i], l_gcd_inv, q);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  R_q multiplication (time-domain in, time-domain out)               */
/* ------------------------------------------------------------------ */

/* Forward declarations for per-slot F_{q⁶} arithmetic */
static void s_phi9_mul_q(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                         const int32_t a_a[CHIPMUNK_FQ6_EXT_DEG],
                         const int32_t a_b[CHIPMUNK_FQ6_EXT_DEG],
                         uint64_t q);

static int s_rq_mul_q(chipmunk_poly_t *a_out,
                      const chipmunk_poly_t *a_a,
                      const chipmunk_poly_t *a_b,
                      uint64_t q)
{
    chipmunk_poly_t l_a = *a_a, l_b = *a_b;
    int rc = chipmunk_poly_ntt(&l_a);
    if (rc != 0) { return rc; }
    rc = chipmunk_poly_ntt(&l_b);
    if (rc != 0) { return rc; }
    chipmunk_poly_mul_ntt_q(a_out, &l_a, &l_b, q);
    return chipmunk_poly_invntt(a_out);
}

static int s_rq_mul(chipmunk_poly_t *a_out,
                    const chipmunk_poly_t *a_a,
                    const chipmunk_poly_t *a_b)
{
    return s_rq_mul_q(a_out, a_a, a_b, (uint64_t)CHIPMUNK_Q);
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

void chipmunk_fq6_ext_canonicalize_q(chipmunk_fq6_ext_t *a, uint64_t q)
{
    if (!a) {
        return;
    }
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            a->c[j].coeffs[k] = s_fq_norm_q(a->c[j].coeffs[k], q);
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

bool chipmunk_fq6_ext_is_in_base_q(const chipmunk_fq6_ext_t *a, uint64_t q)
{
    if (!a) { return false; }
    for (int j = 1; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (int i = 0; i < CHIPMUNK_N; ++i) {
            if (s_fq_norm_q(a->c[j].coeffs[i], q) != 0) { return false; }
        }
    }
    return true;
}

/* ------------------------------------------------------------------ */
/*  ring operations                                                    */
/* ------------------------------------------------------------------ */

int chipmunk_fq6_ext_add_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_add_q(&a_out->c[j], &a->c[j], &b->c[j], q);
        if (rc != 0) { return rc; }
    }
    return 0;
}

int chipmunk_fq6_ext_add(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    return chipmunk_fq6_ext_add_q(a_out, a, b, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_fq6_ext_sub_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_sub_q(&a_out->c[j], &a->c[j], &b->c[j], q);
        if (rc != 0) { return rc; }
    }
    return 0;
}

int chipmunk_fq6_ext_sub(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    return chipmunk_fq6_ext_sub_q(a_out, a, b, (uint64_t)CHIPMUNK_Q);
}

/* Reduce a raw degree-≤(2e-2) array of R_q coefficients modulo Φ₉,
 * using Yᵏ (k≥6) = −Y^{k−3} − Y^{k−6}.  Process high→low so that
 * contributions pushed into degrees 6..9 are themselves reduced.
 * On return l_p[e..2e-2] are zeroed and l_p[0..e-1] hold the result. */
static int s_reduce_phi9_q(chipmunk_poly_t a_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1],
                            uint64_t q)
{
    for (int k = 2 * CHIPMUNK_FQ6_EXT_DEG - 2; k >= CHIPMUNK_FQ6_EXT_DEG; --k) {
        int rc = chipmunk_poly_sub_q(&a_p[k - 3], &a_p[k - 3], &a_p[k], q);
        if (rc != 0) { return rc; }
        rc = chipmunk_poly_sub_q(&a_p[k - 6], &a_p[k - 6], &a_p[k], q);
        if (rc != 0) { return rc; }
        memset(&a_p[k], 0, sizeof(a_p[k]));
    }
    return 0;
}

static int s_reduce_phi9(chipmunk_poly_t a_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1])
{
    return s_reduce_phi9_q(a_p, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_fq6_ext_mul_q(chipmunk_fq6_ext_t *a_out,
                             const chipmunk_fq6_ext_t *a,
                             const chipmunk_fq6_ext_t *b,
                             uint64_t q)
{
    if (!a_out || !a || !b) { return -EINVAL; }
    if (a_out == a || a_out == b) { return -EINVAL; } /* no aliasing */

    /* NTT-native F_{q⁶} multiplication via per-slot evaluation.
     *
     * Since R_q = Z_q[X]/(X^512+1) fully splits into 512 slots under
     * CHIPMUNK_Q (2-adicity ≥ 9), each NTT slot is an independent F_q.
     * We NTT all 6 Y-coefficients of both operands, then for each of
     * the 512 slots perform a scalar F_{q⁶} = F_q[Y]/(Φ₉) multiply.
     *
     * Cost: 12 forward NTTs + 512 scalar F_{q⁶} muls + 6 invNTTs
     * vs old: 36 full R_q round-trips (72 forward + 36 inverse NTTs).
     * Net savings: ~60% fewer NTT operations.
     */
    chipmunk_poly_t l_a_ntt[CHIPMUNK_FQ6_EXT_DEG];
    chipmunk_poly_t l_b_ntt[CHIPMUNK_FQ6_EXT_DEG];

    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        l_a_ntt[j] = a->c[j];
        int rc = chipmunk_poly_ntt(&l_a_ntt[j]);
        if (rc != 0) { return rc; }
        l_b_ntt[j] = b->c[j];
        rc = chipmunk_poly_ntt(&l_b_ntt[j]);
        if (rc != 0) { return rc; }
    }

    chipmunk_poly_t l_res[CHIPMUNK_FQ6_EXT_DEG];
    memset(l_res, 0, sizeof(l_res));

    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_slot_a[CHIPMUNK_FQ6_EXT_DEG];
        int32_t l_slot_b[CHIPMUNK_FQ6_EXT_DEG];
        int32_t l_slot_r[CHIPMUNK_FQ6_EXT_DEG];

        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_slot_a[j] = l_a_ntt[j].coeffs[i];
            l_slot_b[j] = l_b_ntt[j].coeffs[i];
        }

        /* Scalar F_{q⁶} multiply mod Φ₉ = Y⁶ + Y³ + 1 */
        s_phi9_mul_q(l_slot_r, l_slot_a, l_slot_b, q);

        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_res[j].coeffs[i] = l_slot_r[j];
        }
    }

    /* invNTT each result component back to time domain */
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc = chipmunk_poly_invntt(&l_res[j]);
        if (rc != 0) { return rc; }
        a_out->c[j] = l_res[j];
    }
    return 0;
}

int chipmunk_fq6_ext_mul(chipmunk_fq6_ext_t *a_out,
                           const chipmunk_fq6_ext_t *a,
                           const chipmunk_fq6_ext_t *b)
{
    return chipmunk_fq6_ext_mul_q(a_out, a, b, (uint64_t)CHIPMUNK_Q);
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

void chipmunk_fq6_ext_scalar_set_q(chipmunk_fq6_ext_t *a_out,
                                     const int32_t a_coords[CHIPMUNK_FQ6_EXT_DEG],
                                     uint64_t q)
{
    if (!a_out || !a_coords) { return; }
    memset(a_out, 0, sizeof(*a_out));
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        a_out->c[j].coeffs[0] = s_fq_norm_q(a_coords[j], q);
    }
}

int chipmunk_fq6_ext_scalar_get_q(int32_t a_coords_out[CHIPMUNK_FQ6_EXT_DEG],
                                    const chipmunk_fq6_ext_t *a, uint64_t q)
{
    if (!a_coords_out || !a) { return -EINVAL; }
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (int i = 1; i < CHIPMUNK_N; ++i) {
            if (s_fq_norm_q(a->c[j].coeffs[i], q) != 0) { return -EINVAL; }
        }
        a_coords_out[j] = s_fq_norm_q(a->c[j].coeffs[0], q);
    }
    return 0;
}

int chipmunk_fq6_ext_scalar_invert_q(chipmunk_fq6_ext_t *a_out,
                                       const chipmunk_fq6_ext_t *a, uint64_t q)
{
    if (!a_out || !a) { return -EINVAL; }
    int32_t l_coords[CHIPMUNK_FQ6_EXT_DEG];
    int rc = chipmunk_fq6_ext_scalar_get_q(l_coords, a, q);
    if (rc != 0) { return rc; }
    int32_t l_inv[CHIPMUNK_FQ6_EXT_DEG];
    rc = s_fqx_inv_mod_phi9_q(l_inv, l_coords, q);
    if (rc != 0) { return rc; }
    chipmunk_fq6_ext_scalar_set_q(a_out, l_inv, q);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Fiat-Shamir challenge sampler over the subtractive set S = F_{q⁶}   */
/* ------------------------------------------------------------------ */

#define FQ6_EXT_FS_DOMAIN "MRNG-G3.1-fold-challenge-v1"

static uint8_t s_xof_u8(ext_xof_reader_t *a_r)
{
    if (a_r->pos == a_r->avail) {
        dap_hash_shake256_squeezeblocks(a_r->block, 1u, a_r->st);
        a_r->pos = 0;
        a_r->avail = sizeof(a_r->block);
    }
    return a_r->block[a_r->pos++];
}

/* Parameterized rejection sample: uniform F_q element. */
static int32_t s_xof_fq_q(ext_xof_reader_t *a_r, uint64_t q)
{
    for (;;) {
        uint32_t v = (uint32_t)s_xof_u8(a_r);
        v |= (uint32_t)s_xof_u8(a_r) << 8;
        v |= (uint32_t)s_xof_u8(a_r) << 16;
        v &= 0x3FFFFFu; /* low 22 bits — valid for q < 2^22 */
        if (v < (uint32_t)q) {
            return (int32_t)v;
        }
    }
}

/* Parameterized challenge sampler over S = F_{q^6}\{0} for arbitrary q. */
int chipmunk_fq6_ext_sample_challenge_q(chipmunk_fq6_ext_t *a_out,
                                          const uint8_t a_fs_hash[32],
                                          uint32_t a_counter, uint64_t q)
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
            l_coords[j] = s_xof_fq_q(&l_r, q);
            if (l_coords[j] != 0) { l_all_zero = false; }
        }
        if (!l_all_zero) { break; }
    }
    chipmunk_fq6_ext_scalar_set_q(a_out, l_coords, q);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  general inversion (per-slot F_{q⁶})                                */
/* ------------------------------------------------------------------ */

int chipmunk_fq6_ext_invert_q(chipmunk_fq6_ext_t *a_out,
                                const chipmunk_fq6_ext_t *a,
                                uint64_t q,
                                const chipmunk_ntt_ctx_t *ntt_ctx)
{
    if (!a_out || !a) { return -EINVAL; }

    /* NTT each Y-coefficient → 512 slots, each slot an F_{q^6} element. */
    chipmunk_poly_t l_ntt[CHIPMUNK_FQ6_EXT_DEG];
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        l_ntt[j] = a->c[j];
        int rc;
        if (ntt_ctx) {
            rc = chipmunk_poly_ntt_q(&l_ntt[j], ntt_ctx);
        } else {
            rc = chipmunk_poly_ntt(&l_ntt[j]);
        }
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
        int rc = s_fqx_inv_mod_phi9_q(l_inv, l_slot, q);
        if (rc != 0) { return rc; }
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_res[j].coeffs[i] = l_inv[j];
        }
    }

    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int rc;
        if (ntt_ctx) {
            rc = chipmunk_poly_invntt_q(&l_res[j], ntt_ctx);
        } else {
            rc = chipmunk_poly_invntt(&l_res[j]);
        }
        if (rc != 0) { return rc; }
        a_out->c[j] = l_res[j];
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Rabin irreducibility self-check for Φ₉ over F_q                    */
/* ------------------------------------------------------------------ */

/* Multiply two deg-<6 polys mod Φ₉ over F_q (scalar coefficients). */
static void s_phi9_mul_q(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                         const int32_t a_a[CHIPMUNK_FQ6_EXT_DEG],
                         const int32_t a_b[CHIPMUNK_FQ6_EXT_DEG],
                         uint64_t q)
{
    int64_t l_p[2 * CHIPMUNK_FQ6_EXT_DEG - 1] = {0};
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
            l_p[i + j] = (l_p[i + j] + (int64_t)a_a[i] * a_b[j]) % (int64_t)q;
        }
    }
    for (int k = 2 * CHIPMUNK_FQ6_EXT_DEG - 2; k >= CHIPMUNK_FQ6_EXT_DEG; --k) {
        int64_t l_v = l_p[k];
        l_p[k - 3] = (l_p[k - 3] - l_v) % (int64_t)q;
        l_p[k - 6] = (l_p[k - 6] - l_v) % (int64_t)q;
        l_p[k] = 0;
    }
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        a_out[i] = s_fq_norm_q(l_p[i], q);
    }
}

/* base^e mod Φ₉ over F_q (square-and-multiply). */
static void s_phi9_powmod_q(int32_t a_out[CHIPMUNK_FQ6_EXT_DEG],
                            const int32_t a_base[CHIPMUNK_FQ6_EXT_DEG],
                            uint64_t a_exp, uint64_t q)
{
    int32_t l_res[CHIPMUNK_FQ6_EXT_DEG] = {0};
    l_res[0] = 1;
    int32_t l_b[CHIPMUNK_FQ6_EXT_DEG];
    memcpy(l_b, a_base, sizeof(l_b));
    while (a_exp > 0) {
        if (a_exp & 1ULL) {
            int32_t l_t[CHIPMUNK_FQ6_EXT_DEG];
            s_phi9_mul_q(l_t, l_res, l_b, q);
            memcpy(l_res, l_t, sizeof(l_res));
        }
        int32_t l_sq[CHIPMUNK_FQ6_EXT_DEG];
        s_phi9_mul_q(l_sq, l_b, l_b, q);
        memcpy(l_b, l_sq, sizeof(l_b));
        a_exp >>= 1;
    }
    memcpy(a_out, l_res, sizeof(int32_t) * CHIPMUNK_FQ6_EXT_DEG);
}

/* gcd over F_q[Y] of two deg-<=6 polys (given in FQX_MAX buffers);
 * returns degree of gcd. */
static int s_fqx_gcd_deg_q(const int32_t a_a[FQX_MAX], const int32_t a_b[FQX_MAX],
                           uint64_t q)
{
    int32_t l_r0[FQX_MAX], l_r1[FQX_MAX];
    memcpy(l_r0, a_a, sizeof(l_r0));
    memcpy(l_r1, a_b, sizeof(l_r1));
    while (s_fqx_deg(l_r1) >= 0) {
        const int l_d0 = s_fqx_deg(l_r0);
        const int l_d1 = s_fqx_deg(l_r1);
        const int32_t l_lead_inv = s_fq_inv_q(l_r1[l_d1], q);
        int32_t l_rem[FQX_MAX];
        memcpy(l_rem, l_r0, sizeof(l_rem));
        for (int d = l_d0; d >= l_d1; --d) {
            if (l_rem[d] == 0) { continue; }
            const int32_t l_coef = s_fq_mul_q(l_rem[d], l_lead_inv, q);
            const int l_shift = d - l_d1;
            for (int i = 0; i <= l_d1; ++i) {
                l_rem[i + l_shift] = s_fq_norm_q((int64_t)l_rem[i + l_shift]
                                                  - (int64_t)l_coef * l_r1[i], q);
            }
        }
        memcpy(l_r0, l_r1,  sizeof(l_r0));
        memcpy(l_r1, l_rem, sizeof(l_r1));
    }
    return s_fqx_deg(l_r0);
}

/* Parameterized Rabin irreducibility test for Φ₉ over F_q. */
bool chipmunk_fq6_ext_modulus_is_irreducible_q(uint64_t q)
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
        s_phi9_powmod_q(l_nx, l_cur, (uint64_t)q, q);
        memcpy(l_cur, l_nx, sizeof(l_cur));
        if (m == l_n / 2 || m == l_n / 3) {
            /* gcd(Y^{q^m} − Y, g) must be 1. */
            int32_t l_diff[FQX_MAX];
            memset(l_diff, 0, sizeof(l_diff));
            for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) { l_diff[i] = l_cur[i]; }
            l_diff[1] = s_fq_norm_q((int64_t)l_diff[1] - 1, q);
            int32_t l_g[FQX_MAX];
            memset(l_g, 0, sizeof(l_g));
            l_g[0] = 1; l_g[3] = 1; l_g[6] = 1;
            if (s_fqx_gcd_deg_q(l_g, l_diff, q) != 0) { return false; }
        }
    }
    /* Condition 1: Y^{qⁿ} ≡ Y. */
    for (int i = 0; i < CHIPMUNK_FQ6_EXT_DEG; ++i) {
        if (s_fq_norm_q(l_cur[i], q) != s_fq_norm_q(l_y[i], q)) { return false; }
    }
    return true;
}
