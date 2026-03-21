/**
 * @file dilithium_batch_verify.c
 * @brief Batch Dilithium signature verification with optional GPU acceleration.
 *
 * Batches all NTT operations across multiple independent verifications
 * into contiguous arrays, then executes them via GPU (when available)
 * or CPU. Even the CPU path benefits from improved data locality.
 *
 * For ML-DSA-44 (L=4, K=4), each verify needs:
 *   Phase 1: L + 1 + K = 9 forward NTTs  (z, chat, t1)
 *   Phase 2: K = 4 inverse NTTs           (after pointwise ops)
 *   Total: 13 NTTs per signature
 *
 * Batch of 100 signatures -> 900 forward + 400 inverse NTTs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_ntt.h"
#include "dap_hash_sha3.h"
#include "dap_hash_shake256.h"
#include "dilithium_params.h"
#include "dilithium_poly.h"
#include "dilithium_polyvec.h"
#include "dilithium_packing.h"
#include "dilithium_rounding_reduce.h"

#ifdef DAP_HAS_GPU
#include "dap_gpu.h"
#include "dap_gpu_ntt.h"
#endif

#define LOG_TAG "dilithium_batch"

extern const dap_ntt_params_t g_dilithium_ntt_params;
extern void expand_mat(polyvecl mat[], const unsigned char rho[SEEDBYTES], dilithium_param_t *p);
extern void challenge(poly *c, const unsigned char mu[CRHBYTES], const polyveck *w1, dilithium_param_t *p);

typedef struct {
    polyvecl z;
    polyveck t1, h;
    poly c, chat;
    unsigned char rho[SEEDBYTES];
    unsigned char mu[CRHBYTES];
    uint8_t valid;
} s_sig_ctx_t;

/* ===== Thread-safe GPU plan management ===== */

#ifdef DAP_HAS_GPU

static dap_gpu_ntt_plan_t *s_gpu_plan = NULL;
static pthread_once_t s_gpu_plan_once = PTHREAD_ONCE_INIT;

static void s_gpu_plan_cleanup(void)
{
    if (s_gpu_plan) {
        dap_gpu_ntt_plan_destroy(s_gpu_plan);
        s_gpu_plan = NULL;
    }
}

static void s_gpu_plan_init(void)
{
    const dap_ntt_params_t *l_ntt = &g_dilithium_ntt_params;
    if (dap_gpu_is_available()) {
        dap_gpu_ntt_plan_create(l_ntt->n, l_ntt->q, l_ntt->qinv,
                                l_ntt->zetas, l_ntt->zetas_inv,
                                l_ntt->zetas_len, &s_gpu_plan);
        if (s_gpu_plan)
            atexit(s_gpu_plan_cleanup);
    }
}

static inline dap_gpu_ntt_plan_t *s_get_gpu_plan(void)
{
    pthread_once(&s_gpu_plan_once, s_gpu_plan_init);
    return s_gpu_plan;
}

#define GPU_BATCH_THRESHOLD 32

#endif /* DAP_HAS_GPU */

static int s_batch_forward_ntt(int32_t *a_buf, uint32_t a_total,
                               const dap_ntt_params_t *a_ntt)
{
#ifdef DAP_HAS_GPU
    dap_gpu_ntt_plan_t *l_plan = s_get_gpu_plan();
    if (l_plan && a_total >= GPU_BATCH_THRESHOLD) {
        if (dap_gpu_ntt_forward_mont(l_plan, a_buf, a_total) == DAP_GPU_OK)
            return 1;
    }
#endif
    for (uint32_t i = 0; i < a_total; i++)
        dap_ntt_forward_mont(a_buf + (size_t)i * NN, a_ntt);
    return 0;
}

static int s_batch_inverse_ntt(int32_t *a_buf, uint32_t a_total,
                               const dap_ntt_params_t *a_ntt)
{
#ifdef DAP_HAS_GPU
    dap_gpu_ntt_plan_t *l_plan = s_get_gpu_plan();
    if (l_plan && a_total >= GPU_BATCH_THRESHOLD) {
        if (dap_gpu_ntt_inverse_mont(l_plan, a_buf, a_total) == DAP_GPU_OK)
            return 1;
    }
#endif
    for (uint32_t i = 0; i < a_total; i++)
        dap_ntt_inverse_mont(a_buf + (size_t)i * NN, a_ntt);
    return 0;
}

