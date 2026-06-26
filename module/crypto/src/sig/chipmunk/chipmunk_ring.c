/*
 * Chipmunk Ring — non-interactive lattice ring signature.
 *
 * Based on LoTRS binary ring proof (RS) with Fiat-Shamir.
 * Single-signer, non-interactive, O(N) signature size.
 */

#include "chipmunk_ring.h"
#include "lotrs_sample.h"
#include "chipmunk_mring.h"
#include "chipmunk_lrs.h"

#include <errno.h>
#include <string.h>

#define LOG_TAG "chipmunk_ring"
#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"
#include "dap_rand.h"
#include "dap_serialize.h"

/* Constant-time memory comparison (same as LRS s_memcmp_ct). */
static int s_memcmp_ct(const void *a_a, const void *a_b, size_t a_len)
{
    const uint8_t *l_a = (const uint8_t *)a_a;
    const uint8_t *l_b = (const uint8_t *)a_b;
    uint8_t l_diff = 0u;
    for (size_t i = 0u; i < a_len; ++i) {
        l_diff |= l_a[i] ^ l_b[i];
    }
    return (int)l_diff;
}

/* Compute truncated parameter hash (16 bytes). Returns 0 on success. */
static int s_param_hash(uint8_t a_out[16], const lotrs_params_t *a_par)
{
    lotrs_xof_t *l_xof = lotrs_xof_new((const uint8_t *)"crin-params-v1", 14u);
    if (!l_xof) { memset(a_out, 0, 16u); return -ENOMEM; }
    int l_rc;
    uint8_t l_buf[8];
    memcpy(l_buf, &a_par->d, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->q, 8u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 8u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->k, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->l, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->w, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    memcpy(l_buf, &a_par->eta, 4u); l_rc = lotrs_xof_absorb(l_xof, l_buf, 4u); if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }
    lotrs_xof_squeeze(l_xof, a_out, 16u);
    lotrs_xof_free(l_xof);
    dap_memwipe(l_buf, sizeof(l_buf));
    return 0;
}

/* --- Header schema (dap_serialize) --- */

static const dap_serialize_field_t s_chipmunk_ring_header_fields[] = {
    { .name = "magic",        .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, magic),        .size = sizeof(uint32_t) },
    { .name = "version",      .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, version),      .size = sizeof(uint32_t) },
    { .name = "d",            .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, d),            .size = sizeof(uint32_t) },
    { .name = "N",            .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, N),            .size = sizeof(uint32_t) },
    { .name = "rice_k_z",     .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, rice_k_z),     .size = sizeof(uint32_t) },
    { .name = "rice_bound_z", .type = DAP_SERIALIZE_TYPE_INT64,  .offset = offsetof(chipmunk_ring_header_t, rice_bound_z), .size = sizeof(int64_t)  },
    { .name = "flags",        .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, flags),        .size = sizeof(uint32_t) },
    { .name = "param_hash",   .type = DAP_SERIALIZE_TYPE_ARRAY_FIXED, .offset = offsetof(chipmunk_ring_header_t, param_hash),
      .size = 1u, .fixed_count = 16u, .element_type = DAP_SERIALIZE_TYPE_UINT8 },
    { .name = "key_image_len", .type = DAP_SERIALIZE_TYPE_UINT32, .offset = offsetof(chipmunk_ring_header_t, key_image_len), .size = sizeof(uint32_t) },
    { .name = "key_image",    .type = DAP_SERIALIZE_TYPE_ARRAY_FIXED, .offset = offsetof(chipmunk_ring_header_t, key_image),
      .size = 1u, .fixed_count = 9216u, .element_type = DAP_SERIALIZE_TYPE_UINT8 },
};

DAP_SERIALIZE_SCHEMA_DEFINE(s_chipmunk_ring_header_schema,
                            chipmunk_ring_header_t,
                            s_chipmunk_ring_header_fields);

/* Wipe polynomial coefficients before freeing (secret material). */
static inline void s_poly_wipe_free(lotrs_poly_t *a_p)
{
    if (a_p) {
        lotrs_poly_free(a_p);  /* lotrs_poly_free already wipes coeffs */
    }
}

/* --- Cleanup --- */

void chipmunk_ring_keypair_free(chipmunk_ring_keypair_t *a_kp)
{
    if (a_kp) {
        lotrs_polyvec_free(&a_kp->pk.a_hat);
        lotrs_polyvec_free(&a_kp->sk.s);
        dap_memwipe(a_kp, sizeof(*a_kp));
    }
}

void chipmunk_ring_table_free(chipmunk_ring_table_t *a_ring)
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

void chipmunk_ring_sig_free(chipmunk_ring_sig_t *a_sig)
{
    if (a_sig && a_sig->data) {
        dap_memwipe(a_sig->data, a_sig->len);
        DAP_DELETE(a_sig->data);
        a_sig->data = NULL;
        a_sig->len = 0;
    }
}

size_t chipmunk_ring_sig_bytes_max(const lotrs_params_t *a_par, uint32_t a_N)
{
    if (!a_par || a_N == 0u) return 0u;
    /* Header + N * T (k polys each) + N * c (1 poly each) + N * z (l+k polys each). */
    size_t l_poly = lotrs_poly_bytes(a_par);
    if (l_poly == 0u) return 0u;
    size_t l_hdr = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);

    /* Overflow checks: each term must not exceed SIZE_MAX / 2 before addition */
    size_t l_n = (size_t)a_N;
    size_t l_k = (size_t)a_par->k;
    size_t l_l = (size_t)a_par->l;
    size_t l_lk = l_l + l_k;

    /* Check individual multiplications for overflow */
    if (l_k > 0u && l_poly > SIZE_MAX / l_k) return 0u;
    size_t l_t_per = l_k * l_poly;
    if (l_n > 0u && l_t_per > SIZE_MAX / l_n) return 0u;
    size_t l_T_bytes = l_n * l_t_per;

    if (l_n > 0u && l_poly > SIZE_MAX / l_n) return 0u;
    size_t l_c_bytes = l_n * l_poly;

    if (l_lk > 0u && l_poly > SIZE_MAX / l_lk) return 0u;
    size_t l_z_per = l_lk * l_poly;
    if (l_n > 0u && l_z_per > SIZE_MAX / l_n) return 0u;
    size_t l_z_bytes = l_n * l_z_per;

    /* Check final addition for overflow */
    size_t l_total = l_hdr;
    if (l_T_bytes > SIZE_MAX - l_total) return 0u;
    l_total += l_T_bytes;
    if (l_c_bytes > SIZE_MAX - l_total) return 0u;
    l_total += l_c_bytes;
    if (l_z_bytes > SIZE_MAX - l_total) return 0u;
    l_total += l_z_bytes;

    return l_total;
}

/*
 * Key image generation for linkability.
 *
 * I = A_I * s, where A_I is derived from the signer's public key.
 * Same secret key always produces the same key image (deterministic).
 * Key image is included in the Fiat-Shamir transcript for binding.
 *
 * For verification, only the key image bytes are needed (not the secret key).
 * Two signatures with the same key image → same signer (double-vote detected).
 */
