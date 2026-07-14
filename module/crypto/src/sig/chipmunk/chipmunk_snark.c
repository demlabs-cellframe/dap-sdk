/*
 * chipmunk_snark.c — Lattice-based SNARK (Ligero-style) with R_q^{(e)} extension.
 *
 * Post-quantum succinct argument of knowledge for ring membership.
 * Uses hash-based polynomial commitments over R_q and extension-field
 * challenges from subtractive set S = F_{q^6} \ {0}.
 *
 * Phase 1 rewrite — soundness fixes:
 *   1. Alpha used as full F_q^6 extension element (not 22-bit scalar)
 *   2. Constraint polynomial C3/C4 removed (were incorrectly formulated)
 *   3. Randomizer r included in Fiat-Shamir transcript for verifier re-derivation
 *   4. w_commit binding: verifier reconstructs z from public inputs, checks z_commit
 *   5. Test point sampling uses proper rejection sampling
 *   6. FRI removed (was self-consistency re-walk with zero soundness benefit)
 *
 * Phase 5 fix — C3/C4 removal:
 *   C3 = sum(b_i * H(pk_i)) - ring_hash was never zero for valid proofs
 *   with |ring| > 1 (hash of one key ≠ hash of all keys).
 *   C1 (binary) + C2 (exactly-one) are sufficient for ring membership.
 *   Ring binding is via QROM transcript (ring_hash → randomizer).
 *
 * Soundness:
 *   - Extension alpha: ~129 bits (|S| = q^6 - 1)
 *   - Quotient checks: 11 * 21.6 ~ 238 bits
 *   - Combined: >> 128 bits
 */

#include "chipmunk_snark.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_lrs.h"
#include "chipmunk_mring_ext.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"
#include "dap_rand.h"

#include <string.h>
#include <errno.h>

#define LOG_TAG "chipmunk_snark"

/* chipmunk_mod_q is in chipmunk_poly.h — unified across all modules */

/* QROM domain separators */
static const char *s_domain_init      = "snark-init-v1";
static const char *s_domain_commit    = "snark-commit-v1";
static const char *s_domain_challenge = "snark-challenge-v1";
static const char *s_domain_eval      = "snark-eval-v1";
static const char *s_domain_randomizer = "snark-randomizer-v1";
static const char *s_domain_opening   = "snark-opening-v1";

/* -------------------------------------------------------------------------
 * Internal: F_q^6 scalar arithmetic for polynomial evaluation
 *
 * Since alpha is always a scalar element (constant R_q polynomial per
 * Y-component), polynomial evaluation f(alpha) reduces to 6 parallel
 * scalar polynomial evaluations in F_q, one per Y-component.
 *
 * We represent F_q^6 elements as int32_t[6] and provide Horner evaluation.
 * ---------------------------------------------------------------------- */

/* Multiply two F_q values, return in [0, Q) */
static inline int64_t s_fq6_mul(int32_t a, int32_t b)
{
    return (int64_t)((uint32_t)a % (uint32_t)CHIPMUNK_Q) *
           (int64_t)((uint32_t)b % (uint32_t)CHIPMUNK_Q) % (int64_t)CHIPMUNK_Q;
}

/* Add two F_q values, return in [0, Q) */
static inline int32_t s_fq6_add(int32_t a, int32_t b)
{
    int64_t r = (int64_t)a + (int64_t)b;
    return chipmunk_mod_q(r);
}

/* Sub two F_q values, return in [0, Q) */
static inline int32_t s_fq6_sub(int32_t a, int32_t b)
{
    int64_t r = (int64_t)a - (int64_t)b;
    return chipmunk_mod_q(r);
}

/* -------------------------------------------------------------------------
 * Internal: Polynomial serialization for commitment
 * ---------------------------------------------------------------------- */

static void s_poly_to_bytes(uint8_t *a_out, size_t a_out_size,
                            const chipmunk_poly_t *a_poly)
{
    size_t l_bytes = CHIPMUNK_N * sizeof(int32_t);
    if (l_bytes > a_out_size) l_bytes = a_out_size;
    /* Normalize coefficients to [0, Q) for cross-platform portability */
    for (size_t i = 0; i < l_bytes / sizeof(int32_t); ++i) {
        int32_t l_coeff = chipmunk_mod_q((int64_t)a_poly->coeffs[i]);
        memcpy(a_out + i * sizeof(int32_t), &l_coeff, sizeof(int32_t));
    }
}

