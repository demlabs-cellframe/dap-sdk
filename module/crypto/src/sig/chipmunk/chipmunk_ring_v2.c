/*
 * Chipmunk Ring V2 — non-interactive lattice ring signature.
 *
 * Based on LoTRS binary ring proof (RS) with Fiat-Shamir.
 * Single-signer, non-interactive, O(N) signature size.
 */

#include "chipmunk_ring_v2.h"
#include "lotrs_sample.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "chipmunk_ring_v2"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"

/* --- Cleanup --- */

void chipmunk_ring_v2_keypair_free(chipmunk_ring_v2_keypair_t *a_kp)
{
    if (a_kp) {
        lotrs_polyvec_free(&a_kp->pk.a_hat);
        lotrs_polyvec_free(&a_kp->sk.s);
        memset(a_kp, 0, sizeof(*a_kp));
    }
}

void chipmunk_ring_v2_ring_free(chipmunk_ring_v2_ring_t *a_ring)
{
    if (a_ring && a_ring->pks) {
        for (uint32_t i = 0u; i < a_ring->N; ++i) {
            lotrs_polyvec_free(&a_ring->pks[i].a_hat);
        }
        DAP_DELETE(a_ring->pks);
        a_ring->pks = NULL;
        a_ring->N = 0;
    }
}

void chipmunk_ring_v2_sig_free(chipmunk_ring_v2_sig_t *a_sig)
{
    if (a_sig && a_sig->data) {
        dap_memwipe(a_sig->data, a_sig->len);
        DAP_DELETE(a_sig->data);
        a_sig->data = NULL;
        a_sig->len = 0;
    }
}

size_t chipmunk_ring_v2_sig_bytes(const lotrs_params_t *a_par)
{
    /* Header + w (k polys) + c (1 poly) + z (l+k polys). */
    size_t l_poly = lotrs_poly_bytes(a_par);
    return CHIPMUNK_RING_V2_HEADER_BYTES
         + (size_t)a_par->k * l_poly
         + l_poly
         + ((size_t)a_par->l + a_par->k) * l_poly;
}

/* --- Keygen --- */

int chipmunk_ring_v2_keygen(chipmunk_ring_v2_keypair_t *a_kp,
                            const lotrs_params_t *a_par,
                            const uint8_t a_seed[32])
{
    if (!a_kp || !a_par || !a_seed) return -EINVAL;

    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "crv2-keygen-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    const uint32_t l_len = a_par->l + a_par->k;
    a_kp->sk.s = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_kp->sk.s.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    int l_rc = lotrs_sample_short_vec(&a_kp->sk.s, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { lotrs_xof_free(l_xof); return -ENOMEM; }

    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); lotrs_xof_free(l_xof); return -ENOMEM; }

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_xof_free(l_xof); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* pk = A * s_short + s_tail, canonicalized. */
    a_kp->pk.a_hat = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!a_kp->pk.a_hat.polys) {
        lotrs_polymat_free(&l_A); lotrs_xof_free(l_xof); return -ENOMEM;
    }

    lotrs_polyvec_t l_s_short = { .polys = a_kp->sk.s.polys, .n = a_par->l };
    lotrs_polyvec_t l_s_tail  = { .polys = a_kp->sk.s.polys + a_par->l, .n = a_par->k };

    lotrs_polymat_vecmul(&a_kp->pk.a_hat, &l_A, &l_s_short, a_par);
    lotrs_polyvec_add(&a_kp->pk.a_hat, &a_kp->pk.a_hat, &l_s_tail, a_par);

    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            a_kp->pk.a_hat.polys[i]->coeffs[j] %= a_par->q;
        }
    }

    lotrs_polymat_free(&l_A);
    lotrs_xof_free(l_xof);
    return 0;
}

/* --- Sign --- */

