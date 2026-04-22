/**
 * @file dap_mlkem_indcpa.c
 * @brief IND-CPA public-key encryption for ML-KEM (FIPS 203).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdint.h>
#include <string.h>
#include "dap_mlkem_indcpa.h"
#include "dap_mlkem_ctx.h"
#include "dap_mlkem_poly.h"
#include "dap_mlkem_polyvec.h"
#include "dap_mlkem_ntt.h"
#include "dap_mlkem_symmetric.h"
#include "dap_rand.h"
#include "dap_arch_dispatch.h"
#include "dap_cpu_detect.h"

/* ---- pack / unpack ---- */

static void s_pack_pk(uint8_t a_r[MLKEM_INDCPA_PUBLICKEYBYTES],
                       dap_mlkem_polyvec *a_pk,
                       const uint8_t a_seed[MLKEM_SYMBYTES])
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_pk);
    memcpy(a_r + MLKEM_POLYVECBYTES, a_seed, MLKEM_SYMBYTES);
}

static void s_unpack_pk(dap_mlkem_polyvec *a_pk,
                         uint8_t a_seed[MLKEM_SYMBYTES],
                         const uint8_t a_packed[MLKEM_INDCPA_PUBLICKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_pk, a_packed);
    memcpy(a_seed, a_packed + MLKEM_POLYVECBYTES, MLKEM_SYMBYTES);
}

static void s_pack_sk(uint8_t a_r[MLKEM_INDCPA_SECRETKEYBYTES], dap_mlkem_polyvec *a_sk)
{
    MLKEM_NAMESPACE(_polyvec_tobytes)(a_r, a_sk);
}

static void s_unpack_sk(dap_mlkem_polyvec *a_sk,
                         const uint8_t a_packed[MLKEM_INDCPA_SECRETKEYBYTES])
{
    MLKEM_NAMESPACE(_polyvec_frombytes)(a_sk, a_packed);
}

static void s_pack_ciphertext(uint8_t a_r[MLKEM_INDCPA_BYTES],
                               dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v)
{
    MLKEM_NAMESPACE(_polyvec_compress)(a_r, a_b);
    MLKEM_NAMESPACE(_poly_compress)(a_r + MLKEM_POLYVECCOMPRESSEDBYTES, a_v);
}

static void s_unpack_ciphertext(dap_mlkem_polyvec *a_b, dap_mlkem_poly *a_v,
                                 const uint8_t a_c[MLKEM_INDCPA_BYTES])
{
    MLKEM_NAMESPACE(_polyvec_decompress)(a_b, a_c);
    MLKEM_NAMESPACE(_poly_decompress)(a_v, a_c + MLKEM_POLYVECCOMPRESSEDBYTES);
}

/* ---- rejection sampling (dispatched to generated arch variants) ---- */

extern unsigned dap_mlkem_rej_uniform_generic(int16_t *, unsigned, const uint8_t *, unsigned);
#if DAP_PLATFORM_X86
extern unsigned dap_mlkem_rej_uniform_avx2(int16_t *, unsigned, const uint8_t *, unsigned);
extern unsigned dap_mlkem_rej_uniform_avx512_vbmi2(int16_t *, unsigned, const uint8_t *, unsigned);
#endif

DAP_DISPATCH_LOCAL(s_rej_uniform, unsigned, int16_t *, unsigned, const uint8_t *, unsigned);

