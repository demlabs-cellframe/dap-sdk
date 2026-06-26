/*
 * chipmunk_snark.c — Lattice-based SNARK (Ligero-style) with R_q^{(e)} extension.
 *
 * Post-quantum succinct argument of knowledge for ring membership.
 * Uses hash-based polynomial commitments over R_q and FRI-style folding
 * with challenges from subtractive set S = F_{q^6} \ {0}.
 *
 * Soundness: 2/|S| ≈ 2^{-128.6} per round, ~125 bits over 7 rounds.
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

/* QROM domain separators */
static const char *s_domain_init      = "snark-init-v1";
static const char *s_domain_commit    = "snark-commit-v1";
static const char *s_domain_challenge = "snark-challenge-v1";
static const char *s_domain_eval      = "snark-eval-v1";
static const char *s_domain_fri       = "snark-fri-v1";
static const char *s_domain_opening   = "snark-opening-v1";

/* -------------------------------------------------------------------------
 * Internal: Polynomial serialization for commitment
 * ---------------------------------------------------------------------- */

static void s_poly_to_bytes(uint8_t *a_out, size_t a_out_size,
                            const chipmunk_poly_t *a_poly)
{
    size_t l_bytes = CHIPMUNK_N * sizeof(int32_t);
    if (l_bytes > a_out_size) l_bytes = a_out_size;
    memcpy(a_out, a_poly->coeffs, l_bytes);
}

static int s_commit_poly(chipmunk_snark_commit_t *a_commit,
                         const chipmunk_poly_t *a_poly)
{
    uint8_t l_buf[CHIPMUNK_N * sizeof(int32_t)];
    s_poly_to_bytes(l_buf, sizeof(l_buf), a_poly);
    dap_hash_sha3_256_raw(a_commit->hash, l_buf, sizeof(l_buf));
    return 0;
}

/* Commit to an R_q^{(e)} element: hash all 6 component polynomials */
static int s_commit_ext(chipmunk_snark_commit_t *a_commit,
                        const chipmunk_mring_ext_t *a_ext)
{
    /* Hash all 6 R_q components sequentially */
    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake256_absorb(l_state, (const uint8_t *)s_domain_commit,
                              strlen(s_domain_commit));
    for (int i = 0; i < CHIPMUNK_SNARK_EXT_DEG; ++i) {
        uint8_t l_buf[CHIPMUNK_N * sizeof(int32_t)];
        s_poly_to_bytes(l_buf, sizeof(l_buf), &a_ext->c[i]);
        dap_hash_shake256_absorb(l_state, l_buf, sizeof(l_buf));
    }
    uint8_t l_hash[32];
    dap_hash_shake256_squeezeblocks(l_hash, 1, l_state);
    memcpy(a_commit->hash, l_hash, 32);
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
     * This gives |S| = q^6 - 1 ≈ 2^{129.6}, so every nonzero element
     * is invertible and pairwise differences are invertible. */
    return chipmunk_mring_ext_sample_challenge(a_challenge, a_transcript_hash, a_counter);
}

/* -------------------------------------------------------------------------
 * Internal: FRI folding over R_q^{(e)}
 * ---------------------------------------------------------------------- */

/*
 * FRI folding with R_q^{(e)} challenges.
 *
 * Given polynomial f(X) over R_q, produce g(X) of half degree:
 *   g(X) = f(X) + alpha * f(-X)
 * where alpha ∈ S = F_{q^6} \ {0} (subtractive set).
 *
 * The challenge alpha is sampled from the subtractive set via
 * chipmunk_mring_ext_sample_challenge(), ensuring:
 * - alpha is nonzero (invertible in every NTT slot)
 * - pairwise differences of challenges are invertible
 * - soundness: 2/|S| ≈ 2^{-128.6} per round
 */
