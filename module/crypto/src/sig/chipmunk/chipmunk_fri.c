/*
 * chipmunk_fri.c — FRI prover: commit phase (7 rounds of folding + Merkle commitments).
 *
 * See chipmunk_fri.h for documentation.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "chipmunk_fri.h"
#include "chipmunk_poseidon.h"
#include <string.h>
#include <stdlib.h>

#include "dap_common.h"
#include "dap_memwipe.h"

#define LOG_TAG "chipmunk_fri"

/* Total elements across all rounds + final: 2048+1024+512+256+128+64+32+16 = 4080 */
#define FRI_TOTAL_DATA  4080u

/* Parameterized field multiplication in [0, q) (Phase 9.13h). */
static inline int32_t s_fqmul_q(int32_t a_a, int32_t a_b, uint64_t q)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)q);
    if (l_r < 0) l_r += (int32_t)q;
    return l_r;
}

/* Parameterized modular reduction into [0, q). */
static inline int32_t s_mod_q(int64_t a_val, uint64_t q)
{
    int64_t l_r = a_val % (int64_t)q;
    if (l_r < 0) l_r += (int64_t)q;
    return (int32_t)l_r;
}

/* Log2 for powers of 2. */
static inline uint32_t s_log2(uint32_t n)
{
    uint32_t r = 0;
    while (n > 1u) { n >>= 1u; ++r; }
    return r;
}

int chipmunk_fri_prover_init(chipmunk_fri_prover_t *prover)
{
    if (!prover)
        return -1;

    int l_rc = chipmunk_field_init();
    if (l_rc != 0) {
        log_it(L_ERROR, "FRI prover: chipmunk_field_init failed");
        return l_rc;
    }
    l_rc = chipmunk_fri_ntt_init();
    if (l_rc != 0) {
        log_it(L_ERROR, "FRI prover: chipmunk_fri_ntt_init failed");
        return l_rc;
    }

    memset(prover, 0, sizeof(*prover));

    prover->round_data = calloc(FRI_TOTAL_DATA, sizeof(int32_t));
    if (!prover->round_data) {
        log_it(L_ERROR, "FRI prover: alloc round_data failed");
        return -1;
    }

    /* Merkle scratch: double buffer for 2048 leaves. */
    prover->merkle_scratch = calloc(2u * CHIPMUNK_FRI_INIT_SIZE, sizeof(int32_t));
    if (!prover->merkle_scratch) {
        free(prover->round_data);
        prover->round_data = NULL;
        log_it(L_ERROR, "FRI prover: alloc merkle_scratch failed");
        return -1;
    }

    /* Precompute round parameters. */
    uint32_t l_sz = CHIPMUNK_FRI_INIT_SIZE;
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        prover->round_sizes[r] = l_sz;

        /* Cap size: 16 for large trees, tree size for small trees. */
        if (l_sz >= CHIPMUNK_FRI_CAP_SIZE * 2u) {
            prover->cap_sizes[r] = CHIPMUNK_FRI_CAP_SIZE;
        } else {
            prover->cap_sizes[r] = l_sz;
        }

        /* Auth path length = log2(n) - log2(cap_size). */
        prover->auth_path_lens[r] = s_log2(l_sz) - s_log2(prover->cap_sizes[r]);

        l_sz /= 2u;
    }

    /* 2^{-1} mod q. */
    prover->q = (uint64_t)CHIPMUNK_Q;  /* default; overridden by _q variant */
    prover->inv_2 = chipmunk_field_inv_q(2, (uint64_t)CHIPMUNK_Q);

    prover->committed = false;
    return 0;
}

void chipmunk_fri_prover_free(chipmunk_fri_prover_t *prover)
{
    if (!prover)
        return;
    if (prover->round_data) {
        dap_memwipe(prover->round_data, FRI_TOTAL_DATA * sizeof(int32_t));
        free(prover->round_data);
        prover->round_data = NULL;
    }
    if (prover->merkle_scratch) {
        dap_memwipe(prover->merkle_scratch, 2u * CHIPMUNK_FRI_INIT_SIZE * sizeof(int32_t));
        free(prover->merkle_scratch);
        prover->merkle_scratch = NULL;
    }
    if (prover->ntt_ctx_ready) {
        chipmunk_fri_ntt_ctx_free(&prover->ntt_ctx);
        prover->ntt_ctx_ready = false;
    }
    prover->committed = false;
}

