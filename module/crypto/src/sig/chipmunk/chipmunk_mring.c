/*
 * Chipmunk MRNG — sign/verify core + header (de)serialisation.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.
 *
 * M6 (done): end-to-end sign/verify wire glue — s_mring_sign_core,
 *            s_mring_verify_core, public chipmunk_ring_sign_to_bytes /
 *            chipmunk_ring_verify_from_bytes.  Bind block carries z_x
 *            (K_PK zpacks) + c* (qpack) for verifier FS closure.
 * M7 (done): KAT, security, benchmarks, signoff, CT audit.
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_hash_sha3.h"
#include "dap_memwipe.h"
#include "dap_serialize.h"
#include "chipmunk_mring.h"
#include "chipmunk_mring_params.h"
#include "chipmunk_mring_transcript.h"
#include "chipmunk_mring_fold.h"
#include "chipmunk_mring_statement.h"
#include "chipmunk_lrs.h"
#include "chipmunk_poly.h"

#define LOG_TAG "chipmunk_mring"

/* --- Header serialization schema (dap_serialize) --- */

static const dap_serialize_field_t s_mring_header_fields[] = {
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, magic,      DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, version,    DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, params_id,  DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, n_ring,     DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, threshold,  DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, fold_depth, DAP_SERIALIZE_TYPE_UINT32),
    DAP_SERIALIZE_FIELD_SIMPLE(chipmunk_mring_header_t, flags,      DAP_SERIALIZE_TYPE_UINT32),
};

DAP_SERIALIZE_SCHEMA_DEFINE(s_mring_header_schema,
                            chipmunk_mring_header_t,
                            s_mring_header_fields);

#define MRING_SIGN_MASTER_DOMAIN "chipmunk-mring-sign-master-v1"
#define MRING_SIGN_RB_DOMAIN     "chipmunk-mring-sign-rb-v1"
#define MRING_SIGN_FOLD_DOMAIN   "chipmunk-mring-sign-fold-v1"
#define MRING_SIGN_MASK_DOMAIN   "chipmunk-mring-sign-mask-v1"

/* -------------------------------------------------------------------------
 * Helpers.
 * ---------------------------------------------------------------------- */

static inline uint32_t s_load_u32_le(const uint8_t *a_p)
{
    return  (uint32_t)a_p[0]
         | ((uint32_t)a_p[1] <<  8)
         | ((uint32_t)a_p[2] << 16)
         | ((uint32_t)a_p[3] << 24);
}

static inline void s_store_u32_le(uint8_t *a_p, uint32_t a_v)
{
    a_p[0] = (uint8_t)(a_v & 0xFFu);
    a_p[1] = (uint8_t)((a_v >>  8) & 0xFFu);
    a_p[2] = (uint8_t)((a_v >> 16) & 0xFFu);
    a_p[3] = (uint8_t)((a_v >> 24) & 0xFFu);
}

uint32_t chipmunk_mring_fold_depth_for(uint32_t a_n_ring)
{
    /*
     * G2 v2 §A1.1: the fold operates on the augmented vector
     * \tilde b of length 2N (binary check encoded in the second half),
     * so fold_depth = 1 + ceil(log2 N).
     */
    if (a_n_ring < CHIPMUNK_MRING_N_MIN || a_n_ring > CHIPMUNK_MRING_N_MAX) {
        return 0u;
    }
    uint32_t l_d = 0u;
    uint32_t l_v = 1u;
    while (l_v < a_n_ring) {
        l_v <<= 1;
        ++l_d;
    }
    return l_d + 1u;
}

void chipmunk_mring_header_write(uint8_t a_buf[CHIPMUNK_MRING_HEADER_BYTES],
                                 const chipmunk_mring_header_t *a_hdr)
{
    dap_serialize_result_t l_res = dap_serialize_to_buffer_raw(
        &s_mring_header_schema, a_hdr, a_buf,
        CHIPMUNK_MRING_HEADER_BYTES, NULL);
    if (l_res.error_code != 0) {
        log_it(L_ERROR, "MRNG header_write failed: %s", l_res.error_message);
    }
}

chipmunk_ring_error_t chipmunk_mring_header_read(
    chipmunk_mring_header_t *a_hdr_out,
    const uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_hdr_out || !a_buf) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (a_buf_size < (size_t)CHIPMUNK_MRING_HEADER_BYTES) {
        return CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL;
    }

    memset(a_hdr_out, 0, sizeof(*a_hdr_out));
    dap_serialize_result_t l_res = dap_serialize_from_buffer_raw_zero(
        &s_mring_header_schema, a_buf, a_buf_size, a_hdr_out, NULL);
    if (l_res.error_code != 0) {
        return CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL;
    }

    if (a_hdr_out->magic != CHIPMUNK_MRING_MAGIC) {
        return CHIPMUNK_RING_ERR_MAGIC_MISMATCH;
    }
    if (a_hdr_out->version != CHIPMUNK_MRING_VERSION) {
        return CHIPMUNK_RING_ERR_VERSION_MISMATCH;
    }
    if (a_hdr_out->params_id != CHIPMUNK_MRING_PARAMS_ID) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if (a_hdr_out->n_ring < CHIPMUNK_MRING_N_MIN ||
        a_hdr_out->n_ring > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }
    if (a_hdr_out->threshold < CHIPMUNK_MRING_T_MIN ||
        a_hdr_out->threshold > a_hdr_out->n_ring) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }
    const uint32_t l_expected_depth = chipmunk_mring_fold_depth_for(a_hdr_out->n_ring);
    if (a_hdr_out->fold_depth != l_expected_depth) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if ((a_hdr_out->flags & CHIPMUNK_MRING_FLAG_LINKABLE) == 0u) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if ((a_hdr_out->flags & CHIPMUNK_MRING_FLAGS_RESERVED) != 0u) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }

    const uint32_t l_expected_size =
        chipmunk_mring_wire_size(a_hdr_out->fold_depth);
    if (a_buf_size < (size_t)l_expected_size) {
        return CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL;
    }

    return CHIPMUNK_RING_OK;
}

