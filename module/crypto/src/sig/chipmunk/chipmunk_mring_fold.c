/*
 * CR-11.G Phase 7.7 — MRNG halving fold over R_q^{(e)} (M4 / G3.1 §4).
 * Design lock: MRNG_M4_FOLD.md
 */

#include <errno.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_shake256.h"
#include "chipmunk_lrs.h"
#include "chipmunk_mring_fold.h"
#include "chipmunk_mring_statement.h"
#include "chipmunk_mring_transcript.h"
#include "chipmunk_mring.h"

#define LOG_TAG "chipmunk_mring_fold"

#define MRING_FOLD_OPENING_DOMAIN    "MRNG-M4-fold-opening-v1"
#define MRING_LEAF_MASK_DOMAIN       "MRNG-M4-leaf-mask-v1"

typedef struct leaf_xof_reader {
    uint64_t st[25];
    uint8_t  block[DAP_SHAKE256_RATE];
    size_t   pos;
    size_t   avail;
} leaf_xof_reader_t;

static void s_leaf_xof_init(leaf_xof_reader_t *a_r,
                            const char *a_domain,
                            const uint8_t *a_seed, size_t a_seed_len)
{
    memset(a_r, 0, sizeof(*a_r));
    size_t l_domain_len = strlen(a_domain);
    size_t l_abs_len = l_domain_len + a_seed_len;
    uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
    if (!l_abs) return;
    memcpy(l_abs, a_domain, l_domain_len);
    memcpy(l_abs + l_domain_len, a_seed, a_seed_len);
    dap_hash_shake256_absorb(a_r->st, l_abs, l_abs_len);
    DAP_DELETE(l_abs);
}

static uint8_t s_leaf_xof_u8(leaf_xof_reader_t *a_r)
{
    if (a_r->pos == a_r->avail) {
        dap_hash_shake256_squeezeblocks(a_r->block, 1u, a_r->st);
        a_r->pos = 0;
        a_r->avail = sizeof(a_r->block);
    }
    return a_r->block[a_r->pos++];
}

static uint64_t s_leaf_xof_u64(leaf_xof_reader_t *a_r)
{
    uint8_t l_b[8];
    for (size_t i = 0u; i < sizeof(l_b); ++i) {
        l_b[i] = s_leaf_xof_u8(a_r);
    }
    return ((uint64_t)l_b[0])
           | ((uint64_t)l_b[1] << 8)
           | ((uint64_t)l_b[2] << 16)
           | ((uint64_t)l_b[3] << 24)
           | ((uint64_t)l_b[4] << 32)
           | ((uint64_t)l_b[5] << 40)
           | ((uint64_t)l_b[6] << 48)
           | ((uint64_t)l_b[7] << 56);
}

static int64_t s_center_coeff_i64_q(int32_t a_c, uint64_t q)
{
    int64_t l_v = a_c;
    const int64_t l_half = (int64_t)q / 2;
    const int64_t l_q = (int64_t)q;
    /* Branchless: mask is -1 (all ones) when condition holds, 0 otherwise. */
    const int64_t l_hi = -(int64_t)(l_v > l_half);
    const int64_t l_lo = -(int64_t)(l_v < -l_half);
    return l_v - (l_q & l_hi) + (l_q & l_lo);
}

static int64_t s_center_coeff_i64(int32_t a_c)
{
    return s_center_coeff_i64_q(a_c, (uint64_t)CHIPMUNK_Q);
}

static int32_t s_reduce_coeff_i64_q(int64_t a_v, uint64_t q)
{
    const int64_t l_q = (int64_t)q;
    int64_t l_r = a_v % l_q;
    const int64_t l_half = l_q / 2;
    /* Branchless: normalize into [-half, half). */
    const int64_t l_neg = -(int64_t)(l_r < 0);
    const int64_t l_hi  = -(int64_t)(l_r > l_half);
    l_r += l_q & l_neg;
    l_r -= l_q & l_hi;
    return (int32_t)l_r;
}

static int32_t s_reduce_coeff_i64(int64_t a_v)
{
    return s_reduce_coeff_i64_q(a_v, (uint64_t)CHIPMUNK_Q);
}

static int s_leaf_mask_inf_norm(const int64_t a_coeffs[CHIPMUNK_MRING_N],
                                int64_t *a_max_out)
{
    int64_t l_max = 0;
    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        int64_t l_a = a_coeffs[i];
        if (l_a < 0) {
            l_a = -l_a;
        }
        if (l_a > l_max) {
            l_max = l_a;
        }
    }
    if (a_max_out) {
        *a_max_out = l_max;
    }
    return 0;
}

static void s_ext_apply_leaf_mask(chipmunk_fq6_ext_t *a_b,
                                  const int64_t a_omega[CHIPMUNK_MRING_N],
                                  int32_t a_sign)
{
    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        int64_t l_v = s_center_coeff_i64_q(a_b->c[0].coeffs[i],
                                           (uint64_t)CHIPMUNK_Q);
        l_v += (int64_t)a_sign * a_omega[i];
        a_b->c[0].coeffs[i] = s_reduce_coeff_i64_q(l_v,
                                                    (uint64_t)CHIPMUNK_Q);
    }
    chipmunk_fq6_ext_canonicalize_q(a_b, (uint64_t)CHIPMUNK_Q);
}

int64_t chipmunk_mring_leaf_bound_for_depth(uint32_t a_fold_depth)
{
    if (a_fold_depth == 0u
        || a_fold_depth > CHIPMUNK_MRING_FOLD_DEPTH_MAX) {
        return 0;
    }
    int64_t l_w = 1;
    for (uint32_t i = 0u; i < a_fold_depth; ++i) {
        l_w *= (int64_t)CHIPMUNK_MRING_WC;
    }
    return l_w;
}

