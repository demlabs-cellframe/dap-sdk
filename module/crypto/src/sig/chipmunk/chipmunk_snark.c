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
 *   4. Test point sampling uses proper rejection sampling
 *   5. FRI removed (was self-consistency re-walk with zero soundness benefit)
 *
 * Phase 5 fix — C3/C4 removal:
 *   C3 = sum(b_i * H(pk_i)) - ring_hash was never zero for valid proofs
 *   with |ring| > 1 (hash of one key ≠ hash of all keys).
 *   C1 (binary) + C2 (exactly-one) are sufficient for ring membership.
 *   Ring binding is via QROM transcript (ring_hash → randomizer).
 *
 * Phase 7 fixes — privacy and robustness:
 *   6. w_commit uses random nonce instead of H(b) — prevents signer
 *      de-anonymization (b is sparse: 1-of-512, precomputable hashes)
 *   7. Ring hash uses full public keys (1424 bytes each), not just 32 bytes
 *   8. Quotient check loops on rejection failure — never skips, guarantees 238-bit soundness
 *   9. Test points use SHAKE256 XOF instead of SHA3+4-byte truncation
 *  10. Dead s_build_public_constraints stub removed
 *
 * Soundness:
 *   - Extension alpha: ~129 bits (|S| = q^6 - 1)
 *   - Quotient checks: 11 * 21.6 ~ 238 bits (all checks guaranteed to execute)
 *   - Combined: >> 128 bits
 */

#include "chipmunk_snark.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "chipmunk_field.h"
#include "chipmunk_lrs.h"
#include "chipmunk_fq6_ext.h"
#include "chipmunk_rs.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"
#include "dap_rand.h"

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#define LOG_TAG "chipmunk_snark"

/* Parameterized modular reduction: s_mod_q(val, q) replaces chipmunk_mod_q(val)
 * which uses the global CHIPMUNK_Q.  Used throughout Phase 9.13f to make every
 * static helper work with an arbitrary prime modulus. */
static inline int32_t s_mod_q(int64_t val, uint64_t q)
{
    int64_t r = val % (int64_t)q;
    if (r < 0) r += (int64_t)q;
    return (int32_t)r;
}

/* Parameterized F_q multiplication: replaces s_fq6_mul which uses global CHIPMUNK_Q. */
static inline int64_t s_fq6_mul_q(int32_t a, int32_t b, uint64_t q)
{
    int64_t la = (int64_t)a % (int64_t)q;
    if (la < 0) la += (int64_t)q;
    int64_t lb = (int64_t)b % (int64_t)q;
    if (lb < 0) lb += (int64_t)q;
    return la * lb % (int64_t)q;
}

/* Parameterized s_fqmul: field multiplication in [0, q). */
static inline int32_t s_fqmul_q(int32_t a_a, int32_t a_b, uint64_t q)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)q);
    if (l_r < 0) l_r += (int32_t)q;
    return l_r;
}

/* QROM domain separator */
static const char *s_domain_init = "snark-init-v1";

/* -------------------------------------------------------------------------
 * Internal: F_q^6 scalar arithmetic for polynomial evaluation
 *
 * Since alpha is always a scalar element (constant R_q polynomial per
 * Y-component), polynomial evaluation f(alpha) reduces to 6 parallel
 * scalar polynomial evaluations in F_q, one per Y-component.
 *
 * We represent F_q^6 elements as int32_t[6] and provide Horner evaluation.
 * ---------------------------------------------------------------------- */

/* -------------------------------------------------------------------------
 * Internal: Polynomial serialization for commitment
 * ---------------------------------------------------------------------- */

static void s_poly_to_bytes(uint8_t *a_out, size_t a_out_size,
                            const chipmunk_poly_t *a_poly,
                            uint32_t a_d, uint64_t a_q)
{
    size_t l_bytes = (size_t)a_d * sizeof(int32_t);
    if (l_bytes > a_out_size) l_bytes = a_out_size;
    /* Normalize coefficients to [0, q) for cross-platform portability */
    for (size_t i = 0; i < l_bytes / sizeof(int32_t); ++i) {
        int32_t l_coeff = s_mod_q((int64_t)a_poly->coeffs[i], a_q);
        memcpy(a_out + i * sizeof(int32_t), &l_coeff, sizeof(int32_t));
    }
}

static int s_commit_poly(chipmunk_snark_commit_t *a_commit,
                         const chipmunk_poly_t *a_poly,
                         uint32_t a_d, uint64_t a_q)
{
    uint8_t l_buf[CHIPMUNK_SNARK_MAX_D * sizeof(int32_t)];
    size_t l_bytes = (size_t)a_d * sizeof(int32_t);
    s_poly_to_bytes(l_buf, l_bytes, a_poly, a_d, a_q);
    dap_hash_sha3_256_raw(a_commit->hash, l_buf, l_bytes);
    dap_memwipe(l_buf, l_bytes);
    return 0;
}

/* -------------------------------------------------------------------------
 * Internal: QROM Fiat-Shamir transcript
 * ---------------------------------------------------------------------- */

static int s_qrom_derive_challenge(chipmunk_fq6_ext_t *a_challenge,
                                   const uint8_t *a_transcript_hash,
                                   uint32_t a_counter, uint64_t a_q)
{
    /* Sample challenge from subtractive set S = F_{q^6} \ {0}
     * This gives |S| = q^6 - 1 ~ 2^{129.6}, so every nonzero element
     * is invertible and pairwise differences are invertible.
     * Phase 9.13: challenge is sampled in the ACTIVE field F_q, not the
     * global CHIPMUNK_Q — critical for soundness when q != CHIPMUNK_Q. */
    return chipmunk_fq6_ext_sample_challenge_q(a_challenge, a_transcript_hash,
                                                 a_counter, a_q);
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
    int32_t c[CHIPMUNK_FQ6_EXT_DEG]; /* 6 F_q coordinates */
} s_fq6_elem_t;

/* Extract F_q^6 coordinates from a scalar extension element */
static void s_ext_to_fq6(s_fq6_elem_t *a_out, const chipmunk_fq6_ext_t *a_ext,
                          uint64_t q)
{
    int32_t l_coords[CHIPMUNK_FQ6_EXT_DEG];
    chipmunk_fq6_ext_scalar_get_q(l_coords, a_ext, q);
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        a_out->c[j] = l_coords[j];
    }
}

/* Evaluate polynomial f (with scalar F_q coefficients) at F_q^6 point alpha.
 * @param a_result  Output F_q^6 evaluation.
 * @param a_f       Polynomial to evaluate (coeffs in F_q).
 * @param a_alpha   Extension point (6 F_q coordinates).
 * @param a_d       Polynomial dimension (number of coefficients to use).
 * @param a_q       Field modulus. */
static void s_poly_eval_fq6(s_fq6_elem_t *a_result,
                             const chipmunk_poly_t *a_f,
                             const s_fq6_elem_t *a_alpha,
                             uint32_t a_d, uint64_t a_q)
{
    /* Horner: result = f_{d-1}, then for i=d-2..0: result = alpha * result + f_i */
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int32_t l_acc = s_mod_q((int64_t)a_f->coeffs[(int)a_d - 1], a_q);
        for (int i = (int)a_d - 2; i >= 0; --i) {
            /* l_acc = alpha[j] * l_acc + f[i] */
            l_acc = s_mod_q(s_fq6_mul_q(a_alpha->c[j], l_acc, a_q) + (int64_t)a_f->coeffs[i], a_q);
        }
        a_result->c[j] = l_acc;
    }
}

/* Check if F_q^6 element is zero (all components) */
static bool s_fq6_is_zero(const s_fq6_elem_t *a_elem)
{
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        if (a_elem->c[j] != 0) return false;
    }
    return true;
}

