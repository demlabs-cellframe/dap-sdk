/*
 * Authors:
 * Dmitriy A. Gerasimov <gerasimov.dmitriy@demlabs.net>
 * DeM Labs Inc.   https://demlabs.net
 * Copyright  (c) 2026
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

/**
 * @file dap_gpu.c
 * @brief Core GPU module — backend selection, public API dispatch
 */

#include <string.h>
#include <pthread.h>

#include "dap_common.h"
#include "dap_gpu.h"
#include "dap_gpu_backend.h"

#define LOG_TAG "dap_gpu"

static bool                 s_initialized = false;
static dap_gpu_backend_ops_t s_backend_ops;
static dap_gpu_device_info_t s_device_info;
static dap_gpu_context_t    *s_context = NULL;
static pthread_once_t        s_init_once = PTHREAD_ONCE_INIT;

/*
 * Batch thresholds (item count) below which CPU SIMD beats GPU.
 *
 * Discrete GPU (PCIe): dominated by DMA transfer latency (~5-15 µs per
 * direction), so we need enough work to amortize the round-trip.
 *
 * UMA (Apple Silicon, integrated Vulkan/OpenCL, RPi VideoCore): no transfer
 * cost, only GPU dispatch overhead (~10-50 µs for command buffer submit +
 * shader launch). Threshold is much lower.
 */
#define DAP_GPU_MIN_BATCH_DISCRETE  256
#define DAP_GPU_MIN_BATCH_UMA        16

static const char *s_error_strings[] = {
    [DAP_GPU_OK]                  = "Success",
    [DAP_GPU_ERR_NO_BACKEND]      = "No GPU backend available",
    [DAP_GPU_ERR_NO_DEVICE]       = "No suitable GPU device found",
    [DAP_GPU_ERR_INIT_FAILED]     = "Backend initialization failed",
    [DAP_GPU_ERR_OUT_OF_MEMORY]   = "GPU out of memory",
    [DAP_GPU_ERR_INVALID_PARAM]   = "Invalid parameter",
    [DAP_GPU_ERR_SHADER_COMPILE]  = "Shader compile/load error",
    [DAP_GPU_ERR_PIPELINE_CREATE] = "Pipeline creation failed",
    [DAP_GPU_ERR_DISPATCH_FAILED] = "Compute dispatch failed",
    [DAP_GPU_ERR_TIMEOUT]         = "Operation timed out",
    [DAP_GPU_ERR_DEVICE_LOST]     = "GPU device lost",
    [DAP_GPU_ERR_NOT_INITIALIZED] = "GPU subsystem not initialized",
    [DAP_GPU_ERR_BUFFER_MAP_FAILED] = "Buffer mapping failed",
};

typedef struct {
    dap_gpu_backend_t type;
    bool (*probe)(dap_gpu_backend_ops_t *a_ops);
    const char *name;
} s_backend_entry_t;

static const s_backend_entry_t s_backends[] = {
#ifdef DAP_GPU_VULKAN
    { DAP_GPU_BACKEND_VULKAN,  dap_gpu_vulkan_probe,  "Vulkan" },
#endif
#ifdef DAP_GPU_METAL
    { DAP_GPU_BACKEND_METAL,   dap_gpu_metal_probe,   "Metal"  },
#endif
#ifdef DAP_GPU_CUDA
    { DAP_GPU_BACKEND_CUDA,    dap_gpu_cuda_probe,    "CUDA"   },
#endif
#ifdef DAP_GPU_OPENCL
    { DAP_GPU_BACKEND_OPENCL,  dap_gpu_opencl_probe,  "OpenCL" },
#endif
    { DAP_GPU_BACKEND_NONE, NULL, NULL }
};

static bool s_try_backend(const s_backend_entry_t *a_entry)
{
    dap_gpu_backend_ops_t l_ops;
    memset(&l_ops, 0, sizeof(l_ops));

    if (!a_entry->probe || !a_entry->probe(&l_ops)) {
        log_it(L_DEBUG, "GPU backend %s: not available", a_entry->name);
        return false;
    }
    if (!l_ops.init) {
        log_it(L_WARNING, "GPU backend %s: probe OK but no init function", a_entry->name);
        return false;
    }

    memset(&s_device_info, 0, sizeof(s_device_info));
    dap_gpu_error_t l_rc = l_ops.init(&s_device_info);
    if (l_rc != DAP_GPU_OK) {
        log_it(L_WARNING, "GPU backend %s: init failed — %s", a_entry->name,
               dap_gpu_error_str(l_rc));
        if (l_ops.deinit)
            l_ops.deinit();
        return false;
    }

    s_backend_ops = l_ops;
    s_backend_ops.type = a_entry->type;
    s_device_info.backend = a_entry->type;

    log_it(L_NOTICE, "GPU initialized: %s [%s], VRAM: %llu MB, subgroup: %u, %s",
           s_device_info.name, a_entry->name,
           (unsigned long long)(s_device_info.vram_bytes / (1024 * 1024)),
           s_device_info.subgroup_size,
           s_device_info.unified_memory ? "UMA (zero-copy)" : "discrete (PCIe)");
    return true;
}