int chipmunk_fri_commit(chipmunk_fri_prover_t *prover,
                         const int32_t poly[CHIPMUNK_RS_MSG_LEN],
                         const int32_t alphas[CHIPMUNK_FRI_ROUNDS])
{
    if (!prover || !poly || !alphas)
        return -1;

    /* Step 1: RS-encode polynomial → 2048 evaluations.
     * Always use per-q NTT tables. */
    int32_t *l_cur = prover->round_data;  /* round 0 codeword */
    int l_rc;
    if (!prover->ntt_ctx_ready) {
        /* NTT log is always 11 (2048-pt). Caller set prover->q before commit. */
        l_rc = chipmunk_fri_ntt_ctx_init(&prover->ntt_ctx, prover->q,
                                           CHIPMUNK_FRI_NTT_LOG);
        if (l_rc != 0) {
            log_it(L_ERROR, "FRI commit: per-q NTT ctx init failed for q=%lu",
                   (unsigned long)prover->q);
            return l_rc;
        }
        prover->ntt_ctx_ready = true;
    }
    l_rc = chipmunk_rs_encode_q(l_cur, poly, &prover->ntt_ctx);
    if (l_rc != 0) {
        log_it(L_ERROR, "FRI commit: RS encode failed");
        return l_rc;
    }

    /* Build Merkle tree for round 0 and store cap. */
    uint32_t l_n = prover->round_sizes[0];
    l_rc = chipmunk_merkle_build(l_cur, l_n,
                                  prover->proof.caps[0].nodes,
                                  prover->cap_sizes[0],
                                  prover->merkle_scratch);
    if (l_rc != 0) {
        log_it(L_ERROR, "FRI commit: merkle build round 0 failed");
        return l_rc;
    }

    /* Step 2: 7 rounds of folding.
     *
     * Standard FRI folding with domain-point division:
     *   h(y) = [f(x) + f(-x)]/2 + α · [f(x) - f(-x)] / (2·x)
     *        = f_even(y) + α · f_odd(y)
     * where y = x², and x = g·ω^l is the coset domain point.
     *
     * This halves the polynomial degree at each round, which is
     * essential for FRI proximity soundness. */
    int32_t *l_prev = l_cur;
    uint32_t l_offset = 0u;

    /* Precompute inv(g) and inv(ω) for domain point iteration. */
    uint64_t l_q = prover->q;
    int32_t l_inv_g = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, l_q);
    int32_t l_omega_inv = chipmunk_field_omega_2048_inv();

    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        l_n = prover->round_sizes[r];
        int32_t l_alpha = alphas[r];
        uint32_t l_half = l_n / 2u;

        /* Compute offset into round_data for this round. */
        l_offset = 0;
        for (unsigned j = 0; j < r; ++j) {
            l_offset += prover->round_sizes[j];
        }
        l_prev = prover->round_data + l_offset;
        int32_t *l_next = prover->round_data + l_offset + l_n;

        /* Compute inv_domain points for this round.
         * Round r domain: {g · ω^(l·2^r) : l = 0..n-1}
         * inv_domain[l] = inv(g) · (inv(ω))^(l·2^r)
         *
         * We iterate: inv_zeta starts at inv(g)·inv(ω)^(2^r - 1)
         * (the inv domain point at l=0), then multiply by inv(ω)^2^r
         * to advance l. But simpler: precompute inv(ω^stride) where
         * stride = 2^r, then iterate. */
        int32_t l_omega_inv_stride = 1;  /* inv(ω)^(2^r) — will compute */
        {
            /* Compute inv(ω)^(2^r) by repeated squaring. */
            int32_t l_base = l_omega_inv;
            uint32_t l_exp = 1u << r;
            l_omega_inv_stride = 1;
            while (l_exp > 0) {
                if (l_exp & 1u)
                    l_omega_inv_stride = s_fqmul_q(l_omega_inv_stride, l_base, l_q);
                l_base = s_fqmul_q(l_base, l_base, l_q);
                l_exp >>= 1u;
            }
        }

        /* inv_zeta_l = inv(g) · inv(ω)^(l · 2^r)
         * Start at l=0: inv_zeta = inv(g), then multiply by inv(ω)^2^r each step. */
        int32_t l_inv_zeta = l_inv_g;

        /* Phase 9.14: recompute inv_2 from active q (not prover->inv_2 which
         * may be stale if caller overrode prover->q after init). */
        int32_t l_inv2 = chipmunk_field_inv_q(2, l_q);
        for (uint32_t l = 0; l < l_half; ++l) {
            int32_t l_vp = l_prev[l];
            int32_t l_vm = l_prev[l + l_half];

            /* even_sum = [f(x) + f(-x)] / 2 */
            int64_t l_even_sum = (int64_t)l_vp + (int64_t)l_vm;
            l_even_sum = l_even_sum % (int64_t)l_q;
            int32_t l_even = (int32_t)(l_even_sum * (int64_t)l_inv2 % (int64_t)l_q);

            /* odd_sum = [f(x) - f(-x)] / (2·x)
             * = [f(x) - f(-x)] · inv(2) · inv(x) */
            int64_t l_odd_diff = (int64_t)l_vp - (int64_t)l_vm;
            l_odd_diff = l_odd_diff % (int64_t)l_q;
            if (l_odd_diff < 0) l_odd_diff += (int64_t)l_q;
            int32_t l_odd = s_fqmul_q(
                (int32_t)(l_odd_diff * (int64_t)l_inv2 % (int64_t)l_q),
                l_inv_zeta, l_q);

            /* h(y_l) = even + α · odd */
            l_next[l] = l_even + s_fqmul_q(l_alpha, l_odd, l_q);
            if (l_next[l] >= (int32_t)l_q) l_next[l] -= (int32_t)l_q;

            /* Advance inv_zeta for next l. */
            l_inv_zeta = s_fqmul_q(l_inv_zeta, l_omega_inv_stride, l_q);
        }

        /* Build Merkle tree for next round (if not the last). */
        if (r + 1u < CHIPMUNK_FRI_ROUNDS) {
            uint32_t l_next_size = prover->round_sizes[r + 1u];
            l_rc = chipmunk_merkle_build(l_next, l_next_size,
                                          prover->proof.caps[r + 1u].nodes,
                                          prover->cap_sizes[r + 1u],
                                          prover->merkle_scratch);
            if (l_rc != 0) {
                log_it(L_ERROR, "FRI commit: merkle build round %u failed", r + 1);
                return l_rc;
            }
        }
    }

    /* Step 3: Copy final evaluations (round 7 = 16 values). */
    l_offset = 0;
    for (unsigned j = 0; j < CHIPMUNK_FRI_ROUNDS; ++j) {
        l_offset += prover->round_sizes[j];
    }
    const int32_t *l_final = prover->round_data + l_offset;
    memcpy(prover->proof.final_evals, l_final,
           CHIPMUNK_FRI_FINAL_SIZE * sizeof(int32_t));

    prover->committed = true;
    return 0;
}

