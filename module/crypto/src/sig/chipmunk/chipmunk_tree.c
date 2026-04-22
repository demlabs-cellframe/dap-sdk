/*
 * Authors:
 * Dmitry A. Gerasimov <ceo@cellframe.net>
 * DeM Labs Inc.   https://demlabs.net
 * DeM Labs Open source community https://gitlab.demlabs.net/cellframe
 * Copyright  (c) 2017-2026
 * All rights reserved.

 This file is part of DAP (Distributed Applications Platform) the open source project

    DAP (Distributed Applications Platform) is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    DAP is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with any DAP based project.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * =============================================================================
 * CR-D15.A (Round-5) rewrite of the HVC layer used by the Chipmunk Merkle
 * tree.  The historical implementation shipped three stacked breaks:
 *
 *   1.  `chipmunk_hvc_hasher_init` generated the public matrix A via
 *       `((seed[0] + i*1000 + j) * 1664525 + 1013904223) % q_hvc` — a
 *       mundane LCG whose state depends only on the first seed byte,
 *       making A predictable and the matrix rank trivial.
 *
 *   2.  `chipmunk_hvc_hash_decom_then_hash` implemented the Merkle
 *       combiner as `h(x, y) := x + y mod q_hvc` — a *linear* function
 *       where collisions are handed out for free (h(x, y) = h(x + t,
 *       y − t) for every t in Rq).  Every tree built on top of this was
 *       essentially a one-element hash chain.
 *
 *   3.  `chipmunk_hots_pk_to_hvc_poly` folded (v0, v1) into a SHA3-256
 *       block and then expanded the block with a textbook LCG — again
 *       giving an attacker sub-linear control over the leaf digest and
 *       no domain separation against the hasher.
 *
 * In addition `chipmunk_path_verify` only checked the first level
 * (the rest was labelled `TODO`), so any interior forgery passed.  The
 * honest rewrite below replaces all four pieces with:
 *
 *   -  A proper Ajtai lattice hash
 *         h(x, y) = Σ_{i=0}^{W-1} A_left[i]·D(x)[i] + A_right[i]·D(y)[i]
 *      where D is a signed-digit base-(2·ZETA+1) = 59 decomposition that
 *      keeps every piece in [−ZETA, +ZETA] and A_{left,right} ∈ Rq^W
 *      are rejection-sampled from SHAKE128(seed || domain || i).
 *   -  Polynomial arithmetic in R_q = Z_q[X]/(X^N + 1) via an explicit
 *      schoolbook convolution with the negacyclic reduction x^N = −1
 *      folded in.  The HVC prime 202753 is 2N-friendly (202753 mod 1024
 *      = 1), so a future NTT swap-in is possible without touching this
 *      file — see CR-D15.A performance follow-up.
 *   -  A domain-separated SHAKE128 digest of the full HOTS public key
 *      (ρ_seed ‖ v0 ‖ v1) that materialises an HVC polynomial whose
 *      coefficients are uniform in Z_q^HVC.  This is the leaf that will
 *      be Merkle-hashed, so its collision resistance is what ultimately
 *      pins signer identities.
 *   -  A full path verifier that walks every level top-to-bottom, pins
 *      the leaf to the parity-correct side of the lowest level, and
 *      requires strict equality with the stored root.
 * =============================================================================
 */

#include "chipmunk_tree.h"
#include "chipmunk.h"
#include "chipmunk_hash.h"
#include "dap_common.h"
#include "dap_hash_shake128.h"
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>

#define LOG_TAG "chipmunk_tree"

// ============================================================================
// HVC NTT (q = 202753, N = 512)
//
// q is NTT-friendly: q - 1 = 2^11 * 3^2 * 11, so a primitive 2N = 1024-th root
// of unity exists in F_q.  We run a full radix-2 Cooley–Tukey NTT down to
// butterfly length 1 and use the Dilithium-style evaluation convention
//    â[i] = a(ω^{2·BRV(i)+1})
// so that pointwise multiplication implements negacyclic convolution in
// R_q = F_q[X]/(X^N + 1) without any post-mul correction.  No Montgomery
// domain is needed — q fits comfortably in int32 and (int64_t)a·b % q costs
// around one CPU cycle per mul on current x86 cores, which is plenty for the
// tree depths we use (CR-D15.A tests exercise tens to hundreds of non-leaf
// hashes, not millions).
// ============================================================================

#define S_HVC_LOG_N           9u                // log2(N) = 9
#define S_HVC_Q32             ((int32_t)CHIPMUNK_HVC_Q)

static int32_t s_hvc_zetas[CHIPMUNK_N];         // zetas[k] = ω^{BRV_logN(k)} mod q
static int32_t s_hvc_zetas_inv[CHIPMUNK_N];     // inverse-NTT twiddles (same BRV layout, ω^{-1})
static int32_t s_hvc_ninv;                      // N^{-1} mod q (applied after invNTT)
static atomic_int s_hvc_ntt_state = 0;          // 0 = uninitialised, 1 = initialising, 2 = ready
static pthread_mutex_t s_hvc_ntt_init_mutex = PTHREAD_MUTEX_INITIALIZER;

static inline uint32_t s_hvc_modpow_u(uint32_t a_base, uint32_t a_exp)
{
    uint64_t l_result = 1;
    uint64_t l_b = a_base % (uint32_t)CHIPMUNK_HVC_Q;
    while (a_exp) {
        if (a_exp & 1u) {
            l_result = (l_result * l_b) % (uint32_t)CHIPMUNK_HVC_Q;
        }
        l_b = (l_b * l_b) % (uint32_t)CHIPMUNK_HVC_Q;
        a_exp >>= 1;
    }
    return (uint32_t)l_result;
}