static int s_generate_key_image(uint8_t *a_out, size_t a_out_len,
                                const chipmunk_ring_pk_t *a_pk,
                                const chipmunk_ring_sk_t *a_sk,
                                const lotrs_params_t *a_par)
{
    if (!a_out || !a_pk || !a_sk || !a_par) return -EINVAL;

    int l_rc;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Derive A_I from public key: A_I = H("crin-key-image" || pk_bytes) */
    lotrs_polymat_t l_A_I = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A_I.rows) return -ENOMEM;

    const char *l_domain = "crin-key-image-v1";
    lotrs_xof_t *l_xof = lotrs_xof_new((const uint8_t *)l_domain, strlen(l_domain));
    if (!l_xof) { lotrs_polymat_free(&l_A_I); return -ENOMEM; }

    /* Absorb public key bytes */
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        uint8_t l_buf[LOTRS_D_MAX * 8];
        lotrs_poly_pack(l_buf, sizeof(l_buf), a_pk->a_hat.polys[i], a_par);
        l_rc = lotrs_xof_absorb(l_xof, l_buf, lotrs_poly_bytes(a_par));
        if (l_rc != 0) { lotrs_xof_free(l_xof); lotrs_polymat_free(&l_A_I); return l_rc; }
    }

    /* Sample A_I matrix */
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A_I.rows[i].polys[j], l_xof, a_par);
            if (l_rc != 0) { lotrs_xof_free(l_xof); lotrs_polymat_free(&l_A_I); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof);

    /* Compute I = A_I * s_short + s_tail */
    lotrs_polyvec_t l_I = lotrs_polyvec_alloc(a_par, a_par->k);
    if (!l_I.polys) { lotrs_polymat_free(&l_A_I); return -ENOMEM; }

    lotrs_polyvec_t l_s_short = { .polys = a_sk->s.polys, .n = a_par->l };
    lotrs_polyvec_t l_s_tail  = { .polys = a_sk->s.polys + a_par->l, .n = a_par->k };

    lotrs_polymat_vecmul(&l_I, &l_A_I, &l_s_short, a_par);
    lotrs_polyvec_add(&l_I, &l_I, &l_s_tail, a_par);

    /* Canonicalize */
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->d; ++j) {
            l_I.polys[i]->coeffs[j] = (uint64_t)lotrs_mod_reduce(
                (__int128_t)(int64_t)l_I.polys[i]->coeffs[j], a_par->q);
        }
    }

    /* Pack all k polynomials as key image */
    size_t l_poly_bytes = lotrs_poly_bytes(a_par);
    size_t l_ki_bytes = (size_t)a_par->k * l_poly_bytes;
    if (a_out_len < l_ki_bytes) {
        lotrs_polymat_free(&l_A_I);
        lotrs_polyvec_free(&l_I);
        return -EINVAL;
    }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        lotrs_poly_pack(a_out + i * l_poly_bytes, l_poly_bytes, l_I.polys[i], a_par);
    }

    lotrs_polymat_free(&l_A_I);
    lotrs_polyvec_free(&l_I);
    return 0;
}

/*
 * Linkability: compare key images of two signatures.
 * Returns 1 if same signer (key images match), 0 if different, negative on error.
 */
int chipmunk_ring_link(const chipmunk_ring_sig_t *a_sig1, const chipmunk_ring_sig_t *a_sig2)
{
    if (!a_sig1 || !a_sig1->data || !a_sig2 || !a_sig2->data) return -EINVAL;

    size_t l_hdr_bytes = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
    if (a_sig1->len < l_hdr_bytes || a_sig2->len < l_hdr_bytes) return -EINVAL;

    chipmunk_ring_header_t l_hdr1 = {0}, l_hdr2 = {0};
    dap_deserialize_result_t l_d1 = dap_serialize_from_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                   a_sig1->data, l_hdr_bytes, &l_hdr1, NULL);
    dap_deserialize_result_t l_d2 = dap_serialize_from_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                   a_sig2->data, l_hdr_bytes, &l_hdr2, NULL);
    if (l_d1.error_code != 0 || l_d2.error_code != 0) return -EINVAL;

    if (l_hdr1.key_image_len != l_hdr2.key_image_len) return 0;
    return s_memcmp_ct(l_hdr1.key_image, l_hdr2.key_image, l_hdr1.key_image_len) == 0 ? 1 : 0;
}

/* --- Keygen --- */

int chipmunk_ring_keygen(chipmunk_ring_keypair_t *a_kp,
                            const lotrs_params_t *a_par,
                            const uint8_t a_seed[32])
{
    if (!a_kp || !a_par || !a_seed) return -EINVAL;

    int l_rc;
    lotrs_xof_t *l_xof = lotrs_xof_new(a_seed, 32u);
    if (!l_xof) return -ENOMEM;

    const char *l_domain = "crin-keygen-v1";
    l_rc = lotrs_xof_absorb(l_xof, (const uint8_t *)l_domain, strlen(l_domain));
    if (l_rc != 0) { lotrs_xof_free(l_xof); return l_rc; }

    const uint32_t l_len = a_par->l + a_par->k;
    a_kp->sk.s = lotrs_polyvec_alloc(a_par, l_len);
    if (!a_kp->sk.s.polys) { lotrs_xof_free(l_xof); return -ENOMEM; }

    l_rc = lotrs_sample_short_vec(&a_kp->sk.s, l_xof, a_par, a_par->eta);
    if (l_rc != 0) { lotrs_polyvec_free(&a_kp->sk.s); lotrs_xof_free(l_xof); return l_rc; }

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { lotrs_xof_free(l_xof); return -ENOMEM; }

    const char *l_a_domain = "crin-A-v1";
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
            a_kp->pk.a_hat.polys[i]->coeffs[j] = (uint64_t)lotrs_mod_reduce(
                (__int128_t)(int64_t)a_kp->pk.a_hat.polys[i]->coeffs[j], a_par->q);
        }
    }

    lotrs_polymat_free(&l_A);
    lotrs_xof_free(l_xof);
    return 0;
}

/* --- Sign --- */

/* Max rejection sampling retries before giving up. */
#define CHIPMUNK_RING_NONINT_MAX_RETRIES 64

/* Validate a public key table. */
static int s_validate_table(const chipmunk_ring_table_t *a_ring, const lotrs_params_t *a_par)
{
    if (!a_ring || !a_ring->pks || a_ring->N == 0u) return -EINVAL;
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        if (!a_ring->pks[i].a_hat.polys) return -EINVAL;
    }
    return 0;
}

/*
 * Canonical ring sorting — prevents ordering-based deanonymization.
 *
 * Sort ring members by memcmp over serialized pk bytes, reject duplicates,
 * and locate the signer's pk via constant-time full scan (no early break).
 * Follows the same pattern as LRS s_canonicalise_ring and MRNG
 * chipmunk_mring_canonicalise_ring.
 */
