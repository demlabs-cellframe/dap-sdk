/*
 * chipmunk_ring_threshold.c — public threshold dealer/combiner API
 * (CR-9.4.A).  Implementation of dap_chipmunk_ring_threshold.h.
 *
 * Failure-mode discipline (inherited from CR-9.3, applied at every
 * entry/exit):
 *   - every error path zeroises the caller-visible output;
 *   - every internal scratch (chunk arrays, share scratch, lifted
 *     shamir-share buffers) is wiped via dap_memwipe before return;
 *   - dap_random_bytes errors are propagated as -EIO via CR-9.3.
 *
 * Wire format (per share, 72 bytes, all little-endian):
 *     0..3   magic ('CRHS')
 *     4      version (1)
 *     5      n  (2..64)
 *     6      t  (2..n)
 *     7      index (1..n)
 *     8..71  16 chunk slots × uint32_t LE (only low 22 bits populated)
 */

#include "dap_chipmunk_ring_threshold.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chipmunk_ring_shamir.h"
#include "chipmunk.h"            /* CHIPMUNK_Q */
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_ring_threshold"
#include "dap_common.h"

/* ------------------------------------------------------------------
 *  Static contract checks
 * ------------------------------------------------------------------ */

_Static_assert(CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES == 8u,
               "v1 share header must be exactly 8 bytes");
_Static_assert(CHIPMUNK_RING_THRESHOLD_SHARE_BODY_BYTES == 64u,
               "v1 share body must be exactly 64 bytes (16 × 4)");
_Static_assert(CHIPMUNK_RING_THRESHOLD_SHARE_BYTES == 72u,
               "v1 share is 72 bytes total");
_Static_assert(CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES == 32u,
               "master seed is 32 bytes");
_Static_assert(CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS * 2u
                == CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES,
               "16 × 16-bit chunks must cover the 32-byte master seed");
_Static_assert(CHIPMUNK_RING_THRESHOLD_MAX_N <= 0xFFu,
               "MAX_N must fit into a uint8_t share-header field");

/* ------------------------------------------------------------------
 *  Little-endian helpers (no host-endianness assumptions)
 * ------------------------------------------------------------------ */

static inline void s_put_le32(uint8_t *a_dst, uint32_t a_v)
{
    a_dst[0] = (uint8_t)( a_v        & 0xFFu);
    a_dst[1] = (uint8_t)((a_v >>  8) & 0xFFu);
    a_dst[2] = (uint8_t)((a_v >> 16) & 0xFFu);
    a_dst[3] = (uint8_t)((a_v >> 24) & 0xFFu);
}

static inline uint32_t s_get_le32(const uint8_t *a_src)
{
    return  (uint32_t)a_src[0]
         | ((uint32_t)a_src[1] <<  8)
         | ((uint32_t)a_src[2] << 16)
         | ((uint32_t)a_src[3] << 24);
}

static inline uint16_t s_get_le16(const uint8_t *a_src)
{
    return (uint16_t)( (uint16_t)a_src[0]
                    | ((uint16_t)a_src[1] << 8) );
}

static inline void s_put_le16(uint8_t *a_dst, uint16_t a_v)
{
    a_dst[0] = (uint8_t)( a_v       & 0xFFu);
    a_dst[1] = (uint8_t)((a_v >> 8) & 0xFFu);
}

/* ------------------------------------------------------------------
 *  Header read/write
 * ------------------------------------------------------------------ */

typedef struct s_share_header {
    uint32_t magic;
    uint8_t  version;
    uint8_t  n;
    uint8_t  t;
    uint8_t  index;
} s_share_header_t;

static void s_write_header(uint8_t *a_dst, const s_share_header_t *a_h)
{
    s_put_le32(a_dst, a_h->magic);
    a_dst[4] = a_h->version;
    a_dst[5] = a_h->n;
    a_dst[6] = a_h->t;
    a_dst[7] = a_h->index;
}

static void s_read_header(const uint8_t *a_src, s_share_header_t *a_h)
{
    a_h->magic   = s_get_le32(a_src);
    a_h->version = a_src[4];
    a_h->n       = a_src[5];
    a_h->t       = a_src[6];
    a_h->index   = a_src[7];
}

/* ==================================================================
 *  chipmunk_ring_threshold_deal
 * ==================================================================
 *
 *  For each of the 16 chunks of the master seed:
 *    1. lift the chunk into Z_q (chunk_value < 2^16 ⊂ [0, q));
 *    2. call chipmunk_ring_shamir_share(chunk_value, n, t)
 *       — a brand-new random polynomial per chunk (independence is
 *       what gives the per-chunk perfect secrecy claim);
 *    3. for each participant i in [1, n], copy P_chunk(i) into
 *       slot i's chunk[k].
 *
 *  All intermediate buffers are zeroised before return, on every
 *  exit path.
 */