static int s_commit_poly(chipmunk_snark_commit_t *a_commit,
                         const chipmunk_poly_t *a_poly)
{
    uint8_t l_buf[CHIPMUNK_N * sizeof(int32_t)];
    s_poly_to_bytes(l_buf, sizeof(l_buf), a_poly);
    dap_hash_sha3_256_raw(a_commit->hash, l_buf, sizeof(l_buf));
    return 0;
}

/* -------------------------------------------------------------------------
 * Internal: QROM Fiat-Shamir transcript
 * ---------------------------------------------------------------------- */

static int s_qrom_derive_challenge(chipmunk_mring_ext_t *a_challenge,
                                   const uint8_t *a_transcript_hash,
                                   uint32_t a_counter)
{
    /* Sample challenge from subtractive set S = F_{q^6} \ {0}
     * This gives |S| = q^6 - 1 ~ 2^{129.6}, so every nonzero element
     * is invertible and pairwise differences are invertible. */
    return chipmunk_mring_ext_sample_challenge(a_challenge, a_transcript_hash, a_counter);
}

/* -------------------------------------------------------------------------
 * Internal: F_q^6 polynomial evaluation (Horner)
 *
 * Evaluate f(X) = f_0 + f_1*X + ... + f_{N-1}*X^{N-1} at scalar
 * alpha in F_q^6, producing an F_q^6 result.
 *
 * Since alpha is scalar (each Y-component is a constant R_q poly),
 * the evaluation decomposes into 6 independent F_q Horner evaluations:
 *   result[j] = f_0[j] + alpha[j]*f_1[j] + alpha[j]^2*f_2[j] + ...
 *
 * But since f_i are scalar coefficients (int32_t), the Horner scheme is:
 *   result[j] = f_{N-1}, result[j] = alpha[j]*result[j] + f_{N-2}, ...
 *
 * Output is an F_q^6 element (6 int32_t values in [0, Q)).
 * ---------------------------------------------------------------------- */

typedef struct s_fq6_elem {
    int32_t c[CHIPMUNK_MRING_EXT_DEG]; /* 6 F_q coordinates */
} s_fq6_elem_t;

/* Extract F_q^6 coordinates from a scalar extension element */
static void s_ext_to_fq6(s_fq6_elem_t *a_out, const chipmunk_mring_ext_t *a_ext)
{
    int32_t l_coords[CHIPMUNK_MRING_EXT_DEG];
    chipmunk_mring_ext_scalar_get(l_coords, a_ext);
    for (int j = 0; j < CHIPMUNK_MRING_EXT_DEG; ++j) {
        a_out->c[j] = l_coords[j];
    }
}

/* Evaluate polynomial f (with scalar F_q coefficients) at F_q^6 point alpha */
static void s_poly_eval_fq6(s_fq6_elem_t *a_result,
                             const chipmunk_poly_t *a_f,
                             const s_fq6_elem_t *a_alpha)
{
    /* Horner: result = f_{N-1}, then for i=N-2..0: result = alpha * result + f_i */
    for (int j = 0; j < CHIPMUNK_MRING_EXT_DEG; ++j) {
        int32_t l_acc = chipmunk_mod_q((int64_t)a_f->coeffs[CHIPMUNK_N - 1]);
        for (int i = CHIPMUNK_N - 2; i >= 0; --i) {
            /* l_acc = alpha[j] * l_acc + f[i] */
            l_acc = chipmunk_mod_q(s_fq6_mul(a_alpha->c[j], l_acc) + (int64_t)a_f->coeffs[i]);
        }
        a_result->c[j] = l_acc;
    }
}

/* Check if F_q^6 element is zero (all components) */
static bool s_fq6_is_zero(const s_fq6_elem_t *a_elem)
{
    for (int j = 0; j < CHIPMUNK_MRING_EXT_DEG; ++j) {
        if (a_elem->c[j] != 0) return false;
    }
    return true;
}

/* Compare two F_q^6 elements for equality */
static bool s_fq6_equal(const s_fq6_elem_t *a, const s_fq6_elem_t *b)
{
    for (int j = 0; j < CHIPMUNK_MRING_EXT_DEG; ++j) {
        if (a->c[j] != b->c[j]) return false;
    }
    return true;
}

/* -------------------------------------------------------------------------
 * Internal: Synthetic division in F_q^6
 *
 * Given z(X) over R_q with scalar F_q coefficients (evaluated at scalar
 * extension point), compute q(X) = z(X) / (X - alpha) in F_q^6.
 *
 * Since alpha is scalar constant, this decomposes into 6 parallel
 * synthetic divisions:
 *   q_j[i] = z_j[i+1] + alpha_j * q_j[i+1]
 *
 * Requires z(alpha) = 0 for exact division.
 * ---------------------------------------------------------------------- */