static void s_rej_uniform_dispatch_init(void)
{
    dap_algo_class_t l_class = dap_algo_class_register("MLKEM_REJ");

    dap_cpu_tune_rule_t l_rules[] = {
        { DAP_CPU_VENDOR_AMD, 0x19, 0x19, 0x00, 0xFF, l_class, DAP_CPU_ARCH_AVX2 },
        { DAP_CPU_VENDOR_AMD, 0x1A, 0x1A, 0x00, 0xFF, l_class, DAP_CPU_ARCH_AVX2 },
    };
    dap_cpu_tune_add_rules(l_rules, 2);

    DAP_DISPATCH_DEFAULT(s_rej_uniform, dap_mlkem_rej_uniform_generic);
    DAP_DISPATCH_ARCH_SELECT_FOR(l_class);
    DAP_DISPATCH_X86(DAP_CPU_ARCH_AVX2, s_rej_uniform, dap_mlkem_rej_uniform_avx2);

#if DAP_PLATFORM_X86
    if (l_best_arch >= DAP_CPU_ARCH_AVX512) {
        dap_cpu_features_t l_feat = dap_cpu_detect_features();
        if (l_feat.has_avx512_vbmi2)
            s_rej_uniform_ptr = dap_mlkem_rej_uniform_avx512_vbmi2;
    }
#endif
}

#define REJ_ENSURE() DAP_DISPATCH_ENSURE(s_rej_uniform, s_rej_uniform_dispatch_init)

/* ---- matrix generation ---- */

#define GEN_MATRIX_NBLOCKS ((12 * MLKEM_N / 8 * (1 << 12) / MLKEM_Q \
                             + MLKEM_XOF_BLOCKBYTES) / MLKEM_XOF_BLOCKBYTES)

static void s_gen_matrix(dap_mlkem_polyvec *a_mat, const uint8_t a_seed[MLKEM_SYMBYTES],
                          int a_transposed)
{
    REJ_ENSURE();
    unsigned l_total = MLKEM_K * MLKEM_K;
    uint8_t l_buf[4][GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES + 2];

    for (unsigned l_idx = 0; l_idx < l_total; l_idx += 4) {
        uint8_t l_x[4], l_y[4];
        unsigned l_count = (l_total - l_idx >= 4) ? 4 : l_total - l_idx;
        for (unsigned k = 0; k < l_count; k++) {
            unsigned ii = (l_idx + k) / MLKEM_K;
            unsigned jj = (l_idx + k) % MLKEM_K;
            l_x[k] = a_transposed ? (uint8_t)ii : (uint8_t)jj;
            l_y[k] = a_transposed ? (uint8_t)jj : (uint8_t)ii;
        }
        for (unsigned k = l_count; k < 4; k++) {
            l_x[k] = l_x[0];
            l_y[k] = l_y[0];
        }

        dap_keccak_x4_state_t l_x4;
        dap_mlkem_xof_absorb_squeeze_x4(&l_x4,
                                          l_buf[0], l_buf[1], l_buf[2], l_buf[3],
                                          GEN_MATRIX_NBLOCKS, a_seed,
                                          l_x[0], l_y[0], l_x[1], l_y[1],
                                          l_x[2], l_y[2], l_x[3], l_y[3]);

        unsigned l_ctr[4];
        int l_need_more = 0;
        for (unsigned k = 0; k < l_count; k++) {
            unsigned ii = (l_idx + k) / MLKEM_K;
            unsigned jj = (l_idx + k) % MLKEM_K;
            l_ctr[k] = s_rej_uniform_ptr(a_mat[ii].vec[jj].coeffs, MLKEM_N,
                                        l_buf[k], GEN_MATRIX_NBLOCKS * MLKEM_XOF_BLOCKBYTES);
            if (l_ctr[k] < MLKEM_N)
                l_need_more = 1;
        }
        for (unsigned k = l_count; k < 4; k++)
            l_ctr[k] = MLKEM_N;

        while (l_need_more) {
            uint8_t l_extra[4][MLKEM_XOF_BLOCKBYTES];
            dap_mlkem_xof_squeezeblocks_x4(l_extra[0], l_extra[1], l_extra[2], l_extra[3],
                                             1, &l_x4);
            l_need_more = 0;
            for (unsigned k = 0; k < l_count; k++) {
                if (l_ctr[k] < MLKEM_N) {
                    unsigned ii = (l_idx + k) / MLKEM_K;
                    unsigned jj = (l_idx + k) % MLKEM_K;
                    l_ctr[k] += s_rej_uniform_ptr(a_mat[ii].vec[jj].coeffs + l_ctr[k],
                                                  MLKEM_N - l_ctr[k],
                                                  l_extra[k], MLKEM_XOF_BLOCKBYTES);
                    if (l_ctr[k] < MLKEM_N)
                        l_need_more = 1;
                }
            }
        }
    }
}

