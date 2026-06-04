/*
 * Chipmunk MRNG — M1 stub with header parser.
 *
 * CR-11.G Phase 7.7 / task_ac273cea.
 *
 * M0 (done):  ruthlessly deleted CRNG/v1; sign + verify always returned
 *             CHIPMUNK_RING_ERR_NOT_IMPLEMENTED.
 * M1 (this):  pinned wire layout (see chipmunk_mring.h / README_MRNG.md);
 *             header parser + serialiser; sign returns NOT_IMPLEMENTED
 *             after validating that callers WOULD form a valid header;
 *             verify validates the on-wire header BEFORE punting to
 *             NOT_IMPLEMENTED, so size/magic/version regressions surface
 *             precise codes instead of a single generic sentinel.
 * M2+ (next): real cryptography behind hard gates G1..G5.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "dap_common.h"
#include "chipmunk_ring.h"
#include "chipmunk_mring.h"
#include "chipmunk_mring_params.h"

#define LOG_TAG "chipmunk_mring"

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
    s_store_u32_le(a_buf +  0, a_hdr->magic);
    s_store_u32_le(a_buf +  4, a_hdr->version);
    s_store_u32_le(a_buf +  8, a_hdr->params_id);
    s_store_u32_le(a_buf + 12, a_hdr->n_ring);
    s_store_u32_le(a_buf + 16, a_hdr->threshold);
    s_store_u32_le(a_buf + 20, a_hdr->fold_depth);
    s_store_u32_le(a_buf + 24, a_hdr->flags);
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

    chipmunk_mring_header_t l_h = {
        .magic      = s_load_u32_le(a_buf +  0),
        .version    = s_load_u32_le(a_buf +  4),
        .params_id  = s_load_u32_le(a_buf +  8),
        .n_ring     = s_load_u32_le(a_buf + 12),
        .threshold  = s_load_u32_le(a_buf + 16),
        .fold_depth = s_load_u32_le(a_buf + 20),
        .flags      = s_load_u32_le(a_buf + 24),
    };

    if (l_h.magic != CHIPMUNK_MRING_MAGIC) {
        return CHIPMUNK_RING_ERR_MAGIC_MISMATCH;
    }
    if (l_h.version != CHIPMUNK_MRING_VERSION) {
        return CHIPMUNK_RING_ERR_VERSION_MISMATCH;
    }
    if (l_h.params_id != CHIPMUNK_MRING_PARAMS_ID) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if (l_h.n_ring < CHIPMUNK_MRING_N_MIN ||
        l_h.n_ring > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }
    if (l_h.threshold < CHIPMUNK_MRING_T_MIN ||
        l_h.threshold > l_h.n_ring) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }
    const uint32_t l_expected_depth = chipmunk_mring_fold_depth_for(l_h.n_ring);
    if (l_h.fold_depth != l_expected_depth) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if ((l_h.flags & CHIPMUNK_MRING_FLAG_LINKABLE) == 0u) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }
    if ((l_h.flags & CHIPMUNK_MRING_FLAGS_RESERVED) != 0u) {
        return CHIPMUNK_RING_ERR_PARAMS_MISMATCH;
    }

    const uint32_t l_expected_size =
        chipmunk_mring_wire_size(l_h.fold_depth);
    if (a_buf_size < (size_t)l_expected_size) {
        return CHIPMUNK_RING_ERR_BUFFER_TOO_SMALL;
    }

    *a_hdr_out = l_h;
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
    case CHIPMUNK_RING_ERR_NOT_IMPLEMENTED:      return "MRNG: not implemented (M0/M1 stub)";
    case CHIPMUNK_RING_ERR_INTERNAL:             return "MRNG: internal error";
    default:                                     return "MRNG: unknown error";
    }
}

/* -------------------------------------------------------------------------
 * Public sign / verify stubs.
 *
 * sign:    validates (signer_count, ring_size, threshold) against the
 *          envelope; punts to NOT_IMPLEMENTED if and only if those would
 *          otherwise form a valid header.  This way the dap_sign shim
 *          continues to receive a single sentinel error during M1.
 * verify:  reads + validates the header so that any wire regression
 *          (magic / version / params / N / t / fold_depth / flags / size)
 *          surfaces with the exact code.  If the header is well-formed,
 *          returns NOT_IMPLEMENTED.
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
    (void)a_message;
    (void)a_message_size;
    (void)a_ctx;
    (void)a_ctx_size;
    (void)a_randomness_seeds;

    if (a_out_buf)  *a_out_buf  = NULL;
    if (a_out_size) *a_out_size = 0;

    if (!a_out_buf || !a_out_size || !a_signer_sk || !a_ring) {
        return CHIPMUNK_RING_ERR_NULL_PARAM;
    }
    if (a_ring_size < CHIPMUNK_MRING_N_MIN ||
        a_ring_size > CHIPMUNK_MRING_N_MAX) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }
    if (a_threshold < CHIPMUNK_MRING_T_MIN ||
        a_threshold > a_ring_size) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }
    if ((size_t)a_threshold != a_signer_count) {
        return CHIPMUNK_RING_ERR_T_OUT_OF_RANGE;
    }

    log_it(L_NOTICE,
           "MRNG sign: header validates (N=%zu, t=%u) but cryptographic "
           "core is M0/M1 stub (task_ac273cea); enabled in M3+",
           a_ring_size, a_threshold);
    return CHIPMUNK_RING_ERR_NOT_IMPLEMENTED;
}

chipmunk_ring_error_t chipmunk_ring_verify_from_bytes(
    const uint8_t *a_buf, size_t a_buf_size,
    const struct chipmunk_lrs_public_key *a_ring,
    size_t a_ring_size,
    const uint8_t *a_message, size_t a_message_size,
    const void *a_ctx, size_t a_ctx_size)
{
    (void)a_ring;
    (void)a_message;
    (void)a_message_size;
    (void)a_ctx;
    (void)a_ctx_size;

    chipmunk_mring_header_t l_h = {0};
    chipmunk_ring_error_t l_e =
        chipmunk_mring_header_read(&l_h, a_buf, a_buf_size);
    if (l_e != CHIPMUNK_RING_OK) {
        return l_e;
    }

    if (a_ring_size != (size_t)l_h.n_ring) {
        return CHIPMUNK_RING_ERR_N_RING_OUT_OF_RANGE;
    }

    log_it(L_NOTICE,
           "MRNG verify: header parses (N=%u, t=%u, fold_depth=%u, "
           "size=%u) but cryptographic core is M0/M1 stub",
           l_h.n_ring, l_h.threshold, l_h.fold_depth,
           chipmunk_mring_wire_size(l_h.fold_depth));
    return CHIPMUNK_RING_ERR_NOT_IMPLEMENTED;
}
