/**
 * @file chipmunk_batch_verify_hots.c
 * @brief Batch Chipmunk HOTS signature verification with optional GPU NTT.
 *
 * Gathers all forward NTT operations across multiple independent HOTS
 * verifications into contiguous arrays, dispatches them in one GPU batch,
 * then performs pointwise operations on CPU.
 *
 * Per verify:
 *   - 6 NTTs for A[0..5] generation
 *   - 1 NTT for H(m)
 *   - 2 NTTs for v0, v1
 *   - 6 NTTs for sigma[0..5]
 *   Total: 15 forward NTTs per signature.
 *
 * Batch of 50 signatures -> 750 forward NTTs.
 */

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_ntt.h"
#include "chipmunk.h"
#include "chipmunk_ntt.h"
#include "chipmunk_poly.h"
#include "chipmunk_hash.h"
#include "chipmunk_hots.h"

#ifdef DAP_HAS_GPU
#include "dap_gpu.h"
#include "dap_gpu_ntt.h"
#endif

#define LOG_TAG "chipmunk_batch_verify"

extern const dap_ntt_params_t g_chipmunk_ntt_params;

typedef struct {
    chipmunk_public_key_t pk;
    chipmunk_signature_t  sig;
    chipmunk_hots_params_t hots_params;
    chipmunk_poly_t       hm;
    uint8_t               valid;
} s_chipmunk_ctx_t;

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
    const dap_ntt_params_t *p = &g_chipmunk_ntt_params;
    if (dap_gpu_is_available()) {
        dap_gpu_ntt_plan_create_plain(p->n, p->q, p->one_over_n,
                                      p->zetas, p->zetas_inv,
                                      p->zetas_len, &s_gpu_plan);
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

#endif

static void s_batch_forward_ntt(int32_t *a_buf, uint32_t a_total,
                                const dap_ntt_params_t *a_ntt)
{
#ifdef DAP_HAS_GPU
    dap_gpu_ntt_plan_t *l_plan = s_get_gpu_plan();
    if (l_plan && a_total >= GPU_BATCH_THRESHOLD) {
        if (dap_gpu_ntt_forward(l_plan, a_buf, a_total) == DAP_GPU_OK)
            return;
    }
#endif
    for (uint32_t i = 0; i < a_total; i++)
        dap_ntt_forward(a_buf + (size_t)i * CHIPMUNK_N, a_ntt);
}

int chipmunk_batch_verify_hots(
    const uint8_t **a_public_keys,
    const uint8_t **a_messages,
    const size_t *a_msg_lens,
    const uint8_t **a_signatures,
    unsigned int a_count,
    int *a_results)
{
    if (!a_count || !a_public_keys || !a_messages || !a_signatures || !a_results)
        return -1;

    const dap_ntt_params_t *l_ntt = &g_chipmunk_ntt_params;

    s_chipmunk_ctx_t *l_ctx = calloc(a_count, sizeof(s_chipmunk_ctx_t));
    if (!l_ctx) return -1;

    unsigned int l_valid = 0;
    for (unsigned int i = 0; i < a_count; i++) {
        a_results[i] = -1;

        if (chipmunk_public_key_from_bytes(&l_ctx[i].pk, a_public_keys[i]) != 0)
            continue;
        if (chipmunk_signature_from_bytes(&l_ctx[i].sig, a_signatures[i]) != 0)
            continue;
        if (chipmunk_poly_from_hash(&l_ctx[i].hm, a_messages[i], a_msg_lens[i]) != 0)
            continue;

        bool l_a_ok = true;
        for (int j = 0; j < CHIPMUNK_GAMMA && l_a_ok; j++) {
            if (dap_chipmunk_hash_sample_matrix(l_ctx[i].hots_params.a[j].coeffs,
                                                l_ctx[i].pk.rho_seed, j) != 0)
                l_a_ok = false;
        }
        if (!l_a_ok) continue;

        l_ctx[i].valid = 1;
        l_valid++;
    }

    if (l_valid == 0) {
        free(l_ctx);
        return 0;
    }

    uint32_t *l_vidx = malloc(l_valid * sizeof(uint32_t));
    if (!l_vidx) { free(l_ctx); return -1; }
    {
        uint32_t vi = 0;
        for (unsigned int i = 0; i < a_count; i++)
            if (l_ctx[i].valid)
                l_vidx[vi++] = i;
    }

    /*
     * Phase 1: Gather all polynomials for forward NTT.
     * Layout per sig: [A[0..5] | H(m) | v0 | v1 | sigma[0..5]]
     *                  = 6 + 1 + 2 + 6 = 15 polys
     */
    const uint32_t l_fwd_per = CHIPMUNK_GAMMA + 1 + 2 + CHIPMUNK_GAMMA;
    uint32_t l_fwd_total = l_valid * l_fwd_per;
    int32_t *l_fwd_buf = malloc((size_t)l_fwd_total * CHIPMUNK_N * sizeof(int32_t));
    if (!l_fwd_buf) { free(l_vidx); free(l_ctx); return -1; }

    for (uint32_t vi = 0; vi < l_valid; vi++) {
        uint32_t i = l_vidx[vi];
        int32_t *base = l_fwd_buf + (size_t)vi * l_fwd_per * CHIPMUNK_N;
        uint32_t off = 0;

        for (int j = 0; j < CHIPMUNK_GAMMA; j++) {
            memcpy(base + off, l_ctx[i].hots_params.a[j].coeffs, CHIPMUNK_N * sizeof(int32_t));
            off += CHIPMUNK_N;
        }
        memcpy(base + off, l_ctx[i].hm.coeffs, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        memcpy(base + off, l_ctx[i].pk.v0.coeffs, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        memcpy(base + off, l_ctx[i].pk.v1.coeffs, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        for (int j = 0; j < CHIPMUNK_GAMMA; j++) {
            memcpy(base + off, l_ctx[i].sig.sigma[j].coeffs, CHIPMUNK_N * sizeof(int32_t));
            off += CHIPMUNK_N;
        }
    }

    s_batch_forward_ntt(l_fwd_buf, l_fwd_total, l_ntt);

    /* Scatter NTT results back into context structures */
    for (uint32_t vi = 0; vi < l_valid; vi++) {
        uint32_t i = l_vidx[vi];
        int32_t *base = l_fwd_buf + (size_t)vi * l_fwd_per * CHIPMUNK_N;
        uint32_t off = 0;

        for (int j = 0; j < CHIPMUNK_GAMMA; j++) {
            memcpy(l_ctx[i].hots_params.a[j].coeffs, base + off, CHIPMUNK_N * sizeof(int32_t));
            off += CHIPMUNK_N;
        }
        chipmunk_poly_t l_hm_ntt, l_v0_ntt, l_v1_ntt;
        chipmunk_poly_t l_sigma_ntt[CHIPMUNK_GAMMA];

        memcpy(l_hm_ntt.coeffs, base + off, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        memcpy(l_v0_ntt.coeffs, base + off, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        memcpy(l_v1_ntt.coeffs, base + off, CHIPMUNK_N * sizeof(int32_t));
        off += CHIPMUNK_N;
        for (int j = 0; j < CHIPMUNK_GAMMA; j++) {
            memcpy(l_sigma_ntt[j].coeffs, base + off, CHIPMUNK_N * sizeof(int32_t));
            off += CHIPMUNK_N;
        }

        /* Phase 2 (CPU): pointwise operations */
        chipmunk_poly_t l_left_ntt;
        memset(&l_left_ntt, 0, sizeof(l_left_ntt));
        for (int j = 0; j < CHIPMUNK_GAMMA; j++) {
            chipmunk_poly_t l_term;
            chipmunk_poly_mul_ntt(&l_term, &l_ctx[i].hots_params.a[j], &l_sigma_ntt[j]);
            if (j == 0)
                l_left_ntt = l_term;
            else
                chipmunk_poly_add_ntt(&l_left_ntt, &l_left_ntt, &l_term);
        }

        chipmunk_poly_t l_hm_v0, l_right_ntt;
        chipmunk_poly_mul_ntt(&l_hm_v0, &l_hm_ntt, &l_v0_ntt);
        chipmunk_poly_add_ntt(&l_right_ntt, &l_hm_v0, &l_v1_ntt);

        a_results[i] = chipmunk_poly_equal(&l_left_ntt, &l_right_ntt) ? 0 : -1;
    }

    free(l_fwd_buf);

    int l_passed = 0;
    for (unsigned int i = 0; i < a_count; i++)
        if (a_results[i] == 0) l_passed++;

    log_it(L_INFO, "Chipmunk batch HOTS verify: %d/%u passed (%u fwd NTTs batched)",
           l_passed, a_count, l_fwd_total);

    free(l_vidx);
    free(l_ctx);
    return l_passed;
}