int chipmunk_mring_leaf_mask_sample(int64_t a_out[CHIPMUNK_MRING_N],
                                    const uint8_t
                                        a_leaf_mask_seed[CHIPMUNK_MRING_HASH_BYTES],
                                    int64_t a_bound)
{
    if (!a_out || !a_leaf_mask_seed || a_bound <= 0
        || a_bound > CHIPMUNK_MRING_LEAF_BOUND_MAX) {
        return -EINVAL;
    }

    leaf_xof_reader_t l_reader;
    s_leaf_xof_init(&l_reader, MRING_LEAF_MASK_DOMAIN,
                    a_leaf_mask_seed, CHIPMUNK_MRING_HASH_BYTES);

    const uint64_t l_range = (uint64_t)(2 * a_bound + 1);
    const uint64_t l_threshold = UINT64_MAX - (UINT64_MAX % l_range);

    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        uint64_t l_v;
        do {
            l_v = s_leaf_xof_u64(&l_reader);
        } while (l_v >= l_threshold);
        a_out[i] = (int64_t)(l_v % l_range) - a_bound;
    }
    return 0;
}

int chipmunk_mring_leaf_mask_pack(uint8_t *a_out, size_t a_out_size,
                                  const int64_t a_coeffs[CHIPMUNK_MRING_N],
                                  int64_t a_pack_bound)
{
    if (!a_out || !a_coeffs || a_pack_bound <= 0
        || a_pack_bound > CHIPMUNK_MRING_LEAF_BOUND_MAX
        || a_out_size < (size_t)CHIPMUNK_MRING_LEAF_MASK_BYTES) {
        return -EINVAL;
    }

    memset(a_out, 0, CHIPMUNK_MRING_LEAF_MASK_BYTES);
    uint64_t l_acc = 0;
    uint32_t l_bits = 0;
    size_t l_pos = 0;

    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        const int64_t l_c = a_coeffs[i];
        if (l_c < -a_pack_bound || l_c > a_pack_bound) {
            return -EINVAL;
        }
        const uint64_t l_v = (uint64_t)(l_c + a_pack_bound);
        l_acc |= l_v << l_bits;
        l_bits += CHIPMUNK_MRING_LEAF_BITS;
        while (l_bits >= 8u) {
            if (l_pos >= CHIPMUNK_MRING_LEAF_MASK_BYTES) {
                return -EOVERFLOW;
            }
            a_out[l_pos++] = (uint8_t)(l_acc & 0xffu);
            l_acc >>= 8u;
            l_bits -= 8u;
        }
    }

    return (l_pos == CHIPMUNK_MRING_LEAF_MASK_BYTES && l_bits == 0) ? 0
                                                                     : -EOVERFLOW;
}

int chipmunk_mring_leaf_mask_unpack(int64_t a_out[CHIPMUNK_MRING_N],
                                    const uint8_t *a_in, size_t a_in_size,
                                    int64_t a_pack_bound)
{
    if (!a_out || !a_in || a_pack_bound <= 0
        || a_pack_bound > CHIPMUNK_MRING_LEAF_BOUND_MAX
        || a_in_size < (size_t)CHIPMUNK_MRING_LEAF_MASK_BYTES) {
        return -EINVAL;
    }

    uint64_t l_acc = 0;
    uint32_t l_bits = 0;
    size_t l_pos = 0;
    const uint64_t l_mask = (1ull << CHIPMUNK_MRING_LEAF_BITS) - 1ull;
    const uint64_t l_max = (uint64_t)(2 * a_pack_bound);

    for (size_t i = 0u; i < CHIPMUNK_MRING_N; ++i) {
        while (l_bits < CHIPMUNK_MRING_LEAF_BITS) {
            if (l_pos >= CHIPMUNK_MRING_LEAF_MASK_BYTES) {
                return -EINVAL;
            }
            l_acc |= ((uint64_t)a_in[l_pos++]) << l_bits;
            l_bits += 8u;
        }
        const uint64_t l_v = l_acc & l_mask;
        l_acc >>= CHIPMUNK_MRING_LEAF_BITS;
        l_bits -= CHIPMUNK_MRING_LEAF_BITS;
        if (l_v > l_max) {
            return -EINVAL;
        }
        a_out[i] = (int64_t)l_v - a_pack_bound;
    }

    return (l_pos == CHIPMUNK_MRING_LEAF_MASK_BYTES && l_bits == 0) ? 0
                                                                     : -EINVAL;
}

/* ------------------------------------------------------------------ */
/*  ext vector helpers                                                 */
/* ------------------------------------------------------------------ */

typedef struct extvec {
    chipmunk_fq6_ext_t *slots;
    uint32_t length;
} extvec_t;

static int s_extvec_alloc(extvec_t *a_v, uint32_t a_len)
{
    if (!a_v || a_len == 0u) {
        return -EINVAL;
    }
    a_v->slots = DAP_NEW_Z_COUNT(chipmunk_fq6_ext_t, a_len);
    if (!a_v->slots) {
        return -ENOMEM;
    }
    a_v->length = a_len;
    for (uint32_t i = 0u; i < a_len; ++i) {
        chipmunk_fq6_ext_zero(&a_v->slots[i]);
    }
    return 0;
}

static void s_extvec_free(extvec_t *a_v)
{
    if (!a_v) {
        return;
    }
    DAP_DELETE(a_v->slots);
    a_v->slots = NULL;
    a_v->length = 0u;
}

static bool s_ext_equal(const chipmunk_fq6_ext_t *a,
                        const chipmunk_fq6_ext_t *b)
{
    for (uint32_t j = 0u; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        for (size_t k = 0u; k < CHIPMUNK_N; ++k) {
            if (a->c[j].coeffs[k] != b->c[j].coeffs[k]) {
                return false;
            }
        }
    }
    return true;
}