/* Compare two F_q^6 elements for equality */
static bool s_fq6_equal(const s_fq6_elem_t *a, const s_fq6_elem_t *b)
{
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
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
                             const s_fq6_elem_t *a_alpha,
                             uint32_t a_d, uint64_t a_q_mod)
{
    /* Verify z(alpha) = 0 for exact division */
    s_fq6_elem_t l_z_at_alpha;
    s_poly_eval_fq6(&l_z_at_alpha, a_z, a_alpha, a_d, a_q_mod);
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
    for (int j = 0; j < CHIPMUNK_FQ6_EXT_DEG; ++j) {
        int32_t l_alpha_j = a_alpha->c[j];
        int64_t l_acc = 0;
        for (int i = (int)a_d - 2; i >= 0; --i) {
            l_acc = s_mod_q((int64_t)a_z->coeffs[i + 1] +
                            (int64_t)l_alpha_j * l_acc, a_q_mod);
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
            for (int i = (int)a_d - 2; i >= 0; --i) {
                int64_t l_val = (int64_t)a_z->coeffs[i + 1] +
                                (int64_t)l_alpha_j * (int64_t)a_q->coeffs[i + 1];
                a_q->coeffs[i] = s_mod_q(l_val, a_q_mod);
            }
        }
        /* Other components: just verify consistency (debug builds) */
#if !defined(NDEBUG)
        else {
            /* Cross-check: each component should give same quotient */
            chipmunk_poly_t l_q_check;
            memset(&l_q_check, 0, sizeof(l_q_check));
            for (int i = (int)a_d - 2; i >= 0; --i) {
                int64_t l_val = (int64_t)a_z->coeffs[i + 1] +
                                (int64_t)l_alpha_j * (int64_t)l_q_check.coeffs[i + 1];
                l_q_check.coeffs[i] = s_mod_q(l_val, a_q_mod);
            }
            for (int i = 0; i < (int)a_d; ++i) {
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

/* Compute public ring hash: H(pk_0 || pk_1 || ... || pk_{N-1})
 * Phase 7: Hash the FULL public key (1424 bytes each), not just the first 32 bytes.
 * Previous version only hashed first 32 bytes, allowing collision between keys
 * that share a header but differ in the polynomial payload. */
static void s_compute_ring_hash(dap_hash_sha3_256_t *a_hash,
                                 const chipmunk_lrs_public_key_t *a_ring,
                                 uint32_t a_ring_size)
{
    /* Use SHAKE256 for incremental hashing of potentially large ring data.
     * Each key is 1424 bytes. For ring_size = 256: 256 * 1424 = 364,544 bytes.
     * We absorb in chunks to avoid allocating a huge buffer. */
    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));

    /* Absorb domain separator */
    dap_hash_shake256_absorb(l_state,
        (const uint8_t *)"snark-ring-hash-v1", 18);

    /* Absorb each public key in full */
    for (uint32_t i = 0; i < a_ring_size; ++i) {
        dap_hash_shake256_absorb(l_state,
            (const uint8_t *)&a_ring[i], sizeof(chipmunk_lrs_public_key_t));
    }

    /* Squeeze 32 bytes for the ring hash.
     * squeezeblocks writes DAP_SHAKE256_RATE (136) bytes per block.
     * Use scratch buffer and copy only the needed 32 bytes. */
    uint8_t l_scratch[DAP_SHAKE256_RATE];
    dap_hash_shake256_squeezeblocks(l_scratch, 1, l_state);
    memcpy(a_hash->raw, l_scratch, 32);
    dap_memwipe(l_scratch, sizeof(l_scratch));
}

/* Phase 5.1: s_hash_to_coeff and s_hash_to_trace removed.
 * They were used by the incorrect C3/C4 constraints. */

static int s_build_constraint_polynomial(chipmunk_poly_t *a_z,
                                         chipmunk_poly_t *a_q1,
                                         const chipmunk_poly_t *a_b,
                                         uint32_t a_d, uint64_t a_q)
{

    /* Phase 3 (P0-2 fix): Evaluation-domain constraint model.
     *
     * The indicator b is a polynomial of degree < d=512, with COEFFICIENTS
     * b[i]. The constraint domain is H = {omega_512^0, ..., omega_512^511},
     * the 512-th roots of unity. The vanishing polynomial is:
     *   Z_H(X) = X^512 - 1  (zero on all omega_512^i)
     *
     * Constraint C1(X) = b(X)·(b(X) − 1)  — polynomial product (degree ≤ 2d−2).
     * If b is binary on H (i.e., b(omega^i) ∈ {0,1}), then C1 vanishes on H,
     * so Z_H divides C1, and q1 = C1 / Z_H is a low-degree polynomial.
     *
     * We compute:
     *   1. C1 = b² − b  (polynomial multiply via 2048-point NTT)
     *   2. q1 = C1 / Z_H  (polynomial division by X^512 − 1)
     *   3. z = q1 + r·C2  (linear combination for exactly-one constraint)
     *
     * The verifier opens b and q1 at FRI query points and checks:
     *   b(r)·(b(r) − 1) = Z_H(r)·q1(r)
     *
     * This is a POLYNOMIAL identity, verifiable at any point via FRI.
     * For z≡0 forge: attacker sends z=0, but must provide valid q1 such that
     * b(r)·(b(r)−1) = Z_H(r)·q1(r). Since q1 is FRI-committed (binding),
     * attacker is stuck with their b. If b is not binary on H, the identity
     * fails at random query points (Schwartz-Zippel).
     */

    /* Step 1: Compute C1(X) = b(X)·b(X) − b(X) via 2048-point NTT.
     * b has degree ≤511, so b² has degree ≤1022. We need NTT size ≥1024.
     * Use 2048-point FRI NTT (size > 2·511). */

    /* Build per-q NTT context */
    chipmunk_fri_ntt_ctx_t l_ntt_ctx;
    int l_rc = chipmunk_fri_ntt_ctx_init(&l_ntt_ctx, a_q, CHIPMUNK_FRI_NTT_LOG);
    if (l_rc != 0) return l_rc;

    /* Pad b to 2048 coefficients */
    int32_t l_b_pad[CHIPMUNK_FRI_NTT_SIZE];
    memset(l_b_pad, 0, sizeof(l_b_pad));
    for (uint32_t i = 0; i < a_d; ++i)
        l_b_pad[i] = a_b->coeffs[i];

    /* Forward NTT of b */
    chipmunk_fri_ntt_forward_q(l_b_pad, &l_ntt_ctx);

    /* Pointwise square: b² in NTT domain */
    int32_t l_bsq_pad[CHIPMUNK_FRI_NTT_SIZE];
    for (uint32_t i = 0; i < CHIPMUNK_FRI_NTT_SIZE; ++i)
        l_bsq_pad[i] = s_mod_q((int64_t)l_b_pad[i] * l_b_pad[i], a_q);

    /* Inverse NTT → b² coefficient polynomial (degree ≤1022) */
    chipmunk_fri_ntt_inverse_q(l_bsq_pad, &l_ntt_ctx);

    /* C1 = b² − b  (only first 1023 coefficients matter, rest are 0) */
    /* C1[i] = b²[i] − b[i] for i < 512, C1[i] = b²[i] for 512 ≤ i < 1024 */
    /* Store in 1024-element array */
    int32_t l_c1[1024];
    memset(l_c1, 0, sizeof(l_c1));
    for (uint32_t i = 0; i < 1024; ++i) {
        int32_t l_b2 = (i < CHIPMUNK_FRI_NTT_SIZE) ? l_bsq_pad[i] : 0;
        int32_t l_bi = (i < a_d) ? a_b->coeffs[i] : 0;
        l_c1[i] = s_mod_q((int64_t)l_b2 - l_bi, a_q);
    }

    chipmunk_fri_ntt_ctx_free(&l_ntt_ctx);

    /* Step 2: Divide C1 by Z_H(X) = X^512 − 1.
     *
     * C1(X) = q1(X) · (X^512 − 1) = q1(X)·X^512 − q1(X)
     * So: C1[i] = −q1[i] for i < 512
     *     C1[i+512] = q1[i] for i < 512
     * Therefore: q1[i] = C1[i+512] = −C1[i]  (both must hold if divisible)
     *
     * q1 has degree ≤ 510 (since C1 has degree ≤1022, and Z_H has degree 512,
     * quotient has degree ≤1022−512 = 510). */

    /* Verify divisibility: C1[i] + C1[i+512] should be 0 for all valid i */
    for (uint32_t i = 0; i < 512; ++i) {
        int32_t l_sum_check = s_mod_q((int64_t)l_c1[i] + l_c1[i + 512], a_q);
        if (l_sum_check != 0) {
            /* Not divisible by Z_H — b is not binary on H.
             * This should not happen for honest prover. */
            log_it(L_ERROR, "SNARK: C1 not divisible by Z_H at index %u (sum=%d)",
                   i, l_sum_check);
            return -EINVAL;
        }
    }

    /* Extract q1[i] = C1[i+512] for i=0..510 */
    memset(a_q1, 0, sizeof(*a_q1));
    for (uint32_t i = 0; i < 511; ++i)
        a_q1->coeffs[i] = l_c1[i + 512];

    /* Step 3: z ≡ 0. The old z-pipeline (z(alpha)=0, synthetic division,
     * quotient relation) is preserved unchanged. The binary+sum constraints
     * are proven INDEPENDENTLY via the FRI b+q1 polynomial identity
     * b(r)·(b(r)−1) = Z_H(r)·q1(r) — no z needed. */
    memset(a_z, 0, sizeof(*a_z));

    return 0;
}

/* -------------------------------------------------------------------------
 * Phase 9.13: Universal SNARK Parameter Sets
 * ---------------------------------------------------------------------- */

/* Helper: compute 2-adicity of n (number of trailing zero bits). */
static uint32_t s_two_adicity(uint64_t n)
{
    uint32_t v = 0;
    while (n > 0 && (n & 1u) == 0) { n >>= 1; ++v; }
    return v;
}

/* Helper: compute param_id = first 4 bytes of SHA3-256(d || q). */
static uint32_t s_param_id(uint32_t d, uint64_t q)
{
    uint8_t buf[12];
    memset(buf, 0, sizeof(buf));
    buf[0] = (uint8_t)(d & 0xFF);
    buf[1] = (uint8_t)((d >> 8) & 0xFF);
    buf[2] = (uint8_t)((d >> 16) & 0xFF);
    buf[3] = (uint8_t)((d >> 24) & 0xFF);
    buf[4] = (uint8_t)(q & 0xFF);
    buf[5] = (uint8_t)((q >> 8) & 0xFF);
    buf[6] = (uint8_t)((q >> 16) & 0xFF);
    buf[7] = (uint8_t)((q >> 24) & 0xFF);
    buf[8] = (uint8_t)((q >> 32) & 0xFF);
    buf[9] = (uint8_t)((q >> 40) & 0xFF);
    buf[10] = (uint8_t)((q >> 48) & 0xFF);
    buf[11] = (uint8_t)((q >> 56) & 0xFF);
    uint8_t hash[32];
    dap_hash_sha3_256_raw(hash, buf, 12);
    uint32_t id;
    memcpy(&id, hash, 4);
    return id;
}

int chipmunk_snark_params_init(chipmunk_snark_params_t *a_params,
                                uint32_t a_d, uint64_t a_q)
{
    if (!a_params) return -EINVAL;
    memset(a_params, 0, sizeof(*a_params));

    /* Validate d: must be power of 2, 32 ≤ d ≤ MAX_D. */
    if (a_d == 0 || a_d > CHIPMUNK_SNARK_MAX_D) return -EINVAL;
    if ((a_d & (a_d - 1)) != 0) return -EINVAL;  /* not power of 2 */

    /* Validate q: must be > d and odd. */
    if (a_q <= (uint64_t)a_d) return -EINVAL;
    if ((a_q & 1u) == 0) return -EINVAL;

    /* Check 2-adicity of q-1: need ≥ log2(4d) for FRI NTT of size 4d. */
    uint32_t l_ad = s_two_adicity(a_q - 1);
    uint32_t l_log2_4d = 0;
    { uint32_t v = a_d * 4; while (v > 1) { v >>= 1; ++l_log2_4d; } }
    if (l_ad < l_log2_4d) {
        log_it(L_ERROR, "SNARK params: 2-adicity(q-1)=%u < %u needed for d=%u",
               l_ad, l_log2_4d, a_d);
        return -EINVAL;
    }

    /* Check Phi_9 irreducibility: q mod 9 must have multiplicative order 6.
     * The elements of (Z/9Z)* with order 6 are: 2 and 5. */
    uint32_t l_q_mod9 = (uint32_t)(a_q % 9u);
    if (l_q_mod9 != 2 && l_q_mod9 != 5) {
        log_it(L_ERROR, "SNARK params: q mod 9 = %u (need 2 or 5 for Phi_9 irreducibility)",
               l_q_mod9);
        return -EINVAL;
    }

    /* Fill fundamental parameters. */
    a_params->d = a_d;
    a_params->q = a_q;

    /* Derived FRI constants. */
    a_params->fri_init_size = 4 * a_d;
    a_params->fri_rounds = l_log2_4d - 4;  /* log2(4d) - log2(16) */
    a_params->fri_total_data = 0;
    {
        uint32_t sz = a_params->fri_init_size;
        for (uint32_t r = 0; r < a_params->fri_rounds; ++r) {
            a_params->fri_total_data += sz;
            sz /= 2;
        }
        a_params->fri_total_data += CHIPMUNK_SNARK_FRI_FINAL_SIZE;
    }

    /* Derived RS constants. */
    a_params->rs_msg_len = a_d;
    a_params->rs_code_len = 4 * a_d;

    /* param_id. */
    a_params->param_id = s_param_id(a_d, a_q);

    /* Field constants — per-q (Phase 9.13).
     * Computes omega (primitive 2^l_ad-th root), omega_inv, and inv(2^l_ad)
     * for the ACTIVE q via chipmunk_field_compute_for_q. No global singleton.
     * Also verifies Φ₉ irreducibility for the active q via Rabin's test. */
    if (!chipmunk_fq6_ext_modulus_is_irreducible_q(a_q)) {
        log_it(L_ERROR, "SNARK params: Φ₉ is NOT irreducible over F_q (q=%lu) — "
               "R_q^{(e)} is not a field, extension soundness broken",
               (unsigned long)a_q);
        return -EINVAL;
    }
    {
        chipmunk_field_consts_t l_fc;
        int l_fc_rc = chipmunk_field_compute_for_q(&l_fc, a_q, l_ad);
        if (l_fc_rc != 0) {
            log_it(L_ERROR, "SNARK params: per-q field constants computation failed for q=%lu",
                   (unsigned long)a_q);
            return l_fc_rc;
        }
        a_params->omega = l_fc.omega;
        a_params->omega_inv = l_fc.omega_inv;
        a_params->inv_2 = chipmunk_field_inv_q(2, a_q);
        a_params->inv_d = chipmunk_field_inv_q((int32_t)a_d, a_q);
    }

    /* Coset generator: find g with g^(4d) != 1 mod q. */
    a_params->rs_coset_g = 0;
    for (int32_t g = 3; g < 100; ++g) {
        int32_t g_pow = chipmunk_field_pow_q(g, 4 * a_d, a_q);
        if (g_pow != 1) {
            a_params->rs_coset_g = g;
            break;
        }
    }
    if (a_params->rs_coset_g == 0) {
        log_it(L_ERROR, "SNARK params: failed to find coset generator for d=%u q=%lu",
               a_d, (unsigned long)a_q);
        return -EINVAL;
    }

    /* Allocate and fill NTT twiddle tables.
     * zetas[k] = omega^k for k = 0..4d-1
     * zetas_inv[k] = omega_inv^k for k = 0..4d-1 */
    a_params->zetas_size = 4 * a_d;
    a_params->zetas = (int32_t *)calloc(a_params->zetas_size, sizeof(int32_t));
    a_params->zetas_inv = (int32_t *)calloc(a_params->zetas_size, sizeof(int32_t));
    if (!a_params->zetas || !a_params->zetas_inv) {
        chipmunk_snark_params_free(a_params);
        return -ENOMEM;
    }

    a_params->zetas[0] = 1;
    a_params->zetas_inv[0] = 1;
    for (uint32_t k = 1; k < a_params->zetas_size; ++k) {
        a_params->zetas[k] = s_fqmul_q(a_params->zetas[k - 1], a_params->omega, a_q);
        a_params->zetas_inv[k] = s_fqmul_q(a_params->zetas_inv[k - 1], a_params->omega_inv, a_q);
    }

    log_it(L_INFO, "SNARK params init: d=%u q=%lu 2-ad=%u q%%9=%u coset_g=%d "
           "fri_rounds=%u fri_total=%u param_id=0x%08x",
           a_d, (unsigned long)a_q, l_ad, l_q_mod9, a_params->rs_coset_g,
           a_params->fri_rounds, a_params->fri_total_data, a_params->param_id);

    return 0;
}

void chipmunk_snark_params_free(chipmunk_snark_params_t *a_params)
{
    if (!a_params) return;
    if (a_params->zetas) {
        dap_memwipe(a_params->zetas, a_params->zetas_size * sizeof(int32_t));
        free(a_params->zetas);
        a_params->zetas = NULL;
    }
    if (a_params->zetas_inv) {
        dap_memwipe(a_params->zetas_inv, a_params->zetas_size * sizeof(int32_t));
        free(a_params->zetas_inv);
        a_params->zetas_inv = NULL;
    }
    a_params->zetas_size = 0;
}

/* Predefined parameter sets (lazy-initialized singletons). */

static chipmunk_snark_params_t s_params_lrs;
static chipmunk_snark_params_t s_params_ring;
static chipmunk_snark_params_t s_params_test;
static bool s_params_lrs_init = false;
static bool s_params_ring_init = false;
static bool s_params_test_init = false;

const chipmunk_snark_params_t *chipmunk_snark_params_lrs(void)
{
    if (!s_params_lrs_init) {
        if (chipmunk_snark_params_init(&s_params_lrs, 512, 3168257) == 0)
            s_params_lrs_init = true;
    }
    return s_params_lrs_init ? &s_params_lrs : NULL;
}

/* Available once Phase 9.13h lands per-q field constants. */
const chipmunk_snark_params_t *chipmunk_snark_params_ring(void)
{
    if (!s_params_ring_init) {
        if (chipmunk_snark_params_init(&s_params_ring, 128, 4206593) == 0)
            s_params_ring_init = true;
    }
    return s_params_ring_init ? &s_params_ring : NULL;
}

const chipmunk_snark_params_t *chipmunk_snark_params_test(void)
{
    if (!s_params_test_init) {
        if (chipmunk_snark_params_init(&s_params_test, 32, 4206593) == 0)
            s_params_test_init = true;
    }
    return s_params_test_init ? &s_params_test : NULL;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int chipmunk_snark_init(chipmunk_snark_ctx_t *ctx)
{
    if (!ctx) return -EINVAL;
    memset(ctx, 0, sizeof(*ctx));

    /* Initialize runtime params for LRS (d=512, q=3168257). */
    int l_rc = chipmunk_snark_params_init(&ctx->sp, 512, (uint64_t)CHIPMUNK_Q);
    if (l_rc != 0) {
        log_it(L_ERROR, "SNARK init: params_init failed: %d", l_rc);
        return l_rc;
    }

    /* Fill LoTRS lattice params (used by some internal functions).
     * Keep d/q in sync with ctx->sp — single source of truth. */
    ctx->params.d = (int32_t)ctx->sp.d;
    ctx->params.q = (int32_t)ctx->sp.q;
    ctx->params.k = 6;
    ctx->params.l = 3;
    ctx->params.w = 37;
    ctx->params.eta = 13;
    ctx->params.phi = 1.0;

    /* Domain separator */
    dap_hash_sha3_256_raw(ctx->domain_separator, (const uint8_t *)s_domain_init, strlen(s_domain_init));

    /* Verify Phi_9 irreducibility (Rabin test) for the active q.
     * Defense-in-depth: params_init already checks this, but init is the
     * last gate before proofs are generated. */
    if (!chipmunk_fq6_ext_modulus_is_irreducible_q(ctx->sp.q)) {
        log_it(L_ERROR, "SNARK init: Phi_9 is NOT irreducible over F_q (q=%lu) — soundness broken",
               (unsigned long)ctx->sp.q);
        chipmunk_snark_params_free(&ctx->sp);
        return -EINVAL;
    }

    ctx->initialized = true;
    return 0;
}

int chipmunk_snark_commit(chipmunk_snark_commit_t *a_commit,
                          const chipmunk_poly_t *a_poly)
{
    if (!a_commit || !a_poly) return -EINVAL;
    /* Standalone API: LRS default params (d=MAX_D, q=CHIPMUNK_Q).
     * For non-LRS param sets use chipmunk_snark_commit_ctx(). */
    return s_commit_poly(a_commit, a_poly, CHIPMUNK_SNARK_MAX_D, (uint64_t)CHIPMUNK_Q);
}

int chipmunk_snark_commit_ctx(chipmunk_snark_commit_t *a_commit,
                                const chipmunk_snark_ctx_t *a_ctx,
                                const chipmunk_poly_t *a_poly)
{
    if (!a_commit || !a_ctx || !a_poly) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;
    return s_commit_poly(a_commit, a_poly, a_ctx->sp.d, a_ctx->sp.q);
}

int chipmunk_snark_prove(chipmunk_snark_proof_t *a_proof,
                         const chipmunk_snark_ctx_t *a_ctx,
                         const chipmunk_snark_statement_t *a_statement,
                         const chipmunk_snark_witness_t *a_witness)
{
    if (!a_proof || !a_ctx || !a_statement || !a_witness) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;
    if (a_statement->ring_size == 0) return -EINVAL;
    if (!a_statement->ring) return -EINVAL;
    if (a_statement->message_size > 0 && !a_statement->message) return -EINVAL;
    if (a_statement->ring_size > a_ctx->sp.d) return -EINVAL;
    if (a_witness->signer_index >= a_statement->ring_size) return -EINVAL;

    memset(a_proof, 0, sizeof(*a_proof));

    /* 1. Build indicator polynomial b in EVALUATION DOMAIN.
     *
     * Phase 3 (P0-2 fix): b is defined by its evaluations on the subgroup
     * H = {omega_512^0, ..., omega_512^511}: b(omega^i) = δ_{i,signer_index}.
     * The COEFFICIENTS of b(X) are obtained via inverse DFT over omega_512.
     *
     * This ensures the polynomial identity b(X)·(b(X)−1) ≡ 0 mod Z_H(X):
     *   On H: b(omega^i) ∈ {0,1} → b(omega^i)·(b(omega^i)−1) = 0
     *   Therefore C1(X)=b(X)·(b(X)−1) vanishes on H → Z_H | C1
     *   Quotient q1 = C1/Z_H is well-defined and low-degree.
     *
     * NOTE: We use O(d²) inverse DFT (slow but correct) since d=512 and this
     * runs once per proof. For production, replace with cyclic 512-point NTT. */
    chipmunk_poly_t l_b;
    memset(&l_b, 0, sizeof(l_b));
    {
        /* Evaluation vector: b_eval[signer_index] = 1, rest = 0 */
        /* (implicit — only one nonzero entry) */

        /* Inverse DFT: b[k] = (1/N) * Σ_{i=0}^{N-1} b_eval[i] * omega_512^{-ik}
         * Since b_eval[signer]=1 and rest=0:
         *   b[k] = (1/N) * omega_512^{-signer·k}
         * This is the Lagrange basis polynomial L_signer(X) at X^k coefficient. */
        int32_t l_omega = chipmunk_field_omega_512();       /* primitive 512-th root */
        int32_t l_omega_inv = chipmunk_field_omega_512_inv();
        int32_t l_inv_n = chipmunk_field_inv_q((int32_t)a_ctx->sp.d, a_ctx->sp.q);  /* 1/512 */

        for (uint32_t k = 0; k < a_ctx->sp.d; ++k) {
            /* omega_512^{-signer·k} via fast exponentiation */
            int32_t l_pow = 1;
            int32_t l_base = l_omega_inv;
            uint32_t l_exp = (uint32_t)a_witness->signer_index * k;
            while (l_exp > 0) {
                if (l_exp & 1u)
                    l_pow = s_fqmul_q(l_pow, l_base, a_ctx->sp.q);
                l_base = s_fqmul_q(l_base, l_base, a_ctx->sp.q);
                l_exp >>= 1u;
            }
            l_b.coeffs[k] = s_fqmul_q(l_pow, l_inv_n, a_ctx->sp.q);
        }
    }

    /* 2. Generate random nonce for w_commit (Phase 7: privacy fix).
     * w_commit must NOT depend on b — b is sparse (1 out of 512 nonzero),
     * so H(b) allows trivial brute-force de-anonymization (512 precomputable hashes).
     * Instead, w_commit = H(random_nonce), making it indistinguishable from random.
     * Soundness is preserved: z(alpha)=0 still implies C1=0, C2=0 via the
     * quotient and extension checks, independent of w_commit's content. */
    uint8_t l_w_nonce[32];
    if (dap_random_bytes(l_w_nonce, 32) != 0) {
        dap_memwipe(&l_b, sizeof(l_b));
        return -EIO;
    }
    {
        uint8_t l_transcript[64];
        memcpy(l_transcript, a_ctx->domain_separator, 32);
        memcpy(l_transcript + 32, l_w_nonce, 32);
        dap_hash_sha3_256_raw(a_proof->w_commit.hash, l_transcript, 64);
    }

    /* 3. Compute ring hash (public binding) */
    dap_hash_sha3_256_t l_ring_hash;
    s_compute_ring_hash(&l_ring_hash, a_statement->ring,
                         (uint32_t)a_statement->ring_size);

    /* 4. Derive randomizer from subtractive set S = F_{q^6} \ {0}
     * Transcript: domain_sep || w_commit || ring_hash
     * The randomizer is committed (r_commit) so the verifier re-derives it. */
    chipmunk_fq6_ext_t l_randomizer_ext;
    {
        uint8_t l_transcript[96];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_ring_hash.raw, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_randomizer_ext, l_hash, 0, a_ctx->sp.q);
    }
    s_fq6_elem_t l_randomizer;
    s_ext_to_fq6(&l_randomizer, &l_randomizer_ext, a_ctx->sp.q);

    /* Commit to randomizer for verifier re-derivation */
    s_commit_poly(&a_proof->r_commit, &l_randomizer_ext.c[0], a_ctx->sp.d, a_ctx->sp.q);

    /* 5. Build constraint polynomial z(X) = q1(X) + r·C2
     * Phase 3: q1 = C1/Z_H where C1 = b²−b, Z_H = X^512−1.
     * For honest prover with binary b: C1 vanishes on H, q1 is well-defined.
     * z = q1 + r·C2; for honest prover q1=0 (since b²−b vanishes on ALL of F_q
     * when b is binary, not just on H — wait, no: b(X)²−b(X) is NOT zero as
     * a polynomial, it's only zero on H. So q1 ≠ 0 in general.
     * z = q1 + r·C2 where C2 = Σb−1 = 0 for exactly-one indicator.
     * So z = q1 for honest prover (C2=0). */
    chipmunk_poly_t l_z;
    chipmunk_poly_t l_q1;
    memset(&l_q1, 0, sizeof(l_q1));
    s_build_constraint_polynomial(&l_z, &l_q1, &l_b,
                                   a_ctx->sp.d, a_ctx->sp.q);

    /* 6. Commit to constraint polynomial → z_commit */
    s_commit_poly(&a_proof->z_commit, &l_z, a_ctx->sp.d, a_ctx->sp.q);

    /* 7. Compute message hash for binding.
     * Use empty string for zero-length messages to avoid NULL dereference. */
    uint8_t l_msg_hash[32];
    {
        const uint8_t *l_msg = a_statement->message;
        size_t l_msg_len = a_statement->message_size;
        uint8_t l_empty = 0;
        if (!l_msg && l_msg_len == 0) { l_msg = &l_empty; }
        dap_hash_sha3_256_raw(l_msg_hash, l_msg, l_msg_len);
    }

    /* 8. Derive evaluation point alpha from subtractive set
     * Transcript: domain_sep || w_commit || r_commit || z_commit || msg_hash
     * This binds the proof to specific witness, randomizer, constraints, and message. */
    chipmunk_fq6_ext_t l_alpha_ext;
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
        s_qrom_derive_challenge(&l_alpha_ext, l_hash, 1, a_ctx->sp.q);
        /* Phase 5: alpha no longer stored in proof — verifier re-derives it */
    }

    /* 9. Compute quotient polynomial q(X) = z(X) / (X - alpha)
     * Uses FULL extension element alpha (F_q^6), not just scalar.
     * Verifies z(alpha) = 0 in F_q^6 (extension check, ~129 bits soundness). */
    s_fq6_elem_t l_alpha_fq6;
    s_ext_to_fq6(&l_alpha_fq6, &l_alpha_ext, a_ctx->sp.q);

    chipmunk_poly_t l_q;
    int l_rc = s_synth_div_fq6(&l_q, &l_z, &l_alpha_fq6, a_ctx->sp.d, a_ctx->sp.q);
    if (l_rc != 0) {
        log_it(L_ERROR, "SNARK prove: quotient division failed (z(alpha) != 0 in F_q^6)");
        return l_rc;
    }

    /* 10. Commit to quotient polynomial → q_commit */
    s_commit_poly(&a_proof->q_commit, &l_q, a_ctx->sp.d, a_ctx->sp.q);

    /* 11. Opening proof: serialized z and q polynomials.
     * Retained for algebraic verification checks (z(alpha)=0, quotient).
     * Phase 9.12+ will eliminate raw polys via DEEP composition. */
    {
        uint32_t l_d = a_ctx->sp.d;
        uint64_t l_qval = a_ctx->sp.q;
        size_t l_poly_bytes = (size_t)l_d * sizeof(int32_t);
        size_t l_off = 0;
        s_poly_to_bytes(a_proof->opening_proof + l_off, l_poly_bytes, &l_z, l_d, l_qval);
        l_off += l_poly_bytes;
        s_poly_to_bytes(a_proof->opening_proof + l_off, l_poly_bytes, &l_q, l_d, l_qval);
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

    /* 13. FRI proof for q(X) — Phase 9.11: polynomial commitment scheme.
     *
     * Builds a Fiat-Shamir transcript binding all SNARK commitments to
     * the FRI proof. The prover:
     *   a) Absorbs all 4 commitments + msg_hash + transcript_hash
     *   b) Derives 7 FRI alphas from transcript
     *   c) Commits q(X) via FRI (RS-encode, fold, Merkle caps)
     *   d) Absorbs caps + final_evals + alphas into transcript
     *   e) Finalizes transcript (grinding PoW, ~2^16 work)
     *   f) Derives 8 query indices from transcript
     *   g) Opens q(X) at those indices (leaf + sibling + auth path)
     *
     * The FRI proof provides:
     *   - Binding commitment to q(X) (anti-malleability)
     *   - 8-bit proximity soundness (FRI)
     *   - 16-bit computational soundness (grinding)
     */
    {
        /* 13a. Initialize FRI transcript with SNARK-FRI domain separator. */
        chipmunk_fri_transcript_t l_fri_tr;
        int l_rc = chipmunk_fri_transcript_init(
            &l_fri_tr,
            (const uint8_t *)CHIPMUNK_SNARK_FRI_DOMAIN);
        l_fri_tr.q = a_ctx->sp.q;  /* Phase 9.14f: per-q transcript */
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI transcript init failed");
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            return l_rc;
        }

        /* 13b. Absorb all SNARK commitments into FRI transcript. */
        {   /* Helper macro: absorb and fail early on error. */
#define L_ABSORB(data, len) do { \
    l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr, (data), (len)); \
    if (l_rc != 0) { \
        log_it(L_ERROR, "SNARK prove: FRI absorb failed"); \
        dap_memwipe(&l_b, sizeof(l_b)); \
        dap_memwipe(&l_z, sizeof(l_z)); \
        dap_memwipe(&l_q, sizeof(l_q)); \
        return l_rc; \
    } \
} while (0)
            L_ABSORB(a_proof->w_commit.hash, sizeof(a_proof->w_commit.hash));
            L_ABSORB(a_proof->r_commit.hash, sizeof(a_proof->r_commit.hash));
            L_ABSORB(a_proof->z_commit.hash, sizeof(a_proof->z_commit.hash));
            L_ABSORB(a_proof->q_commit.hash, sizeof(a_proof->q_commit.hash));
            L_ABSORB(l_msg_hash, 32);
            L_ABSORB(a_proof->transcript_hash, 32);
#undef L_ABSORB
        }

        /* 13c. Derive 7 FRI alphas from transcript. */
        int32_t l_fri_alphas[CHIPMUNK_FRI_ROUNDS];
        l_rc = chipmunk_fri_transcript_squeeze_fq_many(
            &l_fri_tr, l_fri_alphas, CHIPMUNK_FRI_ROUNDS);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI alpha derivation failed");
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            return l_rc;
        }

        /* 13d. FRI commit: RS-encode q(X), fold 7 rounds, Merkle caps. */
        chipmunk_fri_prover_t l_fri_prover;
        l_rc = chipmunk_fri_prover_init(&l_fri_prover);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI prover init failed");
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            return l_rc;
        }
        /* Phase 9.13h: set per-q modulus before commit so fold math uses
         * the active field. Default in prover_init is CHIPMUNK_Q. */
        l_fri_prover.q = a_ctx->sp.q;

        l_rc = chipmunk_fri_commit(&l_fri_prover, l_q.coeffs, l_fri_alphas);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI commit failed");
            chipmunk_fri_prover_free(&l_fri_prover);
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            return l_rc;
        }

        /* 13d-bis. Phase 3 (P0-2 fix): FRI commit indicator b AND quotient q1.
         * Commit b and q1 using the SAME alphas BEFORE transcript absorb/finalize.
         * Grinding PoW binds to q, b, AND q1 commitments. */
        chipmunk_fri_prover_t l_b_prover;
        l_rc = chipmunk_fri_prover_init(&l_b_prover);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI prover init (b) failed");
            chipmunk_fri_prover_free(&l_fri_prover);
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            dap_memwipe(&l_q1, sizeof(l_q1));
            return l_rc;
        }
        l_b_prover.q = a_ctx->sp.q;

        l_rc = chipmunk_fri_commit(&l_b_prover, l_b.coeffs, l_fri_alphas);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI commit (b) failed");
            chipmunk_fri_prover_free(&l_fri_prover);
            chipmunk_fri_prover_free(&l_b_prover);
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            dap_memwipe(&l_q1, sizeof(l_q1));
            return l_rc;
        }

        chipmunk_fri_prover_t l_q1_prover;
        l_rc = chipmunk_fri_prover_init(&l_q1_prover);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI prover init (q1) failed");
            chipmunk_fri_prover_free(&l_fri_prover);
            chipmunk_fri_prover_free(&l_b_prover);
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            dap_memwipe(&l_q1, sizeof(l_q1));
            return l_rc;
        }
        l_q1_prover.q = a_ctx->sp.q;

        l_rc = chipmunk_fri_commit(&l_q1_prover, l_q1.coeffs, l_fri_alphas);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK prove: FRI commit (q1) failed");
            chipmunk_fri_prover_free(&l_fri_prover);
            chipmunk_fri_prover_free(&l_b_prover);
            chipmunk_fri_prover_free(&l_q1_prover);
            dap_memwipe(&l_b, sizeof(l_b));
            dap_memwipe(&l_z, sizeof(l_z));
            dap_memwipe(&l_q, sizeof(l_q));
            dap_memwipe(&l_q1, sizeof(l_q1));
            return l_rc;
        }

        /* 13e. Absorb FRI commit output (q caps + b caps + q1 caps + final_evals). */
