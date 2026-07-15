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

#define LOG_TAG "chipmunk_fri"

/* Total elements across all rounds + final: 2048+1024+512+256+128+64+32+16 = 4080 */
#define FRI_TOTAL_DATA  4080u

/* Field multiplication in [0, q). */
static inline int32_t s_fqmul(int32_t a_a, int32_t a_b)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % (int64_t)CHIPMUNK_Q);
    if (l_r < 0) l_r += (int32_t)CHIPMUNK_Q;
    return l_r;
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
    prover->inv_2 = chipmunk_field_inv(2);

    prover->committed = false;
    return 0;
}

void chipmunk_fri_prover_free(chipmunk_fri_prover_t *prover)
{
    if (!prover)
        return;
    free(prover->round_data);
    prover->round_data = NULL;
    free(prover->merkle_scratch);
    prover->merkle_scratch = NULL;
    prover->committed = false;
}

int chipmunk_fri_commit(chipmunk_fri_prover_t *prover,
                         const int32_t poly[CHIPMUNK_RS_MSG_LEN],
                         const int32_t alphas[CHIPMUNK_FRI_ROUNDS])
{
    if (!prover || !poly || !alphas)
        return -1;

    /* Step 1: RS-encode polynomial → 2048 evaluations. */
    int32_t *l_cur = prover->round_data;  /* round 0 codeword */
    int l_rc = chipmunk_rs_encode(l_cur, poly);
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

    /* Step 2: 7 rounds of folding. */
    int32_t *l_prev = l_cur;
    uint32_t l_offset = 0u;  /* running offset into round_data */

    for (unsigned r = 0; r < CHIPMUNK_FRI_ROUNDS; ++r) {
        l_n = prover->round_sizes[r];
        int32_t l_alpha = alphas[r];
        int32_t l_one_plus_alpha = (1 + l_alpha) % (int32_t)CHIPMUNK_Q;
        int32_t l_one_minus_alpha = (1 - l_alpha + (int32_t)CHIPMUNK_Q) % (int32_t)CHIPMUNK_Q;

        uint32_t l_half = l_n / 2u;
        int32_t *l_next = prover->round_data + (uint32_t)(CHIPMUNK_RS_MSG_LEN - l_half);

        /* Wait — need to compute the correct offset for each round.
         * Round data layout: round0[2048], round1[1024], ..., round6[32], final[16]
         * Total offset after round r: sum of sizes 0..r */
        l_offset = 0;
        for (unsigned j = 0; j < r; ++j) {
            l_offset += prover->round_sizes[j];
        }
        l_prev = prover->round_data + l_offset;
        l_next = prover->round_data + l_offset + l_n;

        /* Fold: H_{r+1}[l] = [(1+α)·H_r[l] + (1-α)·H_r[l+half]] * inv_2 */
        int32_t l_inv2 = prover->inv_2;
        for (uint32_t l = 0; l < l_half; ++l) {
            int32_t l_vp = l_prev[l];
            int32_t l_vm = l_prev[l + l_half];
            int64_t l_sum = (int64_t)l_one_plus_alpha * (int64_t)l_vp
                          + (int64_t)l_one_minus_alpha * (int64_t)l_vm;
            l_sum = l_sum % (int64_t)CHIPMUNK_Q;
            if (l_sum < 0) l_sum += (int64_t)CHIPMUNK_Q;
            l_next[l] = (int32_t)(l_sum * (int64_t)l_inv2 % (int64_t)CHIPMUNK_Q);
            if (l_next[l] < 0) l_next[l] += (int32_t)CHIPMUNK_Q;
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
                               uint32_t n_r, int32_t alpha, uint32_t l)
{
    if (!h_r || !h_r1 || n_r < 2u || l >= n_r / 2u)
        return false;

    uint32_t l_half = n_r / 2u;
    int32_t l_inv2 = chipmunk_field_inv(2);
    int32_t l_1pa = (1 + alpha) % (int32_t)CHIPMUNK_Q;
    int32_t l_1ma = (1 - alpha + (int32_t)CHIPMUNK_Q) % (int32_t)CHIPMUNK_Q;

    int64_t l_sum = (int64_t)l_1pa * (int64_t)h_r[l]
                  + (int64_t)l_1ma * (int64_t)h_r[l + l_half];
    l_sum = l_sum % (int64_t)CHIPMUNK_Q;
    if (l_sum < 0) l_sum += (int64_t)CHIPMUNK_Q;
    int32_t l_expected = (int32_t)(l_sum * (int64_t)l_inv2 % (int64_t)CHIPMUNK_Q);
    if (l_expected < 0) l_expected += (int32_t)CHIPMUNK_Q;

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
    if (!proof || q >= CHIPMUNK_FRI_NUM_QUERIES)
        return false;

    const chipmunk_fri_query_opening_t *l_qry = &proof->queries[q];
    int32_t l_inv2 = chipmunk_field_inv(2);

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

        /* 2. Verify folding relation: leaf + sibling → next round leaf.
         * The canonical index in round r+1 is min(leaf_idx, sib_idx).
         * H_{r+1}[c] = [(1+α)·H_r[c] + (1-α)·H_r[c+n/2]] / 2
         * If leaf is in the first half, coefficients are (1+α,1-α) for
         * (leaf, sib). If leaf is in the second half, they swap. */
        if (r + 1u < CHIPMUNK_FRI_ROUNDS) {
            uint32_t l_half = l_n / 2u;
            uint32_t l_leaf_idx = l_qry->idx % l_n;

            int32_t l_1pa = (1 + alphas[r]) % (int32_t)CHIPMUNK_Q;
            int32_t l_1ma = (1 - alphas[r] + (int32_t)CHIPMUNK_Q) % (int32_t)CHIPMUNK_Q;

            /* Choose coefficients based on which half the leaf is in. */
            int32_t l_cfirst, l_csecond;
            if (l_leaf_idx < l_half) {
                l_cfirst  = l_1pa;   /* (1+α) for H_r[leaf]   */
                l_csecond = l_1ma;   /* (1-α) for H_r[sibling] */
            } else {
                l_cfirst  = l_1ma;   /* (1-α) for H_r[leaf]   */
                l_csecond = l_1pa;   /* (1+α) for H_r[sibling] */
            }

            int64_t l_sum = (int64_t)l_cfirst  * (int64_t)l_leaf
                          + (int64_t)l_csecond * (int64_t)l_sib;
            l_sum = l_sum % (int64_t)CHIPMUNK_Q;
            if (l_sum < 0) l_sum += (int64_t)CHIPMUNK_Q;
            int32_t l_folded = (int32_t)(l_sum * (int64_t)l_inv2 % (int64_t)CHIPMUNK_Q);
            if (l_folded < 0) l_folded += (int32_t)CHIPMUNK_Q;

            if (l_folded != l_qry->leaf_values[r + 1u])
                return false;
        }
    }

    return true;
}
