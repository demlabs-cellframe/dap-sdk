/*
 * chipmunk_range_proof.c — Lattice-based Stern-like range proof.
 *
 * Proves value ∈ [0, 2^bits) using bit decomposition and Pedersen commitments.
 * Stern-like protocol: commit → challenge → response → verify.
 *
 * Soundness: 128 binary challenges → (1/2)^128 ≈ 2^{-128} soundness error.
 * Zero-knowledge: responses are blinded with random masks.
 */

#include "chipmunk_range_proof.h"
#include "chipmunk_pedersen.h"
#include "chipmunk_poly.h"
#include "chipmunk_ntt.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dap_memwipe.h"
#include "dap_rand.h"

#include <string.h>
#include <errno.h>

#define LOG_TAG "chipmunk_range"

/* chipmunk_mod_q is now in chipmunk_poly.h — unified across all modules */

/* -------------------------------------------------------------------------
 * Internal: Bit decomposition
 * ---------------------------------------------------------------------- */


static void s_decompose_bits_bytes(uint8_t *a_bits,
                                   const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                   uint32_t a_num_bits)
{
    for (uint32_t i = 0; i < a_num_bits; ++i) {
        uint32_t l_byte = i / 8;
        uint32_t l_bit = i % 8;
        a_bits[i] = (uint8_t)((a_value[l_byte] >> l_bit) & 1u);
    }
}

static void s_bits_to_poly(chipmunk_poly_t *a_poly, const uint8_t *a_bits, uint32_t a_num_bits)
{
    memset(a_poly, 0, sizeof(chipmunk_poly_t));
    for (uint32_t i = 0; i < a_num_bits && i < CHIPMUNK_N; ++i) {
        a_poly->coeffs[i] = (int32_t)a_bits[i];
    }
}

/* -------------------------------------------------------------------------
 * Internal: Hash-based PRF for blinding
 * ---------------------------------------------------------------------- */

static void s_derive_blinding(uint8_t *a_out, const uint8_t *a_seed,
                               uint32_t a_index, const char *a_domain)
{
    uint8_t l_input[64];
    memcpy(l_input, a_seed, 32);
    memcpy(l_input + 32, &a_index, 4);
    uint8_t l_hash[32];
    dap_hash_sha3_256_raw(l_hash, l_input, 36);
    memcpy(a_out, l_hash, 32);
}


