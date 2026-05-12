/*
 * chipmunk_ring_shamir.h — internal Shamir secret sharing over Z_CHIPMUNK_Q
 *
 * CR-9.3 (kick-off slice).  See doc/crypto/chipmunk_ring/design_decision_cr9.md
 * for the full design rationale and acceptance criteria.
 *
 * This header is INTERNAL to the chipmunk_ring module.  Public threshold
 * API (chipmunk_ring_threshold_deal/sign_partial/combine) is the next
 * slice (CR-9.4) and will live in module/crypto/include/.
 *
 * Field: Z_CHIPMUNK_Q with q = 3168257 (22-bit prime, shared with HOTS).
 *
 * Contracts (full list in design_decision_cr9.md §3.3):
 *   - 2 <= t <= n
 *   - 2 <= n <= CHIPMUNK_RING_THRESHOLD_MAX_N
 *   - participant indices are 1..n (index 0 is forbidden — would expose
 *     the secret directly through P(0) = secret).
 *   - reconstruction rejects duplicate indices and indices outside [1, n].
 */

#ifndef CHIPMUNK_RING_SHAMIR_H
#define CHIPMUNK_RING_SHAMIR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Operational cap on the number of shares.  The mathematical ceiling is
 * `n < CHIPMUNK_Q ≈ 3M`, but every target use-case (governance,
 * social-recovery, corporate signing, DAO votes) sits well below 64.
 * Capping here keeps the on-wire share-array size predictable and
 * removes a foot-gun for downstream callers. */
#define CHIPMUNK_RING_THRESHOLD_MAX_N 64u

typedef struct chipmunk_ring_shamir_share {
    uint32_t index;     /* participant index in [1, n]                      */
    uint32_t value;     /* P(index) in [0, q)                               */
} chipmunk_ring_shamir_share_t;

/**
 * Split @a a_secret into @a a_n shares with reconstruction threshold @a a_t
 * over Z_CHIPMUNK_Q.  Coefficients are sampled from the system CSPRNG via
 * dap_random_bytes + rejection sampling so the dealer has no statistical
 * advantage in recovering the polynomial from any t-1 shares.
 *
 * @param[in]  a_secret      master secret in [0, CHIPMUNK_Q).  Values >= q
 *                           are rejected (lifting silently would invite
 *                           future "is the secret canonical?" foot-guns).
 * @param[in]  a_n           total share count, 2 <= n <= MAX_N.
 * @param[in]  a_t           reconstruction threshold, 2 <= t <= n.
 * @param[out] a_out_shares  caller-provided array of length @a a_n.
 *
 * @return 0 on success; -EINVAL on contract violation; -EIO if the
 *         CSPRNG cannot deliver entropy.  On any error the output array
 *         is zeroised before return.
 */
int chipmunk_ring_shamir_share(uint32_t a_secret,
                               uint32_t a_n,
                               uint32_t a_t,
                               chipmunk_ring_shamir_share_t *a_out_shares);

/**
 * Reconstruct the secret from exactly @a a_t shares via Lagrange
 * interpolation at x = 0.  The caller is responsible for selecting any
 * valid t-of-n subset; this primitive does not know n.
 *
 * @param[in]  a_shares       exactly @a a_t shares; indices must be
 *                            distinct and in [1, MAX_N].
 * @param[in]  a_t            number of shares (= reconstruction threshold).
 * @param[out] a_out_secret   reconstructed secret in [0, CHIPMUNK_Q).
 *                            Zeroised on any error path.
 *
 * @return 0 on success; -EINVAL on contract violation.
 */
int chipmunk_ring_shamir_reconstruct(const chipmunk_ring_shamir_share_t *a_shares,
                                     uint32_t a_t,
                                     uint32_t *a_out_secret);

/**
 * Modular multiplicative inverse in Z_CHIPMUNK_Q via Fermat
 * (a^{q-2} mod q).  Exposed for the unit-test surface so the
 * field arithmetic can be exercised in isolation.
 *
 * @return inv(a) mod q for a in [1, q); 0 for a == 0 (no inverse).
 */
uint32_t chipmunk_ring_shamir_mod_inverse(uint32_t a_a);

#ifdef __cplusplus
}
#endif

#endif /* CHIPMUNK_RING_SHAMIR_H */