/* -------------------------------------------------------------------------
 * Strerror — covers every code in chipmunk_ring_error_t.
 * ---------------------------------------------------------------------- */

const char *chipmunk_ring_strerror(chipmunk_ring_error_t a_err)
{
    switch (a_err) {
    case CHIPMUNK_RING_OK:                       return "MRNG: ok";
    case CHIPMUNK_RING_ERR_NULL_PARAM:           return "MRNG: null parameter";
    case CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL:     return "MRNG: buffer too small";
    case CHIPMUNK_RING_ERR_MAGIC_MISMATCH:       return "MRNG: magic mismatch";
    case CHIPMUNK_RING_ERR_VERSION_MISMATCH:     return "MRNG: version mismatch";
    case CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE:  return "MRNG: ring size out of range";
    case CHIPMUNK_RING_ERR_T_OUT_OF_RANGE:       return "MRNG: threshold out of range";
    case CHIPMUNK_RING_ERR_RING_HASH_MISMATCH:   return "MRNG: ring hash mismatch";
    case CHIPMUNK_RING_ERR_CTX_HASH_MISMATCH:    return "MRNG: ctx hash mismatch";
    case CHIPMUNK_RING_ERR_TAG_ORDER:            return "MRNG: tag order invalid";
    case CHIPMUNK_RING_ERR_TAG_DUPLICATE:        return "MRNG: tag duplicate";
    case CHIPMUNK_RING_ERR_NORM_BOUND:           return "MRNG: norm bound exceeded";
    case CHIPMUNK_RING_ERR_PROOF_FAIL:           return "MRNG: proof verification failed";
    case CHIPMUNK_RING_ERR_FIAT_SHAMIR_MISMATCH: return "MRNG: Fiat-Shamir mismatch";
    case CHIPMUNK_RING_ERR_PARAMS_MISMATCH:      return "MRNG: parameters mismatch";
    case CHIPMUNK_RING_ERR_RING_PK_DUPLICATE:    return "MRNG: ring pk duplicate";
    case CHIPMUNK_RING_ERR_RING_NOT_CANONICAL:   return "MRNG: ring not canonical";
    case CHIPMUNK_RING_ERR_NOT_IMPLEMENTED:      return "MRNG: not implemented";
    case CHIPMUNK_RING_ERR_INTERNAL:             return "MRNG: internal error";
    default:                                     return "MRNG: unknown error";
    }
}

/* -------------------------------------------------------------------------
 * Internal helpers (M6 sign / verify glue).
 * ---------------------------------------------------------------------- */

static chipmunk_ring_error_t s_map_int_err(int a_rc)
{
    switch (a_rc) {
    case 0:           return CHIPMUNK_RING_OK;
    case -EINVAL:     return CHIPMUNK_RING_ERR_NULL_PARAM;
    case -ENOMEM:     return CHIPMUNK_RING_ERR_INTERNAL;
    case -EEXIST:     return CHIPMUNK_RING_ERR_RING_PK_DUPLICATE;
    case -ENOENT:     return CHIPMUNK_RING_ERR_RING_NOT_CANONICAL;
    case -EAGAIN:     return CHIPMUNK_RING_ERR_NORM_BOUND;
    case -ERANGE:     return CHIPMUNK_RING_ERR_NORM_BOUND;
    case -EDOM:       return CHIPMUNK_RING_ERR_PROOF_FAIL;
    case -EBADMSG:    return CHIPMUNK_RING_ERR_FIAT_SHAMIR_MISMATCH;
    case -EIO:        return CHIPMUNK_RING_ERR_INTERNAL;
    default:          return CHIPMUNK_RING_ERR_PROOF_FAIL;
    }
}

/* Stack buffer threshold — covers domain seed derivation (typical ~80 B). */
#define MRING_HASH_STACK_BUF 256u

static int s_hash_domain_seed(uint8_t a_out[32],
                              const char *a_domain,
                              const uint8_t *a_payload,
                              size_t a_payload_len)
{
    const size_t l_dom_len = strlen(a_domain);
    const size_t l_total = 4u + l_dom_len + 4u + a_payload_len;

    uint8_t l_stack[MRING_HASH_STACK_BUF];
    uint8_t *l_buf = (l_total <= MRING_HASH_STACK_BUF)
                         ? l_stack
                         : DAP_NEW_Z_SIZE(uint8_t, l_total);
    if (!l_buf) {
        return -ENOMEM;
    }
    uint8_t *p = l_buf;
    s_store_u32_le(p, (uint32_t)l_dom_len);
    p += 4u;
    memcpy(p, a_domain, l_dom_len);
    p += l_dom_len;
    s_store_u32_le(p, (uint32_t)a_payload_len);
    p += 4u;
    if (a_payload_len > 0u) {
        memcpy(p, a_payload, a_payload_len);
    }
    dap_hash_sha3_256_t l_h;
    const int rc = dap_hash_sha3_256(l_buf, l_total, &l_h) ? 0 : -EIO;
    if (rc == 0) {
        memcpy(a_out, l_h.raw, 32u);
    }
    if (l_total > MRING_HASH_STACK_BUF) {
        DAP_DELETE(l_buf);
    }
    return rc;
}

static int s_derive_master_seed(uint8_t a_out[32],
                                const uint8_t *a_seeds,
                                size_t a_seed_bytes)
{
    return s_hash_domain_seed(a_out, MRING_SIGN_MASTER_DOMAIN,
                              a_seeds, a_seed_bytes);
}