/* ---- IND-CPA ---- */

void MLKEM_NAMESPACE(_indcpa_keypair)(uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                       uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    uint8_t l_buf[2 * MLKEM_SYMBYTES];
    const uint8_t *l_publicseed = l_buf;
    const uint8_t *l_noiseseed  = l_buf + MLKEM_SYMBYTES;
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_a[MLKEM_K], l_e, l_pkpv, l_skpv;

    dap_random_bytes(l_buf, MLKEM_SYMBYTES);
    dap_mlkem_hash_g(l_buf, l_buf, MLKEM_SYMBYTES);

    s_gen_matrix(l_a, l_publicseed, 0);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_nttpack)(&l_a[i]);

    /* Batch noise sampling: skpv[0..K-1] then e[0..K-1], all eta1 — always x4 */
    dap_mlkem_poly *l_npolys[2 * MLKEM_K];
    for (unsigned j = 0; j < MLKEM_K; j++)
        l_npolys[j] = &l_skpv.vec[j];
    for (unsigned j = 0; j < MLKEM_K; j++)
        l_npolys[MLKEM_K + j] = &l_e.vec[j];

    {
        dap_mlkem_poly l_dummy;
        unsigned i = 0;
        for (; i + 4 <= 2 * MLKEM_K; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_npolys[i], l_npolys[i + 1],
                                                      l_npolys[i + 2], l_npolys[i + 3],
                                                      l_noiseseed, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < 2 * MLKEM_K) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < 2 * MLKEM_K; k++) {
                l_ptrs[k] = l_npolys[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      l_noiseseed, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(2 * MLKEM_K - i);
        }
    }

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_ntt)(&l_e);

    dap_mlkem_polyvec_mulcache l_skpv_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_skpv_cache, &l_skpv);
    for (unsigned i = 0; i < MLKEM_K; i++) {
        MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_pkpv.vec[i], &l_a[i], &l_skpv, &l_skpv_cache);
        MLKEM_NAMESPACE(_poly_tomont)(&l_pkpv.vec[i]);
    }
    MLKEM_NAMESPACE(_polyvec_add)(&l_pkpv, &l_pkpv, &l_e);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_pkpv);

    MLKEM_NAMESPACE(_polyvec_nttunpack)(&l_skpv);
    MLKEM_NAMESPACE(_polyvec_nttunpack)(&l_pkpv);
    s_pack_sk(a_sk, &l_skpv);
    s_pack_pk(a_pk, &l_pkpv, l_publicseed);
}