static inline uint32_t s_hvc_brv9(uint32_t a_x)
{
    uint32_t l_r = 0;
    for (unsigned i = 0; i < S_HVC_LOG_N; ++i) {
        l_r = (l_r << 1) | (a_x & 1u);
        a_x >>= 1;
    }
    return l_r;
}

// Modular multiplication returning a value in [0, q).  Hot path — kept inline.
static inline int32_t s_hvc_fqmul(int32_t a_a, int32_t a_b)
{
    int64_t l_t = (int64_t)a_a * (int64_t)a_b;
    int32_t l_r = (int32_t)(l_t % S_HVC_Q32);
    if (l_r < 0) l_r += S_HVC_Q32;
    return l_r;
}

// Canonicalise a signed value into [0, q).  Only used outside the hot NTT loop.
static inline int32_t s_hvc_freduce(int32_t a_x)
{
    int32_t l_r = a_x % S_HVC_Q32;
    if (l_r < 0) l_r += S_HVC_Q32;
    return l_r;
}

// Locate a primitive 2N-th root of unity by probing small candidates.
// The multiplicative group has order q - 1 = 2^11 · 3^2 · 11, so the first
// generator we try (2) already has a 2N-th-root shadow; if it happens not to
// work for some exotic q change in the future, we gracefully fall through to
// larger bases up to 100.
static int s_hvc_find_omega_2n(uint32_t *a_out_omega)
{
    uint32_t l_exp = ((uint32_t)CHIPMUNK_HVC_Q - 1u) / (2u * (uint32_t)CHIPMUNK_N);
    for (uint32_t l_g = 2; l_g < 100; ++l_g) {
        uint32_t l_o = s_hvc_modpow_u(l_g, l_exp);
        if (l_o <= 1) {
            continue;
        }
        // Need omega^N == q - 1 (i.e. -1 mod q).  If so omega^{2N} = 1 and the
        // order divides 2N; combined with omega != 1 and omega^N = -1, the
        // order is exactly 2N.
        if (s_hvc_modpow_u(l_o, (uint32_t)CHIPMUNK_N) == (uint32_t)CHIPMUNK_HVC_Q - 1u) {
            *a_out_omega = l_o;
            return 0;
        }
    }
    return -1;
}

static int s_hvc_ntt_init(void)
{
    int l_state = atomic_load_explicit(&s_hvc_ntt_state, memory_order_acquire);
    if (l_state == 2) {
        return 0;
    }

    pthread_mutex_lock(&s_hvc_ntt_init_mutex);
    l_state = atomic_load_explicit(&s_hvc_ntt_state, memory_order_acquire);
    if (l_state == 2) {
        pthread_mutex_unlock(&s_hvc_ntt_init_mutex);
        return 0;
    }

    uint32_t l_omega = 0;
    if (s_hvc_find_omega_2n(&l_omega) != 0) {
        pthread_mutex_unlock(&s_hvc_ntt_init_mutex);
        log_it(L_CRITICAL, "HVC NTT: no primitive 2N-th root of unity found in F_q (q=%d)",
               CHIPMUNK_HVC_Q);
        return CHIPMUNK_ERROR_INTERNAL;
    }

    // omega^{-1} = omega^{2N - 1}
    uint32_t l_omega_inv = s_hvc_modpow_u(l_omega, 2u * (uint32_t)CHIPMUNK_N - 1u);

    for (int k = 0; k < CHIPMUNK_N; ++k) {
        uint32_t l_idx = s_hvc_brv9((uint32_t)k);
        s_hvc_zetas[k]     = (int32_t)s_hvc_modpow_u(l_omega,     l_idx);
        s_hvc_zetas_inv[k] = (int32_t)s_hvc_modpow_u(l_omega_inv, l_idx);
    }

    s_hvc_ninv = (int32_t)s_hvc_modpow_u((uint32_t)CHIPMUNK_N,
                                         (uint32_t)CHIPMUNK_HVC_Q - 2u);

    atomic_store_explicit(&s_hvc_ntt_state, 2, memory_order_release);
    pthread_mutex_unlock(&s_hvc_ntt_init_mutex);

    log_it(L_DEBUG, "HVC NTT initialised: omega=%u, omega_inv=%u, N_inv=%d",
           l_omega, l_omega_inv, s_hvc_ninv);
    return 0;
}

// Forward NTT, in-place.  Dilithium-style layout: zetas are used with k = 1..N-1
// (zetas[0] is the identity and unused).  Inputs must be reduced into [0, q).
static void s_hvc_ntt(int32_t a_x[CHIPMUNK_N])
{
    unsigned int l_k = 0;
    for (unsigned int l_len = CHIPMUNK_N / 2; l_len > 0; l_len >>= 1) {
        for (unsigned int l_start = 0; l_start < (unsigned)CHIPMUNK_N; l_start += 2 * l_len) {
            int32_t l_zeta = s_hvc_zetas[++l_k];
            for (unsigned int j = l_start; j < l_start + l_len; ++j) {
                int32_t l_t = s_hvc_fqmul(l_zeta, a_x[j + l_len]);
                int32_t l_u = a_x[j];
                int32_t l_sum = l_u + l_t;
                int32_t l_dif = l_u - l_t;
                if (l_sum >= S_HVC_Q32) l_sum -= S_HVC_Q32;
                if (l_dif < 0)          l_dif += S_HVC_Q32;
                a_x[j]         = l_sum;
                a_x[j + l_len] = l_dif;
            }
        }
    }
}