static int s_ext_inner_product(chipmunk_fq6_ext_t *a_out,
                               const extvec_t *a_left,
                               const extvec_t *a_right)
{
    if (!a_out || !a_left || !a_right) {
        return -EINVAL;
    }
    if (a_left->length != a_right->length || a_left->length == 0u) {
        return -EINVAL;
    }
    chipmunk_fq6_ext_zero(a_out);
    for (uint32_t i = 0u; i < a_left->length; ++i) {
        chipmunk_fq6_ext_t l_prod;
        int rc = chipmunk_fq6_ext_mul(&l_prod,
                                        &a_left->slots[i],
                                        &a_right->slots[i]);
        if (rc != 0) {
            return rc;
        }
        rc = chipmunk_fq6_ext_add(a_out, a_out, &l_prod);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int s_embed_polyvec(extvec_t *a_out,
                           const chipmunk_mring_polyvec_t *a_in)
{
    if (!a_out || !a_in || !a_in->slots) {
        return -EINVAL;
    }
    if (a_out->length != a_in->length) {
        return -EINVAL;
    }
    for (uint32_t i = 0u; i < a_in->length; ++i) {
        chipmunk_fq6_ext_embed(&a_out->slots[i], &a_in->slots[i]);
    }
    return 0;
}

static int s_build_padded_public(extvec_t *a_P,
                                   uint32_t a_padded_dim,
                                   uint32_t a_n_ring,
                                   const chipmunk_poly_t *a_pks,
                                   const chipmunk_poly_t *a_c)
{
    chipmunk_mring_polyvec_t l_P;
    const uint32_t l_aug = chipmunk_mring_augmented_dim(a_n_ring);
    int rc = chipmunk_mring_polyvec_alloc(&l_P, l_aug);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_mring_eval_public_P(&l_P, a_c, a_pks, a_n_ring);
    if (rc != 0) {
        chipmunk_mring_polyvec_free(&l_P);
        return rc;
    }
    rc = s_extvec_alloc(a_P, a_padded_dim);
    if (rc != 0) {
        chipmunk_mring_polyvec_free(&l_P);
        return rc;
    }
    for (uint32_t i = 0u; i < l_aug; ++i) {
        chipmunk_fq6_ext_embed(&a_P->slots[i], &l_P.slots[i]);
    }
    chipmunk_mring_polyvec_free(&l_P);
    return 0;
}

static int s_build_padded_witness(extvec_t *a_b,
                                    uint32_t a_padded_dim,
                                    const uint8_t *a_b_indicator,
                                    uint32_t a_n_ring)
{
    chipmunk_mring_polyvec_t l_b;
    const uint32_t l_aug = chipmunk_mring_augmented_dim(a_n_ring);
    int rc = chipmunk_mring_polyvec_alloc(&l_b, l_aug);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_mring_augment_witness(&l_b, a_b_indicator, a_n_ring);
    if (rc != 0) {
        chipmunk_mring_polyvec_free(&l_b);
        return rc;
    }
    rc = s_extvec_alloc(a_b, a_padded_dim);
    if (rc != 0) {
        chipmunk_mring_polyvec_free(&l_b);
        return rc;
    }
    for (uint32_t i = 0u; i < l_aug; ++i) {
        chipmunk_fq6_ext_embed(&a_b->slots[i], &l_b.slots[i]);
    }
    chipmunk_mring_polyvec_free(&l_b);
    return 0;
}

static uint32_t s_fold_opening_index(uint32_t a_round, uint32_t a_side,
                                     uint32_t a_y_deg, uint32_t a_lane)
{
    return (((a_round * 2u + a_side) * (uint32_t)CHIPMUNK_FQ6_EXT_DEG
             + a_y_deg)
            * (uint32_t)CHIPMUNK_MRING_K_PK)
           + a_lane;
}

int chipmunk_mring_fold_derive_opening(
    chipmunk_poly_t a_r_out[CHIPMUNK_MRING_K_PK],
    const uint8_t a_fold_opening_seed[CHIPMUNK_MRING_FOLD_OPENING_BYTES],
    uint32_t a_round, uint32_t a_side, uint32_t a_y_deg)
{
    if (!a_r_out || !a_fold_opening_seed) {
        return -EINVAL;
    }
    if (a_side > 1u
        || a_y_deg >= (uint32_t)CHIPMUNK_FQ6_EXT_DEG) {
        return -EINVAL;
    }

    for (uint32_t k = 0u; k < (uint32_t)CHIPMUNK_MRING_K_PK; ++k) {
        const uint32_t l_idx =
            s_fold_opening_index(a_round, a_side, a_y_deg, k);
        const int rc = chipmunk_lrs_h_to_short_poly(
            &a_r_out[k],
            MRING_FOLD_OPENING_DOMAIN,
            CHIPMUNK_LRS_PARAMS_C0,
            a_fold_opening_seed,
            l_idx,
            CHIPMUNK_MRING_BETA_W);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int s_ext_vcom_commit(chipmunk_fq6_ext_t *a_C_out,
                             const chipmunk_mring_vcom_gens_t *a_gens,
                             const chipmunk_fq6_ext_t *a_x,
                             const uint8_t a_fold_opening_seed[32],
                             uint32_t a_round, uint32_t a_side)
{
    if (!a_C_out || !a_gens || !a_x || !a_fold_opening_seed) {
        return -EINVAL;
    }
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        chipmunk_poly_t l_r[CHIPMUNK_MRING_K_PK];
        int rc = chipmunk_mring_fold_derive_opening(
            l_r, a_fold_opening_seed, a_round, a_side, j);
        if (rc != 0) {
            return rc;
        }
        rc = chipmunk_mring_vcom_commit(&a_C_out->c[j], a_gens,
                                        &a_x->c[j], l_r);
        if (rc != 0) {
            return rc;
        }
    }
    chipmunk_fq6_ext_canonicalize_q(a_C_out, (uint64_t)CHIPMUNK_Q);
    return 0;
}

static int s_ext_vcom_open(chipmunk_fq6_ext_t *a_x_out,
                           const chipmunk_fq6_ext_t *a_C,
                           const chipmunk_mring_vcom_gens_t *a_gens,
                           const uint8_t a_fold_opening_seed[32],
                           uint32_t a_round, uint32_t a_side)
{
    if (!a_x_out || !a_C || !a_gens || !a_fold_opening_seed) {
        return -EINVAL;
    }
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        chipmunk_poly_t l_r[CHIPMUNK_MRING_K_PK];
        int rc = chipmunk_mring_fold_derive_opening(
            l_r, a_fold_opening_seed, a_round, a_side, j);
        if (rc != 0) {
            return rc;
        }
        rc = chipmunk_mring_vcom_open(&a_x_out->c[j], &a_C->c[j],
                                      a_gens, l_r);
        if (rc != 0) {
            return rc;
        }
    }
    chipmunk_fq6_ext_canonicalize_q(a_x_out, (uint64_t)CHIPMUNK_Q);
    return 0;
}

static int s_one_fold_round(extvec_t *a_b,
                            extvec_t *a_P,
                            chipmunk_fq6_ext_t *a_rho,
                            chipmunk_fq6_ext_t *a_CL_out,
                            chipmunk_fq6_ext_t *a_CR_out,
                            const chipmunk_mring_vcom_gens_t *a_gens,
                            const uint8_t a_fold_opening_seed[32],
                            const uint8_t a_fs_seed[32],
                            uint32_t a_round_idx)
{
    const uint32_t l_len = a_b->length;
    if (l_len < 2u || (l_len & 1u) != 0u) {
        return -EINVAL;
    }
    const uint32_t l_h = l_len / 2u;

    extvec_t l_bL, l_bR, l_pL, l_pR;
    int rc = s_extvec_alloc(&l_bL, l_h);
    if (rc != 0) {
        return rc;
    }
    rc = s_extvec_alloc(&l_bR, l_h);
    if (rc != 0) {
        s_extvec_free(&l_bL);
        return rc;
    }
    rc = s_extvec_alloc(&l_pL, l_h);
    if (rc != 0) {
        s_extvec_free(&l_bR);
        s_extvec_free(&l_bL);
        return rc;
    }
    rc = s_extvec_alloc(&l_pR, l_h);
    if (rc != 0) {
        s_extvec_free(&l_pL);
        s_extvec_free(&l_bR);
        s_extvec_free(&l_bL);
        return rc;
    }

    for (uint32_t j = 0u; j < l_h; ++j) {
        l_bL.slots[j] = a_b->slots[j];
        l_bR.slots[j] = a_b->slots[l_h + j];
        l_pL.slots[j] = a_P->slots[j];
        l_pR.slots[j] = a_P->slots[l_h + j];
    }

    chipmunk_fq6_ext_t l_L, l_R;
    rc = s_ext_inner_product(&l_L, &l_bL, &l_pR);
    if (rc != 0) {
        goto cleanup_halves;
    }
    rc = s_ext_inner_product(&l_R, &l_bR, &l_pL);
    if (rc != 0) {
        goto cleanup_halves;
    }

    chipmunk_fq6_ext_canonicalize_q(&l_L, (uint64_t)CHIPMUNK_Q);
    chipmunk_fq6_ext_canonicalize_q(&l_R, (uint64_t)CHIPMUNK_Q);

    rc = s_ext_vcom_commit(a_CL_out, a_gens, &l_L,
                           a_fold_opening_seed, a_round_idx, 0u);
    if (rc != 0) {
        goto cleanup_halves;
    }
    rc = s_ext_vcom_commit(a_CR_out, a_gens, &l_R,
                           a_fold_opening_seed, a_round_idx, 1u);
    if (rc != 0) {
        goto cleanup_halves;
    }

    uint8_t l_fs[32];
    chipmunk_mring_transcript_fold_round_fs(l_fs, a_fs_seed, a_round_idx,
                                            a_CL_out, a_CR_out);

    chipmunk_fq6_ext_t l_x, l_x_inv;
    rc = chipmunk_fq6_ext_sample_challenge_q(&l_x, l_fs, 0u, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto cleanup_halves;
    }
    rc = chipmunk_fq6_ext_scalar_invert_q(&l_x_inv, &l_x, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto cleanup_halves;
    }

    extvec_t l_b_new, l_p_new;
    rc = s_extvec_alloc(&l_b_new, l_h);
    if (rc != 0) {
        goto cleanup_halves;
    }
    rc = s_extvec_alloc(&l_p_new, l_h);
    if (rc != 0) {
        s_extvec_free(&l_b_new);
        goto cleanup_halves;
    }

    for (uint32_t j = 0u; j < l_h; ++j) {
        chipmunk_fq6_ext_t l_xb, l_xinvp;
        rc = chipmunk_fq6_ext_mul(&l_xb, &l_x, &l_bR.slots[j]);
        if (rc != 0) {
            goto cleanup_new;
        }
        rc = chipmunk_fq6_ext_add(&l_b_new.slots[j], &l_bL.slots[j], &l_xb);
        if (rc != 0) {
            goto cleanup_new;
        }
        rc = chipmunk_fq6_ext_mul(&l_xinvp, &l_x_inv, &l_pR.slots[j]);
        if (rc != 0) {
            goto cleanup_new;
        }
        rc = chipmunk_fq6_ext_add(&l_p_new.slots[j], &l_pL.slots[j], &l_xinvp);
        if (rc != 0) {
            goto cleanup_new;
        }
    }

    chipmunk_fq6_ext_t l_xinvL, l_xR, l_sum;
    rc = chipmunk_fq6_ext_mul(&l_xinvL, &l_x_inv, &l_L);
    if (rc != 0) {
        goto cleanup_new;
    }
    rc = chipmunk_fq6_ext_mul(&l_xR, &l_x, &l_R);
    if (rc != 0) {
        goto cleanup_new;
    }
    rc = chipmunk_fq6_ext_add(&l_sum, &l_xinvL, &l_xR);
    if (rc != 0) {
        goto cleanup_new;
    }
    rc = chipmunk_fq6_ext_add(a_rho, a_rho, &l_sum);
    if (rc != 0) {
        goto cleanup_new;
    }

    for (uint32_t j = 0u; j < l_h; ++j) {
        a_b->slots[j] = l_b_new.slots[j];
        a_P->slots[j] = l_p_new.slots[j];
    }
    a_b->length = l_h;
    a_P->length = l_h;

    rc = 0;

cleanup_new:
    s_extvec_free(&l_p_new);
    s_extvec_free(&l_b_new);
cleanup_halves:
    s_extvec_free(&l_pR);
    s_extvec_free(&l_pL);
    s_extvec_free(&l_bR);
    s_extvec_free(&l_bL);
    return rc;
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

uint32_t chipmunk_mring_fold_padded_dim(uint32_t a_n_ring)
{
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(a_n_ring);
    if (l_depth == 0u) {
        return 0u;
    }
    return 1u << l_depth;
}

int chipmunk_mring_fold_proof_alloc(chipmunk_mring_fold_proof_t *a_proof,
                                    uint32_t a_fold_depth)
{
    if (!a_proof || a_fold_depth == 0u
        || a_fold_depth > CHIPMUNK_MRING_FOLD_DEPTH_MAX) {
        return -EINVAL;
    }
    memset(a_proof, 0, sizeof(*a_proof));
    a_proof->rounds = DAP_NEW_Z_COUNT(chipmunk_mring_fold_round_t, a_fold_depth);
    if (!a_proof->rounds) {
        return -ENOMEM;
    }
    a_proof->leaf_mask = DAP_NEW_Z_COUNT(int64_t, CHIPMUNK_MRING_N);
    if (!a_proof->leaf_mask) {
        DAP_DELETE(a_proof->rounds);
        a_proof->rounds = NULL;
        return -ENOMEM;
    }
    a_proof->fold_depth = a_fold_depth;
    for (uint32_t i = 0u; i < a_fold_depth; ++i) {
        chipmunk_fq6_ext_zero(&a_proof->rounds[i].C_L);
        chipmunk_fq6_ext_zero(&a_proof->rounds[i].C_R);
    }
    chipmunk_fq6_ext_zero(&a_proof->a_star);
    chipmunk_fq6_ext_zero(&a_proof->b_star);
    return 0;
}

void chipmunk_mring_fold_proof_free(chipmunk_mring_fold_proof_t *a_proof)
{
    if (!a_proof) {
        return;
    }
    DAP_DELETE(a_proof->rounds);
    DAP_DELETE(a_proof->leaf_mask);
    a_proof->rounds = NULL;
    a_proof->leaf_mask = NULL;
    a_proof->fold_depth = 0u;
}

int chipmunk_mring_fold_prove(chipmunk_mring_fold_proof_t *a_proof,
                              const uint8_t *a_b_indicator,
                              uint32_t a_n_ring,
                              const chipmunk_poly_t *a_pks,
                              const chipmunk_poly_t *a_c,
                              uint32_t a_t,
                              const chipmunk_poly_t *a_Y_pk,
                              const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                              const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
                              const uint8_t a_fold_opening_seed
                                  [CHIPMUNK_MRING_FOLD_OPENING_BYTES])
{
    if (!a_proof || !a_proof->rounds || !a_proof->leaf_mask || !a_b_indicator || !a_pks
        || !a_c || !a_Y_pk || !a_ring_hash || !a_fs_seed
        || !a_fold_opening_seed) {
        return -EINVAL;
    }
    memcpy(a_proof->fold_opening_seed, a_fold_opening_seed,
           CHIPMUNK_MRING_FOLD_OPENING_BYTES);

    chipmunk_mring_vcom_gens_t l_gens;
    int rc = chipmunk_mring_derive_vcom_generators(&l_gens, a_ring_hash);
    if (rc != 0) {
        return rc;
    }
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(a_n_ring);
    if (l_depth == 0u || a_proof->fold_depth != l_depth) {
        return -EINVAL;
    }
    const uint32_t l_pad = chipmunk_mring_fold_padded_dim(a_n_ring);
    if (l_pad == 0u) {
        return -EINVAL;
    }

    extvec_t l_b, l_P;
    chipmunk_fq6_ext_t l_rho;
    rc = s_build_padded_witness(&l_b, l_pad, a_b_indicator, a_n_ring);
    if (rc != 0) {
        return rc;
    }
    rc = s_build_padded_public(&l_P, l_pad, a_n_ring, a_pks, a_c);
    if (rc != 0) {
        s_extvec_free(&l_b);
        return rc;
    }

    chipmunk_poly_t l_rho_base;
    rc = chipmunk_mring_eval_public_rho_q(&l_rho_base, a_c, a_t, a_Y_pk,
                                          (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        s_extvec_free(&l_P);
        s_extvec_free(&l_b);
        return rc;
    }
    chipmunk_fq6_ext_embed(&l_rho, &l_rho_base);

    for (uint32_t r = 0u; r < l_depth; ++r) {
        rc = s_one_fold_round(&l_b, &l_P, &l_rho,
                              &a_proof->rounds[r].C_L,
                              &a_proof->rounds[r].C_R,
                              &l_gens, a_fold_opening_seed,
                              a_fs_seed, r);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG fold_prove: round %u failed (rc=%d)",
                   (unsigned)r, rc);
            s_extvec_free(&l_P);
            s_extvec_free(&l_b);
            return rc;
        }
    }

    if (l_b.length != 1u || l_P.length != 1u) {
        s_extvec_free(&l_P);
        s_extvec_free(&l_b);
        return -EINVAL;
    }

    a_proof->b_star = l_b.slots[0];
    a_proof->a_star = l_P.slots[0];
    chipmunk_fq6_ext_canonicalize_q(&a_proof->b_star, (uint64_t)CHIPMUNK_Q);
    chipmunk_fq6_ext_canonicalize_q(&a_proof->a_star, (uint64_t)CHIPMUNK_Q);

    const int64_t l_leaf_bound =
        chipmunk_mring_leaf_bound_for_depth(l_depth);
    rc = chipmunk_mring_leaf_mask_sample(a_proof->leaf_mask,
                                         a_fold_opening_seed,
                                         l_leaf_bound);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_prove: leaf_mask sample failed (rc=%d)",
               rc);
        s_extvec_free(&l_P);
        s_extvec_free(&l_b);
        return rc;
    }
    s_ext_apply_leaf_mask(&a_proof->b_star, a_proof->leaf_mask, +1);

    s_extvec_free(&l_P);
    s_extvec_free(&l_b);
    return 0;
}

