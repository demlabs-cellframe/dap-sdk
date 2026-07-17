/*
 * CR-11.G Phase 7.7 — MRNG Fiat-Shamir transcript (G4).
 * See MRNG_G4_TRANSCRIPT.md for byte-exact layout.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "chipmunk_mring_transcript.h"
#include "chipmunk_poly.h"

#define LOG_TAG "chipmunk_mring_transcript"

#define MRING_RING_DOMAIN    "chipmunk-mring-ring-v1"
#define MRING_CTX_DOMAIN     "chipmunk-mring-ctx-v1"
#define MRING_MSG_DOMAIN     "chipmunk-mring-msg-v1"
#define MRING_FS_DOMAIN      "chipmunk-mring-fs-v1"
#define MRING_C_DOMAIN       "chipmunk-mring-c-v1"
#define MRING_BIND_FS_DOMAIN "chipmunk-mring-bind-fs-v1"
#define MRING_FOLD_ROUND_FS_DOMAIN "MRNG-M4-fold-round-fs-v1"

static void s_le32_store(uint8_t a_out[4], uint32_t a_v)
{
    a_out[0] = (uint8_t)(a_v);
    a_out[1] = (uint8_t)(a_v >> 8);
    a_out[2] = (uint8_t)(a_v >> 16);
    a_out[3] = (uint8_t)(a_v >> 24);
}

static void s_hash_update_bytes(uint8_t **a_pos, const void *a_data, size_t a_size)
{
    memcpy(*a_pos, a_data, a_size);
    *a_pos += a_size;
}

#define MRING_HASH_STACK_BUF 4096u

static int s_hash_len_prefixed(uint8_t a_out[32],
                               const char *a_domain,
                               const void *a_data,
                               size_t a_data_size)
{
    if (!a_out || !a_domain || (!a_data && a_data_size != 0)) {
        return -EINVAL;
    }
    const size_t l_domain_len = strlen(a_domain);
    if (l_domain_len > UINT32_MAX || a_data_size > UINT32_MAX) {
        return -EINVAL;
    }

    const size_t l_total = 4u + l_domain_len + 4u + a_data_size;

    uint8_t l_stack[MRING_HASH_STACK_BUF];
    uint8_t *l_buf = (l_total <= MRING_HASH_STACK_BUF)
                         ? l_stack
                         : DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!l_buf) {
        return -ENOMEM;
    }

    uint8_t *p = l_buf;
    uint8_t le[4];
    s_le32_store(le, (uint32_t)l_domain_len);
    s_hash_update_bytes(&p, le, sizeof(le));
    s_hash_update_bytes(&p, a_domain, l_domain_len);
    s_le32_store(le, (uint32_t)a_data_size);
    s_hash_update_bytes(&p, le, sizeof(le));
    if (a_data_size != 0u) {
        s_hash_update_bytes(&p, a_data, a_data_size);
    }

    dap_hash_sha3_256_t l_h;
    if (!dap_hash_sha3_256(l_buf, l_total, &l_h)) {
        if (l_total > MRING_HASH_STACK_BUF) {
            DAP_DELETE(l_buf);
        }
        return -EIO;
    }
    memcpy(a_out, &l_h, 32u);
    if (l_total > MRING_HASH_STACK_BUF) {
        DAP_DELETE(l_buf);
    }
    return 0;
}

static int s_pk_qpack_cmp(const void *a, const void *b)
{
    const chipmunk_lrs_public_key_t *l_a = a;
    const chipmunk_lrs_public_key_t *l_b = b;
    return memcmp(l_a->P, l_b->P, CHIPMUNK_LRS_POLY_QPACK_BYTES);
}

static void s_absorb_ext(uint64_t a_st[25], const chipmunk_fq6_ext_t *a_x)
{
    size_t l_comp_size = sizeof(a_x->c[0].coeffs);
    size_t l_abs_len = (size_t)CHIPMUNK_FQ6_EXT_DEG * l_comp_size;
    uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
    if (!l_abs) return;
    for (uint32_t j = 0u; j < (uint32_t)CHIPMUNK_FQ6_EXT_DEG; ++j) {
        memcpy(l_abs + j * l_comp_size,
               (const uint8_t *)a_x->c[j].coeffs, l_comp_size);
    }
    dap_hash_shake256_absorb(a_st, l_abs, l_abs_len);
    DAP_DELETE(l_abs);
}

static int s_hash_with_params(uint8_t a_out[32],
                              const char *a_domain,
                              uint32_t a_params_id,
                              const void *a_payload, size_t a_payload_len)
{
    const size_t l_payload_size = 4u + a_payload_len;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }
    s_le32_store(l_payload, a_params_id);
    if (a_payload_len != 0u) {
        memcpy(l_payload + 4u, a_payload, a_payload_len);
    }
    const int rc = s_hash_len_prefixed(a_out, a_domain, l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return rc;
}

int chipmunk_mring_canonicalise_ring(chipmunk_lrs_public_key_t *a_sorted_out,
                                     uint32_t a_n_ring,
                                     const chipmunk_lrs_public_key_t *a_ring)
{
    if (!a_sorted_out || !a_ring
        || a_n_ring < CHIPMUNK_MRING_N_MIN
        || a_n_ring > CHIPMUNK_MRING_N_MAX) {
        return -EINVAL;
    }

    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        const int rc = chipmunk_lrs_public_key_validate(&a_ring[i]);
        if (rc != 0) {
            return rc;
        }
        a_sorted_out[i] = a_ring[i];
    }

    qsort(a_sorted_out, a_n_ring, sizeof(*a_sorted_out), s_pk_qpack_cmp);
    for (uint32_t i = 1u; i < a_n_ring; ++i) {
        if (memcmp(a_sorted_out[i - 1u].P, a_sorted_out[i].P,
                   CHIPMUNK_LRS_POLY_QPACK_BYTES) == 0) {
            return -EEXIST;
        }
    }
    return 0;
}

static int s_hash_sorted_ring(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                              const chipmunk_lrs_public_key_t *a_sorted,
                              uint32_t a_n_ring)
{
    const size_t l_payload_size =
        4u + 4u + (size_t)a_n_ring * CHIPMUNK_LRS_POLY_QPACK_BYTES;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }

    uint8_t *p = l_payload;
    s_le32_store(p, CHIPMUNK_MRING_PARAMS_ID);
    p += 4u;
    s_le32_store(p, a_n_ring);
    p += 4u;
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        memcpy(p, a_sorted[i].P, CHIPMUNK_LRS_POLY_QPACK_BYTES);
        p += CHIPMUNK_LRS_POLY_QPACK_BYTES;
    }

    const int rc = s_hash_len_prefixed(a_out, MRING_RING_DOMAIN,
                                       l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return rc;
}

int chipmunk_mring_hash_ring(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                             const chipmunk_lrs_public_key_t *a_ring,
                             uint32_t a_n_ring)
{
    if (!a_out || !a_ring) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t *l_sorted =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_n_ring);
    if (!l_sorted) {
        return -ENOMEM;
    }

    const int rc_canon =
        chipmunk_mring_canonicalise_ring(l_sorted, a_n_ring, a_ring);
    if (rc_canon != 0) {
        DAP_DELETE(l_sorted);
        return rc_canon;
    }

    const int rc = s_hash_sorted_ring(a_out, l_sorted, a_n_ring);
    DAP_DELETE(l_sorted);
    return rc;
}

int chipmunk_mring_hash_sorted_ring(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                                    const chipmunk_lrs_public_key_t *a_sorted,
                                    uint32_t a_n_ring)
{
    if (!a_out || !a_sorted) {
        return -EINVAL;
    }
    return s_hash_sorted_ring(a_out, a_sorted, a_n_ring);
}

int chipmunk_mring_hash_ctx(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                            uint32_t a_params_id,
                            const void *a_ctx, size_t a_ctx_len)
{
    if (!a_out || a_ctx_len > UINT32_MAX) {
        return -EINVAL;
    }
    if (!a_ctx && a_ctx_len != 0u) {
        return -EINVAL;
    }
    return s_hash_with_params(a_out, MRING_CTX_DOMAIN, a_params_id,
                              a_ctx, a_ctx_len);
}

int chipmunk_mring_hash_msg(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                            uint32_t a_params_id,
                            const uint8_t *a_msg, size_t a_msg_len)
{
    if (!a_out || a_msg_len > UINT32_MAX) {
        return -EINVAL;
    }
    if (!a_msg && a_msg_len != 0u) {
        return -EINVAL;
    }
    return s_hash_with_params(a_out, MRING_MSG_DOMAIN, a_params_id,
                              a_msg, a_msg_len);
}

int chipmunk_mring_fs_seed(uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_ring_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_ctx_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t a_msg_hash[CHIPMUNK_MRING_HASH_BYTES],
                           const uint8_t
                               a_T_qpack[CHIPMUNK_MRING_POLY_QPACK],
                           const uint8_t
                               a_Cb_qpack[CHIPMUNK_MRING_POLY_QPACK])
{
    if (!a_out || !a_ring_hash || !a_ctx_hash || !a_msg_hash
        || !a_T_qpack || !a_Cb_qpack) {
        return -EINVAL;
    }

    const size_t l_payload_size =
        4u * CHIPMUNK_MRING_HASH_BYTES
        + 2u * (size_t)CHIPMUNK_MRING_POLY_QPACK;
    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }

    uint8_t *p = l_payload;
    memcpy(p, a_ring_hash, 32u);
    p += 32u;
    memcpy(p, a_ctx_hash, 32u);
    p += 32u;
    memcpy(p, a_msg_hash, 32u);
    p += 32u;
    memcpy(p, a_T_qpack, CHIPMUNK_MRING_POLY_QPACK);
    p += CHIPMUNK_MRING_POLY_QPACK;
    memcpy(p, a_Cb_qpack, CHIPMUNK_MRING_POLY_QPACK);

    const int rc = s_hash_len_prefixed(a_out, MRING_FS_DOMAIN,
                                       l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return rc;
}

int chipmunk_mring_transcript_sample_c(
    chipmunk_poly_t *a_c_out,
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES])
{
    if (!a_c_out || !a_fs_seed) {
        return -EINVAL;
    }

    return chipmunk_lrs_h_to_sparse_ternary(
        a_c_out, MRING_C_DOMAIN, CHIPMUNK_LRS_PARAMS_C0, a_fs_seed);
}

int chipmunk_mring_transcript_fold_round_fs(
    uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
    uint32_t a_round,
    const chipmunk_fq6_ext_t *a_CL,
    const chipmunk_fq6_ext_t *a_CR)
{
    if (!a_out || !a_fs_seed || !a_CL || !a_CR) {
        return -EINVAL;
    }

    uint64_t l_st[25];
    memset(l_st, 0, sizeof(l_st));
    {
        size_t l_domain_len = sizeof(MRING_FOLD_ROUND_FS_DOMAIN) - 1u;
        uint8_t l_rbuf[4];
        s_le32_store(l_rbuf, a_round);
        size_t l_abs_len = l_domain_len + 32u + sizeof(l_rbuf);
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) return -ENOMEM;
        memcpy(l_abs, MRING_FOLD_ROUND_FS_DOMAIN, l_domain_len);
        memcpy(l_abs + l_domain_len, a_fs_seed, 32u);
        memcpy(l_abs + l_domain_len + 32u, l_rbuf, sizeof(l_rbuf));
        dap_hash_shake256_absorb(l_st, l_abs, l_abs_len);
        DAP_DELETE(l_abs);
    }
    s_absorb_ext(l_st, a_CL);
    s_absorb_ext(l_st, a_CR);
    dap_hash_shake256_squeezeblocks(a_out, 1u, l_st);
    return 0;
}

int chipmunk_mring_transcript_bind_fs(
    uint8_t a_out[CHIPMUNK_MRING_HASH_BYTES],
    const uint8_t a_fs_seed[CHIPMUNK_MRING_HASH_BYTES],
    const chipmunk_poly_t *a_c,
    const chipmunk_poly_t *a_M_pk,
    const chipmunk_poly_t *a_M_T,
    const chipmunk_mring_fold_proof_t *a_proof,
    uint32_t a_fold_depth)
{
    if (!a_out || !a_fs_seed || !a_c || !a_M_pk || !a_M_T
        || !a_proof || !a_proof->rounds || a_fold_depth == 0u) {
        return -EINVAL;
    }
    if (a_proof->fold_depth != a_fold_depth) {
        return -EINVAL;
    }

    uint8_t l_c_qpack[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t l_mpk_qpack[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t l_mt_qpack[CHIPMUNK_MRING_POLY_QPACK];
    int rc = chipmunk_lrs_poly_qpack(l_c_qpack, a_c, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_lrs_poly_qpack(l_mpk_qpack, a_M_pk, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_lrs_poly_qpack(l_mt_qpack, a_M_T, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        return rc;
    }

    const size_t l_round_bytes =
        (size_t)a_fold_depth * 2u * (size_t)CHIPMUNK_FQ6_EXT_QPACK_BYTES;
    const size_t l_payload_size =
        CHIPMUNK_MRING_HASH_BYTES
        + 3u * (size_t)CHIPMUNK_MRING_POLY_QPACK
        + l_round_bytes
        + 2u * (size_t)CHIPMUNK_FQ6_EXT_QPACK_BYTES
        + (size_t)CHIPMUNK_MRING_LEAF_MASK_BYTES;

    uint8_t *l_payload = DAP_NEW_Z_SIZE(uint8_t, l_payload_size);
    if (!l_payload) {
        return -ENOMEM;
    }

    uint8_t *p = l_payload;
    memcpy(p, a_fs_seed, CHIPMUNK_MRING_HASH_BYTES);
    p += CHIPMUNK_MRING_HASH_BYTES;
    memcpy(p, l_c_qpack, CHIPMUNK_MRING_POLY_QPACK);
    p += CHIPMUNK_MRING_POLY_QPACK;
    memcpy(p, l_mpk_qpack, CHIPMUNK_MRING_POLY_QPACK);
    p += CHIPMUNK_MRING_POLY_QPACK;
    memcpy(p, l_mt_qpack, CHIPMUNK_MRING_POLY_QPACK);
    p += CHIPMUNK_MRING_POLY_QPACK;

    for (uint32_t r = 0u; r < a_fold_depth; ++r) {
        rc = chipmunk_fq6_ext_qpack(p, CHIPMUNK_FQ6_EXT_QPACK_BYTES,
                                      &a_proof->rounds[r].C_L);
        if (rc != 0) {
            DAP_DELETE(l_payload);
            return rc;
        }
        p += CHIPMUNK_FQ6_EXT_QPACK_BYTES;
        rc = chipmunk_fq6_ext_qpack(p, CHIPMUNK_FQ6_EXT_QPACK_BYTES,
                                      &a_proof->rounds[r].C_R);
        if (rc != 0) {
            DAP_DELETE(l_payload);
            return rc;
        }
        p += CHIPMUNK_FQ6_EXT_QPACK_BYTES;
    }

    rc = chipmunk_fq6_ext_qpack(p, CHIPMUNK_FQ6_EXT_QPACK_BYTES,
                                  &a_proof->a_star);
    if (rc != 0) {
        DAP_DELETE(l_payload);
        return rc;
    }
    p += CHIPMUNK_FQ6_EXT_QPACK_BYTES;
    rc = chipmunk_fq6_ext_qpack(p, CHIPMUNK_FQ6_EXT_QPACK_BYTES,
                                  &a_proof->b_star);
    if (rc != 0) {
        DAP_DELETE(l_payload);
        return rc;
    }
    p += CHIPMUNK_FQ6_EXT_QPACK_BYTES;

    /* Absorb leaf_mask ω (49-bit packed, LEAF_MASK_BYTES). */
    if (!a_proof->leaf_mask) {
        DAP_DELETE(l_payload);
        return -EINVAL;
    }
    const int64_t l_leaf_bound =
        chipmunk_mring_leaf_bound_for_depth(a_fold_depth);
    rc = chipmunk_mring_leaf_mask_pack(
        p, (size_t)CHIPMUNK_MRING_LEAF_MASK_BYTES,
        a_proof->leaf_mask, l_leaf_bound);
    if (rc != 0) {
        DAP_DELETE(l_payload);
        return rc;
    }

    rc = s_hash_len_prefixed(a_out, MRING_BIND_FS_DOMAIN,
                             l_payload, l_payload_size);
    DAP_DELETE(l_payload);
    return rc;
}

int chipmunk_mring_transcript_sample_c_star(
    chipmunk_poly_t *a_c_star_out,
    const uint8_t a_bind_fs[CHIPMUNK_MRING_HASH_BYTES])
{
    if (!a_c_star_out || !a_bind_fs) {
        return -EINVAL;
    }
    return chipmunk_lrs_h_to_sparse_ternary(
        a_c_star_out, MRING_BIND_FS_DOMAIN, CHIPMUNK_LRS_PARAMS_C0, a_bind_fs);
}