// Inverse NTT, in-place.  Walks the Cooley–Tukey schedule in reverse and
// applies the N^{-1} factor at the end.  Uses s_hvc_zetas_inv[] which follows
// the same BRV layout as s_hvc_zetas[].
static void s_hvc_invntt(int32_t a_x[CHIPMUNK_N])
{
    unsigned int l_k = CHIPMUNK_N;
    for (unsigned int l_len = 1; l_len < (unsigned)CHIPMUNK_N; l_len <<= 1) {
        for (unsigned int l_start = 0; l_start < (unsigned)CHIPMUNK_N; l_start += 2 * l_len) {
            int32_t l_zeta = s_hvc_zetas_inv[--l_k];
            for (unsigned int j = l_start; j < l_start + l_len; ++j) {
                int32_t l_u = a_x[j];
                int32_t l_v = a_x[j + l_len];
                int32_t l_sum = l_u + l_v;
                int32_t l_dif = l_u - l_v;
                if (l_sum >= S_HVC_Q32) l_sum -= S_HVC_Q32;
                if (l_dif < 0)          l_dif += S_HVC_Q32;
                a_x[j]         = l_sum;
                a_x[j + l_len] = s_hvc_fqmul(l_zeta, l_dif);
            }
        }
    }
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        a_x[i] = s_hvc_fqmul(a_x[i], s_hvc_ninv);
    }
}

// ============================================================================
// HVC polynomial helpers (R_q = Z_q[X]/(X^N+1), q = CHIPMUNK_HVC_Q = 202753)
// ============================================================================

// After CR-D15.A NTT rewrite the only remaining polynomial "multiplication"
// happens inline in chipmunk_hvc_hash_decom_then_hash() (pointwise product in
// the frequency domain, accumulating straight into an NTT-form accumulator).
// Keeping a standalone wrapper around (+ the legacy schoolbook helpers) would
// just be dead code; scalar arithmetic in the coefficient domain has no
// callers outside that hot path and can be resurrected from git history if
// ever needed.

// Signed-digit base-(2·ZETA+1) = 59 decomposition.
//
// Each HVC coefficient c ∈ [0, q) is written as
//     c ≡ Σ_{i=0}^{W-1} d_i · B^i   (mod q)
// where B = 59 and d_i ∈ [−ZETA, +ZETA] = [−29, +29].
//
// The algorithm: repeatedly take d = c mod B with c := c/B; if d > ZETA set
// d ← d − B and carry +1 into c.  With W = HVC_WIDTH = 3 pieces this covers
// the range |c| ≤ Σ_i ZETA·B^i < B^W/2 = 59^3/2 ≈ 1.03e5; since
// CHIPMUNK_HVC_Q/2 ≈ 1.01e5 the signed representation is injective when
// we interpret c in the balanced representative window (|c| ≤ q/2).
static void s_hvc_poly_decompose(const chipmunk_hvc_poly_t *a_poly,
                                 chipmunk_hvc_poly_t a_comps[CHIPMUNK_HVC_WIDTH])
{
    const int32_t k_base = CHIPMUNK_TWO_ZETA_PLUS_ONE;  // 59
    const int32_t k_half = CHIPMUNK_ZETA;               // 29

    for (int j = 0; j < CHIPMUNK_N; ++j) {
        // Balanced representative: lift c into (−q/2, +q/2].
        int32_t l_c = a_poly->coeffs[j] % CHIPMUNK_HVC_Q;
        if (l_c < 0) {
            l_c += CHIPMUNK_HVC_Q;
        }
        if (l_c > CHIPMUNK_HVC_Q / 2) {
            l_c -= CHIPMUNK_HVC_Q;
        }

        for (int i = 0; i < CHIPMUNK_HVC_WIDTH; ++i) {
            int32_t l_d;
            if (l_c >= 0) {
                l_d = l_c % k_base;
                l_c /= k_base;
                if (l_d > k_half) {
                    l_d -= k_base;
                    l_c += 1;
                }
            } else {
                int32_t l_neg = -l_c;
                l_d = l_neg % k_base;
                l_c = -(l_neg / k_base);
                if (l_d > k_half) {
                    l_d -= k_base;
                    l_c -= 1;
                }
                l_d = -l_d;
            }
            a_comps[i].coeffs[j] = l_d;
        }
    }
}