const int32_t *chipmunk_fri_prover_round_data(
    const chipmunk_fri_prover_t *prover, uint32_t round, uint32_t *len)
{
    if (!prover || !prover->committed || round >= CHIPMUNK_FRI_ROUNDS) {
        if (len) *len = 0;
        return NULL;
    }

    uint32_t l_offset = 0;
    for (unsigned j = 0; j < round; ++j) {
        l_offset += prover->round_sizes[j];
    }
    if (len) *len = prover->round_sizes[round];
    return prover->round_data + l_offset;
}

const chipmunk_fri_cap_t *chipmunk_fri_prover_cap(
    const chipmunk_fri_prover_t *prover, uint32_t round)
{
    if (!prover || !prover->committed || round >= CHIPMUNK_FRI_ROUNDS)
        return NULL;
    return &prover->proof.caps[round];
}

const int32_t *chipmunk_fri_prover_final_evals(
    const chipmunk_fri_prover_t *prover)
{
    if (!prover || !prover->committed)
        return NULL;
    return prover->proof.final_evals;
}

bool chipmunk_fri_verify_fold(const int32_t *h_r, const int32_t *h_r1,
                               uint32_t n_r, int32_t alpha, uint32_t round,
                               uint32_t l)
{
    return chipmunk_fri_verify_fold_q(h_r, h_r1, n_r, alpha, round, l,
                                      (uint64_t)CHIPMUNK_Q);
}