dap_gpu_error_t dap_gpu_init(dap_gpu_backend_t a_preferred)
{
    if (s_initialized) {
        log_it(L_WARNING, "GPU already initialized");
        return DAP_GPU_OK;
    }

    memset(&s_backend_ops, 0, sizeof(s_backend_ops));

    if (a_preferred != DAP_GPU_BACKEND_NONE) {
        for (const s_backend_entry_t *e = s_backends; e->name; ++e) {
            if (e->type == a_preferred) {
                if (s_try_backend(e)) {
                    s_initialized = true;
                    return DAP_GPU_OK;
                }
                log_it(L_WARNING, "Preferred backend %s unavailable, trying others...",
                       e->name);
                break;
            }
        }
    }

    for (const s_backend_entry_t *e = s_backends; e->name; ++e) {
        if (s_try_backend(e)) {
            s_initialized = true;
            return DAP_GPU_OK;
        }
    }

    log_it(L_INFO, "No GPU backend available — all crypto operations will use CPU");
    return DAP_GPU_ERR_NO_BACKEND;
}

void dap_gpu_deinit(void)
{
    if (!s_initialized)
        return;
    if (s_backend_ops.deinit)
        s_backend_ops.deinit();

    memset(&s_backend_ops, 0, sizeof(s_backend_ops));
    memset(&s_device_info, 0, sizeof(s_device_info));
    s_context = NULL;
    s_initialized = false;
    log_it(L_NOTICE, "GPU subsystem deinitialized");
}

bool dap_gpu_is_available(void)
{
    return s_initialized;
}

dap_gpu_backend_t dap_gpu_get_backend(void)
{
    return s_initialized ? s_backend_ops.type : DAP_GPU_BACKEND_NONE;
}

const dap_gpu_device_info_t *dap_gpu_get_device_info(void)
{
    return s_initialized ? &s_device_info : NULL;
}

dap_gpu_context_t *dap_gpu_get_context(void)
{
    return s_context;
}

const char *dap_gpu_error_str(dap_gpu_error_t a_err)
{
    if ((unsigned)a_err < sizeof(s_error_strings) / sizeof(s_error_strings[0])
        && s_error_strings[a_err])
        return s_error_strings[a_err];
    return "Unknown GPU error";
}

/* ========================================================================== */
/*                         Buffer operations                                  */
/* ========================================================================== */

dap_gpu_error_t dap_gpu_buffer_create(size_t a_size, uint32_t a_usage,
                                      dap_gpu_mem_type_t a_mem_type,
                                      dap_gpu_buffer_t **a_out_buf)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_out_buf || a_size == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_create)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_create(a_size, a_usage, a_mem_type, a_out_buf);
}

void dap_gpu_buffer_destroy(dap_gpu_buffer_t *a_buf)
{
    if (!a_buf || !s_initialized)
        return;
    if (s_backend_ops.buffer_destroy)
        s_backend_ops.buffer_destroy(a_buf);
}

dap_gpu_error_t dap_gpu_buffer_upload(dap_gpu_buffer_t *a_buf,
                                      const void *a_data, size_t a_size,
                                      size_t a_offset)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_buf || !a_data || a_size == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_upload)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_upload(a_buf, a_data, a_size, a_offset);
}

dap_gpu_error_t dap_gpu_buffer_download(dap_gpu_buffer_t *a_buf,
                                        void *a_data, size_t a_size,
                                        size_t a_offset)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_buf || !a_data || a_size == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_download)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_download(a_buf, a_data, a_size, a_offset);
}

dap_gpu_error_t dap_gpu_buffer_map(dap_gpu_buffer_t *a_buf, void **a_out_ptr)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_buf || !a_out_ptr)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_map)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_map(a_buf, a_out_ptr);
}

void dap_gpu_buffer_unmap(dap_gpu_buffer_t *a_buf)
{
    if (!a_buf || !s_initialized)
        return;
    if (s_backend_ops.buffer_unmap)
        s_backend_ops.buffer_unmap(a_buf);
}

size_t dap_gpu_buffer_size(const dap_gpu_buffer_t *a_buf)
{
    if (!a_buf || !s_initialized || !s_backend_ops.buffer_size)
        return 0;
    return s_backend_ops.buffer_size(a_buf);
}

/* ========================================================================== */
/*                        Pipeline operations                                 */
/* ========================================================================== */

dap_gpu_error_t dap_gpu_pipeline_create(const dap_gpu_pipeline_desc_t *a_desc,
                                        dap_gpu_pipeline_t **a_out_pipe)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_desc || !a_out_pipe || !a_desc->shader_code || a_desc->shader_code_size == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.pipeline_create)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.pipeline_create(a_desc, a_out_pipe);
}