int dilithium_crypto_sign_open_batch(
    unsigned char **a_msgs,
    unsigned long long *a_msg_lens,
    dilithium_signature_t **a_sigs,
    const dilithium_public_key_t **a_pub_keys,
    unsigned int a_count,
    int *a_results)
{
    if (!a_count || !a_msgs || !a_sigs || !a_pub_keys || !a_results)
        return -1;

    dilithium_param_t l_params;
    if (!dilithium_params_init(&l_params, a_sigs[0]->kind))
        return -1;

    const uint32_t L = l_params.PARAM_L;
    const uint32_t K = l_params.PARAM_K;
    const dap_ntt_params_t *l_ntt = &g_dilithium_ntt_params;

    s_sig_ctx_t *l_ctx = calloc(a_count, sizeof(s_sig_ctx_t));
    if (!l_ctx) return -1;

    unsigned int l_valid_count = 0;
    for (unsigned int i = 0; i < a_count; i++) {
        a_results[i] = -1;
        if (a_pub_keys[i]->kind != a_sigs[i]->kind) continue;
        if (a_sigs[i]->sig_len < l_params.CRYPTO_BYTES) continue;
        if (a_sigs[i]->sig_len - l_params.CRYPTO_BYTES != a_msg_lens[i]) continue;

        dilithium_unpack_pk(l_ctx[i].rho, &l_ctx[i].t1,
                            a_pub_keys[i]->data, &l_params);
        if (dilithium_unpack_sig(&l_ctx[i].z, &l_ctx[i].h, &l_ctx[i].c,
                                  a_sigs[i]->sig_data, &l_params))
            continue;
        if (polyvecl_chknorm(&l_ctx[i].z, GAMMA1 - l_params.PARAM_BETA, &l_params))
            continue;

        unsigned char *l_tmp = malloc(CRHBYTES + a_msg_lens[i]);
        if (!l_tmp) { free(l_ctx); return -1; }
        dap_hash_shake256(l_tmp, CRHBYTES,
                          a_pub_keys[i]->data, l_params.CRYPTO_PUBLICKEYBYTES);
        memcpy(l_tmp + CRHBYTES, a_msgs[i], a_msg_lens[i]);
        dap_hash_shake256(l_ctx[i].mu, CRHBYTES, l_tmp, CRHBYTES + a_msg_lens[i]);
        free(l_tmp);

        l_ctx[i].chat = l_ctx[i].c;
        polyveck_shiftl(&l_ctx[i].t1, D, &l_params);
        l_ctx[i].valid = 1;
        l_valid_count++;
    }

    if (l_valid_count == 0) {
        free(l_ctx);
        return 0;
    }

    uint32_t *l_vidx = malloc(l_valid_count * sizeof(uint32_t));
    if (!l_vidx) { free(l_ctx); return -1; }
    {
        uint32_t vi = 0;
        for (unsigned int i = 0; i < a_count; i++)
            if (l_ctx[i].valid)
                l_vidx[vi++] = i;
    }

    /*
     * Phase 1: Gather all polynomials for forward NTT.
     * Layout per sig: [z.vec[0..L-1] | chat | t1.vec[0..K-1]]
     */
    uint32_t l_fwd_per = L + 1 + K;
    uint32_t l_fwd_total = l_valid_count * l_fwd_per;
    int32_t *l_fwd_buf = malloc((size_t)l_fwd_total * NN * sizeof(int32_t));
    if (!l_fwd_buf) { free(l_vidx); free(l_ctx); return -1; }

    for (uint32_t vi = 0; vi < l_valid_count; vi++) {
        uint32_t i = l_vidx[vi];
        int32_t *l_base = l_fwd_buf + (size_t)vi * l_fwd_per * NN;

        memcpy(l_base, l_ctx[i].z.vec[0].coeffs, (size_t)L * NN * sizeof(int32_t));
        memcpy(l_base + (size_t)L * NN,
               l_ctx[i].chat.coeffs, NN * sizeof(int32_t));
        for (uint32_t j = 0; j < K; j++)
            memcpy(l_base + (size_t)(L + 1 + j) * NN,
                   l_ctx[i].t1.vec[j].coeffs, NN * sizeof(int32_t));
    }

    int l_used_gpu = s_batch_forward_ntt(l_fwd_buf, l_fwd_total, l_ntt);

    for (uint32_t vi = 0; vi < l_valid_count; vi++) {
        uint32_t i = l_vidx[vi];
        int32_t *l_base = l_fwd_buf + (size_t)vi * l_fwd_per * NN;

        memcpy(l_ctx[i].z.vec[0].coeffs, l_base, (size_t)L * NN * sizeof(int32_t));
        memcpy(l_ctx[i].chat.coeffs,
               l_base + (size_t)L * NN, NN * sizeof(int32_t));
        for (uint32_t j = 0; j < K; j++)
            memcpy(l_ctx[i].t1.vec[j].coeffs,
                   l_base + (size_t)(L + 1 + j) * NN, NN * sizeof(int32_t));
    }
    free(l_fwd_buf);

    /*
     * Phase 2 (CPU): expand_mat, pointwise multiply, subtract.
     * Simultaneously gather inverse NTT input.
     */
    uint32_t l_inv_total = l_valid_count * K;
    int32_t *l_inv_buf = malloc((size_t)l_inv_total * NN * sizeof(int32_t));
    if (!l_inv_buf) { free(l_vidx); free(l_ctx); return -1; }

    for (uint32_t vi = 0; vi < l_valid_count; vi++) {
        uint32_t i = l_vidx[vi];
        polyvecl l_mat[l_params.PARAM_K];
        polyveck l_tmp1, l_tmp2;

        expand_mat(l_mat, l_ctx[i].rho, &l_params);

        for (uint32_t j = 0; j < K; j++)
            polyvecl_pointwise_acc_invmontgomery(
                l_tmp1.vec + j, l_mat + j, &l_ctx[i].z, &l_params);

        for (uint32_t j = 0; j < K; j++)
            poly_pointwise_invmontgomery(
                l_tmp2.vec + j, &l_ctx[i].chat, l_ctx[i].t1.vec + j);

        polyveck_sub(&l_tmp1, &l_tmp1, &l_tmp2, &l_params);
        polyveck_reduce(&l_tmp1, &l_params);

        int32_t *l_base = l_inv_buf + (size_t)vi * K * NN;
        for (uint32_t j = 0; j < K; j++)
            memcpy(l_base + (size_t)j * NN,
                   l_tmp1.vec[j].coeffs, NN * sizeof(int32_t));
    }

    /* Phase 3: Batch inverse NTT */
    s_batch_inverse_ntt(l_inv_buf, l_inv_total, l_ntt);

    /* Phase 4: Apply invntt_frominvmont scaling, check challenge */
    const int32_t l_f = (int32_t)(
        (((uint64_t)MONT * MONT % Q) * (Q - 1) % Q) *
        ((Q - 1) >> 8) % Q);

    int l_passed = 0;
    for (uint32_t vi = 0; vi < l_valid_count; vi++) {
        uint32_t i = l_vidx[vi];
        polyveck l_tmp1, l_w1;
        poly l_cp;

        int32_t *l_base = l_inv_buf + (size_t)vi * K * NN;
        for (uint32_t j = 0; j < K; j++)
            memcpy(l_tmp1.vec[j].coeffs,
                   l_base + (size_t)j * NN, NN * sizeof(int32_t));

        for (uint32_t j = 0; j < K; j++)
            for (uint32_t c = 0; c < NN; c++)
                l_tmp1.vec[j].coeffs[c] = (uint32_t)dap_ntt_montgomery_reduce(
                    (int64_t)l_f * (int32_t)l_tmp1.vec[j].coeffs[c], l_ntt);

        polyveck_csubq(&l_tmp1, &l_params);
        polyveck_use_hint(&l_w1, &l_tmp1, &l_ctx[i].h, &l_params);

        challenge(&l_cp, l_ctx[i].mu, &l_w1, &l_params);

        int l_ok = 1;
        for (uint32_t c = 0; c < NN; c++) {
            if (l_ctx[i].c.coeffs[c] != l_cp.coeffs[c]) {
                l_ok = 0;
                break;
            }
        }
        a_results[i] = l_ok ? 0 : -7;
        if (l_ok) l_passed++;
    }

    if (l_used_gpu)
        log_it(L_INFO, "Batch verify: %u/%u passed (GPU, %u fwd + %u inv NTTs)",
               l_passed, a_count, l_fwd_total, l_inv_total);
    else
        log_it(L_INFO, "Batch verify: %u/%u passed (CPU, %u fwd + %u inv NTTs)",
               l_passed, a_count, l_fwd_total, l_inv_total);

    free(l_inv_buf);
    free(l_vidx);
    free(l_ctx);
    return l_passed;
}