bool chipmunk_fri_verify_fold_q(const int32_t *h_r, const int32_t *h_r1,
                                  uint32_t n_r, int32_t alpha, uint32_t round,
                                  uint32_t l, uint64_t q)
{
    if (!h_r || !h_r1 || n_r < 2u || l >= n_r / 2u)
        return false;

    int32_t l_inv2 = chipmunk_field_inv_q(2, q);

    /* Compute inv(x) where x = g·ω^(l·2^round) is the coset domain point. */
    int32_t l_inv_g = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, q);
    int32_t l_omega_inv = chipmunk_field_omega_2048_inv();
    int32_t l_inv_omega_exp = 1;
    {
        int32_t l_base = l_omega_inv;
        uint32_t l_exp = l * (1u << round);
        while (l_exp > 0) {
            if (l_exp & 1u)
                l_inv_omega_exp = s_fqmul_q(l_inv_omega_exp, l_base, q);
            l_base = s_fqmul_q(l_base, l_base, q);
            l_exp >>= 1u;
        }
    }
    int32_t l_inv_x = s_fqmul_q(l_inv_g, l_inv_omega_exp, q);

    /* even = [h_r[l] + h_r[l+half]] / 2 */
    int64_t l_even_sum = (int64_t)h_r[l] + (int64_t)h_r[l + n_r / 2u];
    l_even_sum = l_even_sum % (int64_t)q;
    int32_t l_even = (int32_t)(l_even_sum * (int64_t)l_inv2 % (int64_t)q);

    /* odd = [h_r[l] - h_r[l+half]] · inv(2) · inv(x) */
    int64_t l_odd_diff = (int64_t)h_r[l] - (int64_t)h_r[l + n_r / 2u];
    l_odd_diff = l_odd_diff % (int64_t)q;
    if (l_odd_diff < 0) l_odd_diff += (int64_t)q;
    int32_t l_odd = s_fqmul_q(
        (int32_t)(l_odd_diff * (int64_t)l_inv2 % (int64_t)q),
        l_inv_x, q);

    /* expected = even + α · odd */
    int32_t l_expected = l_even + s_fqmul_q(alpha, l_odd, q);
    if (l_expected >= (int32_t)q) l_expected -= (int32_t)q;

    return l_expected == h_r1[l];
}

/* Helper: round size for verify (avoids depending on prover state). */
static inline uint32_t s_round_sizes_val(unsigned r)
{
    static const uint32_t s_sizes[CHIPMUNK_FRI_ROUNDS] = {
        2048, 1024, 512, 256, 128, 64, 32
    };
    return (r < CHIPMUNK_FRI_ROUNDS) ? s_sizes[r] : 0;
}

/* -------------------------------------------------------------------------
 * FRI Query Phase
 * ------------------------------------------------------------------------- */