static int s_pk_cmp(const void *a_a, const void *a_b)
{
    /* Compare over the a_hat polynomial vector coefficients.
     * Use the polyvec n field (number of polynomials) for bounds.
     * Each polynomial has CHIPMUNK_N coefficients allocated, but only
     * the first d may be valid — however since allocations use
     * sizeof(lotrs_poly_t) = CHIPMUNK_N * 8, all 512 are addressable. */
    const chipmunk_ring_pk_t *l_a = (const chipmunk_ring_pk_t *)a_a;
    const chipmunk_ring_pk_t *l_b = (const chipmunk_ring_pk_t *)a_b;
    if (!l_a->a_hat.polys || !l_b->a_hat.polys) return 0;
    uint32_t l_n = l_a->a_hat.n < l_b->a_hat.n ? l_a->a_hat.n : l_b->a_hat.n;
    for (uint32_t i = 0u; i < l_n; ++i) {
        if (!l_a->a_hat.polys[i] || !l_b->a_hat.polys[i]) continue;
        for (uint32_t j = 0u; j < CHIPMUNK_N; ++j) {
            int32_t l_diff = l_a->a_hat.polys[i]->coeffs[j] - l_b->a_hat.polys[i]->coeffs[j];
            if (l_diff != 0) return (l_diff > 0) ? 1 : -1;
        }
    }
    return 0;
}

/*
 * Canonicalize ring: sort, reject duplicates, find signer index.
 * Returns 0 on success, -EINVAL on invalid input or duplicate keys.
 * a_signer_idx is updated to the new index of the signer in the sorted ring.
 */
static int s_canonicalize_ring(chipmunk_ring_table_t *a_ring,
                               uint32_t *a_signer_idx,
                               const lotrs_params_t *a_par)
{
    if (!a_ring || !a_ring->pks || !a_signer_idx || a_ring->N < 2u)
        return -EINVAL;

    /* Sort ring by pk comparison */
    qsort(a_ring->pks, a_ring->N, sizeof(chipmunk_ring_pk_t), s_pk_cmp);

    /* Reject duplicates */
    for (uint32_t i = 1u; i < a_ring->N; ++i) {
        if (s_pk_cmp(&a_ring->pks[i - 1u], &a_ring->pks[i]) == 0) {
            log_it(L_ERROR, "CRIN: duplicate public keys in ring at positions %u and %u", i - 1u, i);
            return -EINVAL;
        }
    }

    /* Find signer's new position — CT-safe full scan, no early break */
    uint32_t l_signer_idx = *a_signer_idx;
    chipmunk_ring_pk_t l_signer_pk;
    /* We need to find which pk was at the original signer_idx before sort.
     * Since we already sorted, we can't use the old index. Instead, the caller
     * must pass the signer's pk separately. For now, we store the original
     * pk before sorting and search for it after. */
    /* NOTE: This function should be called BEFORE the ring is modified.
     * The caller passes the original signer_idx, and we find the new position. */
    return 0;  /* Placeholder — actual search done in sign/verify after sort */
}

/* Cleanup helper for sign function resources. */
static void s_sign_cleanup(lotrs_polymat_t *a_A,
                           lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                           lotrs_polyvec_t *a_z, uint32_t a_N,
                           uint8_t *a_retry_seed,
                           chipmunk_ring_pk_t *a_sorted_pks)
{
    if (a_T && a_c_arr && a_z) {
        for (uint32_t i = 0u; i < a_N; ++i) {
            lotrs_polyvec_free(&a_T[i]);
            lotrs_poly_free(a_c_arr[i]);
            lotrs_polyvec_free(&a_z[i]);
        }
    }
    DAP_DELETE(a_T);
    DAP_DELETE(a_c_arr);
    DAP_DELETE(a_z);
    lotrs_polymat_free(a_A);
    if (a_retry_seed) dap_memwipe(a_retry_seed, 32u);
    if (a_sorted_pks) DAP_DELETE(a_sorted_pks);
}