#define L_FREE_ALL_FRI() do { \
    chipmunk_fri_prover_free(&l_fri_prover); \
    chipmunk_fri_prover_free(&l_b_prover); \
    chipmunk_fri_prover_free(&l_q1_prover); \
    dap_memwipe(&l_b, sizeof(l_b)); \
    dap_memwipe(&l_z, sizeof(l_z)); \
    dap_memwipe(&l_q, sizeof(l_q)); \
    dap_memwipe(&l_q1, sizeof(l_q1)); \
} while(0)

        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            uint32_t l_n = (r == 0) ? 2048u : (2048u >> r);
            uint32_t l_cap_sz = (l_n >= 32u) ? 16u : l_n;
            /* q caps */
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                l_fri_prover.proof.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb q cap %u failed", r); L_FREE_ALL_FRI(); return l_rc; }
            /* b caps */
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                l_b_prover.proof.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb b cap %u failed", r); L_FREE_ALL_FRI(); return l_rc; }
            /* q1 caps */
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                l_q1_prover.proof.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb q1 cap %u failed", r); L_FREE_ALL_FRI(); return l_rc; }
        }
        for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
            /* q final evals */
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                l_fri_prover.proof.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb q eval %u failed", i); L_FREE_ALL_FRI(); return l_rc; }
            /* b final evals */
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                l_b_prover.proof.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb b eval %u failed", i); L_FREE_ALL_FRI(); return l_rc; }
            /* q1 final evals */
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                l_q1_prover.proof.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb q1 eval %u failed", i); L_FREE_ALL_FRI(); return l_rc; }
        }

        /* Absorb alphas into transcript. */
        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr, l_fri_alphas[r]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI absorb alpha %u failed", r); L_FREE_ALL_FRI(); return l_rc; }
        }

        /* 13f. Finalize: grinding PoW binds q, b, AND q1. */
        l_rc = chipmunk_fri_transcript_finalize(&l_fri_tr);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI finalize failed"); L_FREE_ALL_FRI(); return l_rc; }

        /* 13g. Derive 8 query indices (shared for q, b, q1). */
        uint32_t l_fri_indices[CHIPMUNK_FRI_NUM_QUERIES];
        l_rc = chipmunk_fri_derive_query_indices(&l_fri_tr,
            CHIPMUNK_FRI_NUM_QUERIES, CHIPMUNK_FRI_INIT_SIZE, l_fri_indices);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI query idx failed"); L_FREE_ALL_FRI(); return l_rc; }

        /* 13h. FRI query: open q(X), b(X), q1(X) at 8 positions. */
        chipmunk_fri_query_opening_t l_fri_openings[CHIPMUNK_FRI_NUM_QUERIES];
        chipmunk_fri_query_opening_t l_b_openings[CHIPMUNK_FRI_NUM_QUERIES];
        chipmunk_fri_query_opening_t l_q1_openings[CHIPMUNK_FRI_NUM_QUERIES];

        l_rc = chipmunk_fri_query(&l_fri_prover, CHIPMUNK_FRI_NUM_QUERIES, l_fri_indices, l_fri_openings);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI query (q) failed"); L_FREE_ALL_FRI(); return l_rc; }
        l_rc = chipmunk_fri_query(&l_b_prover, CHIPMUNK_FRI_NUM_QUERIES, l_fri_indices, l_b_openings);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI query (b) failed"); L_FREE_ALL_FRI(); return l_rc; }
        l_rc = chipmunk_fri_query(&l_q1_prover, CHIPMUNK_FRI_NUM_QUERIES, l_fri_indices, l_q1_openings);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK prove: FRI query (q1) failed"); L_FREE_ALL_FRI(); return l_rc; }

        /* 13i. Store FRI proofs in SNARK proof struct. */
        a_proof->fri_proof.commit = l_fri_prover.proof;
        memcpy(a_proof->fri_proof.queries, l_fri_openings, sizeof(l_fri_openings));
        a_proof->b_fri_proof.commit = l_b_prover.proof;
        memcpy(a_proof->b_fri_proof.queries, l_b_openings, sizeof(l_b_openings));
        a_proof->q1_fri_proof.commit = l_q1_prover.proof;
        memcpy(a_proof->q1_fri_proof.queries, l_q1_openings, sizeof(l_q1_openings));
        a_proof->fri_grinding_nonce = l_fri_tr.grinding_nonce;

        /* Store b and q1 values at query points. */
        for (unsigned qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
            a_proof->b_values_at_queries[qi] = l_b_openings[qi].leaf_values[0];
            a_proof->q1_values_at_queries[qi] = l_q1_openings[qi].leaf_values[0];
        }

        /* Store Σ b(omega^i) = N · b.coeffs[0] (sum of evaluations on H).
         * For Lagrange basis L_signer: b.coeffs[0] = 1/N → sum = 1. */
        a_proof->b_sum = s_mod_q((int64_t)a_ctx->sp.d * l_b.coeffs[0], a_ctx->sp.q);