int chipmunk_fri_query(chipmunk_fri_prover_t *prover,
                        uint32_t num_queries,
                        const uint32_t indices[num_queries],
                        chipmunk_fri_query_opening_t out[num_queries])
{
    if (!prover || !prover->committed || !indices || !out)
        return -1;
    if (num_queries == 0 || num_queries > CHIPMUNK_FRI_NUM_QUERIES)
        return -1;

    for (uint32_t qi = 0; qi < num_queries; ++qi) {
        uint32_t l_idx = indices[qi];
        if (l_idx >= CHIPMUNK_FRI_INIT_SIZE)
            return -1;

        out[qi].idx = l_idx;

        for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
            uint32_t l_n = prover->round_sizes[r];
            uint32_t l_cap_size = prover->cap_sizes[r];

            /* Get round codeword. */
            uint32_t l_offset = 0;
            for (unsigned j = 0; j < r; ++j)
                l_offset += prover->round_sizes[j];
            const int32_t *l_data = prover->round_data + l_offset;

            /* Leaf and its antipodal sibling. */
            uint32_t l_half = l_n / 2u;
            uint32_t l_leaf_idx = l_idx % l_n;       /* index within this round */
            uint32_t l_sib_idx = l_leaf_idx ^ l_half; /* antipodal: flip the highest bit */

            out[qi].leaf_values[r] = l_data[l_leaf_idx];
            out[qi].sibling_values[r] = l_data[l_sib_idx];

            /* Generate Merkle auth path for the leaf. */
            int l_rc = chipmunk_merkle_open(l_data, l_n, l_leaf_idx,
                                              l_cap_size,
                                              &out[qi].paths[r],
                                              prover->merkle_scratch);
            if (l_rc != 0) {
                log_it(L_ERROR, "FRI query: merkle_open round %u idx %u failed",
                       r, l_leaf_idx);
                return l_rc;
            }
        }
    }

    return 0;
}

bool chipmunk_fri_verify_query(const chipmunk_fri_proof_t *proof,
                                uint32_t q,
                                const int32_t alphas[CHIPMUNK_FRI_ROUNDS])
{
    return chipmunk_fri_verify_query_q(proof, q, alphas, (uint64_t)CHIPMUNK_Q);
}

bool chipmunk_fri_verify_query_q(const chipmunk_fri_proof_t *proof,
                                  uint32_t q,
                                  const int32_t alphas[CHIPMUNK_FRI_ROUNDS],
                                  uint64_t fq)
{
    if (!proof || q >= CHIPMUNK_FRI_NUM_QUERIES)
        return false;

    const chipmunk_fri_query_opening_t *l_qry = &proof->queries[q];
    int32_t l_inv2 = chipmunk_field_inv_q(2, fq);

    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t l_n = s_round_sizes_val(r);
        uint32_t l_cap_size = (l_n >= 32u) ? 16u : l_n;
        const int32_t *l_cap = proof->commit.caps[r].nodes;
        int32_t l_leaf = l_qry->leaf_values[r];
        int32_t l_sib = l_qry->sibling_values[r];
        const chipmunk_merkle_auth_path_t *l_path = &l_qry->paths[r];

        /* 1. Verify Merkle auth path for the leaf. */
        bool l_ok = chipmunk_merkle_verify(l_leaf, l_n, l_path, l_cap, l_cap_size);
        if (!l_ok)
            return false;

        /* 2. Verify folding relation with domain-point division. */
        if (r + 1u < CHIPMUNK_FRI_ROUNDS) {
            uint32_t l_half = l_n / 2u;
            uint32_t l_leaf_idx = l_qry->idx % l_n;
            uint32_t l_canonical = (l_leaf_idx < l_half) ? l_leaf_idx : (l_leaf_idx - l_half);

            int32_t l_inv_g = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, fq);
            int32_t l_omega_inv = chipmunk_field_omega_2048_inv();
            int32_t l_inv_omega_exp = 1;
            {
                int32_t l_base = l_omega_inv;
                uint32_t l_exp = l_canonical * (1u << r);
                while (l_exp > 0) {
                    if (l_exp & 1u)
                        l_inv_omega_exp = s_fqmul_q(l_inv_omega_exp, l_base, fq);
                    l_base = s_fqmul_q(l_base, l_base, fq);
                    l_exp >>= 1u;
                }
            }
            int32_t l_inv_x = s_fqmul_q(l_inv_g, l_inv_omega_exp, fq);

            int32_t l_first, l_second;
            if (l_leaf_idx < l_half) {
                l_first  = l_leaf;
                l_second = l_sib;
            } else {
                l_first  = l_sib;
                l_second = l_leaf;
            }

            /* even = [first + second] / 2 */
            int64_t l_even_sum = (int64_t)l_first + (int64_t)l_second;
            l_even_sum = l_even_sum % (int64_t)fq;
            int32_t l_even = (int32_t)(l_even_sum * (int64_t)l_inv2 % (int64_t)fq);

            /* odd = [first - second] · inv(2) · inv(x) */
            int64_t l_odd_diff = (int64_t)l_first - (int64_t)l_second;
            l_odd_diff = l_odd_diff % (int64_t)fq;
            if (l_odd_diff < 0) l_odd_diff += (int64_t)fq;
            int32_t l_odd = s_fqmul_q(
                (int32_t)(l_odd_diff * (int64_t)l_inv2 % (int64_t)fq),
                l_inv_x, fq);

            /* folded = even + α · odd */
            int32_t l_folded = l_even + s_fqmul_q(alphas[r], l_odd, fq);
            if (l_folded >= (int32_t)fq) l_folded -= (int32_t)fq;

            if (l_folded != l_qry->leaf_values[r + 1u])
                return false;
        }
    }

    return true;
}