int chipmunk_ring_sign(chipmunk_ring_sig_t *a_sig,
                          const lotrs_params_t *a_par,
                          const chipmunk_ring_table_t *a_ring,
                          const chipmunk_ring_sk_t *a_sk,
                          uint32_t a_signer_idx,
                          const uint8_t *a_msg, size_t a_msg_len,
                          const uint8_t a_seed[32])
{
    if (!a_sig || !a_par || !a_ring || !a_sk || !a_msg || !a_seed) return -EINVAL;
    if (a_signer_idx >= a_ring->N) return -EINVAL;
    if (a_ring->N < CHIPMUNK_RING_N_MIN) {
        log_it(L_ERROR, "CRIN sign: ring size %u below minimum %u", a_ring->N, CHIPMUNK_RING_N_MIN);
        return -EINVAL;
    }
    if (s_validate_table(a_ring, a_par) != 0) return -EINVAL;

    /* Canonical sort: copy ring, sort, reject duplicates, find signer.
     * Prevents ordering-based deanonymization if caller passes ring in
     * a signer-revealing order. */
    chipmunk_ring_table_t l_sorted_ring;
    l_sorted_ring.N = a_ring->N;
    l_sorted_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, a_ring->N);
    if (!l_sorted_ring.pks) return -ENOMEM;
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        l_sorted_ring.pks[i] = a_ring->pks[i];
    }
    /* Save signer's pk before sort */
    chipmunk_ring_pk_t l_signer_pk = a_ring->pks[a_signer_idx];

    qsort(l_sorted_ring.pks, l_sorted_ring.N, sizeof(chipmunk_ring_pk_t), s_pk_cmp);

    /* Reject duplicates */
    for (uint32_t i = 1u; i < l_sorted_ring.N; ++i) {
        if (s_pk_cmp(&l_sorted_ring.pks[i - 1u], &l_sorted_ring.pks[i]) == 0) {
            log_it(L_ERROR, "CRIN sign: duplicate public keys in ring");
            DAP_DELETE(l_sorted_ring.pks);
            return -EINVAL;
        }
    }
    /* Find signer's new index — CT-safe full scan, no early break */
    uint32_t l_signer_idx_new = UINT32_MAX;
    for (uint32_t i = 0u; i < l_sorted_ring.N; ++i) {
        if (s_pk_cmp(&l_sorted_ring.pks[i], &l_signer_pk) == 0) {
            l_signer_idx_new = i;
        }
    }
    if (l_signer_idx_new == UINT32_MAX) {
        log_it(L_ERROR, "CRIN sign: signer pk not found in sorted ring");
        DAP_DELETE(l_sorted_ring.pks);
        return -EINVAL;
    }

    int l_rc;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;
    const uint32_t l_N = l_sorted_ring.N;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Generate key image for linkability (deterministic for same key).
     * Computed once before the retry loop since it doesn't depend on randomness. */
    uint8_t l_key_image[9216];
    size_t l_key_image_len = (size_t)a_par->k * lotrs_poly_bytes(a_par);
    l_rc = s_generate_key_image(l_key_image, sizeof(l_key_image), &l_sorted_ring.pks[l_signer_idx_new], a_sk, a_par);
    if (l_rc != 0) {
        log_it(L_ERROR, "CRIN sign: key image generation failed: %d", l_rc);
        DAP_DELETE(l_sorted_ring.pks);
        return l_rc;
    }

    /* Generate A matrix (shared across retries). */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) return -ENOMEM;
    const char *l_a_domain = "crin-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* Allocate per-member arrays (shared across retries). */
    lotrs_polyvec_t *l_T = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    lotrs_poly_t **l_c_arr = DAP_NEW_Z_COUNT(lotrs_poly_t *, l_N);
    lotrs_polyvec_t *l_z = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    if (!l_T || !l_c_arr || !l_z) {
        DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
        lotrs_polymat_free(&l_A); return -ENOMEM;
    }

    for (uint32_t i = 0u; i < l_N; ++i) {
        l_T[i] = lotrs_polyvec_alloc(a_par, a_par->k);
        l_c_arr[i] = lotrs_poly_alloc(a_par);
        l_z[i] = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_T[i].polys || !l_c_arr[i] || !l_z[i].polys) {
            for (uint32_t j = 0u; j <= i; ++j) {
                lotrs_polyvec_free(&l_T[j]); lotrs_poly_free(l_c_arr[j]); lotrs_polyvec_free(&l_z[j]);
            }
            DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
            lotrs_polymat_free(&l_A); return -ENOMEM;
        }
    }

    /* Simulate non-signer branches: random c_i, z_i, compute T_i.
     * These are deterministic from a_seed and don't change across retries. */
    for (uint32_t i = 0u; i < l_N; ++i) {
        if (i == l_signer_idx_new) continue;
        lotrs_xof_t *l_xof_sim = lotrs_xof_new(a_seed, 32u);
        if (!l_xof_sim) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_sim, (const uint8_t *)"crin-sim", 8u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL, l_sorted_ring.pks); return l_rc; }
        uint8_t l_idx_buf[4];
        l_idx_buf[0] = (uint8_t)i; l_idx_buf[1] = 0; l_idx_buf[2] = 0; l_idx_buf[3] = 0;
        l_rc = lotrs_xof_absorb(l_xof_sim, l_idx_buf, 4u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL, l_sorted_ring.pks); return l_rc; }

        l_rc = lotrs_sample_ternary(l_c_arr[i], l_xof_sim, a_par, a_par->w);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL, l_sorted_ring.pks); return l_rc; }

        lotrs_polyvec_t l_z_short_i = { .polys = l_z[i].polys, .n = a_par->l };
        lotrs_polyvec_t l_z_tail_i  = { .polys = l_z[i].polys + a_par->l, .n = a_par->k };
        l_rc = lotrs_sample_short_vec(&l_z[i], l_xof_sim, a_par, a_par->eta);
        if (l_rc != 0) { lotrs_xof_free(l_xof_sim); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, NULL, l_sorted_ring.pks); return l_rc; }

        lotrs_polymat_vecmul(&l_T[i], &l_A, &l_z_short_i, a_par);
        lotrs_polyvec_add(&l_T[i], &l_T[i], &l_z_tail_i, a_par);

        for (uint32_t j = 0u; j < a_par->k; ++j) {
            lotrs_poly_t *l_cp = lotrs_poly_alloc(a_par);
            lotrs_poly_mul(l_cp, l_c_arr[i], l_sorted_ring.pks[i].a_hat.polys[j], a_par);
            lotrs_poly_sub(l_T[i].polys[j], l_T[i].polys[j], l_cp, a_par);
            lotrs_poly_free(l_cp);
        }
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_T[i].polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_T[i].polys[j]->coeffs[kk], l_q);
            }
        }
        lotrs_xof_free(l_xof_sim);
    }

    /* Rejection sampling loop for real signer branch.
     * Hedged randomness: derive retry seed from H(msg || user_seed || attempt).
     * This ensures different messages get different randomness even with the same seed,
     * and mitigates fault injection that replays seeds. */
    int64_t l_bound = (int64_t)(a_par->phi * a_par->eta);
    uint8_t l_retry_seed[32];

    for (uint32_t l_attempt = 0u; l_attempt < CHIPMUNK_RING_NONINT_MAX_RETRIES; ++l_attempt) {
        /* Derive hedged seed. */
        lotrs_xof_t *l_xof_hedge = lotrs_xof_new((const uint8_t *)"crin-hedge-v1", 13u);
        if (!l_xof_hedge) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_hedge, a_msg, a_msg_len);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        l_rc = lotrs_xof_absorb(l_xof_hedge, a_seed, 32u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        uint8_t l_attempt_buf[4] = {
            (uint8_t)(l_attempt & 0xFF),
            (uint8_t)((l_attempt >> 8) & 0xFF),
            (uint8_t)((l_attempt >> 16) & 0xFF),
            (uint8_t)((l_attempt >> 24) & 0xFF)
        };
        l_rc = lotrs_xof_absorb(l_xof_hedge, l_attempt_buf, 4u);
        if (l_rc != 0) { lotrs_xof_free(l_xof_hedge); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        lotrs_xof_squeeze(l_xof_hedge, l_retry_seed, 32u);
        lotrs_xof_free(l_xof_hedge);

        /* Sample y from hedged seed. */
        lotrs_xof_t *l_xof = lotrs_xof_new(l_retry_seed, 32u);
        if (!l_xof) { s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof, (const uint8_t *)"crin-sign-v1", 12u);
        if (l_rc != 0) { lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }

        lotrs_polyvec_t l_y = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_y.polys) { lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_sample_short_vec(&l_y, l_xof, a_par, a_par->eta);
        if (l_rc != 0) { lotrs_polyvec_free(&l_y); lotrs_xof_free(l_xof); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        lotrs_xof_free(l_xof);

        /* T_ell = A * y_short + y_tail. */
        lotrs_polyvec_t l_y_short = { .polys = l_y.polys, .n = a_par->l };
        lotrs_polyvec_t l_y_tail  = { .polys = l_y.polys + a_par->l, .n = a_par->k };
        lotrs_polymat_vecmul(&l_T[l_signer_idx_new], &l_A, &l_y_short, a_par);
        lotrs_polyvec_add(&l_T[l_signer_idx_new], &l_T[l_signer_idx_new], &l_y_tail, a_par);
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_T[l_signer_idx_new].polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_T[l_signer_idx_new].polys[j]->coeffs[kk], l_q);
            }
        }

        /* FS challenge: c = H(T_0, ..., T_{N-1}, key_image, msg). */
        lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crin-challenge-v1", 17u);
        if (!l_xof_c) { lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
        if (l_rc != 0) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        /* Absorb key image for linkability binding */
        l_rc = lotrs_xof_absorb(l_xof_c, l_key_image, l_key_image_len);
        if (l_rc != 0) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
        for (uint32_t i = 0u; i < l_N; ++i) {
            for (uint32_t j = 0u; j < a_par->k; ++j) {
                uint8_t l_buf[LOTRS_D_MAX * 8]; /* max poly bytes */
                lotrs_poly_pack(l_buf, sizeof(l_buf), l_T[i].polys[j], a_par);
                l_rc = lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
                if (l_rc != 0) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
            }
        }

        lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
        if (!l_c_total) { lotrs_xof_free(l_xof_c); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
        l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
        lotrs_xof_free(l_xof_c);
        if (l_rc != 0) { lotrs_poly_free(l_c_total); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }

        /* c_ell = c_total - Σ_{i!=ell} c_i. */
        lotrs_poly_copy(l_c_arr[l_signer_idx_new], l_c_total, a_par);
        for (uint32_t i = 0u; i < l_N; ++i) {
            if (i == l_signer_idx_new) continue;
            lotrs_poly_sub(l_c_arr[l_signer_idx_new], l_c_arr[l_signer_idx_new], l_c_arr[i], a_par);
        }
        lotrs_poly_free(l_c_total);

        /* z_ell = y + c_ell * s.
         * Blinding: per-component random mask r_i, compute c*(s[i]+r_i) - c*r_i = c*s[i].
         * Both multiplications have randomized inputs, protecting s from side-channel. */
        for (uint32_t i = 0u; i < l_len; ++i) {
            lotrs_poly_t *l_r = lotrs_poly_alloc(a_par);
            if (!l_r) { lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
            lotrs_xof_t *l_xof_r = lotrs_xof_new(l_retry_seed, 32u);
            if (!l_xof_r) { s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM; }
            l_rc = lotrs_xof_absorb(l_xof_r, (const uint8_t *)"crin-blind-v1", 13u);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
            l_rc = lotrs_xof_absorb(l_xof_r, l_attempt_buf, 4u);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
            uint8_t l_comp_buf[4] = {
                (uint8_t)(i & 0xFF), (uint8_t)((i >> 8) & 0xFF),
                (uint8_t)((i >> 16) & 0xFF), (uint8_t)((i >> 24) & 0xFF)
            };
            l_rc = lotrs_xof_absorb(l_xof_r, l_comp_buf, 4u); /* unique mask per component */
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
            l_rc = lotrs_sample_short(l_r, l_xof_r, a_par, a_par->eta);
            if (l_rc != 0) { lotrs_xof_free(l_xof_r); s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc; }
            lotrs_xof_free(l_xof_r);

            /* s_masked = s[i] + r_i. */
            lotrs_poly_t *l_s_masked = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cr = lotrs_poly_alloc(a_par);
            lotrs_poly_t *l_cs_masked = lotrs_poly_alloc(a_par);
            if (!l_s_masked || !l_cr || !l_cs_masked) {
                s_poly_wipe_free(l_s_masked); s_poly_wipe_free(l_cr); lotrs_poly_free(l_cs_masked);
                s_poly_wipe_free(l_r); lotrs_polyvec_free(&l_y); s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM;
            }
            lotrs_poly_add(l_s_masked, a_sk->s.polys[i], l_r, a_par);
            /* cs_masked = c * s_masked (randomized input). */
            lotrs_poly_mul(l_cs_masked, l_c_arr[l_signer_idx_new], l_s_masked, a_par);
            /* cr = c * r_i (randomized input). */
            lotrs_poly_mul(l_cr, l_c_arr[l_signer_idx_new], l_r, a_par);
            /* z[i] = y[i] + cs_masked - cr = y[i] + c*s[i]. */
            lotrs_poly_add(l_z[l_signer_idx_new].polys[i], l_y.polys[i], l_cs_masked, a_par);
            lotrs_poly_sub(l_z[l_signer_idx_new].polys[i], l_z[l_signer_idx_new].polys[i], l_cr, a_par);
            s_poly_wipe_free(l_s_masked); s_poly_wipe_free(l_cr); lotrs_poly_free(l_cs_masked);
            s_poly_wipe_free(l_r);
        }

        lotrs_polyvec_free(&l_y);

        /* Check norm bound — constant-time: always check ALL components,
         * no early break. Use bitwise AND accumulator to prevent timing leak
         * on how many components passed the check. */
        int l_ok = 1;
        for (uint32_t i = 0u; i < l_len; ++i) {
            l_ok &= lotrs_reject_infinity_norm(l_z[l_signer_idx_new].polys[i], l_bound, a_par);
        }
        if (l_ok) {
            /* Serialize: header + T (raw) + c (raw) + z (Rice-coded). */
            size_t l_T_bytes = (size_t)l_N * lotrs_polyvec_bytes(a_par, a_par->k);
            size_t l_c_bytes = (size_t)l_N * lotrs_poly_bytes(a_par);

            /* Pre-encode z with Rice coding. */
            int64_t l_rice_bound_z = l_bound;
            uint32_t l_rice_k_z = lotrs_optimal_rice_k((double)l_rice_bound_z);
            size_t l_z_count = (size_t)l_N * l_len;
            size_t *l_z_sizes = DAP_NEW_Z_SIZE(size_t, l_z_count * sizeof(size_t));
            uint8_t **l_z_bufs = DAP_NEW_Z_COUNT(uint8_t *, l_z_count);
            if (!l_z_sizes || !l_z_bufs) {
                DAP_DELETE(l_z_sizes); DAP_DELETE(l_z_bufs);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM;
            }

            size_t l_z_rice_total = 0u;
            int l_rice_ok = 1;
            for (size_t idx = 0u; idx < l_z_count && l_rice_ok; ++idx) {
                uint32_t ri = (uint32_t)(idx / l_len);
                uint32_t rj = (uint32_t)(idx % l_len);
                size_t l_cap = lotrs_poly_bytes(a_par);
                l_z_bufs[idx] = DAP_NEW_Z_SIZE(uint8_t, l_cap);
                if (!l_z_bufs[idx]) { l_rice_ok = 0; break; }
                size_t l_written = 0u;
                int l_rc = lotrs_poly_pack_rice(l_z_bufs[idx], l_cap,
                                                l_z[ri].polys[rj], a_par,
                                                l_rice_k_z, l_rice_bound_z, &l_written);
                if (l_rc != 0) { l_rice_ok = 0; break; }
                l_z_sizes[idx] = l_written;
                l_z_rice_total += l_written;
            }

            if (!l_rice_ok) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM;
            }

            size_t l_z_wire = l_z_count * 4u + l_z_rice_total;
            size_t l_hdr_bytes = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
            size_t l_total = l_hdr_bytes + l_T_bytes + l_c_bytes + l_z_wire;

            /* Pad to fixed size to prevent norm leak through variable Rice coding.
             * Random padding (not zero-fill) so actual size is not distinguishable. */
            size_t l_max_total = chipmunk_ring_sig_bytes_max(a_par, l_N);
            size_t l_alloc = (l_max_total > l_total) ? l_max_total : l_total;

            a_sig->data = DAP_NEW_Z_SIZE(uint8_t, l_alloc);
            if (!a_sig->data) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -ENOMEM;
            }
            a_sig->len = l_alloc;
            /* Fill padding with random bytes to hide actual signature size */
            if (l_alloc > l_total) {
                dap_random_bytes(a_sig->data + l_total, l_alloc - l_total);
            }

            chipmunk_ring_header_t l_hdr = {
                .magic = CHIPMUNK_RING_MAGIC,
                .version = CHIPMUNK_RING_VERSION,
                .d = a_par->d,
                .N = l_N,
                .rice_k_z = l_rice_k_z,
                .rice_bound_z = l_rice_bound_z,
                .flags = 0u,
            };
            /* Copy pre-computed key image into header */
            l_hdr.key_image_len = (uint32_t)l_key_image_len;
            memcpy(l_hdr.key_image, l_key_image, l_key_image_len);
            l_rc = s_param_hash(l_hdr.param_hash, a_par);
            if (l_rc != 0) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return l_rc;
            }
            uint8_t *l_p = a_sig->data;
            dap_serialize_result_t l_ser = dap_serialize_to_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                       &l_hdr, l_p, l_hdr_bytes, NULL);
            if (l_ser.error_code != 0) {
                for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
                DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);
                s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks); return -EFAULT;
            }
            l_p += l_hdr_bytes;

            /* Write raw T. */
            for (uint32_t i = 0u; i < l_N; ++i) {
                lotrs_polyvec_pack(l_p, lotrs_polyvec_bytes(a_par, a_par->k), &l_T[i], a_par);
                l_p += lotrs_polyvec_bytes(a_par, a_par->k);
            }

            /* Write raw c. */
            for (uint32_t i = 0u; i < l_N; ++i) {
                lotrs_poly_pack(l_p, lotrs_poly_bytes(a_par), l_c_arr[i], a_par);
                l_p += lotrs_poly_bytes(a_par);
            }

            /* Write Rice-coded z. */
            for (size_t idx = 0u; idx < l_z_count; ++idx) {
                uint32_t l_sz32 = (uint32_t)l_z_sizes[idx];
                memcpy(l_p, &l_sz32, 4u);
                l_p += 4u;
                memcpy(l_p, l_z_bufs[idx], l_z_sizes[idx]);
                l_p += l_z_sizes[idx];
            }

            for (size_t k = 0u; k < l_z_count; ++k) DAP_DELETE(l_z_bufs[k]);
            DAP_DELETE(l_z_bufs); DAP_DELETE(l_z_sizes);

            /* Cleanup. */
            s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks);
            return 0;
        }
        /* Retry: norm exceeded. */
    }

    /* All retries exhausted. */
    debug_if(1, L_DEBUG, "CRIN sign: rejection sampling failed after %u retries", CHIPMUNK_RING_NONINT_MAX_RETRIES);
    s_sign_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_retry_seed, l_sorted_ring.pks);
    return -EAGAIN;
}