// Sample a single HVC polynomial uniformly in Z_q^HVC from a SHAKE128 stream.
// Uses rejection sampling on 23-bit words with the bias-free acceptance window
// [0, floor(2^23 / q) · q), exactly like dap_chipmunk_hash_sample_matrix().
static int s_hvc_sample_poly_uniform(chipmunk_hvc_poly_t *a_poly,
                                     const uint8_t *a_seed,
                                     size_t a_seed_len,
                                     uint16_t a_nonce)
{
    if (!a_poly || !a_seed) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    static const uint8_t k_domain[] = "CHIPMUNK/HVC/sample_poly/v1";
    uint8_t l_input[sizeof(k_domain) + 64 + 2];
    if (a_seed_len > 64) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    size_t l_input_len = 0;
    memcpy(l_input, k_domain, sizeof(k_domain));
    l_input_len += sizeof(k_domain);
    memcpy(l_input + l_input_len, a_seed, a_seed_len);
    l_input_len += a_seed_len;
    l_input[l_input_len++] = (uint8_t)(a_nonce & 0xff);
    l_input[l_input_len++] = (uint8_t)((a_nonce >> 8) & 0xff);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake128_absorb(l_state, l_input, l_input_len);

    uint8_t l_block[DAP_SHAKE128_RATE];
    size_t  l_pos = DAP_SHAKE128_RATE;

    const uint32_t k_mul = (0x800000u / (uint32_t)CHIPMUNK_HVC_Q) * (uint32_t)CHIPMUNK_HVC_Q;
    const size_t   k_max_blocks = 1u << 20;
    size_t         l_blocks = 0;

    for (int i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t l_val;
        for (;;) {
            if (l_pos + 3 > DAP_SHAKE128_RATE) {
                if (l_blocks++ >= k_max_blocks) {
                    log_it(L_ERROR, "HVC sample budget exhausted (nonce=%u)", a_nonce);
                    memset(a_poly, 0, sizeof(*a_poly));
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake128_squeezeblocks(l_block, 1, l_state);
                l_pos = 0;
            }
            l_val = (uint32_t)l_block[l_pos]
                  | ((uint32_t)l_block[l_pos + 1] << 8)
                  | ((uint32_t)l_block[l_pos + 2] << 16);
            l_val &= 0x7FFFFFu;
            l_pos += 3;
            if (l_val < k_mul) {
                break;
            }
        }
        a_poly->coeffs[i] = (int32_t)(l_val % (uint32_t)CHIPMUNK_HVC_Q);
    }

    // Zeroise the Keccak state to avoid leaking it via stack residue.
    memset(l_state, 0, sizeof(l_state));
    memset(l_block, 0, sizeof(l_block));

    return CHIPMUNK_ERROR_SUCCESS;
}

// ============================================================================
// Honest HVC hasher API
// ============================================================================

int chipmunk_hvc_hasher_init(chipmunk_hvc_hasher_t *a_hasher, const uint8_t a_seed[32])
{
    if (!a_hasher || !a_seed) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hvc_hasher_init");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Lazily derive the HVC NTT twiddle tables on first use.  Idempotent,
    // thread-safe, and cheap (a few millisecond one-shot cost).
    int l_ntt_rc = s_hvc_ntt_init();
    if (l_ntt_rc != 0) {
        return l_ntt_rc;
    }

    memset(a_hasher, 0, sizeof(*a_hasher));
    memcpy(a_hasher->seed, a_seed, 32);

    // Sample the public matrices A_left, A_right ∈ Rq^W in coefficient form
    // and then immediately fold them through the forward NTT so the hot path
    // skips the W forward transforms per hash call.
    for (int i = 0; i < CHIPMUNK_HVC_WIDTH; ++i) {
        chipmunk_hvc_poly_t l_tmp_left;
        chipmunk_hvc_poly_t l_tmp_right;

        int l_rc = s_hvc_sample_poly_uniform(&l_tmp_left, a_hasher->seed, 32,
                                             (uint16_t)(2 * i + 0));
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            memset(a_hasher, 0, sizeof(*a_hasher));
            return l_rc;
        }
        l_rc = s_hvc_sample_poly_uniform(&l_tmp_right, a_hasher->seed, 32,
                                         (uint16_t)(2 * i + 1));
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            memset(a_hasher, 0, sizeof(*a_hasher));
            return l_rc;
        }

        for (int j = 0; j < CHIPMUNK_N; ++j) {
            a_hasher->matrix_a_left_ntt[i][j]  = s_hvc_freduce(l_tmp_left.coeffs[j]);
            a_hasher->matrix_a_right_ntt[i][j] = s_hvc_freduce(l_tmp_right.coeffs[j]);
        }
        s_hvc_ntt(a_hasher->matrix_a_left_ntt[i]);
        s_hvc_ntt(a_hasher->matrix_a_right_ntt[i]);
    }

    debug_if(true, L_DEBUG, "HVC hasher initialised (seed[0..4]=%02x%02x%02x%02x)",
             a_hasher->seed[0], a_hasher->seed[1], a_hasher->seed[2], a_hasher->seed[3]);

    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_hvc_hash_decom_then_hash(const chipmunk_hvc_hasher_t *a_hasher,
                                       const chipmunk_hvc_poly_t *a_left,
                                       const chipmunk_hvc_poly_t *a_right,
                                       chipmunk_hvc_poly_t *a_result)
{
    if (!a_hasher || !a_left || !a_right || !a_result) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hvc_hash_decom_then_hash");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // 1. Signed-digit base-(2ZETA+1) decomposition of the two inputs.
    chipmunk_hvc_poly_t l_left_comps[CHIPMUNK_HVC_WIDTH];
    chipmunk_hvc_poly_t l_right_comps[CHIPMUNK_HVC_WIDTH];
    s_hvc_poly_decompose(a_left,  l_left_comps);
    s_hvc_poly_decompose(a_right, l_right_comps);

    // 2. Forward-NTT each component and accumulate the pointwise products
    //    Σ A_left[i]·D(x)[i] + A_right[i]·D(y)[i] directly in the frequency
    //    domain — this turns (W forward + W pointwise + W add + W forward
    //    + W pointwise + W add + 1 invNTT) into (2W forward + 2W pointwise
    //    + 2W add + 1 invNTT), i.e. no extra work per addend while skipping
    //    W forward NTTs on the cached matrix.
    int32_t l_acc_ntt[CHIPMUNK_N];
    memset(l_acc_ntt, 0, sizeof(l_acc_ntt));

    int32_t l_scratch[CHIPMUNK_N];

    for (int i = 0; i < CHIPMUNK_HVC_WIDTH; ++i) {
        // Left branch: D(x)[i] → NTT → × A_left_ntt[i] → accumulate.
        for (int k = 0; k < CHIPMUNK_N; ++k) {
            l_scratch[k] = s_hvc_freduce(l_left_comps[i].coeffs[k]);
        }
        s_hvc_ntt(l_scratch);
        for (int k = 0; k < CHIPMUNK_N; ++k) {
            int32_t l_prod = s_hvc_fqmul(l_scratch[k], a_hasher->matrix_a_left_ntt[i][k]);
            int32_t l_sum  = l_acc_ntt[k] + l_prod;
            if (l_sum >= S_HVC_Q32) l_sum -= S_HVC_Q32;
            l_acc_ntt[k] = l_sum;
        }

        // Right branch: D(y)[i] → NTT → × A_right_ntt[i] → accumulate.
        for (int k = 0; k < CHIPMUNK_N; ++k) {
            l_scratch[k] = s_hvc_freduce(l_right_comps[i].coeffs[k]);
        }
        s_hvc_ntt(l_scratch);
        for (int k = 0; k < CHIPMUNK_N; ++k) {
            int32_t l_prod = s_hvc_fqmul(l_scratch[k], a_hasher->matrix_a_right_ntt[i][k]);
            int32_t l_sum  = l_acc_ntt[k] + l_prod;
            if (l_sum >= S_HVC_Q32) l_sum -= S_HVC_Q32;
            l_acc_ntt[k] = l_sum;
        }
    }

    // 3. Single inverse NTT to land back in the canonical coefficient form.
    s_hvc_invntt(l_acc_ntt);
    for (int k = 0; k < CHIPMUNK_N; ++k) {
        a_result->coeffs[k] = l_acc_ntt[k];
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

// ============================================================================
// Merkle tree construction
// ============================================================================

int chipmunk_tree_new_with_leaf_nodes(chipmunk_tree_t *a_tree,
                                       const chipmunk_hvc_poly_t *a_leaf_nodes,
                                       size_t a_leaf_count,
                                       const chipmunk_hvc_hasher_t *a_hasher)
{
    if (!a_tree || !a_leaf_nodes || !a_hasher) {
        log_it(L_ERROR, "NULL parameters in chipmunk_tree_new_with_leaf_nodes");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    memset(a_tree, 0, sizeof(*a_tree));
    // Capture the hasher seed so aggregators / verifiers can reconstruct the
    // same hasher from the multi-signature container (chipmunk_multi_signature_t
    // ::hvc_hasher_seed) without passing it out-of-band.
    memcpy(a_tree->hasher_seed, a_hasher->seed, sizeof(a_tree->hasher_seed));

    a_tree->height = chipmunk_tree_calculate_height(a_leaf_count);
    if (a_tree->height < CHIPMUNK_TREE_HEIGHT_MIN || a_tree->height > CHIPMUNK_TREE_HEIGHT_MAX) {
        log_it(L_ERROR, "Invalid tree height %u (min %d, max %d)",
               a_tree->height, CHIPMUNK_TREE_HEIGHT_MIN, CHIPMUNK_TREE_HEIGHT_MAX);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    a_tree->leaf_count     = CHIPMUNK_TREE_LEAF_COUNT(a_tree->height);
    a_tree->non_leaf_count = CHIPMUNK_TREE_NON_LEAF_COUNT(a_tree->height);

    a_tree->leaf_nodes = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, a_tree->leaf_count);
    if (!a_tree->leaf_nodes) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    a_tree->non_leaf_nodes = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, a_tree->non_leaf_count);
    if (!a_tree->non_leaf_nodes) {
        DAP_DEL_MULTY(a_tree->leaf_nodes);
        a_tree->leaf_nodes = NULL;
        return CHIPMUNK_ERROR_MEMORY;
    }

    // Copy the supplied leaves; pad the tail with zero polynomials so the
    // tree is always complete.
    size_t l_copy = a_leaf_count;
    if (l_copy > a_tree->leaf_count) {
        l_copy = a_tree->leaf_count;
    }
    memcpy(a_tree->leaf_nodes, a_leaf_nodes, l_copy * sizeof(chipmunk_hvc_poly_t));

    // Build internal layers bottom-up using a heap-indexed working array of
    // size 2·leaf_count − 1 (leaves placed in [leaf_start_index … end)).
    size_t l_total = a_tree->leaf_count * 2 - 1;
    chipmunk_hvc_poly_t *l_all = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, l_total);
    if (!l_all) {
        DAP_DEL_MULTY(a_tree->leaf_nodes);
        DAP_DEL_MULTY(a_tree->non_leaf_nodes);
        a_tree->leaf_nodes = NULL;
        a_tree->non_leaf_nodes = NULL;
        return CHIPMUNK_ERROR_MEMORY;
    }

    size_t l_leaf_start = a_tree->leaf_count - 1;
    memcpy(&l_all[l_leaf_start], a_tree->leaf_nodes,
           a_tree->leaf_count * sizeof(chipmunk_hvc_poly_t));

    for (ssize_t i = (ssize_t)l_leaf_start - 1; i >= 0; --i) {
        size_t l_lc = 2 * (size_t)i + 1;
        size_t l_rc = 2 * (size_t)i + 2;
        int l_rc_hash = chipmunk_hvc_hash_decom_then_hash(a_hasher, &l_all[l_lc],
                                                          &l_all[l_rc], &l_all[i]);
        if (l_rc_hash != CHIPMUNK_ERROR_SUCCESS) {
            DAP_DEL_MULTY(l_all);
            chipmunk_tree_free(a_tree);
            return l_rc_hash;
        }
    }

    memcpy(a_tree->non_leaf_nodes, l_all,
           a_tree->non_leaf_count * sizeof(chipmunk_hvc_poly_t));
    DAP_DEL_MULTY(l_all);

    log_it(L_DEBUG, "Merkle tree built: height=%u, leaves=%zu, non_leaf=%zu",
           a_tree->height, a_tree->leaf_count, a_tree->non_leaf_count);
    return CHIPMUNK_ERROR_SUCCESS;
}

int chipmunk_tree_init(chipmunk_tree_t *a_tree, const chipmunk_hvc_hasher_t *a_hasher)
{
    if (!a_tree || !a_hasher) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    chipmunk_hvc_poly_t *l_zero = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t,
                                                  CHIPMUNK_TREE_LEAF_COUNT_DEFAULT);
    if (!l_zero) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    int l_rc = chipmunk_tree_new_with_leaf_nodes(a_tree, l_zero,
                                                 CHIPMUNK_TREE_LEAF_COUNT_DEFAULT, a_hasher);
    DAP_DEL_MULTY(l_zero);
    return l_rc;
}