int chipmunk_mring_fold_verify(const chipmunk_mring_fold_proof_t *a_proof,
                               uint32_t a_n_ring,
                               const chipmunk_poly_t *a_pks,
                               const chipmunk_poly_t *a_c,
                               uint32_t a_t,
                               const chipmunk_poly_t *a_Y_pk,
                               const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                               const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES])
{
    if (!a_proof || !a_proof->rounds || !a_proof->leaf_mask || !a_pks || !a_c || !a_Y_pk
        || !a_ring_hash || !a_fs_seed) {
        return -EINVAL;
    }

    chipmunk_mring_vcom_gens_t l_gens;
    int rc = chipmunk_mring_derive_vcom_generators(&l_gens, a_ring_hash);
    if (rc != 0) {
        return rc;
    }
    const uint32_t l_depth = chipmunk_mring_fold_depth_for(a_n_ring);
    if (l_depth == 0u || a_proof->fold_depth != l_depth) {
        return -EINVAL;
    }
    const uint32_t l_pad = chipmunk_mring_fold_padded_dim(a_n_ring);
    if (l_pad == 0u) {
        return -EINVAL;
    }

    extvec_t l_P;
    rc = s_build_padded_public(&l_P, l_pad, a_n_ring, a_pks, a_c);
    if (rc != 0) {
        return rc;
    }

    chipmunk_poly_t l_rho_base;
    rc = chipmunk_mring_eval_public_rho_q(&l_rho_base, a_c, a_t, a_Y_pk,
                                          (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        s_extvec_free(&l_P);
        return rc;
    }
    chipmunk_fq6_ext_t l_rho;
    chipmunk_fq6_ext_embed(&l_rho, &l_rho_base);

    for (uint32_t r = 0u; r < l_depth; ++r) {
        const chipmunk_fq6_ext_t *l_CL = &a_proof->rounds[r].C_L;
        const chipmunk_fq6_ext_t *l_CR = &a_proof->rounds[r].C_R;

        uint8_t l_fs[32];
        chipmunk_mring_transcript_fold_round_fs(l_fs, a_fs_seed, r, l_CL, l_CR);

        chipmunk_fq6_ext_t l_L, l_R;
        rc = s_ext_vcom_open(&l_L, l_CL, &l_gens,
                             a_proof->fold_opening_seed, r, 0u);
        if (rc != 0) {
            s_extvec_free(&l_P);
            return -EBADMSG;
        }
        rc = s_ext_vcom_open(&l_R, l_CR, &l_gens,
                             a_proof->fold_opening_seed, r, 1u);
        if (rc != 0) {
            s_extvec_free(&l_P);
            return -EBADMSG;
        }

        chipmunk_fq6_ext_t l_x, l_x_inv;
        rc = chipmunk_fq6_ext_sample_challenge_q(&l_x, l_fs, 0u, (uint64_t)CHIPMUNK_Q);
        if (rc != 0) {
            s_extvec_free(&l_P);
            return rc;
        }
        rc = chipmunk_fq6_ext_scalar_invert_q(&l_x_inv, &l_x, (uint64_t)CHIPMUNK_Q);
        if (rc != 0) {
            s_extvec_free(&l_P);
            return rc;
        }

        const uint32_t l_len = l_P.length;
        if (l_len < 2u || (l_len & 1u) != 0u) {
            s_extvec_free(&l_P);
            return -EINVAL;
        }
        const uint32_t l_h = l_len / 2u;

        extvec_t l_pL, l_pR;
        rc = s_extvec_alloc(&l_pL, l_h);
        if (rc != 0) {
            s_extvec_free(&l_P);
            return rc;
        }
        rc = s_extvec_alloc(&l_pR, l_h);
        if (rc != 0) {
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }
        for (uint32_t j = 0u; j < l_h; ++j) {
            l_pL.slots[j] = l_P.slots[j];
            l_pR.slots[j] = l_P.slots[l_h + j];
        }

        extvec_t l_p_new;
        rc = s_extvec_alloc(&l_p_new, l_h);
        if (rc != 0) {
            s_extvec_free(&l_pR);
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }
        for (uint32_t j = 0u; j < l_h; ++j) {
            chipmunk_fq6_ext_t l_xinvp;
            rc = chipmunk_fq6_ext_mul(&l_xinvp, &l_x_inv, &l_pR.slots[j]);
            if (rc != 0) {
                s_extvec_free(&l_p_new);
                s_extvec_free(&l_pR);
                s_extvec_free(&l_pL);
                s_extvec_free(&l_P);
                return rc;
            }
            rc = chipmunk_fq6_ext_add(&l_p_new.slots[j], &l_pL.slots[j], &l_xinvp);
            if (rc != 0) {
                s_extvec_free(&l_p_new);
                s_extvec_free(&l_pR);
                s_extvec_free(&l_pL);
                s_extvec_free(&l_P);
                return rc;
            }
        }

        chipmunk_fq6_ext_t l_xinvL, l_xR, l_sum;
        rc = chipmunk_fq6_ext_mul(&l_xinvL, &l_x_inv, &l_L);
        if (rc != 0) {
            s_extvec_free(&l_p_new);
            s_extvec_free(&l_pR);
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }
        rc = chipmunk_fq6_ext_mul(&l_xR, &l_x, &l_R);
        if (rc != 0) {
            s_extvec_free(&l_p_new);
            s_extvec_free(&l_pR);
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }
        rc = chipmunk_fq6_ext_add(&l_sum, &l_xinvL, &l_xR);
        if (rc != 0) {
            s_extvec_free(&l_p_new);
            s_extvec_free(&l_pR);
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }
        rc = chipmunk_fq6_ext_add(&l_rho, &l_rho, &l_sum);
        if (rc != 0) {
            s_extvec_free(&l_p_new);
            s_extvec_free(&l_pR);
            s_extvec_free(&l_pL);
            s_extvec_free(&l_P);
            return rc;
        }

        for (uint32_t j = 0u; j < l_h; ++j) {
            l_P.slots[j] = l_p_new.slots[j];
        }
        l_P.length = l_h;

        s_extvec_free(&l_p_new);
        s_extvec_free(&l_pR);
        s_extvec_free(&l_pL);
    }

    if (l_P.length != 1u) {
        s_extvec_free(&l_P);
        return -EBADMSG;
    }

    chipmunk_fq6_ext_t l_a_cmp = l_P.slots[0];
    chipmunk_fq6_ext_canonicalize_q(&l_a_cmp, (uint64_t)CHIPMUNK_Q);
    chipmunk_fq6_ext_t l_a_star = a_proof->a_star;
    chipmunk_fq6_ext_canonicalize_q(&l_a_star, (uint64_t)CHIPMUNK_Q);
    if (!s_ext_equal(&l_a_cmp, &l_a_star)) {
        s_extvec_free(&l_P);
        return -EBADMSG;
    }

    const int64_t l_leaf_bound =
        chipmunk_mring_leaf_bound_for_depth(l_depth);
    int64_t l_leaf_max = 0;
    if (s_leaf_mask_inf_norm(a_proof->leaf_mask, &l_leaf_max) != 0
        || l_leaf_max > l_leaf_bound) {
        s_extvec_free(&l_P);
        return -EBADMSG;
    }

    chipmunk_fq6_ext_t l_b_unmask = a_proof->b_star;
    s_ext_apply_leaf_mask(&l_b_unmask, a_proof->leaf_mask, -1);

    chipmunk_fq6_ext_t l_prod;
    rc = chipmunk_fq6_ext_mul(&l_prod, &l_b_unmask, &a_proof->a_star);
    if (rc != 0) {
        s_extvec_free(&l_P);
        return rc;
    }
    chipmunk_fq6_ext_canonicalize_q(&l_prod, (uint64_t)CHIPMUNK_Q);
    chipmunk_fq6_ext_canonicalize_q(&l_rho, (uint64_t)CHIPMUNK_Q);
    if (!s_ext_equal(&l_prod, &l_rho)) {
        s_extvec_free(&l_P);
        return -EBADMSG;
    }

    s_extvec_free(&l_P);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  M4.1 — R_q^{(e)} wire pack/unpack                                   */
/* ------------------------------------------------------------------ */

int chipmunk_fq6_ext_qpack(uint8_t *a_out, size_t a_out_size,
                             const chipmunk_fq6_ext_t *a_x)
{
    if (!a_out || !a_x) {
        return -EINVAL;
    }
    if (a_out_size < (size_t)CHIPMUNK_FQ6_EXT_QPACK_BYTES) {
        return -EINVAL;
    }

    uint8_t *l_p = a_out;
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        const int rc =
            chipmunk_lrs_poly_qpack(l_p, &a_x->c[j]);
        if (rc != 0) {
            return rc;
        }
        l_p += CHIPMUNK_MRING_POLY_QPACK;
    }
    return 0;
}

int chipmunk_fq6_ext_qunpack(chipmunk_fq6_ext_t *a_out,
                               const uint8_t *a_in, size_t a_in_size)
{
    if (!a_out || !a_in) {
        return -EINVAL;
    }
    if (a_in_size < (size_t)CHIPMUNK_FQ6_EXT_QPACK_BYTES) {
        return -EINVAL;
    }

    const uint8_t *l_p = a_in;
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        const int rc =
            chipmunk_lrs_poly_qunpack(&a_out->c[j], l_p);
        if (rc != 0) {
            return rc;
        }
        l_p += CHIPMUNK_MRING_POLY_QPACK;
    }
    chipmunk_fq6_ext_canonicalize_q(a_out, (uint64_t)CHIPMUNK_Q);
    return 0;
}