/* -------------------------------------------------------------------------
 * FRI Verify Phase (verifier-side)
 * ------------------------------------------------------------------------- */

int chipmunk_fri_derive_query_indices(chipmunk_fri_transcript_t *tr,
                                       uint32_t num_queries,
                                       uint32_t domain_size,
                                       uint32_t out[])
{
    if (!tr || !out || num_queries == 0 || domain_size == 0)
        return -1;

    for (uint32_t i = 0; i < num_queries; ++i) {
        int32_t val;
        int rc = chipmunk_fri_transcript_squeeze_fq(tr, &val);
        if (rc < 0) {
            log_it(L_ERROR, "FRI verify: squeeze query index %u failed", i);
            return rc;
        }
        /* Map to [0, domain_size) via modular reduction. */
        out[i] = (uint32_t)val % domain_size;
    }
    return 0;
}

bool chipmunk_fri_verify_q(const chipmunk_fri_proof_t *proof,
                             const uint8_t domain[16],
                             const int32_t alphas[CHIPMUNK_FRI_ROUNDS],
                             uint64_t q,
                             chipmunk_fri_verify_result_t *result)
{
    if (!proof || !domain || !alphas) {
        if (result) {
            memset(result, 0, sizeof(*result));
            memcpy(result->reason, "null proof, domain, or alphas", 29);
        }
        return false;
    }

    if (result)
        memset(result, 0, sizeof(*result));

    /* 1. Initialize transcript with domain separator. */
    chipmunk_fri_transcript_t tr;
    int rc = chipmunk_fri_transcript_init(&tr, domain);
    tr.q = q;  /* Phase 9.14: per-q transcript */
    if (rc != 0) {
        if (result) memcpy(result->reason, "transcript init", 16);
        return false;
    }

    /* 2. Absorb all 7 Merkle caps into transcript. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t cap_size = s_round_sizes_val(r) >= 32u ? 16u : s_round_sizes_val(r);
        rc = chipmunk_fri_transcript_absorb_cap(&tr, proof->commit.caps[r].nodes,
                                                 cap_size);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb cap round %u failed", r);
            }
            return false;
        }
    }

    /* 3. Absorb final evaluations (16 values). */
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, proof->commit.final_evals[i]);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb final eval %u failed", i);
            }
            return false;
        }
    }

    /* 4. Absorb the alphas (verifier-derived from earlier transcript context). */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, alphas[r]);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb alpha %u failed", r);
            }
            return false;
        }
    }

    /* 5. Finalize: grinding PoW + prepare XOF. */
    rc = chipmunk_fri_transcript_finalize(&tr);
    if (rc != 0) {
        if (result) memcpy(result->reason, "finalize failed", 15);
        return false;
    }

    if (result)
        result->grinding_nonce = tr.grinding_nonce;

    /* 6. Derive 8 query indices. */
    uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES];
    rc = chipmunk_fri_derive_query_indices(&tr, CHIPMUNK_FRI_NUM_QUERIES,
                                            CHIPMUNK_FRI_INIT_SIZE, indices);
    if (rc != 0) {
        if (result) memcpy(result->reason, "derive indices failed", 21);
        return false;
    }

    /* 7. Verify each query. */
    for (uint32_t qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
        /* Check that the query's stored index matches the derived one. */
        if (proof->queries[qi].idx != indices[qi]) {
            if (result) {
                result->failed_query = qi;
                snprintf(result->reason, sizeof(result->reason),
                         "query %u index mismatch (got %u, expected %u)",
                         qi, proof->queries[qi].idx, indices[qi]);
            }
            return false;
        }

        if (!chipmunk_fri_verify_query_q(proof, qi, alphas, q)) {
            if (result) {
                result->failed_query = qi;
                /* Determine which round failed. */
                for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
                    uint32_t l_n = s_round_sizes_val(r);
                    uint32_t l_cap_size = (l_n >= 32u) ? 16u : l_n;
                    const int32_t *l_cap = proof->commit.caps[r].nodes;
                    bool l_ok = chipmunk_merkle_verify(
                        proof->queries[qi].leaf_values[r], l_n,
                        &proof->queries[qi].paths[r], l_cap, l_cap_size);
                    if (!l_ok) {
                        result->failed_round = r;
                        snprintf(result->reason, sizeof(result->reason),
                                 "query %u round %u merkle verify", qi, r);
                        break;
                    }
                    /* Check folding with domain-point division. */
                    if (r + 1u < CHIPMUNK_FRI_ROUNDS) {
                        uint32_t l_half = l_n / 2u;
                        uint32_t l_leaf_idx = proof->queries[qi].idx % l_n;
                        uint32_t l_canonical = (l_leaf_idx < l_half) ? l_leaf_idx : (l_leaf_idx - l_half);
                        int32_t l_inv2 = chipmunk_field_inv_q(2, q);

                        /* Compute inv(x) where x = g·ω^(canonical·2^r). */
                        int32_t l_inv_g = chipmunk_field_inv_q((int32_t)CHIPMUNK_RS_COSET_G, q);
                        int32_t l_omega_inv = chipmunk_field_omega_2048_inv();
                        int32_t l_inv_omega_exp = 1;
                        {
                            int32_t l_base = l_omega_inv;
                            uint32_t l_exp = l_canonical * (1u << r);
                            while (l_exp > 0) {
                                if (l_exp & 1u)
                                    l_inv_omega_exp = s_fqmul_q(l_inv_omega_exp, l_base, q);
                                l_base = s_fqmul_q(l_base, l_base, q);
                                l_exp >>= 1u;
                            }
                        }
                        int32_t l_inv_x = s_fqmul_q(l_inv_g, l_inv_omega_exp, q);

                        int32_t l_first, l_second;
                        if (l_leaf_idx < l_half) {
                            l_first = proof->queries[qi].leaf_values[r];
                            l_second = proof->queries[qi].sibling_values[r];
                        } else {
                            l_first = proof->queries[qi].sibling_values[r];
                            l_second = proof->queries[qi].leaf_values[r];
                        }

                        int64_t l_even_sum = (int64_t)l_first + (int64_t)l_second;
                        l_even_sum = l_even_sum % (int64_t)q;
                        int32_t l_even = (int32_t)(l_even_sum * (int64_t)l_inv2 % (int64_t)q);

                        int64_t l_odd_diff = (int64_t)l_first - (int64_t)l_second;
                        l_odd_diff = l_odd_diff % (int64_t)q;
                        if (l_odd_diff < 0) l_odd_diff += (int64_t)q;
                        int32_t l_odd = s_fqmul_q(
                            (int32_t)(l_odd_diff * (int64_t)l_inv2 % (int64_t)q),
                            l_inv_x, q);

                        int32_t l_folded = l_even + s_fqmul_q(alphas[r], l_odd, q);
                        if (l_folded >= (int32_t)q) l_folded -= (int32_t)q;
                        if (l_folded != proof->queries[qi].leaf_values[r + 1u]) {
                            result->failed_round = r;
                            snprintf(result->reason, sizeof(result->reason),
                                     "query %u round %u folding", qi, r);
                            break;
                        }
                    }
                }
            }
            return false;
        }
    }

    /* All checks passed. */
    if (result)
        result->valid = true;
    return true;
}