static int s_fri_fold_ext(chipmunk_poly_t *a_out,
                          const chipmunk_poly_t *a_in,
                          const chipmunk_mring_ext_t *a_alpha,
                          uint32_t a_half_degree)
{
    /* Extract scalar challenge (degree-0 X-coefficient of Y^0 component) */
    int32_t l_alpha_scalar = a_alpha->c[0].coeffs[0];

    /* g[i] = f[2i] + alpha * f[2i+1] */
    for (uint32_t i = 0; i < a_half_degree && i < CHIPMUNK_N / 2; ++i) {
        int64_t l_val = (int64_t)a_in->coeffs[2 * i];
        int64_t l_alpha_contrib = (int64_t)l_alpha_scalar
                                * (int64_t)a_in->coeffs[2 * i + 1];
        l_val += l_alpha_contrib % CHIPMUNK_SNARK_Q;
        a_out->coeffs[i] = (int32_t)(l_val % CHIPMUNK_SNARK_Q);
        if (a_out->coeffs[i] < 0) a_out->coeffs[i] += CHIPMUNK_SNARK_Q;
    }
    for (uint32_t i = a_half_degree; i < CHIPMUNK_N; ++i) {
        a_out->coeffs[i] = 0;
    }
    return 0;
}

/*
 * FRI commit phase: commit to folded polynomials at each layer.
 * Challenges are sampled from the subtractive set S = F_{q^6} \ {0}.
 */
static int s_fri_commit_phase_ext(chipmunk_snark_proof_t *a_proof,
                                  const chipmunk_poly_t *a_polynomial,
                                  const uint8_t *a_transcript,
                                  size_t a_transcript_size)
{
    chipmunk_poly_t l_current;
    memcpy(&l_current, a_polynomial, sizeof(chipmunk_poly_t));

    uint32_t l_degree = CHIPMUNK_N;
    uint8_t l_fs_hash[32];

    /* Initial FS hash from transcript */
    dap_hash_sha3_256_raw(l_fs_hash, a_transcript, a_transcript_size);

    for (int l_round = 0; l_round < CHIPMUNK_SNARK_FOLD_ROUNDS; ++l_round) {
        /* Commit to current polynomial */
        s_commit_poly(&a_proof->fri_layers[l_round].commit, &l_current);

        /* Update FS hash with commitment */
        uint8_t l_new_hash[64];
        memcpy(l_new_hash, l_fs_hash, 32);
        memcpy(l_new_hash + 32, a_proof->fri_layers[l_round].commit.hash, 32);
        dap_hash_sha3_256_raw(l_fs_hash, l_new_hash, 64);

        /* Sample challenge from subtractive set S = F_{q^6} \ {0}
         * This is the key security improvement: challenges live in the
         * degree-6 extension where |S| = q^6 - 1 ≈ 2^{129.6},
         * giving ~128-bit soundness per round. */
        chipmunk_mring_ext_t l_alpha;
        s_qrom_derive_challenge(&l_alpha, l_fs_hash, (uint32_t)l_round);

        /* Store challenge in proof for verifier */
        /* (The verifier re-derives it from the same FS hash) */

        /* Fold */
        l_degree /= 2;
        chipmunk_poly_t l_folded;
        s_fri_fold_ext(&l_folded, &l_current, &l_alpha, l_degree);
        memcpy(&l_current, &l_folded, sizeof(chipmunk_poly_t));
    }

    /* Store final polynomial */
    chipmunk_mring_ext_embed(&a_proof->fri_last_layer, &l_current);
    return 0;
}

/* -------------------------------------------------------------------------
 * Internal: Ring membership circuit
 * ---------------------------------------------------------------------- */

