/*
 * test_gpu_ntt.c — Correctness test for GPU batch Montgomery NTT
 *
 * Compares GPU NTT output against CPU reference (dap_ntt_forward_mont)
 * using Dilithium parameters (N=256, q=8380417).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#include "dap_common.h"
#include "dap_ntt.h"
#include "dap_gpu.h"
#include "dap_gpu_ntt.h"

extern const dap_ntt_params_t g_dilithium_ntt_params;

static void s_random_poly(int32_t *a_coeffs, uint32_t a_n, int32_t a_q)
{
    for (uint32_t i = 0; i < a_n; ++i)
        a_coeffs[i] = (int32_t)((uint32_t)rand() % (uint32_t)a_q);
}

static int s_test_forward_ntt(dap_gpu_ntt_plan_t *a_plan,
                               const dap_ntt_params_t *a_params,
                               uint32_t a_batch)
{
    uint32_t l_n = a_params->n;
    size_t l_total = (size_t)a_batch * l_n;

    int32_t *l_gpu_data = malloc(l_total * sizeof(int32_t));
    int32_t *l_cpu_data = malloc(l_total * sizeof(int32_t));
    if (!l_gpu_data || !l_cpu_data) {
        free(l_gpu_data);
        free(l_cpu_data);
        return -1;
    }

    for (uint32_t i = 0; i < a_batch; i++)
        s_random_poly(l_gpu_data + (size_t)i * l_n, l_n, a_params->q);
    memcpy(l_cpu_data, l_gpu_data, l_total * sizeof(int32_t));

    for (uint32_t i = 0; i < a_batch; i++)
        dap_ntt_forward_mont(l_cpu_data + (size_t)i * l_n, a_params);

    dap_gpu_error_t l_rc = dap_gpu_ntt_forward_mont(a_plan, l_gpu_data, a_batch);
    if (l_rc != DAP_GPU_OK) {
        printf("  FAIL: batch=%u, GPU dispatch: %s\n", a_batch, dap_gpu_error_str(l_rc));
        free(l_gpu_data);
        free(l_cpu_data);
        return 1;
    }

    int l_mismatches = 0;
    for (size_t i = 0; i < l_total; i++) {
        if (l_gpu_data[i] != l_cpu_data[i]) {
            if (l_mismatches == 0) {
                uint32_t poly = (uint32_t)(i / l_n);
                uint32_t coeff = (uint32_t)(i % l_n);
                printf("  FAIL: batch=%u, poly[%u] coeff[%u]: GPU=%d CPU=%d\n",
                       a_batch, poly, coeff, l_gpu_data[i], l_cpu_data[i]);
            }
            l_mismatches++;
        }
    }

    free(l_gpu_data);
    free(l_cpu_data);

    if (l_mismatches == 0) {
        printf("  PASS: forward NTT batch=%u (%zu coefficients)\n", a_batch, l_total);
        return 0;
    }
    printf("  FAIL: %d/%zu mismatches in batch=%u\n", l_mismatches, l_total, a_batch);
    return 1;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    srand((unsigned)time(NULL));

    dap_log_level_set(L_INFO);

    printf("=== GPU NTT Correctness Test ===\n\n");

    /* Ensure CPU NTT dispatch is initialized */
    dap_ntt_dispatch_init();

    /* Initialize GPU */
    dap_gpu_error_t l_rc = dap_gpu_init(DAP_GPU_BACKEND_NONE);
    if (l_rc != DAP_GPU_OK) {
        printf("GPU init failed: %s — skipping GPU tests\n", dap_gpu_error_str(l_rc));
        return 0;
    }

    const dap_gpu_device_info_t *l_info = dap_gpu_get_device_info();
    printf("GPU: %s (%s)\n\n", l_info->name,
           l_info->unified_memory ? "UMA" : "discrete");

    const dap_ntt_params_t *l_params = &g_dilithium_ntt_params;

    dap_gpu_ntt_plan_t *l_plan = NULL;
    l_rc = dap_gpu_ntt_plan_create(l_params->n, l_params->q, l_params->qinv,
                                    l_params->zetas, l_params->zetas_inv,
                                    l_params->zetas_len, &l_plan);
    if (l_rc != DAP_GPU_OK) {
        printf("Plan creation failed: %s\n", dap_gpu_error_str(l_rc));
        dap_gpu_deinit();
        return 1;
    }

    const uint32_t l_batch_sizes[] = { 1, 4, 16, 64, 256, 1024 };
    int l_num_tests = sizeof(l_batch_sizes) / sizeof(l_batch_sizes[0]);
    int l_passed = 0, l_failed = 0;

    printf("--- Forward NTT (Dilithium: N=%u, q=%d) ---\n", l_params->n, l_params->q);
    for (int t = 0; t < l_num_tests; t++) {
        int r = s_test_forward_ntt(l_plan, l_params, l_batch_sizes[t]);
        if (r == 0) l_passed++;
        else if (r > 0) l_failed++;
    }

    printf("\n--- Inverse NTT ---\n");
    for (int t = 0; t < l_num_tests; t++) {
        uint32_t l_batch = l_batch_sizes[t];
        uint32_t l_n = l_params->n;
        size_t l_total = (size_t)l_batch * l_n;

        int32_t *l_gpu = malloc(l_total * sizeof(int32_t));
        int32_t *l_cpu = malloc(l_total * sizeof(int32_t));
        for (uint32_t i = 0; i < l_batch; i++)
            s_random_poly(l_gpu + (size_t)i * l_n, l_n, l_params->q);

        /* Apply forward NTT on CPU first */
        memcpy(l_cpu, l_gpu, l_total * sizeof(int32_t));
        for (uint32_t i = 0; i < l_batch; i++) {
            dap_ntt_forward_mont(l_cpu + (size_t)i * l_n, l_params);
            dap_ntt_forward_mont(l_gpu + (size_t)i * l_n, l_params);
        }

        /* Then inverse on both */
        for (uint32_t i = 0; i < l_batch; i++)
            dap_ntt_inverse_mont(l_cpu + (size_t)i * l_n, l_params);

        dap_gpu_ntt_inverse_mont(l_plan, l_gpu, l_batch);

        int l_mm = 0;
        for (size_t i = 0; i < l_total; i++)
            if (l_gpu[i] != l_cpu[i]) l_mm++;

        if (l_mm == 0) {
            printf("  PASS: inverse NTT batch=%u\n", l_batch);
            l_passed++;
        } else {
            printf("  FAIL: inverse NTT batch=%u (%d mismatches)\n", l_batch, l_mm);
            l_failed++;
        }
        free(l_gpu); free(l_cpu);
    }

    printf("\n=== Results: %d passed, %d failed ===\n", l_passed, l_failed);

    dap_gpu_ntt_plan_destroy(l_plan);
    dap_gpu_deinit();
    return l_failed > 0 ? 1 : 0;
}
