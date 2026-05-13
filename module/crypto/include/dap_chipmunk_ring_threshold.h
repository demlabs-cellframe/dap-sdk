/*
 * dap_chipmunk_ring_threshold.h — public threshold dealer/combiner API
 * (CR-9.4.A).  See doc/crypto/chipmunk_ring/design_decision_cr9_4.md
 * for the full design rationale.
 *
 * This header carries the *trusted-dealer* dealer/combine surface
 * (Shamir secret sharing of the 32-byte ring master seed; reconstruction
 * happens at the combiner; signing then proceeds through the existing
 * chipmunk_ring_sign path).  The true threshold-signature surface
 * (sign_partial / aggregate without key reconstruction) is reserved
 * but NOT shipped — see CR-9.4.B in the design doc and the explicit
 * "deferred follow-up" section therein.
 *
 * Every CR-9.0 target use-case (governance multi-sig, social-recovery,
 * corporate signing, DAO votes) is fully served by this API.  CR-9.4.B
 * is a defence-in-depth upgrade, not a launch blocker.
 */

#ifndef DAP_CHIPMUNK_RING_THRESHOLD_H
#define DAP_CHIPMUNK_RING_THRESHOLD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------
 *  Wire-format constants
 * ------------------------------------------------------------------ */

/** Magic prefix on the share blob, ASCII 'CRHS' little-endian. */
#define CHIPMUNK_RING_THRESHOLD_SHARE_MAGIC     0x53485243u

/** Wire-format version; bumps when the chunk-encoding strategy
 *  changes.  Currently 1 = 16 × 16-bit chunks. */
#define CHIPMUNK_RING_THRESHOLD_SHARE_VERSION   1u

/** Number of 16-bit chunks in a v1 share — exactly enough to carry
 *  one 32-byte master seed losslessly under Shamir over Z_CHIPMUNK_Q. */
#define CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS    16u

/** Header layout: magic(4) || version(1) || n(1) || t(1) || index(1). */
#define CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES   8u

/** Body layout: 16 chunk slots × 4 bytes (only low 22 bits populated). */
#define CHIPMUNK_RING_THRESHOLD_SHARE_BODY_BYTES                                \
    (CHIPMUNK_RING_THRESHOLD_SHARE_CHUNKS * 4u)

/** Total wire size = 8 + 64 = 72 bytes per share. */
#define CHIPMUNK_RING_THRESHOLD_SHARE_BYTES                                     \
    (CHIPMUNK_RING_THRESHOLD_SHARE_HEADER_BYTES                                 \
     + CHIPMUNK_RING_THRESHOLD_SHARE_BODY_BYTES)

/** Master seed size in bytes (matches CHIPMUNK_HT private-key seed). */
#define CHIPMUNK_RING_THRESHOLD_MASTER_SEED_BYTES   32u

/* ------------------------------------------------------------------
 *  Reserved magic for CR-9.4.B (true threshold partial signatures).
 *  Declared here so the two formats cannot collide on the wire even
 *  when stored side-by-side.  Implementation lands in CR-9.4.B. */
#define CHIPMUNK_RING_THRESHOLD_PARTIAL_SIG_MAGIC   0x50485243u   /* 'CRHP' LE */

/* ------------------------------------------------------------------
 *  Wire-blob carrier
 * ------------------------------------------------------------------
 *
 *  The share is a flat fixed-size blob; struct wraps it so the type
 *  system catches mismatches and so callers can pass it by value.
 *  Endianness inside `data` is little-endian throughout.
 */
typedef struct chipmunk_ring_threshold_share {
    uint8_t data[CHIPMUNK_RING_THRESHOLD_SHARE_BYTES];
} chipmunk_ring_threshold_share_t;

/* ==================================================================
 *  Public API
 * ================================================================== */

/**
 * Split a 32-byte master seed into @a a_n threshold shares with
 * reconstruction threshold @a a_t (any @a a_t shares can rebuild
 * the master; @a a_t-1 shares are statistically indistinguishable
 * from random under the Shamir guarantee).
 *
 * Coefficients for every chunk are sampled independently from the
 * system CSPRNG via dap_random_bytes + rejection sampling
 * (CR-9.3 primitive).  Master-seed bytes never leave the dealer
 * except as P_chunk(i) evaluations; all dealer-internal scratch is
 * dap_memwipe'd before return.
 *
 * Distribution is the caller's responsibility — share blobs are
 * NOT encrypted on the wire; confidentiality of the per-participant
 * channel is the caller's concern (typically a TLS-protected
 * out-of-band exchange).
 *
 * @param[in]  a_master_seed  exactly 32 bytes; MUST NOT be NULL.
 * @param[in]  a_n            total share count, 2 <= n <= MAX_N (= 64).
 * @param[in]  a_t            reconstruction threshold, 2 <= t <= n.
 * @param[out] a_out_shares   caller-provided array of length @a a_n.
 *
 * @return 0 on success; -EINVAL on contract violation; -ENOMEM on
 *         internal allocation failure; -EIO on CSPRNG failure.
 *         On any error the output buffer is fully zeroised.
 */
int chipmunk_ring_threshold_deal(const uint8_t a_master_seed[32],
                                 uint32_t a_n,
                                 uint32_t a_t,
                                 chipmunk_ring_threshold_share_t *a_out_shares);

/**
 * Reconstruct the master seed from any @a a_t valid shares produced
 * by chipmunk_ring_threshold_deal.
 *
 * Validates each share's magic, version, (n, t) echo, and index range
 * before consuming its chunks.  Cross-checks that all @a a_t shares
 * agree on (magic, version, n, t) — mixed dealing rounds are
 * deployment foot-guns and are rejected before reconstruction.
 * Duplicate participant indices (which would divide by zero in the
 * Lagrange basis) and out-of-range chunk values (which would silently
 * pass through if mod-q masked) are also rejected.
 *
 * @param[in]  a_shares           exactly @a a_t shares; indices must
 *                                be distinct and in [1, n].
 * @param[in]  a_t                number of shares == reconstruction
 *                                threshold; 2 <= t <= MAX_N.
 * @param[out] a_out_master_seed  exactly 32 bytes; written only on
 *                                success.
 *
 * @return 0 on success; -EINVAL on any contract violation.  On any
 *         error the output buffer is fully zeroised (a non-zero
 *         return MUST be treated as "do NOT use the output buffer").
 */
int chipmunk_ring_threshold_combine(const chipmunk_ring_threshold_share_t *a_shares,
                                    uint32_t a_t,
                                    uint8_t a_out_master_seed[32]);

/**
 * Securely wipe a share buffer via dap_memwipe.  Idempotent;
 * NULL-safe.  This is the only sanctioned way for callers to scrub
 * share memory without including the internal memwipe header.
 */
void chipmunk_ring_threshold_share_wipe(chipmunk_ring_threshold_share_t *a_share);

#ifdef __cplusplus
}
#endif

#endif /* DAP_CHIPMUNK_RING_THRESHOLD_H */