int chipmunk_tree_init_with_size(chipmunk_tree_t *a_tree,
                                  size_t a_participant_count,
                                  const chipmunk_hvc_hasher_t *a_hasher)
{
    if (!a_tree || !a_hasher || !chipmunk_tree_validate_participant_count(a_participant_count)) {
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }
    uint32_t l_height = chipmunk_tree_calculate_height(a_participant_count);
    size_t l_leaf_count = CHIPMUNK_TREE_LEAF_COUNT(l_height);
    chipmunk_hvc_poly_t *l_zero = DAP_NEW_Z_COUNT(chipmunk_hvc_poly_t, l_leaf_count);
    if (!l_zero) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    int l_rc = chipmunk_tree_new_with_leaf_nodes(a_tree, l_zero, l_leaf_count, a_hasher);
    DAP_DEL_MULTY(l_zero);
    return l_rc;
}

const chipmunk_hvc_poly_t* chipmunk_tree_root(const chipmunk_tree_t *a_tree)
{
    if (!a_tree || !a_tree->non_leaf_nodes) {
        return NULL;
    }
    return &a_tree->non_leaf_nodes[0];
}

// ============================================================================
// Proof generation and verification
// ============================================================================

int chipmunk_tree_gen_proof(const chipmunk_tree_t *a_tree, size_t a_index,
                            chipmunk_path_t *a_path)
{
    if (!a_tree || !a_path || a_index >= a_tree->leaf_count) {
        log_it(L_ERROR, "Invalid parameters in chipmunk_tree_gen_proof (index=%zu)", a_index);
        return CHIPMUNK_ERROR_INVALID_PARAM;
    }

    size_t l_path_length = a_tree->height - 1;
    a_path->nodes = DAP_NEW_Z_COUNT(chipmunk_path_node_t, l_path_length);
    if (!a_path->nodes) {
        return CHIPMUNK_ERROR_MEMORY;
    }
    a_path->path_length = l_path_length;
    a_path->index       = a_index;

    // Level 0: the (leaf_index, sibling_leaf) pair.
    size_t l_pair_base = (a_index % 2 == 0) ? a_index : (a_index - 1);
    memcpy(&a_path->nodes[0].left,  &a_tree->leaf_nodes[l_pair_base],
           sizeof(chipmunk_hvc_poly_t));
    memcpy(&a_path->nodes[0].right, &a_tree->leaf_nodes[l_pair_base + 1],
           sizeof(chipmunk_hvc_poly_t));

    // Levels 1..H-2: sibling pairs among non-leaf nodes.
    // We walk up by halving heap indices; `l_node` is the current node at the
    // previous level (starts at the parent of the leaf pair).
    size_t l_leaf_heap_idx = (1UL << (a_tree->height - 1)) - 1 + a_index;
    size_t l_node = (l_leaf_heap_idx - 1) >> 1;

    for (size_t level = 1; level < l_path_length; ++level) {
        size_t l_sibling = (l_node % 2 == 1) ? (l_node + 1) : (l_node - 1);
        if (l_sibling >= a_tree->non_leaf_count) {
            log_it(L_ERROR, "Sibling heap index %zu OOB in gen_proof", l_sibling);
            DAP_DEL_MULTY(a_path->nodes);
            a_path->nodes = NULL;
            return CHIPMUNK_ERROR_INTERNAL;
        }
        if (l_node % 2 == 1) {
            memcpy(&a_path->nodes[level].left,  &a_tree->non_leaf_nodes[l_node],
                   sizeof(chipmunk_hvc_poly_t));
            memcpy(&a_path->nodes[level].right, &a_tree->non_leaf_nodes[l_sibling],
                   sizeof(chipmunk_hvc_poly_t));
        } else {
            memcpy(&a_path->nodes[level].left,  &a_tree->non_leaf_nodes[l_sibling],
                   sizeof(chipmunk_hvc_poly_t));
            memcpy(&a_path->nodes[level].right, &a_tree->non_leaf_nodes[l_node],
                   sizeof(chipmunk_hvc_poly_t));
        }
        l_node = (l_node - 1) >> 1;
    }

    return CHIPMUNK_ERROR_SUCCESS;
}