void MLKEM_NAMESPACE(_indcpa_enc)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES],
                                   const uint8_t a_coins[MLKEM_SYMBYTES])
{
    uint8_t l_seed[MLKEM_SYMBYTES];
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_sp, l_pkpv, l_ep, l_at[MLKEM_K], l_bp;
    dap_mlkem_poly l_v, l_k, l_epp;

    s_unpack_pk(&l_pkpv, l_seed, a_pk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&l_pkpv);
    MLKEM_NAMESPACE(_poly_frommsg)(&l_k, a_m);
    s_gen_matrix(l_at, l_seed, 1);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_nttpack)(&l_at[i]);

    /* sp: K polys with eta1 — always x4 (pad with dummy if K < 4) */
    {
        dap_mlkem_poly l_dummy;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(&l_sp.vec[i], &l_sp.vec[i + 1],
                                                      &l_sp.vec[i + 2], &l_sp.vec[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K; k++) {
                l_ptrs[k] = &l_sp.vec[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(MLKEM_K - i);
            i = MLKEM_K;
        }
    }
    /* ep: K polys with eta2, epp: 1 poly with eta2 — always x4 */
    {
        dap_mlkem_poly l_dummy;
        dap_mlkem_poly *l_eta2[MLKEM_K + 1];
        for (unsigned j = 0; j < MLKEM_K; j++)
            l_eta2[j] = &l_ep.vec[j];
        l_eta2[MLKEM_K] = &l_epp;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K + 1; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_eta2[i], l_eta2[i + 1],
                                                      l_eta2[i + 2], l_eta2[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K + 1) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K + 1; k++) {
                l_ptrs[k] = l_eta2[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(MLKEM_K + 1 - i);
        }
    }

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_sp);

    dap_mlkem_polyvec_mulcache l_sp_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_sp_cache, &l_sp);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_bp.vec[i], &l_at[i], &l_sp, &l_sp_cache);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_v, &l_pkpv, &l_sp, &l_sp_cache);

    MLKEM_NAMESPACE(_polyvec_invntt_tomont)(&l_bp);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_v);

    MLKEM_NAMESPACE(_polyvec_add)(&l_bp, &l_bp, &l_ep);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_epp);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_k);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_bp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_v);

    s_pack_ciphertext(a_c, &l_bp, &l_v);
}

void MLKEM_NAMESPACE(_indcpa_dec)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                   const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                   const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    dap_mlkem_polyvec l_bp, l_skpv;
    dap_mlkem_poly l_v, l_mp;

    s_unpack_ciphertext(&l_bp, &l_v, a_c);
    s_unpack_sk(&l_skpv, a_sk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&l_skpv);

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_bp);
    dap_mlkem_polyvec_mulcache l_bp_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_bp_cache, &l_bp);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_mp, &l_skpv, &l_bp, &l_bp_cache);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_mp);

    MLKEM_NAMESPACE(_poly_sub)(&l_mp, &l_v, &l_mp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_mp);

    MLKEM_NAMESPACE(_poly_tomsg)(a_m, &l_mp);
}

/* =========================================================================
 * Context-based variants — use pre-computed NTT state from dap_mlkem_ctx_t
 * ========================================================================= */

void MLKEM_NAMESPACE(_indcpa_ctx_init_pk)(dap_mlkem_ctx_t *a_ctx,
                                           const uint8_t a_pk[MLKEM_INDCPA_PUBLICKEYBYTES])
{
    s_unpack_pk(&a_ctx->pk_ntt, a_ctx->seed, a_pk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&a_ctx->pk_ntt);

    s_gen_matrix(a_ctx->mat_t, a_ctx->seed, 1);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_nttpack)(&a_ctx->mat_t[i]);
}

void MLKEM_NAMESPACE(_indcpa_ctx_init_sk)(dap_mlkem_ctx_t *a_ctx,
                                           const uint8_t a_sk[MLKEM_INDCPA_SECRETKEYBYTES])
{
    s_unpack_sk(&a_ctx->sk_ntt, a_sk);
    MLKEM_NAMESPACE(_polyvec_nttpack)(&a_ctx->sk_ntt);
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&a_ctx->sk_cache, &a_ctx->sk_ntt);
}