int chipmunk_ring_v2_sign(chipmunk_ring_v2_sig_t *a_sig,
                          const lotrs_params_t *a_par,
                          const chipmunk_ring_v2_ring_t *a_ring,
                          const chipmunk_ring_v2_sk_t *a_sk,
                          uint32_t a_signer_idx,
                          const uint8_t *a_msg, size_t a_msg_len,
                          const uint8_t a_seed[32])
{
    if (!a_sig || !a_par || !a_ring || !a_sk || !a_msg || !a_seed) return -EINVAL;
    if (a_signer_idx >= a_ring->N) return -EINVAL;

    int l_rc;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;

    /* Compute pk_sum = Σ pk[i] (ring aggregation). */
    lotrs_polyvec_t l_pk_sum = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_pk_sum.polys) return -ENOMEM;
    lotrs_polyvec_zero(&l_pk_sum, a_par);
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        lotrs_polyvec_add(&l_pk_sum, &l_pk_sum, &a_ring->pks[i].a_hat, a_par);
    }

    /* Compute signer's contribution: A * s_short + s_tail = pk[signer_idx].
     * The sign operation computes: z = x * s - y (masked witness).
     * For non-interactive: compute w = A * y_short + y_tail, then
     * FS challenge x = H(w, msg, pk_hash), then z = x*s - y. */

    /* Sample mask y. */
    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) { lotrs_polyvec_free(&l_pk_sum); return -ENOMEM; }

    const char *l_domain = "crv2-sign-v1";
    lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));

    const uint32_t l_len = a_par->l + a_par->k;
    lotrs_polyvec_t l_y = lotrs_polyvec_alloc(a_par, l_len);
    if (!l_y.polys) { lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum); return -ENOMEM; }

    l_rc = lotrs_sample_short_vec(&l_y, l_xof, a_par, a_par->eta);
    if (l_rc != 0) {
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return l_rc;
    }

    /* Compute A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) {
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) {
        lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
                return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* w = A * y_short + y_tail. */
    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_w.polys) {
        lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    lotrs_polyvec_t l_y_short = { .polys = l_y.polys, .n = a_par->l };
    lotrs_polyvec_t l_y_tail  = { .polys = l_y.polys + a_par->l, .n = a_par->k };
    lotrs_polymat_vecmul(&l_w, &l_A, &l_y_short, a_par);
    lotrs_polyvec_add(&l_w, &l_w, &l_y_tail, a_par);
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            l_w.polys[i]->coeffs[j] %= l_q;
        }
    }

    /* FS challenge c = H(w, msg, pk_sum). */
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
    if (!l_xof_c) {
        lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_buf[8 * 128];
        lotrs_poly_pack(l_buf, a_par->d * 8u, l_w.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_buf, a_par->d * 8u);
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_buf[8 * 128];
        lotrs_poly_pack(l_buf, a_par->d * 8u, l_pk_sum.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_buf, a_par->d * 8u);
    }

    lotrs_poly_t *l_c = lotrs_poly_alloc(a_par);
    if (!l_c) {
        lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    l_rc = lotrs_sample_ternary(l_c, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) {
        lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return l_rc;
    }

    /* z = c * s - y. */
    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(a_par, l_len);
    if (!l_z.polys) {
        lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w); lotrs_polymat_free(&l_A);
        lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); lotrs_polyvec_free(&l_pk_sum);
        return -ENOMEM;
    }
    for (uint32_t i = 0u; i < l_len; ++i) {
        lotrs_poly_t *l_cs = lotrs_poly_alloc(a_par);
        if (!l_cs) {
            lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
            lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
            lotrs_polyvec_free(&l_pk_sum); return -ENOMEM;
        }
        lotrs_poly_mul(l_cs, l_c, a_sk->s.polys[i], a_par);
        lotrs_poly_sub(l_z.polys[i], l_cs, l_y.polys[i], a_par);
        lotrs_poly_free(l_cs);
    }

    /* Rejection sampling. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    for (uint32_t i = 0u; i < l_len; ++i) {
        if (!lotrs_reject_infinity_norm(l_z.polys[i], l_bound, a_par)) {
            lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
            lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
            lotrs_polyvec_free(&l_pk_sum);
            return -EAGAIN;
        }
    }

    /* Serialize: header + w + c + z (same as LoTRS). */
    size_t l_w_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);
    size_t l_z_bytes = lotrs_polyvec_bytes(a_par, l_len);
    size_t l_total = CHIPMUNK_RING_V2_HEADER_BYTES + l_w_bytes + l_c_bytes + l_z_bytes;

    a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!a_sig->data) {
        lotrs_polyvec_free(&l_z); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_w);
        lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof);
        lotrs_polyvec_free(&l_pk_sum); return -ENOMEM;
    }
    a_sig->len = l_total;

    /* Write header. */
    chipmunk_ring_v2_header_t l_hdr = {
        .magic = CHIPMUNK_RING_V2_MAGIC,
        .version = CHIPMUNK_RING_V2_VERSION,
        .d = a_par->d,
        .N = a_ring->N,
        .rice_k = lotrs_optimal_rice_k((double)a_par->eta),
        .flags = 0u,
    };
    uint8_t *l_p = a_sig->data;
    memcpy(l_p, &l_hdr, CHIPMUNK_RING_V2_HEADER_BYTES);
    l_p += CHIPMUNK_RING_V2_HEADER_BYTES;

    /* Write w, c, z. */
    lotrs_polyvec_pack(l_p, l_w_bytes, &l_w, a_par);
    l_p += l_w_bytes;
    lotrs_poly_pack(l_p, l_c_bytes, l_c, a_par);
    l_p += l_c_bytes;
    lotrs_polyvec_pack(l_p, l_z_bytes, &l_z, a_par);

    lotrs_polyvec_free(&l_z);
    lotrs_poly_free(l_c);
    lotrs_polyvec_free(&l_w);
    lotrs_polymat_free(&l_A);
    lotrs_polyvec_free(&l_y);
    lotrs_xof_free(l_xof);
    lotrs_polyvec_free(&l_pk_sum);
    return 0;
}