static inline bool s_poly_eq(const chipmunk_hvc_poly_t *a, const chipmunk_hvc_poly_t *b)
{
    return memcmp(a, b, sizeof(*a)) == 0;
}

bool chipmunk_path_verify(const chipmunk_path_t *a_path,
                          const chipmunk_hvc_poly_t *a_leaf,
                          const chipmunk_hvc_poly_t *a_root,
                          const chipmunk_hvc_hasher_t *a_hasher)
{
    if (!a_path || !a_leaf || !a_root || !a_hasher || !a_path->nodes ||
        a_path->path_length == 0) {
        log_it(L_ERROR, "NULL / empty parameters in chipmunk_path_verify");
        return false;
    }

    // 1. Pin the leaf: it must occupy the parity-correct side of path[0].
    //    If the leaf index is even the leaf sits on the left; if odd, on the
    //    right.  Without this check an attacker could swap the claimed leaf
    //    for any other polynomial, because the upper levels of the path are
    //    only a function of path[0]'s *hash*, not of the individual sides.
    const chipmunk_hvc_poly_t *l_claimed_leaf_side =
        (a_path->index % 2 == 0) ? &a_path->nodes[0].left : &a_path->nodes[0].right;
    if (!s_poly_eq(a_leaf, l_claimed_leaf_side)) {
        log_it(L_ERROR, "chipmunk_path_verify: leaf does not occupy parity-correct side of path[0]");
        return false;
    }

    // 2. Walk bottom-up, recomputing the hash at every level.
    chipmunk_hvc_poly_t l_cur;
    int l_rc = chipmunk_hvc_hash_decom_then_hash(a_hasher, &a_path->nodes[0].left,
                                                  &a_path->nodes[0].right, &l_cur);
    if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
        log_it(L_ERROR, "chipmunk_path_verify: level-0 hash failed (%d)", l_rc);
        return false;
    }

    // 3. For every higher level, the freshly-computed hash must sit on the
    //    parity-correct side of the next pair.
    for (size_t level = 1; level < a_path->path_length; ++level) {
        // The bit at position `level` of `index` tells us whether `l_cur`
        // belongs on the left or right of path[level]: bit = 0 → left.
        bool l_right = ((a_path->index >> level) & 1UL) != 0;
        const chipmunk_hvc_poly_t *l_expected_slot =
            l_right ? &a_path->nodes[level].right : &a_path->nodes[level].left;
        if (!s_poly_eq(&l_cur, l_expected_slot)) {
            log_it(L_ERROR, "chipmunk_path_verify: level-%zu inner hash mismatch", level);
            return false;
        }
        l_rc = chipmunk_hvc_hash_decom_then_hash(a_hasher, &a_path->nodes[level].left,
                                                  &a_path->nodes[level].right, &l_cur);
        if (l_rc != CHIPMUNK_ERROR_SUCCESS) {
            log_it(L_ERROR, "chipmunk_path_verify: level-%zu hash failed (%d)", level, l_rc);
            return false;
        }
    }

    // 4. The top-level hash must equal the stored root.
    if (!s_poly_eq(&l_cur, a_root)) {
        log_it(L_ERROR, "chipmunk_path_verify: root mismatch");
        return false;
    }

    return true;
}