void MLKEM_NAMESPACE(_indcpa_enc_ctx)(uint8_t a_c[MLKEM_INDCPA_BYTES],
                                       const uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                       const dap_mlkem_ctx_t *a_ctx,
                                       const uint8_t a_coins[MLKEM_SYMBYTES])
{
    uint8_t l_nonce = 0;
    dap_mlkem_polyvec l_sp, l_ep, l_bp;
    dap_mlkem_poly l_v, l_k, l_epp;

    MLKEM_NAMESPACE(_poly_frommsg)(&l_k, a_m);

    {
        dap_mlkem_poly l_dummy;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(&l_sp.vec[i], &l_sp.vec[i + 1],
                                                      &l_sp.vec[i + 2], &l_sp.vec[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K; k++) {
                l_ptrs[k] = &l_sp.vec[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta1_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
            l_nonce += (uint8_t)(MLKEM_K - i);
        }
    }
    {
        dap_mlkem_poly l_dummy;
        dap_mlkem_poly *l_eta2[MLKEM_K + 1];
        for (unsigned j = 0; j < MLKEM_K; j++)
            l_eta2[j] = &l_ep.vec[j];
        l_eta2[MLKEM_K] = &l_epp;
        unsigned i = 0;
        for (; i + 4 <= MLKEM_K + 1; i += 4, l_nonce += 4)
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_eta2[i], l_eta2[i + 1],
                                                      l_eta2[i + 2], l_eta2[i + 3],
                                                      a_coins, l_nonce, (uint8_t)(l_nonce + 1),
                                                      (uint8_t)(l_nonce + 2), (uint8_t)(l_nonce + 3));
        if (i < MLKEM_K + 1) {
            dap_mlkem_poly *l_ptrs[4];
            uint8_t l_nonces[4];
            unsigned k = 0;
            for (; i + k < MLKEM_K + 1; k++) {
                l_ptrs[k] = l_eta2[i + k];
                l_nonces[k] = (uint8_t)(l_nonce + k);
            }
            for (; k < 4; k++) {
                l_ptrs[k] = &l_dummy;
                l_nonces[k] = l_nonces[0];
            }
            MLKEM_NAMESPACE(_poly_getnoise_eta2_x4)(l_ptrs[0], l_ptrs[1], l_ptrs[2], l_ptrs[3],
                                                      a_coins, l_nonces[0], l_nonces[1],
                                                      l_nonces[2], l_nonces[3]);
        }
    }

    MLKEM_NAMESPACE(_polyvec_ntt)(&l_sp);

    dap_mlkem_polyvec_mulcache l_sp_cache;
    MLKEM_NAMESPACE(_polyvec_mulcache_compute)(&l_sp_cache, &l_sp);
    for (unsigned i = 0; i < MLKEM_K; i++)
        MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_bp.vec[i], &a_ctx->mat_t[i], &l_sp, &l_sp_cache);
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(&l_v, &a_ctx->pk_ntt, &l_sp, &l_sp_cache);

    MLKEM_NAMESPACE(_polyvec_invntt_tomont)(&l_bp);
    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_v);

    MLKEM_NAMESPACE(_polyvec_add)(&l_bp, &l_bp, &l_ep);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_epp);
    MLKEM_NAMESPACE(_poly_add)(&l_v, &l_v, &l_k);
    MLKEM_NAMESPACE(_polyvec_reduce)(&l_bp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_v);

    s_pack_ciphertext(a_c, &l_bp, &l_v);
}

void MLKEM_NAMESPACE(_indcpa_dec_ctx)(uint8_t a_m[MLKEM_INDCPA_MSGBYTES],
                                       const uint8_t a_c[MLKEM_INDCPA_BYTES],
                                       const dap_mlkem_ctx_t *a_ctx)
{
    dap_mlkem_polyvec l_bp;
    dap_mlkem_poly l_v, l_mp;

    s_unpack_ciphertext(&l_bp, &l_v, a_c);
    MLKEM_NAMESPACE(_polyvec_ntt)(&l_bp);

    /* basemul(a,b) is commutative: use pre-computed sk_cache instead of per-call bp_cache */
    MLKEM_NAMESPACE(_polyvec_basemul_acc_montgomery_cached)(
        &l_mp, &l_bp, &a_ctx->sk_ntt, &a_ctx->sk_cache);

    MLKEM_NAMESPACE(_poly_invntt_tomont)(&l_mp);
    MLKEM_NAMESPACE(_poly_sub)(&l_mp, &l_v, &l_mp);
    MLKEM_NAMESPACE(_poly_reduce)(&l_mp);
    MLKEM_NAMESPACE(_poly_tomsg)(a_m, &l_mp);
}