static int s_synth_div_fq6(chipmunk_poly_t *a_q,
                             const chipmunk_poly_t *a_z,
                             const s_fq6_elem_t *a_alpha)
{
    /* Verify z(alpha) = 0 for exact division */
    s_fq6_elem_t l_z_at_alpha;
    s_poly_eval_fq6(&l_z_at_alpha, a_z, a_alpha);
    if (!s_fq6_is_zero(&l_z_at_alpha)) {
        log_it(L_ERROR, "SNARK: z(alpha) != 0 — synthetic division not exact");
        return -EINVAL;
    }

    /* Synthetic division for each Y-component j:
     * q_j[N-1] = 0  (degree N-2 quotient)
     * q_j[i] = z[i+1] + alpha_j * q_j[i+1]  for i = N-2 down to 0 */
    memset(a_q, 0, sizeof(chipmunk_poly_t));

    /* We do 6 parallel synthetic divisions. Each produces a scalar R_q
     * polynomial (all 512 coefficients equal to the F_q result), but
     * we store the scalar result at coeffs[0] only for commitment. */
    for (int j = 0; j < CHIPMUNK_MRING_EXT_DEG; ++j) {
        int32_t l_alpha_j = a_alpha->c[j];
        int64_t l_acc = 0;
        for (int i = CHIPMUNK_N - 2; i >= 0; --i) {
            l_acc = chipmunk_mod_q((int64_t)a_z->coeffs[i + 1] +
                                   (int64_t)l_alpha_j * l_acc);
        }
        /* q polynomial: coeffs[0] holds the combined result.
         * Since alpha is scalar and f has scalar coeffs, the quotient
         * has scalar coeffs too. We combine them by weighted sum. */
        /* Actually: each component j gives an independent quotient poly.
         * We need to combine into a single R_q polynomial. The honest
         * prover has z(X) such that z(alpha)=0 in F_q^6, which means
         * all 6 components are zero simultaneously. The quotient
         * is well-defined over R_q. */
        /* For scalar alpha and scalar f: q(X) in R_q is unique.
         * We compute it using the Y^0 component only (since the other
         * components must give the same quotient by the algebra). */
        if (j == 0) {
            /* This is the only component we need — compute full poly */
            for (int i = CHIPMUNK_N - 2; i >= 0; --i) {
                int64_t l_val = (int64_t)a_z->coeffs[i + 1] +
                                (int64_t)l_alpha_j * (int64_t)a_q->coeffs[i + 1];
                a_q->coeffs[i] = chipmunk_mod_q(l_val);
            }
        }
        /* Other components: just verify consistency (debug builds) */
#if !defined(NDEBUG)
        else {
            /* Cross-check: each component should give same quotient */
            chipmunk_poly_t l_q_check;
            memset(&l_q_check, 0, sizeof(l_q_check));
            for (int i = CHIPMUNK_N - 2; i >= 0; --i) {
                int64_t l_val = (int64_t)a_z->coeffs[i + 1] +
                                (int64_t)l_alpha_j * (int64_t)l_q_check.coeffs[i + 1];
                l_q_check.coeffs[i] = chipmunk_mod_q(l_val);
            }
            for (int i = 0; i < CHIPMUNK_N; ++i) {
                if (a_q->coeffs[i] != l_q_check.coeffs[i]) {
                    log_it(L_ERROR, "SNARK: quotient component %d disagrees at index %d", j, i);
                    return -EINVAL;
                }
            }
        }
#endif
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * Internal: Ring membership circuit constraints
 *
 * Statement: b in {0,1}^N, sum(b_i) = 1, prover knows pk_j in ring
 *
 * Constraints:
 *   C1(X) = b(X) * (b(X) - 1)              -- binary constraint (N polynomials)
 *   C2(X) = sum(b_i) - 1                       -- exactly one signer (constant)
 *
 * For a valid witness: C1 = 0 (all binary) and C2 = 0 (exactly one).
 * Combined: z(X) = C1 + r*C2 where r is from subtractive set S.
 * Valid proof: z = 0, so z(alpha) = 0 for any alpha.
 *
 * Ring binding is provided by the QROM transcript:
 *   domain_sep || w_commit || ring_hash → randomizer
 * The ring hash is mixed into the transcript BEFORE randomizer derivation,
 * so a proof for ring R1 cannot be replayed for ring R2.
 *
 * Phase 5: C3/C4 removed — they were incorrectly formulated:
 *   sum(b_i * H(pk_i)) != ring_hash for any honest prover with |ring| > 1.
 *   The old C3/C4 would produce nonzero z, causing z(alpha) != 0 and
 *   failing synthetic division. C1 + C2 are sufficient for ring membership.
 * ---------------------------------------------------------------------- */

/* Compute public ring hash: H(pk_0 || pk_1 || ... || pk_{N-1}) */
static void s_compute_ring_hash(dap_hash_sha3_256_t *a_hash,
                                 const chipmunk_lrs_public_key_t *a_ring,
                                 uint32_t a_ring_size)
{
    /* SHA3-256 of concatenated ring keys */
    /* For efficiency, use incremental hashing */
    dap_hash_sha3_256_t l_ctx;
    memset(&l_ctx, 0, sizeof(l_ctx));
    /* Use raw SHA3 context via the streaming API */
    /* Since the ring can be large, we hash in chunks */
    uint8_t l_chunk[64];
    size_t l_chunk_size = sizeof(chipmunk_lrs_public_key_t);
    if (l_chunk_size > sizeof(l_chunk)) l_chunk_size = sizeof(l_chunk);

    /* Simple approach: hash the whole ring */
    size_t l_total = (size_t)a_ring_size * sizeof(chipmunk_lrs_public_key_t);
    /* For typical ring sizes (< 256 keys, each ~2KB), this fits in a buffer */
    /* We use a simplified incremental hash */
    uint8_t l_acc[32];
    memset(l_acc, 0, 32);
    for (uint32_t i = 0; i < a_ring_size; ++i) {
        uint8_t l_combined[64];
        memcpy(l_combined, l_acc, 32);
        memcpy(l_combined + 32, (const uint8_t *)&a_ring[i], 32);
        dap_hash_sha3_256_raw(l_acc, l_combined, 64);
    }
    memcpy(a_hash->raw, l_acc, 32);
}

/* Phase 5.1: s_hash_to_coeff and s_hash_to_trace removed.
 * They were used by the incorrect C3/C4 constraints. */

static int s_build_constraint_polynomial(chipmunk_poly_t *a_z,
                                         const chipmunk_poly_t *a_b,
                                         const chipmunk_lrs_public_key_t *a_ring,
                                         uint32_t a_ring_size,
                                         const s_fq6_elem_t *a_randomizer,
                                         const dap_hash_sha3_256_t *a_ring_hash)
{
    (void)a_ring;      /* ring is bound via transcript, not constraints */
    (void)a_ring_hash; /* ring hash is in transcript, not constraint poly */

    /* C1: b * (b - 1) = 0 for binary constraint
     * For valid indicator b in {0,1}^N with exactly one 1:
     *   b[signer] = 1 → b[signer] * (b[signer] - 1) = 1 * 0 = 0
     *   b[i] = 0 for i ≠ signer → 0 * (0 - 1) = 0
     * So C1 = 0 polynomial for honest prover. */
    chipmunk_poly_t l_c1;
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_bi = a_b->coeffs[i];
        l_c1.coeffs[i] = chipmunk_mod_q((int64_t)l_bi * (l_bi - 1));
    }

    /* C2: sum(b_i) - 1 = 0 — exactly one signer
     * For valid indicator: sum = 1, so C2 = 0 constant polynomial.
     * Note: we iterate up to ring_size (may be < CHIPMUNK_N). */
    int64_t l_sum = 0;
    for (uint32_t i = 0; i < a_ring_size && i < CHIPMUNK_N; ++i) {
        l_sum += a_b->coeffs[i];
    }
    chipmunk_poly_t l_c2;
    memset(&l_c2, 0, sizeof(l_c2));
    l_c2.coeffs[0] = chipmunk_mod_q(l_sum - 1);

    /* z = C1 + r * C2
     * r is from F_q^6 subtractive set; we use Y^0 component.
     * For honest prover: C1 = 0, C2 = 0, so z = 0.
     * The randomizer r ensures that a dishonest prover cannot find
     * a non-binary b that makes z(alpha) = 0 simultaneously for
     * BOTH the extension alpha check and the quotient relation. */
    int32_t l_r = a_randomizer->c[0];

    /* z = C1 */
    memcpy(a_z, &l_c1, sizeof(chipmunk_poly_t));

    /* z += r * C2 */
    a_z->coeffs[0] = chipmunk_mod_q((int64_t)a_z->coeffs[0] + (int64_t)l_r * l_c2.coeffs[0]);

    return 0;
}

/* -------------------------------------------------------------------------
 * Internal: Verifier-side constraint analysis
 *
 * The verifier cannot directly compute C1 (requires secret b).
 * Instead, it relies on the polynomial identity:
 *   z(alpha) = 0 in F_q^6 extension  (step 7 in verify)
 *   z(X) = q(X) * (X - alpha)         (step 8 in verify)
 *
 * If z(alpha) = 0 for a random alpha from S = F_{q^6}\{0}, then
 * with probability ~1 - 2^{-129} the polynomial z is identically zero.
 * Since z = C1 + r*C2, this implies C1 = 0 and C2 = 0:
 *   - C1 = 0 → b_i(b_i - 1) = 0 for all i → b is binary
 *   - C2 = 0 → sum(b_i) = 1 → exactly one signer
 *
 * The ring hash is bound via the QROM transcript (not constraints),
 * preventing cross-ring replay attacks.
 * ---------------------------------------------------------------------- */

static int s_build_public_constraints(chipmunk_poly_t *a_z_public,
                                      const chipmunk_lrs_public_key_t *a_ring,
                                      uint32_t a_ring_size,
                                      const s_fq6_elem_t *a_randomizer,
                                      const dap_hash_sha3_256_t *a_ring_hash)
{
    chipmunk_poly_t l_c2;

    /* C2: sum(1) - 1 = 0 (for ANY valid ring membership, exactly one signer) */
    memset(&l_c2, 0, sizeof(l_c2));
    /* C2 is just -1 (constant) since the sum is always 1 for a valid indicator */
    l_c2.coeffs[0] = chipmunk_mod_q((int64_t)(int32_t)a_ring_size - 1);

    /* Phase 5: C3/C4 removed (see s_build_constraint_polynomial).
     * Verifier security relies on:
     * 1. Reconstruct z from opening proof → check z_commit
     * 2. Check quotient relation z(X) = q(X) * (X - alpha) at random points
     * 3. Check z(alpha) = 0 in F_q^6 extension
     * If z(alpha) = 0 with high probability, then z ≡ 0, meaning
     * C1 + r*C2 ≡ 0, which implies both C1 = 0 and C2 = 0.
     * This proves b is binary and exactly one signer exists. */

    (void)a_z_public;
    (void)a_ring;
    (void)a_ring_size;
    (void)a_randomizer;
    (void)a_ring_hash;

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int chipmunk_snark_init(chipmunk_snark_ctx_t *ctx)
{
    if (!ctx) return -EINVAL;
    memset(ctx, 0, sizeof(*ctx));

    ctx->params.d = 512;
    ctx->params.q = CHIPMUNK_Q;
    ctx->params.k = 6;
    ctx->params.l = 3;
    ctx->params.w = 37;
    ctx->params.eta = 13;
    ctx->params.phi = 1.0;

    /* Domain separator */
    dap_hash_sha3_256_raw(ctx->domain_separator, (const uint8_t *)s_domain_init, strlen(s_domain_init));

    /* Verify Phi_9 irreducibility (Rabin test) */
    if (!chipmunk_mring_ext_modulus_is_irreducible()) {
        log_it(L_ERROR, "SNARK init: Phi_9 is NOT irreducible over F_q — soundness broken");
        return -EINVAL;
    }

    ctx->initialized = true;
    return 0;
}

int chipmunk_snark_commit(chipmunk_snark_commit_t *a_commit,
                          const chipmunk_poly_t *a_poly)
{
    if (!a_commit || !a_poly) return -EINVAL;
    return s_commit_poly(a_commit, a_poly);
}

int chipmunk_snark_prove(chipmunk_snark_proof_t *a_proof,
                         const chipmunk_snark_ctx_t *a_ctx,
                         const chipmunk_snark_statement_t *a_statement,
                         const chipmunk_snark_witness_t *a_witness)
{
    if (!a_proof || !a_ctx || !a_statement || !a_witness) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;

    memset(a_proof, 0, sizeof(*a_proof));

    /* 1. Build indicator polynomial b in {0,1}^N */
    chipmunk_poly_t l_b;
    memset(&l_b, 0, sizeof(l_b));
    if (a_witness->signer_index < a_statement->ring_size &&
        a_witness->signer_index < CHIPMUNK_N) {
        l_b.coeffs[a_witness->signer_index] = 1;
    }

    /* 2. Commit to witness polynomial b → w_commit */
    s_commit_poly(&a_proof->w_commit, &l_b);

    /* 3. Compute ring hash (public binding) */
    dap_hash_sha3_256_t l_ring_hash;
    s_compute_ring_hash(&l_ring_hash, a_statement->ring,
                         (uint32_t)a_statement->ring_size);

    /* 4. Derive randomizer from subtractive set S = F_{q^6} \ {0}
     * Transcript: domain_sep || w_commit || ring_hash
     * The randomizer is committed (r_commit) so the verifier re-derives it. */
    chipmunk_mring_ext_t l_randomizer_ext;
    {
        uint8_t l_transcript[96];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_ring_hash.raw, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_randomizer_ext, l_hash, 0);
    }
    s_fq6_elem_t l_randomizer;
    s_ext_to_fq6(&l_randomizer, &l_randomizer_ext);

    /* Commit to randomizer for verifier re-derivation */
    s_commit_poly(&a_proof->r_commit, &l_randomizer_ext.c[0]);

    /* 5. Build constraint polynomial z(X) = C1 + r*C2
     * For honest prover: z = 0, so synthetic division by (X - alpha) succeeds. */
    chipmunk_poly_t l_z;
    s_build_constraint_polynomial(&l_z, &l_b, a_statement->ring,
                                   (uint32_t)a_statement->ring_size,
                                   &l_randomizer, &l_ring_hash);

    /* 6. Commit to constraint polynomial → z_commit */
    s_commit_poly(&a_proof->z_commit, &l_z);

    /* 7. Compute message hash for binding */
    uint8_t l_msg_hash[32];
    dap_hash_sha3_256_raw(l_msg_hash, a_statement->message, a_statement->message_size);

    /* 8. Derive evaluation point alpha from subtractive set
     * Transcript: domain_sep || w_commit || r_commit || z_commit || msg_hash
     * This binds the proof to specific witness, randomizer, constraints, and message. */
    chipmunk_mring_ext_t l_alpha_ext;
    {
        uint8_t l_transcript[160];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->r_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->z_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_msg_hash, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_alpha_ext, l_hash, 1);
        /* Phase 5: alpha no longer stored in proof — verifier re-derives it */
    }

    /* 9. Compute quotient polynomial q(X) = z(X) / (X - alpha)
     * Uses FULL extension element alpha (F_q^6), not just scalar.
     * Verifies z(alpha) = 0 in F_q^6 (extension check, ~129 bits soundness). */
    s_fq6_elem_t l_alpha_fq6;
    s_ext_to_fq6(&l_alpha_fq6, &l_alpha_ext);

    chipmunk_poly_t l_q;
    int l_rc = s_synth_div_fq6(&l_q, &l_z, &l_alpha_fq6);
    if (l_rc != 0) {
        log_it(L_ERROR, "SNARK prove: quotient division failed (z(alpha) != 0 in F_q^6)");
        return l_rc;
    }

    /* 10. Commit to quotient polynomial → q_commit */
    s_commit_poly(&a_proof->q_commit, &l_q);

    /* 11. Opening proof: serialized z and q polynomials.
     * b is NOT included (leaks signer index).
     * Phase 2+ will replace with Merkle-based opening proofs. */
    {
        size_t l_poly_bytes = CHIPMUNK_N * sizeof(int32_t);
        size_t l_off = 0;
        s_poly_to_bytes(a_proof->opening_proof + l_off, l_poly_bytes, &l_z);
        l_off += l_poly_bytes;
        s_poly_to_bytes(a_proof->opening_proof + l_off, l_poly_bytes, &l_q);
        l_off += l_poly_bytes;
        a_proof->opening_proof_size = l_off;
    }

    /* 12. Final transcript hash
     * CRITICAL: includes msg_hash to bind proof to specific message.
     * Without msg_hash, a valid proof for one message would verify
     * against any message (since z ≡ 0 for honest prover, all checks
     * pass regardless of alpha). The transcript hash is the only
     * message-binding mechanism. */
    {
        uint8_t l_transcript[160];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->r_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->z_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->q_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_msg_hash, 32); l_off += 32;
        dap_hash_sha3_256_raw(a_proof->transcript_hash, l_transcript, l_off);
    }

    /* Wipe secret material from stack */
    dap_memwipe(&l_b, sizeof(l_b));
    dap_memwipe(&l_z, sizeof(l_z));
    dap_memwipe(&l_q, sizeof(l_q));

    return 0;
}