/* --- Verify --- */

/* Cleanup helper for verify function resources. */
static void s_verify_cleanup(lotrs_polymat_t *a_A,
                             lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                             lotrs_polyvec_t *a_z, uint32_t a_N,
                             chipmunk_ring_pk_t *a_sorted_pks)
{
    if (a_T && a_c_arr && a_z) {
        for (uint32_t i = 0u; i < a_N; ++i) {
            lotrs_polyvec_free(&a_T[i]);
            lotrs_poly_free(a_c_arr[i]);
            lotrs_polyvec_free(&a_z[i]);
        }
    }
    DAP_DELETE(a_T);
    DAP_DELETE(a_c_arr);
    DAP_DELETE(a_z);
    if (a_A) lotrs_polymat_free(a_A);
    if (a_sorted_pks) DAP_DELETE(a_sorted_pks);
}

/* Cleanup for deserialization phase (no A matrix yet). */
static void s_verify_cleanup_arrays(lotrs_polyvec_t *a_T, lotrs_poly_t **a_c_arr,
                                    lotrs_polyvec_t *a_z, uint32_t a_N,
                                    chipmunk_ring_pk_t *a_sorted_pks)
{
    s_verify_cleanup(NULL, a_T, a_c_arr, a_z, a_N, a_sorted_pks);
}