void dap_gpu_pipeline_destroy(dap_gpu_pipeline_t *a_pipe)
{
    if (!a_pipe || !s_initialized)
        return;
    if (s_backend_ops.pipeline_destroy)
        s_backend_ops.pipeline_destroy(a_pipe);
}

/* ========================================================================== */
/*                     Async transfer + batch dispatch                        */
/* ========================================================================== */

dap_gpu_error_t dap_gpu_buffer_upload_async(dap_gpu_buffer_t *a_buf,
                                            const void *a_data, size_t a_size,
                                            size_t a_offset,
                                            dap_gpu_fence_t **a_out_fence)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_buf || !a_data || a_size == 0 || !a_out_fence)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_upload_async)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_upload_async(a_buf, a_data, a_size, a_offset, a_out_fence);
}

dap_gpu_error_t dap_gpu_buffer_download_async(dap_gpu_buffer_t *a_buf,
                                              void *a_data, size_t a_size,
                                              size_t a_offset,
                                              dap_gpu_fence_t **a_out_fence)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_buf || !a_data || a_size == 0 || !a_out_fence)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.buffer_download_async)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.buffer_download_async(a_buf, a_data, a_size, a_offset, a_out_fence);
}

dap_gpu_error_t dap_gpu_batch_begin(void)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!s_backend_ops.batch_begin)
        return DAP_GPU_ERR_INIT_FAILED;
    return s_backend_ops.batch_begin();
}

dap_gpu_error_t dap_gpu_batch_dispatch(const dap_gpu_dispatch_desc_t *a_desc)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_desc || !a_desc->pipeline || a_desc->group_count[0] == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.batch_dispatch)
        return DAP_GPU_ERR_INIT_FAILED;
    return s_backend_ops.batch_dispatch(a_desc);
}

dap_gpu_error_t dap_gpu_batch_end(dap_gpu_fence_t **a_out_fence)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!s_backend_ops.batch_end)
        return DAP_GPU_ERR_INIT_FAILED;
    return s_backend_ops.batch_end(a_out_fence);
}

/* ========================================================================== */
/*                        Dispatch + sync                                     */
/* ========================================================================== */

dap_gpu_error_t dap_gpu_dispatch(const dap_gpu_dispatch_desc_t *a_desc,
                                 dap_gpu_fence_t **a_out_fence)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_desc || !a_desc->pipeline)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (a_desc->group_count[0] == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.dispatch)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.dispatch(a_desc, a_out_fence);
}

dap_gpu_error_t dap_gpu_fence_wait(dap_gpu_fence_t *a_fence, uint64_t a_timeout_ns)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!a_fence)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!s_backend_ops.fence_wait)
        return DAP_GPU_ERR_INIT_FAILED;

    return s_backend_ops.fence_wait(a_fence, a_timeout_ns);
}

bool dap_gpu_fence_is_signaled(dap_gpu_fence_t *a_fence)
{
    if (!a_fence || !s_initialized || !s_backend_ops.fence_is_signaled)
        return true;
    return s_backend_ops.fence_is_signaled(a_fence);
}

void dap_gpu_fence_destroy(dap_gpu_fence_t *a_fence)
{
    if (!a_fence || !s_initialized)
        return;
    if (s_backend_ops.fence_destroy)
        s_backend_ops.fence_destroy(a_fence);
}

dap_gpu_error_t dap_gpu_wait_idle(void)
{
    if (!s_initialized)
        return DAP_GPU_ERR_NOT_INITIALIZED;
    if (!s_backend_ops.wait_idle)
        return DAP_GPU_ERR_INIT_FAILED;
    return s_backend_ops.wait_idle();
}

/* ========================================================================== */
/*                       Batch threshold helpers                              */
/* ========================================================================== */

uint32_t dap_gpu_min_batch_size(void)
{
    if (!s_initialized)
        return UINT32_MAX;
    return s_device_info.unified_memory ? DAP_GPU_MIN_BATCH_UMA
                                        : DAP_GPU_MIN_BATCH_DISCRETE;
}

bool dap_gpu_should_use(uint32_t a_batch_count, size_t a_item_bytes)
{
    if (!s_initialized)
        return false;

    uint32_t l_min = s_device_info.unified_memory ? DAP_GPU_MIN_BATCH_UMA
                                                  : DAP_GPU_MIN_BATCH_DISCRETE;
    if (a_batch_count < l_min)
        return false;

    if (!s_device_info.unified_memory) {
        /*
         * Discrete GPU: PCIe ~12-16 GB/s, data travels both directions.
         * Below ~64 KB total, the DMA setup + round-trip latency exceeds
         * what CPU SIMD can compute in the same wall time.
         */
        size_t l_total_bytes = (size_t)a_batch_count * a_item_bytes;
        if (l_total_bytes < 64 * 1024)
            return false;
    }
    /* UMA: no transfer cost, only dispatch overhead — batch count check above suffices */

    return true;
}
