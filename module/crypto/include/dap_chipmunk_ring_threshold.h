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

/* Forward declarations from the chipmunk module so this public header
 * stays free of internal includes (the CR-9.5 PoP API needs them but
 * the threshold dealer/combiner above does not).  Full definitions
 * live in module/crypto/src/sig/chipmunk/chipmunk_hypertree.h. */
struct chipmunk_ht_public_key;
struct chipmunk_ht_private_key;

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

/* ==================================================================
 *  CR-9.5 — Proof of Possession (PoP) against rogue-key attack
 * ==================================================================
 *
 *  See doc/crypto/chipmunk_ring/design_decision_cr9_5.md for the
 *  full design rationale.  TL;DR: every public key admitted into a
 *  ring container (or a future CR-9.4.B aggregate) MUST be
 *  accompanied by a PoP — a chipmunk_ht signature under sk_i over a
 *  fixed, domain-separated, pk-bound message.  Without a valid PoP
 *  an attacker can publish pk_rogue = pk_target ⊕ pk_alibi (or any
 *  algebraic combination of other pks) and claim ownership without
 *  ever having held the matching secret.
 */

/** Magic prefix on the PoP blob, ASCII 'CRRP' little-endian
 *  (Chipmunk Ring **R**ing-**P**roof).  Distinct from the share
 *  magic 'CRHS' (CR-9.4.A) and the reserved partial-sig magic
 *  'CRHP' (CR-9.4.B) so no two CR-9 wire blobs collide on the wire. */
#define CHIPMUNK_RING_POP_MAGIC                 0x50525243u

/** PoP wire-format version.  Bumps when the body layout changes
 *  (today: serialised chipmunk_ht_signature). */
#define CHIPMUNK_RING_POP_VERSION               1u

/** Header layout: magic(4) || version(1) || reserved(3 zero bytes). */
#define CHIPMUNK_RING_POP_HEADER_BYTES          8u

/** Body layout: serialised hypertree signature.  Definition lives
 *  in chipmunk_hypertree.h (CHIPMUNK_HT_SIGNATURE_SIZE); we surface
 *  the *blob* constants here without dragging the chipmunk header
 *  into the public include path.  The implementation has a
 *  _Static_assert pinning CHIPMUNK_RING_POP_BODY_BYTES ==
 *  CHIPMUNK_HT_SIGNATURE_SIZE so any future signature-size change is
 *  a compile error at the exact buffer that depends on it.  Callers
 *  that need only the total blob size should use
 *  CHIPMUNK_RING_POP_BYTES; callers serialising directly must include
 *  chipmunk/chipmunk_hypertree.h to learn CHIPMUNK_HT_SIGNATURE_SIZE. */

/** Total wire size of a PoP blob (header + serialised ht_sig). */
#define CHIPMUNK_RING_POP_BYTES_FROM_HT_SIG(_ht_sig_size)                       \
    (CHIPMUNK_RING_POP_HEADER_BYTES + (size_t)(_ht_sig_size))

/**
 * Produce a Proof of Possession for the public key carried by @a a_sk.
 *
 * Internally:
 *   1. extract pk_bytes via chipmunk_ht_public_key_to_bytes;
 *   2. derive pop_message = SHA3-256("chipmunk-ring-pop/v1" ||
 *      LE32(len(pk_bytes)) || pk_bytes) — TupleHash discipline
 *      established in CR-D31;
 *   3. sign pop_message under sk via chipmunk_ht_sign (consumes
 *      leaf_index 0);
 *   4. wrap the signature with the CR-9.5 envelope.
 *
 * Contract:
 *   * @a a_sk MUST be a freshly-materialised hypertree sk with
 *     leaf_index == 0; if any production signing already consumed
 *     leaf 0 the function returns -EBUSY without touching @a a_sk.
 *   * @a a_out_pop is a caller-provided buffer of at least
 *     CHIPMUNK_RING_POP_BYTES_FROM_HT_SIG(CHIPMUNK_HT_SIGNATURE_SIZE)
 *     bytes.
 *
 * @return 0 on success;
 *         -EINVAL on NULL input;
 *         -EBUSY  if sk->leaf_index != 0;
 *         negative chipmunk_ht_sign / serialise errors otherwise.
 *         On any error the output buffer is fully zeroised.
 */
int chipmunk_ring_pop_create(struct chipmunk_ht_private_key *a_sk,
                             uint8_t *a_out_pop, size_t a_out_pop_size);

/**
 * Verify a PoP blob against a public key (in struct form).
 *
 * Validates the envelope (magic, version, reserved bytes all zero)
 * BEFORE deserialising the signature.  Recomputes pop_message from
 * the supplied @a a_pk — the verifier does NOT trust any message
 * accompanying the blob.
 *
 * @return 0 on PoP success; -EINVAL on contract violation;
 *         CHIPMUNK_ERROR_VERIFY_FAILED on signature mismatch.
 */
int chipmunk_ring_pop_verify(const struct chipmunk_ht_public_key *a_pk,
                             const uint8_t *a_pop, size_t a_pop_size);

/**
 * Same as `chipmunk_ring_pop_verify`, but takes the serialised
 * pk-bytes form (so a caller assembling a ring container from
 * on-wire pk blobs does not have to round-trip through the in-memory
 * struct).  @a a_pk_bytes_size MUST equal CHIPMUNK_HT_PUBLIC_KEY_SIZE.
 */
int chipmunk_ring_pop_verify_bytes(const uint8_t *a_pk_bytes, size_t a_pk_bytes_size,
                                   const uint8_t *a_pop,     size_t a_pop_size);

#ifdef __cplusplus
}
#endif

#endif /* DAP_CHIPMUNK_RING_THRESHOLD_H */