int chipmunk_ring_verify(const chipmunk_ring_sig_t *a_sig,
                            const lotrs_params_t *a_par,
                            const chipmunk_ring_table_t *a_ring,
                            const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_sig || !a_sig->data || !a_par || !a_ring || !a_msg) return -EINVAL;
    if (a_ring->N < CHIPMUNK_RING_N_MIN) {
        log_it(L_ERROR, "CRIN verify: ring size %u below minimum %u", a_ring->N, CHIPMUNK_RING_N_MIN);
        return -EINVAL;
    }
    if (s_validate_table(a_ring, a_par) != 0) return -EINVAL;
    size_t l_hdr_bytes = dap_serialize_calc_size_raw(&s_chipmunk_ring_header_schema, NULL, NULL, NULL);
    if (a_sig->len < l_hdr_bytes) return -EINVAL;

    /* Canonical sort: copy ring, sort, reject duplicates.
     * The verifier must use the same canonical ordering as the signer. */
    chipmunk_ring_table_t l_sorted_ring;
    l_sorted_ring.N = a_ring->N;
    l_sorted_ring.pks = DAP_NEW_Z_COUNT(chipmunk_ring_pk_t, a_ring->N);
    if (!l_sorted_ring.pks) return -ENOMEM;
    for (uint32_t i = 0u; i < a_ring->N; ++i) {
        l_sorted_ring.pks[i] = a_ring->pks[i];
    }
    qsort(l_sorted_ring.pks, l_sorted_ring.N, sizeof(chipmunk_ring_pk_t), s_pk_cmp);
    for (uint32_t i = 1u; i < l_sorted_ring.N; ++i) {
        if (s_pk_cmp(&l_sorted_ring.pks[i - 1u], &l_sorted_ring.pks[i]) == 0) {
            log_it(L_ERROR, "CRIN verify: duplicate public keys in ring");
            DAP_DELETE(l_sorted_ring.pks);
            return -EINVAL;
        }
    }

    int l_rc;

    /* Read header. */
    chipmunk_ring_header_t l_hdr = {0};
    dap_deserialize_result_t l_deser = dap_serialize_from_buffer_raw(&s_chipmunk_ring_header_schema,
                                                                     a_sig->data, l_hdr_bytes,
                                                                     &l_hdr, NULL);
    if (l_deser.error_code != 0) return -EINVAL;
    if (l_hdr.magic != CHIPMUNK_RING_MAGIC) return -EINVAL;
    if (l_hdr.version != CHIPMUNK_RING_VERSION) return -EINVAL;
    if (l_hdr.d != a_par->d) return -EINVAL;
    if (l_hdr.N != l_sorted_ring.N) return -EINVAL;

    /* Verify parameter hash. */
    uint8_t l_expected_hash[16];
    l_rc = s_param_hash(l_expected_hash, a_par);
    if (l_rc != 0) return l_rc;
    if (memcmp(l_hdr.param_hash, l_expected_hash, 16u) != 0) return -EINVAL;

    const uint32_t l_N = l_sorted_ring.N;
    const uint32_t l_d = a_par->d;
    const uint64_t l_q = a_par->q;
    const uint32_t l_len = a_par->l + a_par->k;

    /* Deserialize T_i (raw), c_i (raw), z_i (Rice-coded). */
    const uint8_t *l_p = a_sig->data + l_hdr_bytes;
    size_t l_T_bytes = lotrs_polyvec_bytes(a_par, a_par->k);
    size_t l_c_bytes = lotrs_poly_bytes(a_par);

    lotrs_polyvec_t *l_T = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    lotrs_poly_t **l_c_arr = DAP_NEW_Z_COUNT(lotrs_poly_t *, l_N);
    lotrs_polyvec_t *l_z = DAP_NEW_Z_COUNT(lotrs_polyvec_t, l_N);
    if (!l_T || !l_c_arr || !l_z) {
        DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
        return -ENOMEM;
    }

    for (uint32_t i = 0u; i < l_N; ++i) {
        l_T[i] = lotrs_polyvec_alloc(a_par, a_par->k);
        l_c_arr[i] = lotrs_poly_alloc(a_par);
        l_z[i] = lotrs_polyvec_alloc(a_par, l_len);
        if (!l_T[i].polys || !l_c_arr[i] || !l_z[i].polys) {
            for (uint32_t j = 0u; j <= i; ++j) {
                lotrs_polyvec_free(&l_T[j]); lotrs_poly_free(l_c_arr[j]); lotrs_polyvec_free(&l_z[j]);
            }
            DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
            return -ENOMEM;
        }
        lotrs_polyvec_unpack(&l_T[i], l_p, l_T_bytes, a_par);
        l_p += l_T_bytes;
    }

    /* Deserialize c_i (raw). */
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_poly_unpack(l_c_arr[i], l_p, l_c_bytes, a_par);
        l_p += l_c_bytes;
    }

    /* Deserialize z_i with Rice coding. */
    int64_t l_rice_bound_z = l_hdr.rice_bound_z;
    uint32_t l_rice_k_z = l_hdr.rice_k_z;
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t j = 0u; j < l_len; ++j) {
            if (l_p + 4u > a_sig->data + a_sig->len) {
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -EINVAL;
            }
            uint32_t l_sz32 = 0u;
            memcpy(&l_sz32, l_p, 4u);
            l_p += 4u;
            if (l_p + l_sz32 > a_sig->data + a_sig->len) {
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -EINVAL;
            }
            size_t l_consumed = 0u;
            l_rc = lotrs_poly_unpack_rice(l_z[i].polys[j], l_p, l_sz32,
                                          a_par, l_rice_k_z, l_rice_bound_z, &l_consumed);
            if (l_rc != 0) {
                s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc;
            }
            l_p += l_sz32;
        }
    }

    /* Recompute FS challenge: c_total = H(T_0..T_{N-1}, key_image, msg). */
    lotrs_xof_t *l_xof_c = lotrs_xof_new((const uint8_t *)"crin-challenge-v1", 17u);
    if (!l_xof_c) { s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
    l_rc = lotrs_xof_absorb(l_xof_c, a_msg, a_msg_len);
    if (l_rc != 0) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc; }
    /* Absorb key image for linkability binding */
    l_rc = lotrs_xof_absorb(l_xof_c, l_hdr.key_image, l_hdr.key_image_len);
    if (l_rc != 0) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc; }
    for (uint32_t i = 0u; i < l_N; ++i) {
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            uint8_t l_buf[LOTRS_D_MAX * 8]; /* max poly bytes */
            lotrs_poly_pack(l_buf, sizeof(l_buf), l_T[i].polys[j], a_par);
            l_rc = lotrs_xof_absorb(l_xof_c, l_buf, lotrs_poly_bytes(a_par));
            if (l_rc != 0) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc; }
        }
    }

    lotrs_poly_t *l_c_total = lotrs_poly_alloc(a_par);
    if (!l_c_total) { lotrs_xof_free(l_xof_c); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
    l_rc = lotrs_sample_ternary(l_c_total, l_xof_c, a_par, a_par->w);
    lotrs_xof_free(l_xof_c);
    if (l_rc != 0) { lotrs_poly_free(l_c_total); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc; }

    /* Check c_total == Σ c_i. */
    lotrs_poly_t *l_c_sum = lotrs_poly_alloc(a_par);
    if (!l_c_sum) { lotrs_poly_free(l_c_total); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
    lotrs_poly_zero(l_c_sum, a_par);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_poly_add(l_c_sum, l_c_sum, l_c_arr[i], a_par);
    }
    for (uint32_t i = 0u; i < l_d; ++i) {
        if (l_c_total->coeffs[i] % l_q != l_c_sum->coeffs[i] % l_q) {
            debug_if(1, L_DEBUG, "CRIN verify: challenge sum mismatch at [%u]", i);
            lotrs_poly_free(l_c_total); lotrs_poly_free(l_c_sum);
            s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -EINVAL;
        }
    }
    lotrs_poly_free(l_c_total);
    lotrs_poly_free(l_c_sum);

    /* Generate A matrix. */
    lotrs_polymat_t l_A = lotrs_polymat_alloc(a_par, a_par->k, a_par->l);
    if (!l_A.rows) { s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
    const char *l_a_domain = "crin-A-v1";
    lotrs_xof_t *l_xof_a = lotrs_xof_new((const uint8_t *)l_a_domain, strlen(l_a_domain));
    if (!l_xof_a) { lotrs_polymat_free(&l_A); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
    for (uint32_t i = 0u; i < a_par->k; ++i) {
        for (uint32_t j = 0u; j < a_par->l; ++j) {
            l_rc = lotrs_sample_uniform(l_A.rows[i].polys[j], l_xof_a, a_par);
            if (l_rc != 0) {
                lotrs_xof_free(l_xof_a); lotrs_polymat_free(&l_A); s_verify_cleanup_arrays(l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return l_rc;
            }
        }
    }
    lotrs_xof_free(l_xof_a);

    /* For each i: norm check + algebraic check A*z_short + z_tail == T_i + c_i*pk[i]. */
    int l_match = 1;
    int64_t l_bound = l_rice_bound_z;
    for (uint32_t i = 0u; i < l_N && l_match; ++i) {
        /* Norm check: ‖z_i‖∞ < φ·η. */
        for (uint32_t j = 0u; j < l_len; ++j) {
            if (!lotrs_reject_infinity_norm(l_z[i].polys[j], l_bound, a_par)) {
                debug_if(1, L_DEBUG, "CRIN verify: norm check FAILED at member[%u][%u]", i, j);
                l_match = 0;
                break;
            }
        }
        if (!l_match) break;

        lotrs_polyvec_t l_z_short = { .polys = l_z[i].polys, .n = a_par->l };
        lotrs_polyvec_t l_z_tail  = { .polys = l_z[i].polys + a_par->l, .n = a_par->k };

        /* lhs = A * z_short + z_tail. */
        lotrs_polyvec_t l_lhs = lotrs_polyvec_alloc(a_par, a_par->k);
        if (!l_lhs.polys) { s_verify_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM; }
        lotrs_polymat_vecmul(&l_lhs, &l_A, &l_z_short, a_par);
        lotrs_polyvec_add(&l_lhs, &l_lhs, &l_z_tail, a_par);

        /* rhs = T_i + c_i * pk[i]. */
        lotrs_polyvec_t l_rhs = lotrs_polyvec_alloc(a_par, a_par->k);
        if (!l_rhs.polys) {
            lotrs_polyvec_free(&l_lhs); s_verify_cleanup(&l_A, l_T, l_c_arr, l_z, l_N, l_sorted_ring.pks); return -ENOMEM;
        }
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            lotrs_poly_t *l_cp = lotrs_poly_alloc(a_par);
            lotrs_poly_mul(l_cp, l_c_arr[i], l_sorted_ring.pks[i].a_hat.polys[j], a_par);
            lotrs_poly_add(l_rhs.polys[j], l_T[i].polys[j], l_cp, a_par);
            lotrs_poly_free(l_cp);
        }

        /* Canonicalize. */
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                l_lhs.polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_lhs.polys[j]->coeffs[kk], l_q);
                l_rhs.polys[j]->coeffs[kk] = (uint64_t)lotrs_mod_reduce(
                    (__int128_t)(int64_t)l_rhs.polys[j]->coeffs[kk], l_q);
            }
        }

        /* Compare lhs == rhs. */
        for (uint32_t j = 0u; j < a_par->k; ++j) {
            for (uint32_t kk = 0u; kk < l_d; ++kk) {
                if (l_lhs.polys[j]->coeffs[kk] != l_rhs.polys[j]->coeffs[kk]) {
                    debug_if(1, L_DEBUG, "CRIN verify: algebraic FAILED at member[%u][%u][%u]", i, j, kk);
                    l_match = 0;
                    break;
                }
            }
            if (!l_match) break;
        }

        lotrs_polyvec_free(&l_lhs);
        lotrs_polyvec_free(&l_rhs);
    }

    lotrs_polymat_free(&l_A);
    for (uint32_t i = 0u; i < l_N; ++i) {
        lotrs_polyvec_free(&l_T[i]); lotrs_poly_free(l_c_arr[i]); lotrs_polyvec_free(&l_z[i]);
    }
    DAP_DELETE(l_T); DAP_DELETE(l_c_arr); DAP_DELETE(l_z);
    DAP_DELETE(l_sorted_ring.pks);

    return l_match ? 0 : -EINVAL;
}