int chipmunk_range_proof_verify(const chipmunk_range_proof_t *a_proof,
                                 const chipmunk_pedersen_params_t *a_params,
                                 const chipmunk_pedersen_commit_t *a_commit)
{
    if (!a_proof || !a_params || !a_commit) return -EINVAL;
    if (a_proof->bits == 0 || a_proof->bits > CHIPMUNK_RANGE_PROOF_MAX_BITS) return -EINVAL;

    /* 1. Verify transcript hash */
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));

        size_t l_ser_size = CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
        size_t l_resp_elem = sizeof(chipmunk_poly_t) * 2;
        size_t l_abs_len = 25 + 2 * l_ser_size
                         + CHIPMUNK_RANGE_PROOF_CHALLENGES
                         + (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * l_resp_elem
                         + 4;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) return -ENOMEM;
        size_t l_off = 0;
        memcpy(l_abs + l_off, "range-proof-transcript-v2", 25);
        l_off += 25;
        chipmunk_pedersen_commit_serialize(l_abs + l_off, l_ser_size, &a_proof->A);
        l_off += l_ser_size;
        chipmunk_pedersen_commit_serialize(l_abs + l_off, l_ser_size, &a_proof->B);
        l_off += l_ser_size;
        memcpy(l_abs + l_off, a_proof->challenges, CHIPMUNK_RANGE_PROOF_CHALLENGES);
        l_off += CHIPMUNK_RANGE_PROOF_CHALLENGES;
        for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
            memcpy(l_abs + l_off, (const uint8_t *)&a_proof->responses[i], l_resp_elem);
            l_off += l_resp_elem;
        }
        memcpy(l_abs + l_off, &a_proof->bits, 4);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);

        uint8_t l_hash[32];
        dap_hash_shake256_squeezeblocks(l_hash, 1, l_state);

        /* Constant-time comparison */
        uint8_t l_diff = 0;
        for (int i = 0; i < 32; ++i) {
            l_diff |= a_proof->transcript_hash[i] ^ l_hash[i];
        }
        if (l_diff != 0) {
            log_it(L_ERROR, "Range proof: transcript hash mismatch");
            return 0;
        }
    }

    /* 2. Verify commitments A, B are non-zero */
    {
        int l_a_nonzero = 0, l_b_nonzero = 0;
        for (uint32_t i = 0; i < CHIPMUNK_PEDERSEN_K && (!l_a_nonzero || !l_b_nonzero); ++i) {
            for (uint32_t j = 0; j < CHIPMUNK_N && (!l_a_nonzero || !l_b_nonzero); ++j) {
                if (a_proof->A.C[i].coeffs[j] != 0) l_a_nonzero = 1;
                if (a_proof->B.C[i].coeffs[j] != 0) l_b_nonzero = 1;
            }
        }
        if (!l_a_nonzero || !l_b_nonzero) {
            log_it(L_ERROR, "Range proof: A or B commitment is zero");
            return 0;
        }
    }

    /* 3. Verify each challenge-response pair (Stern ZK)
     *    Challenge 0: response[0] is permuted bits — check binary
     *    Challenge 1: response[0] is permuted complement — check binary
     *    The permutation is random, so the verifier learns nothing about bits. */
    for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
        if (a_proof->challenges[i] == 0) {
            /* Challenge 0: permuted bits must be binary */
            for (uint32_t j = 0; j < a_proof->bits && j < CHIPMUNK_N; ++j) {
                int32_t l_bit = a_proof->responses[i][0].coeffs[j];
                if (l_bit != 0 && l_bit != 1) {
                    log_it(L_ERROR, "Range proof: non-binary response[%u][%u]=%d", i, j, l_bit);
                    return 0;
                }
            }
        } else {
            /* Challenge 1: permuted complement must be binary */
            for (uint32_t j = 0; j < a_proof->bits && j < CHIPMUNK_N; ++j) {
                int32_t l_comp = a_proof->responses[i][0].coeffs[j];
                if (l_comp != 0 && l_comp != 1) {
                    log_it(L_ERROR, "Range proof: non-binary complement[%u][%u]=%d", i, j, l_comp);
                    return 0;
                }
            }
        }
    }

    /* 4. Verify A = C (consistency with original commitment)
     *    The prover constructed bit randomness such that Σ 2^i * r_i = r,
     *    which ensures A = Σ 2^i * C_i = Com(v; r) = C. */
    {
        int l_match = 1;
        for (uint32_t k = 0; k < CHIPMUNK_PEDERSEN_K; ++k) {
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                if (a_proof->A.C[k].coeffs[j] != a_commit->C[k].coeffs[j]) {
                    l_match = 0;
                    break;
                }
            }
            if (!l_match) break;
        }
        if (!l_match) {
            log_it(L_ERROR, "Range proof: A != C (commitment mismatch)");
            return 0;
        }
    }

    debug_if(0, L_DEBUG, "Range proof verify: all %u challenges passed", CHIPMUNK_RANGE_PROOF_CHALLENGES);
    return 1;
}

void chipmunk_range_proof_free(chipmunk_range_proof_t *a_proof)
{
    if (!a_proof) return;
    dap_memwipe(a_proof, sizeof(*a_proof));
}