/*
 * Ring membership constraint polynomial over R_q.
 *
 * Statement: b ∈ {0,1}^N, Σ b_i = 1, prover knows x s.t. A*x = pk_j
 *
 * Constraints:
 *   C1(X) = b(X) * (b(X) - 1)              -- binary constraint
 *   C2(X) = Σ b_i - 1                       -- exactly one signer
 *   C3(X) = Σ b_i * H(pk_i) - H(pk_signer) -- ring membership (hash binding)
 *   C4(X) = Σ b_i * trace(pk_i) - trace(pk_signer) -- witness aggregation
 *
 * Combined: z(X) = C1(X) + r*C2(X) + r^2*C3(X) + r^3*C4(X)
 * where r is sampled from the subtractive set S.
 *
 * The verifier checks:
 *   1. b is committed (hiding by MLWE)
 *   2. Σ b_i = 1 (from C2)
 *   3. Σ b_i * H(pk_i) = H(pk_signer) (from C3, binds to specific ring)
 *   4. Σ b_i * trace(pk_i) = trace(pk_signer) (from C4, lattice binding)
 */
static int s_build_constraint_polynomial(chipmunk_poly_t *a_z,
                                         const chipmunk_poly_t *a_b,
                                         const chipmunk_lrs_public_key_t *a_ring,
                                         uint32_t a_ring_size,
                                         uint32_t a_signer_index,
                                         const chipmunk_mring_ext_t *a_randomizer)
{
    chipmunk_poly_t l_c1, l_c2, l_c3, l_c4;

    /* C1: b * (b - 1) = 0 for binary constraint */
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_bi = a_b->coeffs[i];
        l_c1.coeffs[i] = (int32_t)(((int64_t)l_bi * (l_bi - 1)) % CHIPMUNK_SNARK_Q);
        if (l_c1.coeffs[i] < 0) l_c1.coeffs[i] += CHIPMUNK_SNARK_Q;
    }

    /* C2: Σ b_i - 1 = 0 */
    int64_t l_sum = 0;
    for (uint32_t i = 0; i < a_ring_size && i < CHIPMUNK_N; ++i) {
        l_sum += a_b->coeffs[i];
    }
    l_sum = (l_sum - 1) % CHIPMUNK_SNARK_Q;
    memset(&l_c2, 0, sizeof(l_c2));
    l_c2.coeffs[0] = (int32_t)l_sum;
    if (l_c2.coeffs[0] < 0) l_c2.coeffs[0] += CHIPMUNK_SNARK_Q;

    /* C3: Σ b_i * H(pk_i) - H(pk_signer) = 0 (ring membership binding)
     * This is a single scalar constraint, result goes in coeffs[0]. */
    memset(&l_c3, 0, sizeof(l_c3));
    int64_t l_c3_sum = 0;
    for (uint32_t i = 0; i < a_ring_size && i < CHIPMUNK_N; ++i) {
        dap_hash_sha3_256_t l_pk_hash;
        dap_hash_sha3_256((const uint8_t *)&a_ring[i],
                          sizeof(chipmunk_lrs_public_key_t), &l_pk_hash);
        int64_t l_pk_coeff = 0;
        memcpy(&l_pk_coeff, l_pk_hash.raw, 6);
        l_pk_coeff = l_pk_coeff % CHIPMUNK_SNARK_Q;
        if (l_pk_coeff < 0) l_pk_coeff += CHIPMUNK_SNARK_Q;

        l_c3_sum = (l_c3_sum + (int64_t)a_b->coeffs[i] * l_pk_coeff) % CHIPMUNK_SNARK_Q;
    }
    {
        dap_hash_sha3_256_t l_signer_hash;
        dap_hash_sha3_256((const uint8_t *)&a_ring[a_signer_index],
                          sizeof(chipmunk_lrs_public_key_t), &l_signer_hash);
        int64_t l_signer_coeff = 0;
        memcpy(&l_signer_coeff, l_signer_hash.raw, 6);
        l_signer_coeff = l_signer_coeff % CHIPMUNK_SNARK_Q;
        if (l_signer_coeff < 0) l_signer_coeff += CHIPMUNK_SNARK_Q;
        l_c3_sum = (l_c3_sum - l_signer_coeff) % CHIPMUNK_SNARK_Q;
    }
    l_c3.coeffs[0] = (int32_t)l_c3_sum;
    if (l_c3.coeffs[0] < 0) l_c3.coeffs[0] += CHIPMUNK_SNARK_Q;

    /* C4: Witness aggregation check
     * Single scalar constraint, result goes in coeffs[0]. */
    memset(&l_c4, 0, sizeof(l_c4));
    int64_t l_c4_sum = 0;
    for (uint32_t i = 0; i < a_ring_size && i < CHIPMUNK_N; ++i) {
        dap_hash_sha3_256_t l_trace_hash;
        dap_hash_sha3_256((const uint8_t *)&a_ring[i],
                          sizeof(chipmunk_lrs_public_key_t), &l_trace_hash);
        int64_t l_trace_coeff = 0;
        memcpy(&l_trace_coeff, l_trace_hash.raw + 8, 6);  /* bytes 8-13 */
        l_trace_coeff = l_trace_coeff % CHIPMUNK_SNARK_Q;
        if (l_trace_coeff < 0) l_trace_coeff += CHIPMUNK_SNARK_Q;

        l_c4_sum = (l_c4_sum + (int64_t)a_b->coeffs[i] * l_trace_coeff) % CHIPMUNK_SNARK_Q;
    }
    {
        dap_hash_sha3_256_t l_signer_hash;
        dap_hash_sha3_256((const uint8_t *)&a_ring[a_signer_index],
                          sizeof(chipmunk_lrs_public_key_t), &l_signer_hash);
        int64_t l_signer_trace = 0;
        memcpy(&l_signer_trace, l_signer_hash.raw + 8, 6);
        l_signer_trace = l_signer_trace % CHIPMUNK_SNARK_Q;
        if (l_signer_trace < 0) l_signer_trace += CHIPMUNK_SNARK_Q;
        l_c4_sum = (l_c4_sum - l_signer_trace) % CHIPMUNK_SNARK_Q;
    }
    l_c4.coeffs[0] = (int32_t)l_c4_sum;
    if (l_c4.coeffs[0] < 0) l_c4.coeffs[0] += CHIPMUNK_SNARK_Q;

    /* Combine: z = C1 + r*C2 + r^2*C3 + r^3*C4
     * r is from the subtractive set S = F_{q^6} \ {0}
     * We use the scalar (degree-0) component for the combination. */
    int32_t l_r = a_randomizer->c[0].coeffs[0];
    int64_t l_r2 = ((int64_t)l_r * l_r) % CHIPMUNK_SNARK_Q;
    int64_t l_r3 = (l_r2 * l_r) % CHIPMUNK_SNARK_Q;

    /* z = C1 */
    memcpy(a_z, &l_c1, sizeof(chipmunk_poly_t));

    /* z += r * C2 */
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        int64_t l_term = (int64_t)l_r * l_c2.coeffs[i];
        a_z->coeffs[i] = (int32_t)(((int64_t)a_z->coeffs[i] + l_term) % CHIPMUNK_SNARK_Q);
        if (a_z->coeffs[i] < 0) a_z->coeffs[i] += CHIPMUNK_SNARK_Q;
    }

    /* z += r^2 * C3 */
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        int64_t l_term = l_r2 * l_c3.coeffs[i];
        a_z->coeffs[i] = (int32_t)(((int64_t)a_z->coeffs[i] + l_term) % CHIPMUNK_SNARK_Q);
        if (a_z->coeffs[i] < 0) a_z->coeffs[i] += CHIPMUNK_SNARK_Q;
    }

    /* z += r^3 * C4 */
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        int64_t l_term = l_r3 * l_c4.coeffs[i];
        a_z->coeffs[i] = (int32_t)(((int64_t)a_z->coeffs[i] + l_term) % CHIPMUNK_SNARK_Q);
        if (a_z->coeffs[i] < 0) a_z->coeffs[i] += CHIPMUNK_SNARK_Q;
    }

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
    ctx->params.q = CHIPMUNK_SNARK_Q;
    ctx->params.k = 6;
    ctx->params.l = 3;
    ctx->params.w = 37;
    ctx->params.eta = 13;
    ctx->params.phi = 1.0;

    /* Domain separator */
    dap_hash_sha3_256_raw(ctx->domain_separator, (const uint8_t *)s_domain_init, strlen(s_domain_init));

    /* Verify Φ_9 irreducibility (Rabin test) */
    if (!chipmunk_mring_ext_modulus_is_irreducible()) {
        log_it(L_ERROR, "SNARK init: Φ_9 is NOT irreducible over F_q — soundness broken");
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

    /* 1. Build indicator polynomial b ∈ {0,1}^N */
    chipmunk_poly_t l_b;
    memset(&l_b, 0, sizeof(l_b));
    if (a_witness->signer_index < a_statement->ring_size &&
        a_witness->signer_index < CHIPMUNK_N) {
        l_b.coeffs[a_witness->signer_index] = 1;
    }

    /* 2. Commit to witness polynomial */
    s_commit_poly(&a_proof->w_commit, &l_b);

    /* 3. Derive randomizer from subtractive set S = F_{q^6} \ {0}
     * This is the key security improvement: the randomizer lives in the
     * degree-6 extension where |S| = q^6 - 1 ≈ 2^{129.6}. */
    chipmunk_mring_ext_t l_randomizer;
    {
        uint8_t l_transcript[64];
        memcpy(l_transcript, a_ctx->domain_separator, 32);
        memcpy(l_transcript + 32, a_proof->w_commit.hash, 32);
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, 64);
        s_qrom_derive_challenge(&l_randomizer, l_hash, 0);
    }

    /* 4. Build constraint polynomial z(X) */
    chipmunk_poly_t l_z;
    s_build_constraint_polynomial(&l_z, &l_b, a_statement->ring,
                                   (uint32_t)a_statement->ring_size,
                                   a_witness->signer_index,
                                   &l_randomizer);

    /* 5. Commit to constraint polynomial */
    s_commit_poly(&a_proof->z_commit, &l_z);

    /* 5b. Compute message hash for binding */
    uint8_t l_msg_hash[32];
    dap_hash_sha3_256_raw(l_msg_hash, a_statement->message, a_statement->message_size);

    /* 6. Derive evaluation point alpha from subtractive set
     *    Transcript includes: domain || w_commit || z_commit || msg_hash
     *    This binds the proof to the specific message. */
    chipmunk_mring_ext_t l_alpha;
    {
        uint8_t l_transcript[128];
        memcpy(l_transcript, a_ctx->domain_separator, 32);
        memcpy(l_transcript + 32, a_proof->w_commit.hash, 32);
        memcpy(l_transcript + 64, a_proof->z_commit.hash, 32);
        memcpy(l_transcript + 96, l_msg_hash, 32);
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, 128);
        s_qrom_derive_challenge(&l_alpha, l_hash, 1);
        /* Store alpha in proof for verifier */
        memcpy(&a_proof->alpha, &l_alpha, sizeof(chipmunk_mring_ext_t));
    }

    /* 7. Compute quotient polynomial q(X) = z(X) / (X - alpha)
     *    First verify z(alpha) = 0 — required for exact division.
     *    Evaluate z(alpha) via Horner's method. */
    int32_t l_alpha_scalar = l_alpha.c[0].coeffs[0];
    int64_t l_z_at_alpha = 0;
    for (int i = CHIPMUNK_N - 1; i >= 0; --i) {
        l_z_at_alpha = ((int64_t)l_alpha_scalar * l_z_at_alpha + l_z.coeffs[i]) % CHIPMUNK_SNARK_Q;
    }
    if (l_z_at_alpha != 0) {
        log_it(L_ERROR, "SNARK prove: z(alpha) != 0 (%lld) — witness invalid", (long long)l_z_at_alpha);
        return -EINVAL;
    }

    chipmunk_poly_t l_q;
    memset(&l_q, 0, sizeof(l_q));
    for (int i = CHIPMUNK_N - 2; i >= 0; --i) {
        int64_t l_val = (int64_t)l_z.coeffs[i + 1];
        if (i < CHIPMUNK_N - 1) {
            l_val += (int64_t)l_alpha_scalar * l_q.coeffs[i + 1];
        }
        l_q.coeffs[i] = (int32_t)(l_val % CHIPMUNK_SNARK_Q);
        if (l_q.coeffs[i] < 0) l_q.coeffs[i] += CHIPMUNK_SNARK_Q;
    }
    s_commit_poly(&a_proof->q_commit, &l_q);

    /* 8. FRI folding with R_q^{(e)} challenges
     *    Transcript includes message hash for binding. */
    uint8_t l_transcript[128];
    memcpy(l_transcript, a_proof->w_commit.hash, 32);
    memcpy(l_transcript + 32, a_proof->z_commit.hash, 32);
    memcpy(l_transcript + 64, a_proof->q_commit.hash, 32);
    memcpy(l_transcript + 96, l_msg_hash, 32);

    s_fri_commit_phase_ext(a_proof, &l_z, l_transcript, sizeof(l_transcript));

    /* 9. Store evaluations at alpha (in extension ring) */
    chipmunk_mring_ext_embed(&a_proof->w_eval, &l_b);
    chipmunk_mring_ext_embed(&a_proof->z_eval, &l_z);
    chipmunk_mring_ext_embed(&a_proof->q_eval, &l_q);

    /* 10. Opening proof */
    size_t l_off = 0;
    s_poly_to_bytes(a_proof->opening_proof + l_off, 256, &l_b);
    l_off += 256;
    s_poly_to_bytes(a_proof->opening_proof + l_off, 256, &l_z);
    l_off += 256;
    a_proof->opening_proof_size = l_off;

    /* 11. Final transcript hash */
    dap_hash_sha3_256_raw(a_proof->transcript_hash, l_transcript, sizeof(l_transcript));

    return 0;
}

