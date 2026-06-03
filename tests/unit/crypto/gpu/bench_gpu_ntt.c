/*
 * bench_gpu_ntt.c — Benchmark GPU batch NTT vs CPU NTT
 *
 * Measures throughput (NTTs/sec) for increasing batch sizes to find
 * the break-even point where GPU becomes faster than CPU.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "dap_common.h"
#include "dap_ntt.h"
#include "dap_gpu.h"
#include "dap_gpu_ntt.h"

extern const dap_ntt_params_t g_dilithium_ntt_params;

static double s_time_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static void s_random_polys(int32_t *buf, uint32_t n, uint32_t batch, int32_t q)
{
    size_t total = (size_t)batch * n;
    for (size_t i = 0; i < total; i++)
        buf[i] = (int32_t)((uint32_t)rand() % (uint32_t)q);
}

static double s_bench_cpu(const dap_ntt_params_t *p, int32_t *data,
                          uint32_t batch, int reps)
{
    double best = 1e18;
    for (int r = 0; r < reps; r++) {
        s_random_polys(data, p->n, batch, p->q);
        double t0 = s_time_ms();
        for (uint32_t i = 0; i < batch; i++)
            dap_ntt_forward_mont(data + (size_t)i * p->n, p);
        double dt = s_time_ms() - t0;
        if (dt < best) best = dt;
    }
    return best;
}

static double s_bench_gpu(dap_gpu_ntt_plan_t *plan, const dap_ntt_params_t *p,
                          int32_t *data, uint32_t batch, int reps)
{
    double best = 1e18;
    for (int r = 0; r < reps; r++) {
        s_random_polys(data, p->n, batch, p->q);
        double t0 = s_time_ms();
        dap_gpu_ntt_forward_mont(plan, data, batch);
        double dt = s_time_ms() - t0;
        if (dt < best) best = dt;
    }
    return best;
}

int main(void)
{
    srand(42);
    dap_log_level_set(L_WARNING);
    dap_ntt_dispatch_init();

    printf("=== GPU vs CPU NTT Benchmark ===\n");
    printf("Dilithium: N=256, q=8380417\n\n");

    dap_gpu_error_t rc = dap_gpu_init(DAP_GPU_BACKEND_NONE);
    if (rc != DAP_GPU_OK) {
        printf("GPU unavailable: %s\n", dap_gpu_error_str(rc));
        return 1;
    }

    const dap_gpu_device_info_t *info = dap_gpu_get_device_info();
    printf("GPU: %s (%s)\n", info->name,
           info->unified_memory ? "UMA" : "discrete");
    printf("CPU: forward NTT dispatch (SIMD if available)\n\n");

    const dap_ntt_params_t *p = &g_dilithium_ntt_params;

    dap_gpu_ntt_plan_t *plan = NULL;
    rc = dap_gpu_ntt_plan_create(p->n, p->q, p->qinv,
                                  p->zetas, p->zetas_inv,
                                  p->zetas_len, &plan);
    if (rc != DAP_GPU_OK) {
        printf("Plan: %s\n", dap_gpu_error_str(rc));
        dap_gpu_deinit();
        return 1;
    }

    const uint32_t batch_sizes[] = {
        1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192
    };
    int num = sizeof(batch_sizes) / sizeof(batch_sizes[0]);

    size_t max_batch = batch_sizes[num - 1];
    int32_t *data = malloc(max_batch * p->n * sizeof(int32_t));

    printf("%-8s  %10s  %10s  %10s  %10s  %s\n",
           "Batch", "CPU (ms)", "GPU (ms)", "CPU NTT/s", "GPU NTT/s", "Winner");
    printf("%-8s  %10s  %10s  %10s  %10s  %s\n",
           "-----", "--------", "--------", "---------", "---------", "------");

    for (int i = 0; i < num; i++) {
        uint32_t batch = batch_sizes[i];

        int reps = batch <= 64 ? 20 : (batch <= 1024 ? 10 : 5);

        double cpu_ms = s_bench_cpu(p, data, batch, reps);
        double gpu_ms = s_bench_gpu(plan, p, data, batch, reps);

        double cpu_nps = batch / (cpu_ms / 1000.0);
        double gpu_nps = batch / (gpu_ms / 1000.0);

        const char *winner = gpu_ms < cpu_ms ? "GPU" : "CPU";
        double ratio = gpu_ms < cpu_ms ? cpu_ms / gpu_ms : gpu_ms / cpu_ms;

        printf("%-8u  %10.3f  %10.3f  %10.0f  %10.0f  %s (%.1fx)\n",
               batch, cpu_ms, gpu_ms, cpu_nps, gpu_nps, winner, ratio);
    }

    /* Transfer overhead estimate: buffer create + upload + download roundtrip */
    printf("--- Transfer overhead estimate ---\n\n");
    printf("%-10s  %12s  %12s  %12s\n",
           "Data size", "Upload", "Download", "Roundtrip");

    uint32_t xfer_batches[] = { 64, 256, 1024, 4096, 8192 };
    int nxfer = sizeof(xfer_batches) / sizeof(xfer_batches[0]);
    for (int i = 0; i < nxfer; i++) {
        uint32_t batch = xfer_batches[i];
        size_t bytes = (size_t)batch * p->n * sizeof(int32_t);
        s_random_polys(data, p->n, batch, p->q);

        dap_gpu_buffer_t *cbuf = NULL;
        dap_gpu_buffer_create(bytes,
                              DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_SRC | DAP_GPU_BUF_TRANSFER_DST,
                              DAP_GPU_MEM_DEVICE, &cbuf);

        double t0 = s_time_ms();
        dap_gpu_buffer_upload(cbuf, data, bytes, 0);
        double t_up = s_time_ms() - t0;

        t0 = s_time_ms();
        dap_gpu_buffer_download(cbuf, data, bytes, 0);
        double t_down = s_time_ms() - t0;

        dap_gpu_buffer_destroy(cbuf);

        printf("%-6u (%4zu KB)  %9.3f ms  %9.3f ms  %9.3f ms\n",
               batch, bytes / 1024, t_up, t_down, t_up + t_down);
    }

    printf("\nNote: llvmpipe is SOFTWARE Vulkan — dispatch runs on CPU.\n"
           "On real GPU hardware:\n"
           "  - Dispatch time drops to microseconds for NTT\n"
           "  - Transfer dominates on discrete GPU (PCIe)\n"
           "  - Transfer is ~free on UMA (Apple Silicon, integrated GPU)\n"
           "  - Break-even: ~16-64 batch on UMA, ~256-1024 on discrete\n");

    free(data);
    dap_gpu_ntt_plan_destroy(plan);
    dap_gpu_deinit();
    return 0;
}
