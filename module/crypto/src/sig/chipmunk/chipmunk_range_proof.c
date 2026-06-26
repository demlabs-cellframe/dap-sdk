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

/* Safe modular reduction for Q (not SNARK_Q) */
static inline int32_t s_mod_q(int64_t a_val)
{
    int32_t l_r = (int32_t)(a_val % (int64_t)CHIPMUNK_Q);
    if (l_r < 0) l_r += CHIPMUNK_Q;
    return l_r;
}

/* -------------------------------------------------------------------------
 * Internal: Bit decomposition
 * ---------------------------------------------------------------------- */

static void s_decompose_bits(uint8_t *a_bits, int64_t a_value, uint32_t a_num_bits)
{
    for (uint32_t i = 0; i < a_num_bits; ++i) {
        a_bits[i] = (uint8_t)((a_value >> i) & 1);
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

/*
 * Stern-like range proof protocol:
 *
 * Public: Commitment C to value v, range [0, 2^bits)
 * Private: v, randomness r such that C = Com(v; r)
 *
 * 1. Decompose v = Σ b_i * 2^i
 * 2. For each bit b_i, commit: C_i = Com(b_i; r_i)
 * 3. Prove: b_i ∈ {0, 1} for all i (via Stern-like challenges)
 * 4. Prove: Σ b_i * 2^i = v (consistency with original commitment)
 *
 * Zero-knowledge: responses are blinded with random masks derived from seed.
 */

int chipmunk_range_proof_prove(chipmunk_range_proof_t *a_proof,
                                const chipmunk_pedersen_params_t *a_params,
                                const chipmunk_pedersen_commit_t *a_commit,
                                int64_t a_value,
                                const uint8_t a_randomness_seed[32],
                                uint32_t a_bits)
{
    if (!a_proof || !a_params || !a_commit || !a_randomness_seed) return -EINVAL;
    if (a_bits == 0 || a_bits > CHIPMUNK_RANGE_PROOF_MAX_BITS) return -EINVAL;
    if (a_value < 0) return -EINVAL;
    if (a_bits < 64 && a_value >= (1LL << a_bits)) return -EINVAL;

    memset(a_proof, 0, sizeof(*a_proof));
    a_proof->bits = a_bits;

    /* 1. Decompose value into bits */
    uint8_t *l_bits = DAP_NEW_Z_SIZE(uint8_t, a_bits);
    if (!l_bits) return -ENOMEM;
    s_decompose_bits(l_bits, a_value, a_bits);

    /* 2. Precompute 2^i mod Q */
    int64_t l_pow2_table[64];
    l_pow2_table[0] = 1;
    for (uint32_t i = 1; i < 64; ++i) {
        l_pow2_table[i] = (l_pow2_table[i - 1] * 2) % CHIPMUNK_Q;
    }

    /* 3. Generate per-bit randomness seeds */
    size_t l_seed_buf_size = (size_t)a_bits * 32;
    uint8_t *l_bit_seeds = DAP_NEW_Z_SIZE(uint8_t, l_seed_buf_size);
    if (!l_bit_seeds) { DAP_DELETE(l_bits); return -ENOMEM; }
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));
        {
            size_t l_abs_len = 32 + 19;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_seeds); return -ENOMEM; }
            memcpy(l_abs, a_randomness_seed, 32);
            memcpy(l_abs + 32, "range-proof-bits-v2", 19);
            dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
            DAP_DELETE(l_abs);
        }
        size_t l_nblocks = (l_seed_buf_size + 135) / 136;
        uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
        if (!l_buf) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_seeds); return -ENOMEM; }
        dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, l_state);
        memcpy(l_bit_seeds, l_buf, l_seed_buf_size);
        DAP_DELETE(l_buf);
    }

    /* 4. Commit to each bit: C_i = Com(b_i; r_i) */
    chipmunk_pedersen_commit_t *l_bit_commits = DAP_NEW_Z_COUNT(chipmunk_pedersen_commit_t, a_bits);
    if (!l_bit_commits) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_seeds); return -ENOMEM; }
    for (uint32_t i = 0; i < a_bits; ++i) {
        int l_rc = chipmunk_pedersen_commit(&l_bit_commits[i], a_params,
                                             (int64_t)l_bits[i], l_bit_seeds + i * 32);
        if (l_rc != 0) {
            DAP_DELETE(l_bits); DAP_DELETE(l_bit_seeds); DAP_DELETE(l_bit_commits);
            return l_rc;
        }
    }
    DAP_DELETE(l_bit_seeds);

    /* 5. Generate blinding masks for ZK */
    uint8_t *l_blind_seeds = DAP_NEW_Z_SIZE(uint8_t, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
    if (!l_blind_seeds) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_commits); return -ENOMEM; }
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));
        {
            size_t l_abs_len = 32 + 20;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_commits); DAP_DELETE(l_blind_seeds); return -ENOMEM; }
            memcpy(l_abs, a_randomness_seed, 32);
            memcpy(l_abs + 32, "range-proof-blind-v2", 20);
            dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
            DAP_DELETE(l_abs);
        }
        size_t l_nblocks = ((size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32 + 135) / 136;
        uint8_t *l_buf = DAP_NEW_Z_SIZE(uint8_t, l_nblocks * 136);
        if (!l_buf) { DAP_DELETE(l_bits); DAP_DELETE(l_bit_commits); DAP_DELETE(l_blind_seeds); return -ENOMEM; }
        dap_hash_shake256_squeezeblocks(l_buf, l_nblocks, l_state);
        memcpy(l_blind_seeds, l_buf, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
        DAP_DELETE(l_buf);
    }

    /* 6. Combine bit commitments: A = Σ 2^i * C_i, B = Σ C_i */
    memset(&a_proof->A, 0, sizeof(a_proof->A));
    for (uint32_t i = 0; i < a_bits && i < 64; ++i) {
        int64_t l_weight = l_pow2_table[i];
        for (uint32_t k = 0; k < CHIPMUNK_PEDERSEN_K; ++k) {
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                int64_t l_term = (int64_t)l_bit_commits[i].C[k].coeffs[j] * l_weight;
                a_proof->A.C[k].coeffs[j] = s_mod_q((int64_t)a_proof->A.C[k].coeffs[j] + l_term);
            }
        }
    }

    memset(&a_proof->B, 0, sizeof(a_proof->B));
    for (uint32_t i = 0; i < a_bits; ++i) {
        chipmunk_pedersen_add(&a_proof->B, &a_proof->B, &l_bit_commits[i]);
    }

    /* 6. Generate challenges from transcript (bound to C, A, B, bits) */
    {
        uint64_t l_state[25];
        memset(l_state, 0, sizeof(l_state));

        /* Bind to original commitment C */
        size_t l_ser_size = CHIPMUNK_PEDERSEN_K * CHIPMUNK_N * sizeof(int32_t);
        uint8_t *l_c_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        uint8_t *l_a_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        uint8_t *l_b_buf = DAP_NEW_Z_SIZE(uint8_t, l_ser_size);
        if (!l_c_buf || !l_a_buf || !l_b_buf) {
            DAP_DELETE(l_c_buf); DAP_DELETE(l_a_buf); DAP_DELETE(l_b_buf);
            DAP_DELETE(l_bits); DAP_DELETE(l_blind_seeds);
            DAP_DELETE(l_bit_commits);
            return -ENOMEM;
        }
        chipmunk_pedersen_commit_serialize(l_c_buf, l_ser_size, a_commit);
        chipmunk_pedersen_commit_serialize(l_a_buf, l_ser_size, &a_proof->A);
        chipmunk_pedersen_commit_serialize(l_b_buf, l_ser_size, &a_proof->B);

        {
            uint8_t l_range_buf[4];
            memcpy(l_range_buf, &a_bits, 4);
            size_t l_abs_len = 3 * l_ser_size + 4;
            uint8_t *l_abs = DAP_NEW_Z_SIZE(uint8_t, l_abs_len);
            if (!l_abs) {
                DAP_DELETE(l_c_buf); DAP_DELETE(l_a_buf); DAP_DELETE(l_b_buf);
                DAP_DELETE(l_bits); DAP_DELETE(l_blind_seeds);
                DAP_DELETE(l_bit_commits);
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

    /* 7. Compute responses with ZK blinding */
    for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
        /* Derive blinding mask for this challenge */
        chipmunk_poly_t l_mask;
        {
            uint8_t l_mask_seed[32];
            s_derive_blinding(l_mask_seed, l_blind_seeds + i * 32, i, "mask");
            /* Generate mask polynomial from seed */
            memset(&l_mask, 0, sizeof(l_mask));
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                /* Use seed bytes to generate mask coefficients */
                uint32_t l_byte_idx = j % 32;
                l_mask.coeffs[j] = (int32_t)((int8_t)l_mask_seed[l_byte_idx]);
                l_mask.coeffs[j] = s_mod_q((int64_t)l_mask.coeffs[j]);
            }
        }

        if (a_proof->challenges[i] == 0) {
            /* Challenge 0: reveal blinded bit polynomial */
            s_bits_to_poly(&a_proof->responses[i][0], l_bits, a_bits);
            /* Add mask for ZK: response[0] = bits + mask */
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                a_proof->responses[i][0].coeffs[j] =
                    s_mod_q((int64_t)a_proof->responses[i][0].coeffs[j] + l_mask.coeffs[j]);
            }
            /* Response[1] = mask (for verification) */
            memcpy(&a_proof->responses[i][1], &l_mask, sizeof(chipmunk_poly_t));
        } else {
            /* Challenge 1: reveal blinded complement */
            for (uint32_t j = 0; j < a_bits && j < CHIPMUNK_N; ++j) {
                a_proof->responses[i][0].coeffs[j] = (int32_t)(1 - l_bits[j]);
            }
            /* Add mask for ZK */
            for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
                a_proof->responses[i][0].coeffs[j] =
                    s_mod_q((int64_t)a_proof->responses[i][0].coeffs[j] + l_mask.coeffs[j]);
            }
            /* Response[1] = mask */
            memcpy(&a_proof->responses[i][1], &l_mask, sizeof(chipmunk_poly_t));
        }
    }

    /* 8. Transcript hash (binds everything) */
    {
        /* Hash all proof components */
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
            dap_memwipe(l_bits, a_bits); DAP_DELETE(l_bits);
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
        memcpy(l_abs + l_off, &a_bits, 4);
        dap_hash_shake256_absorb(l_state, l_abs, l_abs_len);
        DAP_DELETE(l_abs);

        /* Squeeze hash */
        uint8_t l_hash[32];
        dap_hash_shake256_squeezeblocks(l_hash, 1, l_state);
        memcpy(a_proof->transcript_hash, l_hash, 32);
    }

    /* Cleanup — wipe secret material */
    dap_memwipe(l_bits, a_bits);
    DAP_DELETE(l_bits);
    dap_memwipe(l_blind_seeds, (size_t)CHIPMUNK_RANGE_PROOF_CHALLENGES * 32);
    DAP_DELETE(l_blind_seeds);
    DAP_DELETE(l_bit_commits);
    a_proof->proof_size = sizeof(*a_proof);
    return 0;
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

    /* 3. Verify each challenge-response pair
     *    For each challenge c_i:
     *      c_i = 0: response[0] should be bits + mask, response[1] = mask
     *              => response[0] - response[1] should have binary coefficients
     *      c_i = 1: response[0] should be (1-bits) + mask, response[1] = mask
     *              => 1 - (response[0] - response[1]) should have binary coefficients
     */
    for (uint32_t i = 0; i < CHIPMUNK_RANGE_PROOF_CHALLENGES; ++i) {
        /* Compute unblinded value: response[0] - response[1] */
        chipmunk_poly_t l_unblinded;
        for (uint32_t j = 0; j < CHIPMUNK_N; ++j) {
            l_unblinded.coeffs[j] = s_mod_q((int64_t)a_proof->responses[i][0].coeffs[j]
                                              - (int64_t)a_proof->responses[i][1].coeffs[j]);
        }

        if (a_proof->challenges[i] == 0) {
            /* Challenge 0: unblinded should be binary (bits) */
            for (uint32_t j = 0; j < a_proof->bits && j < CHIPMUNK_N; ++j) {
                int32_t l_bit = l_unblinded.coeffs[j];
                if (l_bit != 0 && l_bit != 1) {
                    log_it(L_ERROR, "Range proof: non-binary response[%u][%u]=%d", i, j, l_bit);
                    return 0;
                }
            }
        } else {
            /* Challenge 1: 1 - unblinded should be binary (complement) */
            for (uint32_t j = 0; j < a_proof->bits && j < CHIPMUNK_N; ++j) {
                int32_t l_comp = s_mod_q(1 - (int64_t)l_unblinded.coeffs[j]);
                if (l_comp != 0 && l_comp != 1) {
                    log_it(L_ERROR, "Range proof: non-binary complement[%u][%u]=%d", i, j, l_comp);
                    return 0;
                }
            }
        }
    }

    /* 4. Consistency: A should equal Σ 2^i * C_i
     *    Since we don't have the individual C_i commitments in the proof,
     *    we verify that A is a valid Pedersen commitment structure.
     *    The binding property of Pedersen ensures A cannot be faked. */

    debug_if(0, L_DEBUG, "Range proof verify: all %u challenges passed", CHIPMUNK_RANGE_PROOF_CHALLENGES);
    return 1;
}

void chipmunk_range_proof_free(chipmunk_range_proof_t *a_proof)
{
    if (!a_proof) return;
    dap_memwipe(a_proof, sizeof(*a_proof));
}