int chipmunk_ring_threshold_deal(const uint8_t a_master_seed[32],
                                 uint32_t a_n,
                                 uint32_t a_t,
                                 chipmunk_ring_threshold_share_t *a_out_shares)
{
    if (a_out_shares == NULL) return -EINVAL;
    /* Up-front zero so every later error path is just `goto fail`. */
    memset(a_out_shares, 0,
           sizeof(*a_out_shares) * (a_n ? a_n : 1u));

    if (a_master_seed == NULL)                        return -EINVAL;
    if (a_n < 2u || a_n > CHIPMUNK_RING_THRESHOLD_MAX_N) return -EINVAL;
    if (a_t < 2u || a_t > a_n)                        return -EINVAL;

    /* Headers can be written immediately; bodies fill chunk-by-chunk. */
    const s_share_header_t l_hdr_template = {
        .magic   = CHIPMUNK_RING_THRESHOLD_SHARE_MAGIC,
        .version = (uint8_t)CHIPMUNK_RING_THRESHOLD_SHARE_VERSION,
        .n       = (uint8_t)a_n,
        .t       = (uint8_t)a_t,
        /* .index filled per-share */
    };
    for (uint32_t i = 0; i < a_n; ++i) {
        s_share_header_t l_hdr = l_hdr_template;
        l_hdr.index = (uint8_t)(i + 1u);
        s_write_header(a_out_shares[i].data, &l_hdr);
    }

    /* Per-chunk scratch reused across the 16 iterations.  Sized for
     * MAX_N so we allocate once, not per-chunk. */
    chipmunk_ring_shamir_share_t *l_shares =
        DAP_NEW_Z_COUNT(chipmunk_ring_shamir_share_t, a_n);
    if (l_shares == NULL) {
        memset(a_out_shares, 0, sizeof(*a_out_shares) * a_n);
        return -ENOMEM;
    }

    int l_rc = 0;
    for (uint32_t k = 0; k < CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS; ++k) {
        /* Lift the k-th 16-bit chunk into Z_q.  Bytes are little-endian
         * inside the master seed by definition (caller's seed bytes are
         * opaque, but the chunk encoding is canonical LE). */
        uint16_t l_chunk = s_get_le16(a_master_seed + (k * 2u));

        l_rc = chipmunk_ring_shamir_share((uint32_t)l_chunk, a_n, a_t, l_shares);
        if (l_rc != 0) goto fail;

        /* Distribute P_chunk(i) into share slot i. */
        for (uint32_t i = 0; i < a_n; ++i) {
            /* Sanity: shamir guarantees indices = 1..n in order. */
            if (l_shares[i].index != (i + 1u)) { l_rc = -EINVAL; goto fail; }
            uint8_t *l_body = a_out_shares[i].data
                            + CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES;
            s_put_le32(l_body + (k * 4u), l_shares[i].value);
        }
    }

    /* CR-D13/D25: wipe shamir scratch (carries y-values that are
     * sensitive — they are the share material itself). */
    dap_memwipe(l_shares, sizeof(*l_shares) * a_n);
    DAP_DELETE(l_shares);
    return 0;

fail:
    dap_memwipe(l_shares, sizeof(*l_shares) * a_n);
    DAP_DELETE(l_shares);
    memset(a_out_shares, 0, sizeof(*a_out_shares) * a_n);
    return l_rc;
}

/* ==================================================================
 *  chipmunk_ring_threshold_combine
 * ==================================================================
 *
 *  1. Validate each share (magic, version, n/t consistency, index
 *     range, chunk values in [0, q)).
 *  2. For each chunk k in [0, 16):
 *       reconstruct(chunk_k) from t (index, P_k(index)) pairs.
 *  3. Assemble 16 × 16-bit chunks back into the 32-byte master seed.
 */