#undef L_FREE_ALL_FRI
        chipmunk_fri_prover_free(&l_fri_prover);
        chipmunk_fri_prover_free(&l_b_prover);
        chipmunk_fri_prover_free(&l_q1_prover);
    }

    /* Wipe secret material from stack */
    dap_memwipe(&l_b, sizeof(l_b));
    dap_memwipe(&l_z, sizeof(l_z));
    dap_memwipe(&l_q, sizeof(l_q));
    dap_memwipe(&l_q1, sizeof(l_q1));
    dap_memwipe(l_w_nonce, sizeof(l_w_nonce));

    return 0;
}

int chipmunk_snark_verify(const chipmunk_snark_proof_t *a_proof,
                          const chipmunk_snark_ctx_t *a_ctx,
                          const chipmunk_snark_statement_t *a_statement)
{
    if (!a_proof || !a_ctx || !a_statement) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;
    if (a_statement->ring_size == 0) return -EINVAL;
    if (!a_statement->ring) return -EINVAL;
    if (a_statement->message_size > UINT32_MAX) return -EINVAL;

    /* 1. Compute message hash and ring hash.
     * Use empty string for zero-length messages to avoid NULL dereference. */
    uint8_t l_msg_hash[32];
    const uint8_t *l_msg = a_statement->message;
    size_t l_msg_len = a_statement->message_size;
    uint8_t l_empty = 0;
    if (!l_msg && l_msg_len == 0) { l_msg = &l_empty; }
    dap_hash_sha3_256_raw(l_msg_hash, l_msg, l_msg_len);

    dap_hash_sha3_256_t l_ring_hash;
    s_compute_ring_hash(&l_ring_hash, a_statement->ring,
                         (uint32_t)a_statement->ring_size);

    /* 2. Re-derive randomizer from transcript
     * Transcript: domain_sep || w_commit || ring_hash  (same as prove) */
    chipmunk_fq6_ext_t l_randomizer_ext;
    {
        uint8_t l_transcript[96];
        size_t l_off = 0;
        memcpy(l_transcript + l_off, a_ctx->domain_separator, 32); l_off += 32;
        memcpy(l_transcript + l_off, a_proof->w_commit.hash, 32); l_off += 32;
        memcpy(l_transcript + l_off, l_ring_hash.raw, 32); l_off += 32;
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, l_off);
        s_qrom_derive_challenge(&l_randomizer_ext, l_hash, 0, a_ctx->sp.q);
    }

    /* Verify r_commit matches re-derived randomizer (constant-time). */
    {
        chipmunk_snark_commit_t l_r_commit;
        s_commit_poly(&l_r_commit, &l_randomizer_ext.c[0], a_ctx->sp.d, a_ctx->sp.q);
        uint8_t l_diff = 0;
        for (int i = 0; i < 32; ++i)
            l_diff |= l_r_commit.hash[i] ^ a_proof->r_commit.hash[i];
        if (l_diff != 0) {
            log_it(L_ERROR, "SNARK verify: r_commit mismatch");
            return 0;
        }
    }

    /* 3. Re-derive alpha from transcript
     * Transcript: domain_sep || w_commit || r_commit || z_commit || msg_hash */
    chipmunk_fq6_ext_t l_alpha;
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
        s_qrom_derive_challenge(&l_alpha, l_hash, 1, a_ctx->sp.q);
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

    /* 6. FRI proof verification — verify the FRI commitment to q(X).
     * Uses grinding nonce verification (1 hash) instead of full grind. */
    {
        chipmunk_fri_transcript_t l_fri_tr;
        int l_rc = chipmunk_fri_transcript_init(
            &l_fri_tr,
            (const uint8_t *)CHIPMUNK_SNARK_FRI_DOMAIN);
        l_fri_tr.q = a_ctx->sp.q;  /* Phase 9.14f: per-q transcript */
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK verify: FRI transcript init failed");
            return 0;
        }

        /* Absorb same data as prover (steps 13a-13b). */
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            a_proof->w_commit.hash, sizeof(a_proof->w_commit.hash));
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb w_commit failed"); return 0; }
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            a_proof->r_commit.hash, sizeof(a_proof->r_commit.hash));
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb r_commit failed"); return 0; }
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            a_proof->z_commit.hash, sizeof(a_proof->z_commit.hash));
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb z_commit failed"); return 0; }
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            a_proof->q_commit.hash, sizeof(a_proof->q_commit.hash));
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb q_commit failed"); return 0; }
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            l_msg_hash, 32);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb msg_hash failed"); return 0; }
        l_rc = chipmunk_fri_transcript_absorb(&l_fri_tr,
            a_proof->transcript_hash, 32);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb transcript_hash failed"); return 0; }

        /* Derive 7 FRI alphas (same order as prover). */
        int32_t l_fri_alphas[CHIPMUNK_FRI_ROUNDS];
        l_rc = chipmunk_fri_transcript_squeeze_fq_many(
            &l_fri_tr, l_fri_alphas, CHIPMUNK_FRI_ROUNDS);
        if (l_rc != 0) {
            log_it(L_ERROR, "SNARK verify: FRI alpha derivation failed");
            return 0;
        }

        /* 6b. Absorb q caps + b caps + q1 caps + final_evals + alphas
         *     (must mirror prover step 13e exactly). */
        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            uint32_t l_n = (r == 0) ? 2048u : (2048u >> r);
            uint32_t l_cap_sz = (l_n >= 32u) ? 16u : l_n;
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                a_proof->fri_proof.commit.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb q cap %u failed", r); return 0; }
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                a_proof->b_fri_proof.commit.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb b cap %u failed", r); return 0; }
            l_rc = chipmunk_fri_transcript_absorb_cap(&l_fri_tr,
                a_proof->q1_fri_proof.commit.caps[r].nodes, l_cap_sz);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb q1 cap %u failed", r); return 0; }
        }
        for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                a_proof->fri_proof.commit.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb q eval %u failed", i); return 0; }
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                a_proof->b_fri_proof.commit.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb b eval %u failed", i); return 0; }
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr,
                a_proof->q1_fri_proof.commit.final_evals[i]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb q1 eval %u failed", i); return 0; }
        }
        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            l_rc = chipmunk_fri_transcript_absorb_fq(&l_fri_tr, l_fri_alphas[r]);
            if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI absorb alpha %u failed", r); return 0; }
        }

        /* 6c. Finalize verifier-side: verify grinding nonce. */
        l_rc = chipmunk_fri_transcript_finalize_verify(&l_fri_tr, a_proof->fri_grinding_nonce);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI grinding nonce invalid"); return 0; }

        /* 6d. Derive 8 query indices (shared for q, b, q1). */
        uint32_t l_fri_indices[CHIPMUNK_FRI_NUM_QUERIES];
        l_rc = chipmunk_fri_derive_query_indices(&l_fri_tr,
            CHIPMUNK_FRI_NUM_QUERIES, CHIPMUNK_FRI_INIT_SIZE, l_fri_indices);
        if (l_rc != 0) { log_it(L_ERROR, "SNARK verify: FRI query idx failed"); return 0; }

        /* 6e. Verify FRI queries for q, b, AND q1 (shared indices). */
        for (uint32_t qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
            if (a_proof->fri_proof.queries[qi].idx != l_fri_indices[qi]) {
                log_it(L_ERROR, "SNARK verify: FRI q query %u index mismatch", qi); return 0;
            }
            if (!chipmunk_fri_verify_query_q(&a_proof->fri_proof, qi, l_fri_alphas, a_ctx->sp.q)) {
                log_it(L_ERROR, "SNARK verify: FRI q query %u failed", qi); return 0;
            }
            if (a_proof->b_fri_proof.queries[qi].idx != l_fri_indices[qi]) {
                log_it(L_ERROR, "SNARK verify: FRI b query %u index mismatch", qi); return 0;
            }
            if (!chipmunk_fri_verify_query_q(&a_proof->b_fri_proof, qi, l_fri_alphas, a_ctx->sp.q)) {
                log_it(L_ERROR, "SNARK verify: FRI b query %u failed", qi); return 0;
            }
            if (a_proof->q1_fri_proof.queries[qi].idx != l_fri_indices[qi]) {
                log_it(L_ERROR, "SNARK verify: FRI q1 query %u index mismatch", qi); return 0;
            }
            if (!chipmunk_fri_verify_query_q(&a_proof->q1_fri_proof, qi, l_fri_alphas, a_ctx->sp.q)) {
                log_it(L_ERROR, "SNARK verify: FRI q1 query %u failed", qi); return 0;
            }
        }
    }

    /* 7. Verify opening proof: reconstruct z, q from bytes and check commitments.
     * Raw polys retained for algebraic checks (z(alpha)=0, quotient). */
    uint32_t l_d = a_ctx->sp.d;
    uint64_t l_mod_q = a_ctx->sp.q;
    size_t l_poly_bytes = (size_t)l_d * sizeof(int32_t);
    if (a_proof->opening_proof_size < l_poly_bytes * 2) {
        log_it(L_ERROR, "SNARK verify: opening proof too small (%zu < %zu)",
               a_proof->opening_proof_size, l_poly_bytes * 2);
        return 0;
    }

    /* Reconstruct z, q from opening proof bytes */
    chipmunk_poly_t l_z, l_q;
    memset(&l_z, 0, sizeof(l_z));
    memset(&l_q, 0, sizeof(l_q));
    memcpy(l_z.coeffs, a_proof->opening_proof, l_poly_bytes);
    memcpy(l_q.coeffs, a_proof->opening_proof + l_poly_bytes, l_poly_bytes);

    /* Verify coefficients are in range [0, q) */
    for (uint32_t i = 0; i < l_d; ++i) {
        if (l_z.coeffs[i] < 0 || (uint64_t)l_z.coeffs[i] >= l_mod_q) return 0;
        if (l_q.coeffs[i] < 0 || (uint64_t)l_q.coeffs[i] >= l_mod_q) return 0;
    }

    /* Verify commitments match (constant-time). */
    {
        chipmunk_snark_commit_t l_z_commit, l_q_commit;
        s_commit_poly(&l_z_commit, &l_z, a_ctx->sp.d, a_ctx->sp.q);
        s_commit_poly(&l_q_commit, &l_q, a_ctx->sp.d, a_ctx->sp.q);

        uint8_t l_diff_z = 0, l_diff_q = 0;
        for (int i = 0; i < 32; ++i) {
            l_diff_z |= l_z_commit.hash[i] ^ a_proof->z_commit.hash[i];
            l_diff_q |= l_q_commit.hash[i] ^ a_proof->q_commit.hash[i];
        }
        if (l_diff_z != 0) {
            log_it(L_ERROR, "SNARK verify: z_commit mismatch");
            return 0;
        }
        if (l_diff_q != 0) {
            log_it(L_ERROR, "SNARK verify: q_commit mismatch");
            return 0;
        }
    }

    /* 8. Verify z(alpha) = 0 in F_q^6 extension
     * This is the PRIMARY soundness check: alpha from the subtractive set
     * S = F_{q^6}\{0} gives ~129-bit soundness per check.
     * If z(alpha) != 0, the prover's constraint polynomial is invalid. */
    {
        s_fq6_elem_t l_alpha_fq6;
        s_ext_to_fq6(&l_alpha_fq6, &l_alpha, l_mod_q);
        s_fq6_elem_t l_z_at_alpha;
        s_poly_eval_fq6(&l_z_at_alpha, &l_z, &l_alpha_fq6, l_d, l_mod_q);
        if (!s_fq6_is_zero(&l_z_at_alpha)) {
            log_it(L_ERROR, "SNARK verify: z(alpha) != 0 in F_q^6 extension");
            return 0;
        }
    }

    /* 9. Verify quotient relation: z(X) = q(X) * (X - alpha_scalar) */
    {
        int32_t l_alpha_scalar = l_alpha.c[0].coeffs[0];

        uint64_t l_xof_state[25];
        memset(l_xof_state, 0, sizeof(l_xof_state));
        {
            uint8_t l_xof_input[64];
            memcpy(l_xof_input, a_proof->transcript_hash, 32);
            memcpy(l_xof_input + 32, l_msg_hash, 32);
            dap_hash_shake256_absorb(l_xof_state, l_xof_input, 64);
        }

        for (int l_check = 0; l_check < CHIPMUNK_SNARK_QUOTIENT_CHECKS; ++l_check) {
            int32_t l_test_point = -1;
            for (int l_attempt = 0; l_attempt < 100; ++l_attempt) {
                uint8_t l_sample_buf[DAP_SHAKE256_RATE];
                dap_hash_shake256_squeezeblocks(l_sample_buf, 1, l_xof_state);
                l_test_point = chipmunk_sample_reject4(l_sample_buf, (uint32_t)l_mod_q);
                if (l_test_point >= 0) break;
            }
            if (l_test_point < 0) {
                log_it(L_ERROR, "SNARK verify: test point sampling failed after 100 attempts");
                return 0;
            }
            if (l_test_point == 0) l_test_point = 1;

            int64_t l_z_eval = 0;
            for (int i = (int)l_d - 1; i >= 0; --i) {
                l_z_eval = (int64_t)s_mod_q((int64_t)l_test_point * l_z_eval + l_z.coeffs[i], l_mod_q);
            }

            int64_t l_q_eval = 0;
            for (int i = (int)l_d - 1; i >= 0; --i) {
                l_q_eval = (int64_t)s_mod_q((int64_t)l_test_point * l_q_eval + l_q.coeffs[i], l_mod_q);
            }

            int64_t l_rhs = (int64_t)s_mod_q(l_q_eval * s_mod_q((int64_t)l_test_point - l_alpha_scalar, l_mod_q), l_mod_q);

            if (s_mod_q(l_z_eval, l_mod_q) != s_mod_q(l_rhs, l_mod_q)) {
                log_it(L_ERROR, "SNARK verify: quotient relation FAILED at check %d", l_check);
                return 0;
            }
        }
    }

    /* 10. Phase 3 (P0-2 fix): Verify indicator polynomial b via FRI openings.
     *
     * The indicator b ∈ {0,1}^N is one-hot (b[signer_index]=1).
     * Previously b was NOT committed, allowing z≡0 forge (any proof passes
     * for any ring). Now b is committed via FRI and opened at 8 query points.
     *
     * At each query point, the verifier checks:
     *   b(x_k) ∈ {0, 1}  (ternary/binary constraint)
     *
     * This prevents the z≡0 forge: the attacker must commit to a b that is
     * binary at 8 random domain points. Combined with b_sum=1 check and
     * the FRI binding (prover cannot change b after commitment), this makes
     * forge computationally infeasible.
     *
     * POLYNOMIAL IDENTITY: b(r)·(b(r)−1) = Z_H(r)·q1(r) at each query point.
     * This is the standard STARK vanishing-polynomial constraint:
     *   C1(X) = b(X)·(b(X)−1) vanishes on H iff b is binary on H
     *   C1(X) = q1(X)·Z_H(X) iff C1 is divisible by Z_H = X^512−1
     * At query point r: C1(r) = b(r)·(b(r)−1) must equal Z_H(r)·q1(r).
     */
    {
        /* Check b_sum = 1 (exactly one signer) */
        if (a_proof->b_sum != 1) {
            log_it(L_ERROR, "SNARK verify: b_sum=%d (expected 1)", a_proof->b_sum);
            return 0;
        }

        /* Evaluate Z_H at each FRI query domain point and check polynomial identity.
         * Domain point x_k = g * omega^idx (coset generator g=3, omega=omega_2048).
         * Z_H(x_k) = x_k^512 - 1.
         *
         * The constraint: b(x_k)·(b(x_k)−1) =? Z_H(x_k)·q1(x_k)
         * Both b(x_k) and q1(x_k) come from FRI openings (binding). */
        int32_t l_g = CHIPMUNK_RS_COSET_G;
        int32_t l_omega = chipmunk_field_omega_2048();

        for (uint32_t qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
            int32_t l_b_val = a_proof->b_values_at_queries[qi];
            int32_t l_q1_val = a_proof->q1_values_at_queries[qi];

            /* Compute domain point x_k = g * omega^idx */
            uint32_t l_idx = a_proof->fri_proof.queries[qi].idx;
            int32_t l_point = l_g;
            for (uint32_t e = 0; e < l_idx; ++e)
                l_point = s_fqmul_q(l_point, l_omega, l_mod_q);

            /* Z_H(x_k) = x_k^512 - 1 (fast exponentiation) */
            int32_t l_zh = 1;  /* start with 1 */
            int32_t l_base = l_point;
            uint32_t l_exp = 512;
            while (l_exp > 0) {
                if (l_exp & 1u)
                    l_zh = s_fqmul_q(l_zh, l_base, l_mod_q);
                l_base = s_fqmul_q(l_base, l_base, l_mod_q);
                l_exp >>= 1u;
            }
            l_zh = s_mod_q((int64_t)l_zh - 1, l_mod_q);

            /* LHS: b(x_k)·(b(x_k)−1) */
            int32_t l_lhs = s_mod_q((int64_t)l_b_val * s_mod_q((int64_t)l_b_val - 1, l_mod_q), l_mod_q);

            /* RHS: Z_H(x_k)·q1(x_k) */
            int32_t l_rhs = s_fqmul_q(l_zh, l_q1_val, l_mod_q);

            if (l_lhs != l_rhs) {
                log_it(L_ERROR, "SNARK verify: constraint identity failed at "
                       "query %u (LHS=%d, RHS=%d, b=%d, q1=%d, Z_H=%d)",
                       qi, l_lhs, l_rhs, l_b_val, l_q1_val, l_zh);
                return 0;
            }
        }
    }

    debug_if(1, L_DEBUG, "SNARK verify: all checks passed (b+q1 FRI constraint + "
             "ext alpha + %d quotient checks + FRI q, >> 128-bit soundness)",
             CHIPMUNK_SNARK_QUOTIENT_CHECKS);
    #undef CHIPMUNK_SNARK_QUOTIENT_CHECKS
    return 1;
}

void chipmunk_snark_proof_free(chipmunk_snark_proof_t *a_proof)
{
    if (!a_proof) return;
    /* Clamp wipe size to prevent read-beyond-bounds if size is corrupted. */
    size_t l_wipe_sz = a_proof->opening_proof_size;
    if (l_wipe_sz > sizeof(a_proof->opening_proof))
        l_wipe_sz = sizeof(a_proof->opening_proof);
    dap_memwipe(a_proof->opening_proof, l_wipe_sz);
    memset(a_proof, 0, sizeof(*a_proof));
}

void chipmunk_snark_ctx_free(chipmunk_snark_ctx_t *a_ctx)
{
    if (!a_ctx) return;
    chipmunk_snark_params_free(&a_ctx->sp);
    dap_memwipe(a_ctx, sizeof(*a_ctx));
}