/* =========================================================================
 * Anonymous Ring Signature via MRNG (threshold=1, O(log N) size)
 *
 * Uses MRNG's algebraic aggregation + halving fold for logarithmic-size
 * single-signer anonymous ring signatures.  Replaces the O(N) per-member
 * structure of the legacy Ring V2 path.
 *
 * Statement: "I know sk_j for pk_j in {pk_0, ..., pk_{N-1}}"
 * Witness: b ∈ {0,1}^N (indicator), x = sk_j
 * Proof: halving fold on b̃ = (b, b∘(b-1)), depth = ceil(log2(N)) + 1
 * Size: ~20-40 KB for N=2..256 (vs O(N) for legacy Ring V2)
 * ========================================================================= */

/*
 * Generate an anonymous ring signature using MRNG with threshold=1.
 *
 * @param a_out_buf     Receives allocated signature buffer. Caller owns.
 * @param a_out_size    Receives signature size.
 * @param a_signer_sk   Signer's LRS secret key.
 * @param a_ring        Ring of LRS public keys.
 * @param a_ring_size   Number of keys in ring (8..256).
 * @param a_message     Message to sign.
 * @param a_message_size Message length.
 * @param a_randomness_seed 32-byte CSPRNG seed.
 * @return CHIPMUNK_RING_OK on success, error code on failure.
 */