static int s_fold_write_ext(uint8_t *a_dst, size_t a_dst_size,
                            const chipmunk_fq6_ext_t *a_x)
{
    return chipmunk_fq6_ext_qpack(a_dst, a_dst_size, a_x);
}

static int s_fold_read_ext(chipmunk_fq6_ext_t *a_out,
                           const uint8_t *a_src, size_t a_src_size)
{
    return chipmunk_fq6_ext_qunpack(a_out, a_src, a_src_size);
}

int chipmunk_mring_fold_write(uint8_t *a_buf, size_t a_buf_size,
                              uint32_t a_fold_depth,
                              const chipmunk_mring_fold_proof_t *a_proof)
{
    if (!a_buf || !a_proof || !a_proof->rounds) {
        return -EINVAL;
    }
    if (a_fold_depth == 0u
        || a_fold_depth > CHIPMUNK_MRING_FOLD_DEPTH_MAX
        || a_proof->fold_depth != a_fold_depth) {
        return -EINVAL;
    }

    const uint32_t l_wire = chipmunk_mring_wire_size(a_fold_depth);
    if (a_buf_size < (size_t)l_wire) {
        return -EINVAL;
    }

    const uint32_t l_off_seed =
        chipmunk_mring_section_off_fold_opening_seed();
    memcpy(a_buf + l_off_seed, a_proof->fold_opening_seed,
           CHIPMUNK_MRING_FOLD_OPENING_BYTES);

    const uint32_t l_off_fold = chipmunk_mring_section_off_fold();
    const uint32_t l_round_bytes = CHIPMUNK_MRING_FOLD_ROUND_BYTES;
    const uint32_t l_ext_bytes = CHIPMUNK_FQ6_EXT_QPACK_BYTES;

    for (uint32_t r = 0u; r < a_fold_depth; ++r) {
        uint8_t *l_base = a_buf + l_off_fold + r * l_round_bytes;
        int rc = s_fold_write_ext(l_base, l_ext_bytes,
                                  &a_proof->rounds[r].C_L);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG fold_write: C_L[%u] pack failed (rc=%d)",
                   (unsigned)r, rc);
            return rc;
        }
        rc = s_fold_write_ext(l_base + l_ext_bytes, l_ext_bytes,
                              &a_proof->rounds[r].C_R);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG fold_write: C_R[%u] pack failed (rc=%d)",
                   (unsigned)r, rc);
            return rc;
        }
    }

    const uint32_t l_off_final = chipmunk_mring_section_off_final(a_fold_depth);
    int rc = s_fold_write_ext(a_buf + l_off_final, l_ext_bytes, &a_proof->a_star);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_write: a* pack failed (rc=%d)", rc);
        return rc;
    }
    rc = s_fold_write_ext(a_buf + l_off_final + l_ext_bytes, l_ext_bytes,
                          &a_proof->b_star);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_write: b* pack failed (rc=%d)", rc);
        return rc;
    }

    const uint32_t l_off_leaf =
        chipmunk_mring_section_off_leaf_mask(a_fold_depth);
    rc = chipmunk_mring_leaf_mask_pack(
        a_buf + l_off_leaf, a_buf_size - l_off_leaf,
        a_proof->leaf_mask, CHIPMUNK_MRING_LEAF_BOUND_MAX);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_write: leaf_mask pack failed (rc=%d)", rc);
        return rc;
    }
    return 0;
}