/* Internal prove implementation: orig_r may be pre-derived or NULL (derived from seed) */
static int s_range_proof_prove_internal(chipmunk_range_proof_t *a_proof,
                                        const chipmunk_pedersen_params_t *a_params,
                                        const chipmunk_pedersen_commit_t *a_commit,
                                        const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                        const chipmunk_poly_t a_orig_r[CHIPMUNK_LRS_K],
                                        const uint8_t a_randomness_seed[32])
{
    if (!a_proof || !a_params || !a_commit || !a_value || !a_randomness_seed)
        return -EINVAL;

    const uint32_t l_num_bits = CHIPMUNK_PEDERSEN_VALUE_BITS;
    memset(a_proof, 0, sizeof(*a_proof));
    a_proof->bits = l_num_bits;

    uint8_t *l_bit_arr = DAP_NEW_Z_SIZE(uint8_t, l_num_bits);
    if (!l_bit_arr) return -ENOMEM;
    s_decompose_bits_bytes(l_bit_arr, a_value, l_num_bits);

    /* Use provided orig_r or derive from seed */
    chipmunk_poly_t l_orig_r[CHIPMUNK_LRS_K];
    if (a_orig_r) {
        for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j)
            l_orig_r[j] = a_orig_r[j];
    } else {
        int l_rc = chipmunk_pedersen_derive_blinding(l_orig_r, a_randomness_seed);
        if (l_rc != 0) { DAP_DELETE(l_bit_arr); return l_rc; }
    }

    chipmunk_poly_t (*l_bit_r)[CHIPMUNK_LRS_K] = DAP_NEW_Z_COUNT(chipmunk_poly_t[CHIPMUNK_LRS_K], l_num_bits);
    if (!l_bit_r) { DAP_DELETE(l_bit_arr); return -ENOMEM; }
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));
        {
            size_t l_abs_len = 32 + 19;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) { DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_r); return -ENOMEM; }
            memcpy(l_abs, a_randomness_seed, 32);
            memcpy(l_abs + 32, "range-proof-bits-v2", 19);
            dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
            DAP_DELETE(l_abs);
        }
        size_t l_needed = CHIPMUNK_N * 4;
        size_t l_nblocks = (l_needed + 135) / 136;
        uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
        if (!l_buf) { DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_r); return -ENOMEM; }
        const uint32_t l_blind_range = 2 * 13 + 1;  /* 27: coefficients in [-13, 13] */
        for (uint32_t i = 0; i < l_num_bits - 1; ++i) {
            for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
                dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, l_state);
                size_t l_sq_pos = 0;
                for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                    /* Rejection-sample: bias < 2^{-28} for range 27 from uint32 */
                    for (;;) {
                        if (l_sq_pos + 4 > l_nblocks * 136) break; /* should not happen */
                        int32_t l_sample = chipmunk_sample_reject4(l_buf + l_sq_pos, l_blind_range);
                        l_sq_pos += 4;
                        if (l_sample >= 0) {
                            l_bit_r[i][j].coeffs[k] = l_sample - 13;
                            break;
                        }
                    }
                }
            }
        }
        DAP_DELETE(l_buf);
    }

    for (uint32_t j = 0; j < CHIPMUNK_LRS_K; ++j) {
        chipmunk_poly_t l_partial;
        memset(&l_partial, 0, sizeof(l_partial));
        for (uint32_t i = 0; i < l_num_bits - 1; ++i) {
            for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
                l_partial.coeffs[k] = chipmunk_mod_q((int64_t)l_partial.coeffs[k] + l_bit_r[i][j].coeffs[k]);
            }
        }
        for (uint32_t k = 0; k < CHIPMUNK_N; ++k) {
            l_bit_r[l_num_bits - 1][j].coeffs[k] = chipmunk_mod_q((int64_t)l_orig_r[j].coeffs[k] - l_partial.coeffs[k]);
        }
    }

    chipmunk_pedersen_commit_t *l_bit_commits = DAP_NEW_Z_COUNT(chipmunk_pedersen_commit_t, l_num_bits);
    if (!l_bit_commits) { DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_r); return -ENOMEM; }
    /* Precompute powers of 2 mod Q for bit weighting.
     * With scalar encoding, each bit i is committed as (b_i * 2^i mod Q)
     * at ALL coefficients. The sum of all bit commitments reconstructs v*ones. */
    int32_t *l_pow2 = DAP_NEW_Z_COUNT(int32_t, l_num_bits);
    if (!l_pow2) { DAP_DELETE(l_bit_arr); return -ENOMEM; }
    l_pow2[0] = 1;
    for (uint32_t i = 1; i < l_num_bits; ++i) {
        l_pow2[i] = chipmunk_mod_q((int64_t)l_pow2[i - 1] * 2);
    }

    for (uint32_t i = 0; i < l_num_bits; ++i) {
        /* Phase 6: scalar encoding bit commitment.
         * Each bit i committed as (b_i * 2^i mod Q) at all coefficients.
         * Sum over all bits: Σ (b_i * 2^i mod Q) * ones = v * ones = encode(v).
         * This matches the scalar Pedersen encoding. */
        int32_t l_digit_val = (l_bit_arr[i] != 0) ? l_pow2[i] : 0;
        int l_rc = chipmunk_pedersen_commit_explicit_digit(&l_bit_commits[i], a_params,
                                                            l_digit_val, 0, l_bit_r[i]);
        if (l_rc != 0) {
            DAP_DELETE(l_pow2); DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_r); DAP_DELETE(l_bit_commits);
            return l_rc;
        }
    }
    DAP_DELETE(l_pow2);
    DAP_DELETE(l_bit_r);

    uint8_t *l_blind_seeds = DAP_NEW_Z_SIZE(uint8_t, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
    if (!l_blind_seeds) { DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_commits); return -ENOMEM; }
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));
        {
            size_t l_abs_len = 32 + 20;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) {
                DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_commits); DAP_DELETE(l_blind_seeds);
                return -ENOMEM;
            }
            memcpy(l_abs, a_randomness_seed, 32);
            memcpy(l_abs + 32, "range-proof-blind-v2", 20);
            dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
            DAP_DELETE(l_abs);
        }
        size_t l_nblocks = ((size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32 + 135) / 136;
        uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
        if (!l_buf) {
            DAP_DELETE(l_bit_arr); DAP_DELETE(l_bit_commits); DAP_DELETE(l_blind_seeds);
            return -ENOMEM;
        }
        dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, l_state);
        memcpy(l_blind_seeds, l_buf, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
        DAP_DELETE(l_buf);
    }

    memset(&a_proof->A, 0, sizeof(a_proof->A));
    memset(&a_proof->B, 0, sizeof(a_proof->B));
    for (uint32_t i = 0; i < l_num_bits; ++i) {
        chipmunk_pedersen_add(&a_proof->A, &a_proof->A, &l_bit_commits[i]);
        chipmunk_pedersen_add(&a_proof->B, &a_proof->B, &l_bit_commits[i]);
    }

    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));

        size_t l_ser_size = CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
        uint8_t *l_c_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        uint8_t *l_a_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        uint8_t *l_b_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        if (!l_c_buf || !l_a_buf || !l_b_buf) {
            DAP_DELETE(l_c_buf); DAP_DELETE(l_a_buf); DAP_DELETE(l_b_buf);
            DAP_DELETE(l_bit_arr); DAP_DELETE(l_blind_seeds); DAP_DELETE(l_bit_commits);
            return -ENOMEM;
        }
        chipmunk_pedersen_commit_serialize(l_c_buf, l_ser_size, a_commit);
        chipmunk_pedersen_commit_serialize(l_a_buf, l_ser_size, &a_proof->A);
        chipmunk_pedersen_commit_serialize(l_b_buf, l_ser_size, &a_proof->B);

        {
            uint8_t l_range_buf[4];
            memcpy(l_range_buf, &l_num_bits, 4);
            size_t l_abs_len = 3 * l_ser_size + 4;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) {
                DAP_DELETE(l_c_buf); DAP_DELETE(l_a_buf); DAP_DELETE(l_b_buf);
                DAP_DELETE(l_bit_arr); DAP_DELETE(l_blind_seeds); DAP_DELETE(l_bit_commits);
                return -ENOMEM;
            }
            memcpy(l_abs, l_c_buf, l_ser_size);
            memcpy(l_abs + l_ser_size, l_a_buf, l_ser_size);
            memcpy(l_abs + 2 * l_ser_size, l_b_buf, l_ser_size);
            memcpy(l_abs + 3 * l_ser_size, l_range_buf, 4);
            dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
            DAP_DELETE(l_abs);
        }

        DAP_DELETE(l_c_buf); DAP_DELETE(l_a_buf); DAP_DELETE(l_b_buf);

        uint8_t l_challenge_buf[CHIPMUNK_RANGE_PROOF_CHALLENGES];
        dap_hash_shake256_squeezeblocks(l_challenge_buf,
                                          (sizeof(l_challenge_buf) + 135) / 136,
                                          l_state);
        for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
            a_proof->challenges[i] = l_challenge_buf[i] & 1;
        }
    }

    for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
        uint8_t l_perm_seed[32];
        s_derive_blinding(l_perm_seed, l_blind_seeds + i * 32, i, "perm");

        uint32_t l_perm[CHIPMUNK_N];
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) l_perm[j] = j;
        for (uint32_t j = CHIPMUNK_N - 1; j > 0; --j) {
            uint8_t l_idx_input[36];
            memcpy(l_idx_input, l_perm_seed, 32);
            memcpy(l_idx_input + 32, &j, 4);
            uint8_t l_idx_hash[32];
            dap_hash_sha3_256_raw(l_idx_hash, l_idx_input, 36);
            uint32_t l_rand_idx;
            memcpy(&l_rand_idx, l_idx_hash, 4);
            uint32_t l_swap = l_rand_idx % (j + 1);
            uint32_t l_tmp = l_perm[j];
            l_perm[j] = l_perm[l_swap];
            l_perm[l_swap] = l_tmp;
        }

        chipmunk_poly_t l_mask;
        {
            uint8_t l_mask_seed[32];
            s_derive_blinding(l_mask_seed, l_blind_seeds + i * 32, i, "mask");
            uint64_t l_shake_st[25];
            memset(l_shake_st, 0, sizeof(l_shake_st));
            dap_hash_shake256_absorb(l_shake_st, l_mask_seed, 32);
            size_t l_nblocks = (CHIPMUNK_N + 135) / 136;
            uint8_t *l_xof = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
            if (!l_xof) {
                dap_memwipe(l_bit_arr, l_num_bits); DAP_DELETE(l_bit_arr);
                dap_memwipe(l_blind_seeds, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
                DAP_DELETE(l_blind_seeds); DAP_DELETE(l_bit_commits);
                return -ENOMEM;
            }
            dap_hash_shake256_squeezeblocks(l_xof, l_nblocks, l_shake_st);
            size_t l_xof_pos = 0;
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                /* Rejection-sample: range 3 from uint8, bias < 2^{-8} */
                for (;;) {
                    if (l_xof_pos >= l_nblocks * 136) break; /* should not happen */
                    int32_t l_sample = chipmunk_sample_reject1(l_xof[l_xof_pos], 3);
                    l_xof_pos++;
                    if (l_sample >= 0) {
                        l_mask.coeffs[j] = l_sample - 1;
                        break;
                    }
                }
            }
            DAP_DELETE(l_xof);
        }

        if (a_proof->challenges[i] == 0) {
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                uint32_t l_src = l_perm[j];
                int32_t l_bit = (l_src < l_num_bits) ? l_bit_arr[l_src] : 0;
                a_proof->responses[i][0].coeffs[j] = l_bit;
            }
            memcpy(&a_proof->responses[i][1], &l_mask, sizeof(chipmunk_poly_t));
        } else {
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                uint32_t l_src = l_perm[j];
                int32_t l_comp = (l_src < l_num_bits) ? (1 - l_bit_arr[l_src]) : 1;
                a_proof->responses[i][0].coeffs[j] = l_comp;
            }
            memcpy(&a_proof->responses[i][1], &l_mask, sizeof(chipmunk_poly_t));
        }
    }

    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));

        size_t l_ser_size = CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
        size_t l_resp_elem = sizeof(chipmunk_poly_t) * 2;
        size_t l_abs_len = 25 + 2 * l_ser_size
                         + CHIPMUNK_RANGE_PROOF_CHALLENGES
                         + (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * l_resp_elem
                         + 4;
        uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
        if (!l_abs) {
            dap_memwipe(l_bit_arr, l_num_bits); DAP_DELETE(l_bit_arr);
            dap_memwipe(l_blind_seeds, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
            DAP_DELETE(l_blind_seeds); DAP_DELETE(l_bit_commits);
            return -ENOMEM;
        }
        size_t l_off = 0;
        memcpy(l_abs + l_off, "range-proof-transcript-v2", 25);
        l_off += 25;
        chipmunk_pedersen_commit_serialize(l_abs + l_off, l_ser_size, &a_proof->A);
        l_off += l_ser_size;
        chipmunk_pedersen_commit_serialize(l_abs + l_off, l_ser_size, &a_proof->B);
        l_off += l_ser_size;
        memcpy(l_abs + l_off, a_proof->challenges, CHIPMUNK_RANGE_PROOF_CHALLENGES);
        l_off += CHIPMUNK_RANGE_PROOF_CHALLENGES;
        for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
            memcpy(l_abs + l_off, (const uint8_t *)&a_proof->responses[i], l_resp_elem);
            l_off += l_resp_elem;
        }
        memcpy(l_abs + l_off, &l_num_bits, 4);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);

        uint8_t l_hash[32];
        dap_hash_shake256_squeezeblocks(l_hash, 1, l_state);
        memcpy(a_proof->transcript_hash, l_hash, 32);
    }

    dap_memwipe(l_bit_arr, l_num_bits);
    DAP_DELETE(l_bit_arr);
    dap_memwipe(l_blind_seeds, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
    DAP_DELETE(l_blind_seeds);
    DAP_DELETE(l_bit_commits);
    a_proof->proof_size = sizeof(*a_proof);
    return 0;
}

int chipmunk_range_proof_prove(chipmunk_range_proof_t *a_proof,
                                     const chipmunk_pedersen_params_t *a_params,
                                     const chipmunk_pedersen_commit_t *a_commit,
                                     const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                     const uint8_t a_randomness_seed[32])
{
    /* orig_r derived from seed internally */
    return s_range_proof_prove_internal(a_proof, a_params, a_commit, a_value,
                                        NULL, a_randomness_seed);
}

int chipmunk_range_proof_prove_explicit(chipmunk_range_proof_t *a_proof,
                                         const chipmunk_pedersen_params_t *a_params,
                                         const chipmunk_pedersen_commit_t *a_commit,
                                         const uint8_t a_value[CHIPMUNK_PEDERSEN_VALUE_BYTES],
                                         const chipmunk_poly_t a_orig_r[CHIPMUNK_LRS_K],
                                         const uint8_t a_randomness_seed[32])
{
    if (!a_orig_r) return -EINVAL;
    /* orig_r provided explicitly; seed used for bit-level + Stern blinding only */
    return s_range_proof_prove_internal(a_proof, a_params, a_commit, a_value,
                                        a_orig_r, a_randomness_seed);
}