int chipmunk_ring_threshold_combine(const chipmunk_ring_threshold_share_t *a_shares,
                                    uint32_t a_t,
                                    uint8_t a_out_master_seed[32])
{
    if (a_out_master_seed == NULL) return -EINVAL;
    /* Up-front zero. */
    memset(a_out_master_seed, 0, CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES);

    if (a_shares == NULL)                                return -EINVAL;
    if (a_t < 2u || a_t > CHIPMUNK_RING_THRESHOLD_MAX_N) return -EINVAL;

    /* Step 1 — validate every share, capture the consensus header. */
    s_share_header_t l_consensus;
    s_read_header(a_shares[0].data, &l_consensus);

    if (l_consensus.magic   != CHIPMUNK_RING_THRESHOLD_SHARE_MAGIC)   return -EINVAL;
    if (l_consensus.version != CHIPMUNK_RING_THRESHOLD_SHARE_VERSION) return -EINVAL;
    if (l_consensus.n < 2u || l_consensus.n > CHIPMUNK_RING_THRESHOLD_MAX_N)
        return -EINVAL;
    if (l_consensus.t < 2u || l_consensus.t > l_consensus.n) return -EINVAL;
    if ((uint32_t)l_consensus.t != a_t) {
        /* Caller asked for `a_t` shares but the dealing was for a
         * different `t`; mixing is a deployment foot-gun. */
        return -EINVAL;
    }

    for (uint32_t i = 0; i < a_t; ++i) {
        s_share_header_t l_h;
        s_read_header(a_shares[i].data, &l_h);
        if (l_h.magic   != l_consensus.magic
         || l_h.version != l_consensus.version
         || l_h.n       != l_consensus.n
         || l_h.t       != l_consensus.t) {
            return -EINVAL;
        }
        if (l_h.index < 1u || l_h.index > l_consensus.n) return -EINVAL;

        /* Duplicate-index detection (Lagrange divides by zero). */
        for (uint32_t j = i + 1u; j < a_t; ++j) {
            s_share_header_t l_h2;
            s_read_header(a_shares[j].data, &l_h2);
            if (l_h2.index == l_h.index) return -EINVAL;
        }

        /* Chunk-value range check: every y-value MUST be in [0, q).
         * Anything above the field bound is either a corrupted share
         * or coordinated tampering; either way Lagrange would silently
         * mod-reduce and produce a garbage seed.  Reject up-front. */
        const uint8_t *l_body = a_shares[i].data
                              + CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES;
        for (uint32_t k = 0; k < CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS; ++k) {
            uint32_t l_v = s_get_le32(l_body + (k * 4u));
            if (l_v >= (uint32_t)CHIPMUNK_Q) return -EINVAL;
        }
    }

    /* Step 2 — per-chunk reconstruction. */
    chipmunk_ring_shamir_share_t *l_chunk_shares =
        DAP_NEW_Z_COUNT(chipmunk_ring_shamir_share_t, a_t);
    if (l_chunk_shares == NULL) {
        memset(a_out_master_seed, 0, CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES);
        return -ENOMEM;
    }

    int l_rc = 0;
    uint8_t l_seed_scratch[CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES] = {0};

    for (uint32_t k = 0; k < CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS; ++k) {
        for (uint32_t i = 0; i < a_t; ++i) {
            const uint8_t *l_body = a_shares[i].data
                                  + CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES;
            l_chunk_shares[i].index = a_shares[i].data[7]; /* header.index */
            l_chunk_shares[i].value = s_get_le32(l_body + (k * 4u));
        }
        uint32_t l_chunk_secret = 0;
        l_rc = chipmunk_ring_shamir_reconstruct(l_chunk_shares, a_t,
                                                &l_chunk_secret);
        if (l_rc != 0) goto fail;
        if (l_chunk_secret > 0xFFFFu) {
            /* Reconstructed a chunk that doesn't fit back into 16
             * bits — must be a corrupted share that survived the
             * range check (e.g. coordinated tamper). */
            l_rc = -EINVAL;
            goto fail;
        }
        s_put_le16(l_seed_scratch + (k * 2u), (uint16_t)l_chunk_secret);
    }

    memcpy(a_out_master_seed, l_seed_scratch,
           CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES);
    dap_memwipe(l_seed_scratch, sizeof(l_seed_scratch));
    dap_memwipe(l_chunk_shares, sizeof(*l_chunk_shares) * a_t);
    DAP_DELETE(l_chunk_shares);
    return 0;

fail:
    dap_memwipe(l_seed_scratch, sizeof(l_seed_scratch));
    dap_memwipe(l_chunk_shares, sizeof(*l_chunk_shares) * a_t);
    DAP_DELETE(l_chunk_shares);
    memset(a_out_master_seed, 0, CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES);
    return l_rc;
}

/* ==================================================================
 *  chipmunk_ring_threshold_share_wipe
 * ================================================================== */
void chipmunk_ring_threshold_share_wipe(chipmunk_ring_threshold_share_t *a_share)
{
    if (a_share == NULL) return;
    dap_memwipe(a_share->data, sizeof(a_share->data));
}