int chipmunk_mring_fold_read(chipmunk_mring_fold_proof_t *a_proof,
                             uint32_t a_fold_depth,
                             const uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_proof || !a_proof->rounds || !a_buf) {
        return -EINVAL;
    }
    if (a_fold_depth == 0u
        || a_fold_depth > CHIPMUNK_MRING_FOLD_DEPTH_MAX
        || a_proof->fold_depth != a_fold_depth) {
        return -EINVAL;
    }

    const uint32_t l_wire = chipmunk_mring_wire_size(a_fold_depth);
    if (a_buf_size < (size_t)l_wire) {
        return -EINVAL;
    }

    const uint32_t l_off_seed =
        chipmunk_mring_section_off_fold_opening_seed();
    memcpy(a_proof->fold_opening_seed, a_buf + l_off_seed,
           CHIPMUNK_MRING_FOLD_OPENING_BYTES);

    const uint32_t l_off_fold = chipmunk_mring_section_off_fold();
    const uint32_t l_round_bytes = CHIPMUNK_MRING_FOLD_ROUND_BYTES;
    const uint32_t l_ext_bytes = CHIPMUNK_FQ6_EXT_QPACK_BYTES;

    for (uint32_t r = 0u; r < a_fold_depth; ++r) {
        const uint8_t *l_base = a_buf + l_off_fold + r * l_round_bytes;
        int rc = s_fold_read_ext(&a_proof->rounds[r].C_L, l_base, l_ext_bytes);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG fold_read: C_L[%u] unpack failed (rc=%d)",
                   (unsigned)r, rc);
            return rc;
        }
        rc = s_fold_read_ext(&a_proof->rounds[r].C_R,
                             l_base + l_ext_bytes, l_ext_bytes);
        if (rc != 0) {
            log_it(L_ERROR, "MRNG fold_read: C_R[%u] unpack failed (rc=%d)",
                   (unsigned)r, rc);
            return rc;
        }
    }

    const uint32_t l_off_final = chipmunk_mring_section_off_final(a_fold_depth);
    int rc = s_fold_read_ext(&a_proof->a_star, a_buf + l_off_final, l_ext_bytes);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_read: a* unpack failed (rc=%d)", rc);
        return rc;
    }
    rc = s_fold_read_ext(&a_proof->b_star,
                         a_buf + l_off_final + l_ext_bytes, l_ext_bytes);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_read: b* unpack failed (rc=%d)", rc);
        return rc;
    }

    const uint32_t l_off_leaf =
        chipmunk_mring_section_off_leaf_mask(a_fold_depth);
    rc = chipmunk_mring_leaf_mask_unpack(
        a_proof->leaf_mask, a_buf + l_off_leaf,
        a_buf_size - l_off_leaf, CHIPMUNK_MRING_LEAF_BOUND_MAX);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG fold_read: leaf_mask unpack failed (rc=%d)",
               rc);
        return rc;
    }
    return 0;
}