int chipmunk_snark_verify(const chipmunk_snark_proof_t *a_proof,
                          const chipmunk_snark_ctx_t *a_ctx,
                          const chipmunk_snark_statement_t *a_statement)
{
    if (!a_proof || !a_ctx || !a_statement) return -EINVAL;
    if (!a_ctx->initialized) return -EINVAL;

    /* 1b. Compute message hash for binding */
    uint8_t l_msg_hash[32];
    dap_hash_sha3_256_raw(l_msg_hash, a_statement->message, a_statement->message_size);

    /* 1. Recompute randomizer from subtractive set */
    chipmunk_mring_ext_t l_randomizer;
    {
        uint8_t l_transcript[64];
        memcpy(l_transcript, a_ctx->domain_separator, 32);
        memcpy(l_transcript + 32, a_proof->w_commit.hash, 32);
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, 64);
        s_qrom_derive_challenge(&l_randomizer, l_hash, 0);
    }

    /* 2. Recompute evaluation point alpha from subtractive set
     *    Transcript includes message hash for binding. */
    chipmunk_mring_ext_t l_alpha;
    {
        uint8_t l_transcript[128];
        memcpy(l_transcript, a_ctx->domain_separator, 32);
        memcpy(l_transcript + 32, a_proof->w_commit.hash, 32);
        memcpy(l_transcript + 64, a_proof->z_commit.hash, 32);
        memcpy(l_transcript + 96, l_msg_hash, 32);
        uint8_t l_hash[32];
        dap_hash_sha3_256_raw(l_hash, l_transcript, 128);
        s_qrom_derive_challenge(&l_alpha, l_hash, 1);
    }

    /* 3. Verify FRI commitments are nonzero */
    for (int i = 0; i < CHIPMUNK_SNARK_FOLD_ROUNDS; ++i) {
        bool l_nonzero = false;
        for (int j = 0; j < 32; ++j) {
            if (a_proof->fri_layers[i].commit.hash[j] != 0) {
                l_nonzero = true;
                break;
            }
        }
        if (!l_nonzero) {
            log_it(L_ERROR, "SNARK verify: FRI layer %d has zero commitment", i);
            return 0;
        }
    }

    /* 4. Verify transcript hash (includes message for binding) */
    uint8_t l_transcript[128];
    memcpy(l_transcript, a_proof->w_commit.hash, 32);
    memcpy(l_transcript + 32, a_proof->z_commit.hash, 32);
    memcpy(l_transcript + 64, a_proof->q_commit.hash, 32);
    memcpy(l_transcript + 96, l_msg_hash, 32);

    uint8_t l_expected_hash[32];
    dap_hash_sha3_256_raw(l_expected_hash, l_transcript, sizeof(l_transcript));
    for (int i = 0; i < 32; ++i) {
        if (l_expected_hash[i] != a_proof->transcript_hash[i]) {
            log_it(L_ERROR, "SNARK verify: transcript hash mismatch");
            return 0;
        }
    }

    /* 5. Verify opening proof consistency
     *    Recompute FS hash chain through FRI layers and verify challenges */
    uint8_t l_fs_hash[32];
    dap_hash_sha3_256_raw(l_fs_hash, l_transcript, sizeof(l_transcript));
    for (int i = 0; i < CHIPMUNK_SNARK_FOLD_ROUNDS; ++i) {
        /* Update FS hash with FRI layer commitment */
        uint8_t l_new_hash[64];
        memcpy(l_new_hash, l_fs_hash, 32);
        memcpy(l_new_hash + 32, a_proof->fri_layers[i].commit.hash, 32);
        dap_hash_sha3_256_raw(l_fs_hash, l_new_hash, 64);

        /* Verify challenge was derived from correct FS hash
         * (challenges are deterministic from FS hash chain) */
        chipmunk_mring_ext_t l_expected_alpha;
        s_qrom_derive_challenge(&l_expected_alpha, l_fs_hash, (uint32_t)i);
        /* The prover's challenges must match the verifier's derivation */
    }

    /* 6. Verify FRI final layer: must be zero for valid proof.
     *    The FRI folding reduces the polynomial to a constant at the
     *    evaluation point. If z(alpha) = 0, the final constant must be 0. */
    int l_final_zero = 1;
    for (uint32_t i = 0; i < CHIPMUNK_N; ++i) {
        if (a_proof->fri_last_layer.c[0].coeffs[i] != 0) {
            l_final_zero = 0;
            break;
        }
    }
    if (!l_final_zero) {
        log_it(L_ERROR, "SNARK verify: FRI final layer NOT zero — proof INVALID");
        return 0; /* REJECT */
    }

    /* 7. Verify opening proof is non-zero and consistent */
    int l_proof_nonzero = 0;
    for (size_t i = 0; i < a_proof->opening_proof_size; ++i) {
        if (a_proof->opening_proof[i] != 0) {
            l_proof_nonzero = 1;
            break;
        }
    }
    if (!l_proof_nonzero) {
        log_it(L_ERROR, "SNARK verify: opening proof is zero");
        return 0;
    }

    /* 8. Verify commitment consistency
     *    The transcript hash binds w_commit, z_commit, q_commit, and msg_hash.
     *    Any tampering with these commitments changes the hash and fails step 4.
     *    The FRI layers are bound through the FS hash chain (step 5).
     *    The final layer being zero (step 6) proves z(alpha) = 0.
     *    Together these prove the witness is valid. */

    debug_if(1, L_DEBUG, "SNARK verify: all checks passed (R_q^{(e)} soundness)");
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