bool chipmunk_fri_verify_fast_q(const chipmunk_fri_proof_t *proof,
                                  const uint8_t domain[16],
                                  const int32_t alphas[CHIPMUNK_FRI_ROUNDS],
                                  uint32_t grinding_nonce,
                                  uint64_t q,
                                  chipmunk_fri_verify_result_t *result)
{
    if (!proof || !domain || !alphas) {
        if (result) {
            memset(result, 0, sizeof(*result));
            memcpy(result->reason, "null proof, domain, or alphas", 29);
        }
        return false;
    }

    if (result)
        memset(result, 0, sizeof(*result));

    /* 1. Initialize transcript with domain separator. */
    chipmunk_fri_transcript_t tr;
    int rc = chipmunk_fri_transcript_init(&tr, domain);
    tr.q = q;  /* Phase 9.14: per-q transcript */
    if (rc != 0) {
        if (result) memcpy(result->reason, "transcript init", 16);
        return false;
    }

    /* 2. Absorb all 7 Merkle caps into transcript. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        uint32_t cap_size = s_round_sizes_val(r) >= 32u ? 16u : s_round_sizes_val(r);
        rc = chipmunk_fri_transcript_absorb_cap(&tr, proof->commit.caps[r].nodes,
                                                 cap_size);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb cap round %u failed", r);
            }
            return false;
        }
    }

    /* 3. Absorb final evaluations (16 values). */
    for (unsigned i = 0; i < CHIPMUNK_FRI_FINAL_SIZE; ++i) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, proof->commit.final_evals[i]);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb final eval %u failed", i);
            }
            return false;
        }
    }

    /* 4. Absorb the alphas. */
    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        rc = chipmunk_fri_transcript_absorb_fq(&tr, alphas[r]);
        if (rc != 0) {
            if (result) {
                snprintf(result->reason, sizeof(result->reason),
                         "absorb alpha %u failed", r);
            }
            return false;
        }
    }

    /* 5. Finalize with verifier-side grinding check (1 hash, not 2^16). */
    rc = chipmunk_fri_transcript_finalize_verify(&tr, grinding_nonce);
    if (rc != 0) {
        if (result) memcpy(result->reason, "finalize verify failed", 23);
        return false;
    }

    if (result)
        result->grinding_nonce = grinding_nonce;

    /* 6. Derive 8 query indices. */
    uint32_t indices[CHIPMUNK_FRI_NUM_QUERIES];
    rc = chipmunk_fri_derive_query_indices(&tr, CHIPMUNK_FRI_NUM_QUERIES,
                                            CHIPMUNK_FRI_INIT_SIZE, indices);
    if (rc != 0) {
        if (result) memcpy(result->reason, "derive indices failed", 21);
        return false;
    }

    /* 7. Verify each query. */
    for (uint32_t qi = 0; qi < CHIPMUNK_FRI_NUM_QUERIES; ++qi) {
        if (proof->queries[qi].idx != indices[qi]) {
            if (result) {
                result->failed_query = qi;
                snprintf(result->reason, sizeof(result->reason),
                         "query %u index mismatch (got %u, expected %u)",
                         qi, proof->queries[qi].idx, indices[qi]);
            }
            return false;
        }

        if (!chipmunk_fri_verify_query_q(proof, qi, alphas, q)) {
            if (result) {
                result->failed_query = qi;
                snprintf(result->reason, sizeof(result->reason),
                         "query %u verification failed", qi);
            }
            return false;
        }
    }

    /* All checks passed. */
    if (result)
        result->valid = true;
    return true;
}