// ============================================================================
// HOTS pk → HVC leaf digest (honest SHAKE-based projection)
// ============================================================================
//
// The leaf that enters the Merkle tree must be a *collision-resistant*
// function of the signer's public HOTS pair (v0, v1).  We use SHAKE128 with
// a dedicated domain tag to absorb the canonical (ρ-seed, v0, v1) encoding
// and then squeeze a uniform HVC polynomial via the same rejection-sampling
// routine used to derive A.  This gives the "digest" semantics the aggregate
// verifier relies on in s_verify_pk_leaf_binding().
//
// Binary encoding used for the HOTS pk:
//   • 32-byte rho_seed (little-endian as-is; HOTS public matrix A is derived
//     from it; even though the current primitive bakes in a global A, the
//     rho_seed is still part of the signer identity, so we commit to it);
//   • 2 × CHIPMUNK_N 32-bit little-endian canonical residues of v0 then v1.
//
// This encoding is schema-compatible with chipmunk_public_key_to_bytes() up
// to endianness of the coefficient field; we pin the endianness explicitly
// here to avoid relying on host-byte-order assumptions in the rest of the
// codebase.
int chipmunk_hots_pk_to_hvc_poly(const chipmunk_public_key_t *a_hots_pk,
                                  chipmunk_hvc_poly_t *a_hvc_poly)
{
    if (!a_hots_pk || !a_hvc_poly) {
        log_it(L_ERROR, "NULL parameters in chipmunk_hots_pk_to_hvc_poly");
        return CHIPMUNK_ERROR_NULL_PARAM;
    }

    // Serialise (rho_seed || v0 || v1) in a canonical little-endian form.
    const size_t k_coeff_bytes = 4;
    const size_t k_body_len = 32 + 2 * CHIPMUNK_N * k_coeff_bytes;
    uint8_t *l_body = DAP_NEW_Z_COUNT(uint8_t, k_body_len);
    if (!l_body) {
        return CHIPMUNK_ERROR_MEMORY;
    }

    memcpy(l_body, a_hots_pk->rho_seed, 32);
    uint8_t *l_p = l_body + 32;
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_c = a_hots_pk->v0.coeffs[i];
        int32_t l_r = l_c % CHIPMUNK_Q;
        if (l_r < 0) l_r += CHIPMUNK_Q;
        l_p[0] = (uint8_t)(l_r      );
        l_p[1] = (uint8_t)(l_r >>  8);
        l_p[2] = (uint8_t)(l_r >> 16);
        l_p[3] = (uint8_t)(l_r >> 24);
        l_p += 4;
    }
    for (int i = 0; i < CHIPMUNK_N; ++i) {
        int32_t l_c = a_hots_pk->v1.coeffs[i];
        int32_t l_r = l_c % CHIPMUNK_Q;
        if (l_r < 0) l_r += CHIPMUNK_Q;
        l_p[0] = (uint8_t)(l_r      );
        l_p[1] = (uint8_t)(l_r >>  8);
        l_p[2] = (uint8_t)(l_r >> 16);
        l_p[3] = (uint8_t)(l_r >> 24);
        l_p += 4;
    }

    // Absorb (domain ‖ body) and squeeze a uniform HVC polynomial.
    static const uint8_t k_domain[] = "CHIPMUNK/HOTS-pk-to-HVC-leaf/v1";
    const size_t k_input_len = sizeof(k_domain) + k_body_len;
    uint8_t *l_input = DAP_NEW_Z_COUNT(uint8_t, k_input_len);
    if (!l_input) {
        DAP_DEL_MULTY(l_body);
        return CHIPMUNK_ERROR_MEMORY;
    }
    memcpy(l_input, k_domain, sizeof(k_domain));
    memcpy(l_input + sizeof(k_domain), l_body, k_body_len);

    uint64_t l_state[25];
    memset(l_state, 0, sizeof(l_state));
    dap_hash_shake128_absorb(l_state, l_input, k_input_len);

    DAP_DEL_MULTY(l_body);
    DAP_DEL_MULTY(l_input);

    uint8_t l_block[DAP_SHAKE128_RATE];
    size_t  l_pos = DAP_SHAKE128_RATE;

    const uint32_t k_mul = (0x800000u / (uint32_t)CHIPMUNK_HVC_Q) * (uint32_t)CHIPMUNK_HVC_Q;
    const size_t   k_max_blocks = 1u << 20;
    size_t         l_blocks = 0;

    for (int i = 0; i < CHIPMUNK_N; ++i) {
        uint32_t l_val;
        for (;;) {
            if (l_pos + 3 > DAP_SHAKE128_RATE) {
                if (l_blocks++ >= k_max_blocks) {
                    log_it(L_ERROR, "HVC leaf digest squeeze budget exhausted");
                    memset(a_hvc_poly, 0, sizeof(*a_hvc_poly));
                    memset(l_state, 0, sizeof(l_state));
                    return CHIPMUNK_ERROR_INTERNAL;
                }
                dap_hash_shake128_squeezeblocks(l_block, 1, l_state);
                l_pos = 0;
            }
            l_val = (uint32_t)l_block[l_pos]
                  | ((uint32_t)l_block[l_pos + 1] << 8)
                  | ((uint32_t)l_block[l_pos + 2] << 16);
            l_val &= 0x7FFFFFu;
            l_pos += 3;
            if (l_val < k_mul) {
                break;
            }
        }
        a_hvc_poly->coeffs[i] = (int32_t)(l_val % (uint32_t)CHIPMUNK_HVC_Q);
    }

    memset(l_state, 0, sizeof(l_state));
    memset(l_block, 0, sizeof(l_block));
    return CHIPMUNK_ERROR_SUCCESS;
}