chipmunk_ring_error_t chipmunk_ring_sign_anonymous(
    uint8_t **a_out_buf, size_t *a_out_size,
    const chipmunk_lrs_secret_key_t *a_signer_sk,
    const chipmunk_lrs_public_key_t *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size,
    const uint8_t a_randomness_seed[32])
{
    if (!a_out_buf || !a_out_size || !a_signer_sk || !a_ring
        || !a_message || !a_randomness_seed) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (a_ring_size < CHIPMUNK_RING_N_MIN || a_ring_size > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }

    /* MRNG sign with threshold=1, single signer */
    const chipmunk_lrs_secret_key_t *l_sk_arr[1] = { a_signer_sk };
    return chipmunk_ring_sign_to_bytes(
        a_out_buf, a_out_size,
        l_sk_arr, 1u,  /* 1 signer */
        a_ring, (uint32_t)a_ring_size,
        1u,  /* threshold = 1 */
        a_message, a_message_size,
        NULL, 0u,  /* no extra context */
        a_randomness_seed);
}

/*
 * Verify an anonymous ring signature (MRNG with threshold=1).
 *
 * @param a_buf         Signature buffer.
 * @param a_buf_size    Signature size.
 * @param a_ring        Ring of LRS public keys.
 * @param a_ring_size   Number of keys in ring.
 * @param a_message     Message that was signed.
 * @param a_message_size Message length.
 * @return CHIPMUNK_RING_OK if valid, error code otherwise.
 */
chipmunk_ring_error_t chipmunk_ring_verify_anonymous(
    const uint8_t *a_buf, size_t a_buf_size,
    const chipmunk_lrs_public_key_t *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size)
{
    if (!a_buf || !a_ring || !a_message) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (a_ring_size < CHIPMUNK_RING_N_MIN || a_ring_size > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }

    return chipmunk_ring_verify_from_bytes(
        a_buf, a_buf_size,
        a_ring, (uint32_t)a_ring_size,
        a_message, a_message_size,
        NULL, 0u);  /* no extra context */
}

/*
 * Linkability for anonymous ring signatures.
 * Extracts the link tag (T) from the signature wire format and compares.
 *
 * @param a_buf1, a_buf_size1  First signature.
 * @param a_buf2, a_buf_size2  Second signature.
 * @return 1 if same signer (linked), 0 if different, negative on error.
 */
int chipmunk_ring_link_anonymous(
    const uint8_t *a_buf1, size_t a_buf_size1,
    const uint8_t *a_buf2, size_t a_buf_size2)
{
    if (!a_buf1 || !a_buf2) return -EINVAL;
    if (a_buf_size1 < CHIPMUNK_MRING_HEADER_BYTES ||
        a_buf_size2 < CHIPMUNK_MRING_HEADER_BYTES) return -EINVAL;

    /* Parse headers to extract T block offset */
    chipmunk_mring_header_t l_hdr1, l_hdr2;
    int rc1 = chipmunk_mring_header_read(&l_hdr1, a_buf1, a_buf_size1);
    int rc2 = chipmunk_mring_header_read(&l_hdr2, a_buf2, a_buf_size2);
    if (rc1 != 0 || rc2 != 0) return -EINVAL;

    /* T block starts at fixed offset */
    uint32_t l_t_off = chipmunk_mring_section_off_T();
    if (a_buf_size1 < l_t_off + CHIPMUNK_MRING_T_BYTES ||
        a_buf_size2 < l_t_off + CHIPMUNK_MRING_T_BYTES) return -EINVAL;

    /* Compare T blocks (constant-time) */
    const uint8_t *l_t1 = a_buf1 + l_t_off;
    const uint8_t *l_t2 = a_buf2 + l_t_off;
    uint8_t l_diff = 0u;
    for (size_t i = 0u; i < CHIPMUNK_MRING_T_BYTES; ++i) {
        l_diff |= l_t1[i] ^ l_t2[i];
    }
    return l_diff == 0u ? 1 : 0;
}