/* --- Verify --- */

int chipmunk_ring_v2_verify(const chipmunk_ring_v2_sig_t *a_sig,
                            const lotrs_params_t *a_par,
                            const chipmunk_ring_v2_ring_t *a_ring,
                            const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sig || !a_sig->data || !a_par || !a_ring || !a_msg) return -EINVAL;
    if (a_sig->len < CHIPMUNK_RING_V2_HEADER_BYTES) return -EINVAL;

    int l_rc;

    /* Read header. */
    chipmunk_ring_v2_header_t l_hdr;
    memcpy(&l_hdr, a_sig->data, CHIPMUNK_RING_V2_HEADER_BYTES);
    if (l_hdr.magic != CHIPMUNK_RING_V2_MAGIC) return -EINVAL;
    if (l_hdr.version != CHIPMUNK_RING_V2_VERSION) return -EINVAL;
    if (l_hdr.N != a_ring->N) return -EINVAL;

    /* Deserialize w, c, z. */
    const uint8_t *l_p = a_sig->data + CHIPMUNK_RING_V2_HEADER_BYTES;
    size_t l_remaining = a_sig->len - CHIPMUNK_RING_V2_HEADER_BYTES;

    size_t l_w_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);
    const uint32_t l_len = a_par->l + a_par->k;
    size_t l_z_bytes = lotrs_polyvec_bytes(a_par, l_len);

    if (l_remaining != l_w_bytes + l_c_bytes + l_z_bytes) return -EINVAL;

    lotrs_polyvec_t l_w = lotrs_polyvec_alloc(a_par, a_par->k);
    lotrs_poly_t *l_c = lotrs_poly_alloc(a_par);
    lotrs_polyvec_t l_z = lotrs_polyvec_alloc(a_par, l_len);
    if (!l_w.polys || !l_c || !l_z.polys) {
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }

    lotrs_polyvec_unpack(&l_w, l_p, l_w_bytes, a_par);
    l_p += l_w_bytes;
    lotrs_poly_unpack(l_c, l_p, l_c_bytes, a_par);
    l_p += l_c_bytes;
    lotrs_polyvec_unpack(&l_z, l_p, l_z_bytes, a_par);

    /* Recompute challenge c' from w, msg, pk_sum. */
    lotrs_polyvec_t l_pk_sum = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_pk_sum.polys) {
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    lotrs_polyvec_zero(&l_pk_sum, a_par);
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        lotrs_polyvec_add(&l_pk_sum, &l_pk_sum, &a_ring->pks[i].a_hat, a_par);
    }

    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crv2-challenge-v1", 17u);
    if (!l_xof_c) {
        lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_buf[8 * 128];
        lotrs_poly_pack(l_buf, a_par->d * 8u, l_w.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_buf, a_par->d * 8u);
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_buf[8 * 128];
        lotrs_poly_pack(l_buf, a_par->d * 8u, l_pk_sum.polys[i], a_par);
        lotrs_xof_absorb(l_xof_c, l_buf, a_par->d * 8u);
    }

    lotrs_poly_t *l_c_prime = lotrs_poly_alloc(a_par);
    if (!l_c_prime) {
        lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    l_rc = lotrs_sample_ternary(l_c_prime, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) {
        lotrs_poly_free(l_c_prime); lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return l_rc;
    }

    /* Check c == c'. */
    for (uint32_t i = 0u; i < a_par->d; ++i) {
        if (l_c->coeffs[i] % a_par->q != l_c_prime->coeffs[i] % a_par->q) {
            lotrs_poly_free(l_c_prime); lotrs_polyvec_free(&l_pk_sum);
            lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
            return -EINVAL;
        }
    }
    lotrs_poly_free(l_c_prime);

    /* Norm check: ‖z‖∞ < φ·η. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    for (uint32_t i = 0u; i < l_len; ++i) {
        if (!lotrs_reject_infinity_norm(l_z.polys[i], l_bound, a_par)) {
            lotrs_polyvec_free(&l_pk_sum);
            lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
            return -EINVAL;
        }
    }

    /* Algebraic check: A*z_short + z_tail + w == c*pk_sum. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) {
        lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    const char *l_a_domain = "crv2-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) {
        lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A);
                lotrs_polyvec_free(&l_pk_sum);
                lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
                return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* lhs = A * z_short + z_tail + w. */
    lotrs_polyvec_t l_z_short = { .polys = l_z.polys, .n = a_par->l };
    lotrs_polyvec_t l_z_tail  = { .polys = l_z.polys + a_par->l, .n = a_par->k };

    lotrs_polyvec_t l_lhs = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_lhs.polys) {
        lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    lotrs_polymat_vecmul(&l_lhs, &l_A, &l_z_short, a_par);
    lotrs_polyvec_add(&l_lhs, &l_lhs, &l_z_tail, a_par);
    lotrs_polyvec_add(&l_lhs, &l_lhs, &l_w, a_par);

    /* Canonicalize lhs. */
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            l_lhs.polys[i]->coeffs[j] %= a_par->q;
        }
    }

    /* rhs = c * pk_sum. */
    lotrs_polyvec_t l_rhs = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_rhs.polys) {
        lotrs_polyvec_free(&l_lhs); lotrs_polymat_free(&l_A); lotrs_polyvec_free(&l_pk_sum);
        lotrs_polyvec_free(&l_w); lotrs_poly_free(l_c); lotrs_polyvec_free(&l_z);
        return -ENOMEM;
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        lotrs_poly_mul(l_rhs.polys[i], l_c, l_pk_sum.polys[i], a_par);
    }

    /* Check lhs == rhs. */
    int l_match = 1;
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            if (l_lhs.polys[i]->coeffs[j] % a_par->q !=
                l_rhs.polys[i]->coeffs[j] % a_par->q) {
                l_match = 0;
                break;
            }
        }
        if (!l_match) break;
    }

    lotrs_polyvec_free(&l_rhs);
    lotrs_polyvec_free(&l_lhs);
    lotrs_polymat_free(&l_A);
    lotrs_polyvec_free(&l_pk_sum);
    lotrs_polyvec_free(&l_w);
    lotrs_poly_free(l_c);
    lotrs_polyvec_free(&l_z);

    return l_match ? 0 : -EINVAL;
}