// ============================================================================
// Tree lifecycle / statistics
// ============================================================================

void chipmunk_tree_clear(chipmunk_tree_t *a_tree)
{
    if (a_tree) {
        if (a_tree->leaf_nodes) {
            memset(a_tree->leaf_nodes, 0,
                   a_tree->leaf_count * sizeof(chipmunk_hvc_poly_t));
        }
        if (a_tree->non_leaf_nodes) {
            memset(a_tree->non_leaf_nodes, 0,
                   a_tree->non_leaf_count * sizeof(chipmunk_hvc_poly_t));
        }
    }
}

void chipmunk_path_clear(chipmunk_path_t *a_path)
{
    if (a_path && a_path->nodes) {
        memset(a_path->nodes, 0, a_path->path_length * sizeof(chipmunk_path_node_t));
    }
}

void chipmunk_tree_free(chipmunk_tree_t *a_tree)
{
    if (!a_tree) {
        return;
    }
    if (a_tree->leaf_nodes) {
        DAP_DEL_MULTY(a_tree->leaf_nodes);
        a_tree->leaf_nodes = NULL;
    }
    if (a_tree->non_leaf_nodes) {
        DAP_DEL_MULTY(a_tree->non_leaf_nodes);
        a_tree->non_leaf_nodes = NULL;
    }
    memset(a_tree, 0, sizeof(*a_tree));
}

void chipmunk_path_free(chipmunk_path_t *a_path)
{
    if (!a_path) {
        return;
    }
    if (a_path->nodes) {
        DAP_DEL_MULTY(a_path->nodes);
        a_path->nodes = NULL;
    }
    a_path->path_length = 0;
    a_path->index       = 0;
}

uint32_t chipmunk_tree_calculate_height(size_t a_participant_count)
{
    if (a_participant_count == 0) {
        return CHIPMUNK_TREE_HEIGHT_MIN;
    }
    if (a_participant_count <= 1) {
        return CHIPMUNK_TREE_HEIGHT_MIN;
    }
    if (a_participant_count > CHIPMUNK_TREE_MAX_PARTICIPANTS) {
        return CHIPMUNK_TREE_HEIGHT_MAX;
    }

    uint32_t l_h = CHIPMUNK_TREE_HEIGHT_MIN;
    size_t   l_cap = 1UL << (l_h - 1);
    while (l_cap < a_participant_count && l_h < CHIPMUNK_TREE_HEIGHT_MAX) {
        ++l_h;
        l_cap = 1UL << (l_h - 1);
    }
    return l_h;
}

bool chipmunk_tree_validate_participant_count(size_t a_participant_count)
{
    if (a_participant_count == 0 || a_participant_count > CHIPMUNK_TREE_MAX_PARTICIPANTS) {
        return false;
    }
    return chipmunk_tree_calculate_height(a_participant_count) <= CHIPMUNK_TREE_HEIGHT_MAX;
}

int chipmunk_tree_get_stats(const chipmunk_tree_t *a_tree,
                             uint32_t *a_height,
                             size_t *a_leaf_count,
                             size_t *a_memory_usage)
{
    if (!a_tree) {
        return CHIPMUNK_ERROR_NULL_PARAM;
    }
    if (a_height)       *a_height       = a_tree->height;
    if (a_leaf_count)   *a_leaf_count   = a_tree->leaf_count;
    if (a_memory_usage) *a_memory_usage = sizeof(*a_tree)
                                        + a_tree->leaf_count     * sizeof(chipmunk_hvc_poly_t)
                                        + a_tree->non_leaf_count * sizeof(chipmunk_hvc_poly_t);
    return CHIPMUNK_ERROR_SUCCESS;
}