static int s_derive_sub_seed(uint8_t a_out[32],
                             const uint8_t a_master[32],
                             const char *a_domain,
                             uint32_t a_attempt)
{
    uint8_t l_payload[32u + 4u];
    memcpy(l_payload, a_master, 32u);
    s_store_u32_le(l_payload + 32u, a_attempt);
    return s_hash_domain_seed(a_out, a_domain, l_payload, sizeof(l_payload));
}

static int32_t s_canon_mod_q_q(int32_t a_v, uint64_t q)
{
    int64_t v = (int64_t)a_v % (int64_t)q;
    if (v < 0) {
        v += (int64_t)q;
    }
    return (int32_t)v;
}

static int32_t s_canon_mod_q(int32_t a_v)
{
    return s_canon_mod_q_q(a_v, (uint64_t)CHIPMUNK_Q);
}

static bool s_poly_equal(const chipmunk_poly_t *a, const chipmunk_poly_t *b)
{
    for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
        if (s_canon_mod_q(a->coeffs[i]) != s_canon_mod_q(b->coeffs[i])) {
            return false;
        }
    }
    return true;
}

/*
 * CT-safe memcmp: returns 0 when a==b, non-zero otherwise.
 * Iterates over all bytes regardless of mismatch position.
 */
static int s_memcmp_ct(const void *a, const void *b, size_t a_len)
{
    const uint8_t *l_a = (const uint8_t *)a;
    const uint8_t *l_b = (const uint8_t *)b;
    uint8_t l_diff = 0u;
    for (size_t i = 0u; i < a_len; ++i) {
        l_diff |= l_a[i] ^ l_b[i];
    }
    return (int)l_diff;
}

static int s_build_b_indicator(uint8_t *a_b,
                               uint32_t a_n_ring,
                               const chipmunk_lrs_public_key_t *a_sorted,
                               const chipmunk_lrs_secret_key_t *const *a_sks,
                               size_t a_signer_count,
                               uint32_t a_threshold)
{
    memset(a_b, 0, a_n_ring);
    uint32_t l_count = 0u;

    for (size_t s = 0u; s < a_signer_count; ++s) {
        if (!a_sks[s]) {
            return -EINVAL;
        }
        int rc = chipmunk_lrs_secret_key_validate(a_sks[s]);
        if (rc != 0) {
            return rc;
        }

        /*
         * CT-safe: always iterate the full ring, no early break.
         * match = (memcmp == 0) ? 1 : 0, applied via mask.
         * b[i] |= match (but only if b[i] was 0 — duplicate check).
         */
        uint32_t l_signer_matches = 0u;
        for (uint32_t i = 0u; i < a_n_ring; ++i) {
            const int l_cmp = s_memcmp_ct(a_sks[s]->P, a_sorted[i].P,
                                          CHIPMUNK_LRS_POLY_QPACK_BYTES);
            const uint32_t l_match = (l_cmp == 0) ? 1u : 0u;
            l_signer_matches += l_match;
            /* Duplicate: b[i] already set by a previous signer. */
            if (l_match != 0u && a_b[i] != 0u) {
                return -EEXIST;
            }
            a_b[i] |= (uint8_t)l_match;
        }
        if (l_signer_matches != 1u) {
            return -ENOENT;
        }
        ++l_count;
    }

    if (l_count != a_threshold) {
        return -EINVAL;
    }
    return 0;
}

static int s_load_pks_from_ring(chipmunk_poly_t *a_pks,
                                const chipmunk_lrs_public_key_t *a_sorted,
                                uint32_t a_n_ring)
{
    for (uint32_t i = 0u; i < a_n_ring; ++i) {
        const int rc = chipmunk_lrs_poly_qunpack(&a_pks[i], a_sorted[i].P, (uint64_t)CHIPMUNK_Q);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int s_sample_r_b(chipmunk_poly_t a_r_b[CHIPMUNK_MRING_K_PK],
                        const uint8_t a_seed[32],
                        uint32_t a_attempt)
{
    uint8_t l_slot_seed[32];
    const int rc_seed = s_derive_sub_seed(l_slot_seed, a_seed,
                                         MRING_SIGN_RB_DOMAIN, a_attempt);
    if (rc_seed != 0) {
        return rc_seed;
    }

    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        const int rc = chipmunk_lrs_h_to_short_poly(
            &a_r_b[j], MRING_SIGN_RB_DOMAIN, CHIPMUNK_LRS_PARAMS_C0,
            l_slot_seed, j, CHIPMUNK_MRING_BETA_W);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}

static int s_pack_bind_block(uint8_t *a_buf, size_t a_buf_size,
                             const chipmunk_poly_t a_z_x[CHIPMUNK_MRING_K_PK],
                             const chipmunk_poly_t *a_c_star)
{
    if (!a_c_star || a_buf_size < (size_t)CHIPMUNK_MRING_BIND_BYTES) {
        return -EINVAL;
    }
    const size_t l_z_off = 0u;
    const size_t l_c_off =
        (size_t)CHIPMUNK_MRING_K_PK * CHIPMUNK_MRING_POLY_ZPACK;
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        const int rc = chipmunk_lrs_poly_zpack(
            a_buf + l_z_off + (size_t)j * CHIPMUNK_MRING_POLY_ZPACK,
            &a_z_x[j]);
        if (rc != 0) {
            return rc;
        }
    }
    return chipmunk_lrs_poly_qpack(a_buf + l_c_off, a_c_star, (uint64_t)CHIPMUNK_Q);
}

static int s_unpack_bind_block(chipmunk_poly_t a_z_x[CHIPMUNK_MRING_K_PK],
                               chipmunk_poly_t *a_c_star_out,
                               const uint8_t *a_buf, size_t a_buf_size)
{
    if (!a_c_star_out || a_buf_size < (size_t)CHIPMUNK_MRING_BIND_BYTES) {
        return -EINVAL;
    }
    const size_t l_z_off = 0u;
    const size_t l_c_off =
        (size_t)CHIPMUNK_MRING_K_PK * CHIPMUNK_MRING_POLY_ZPACK;
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        const int rc = chipmunk_lrs_poly_zunpack(
            &a_z_x[j],
            a_buf + l_z_off + (size_t)j * CHIPMUNK_MRING_POLY_ZPACK);
        if (rc != 0) {
            return rc;
        }
    }
    return chipmunk_lrs_poly_qunpack(a_c_star_out, a_buf + l_c_off, (uint64_t)CHIPMUNK_Q);
}