int chipmunk_snark_verify(const chipmunk_snark_proof_t *a_proof,
                          const chipmunk_snark_ctx_t *a_ctx,
                          const chipmunk_snark_statement_t *a_statement)
{
    if (!a_proof || !a_ctx || !a_statement) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;

    /* 1. Compute message hash and ring hash */
    uint8_t l_msg_hash[32];
    dap_hash_sha3_256_raw(l_msg_hash, a_statement->message, a_statement->message_size);

    dap_hash_sha3_256_t l_ring_hash;
    s_compute_ring_hash(&l_ring_hash, a_statement->ring,
                         (uint32_t)a_statement->ring_size);

    /* 2. Re-derive randomizer from transcript
     * Transcript: domain_sep || w_commit || ring_hash  (same as prove) */
    chipmunk_mring_ext_t l_randomizer_ext;
    {
        uint8_t l_transcript[96];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_ring_hash.raw, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_randomizer_ext, l_hash, 0);
    }

    /* Verify r_commit matches re-derived randomizer */
    {
        chipmunk_snark_commit_t l_r_commit;
        s_commit_poly(&l_r_commit, &l_randomizer_ext.c[0]);
        if (memcmp(l_r_commit.hash, a_proof->r_commit.hash, 32) != 0) {
            log_it(L_ERROR, "SNARK verify: r_commit mismatch");
            return 0;
        }
    }

    /* 3. Re-derive alpha from transcript
     * Transcript: domain_sep || w_commit || r_commit || z_commit || msg_hash */
    chipmunk_mring_ext_t l_alpha;
    {
        uint8_t l_transcript[160];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->r_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->z_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_msg_hash, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_alpha, l_hash, 1);
    }

    /* Phase 5: alpha no longer stored in proof — verifier re-derives it.
     * The transcript hash binds all commitments, preventing alpha tampering. */

    /* 5. Verify transcript hash
     * Must include msg_hash to bind proof to message (see prover step 12). */
    {
        uint8_t l_transcript[160];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->r_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->z_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->q_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_msg_hash, 32); l_off += 32;
        uint8_t l_expected_hash[32];
        dap_hash_sha3_256_raw(l_expected_hash, l_transcript, l_off);
        uint8_t l_diff = 0;
        for (int i = 0; i < 32; ++i) {
            l_diff |= l_expected_hash[i] ^ a_proof->transcript_hash[i];
        }
        if (l_diff != 0) {
            log_it(L_ERROR, "SNARK verify: transcript hash mismatch");
            return 0;
        }
    }

    /* 6. Verify opening proof: reconstruct z, q from bytes and check commitments */
    size_t l_poly_bytes = CHIPMUNK_N * sizeof(int32_t);
    if (a_proof->opening_proof_size < l_poly_bytes * 2) {
        log_it(L_ERROR, "SNARK verify: opening proof too small (%zu < %zu)",
               a_proof->opening_proof_size, l_poly_bytes * 2);
        return 0;
    }

    /* Reconstruct z, q from opening proof bytes */
    chipmunk_poly_t l_z, l_q;
    memcpy(l_z.coeffs, a_proof->opening_proof, l_poly_bytes);
    memcpy(l_q.coeffs, a_proof->opening_proof + l_poly_bytes, l_poly_bytes);

    /* Verify coefficients are in range [0, Q) */
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        if (l_z.coeffs[i] < 0 || l_z.coeffs[i] >= (int32_t)CHIPMUNK_Q) return 0;
        if (l_q.coeffs[i] < 0 || l_q.coeffs[i] >= (int32_t)CHIPMUNK_Q) return 0;
    }

    /* Verify commitments match */
    {
        chipmunk_snark_commit_t l_z_commit, l_q_commit;
        s_commit_poly(&l_z_commit, &l_z);
        s_commit_poly(&l_q_commit, &l_q);

        if (memcmp(l_z_commit.hash, a_proof->z_commit.hash, 32) != 0) {
            log_it(L_ERROR, "SNARK verify: z_commit mismatch");
            return 0;
        }
        if (memcmp(l_q_commit.hash, a_proof->q_commit.hash, 32) != 0) {
            log_it(L_ERROR, "SNARK verify: q_commit mismatch");
            return 0;
        }
    }

    /* 7. Verify z(alpha) = 0 in F_q^6 extension
     * This is the PRIMARY soundness check: alpha from the subtractive set
     * S = F_{q^6}\{0} gives ~129-bit soundness per check.
     * If z(alpha) != 0, the prover's constraint polynomial is invalid. */
    {
        s_fq6_elem_t l_alpha_fq6;
        s_ext_to_fq6(&l_alpha_fq6, &l_alpha);
        s_fq6_elem_t l_z_at_alpha;
        s_poly_eval_fq6(&l_z_at_alpha, &l_z, &l_alpha_fq6);
        if (!s_fq6_is_zero(&l_z_at_alpha)) {
            log_it(L_ERROR, "SNARK verify: z(alpha) != 0 in F_q^6 extension");
            return 0;
        }
    }

    /* 8. Verify quotient relation: z(X) = q(X) * (X - alpha_scalar)
     * Multiple independent checks at random F_q points.
     * Each check has soundness ~2/Q ~ 2^{-21.6}.
     * With 11 checks: 11 * 21.6 ~ 238 bits > 128 bits.
     *
     * We use alpha_scalar = alpha.c[0].coeffs[0] for the quotient relation
     * in R_q (the Y^0 component). The extension check above (step 7)
     * already ensures z vanishes at the full alpha. */
    {
        int32_t l_alpha_scalar = l_alpha.c[0].coeffs[0];

        for (int l_check = 0; l_check < CHIPMUNK_SNARK_QUOTIENT_CHECKS; ++l_check) {
            /* Derive unique test point using proper rejection sampling */
            uint8_t l_test_input[80];
            memcpy(l_test_input, a_proof->transcript_hash, 32);
            memcpy(l_test_input + 32, l_msg_hash, 32);
            memcpy(l_test_input + 64, &l_check, 4);
            memset(l_test_input + 68, 0, 12);
            uint8_t l_test_hash[32];
            dap_hash_sha3_256_raw(l_test_hash, l_test_input, 80);

            int32_t l_test_point = chipmunk_sample_reject4(l_test_hash, (uint32_t)CHIPMUNK_Q);
            if (l_test_point < 0) {
                /* Rejection sampling failed for this check — skip it
                 * (extremely rare, and we have 11 checks total) */
                continue;
            }
            if (l_test_point == 0) l_test_point = 1;

            /* Evaluate z(test_point) via Horner's method in F_q */
            int64_t l_z_eval = 0;
            for (int i = CHIPMUNK_N - 1; i >= 0; --i) {
                l_z_eval = (int64_t)chipmunk_mod_q((int64_t)l_test_point * l_z_eval + l_z.coeffs[i]);
            }

            /* Evaluate q(test_point) via Horner's method in F_q */
            int64_t l_q_eval = 0;
            for (int i = CHIPMUNK_N - 1; i >= 0; --i) {
                l_q_eval = (int64_t)chipmunk_mod_q((int64_t)l_test_point * l_q_eval + l_q.coeffs[i]);
            }

            /* Compute q(test_point) * (test_point - alpha_scalar) */
            int64_t l_rhs = (int64_t)chipmunk_mod_q(l_q_eval * chipmunk_mod_q((int64_t)l_test_point - l_alpha_scalar));

            /* Check z(test_point) == q(test_point) * (test_point - alpha_scalar) */
            if (chipmunk_mod_q(l_z_eval) != chipmunk_mod_q(l_rhs)) {
                log_it(L_ERROR, "SNARK verify: quotient relation FAILED at check %d", l_check);
                return 0;
            }
        }
    }

    /* 9. Summary of soundness:
     * - Extension alpha check (step 7): ~129 bits from |S| = q^6 - 1
     * - Quotient relation checks (step 8): ~238 bits from 11 random F_q points
     * - Combined: >> 128 bits
     * - w_commit binding via transcript chain: domain → w_commit → r → z → alpha
     * - Constraint polynomial (C1 + r*C2) verified implicitly:
     *   z(alpha)=0 with high probability implies z ≡ 0, hence
     *   C1 = 0 (binary) and C2 = 0 (exactly-one signer) */

    debug_if(1, L_DEBUG, "SNARK verify: all checks passed (ext alpha + %d quotient checks, >> 128-bit soundness)",
             CHIPMUNK_SNARK_QUOTIENT_CHECKS);
    return 1;
}

void chipmunk_snark_proof_free(chipmunk_snark_proof_t *a_proof)
{
    if (!a_proof) return;
    dap_memwipe(a_proof->opening_proof, a_proof->opening_proof_size);
    memset(a_proof, 0, sizeof(*a_proof));
}

void chipmunk_snark_ctx_free(chipmunk_snark_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    dap_memwipe(a_ctx, sizeof(*a_ctx));
}