/*
 * Like chipmunk_lrs_relation_eval but assumes a_A is already in NTT domain.
 * Saves K_PK NTTs per call when A is reused across multiple evals.
 */
static int s_relation_eval_ntt(chipmunk_poly_t *a_out,
                                const chipmunk_poly_t a_A_ntt[CHIPMUNK_MRING_K_PK],
                                const chipmunk_poly_t a_x[CHIPMUNK_MRING_K_PK],
                                uint64_t q)
{
    memset(a_out, 0, sizeof(*a_out));
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        chipmunk_poly_t l_x = a_x[j];
        chipmunk_poly_t l_prod;
        int rc = chipmunk_poly_ntt(&l_x);
        if (rc != 0) return rc;
        chipmunk_poly_mul_ntt_q(&l_prod, &a_A_ntt[j], &l_x, q);
        rc = chipmunk_poly_invntt(&l_prod);
        if (rc != 0) return rc;
        for (size_t i = 0u; i < CHIPMUNK_N; ++i) {
            a_out->coeffs[i] =
                s_canon_mod_q_q((int64_t)a_out->coeffs[i] + l_prod.coeffs[i], q);
        }
    }
    return 0;
}

static int s_verify_bind_block(const chipmunk_poly_t a_A_pk[CHIPMUNK_MRING_K_PK],
                               const chipmunk_poly_t a_A_T[CHIPMUNK_MRING_K_PK],
                               const chipmunk_poly_t a_z_x[CHIPMUNK_MRING_K_PK],
                               const chipmunk_poly_t *a_c_star,
                               const chipmunk_poly_t *a_Y_pk,
                               const chipmunk_poly_t *a_T,
                               const chipmunk_poly_t *a_c,
                               const uint8_t a_fs_seed[32],
                               const chipmunk_mring_fold_proof_t *a_proof,
                               uint32_t a_fold_depth,
                               uint32_t a_n_ring,
                               uint32_t a_threshold)
{
    chipmunk_poly_t l_M_pk;
    chipmunk_poly_t l_M_T;
    chipmunk_poly_t l_c_chk;
    uint8_t l_bind_fs[32];
    int rc;

    rc = chipmunk_mring_bind_verify_reconstruct(
        &l_M_pk, &l_M_T, a_A_pk, a_A_T, a_z_x, a_c_star, a_Y_pk, a_T,
        (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        return rc;
    }

    rc = chipmunk_mring_transcript_bind_fs(
        l_bind_fs, a_fs_seed, a_c, &l_M_pk, &l_M_T, a_Y_pk, a_T,
        a_n_ring, a_threshold, a_proof, a_fold_depth);
    if (rc != 0) {
        return rc;
    }
    rc = chipmunk_mring_transcript_sample_c_star(&l_c_chk, l_bind_fs);
    if (rc != 0) {
        return rc;
    }
    return s_poly_equal(a_c_star, &l_c_chk) ? 0 : -EINVAL;
}

static int s_mring_sign_core(uint8_t **a_out_buf, size_t *a_out_size,
                             const chipmunk_lrs_secret_key_t *const *a_signer_sk,
                             size_t a_signer_count,
                             const chipmunk_lrs_public_key_t *a_ring,
                             uint32_t a_n_ring,
                             uint32_t a_threshold,
                             const uint8_t *a_message, size_t a_message_size,
                             const void *a_ctx, size_t a_ctx_size,
                             const uint8_t *a_randomness_seeds,
                             uint32_t a_outer_attempt)
{
    chipmunk_lrs_public_key_t *l_sorted =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_n_ring);
    chipmunk_poly_t *l_pks =
        DAP_NEW_Z_COUNT(chipmunk_poly_t, a_n_ring);
    chipmunk_poly_t *l_x_flat =
        DAP_NEW_Z_COUNT(chipmunk_poly_t, (size_t)a_n_ring * CHIPMUNK_MRING_K_PK);
    uint8_t *l_b = DAP_NEW_Z_SIZE(uint8_t, a_n_ring);
    if (!l_sorted || !l_pks || !l_x_flat || !l_b) {
        DAP_DELETE(l_sorted);
        DAP_DELETE(l_pks);
        DAP_DELETE(l_x_flat);
        DAP_DELETE(l_b);
        return -ENOMEM;
    }

    int rc = chipmunk_mring_canonicalise_ring(l_sorted, a_n_ring, a_ring);
    if (rc != 0) {
        goto out;
    }

    rc = s_build_b_indicator(l_b, a_n_ring, l_sorted, a_signer_sk,
                             a_signer_count, a_threshold);
    if (rc != 0) {
        goto out;
    }

    rc = s_load_pks_from_ring(l_pks, l_sorted, a_n_ring);
    if (rc != 0) {
        goto out;
    }

    for (size_t s = 0u; s < a_signer_count; ++s) {
        /*
         * CT-safe: always scan full ring, derive witness at matching
         * position via CT conditional (no early break).
         */
        for (uint32_t i = 0u; i < a_n_ring; ++i) {
            const int l_cmp = s_memcmp_ct(a_signer_sk[s]->P, l_sorted[i].P,
                                          CHIPMUNK_LRS_POLY_QPACK_BYTES);
            if (l_cmp != 0) {
                continue;
            }
            rc = chipmunk_lrs_derive_witness(
                &l_x_flat[(size_t)i * CHIPMUNK_MRING_K_PK],
                a_signer_sk[s]->x_seed);
            if (rc != 0) {
                goto out;
            }
        }
    }

    uint8_t l_ring_hash[32], l_ctx_hash[32], l_msg_hash[32];
    rc = chipmunk_mring_hash_sorted_ring(l_ring_hash, l_sorted, a_n_ring);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_hash_ctx(l_ctx_hash, CHIPMUNK_MRING_PARAMS_ID,
                                 a_ctx, a_ctx_size);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_hash_msg(l_msg_hash, CHIPMUNK_MRING_PARAMS_ID,
                                 a_message, a_message_size);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_A_pk[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_A_T[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_X[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_Y_pk;
    chipmunk_poly_t l_T_tag;

    rc = chipmunk_lrs_derive_A_pk(l_A_pk, CHIPMUNK_LRS_PARAMS_C0);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_derive_A_T(l_A_T, l_ring_hash, l_ctx_hash);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_aggregate_X(l_X, l_b, l_x_flat, a_n_ring,
                                    (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_lrs_relation_eval(&l_Y_pk, l_A_pk, l_X, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_lrs_relation_eval(&l_T_tag, l_A_T, l_X, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }

    chipmunk_mring_vcom_gens_t l_vcom_gens;
    rc = chipmunk_mring_derive_vcom_generators(&l_vcom_gens, l_ring_hash);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_b_poly;
    rc = chipmunk_mring_vcom_pack_b(&l_b_poly, l_b, a_n_ring);
    if (rc != 0) {
        goto out;
    }

    uint8_t l_master[32] = {0};
    uint8_t l_rb_seed[32] = {0};
    uint8_t l_fold_seed[32] = {0};
    uint8_t l_mask_seed[32] = {0};
    const size_t l_seed_bytes =
        a_signer_count * CHIPMUNK_LRS_SEED_BYTES;
    rc = s_derive_master_seed(l_master, a_randomness_seeds, l_seed_bytes);
    if (rc != 0) {
        goto out;
    }
    rc = s_derive_sub_seed(l_rb_seed, l_master, MRING_SIGN_RB_DOMAIN,
                           a_outer_attempt);
    if (rc != 0) {
        goto out;
    }
    rc = s_derive_sub_seed(l_fold_seed, l_master, MRING_SIGN_FOLD_DOMAIN,
                           a_outer_attempt);
    if (rc != 0) {
        goto out;
    }
    rc = s_derive_sub_seed(l_mask_seed, l_master, MRING_SIGN_MASK_DOMAIN,
                           a_outer_attempt);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_r_b[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_C_b;
    uint8_t l_T_q[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t l_Cb_q[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t l_Ypk_q[CHIPMUNK_MRING_POLY_QPACK];
    uint8_t l_fs_seed[32];

    rc = s_sample_r_b(l_r_b, l_rb_seed, 0u);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_vcom_commit(&l_C_b, &l_vcom_gens, &l_b_poly, l_r_b,
                                    (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_lrs_poly_qpack(l_T_q, &l_T_tag, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_lrs_poly_qpack(l_Cb_q, &l_C_b, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_fs_seed(l_fs_seed, l_ring_hash, l_ctx_hash,
                                l_msg_hash, l_T_q, l_Cb_q);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_c;
    rc = chipmunk_mring_transcript_sample_c(&l_c, l_fs_seed);
    if (rc != 0) {
        goto out;
    }

    rc = chipmunk_lrs_poly_qpack(l_Ypk_q, &l_Y_pk, (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }

    const uint32_t l_depth = chipmunk_mring_fold_depth_for(a_n_ring);
    chipmunk_mring_fold_proof_t *l_proof =
        DAP_NEW_Z(chipmunk_mring_fold_proof_t);
    if (!l_proof) {
        rc = -ENOMEM;
        goto out;
    }
    rc = chipmunk_mring_fold_proof_alloc(l_proof, l_depth);
    if (rc != 0) {
        DAP_DELETE(l_proof);
        goto out;
    }

    rc = chipmunk_mring_fold_prove(l_proof, l_b, a_n_ring, l_pks, &l_c,
                                   a_threshold, &l_Y_pk, l_ring_hash,
                                   l_fs_seed, l_fold_seed,
                                   (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }

    chipmunk_poly_t l_rho_x[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_z_x[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_c_star;
    int l_bind_ok = 0;

    for (uint32_t l_mask_att = 0u;
         l_mask_att < CHIPMUNK_MRING_MAX_ATTEMPTS && !l_bind_ok;
         ++l_mask_att) {
        rc = chipmunk_mring_bind_mask_sample(l_rho_x, l_mask_seed, l_mask_att);
        if (rc != 0) {
            break;
        }

        chipmunk_poly_t l_M_pk;
        chipmunk_poly_t l_M_T;
        rc = chipmunk_lrs_relation_eval(&l_M_pk, l_A_pk, l_rho_x, (uint64_t)CHIPMUNK_Q);
        if (rc != 0) {
            break;
        }
        rc = chipmunk_lrs_relation_eval(&l_M_T, l_A_T, l_rho_x, (uint64_t)CHIPMUNK_Q);
        if (rc != 0) {
            break;
        }

        uint8_t l_bind_fs[32];
        rc = chipmunk_mring_transcript_bind_fs(
            l_bind_fs, l_fs_seed, &l_c, &l_M_pk, &l_M_T,
            &l_Y_pk, &l_T_tag, a_n_ring, a_threshold,
            l_proof, l_depth);
        if (rc != 0) {
            break;
        }
        rc = chipmunk_mring_transcript_sample_c_star(&l_c_star, l_bind_fs);
        if (rc != 0) {
            break;
        }
        rc = chipmunk_mring_bind_prove_z_x(l_z_x, l_rho_x, &l_c_star, l_X,
                                            (uint64_t)CHIPMUNK_Q);
        if (rc == -EAGAIN) {
            continue;
        }
        if (rc != 0) {
            break;
        }
        l_bind_ok = 1;
    }

    if (!l_bind_ok) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        rc = rc == 0 ? -EAGAIN : rc;
        goto out;
    }

    const uint32_t l_wire = chipmunk_mring_wire_size(l_depth);
    uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_wire);
    if (!l_buf) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        rc = -ENOMEM;
        goto out;
    }

    chipmunk_mring_header_t l_hdr = {
        .magic      = CHIPMUNK_MRING_MAGIC,
        .version    = CHIPMUNK_MRING_VERSION,
        .params_id  = CHIPMUNK_MRING_PARAMS_ID,
        .n_ring     = a_n_ring,
        .threshold  = a_threshold,
        .fold_depth = l_depth,
        .flags      = CHIPMUNK_MRING_FLAGS_DEFAULT,
    };
    chipmunk_mring_header_write(l_buf, &l_hdr);

    const uint32_t l_off_hash = chipmunk_mring_section_off_fixed_hashes();
    memcpy(l_buf + l_off_hash, l_ring_hash, 32u);
    memcpy(l_buf + l_off_hash + 32u, l_ctx_hash, 32u);
    memcpy(l_buf + l_off_hash + 64u, l_msg_hash, 32u);
    memcpy(l_buf + l_off_hash + 96u, l_fs_seed, 32u);

    memcpy(l_buf + chipmunk_mring_section_off_T(), l_T_q,
           CHIPMUNK_MRING_POLY_QPACK);
    memcpy(l_buf + chipmunk_mring_section_off_cb(), l_Cb_q,
           CHIPMUNK_MRING_POLY_QPACK);
    memcpy(l_buf + chipmunk_mring_section_off_ypk(), l_Ypk_q,
           CHIPMUNK_MRING_POLY_QPACK);

    rc = chipmunk_mring_fold_write(l_buf, l_wire, l_depth, l_proof);
    chipmunk_mring_fold_proof_free(l_proof);
    DAP_DELETE(l_proof);
    if (rc != 0) {
        dap_memwipe(l_buf, l_wire);
        DAP_DELETE(l_buf);
        goto out;
    }

    rc = s_pack_bind_block(
        l_buf + chipmunk_mring_section_off_bind(l_depth),
        l_wire - chipmunk_mring_section_off_bind(l_depth),
        l_z_x, &l_c_star);
    if (rc != 0) {
        dap_memwipe(l_buf, l_wire);
        DAP_DELETE(l_buf);
        goto out;
    }

    *a_out_buf = l_buf;
    *a_out_size = l_wire;
    rc = 0;

out:
    dap_memwipe(l_master, sizeof(l_master));
    dap_memwipe(l_rb_seed, sizeof(l_rb_seed));
    dap_memwipe(l_fold_seed, sizeof(l_fold_seed));
    dap_memwipe(l_mask_seed, sizeof(l_mask_seed));
    /* Wipe secret witness material from stack. */
    dap_memwipe(l_X, sizeof(l_X));
    dap_memwipe(&l_Y_pk, sizeof(l_Y_pk));
    dap_memwipe(&l_T_tag, sizeof(l_T_tag));
    dap_memwipe(l_rho_x, sizeof(l_rho_x));
    dap_memwipe(l_z_x, sizeof(l_z_x));
    dap_memwipe(&l_c_star, sizeof(l_c_star));
    if (l_b) {
        dap_memwipe(l_b, a_n_ring);
    }
    DAP_DELETE(l_b);
    if (l_x_flat) {
        dap_memwipe(l_x_flat,
                    (size_t)a_n_ring * CHIPMUNK_MRING_K_PK
                    * sizeof(chipmunk_poly_t));
    }
    DAP_DELETE(l_x_flat);
    DAP_DELETE(l_pks);
    DAP_DELETE(l_sorted);
    return rc;
}

static int s_mring_verify_core(const uint8_t *a_buf, size_t a_buf_size,
                               const chipmunk_lrs_public_key_t *a_ring,
                               uint32_t a_n_ring,
                               const uint8_t *a_message, size_t a_message_size,
                               const void *a_ctx, size_t a_ctx_size,
                               uint64_t q)
{
    chipmunk_mring_header_t l_hdr;
    chipmunk_ring_error_t l_he =
        chipmunk_mring_header_read(&l_hdr, a_buf, a_buf_size);
    if (l_he != CHIPMUNK_RING_OK) {
        return -EINVAL;
    }
    if (a_n_ring != l_hdr.n_ring) {
        return -EINVAL;
    }

    chipmunk_lrs_public_key_t *l_sorted =
        DAP_NEW_Z_COUNT(chipmunk_lrs_public_key_t, a_n_ring);
    chipmunk_poly_t *l_pks =
        DAP_NEW_Z_COUNT(chipmunk_poly_t, a_n_ring);
    if (!l_sorted || !l_pks) {
        DAP_DELETE(l_sorted);
        DAP_DELETE(l_pks);
        return -ENOMEM;
    }

    int rc = chipmunk_mring_canonicalise_ring(l_sorted, a_n_ring, a_ring);
    if (rc != 0) {
        goto out;
    }

    uint8_t l_ring_hash[32], l_ctx_hash[32], l_msg_hash[32];
    rc = chipmunk_mring_hash_sorted_ring(l_ring_hash, l_sorted, a_n_ring);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_hash_ctx(l_ctx_hash, CHIPMUNK_MRING_PARAMS_ID,
                                 a_ctx, a_ctx_size);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_mring_hash_msg(l_msg_hash, CHIPMUNK_MRING_PARAMS_ID,
                                 a_message, a_message_size);
    if (rc != 0) {
        goto out;
    }

    const uint32_t l_off_hash = chipmunk_mring_section_off_fixed_hashes();
    if (s_memcmp_ct(l_ring_hash, a_buf + l_off_hash, 32u) != 0) {
        log_it(L_ERROR, "MRNG verify: ring_hash mismatch");
        rc = -EINVAL;
        goto out;
    }
    if (s_memcmp_ct(l_ctx_hash, a_buf + l_off_hash + 32u, 32u) != 0) {
        log_it(L_ERROR, "MRNG verify: ctx_hash mismatch");
        rc = -EINVAL;
        goto out;
    }
    if (s_memcmp_ct(l_msg_hash, a_buf + l_off_hash + 64u, 32u) != 0) {
        log_it(L_ERROR, "MRNG verify: msg_hash mismatch");
        rc = -EINVAL;
        goto out;
    }

    const uint8_t *l_fs_wire = a_buf + l_off_hash + 96u;
    uint8_t l_fs_seed[32];
    rc = chipmunk_mring_fs_seed(
        l_fs_seed, l_ring_hash, l_ctx_hash, l_msg_hash,
        a_buf + chipmunk_mring_section_off_T(),
        a_buf + chipmunk_mring_section_off_cb());
    if (rc != 0) {
        goto out;
    }
    if (s_memcmp_ct(l_fs_seed, l_fs_wire, 32u) != 0) {
        log_it(L_ERROR, "MRNG verify: fs_seed mismatch");
        rc = -EINVAL;
        goto out;
    }

    rc = s_load_pks_from_ring(l_pks, l_sorted, a_n_ring);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_c;
    rc = chipmunk_mring_transcript_sample_c(&l_c, l_fs_seed);
    if (rc != 0) {
        goto out;
    }

    chipmunk_poly_t l_Y_pk;
    chipmunk_poly_t l_T_tag;
    rc = chipmunk_lrs_poly_qunpack(
        &l_T_tag, a_buf + chipmunk_mring_section_off_T(), (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }
    rc = chipmunk_lrs_poly_qunpack(
        &l_Y_pk, a_buf + chipmunk_mring_section_off_ypk(), (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        goto out;
    }

    chipmunk_mring_fold_proof_t *l_proof =
        DAP_NEW_Z(chipmunk_mring_fold_proof_t);
    if (!l_proof) {
        rc = -ENOMEM;
        goto out;
    }
    rc = chipmunk_mring_fold_proof_alloc(l_proof, l_hdr.fold_depth);
    if (rc != 0) {
        DAP_DELETE(l_proof);
        goto out;
    }
    rc = chipmunk_mring_fold_read(l_proof, l_hdr.fold_depth,
                                  a_buf, a_buf_size,
                                  (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }

    rc = chipmunk_mring_fold_verify(l_proof, a_n_ring, l_pks, &l_c,
                                    l_hdr.threshold, &l_Y_pk,
                                    l_ring_hash, l_fs_seed,
                                    (uint64_t)CHIPMUNK_Q);
    if (rc != 0) {
        log_it(L_ERROR, "MRNG verify: fold_verify failed (rc=%d)", rc);
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }

    chipmunk_poly_t l_z_x[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_c_star;
    rc = s_unpack_bind_block(
        l_z_x, &l_c_star,
        a_buf + chipmunk_mring_section_off_bind(l_hdr.fold_depth),
        a_buf_size - chipmunk_mring_section_off_bind(l_hdr.fold_depth));
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }

    chipmunk_poly_t l_A_pk[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_A_T[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_A_pk_ntt[CHIPMUNK_MRING_K_PK];
    chipmunk_poly_t l_A_T_ntt[CHIPMUNK_MRING_K_PK];
    rc = chipmunk_lrs_derive_A_pk(l_A_pk, CHIPMUNK_LRS_PARAMS_C0);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    rc = chipmunk_mring_derive_A_T(l_A_T, l_ring_hash, l_ctx_hash);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    /* Pre-NTT A_pk and A_T once for the bind-verify reconstruct. */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        l_A_pk_ntt[j] = l_A_pk[j];
        l_A_T_ntt[j] = l_A_T[j];
        rc = chipmunk_poly_ntt(&l_A_pk_ntt[j]);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        rc = chipmunk_poly_ntt(&l_A_T_ntt[j]);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
    }

    /* Inline bind-verify-reconstruct with cached NTT. */
    for (uint32_t j = 0u; j < CHIPMUNK_MRING_K_PK; ++j) {
        if (chipmunk_lrs_poly_chknorm_centered(
                &l_z_x[j], CHIPMUNK_MRING_RESPONSE_BOUND, (uint64_t)CHIPMUNK_Q) != 0) {
            log_it(L_ERROR, "MRNG verify: ‖z_x[%u]‖∞ exceeds RESPONSE_BOUND", j);
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            rc = -ERANGE;
            goto out;
        }
    }

    chipmunk_poly_t l_M_pk, l_M_T;
    rc = s_relation_eval_ntt(&l_M_pk, l_A_pk_ntt, l_z_x, q);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    /* M_pk = A_pk·z_x − c*·Y_pk. */
    {
        chipmunk_poly_t l_cstar_ntt = l_c_star;
        chipmunk_poly_t l_Ypk_ntt = l_Y_pk;
        rc = chipmunk_poly_ntt(&l_cstar_ntt);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        rc = chipmunk_poly_ntt(&l_Ypk_ntt);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        chipmunk_poly_t l_cY;
        chipmunk_poly_mul_ntt_q(&l_cY, &l_cstar_ntt, &l_Ypk_ntt, q);
        rc = chipmunk_poly_invntt(&l_cY);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        rc = chipmunk_poly_sub_q(&l_M_pk, &l_M_pk, &l_cY, q);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
    }

    rc = s_relation_eval_ntt(&l_M_T, l_A_T_ntt, l_z_x, q);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    {
        chipmunk_poly_t l_cstar_ntt = l_c_star;
        chipmunk_poly_t l_T_ntt = l_T_tag;
        rc = chipmunk_poly_ntt(&l_cstar_ntt);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        rc = chipmunk_poly_ntt(&l_T_ntt);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        chipmunk_poly_t l_cT;
        chipmunk_poly_mul_ntt_q(&l_cT, &l_cstar_ntt, &l_T_ntt, q);
        rc = chipmunk_poly_invntt(&l_cT);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
        rc = chipmunk_poly_sub_q(&l_M_T, &l_M_T, &l_cT, q);
        if (rc != 0) {
            chipmunk_mring_fold_proof_free(l_proof);
            DAP_DELETE(l_proof);
            goto out;
        }
    }

    /* Bind FS and c* check. */
    uint8_t l_bind_fs[32];
    chipmunk_poly_t l_c_chk;
    rc = chipmunk_mring_transcript_bind_fs(
        l_bind_fs, l_fs_seed, &l_c, &l_M_pk, &l_M_T,
        &l_Y_pk, &l_T_tag, a_n_ring, l_hdr.threshold,
        l_proof, l_hdr.fold_depth);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    rc = chipmunk_mring_transcript_sample_c_star(&l_c_chk, l_bind_fs);
    if (rc != 0) {
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        goto out;
    }
    if (!s_poly_equal(&l_c_star, &l_c_chk)) {
        log_it(L_ERROR, "MRNG verify: bind block c* mismatch");
        chipmunk_mring_fold_proof_free(l_proof);
        DAP_DELETE(l_proof);
        rc = -EINVAL;
        goto out;
    }

    chipmunk_mring_fold_proof_free(l_proof);
    DAP_DELETE(l_proof);

out:
    DAP_DELETE(l_pks);
    DAP_DELETE(l_sorted);
    return rc;
}

/* -------------------------------------------------------------------------
 * Public sign / verify (M6 end-to-end wire glue).
 * ---------------------------------------------------------------------- */

chipmunk_ring_error_t chipmunk_ring_sign_to_bytes(
    uint8_t **a_out_buf, size_t *a_out_size,
    const struct chipmunk_lrs_secret_key *const *a_signer_sk,
    size_t a_signer_count,
    const struct chipmunk_lrs_public_key *a_ring,
    size_t a_ring_size,
    uint32_t a_threshold,
    const uint8_t *a_message, size_t a_message_size,
    const void *a_ctx, size_t a_ctx_size,
    const uint8_t *a_randomness_seeds)
{
    if (a_out_buf) {
        *a_out_buf = NULL;
    }
    if (a_out_size) {
        *a_out_size = 0;
    }

    if (!a_out_buf || !a_out_size || !a_signer_sk || !a_ring
        || !a_randomness_seeds) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if ((!a_message && a_message_size != 0u)
        || a_message_size > UINT32_MAX) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (!a_ctx && a_ctx_size != 0u) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (a_ring_size < CHIPMUNK_MRING_N_MIN
        || a_ring_size > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }
    if (a_threshold < CHIPMUNK_MRING_T_MIN
        || a_threshold > a_ring_size) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }
    if ((size_t)a_threshold != a_signer_count) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }
    if (a_signer_count * CHIPMUNK_LRS_SEED_BYTES == 0u) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }

    const uint32_t l_N = (uint32_t)a_ring_size;
    int l_rc = -EAGAIN;

    for (uint32_t l_attempt = 0u;
         l_attempt < CHIPMUNK_MRING_MAX_ATTEMPTS
         && l_rc == -EAGAIN;
         ++l_attempt) {
        l_rc = s_mring_sign_core(
            a_out_buf, a_out_size, a_signer_sk, a_signer_count,
            a_ring, l_N, a_threshold, a_message, a_message_size,
            a_ctx, a_ctx_size, a_randomness_seeds, l_attempt);
    }

    if (l_rc == 0) {
        return CHIPMUNK_RING_OK;
    }
    return s_map_int_err(l_rc);
}

chipmunk_ring_error_t chipmunk_ring_verify_from_bytes(
    const uint8_t *a_buf, size_t a_buf_size,
    const struct chipmunk_lrs_public_key *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size,
    const void *a_ctx, size_t a_ctx_size)
{
    if (!a_buf || !a_ring) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if ((!a_message && a_message_size != 0u)
        || a_message_size > UINT32_MAX) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (!a_ctx && a_ctx_size != 0u) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }

    chipmunk_mring_header_t l_h = {0};
    chipmunk_ring_error_t l_e =
        chipmunk_mring_header_read(&l_h, a_buf, a_buf_size);
    if (l_e != CHIPMUNK_RING_OK) {
        return l_e;
    }
    if (a_ring_size != (size_t)l_h.n_ring) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }

    const int l_rc = s_mring_verify_core(
        a_buf, a_buf_size, a_ring, l_h.n_ring,
        a_message, a_message_size, a_ctx, a_ctx_size,
        (uint64_t)CHIPMUNK_Q);
    return l_rc == 0 ? CHIPMUNK_RING_OK : s_map_int_err(l_rc);
}
