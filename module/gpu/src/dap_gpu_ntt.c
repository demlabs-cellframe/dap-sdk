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
 * @file dap_gpu_ntt.c
 * @brief GPU batch NTT — host-side implementation
 *
 * Creates GPU pipelines from embedded SPIR-V shaders, manages buffer
 * allocation for batch coefficient arrays, and dispatches NTT workgroups.
 *
 * Optimization: persistent coefficient buffer is grown on demand and reused
 * across calls, eliminating per-call vkAllocateMemory/vkFreeMemory overhead.
 */

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_gpu.h"
#include "dap_gpu_ntt.h"

#ifdef DAP_GPU_VULKAN
#include "ntt_mont_forward_spv.h"
#include "ntt_mont_inverse_spv.h"
#include "ntt_forward_spv.h"
#include "ntt_inverse_spv.h"
#include "ntt16_mont_forward_spv.h"
#include "ntt16_mont_inverse_spv.h"
#endif

#define LOG_TAG "dap_gpu_ntt"

struct dap_gpu_ntt_plan {
    uint32_t n;
    int32_t  q;
    uint32_t qinv;
    int32_t  one_over_n;
    bool     is_montgomery;

    dap_gpu_pipeline_t *pipeline_forward;
    dap_gpu_pipeline_t *pipeline_inverse;
    dap_gpu_buffer_t   *zetas_buf;
    dap_gpu_buffer_t   *zetas_inv_buf;

    /* Persistent coefficient buffer, grown on demand */
    dap_gpu_buffer_t   *coeffs_buf;
    size_t              coeffs_buf_capacity;
};

typedef struct {
    int32_t  q;
    uint32_t qinv;
    uint32_t n;
    uint32_t batch_size;
} s_ntt_push_constants_t;

#ifdef DAP_GPU_VULKAN

static dap_gpu_error_t s_ensure_coeffs_buf(dap_gpu_ntt_plan_t *a_plan, size_t a_needed)
{
    if (a_plan->coeffs_buf && a_plan->coeffs_buf_capacity >= a_needed)
        return DAP_GPU_OK;

    if (a_plan->coeffs_buf) {
        dap_gpu_buffer_destroy(a_plan->coeffs_buf);
        a_plan->coeffs_buf = NULL;
        a_plan->coeffs_buf_capacity = 0;
    }

    size_t l_alloc = a_needed;
    if (l_alloc < 4096)
        l_alloc = 4096;
    /* Round up to next power of 2 for geometric growth */
    size_t l_po2 = 1;
    while (l_po2 < l_alloc) l_po2 <<= 1;
    l_alloc = l_po2;

    dap_gpu_error_t l_rc = dap_gpu_buffer_create(
        l_alloc,
        DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_SRC | DAP_GPU_BUF_TRANSFER_DST,
        DAP_GPU_MEM_DEVICE, &a_plan->coeffs_buf);
    if (l_rc == DAP_GPU_OK)
        a_plan->coeffs_buf_capacity = l_alloc;
    return l_rc;
}

dap_gpu_error_t dap_gpu_ntt_plan_create(uint32_t a_n, int32_t a_q, uint32_t a_qinv,
                                        const int32_t *a_zetas,
                                        const int32_t *a_zetas_inv,
                                        uint32_t a_zetas_len,
                                        dap_gpu_ntt_plan_t **a_out_plan)
{
    if (!a_out_plan || !a_zetas || !a_zetas_inv || a_n == 0 || a_zetas_len == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!dap_gpu_is_available())
        return DAP_GPU_ERR_NOT_INITIALIZED;

    const dap_gpu_device_info_t *l_info = dap_gpu_get_device_info();
    if (l_info && !l_info->supports_int64) {
        log_it(L_INFO, "GPU device does not support int64 — using umulExtended fallback in shaders");
    }

    dap_gpu_ntt_plan_t *l_plan = calloc(1, sizeof(*l_plan));
    if (!l_plan)
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    l_plan->n = a_n;
    l_plan->q = a_q;
    l_plan->qinv = a_qinv;

    dap_gpu_error_t l_rc;
    size_t l_zetas_bytes = a_zetas_len * sizeof(int32_t);

    l_rc = dap_gpu_buffer_create(l_zetas_bytes,
                                 DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_buf);
    if (l_rc != DAP_GPU_OK) {
        free(l_plan);
        return l_rc;
    }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_buf, a_zetas, l_zetas_bytes, 0);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_buf);
        free(l_plan);
        return l_rc;
    }

    l_rc = dap_gpu_buffer_create(l_zetas_bytes,
                                 DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_inv_buf);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_buf);
        free(l_plan);
        return l_rc;
    }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_inv_buf, a_zetas_inv, l_zetas_bytes, 0);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf);
        free(l_plan);
        return l_rc;
    }

    dap_gpu_pipeline_desc_t l_fwd_desc = {
        .shader_code = ntt_mont_forward_spirv,
        .shader_code_size = ntt_mont_forward_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_fwd_desc, &l_plan->pipeline_forward);
    if (l_rc != DAP_GPU_OK) {
        log_it(L_ERROR, "Failed to create forward NTT pipeline: %s",
               dap_gpu_error_str(l_rc));
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf);
        free(l_plan);
        return l_rc;
    }

    dap_gpu_pipeline_desc_t l_inv_desc = {
        .shader_code = ntt_mont_inverse_spirv,
        .shader_code_size = ntt_mont_inverse_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_inv_desc, &l_plan->pipeline_inverse);
    if (l_rc != DAP_GPU_OK) {
        log_it(L_ERROR, "Failed to create inverse NTT pipeline: %s",
               dap_gpu_error_str(l_rc));
        dap_gpu_pipeline_destroy(l_plan->pipeline_forward);
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf);
        free(l_plan);
        return l_rc;
    }

    l_plan->is_montgomery = true;
    log_it(L_INFO, "GPU Montgomery NTT plan created: N=%u, q=%d, %u zetas",
           a_n, a_q, a_zetas_len);
    *a_out_plan = l_plan;
    return DAP_GPU_OK;
}

dap_gpu_error_t dap_gpu_ntt_plan_create_plain(uint32_t a_n, int32_t a_q,
                                              int32_t a_one_over_n,
                                              const int32_t *a_zetas,
                                              const int32_t *a_zetas_inv,
                                              uint32_t a_zetas_len,
                                              dap_gpu_ntt_plan_t **a_out_plan)
{
    if (!a_out_plan || !a_zetas || !a_zetas_inv || a_n == 0 || a_zetas_len == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!dap_gpu_is_available())
        return DAP_GPU_ERR_NOT_INITIALIZED;

    dap_gpu_ntt_plan_t *l_plan = calloc(1, sizeof(*l_plan));
    if (!l_plan)
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    l_plan->n = a_n;
    l_plan->q = a_q;
    l_plan->qinv = 0;
    l_plan->one_over_n = a_one_over_n;
    l_plan->is_montgomery = false;

    dap_gpu_error_t l_rc;
    size_t l_zetas_bytes = a_zetas_len * sizeof(int32_t);

    l_rc = dap_gpu_buffer_create(l_zetas_bytes,
                                 DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_buf);
    if (l_rc != DAP_GPU_OK) { free(l_plan); return l_rc; }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_buf, a_zetas, l_zetas_bytes, 0);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    l_rc = dap_gpu_buffer_create(l_zetas_bytes,
                                 DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_inv_buf);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_inv_buf, a_zetas_inv, l_zetas_bytes, 0);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    dap_gpu_pipeline_desc_t l_fwd_desc = {
        .shader_code = ntt_forward_spirv,
        .shader_code_size = ntt_forward_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_fwd_desc, &l_plan->pipeline_forward);
    if (l_rc != DAP_GPU_OK) {
        log_it(L_ERROR, "Failed to create plain forward NTT pipeline: %s",
               dap_gpu_error_str(l_rc));
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    dap_gpu_pipeline_desc_t l_inv_desc = {
        .shader_code = ntt_inverse_spirv,
        .shader_code_size = ntt_inverse_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_inv_desc, &l_plan->pipeline_inverse);
    if (l_rc != DAP_GPU_OK) {
        log_it(L_ERROR, "Failed to create plain inverse NTT pipeline: %s",
               dap_gpu_error_str(l_rc));
        dap_gpu_pipeline_destroy(l_plan->pipeline_forward);
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    log_it(L_INFO, "GPU plain NTT plan created: N=%u, q=%d, %u zetas",
           a_n, a_q, a_zetas_len);
    *a_out_plan = l_plan;
    return DAP_GPU_OK;
}

void dap_gpu_ntt_plan_destroy(dap_gpu_ntt_plan_t *a_plan)
{
    if (!a_plan)
        return;
    if (a_plan->coeffs_buf)
        dap_gpu_buffer_destroy(a_plan->coeffs_buf);
    dap_gpu_pipeline_destroy(a_plan->pipeline_inverse);
    dap_gpu_pipeline_destroy(a_plan->pipeline_forward);
    dap_gpu_buffer_destroy(a_plan->zetas_inv_buf);
    dap_gpu_buffer_destroy(a_plan->zetas_buf);
    free(a_plan);
}

static dap_gpu_error_t s_gpu_ntt_exec(dap_gpu_ntt_plan_t *a_plan,
                                       int32_t *a_coeffs,
                                       uint32_t a_batch_count,
                                       bool a_forward)
{
    size_t l_total_bytes = (size_t)a_batch_count * a_plan->n * sizeof(int32_t);

    dap_gpu_error_t l_rc = s_ensure_coeffs_buf(a_plan, l_total_bytes);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    l_rc = dap_gpu_buffer_upload(a_plan->coeffs_buf, a_coeffs, l_total_bytes, 0);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    s_ntt_push_constants_t l_pc = {
        .q = a_plan->q,
        .qinv = a_plan->is_montgomery ? a_plan->qinv
                : (a_forward ? 0 : (uint32_t)a_plan->one_over_n),
        .n = a_plan->n,
        .batch_size = a_batch_count,
    };

    dap_gpu_buffer_t *l_zetas_buf = a_forward ? a_plan->zetas_buf
                                               : a_plan->zetas_inv_buf;

    dap_gpu_binding_desc_t l_bindings[2] = {
        { .binding = 0, .buffer = a_plan->coeffs_buf, .offset = 0, .range = 0 },
        { .binding = 1, .buffer = l_zetas_buf,         .offset = 0, .range = 0 },
    };

    dap_gpu_dispatch_desc_t l_desc = {
        .pipeline = a_forward ? a_plan->pipeline_forward : a_plan->pipeline_inverse,
        .bindings = l_bindings,
        .num_bindings = 2,
        .push_constants = &l_pc,
        .push_constant_size = sizeof(l_pc),
        .group_count = { a_batch_count, 1, 1 },
    };

    dap_gpu_fence_t *l_fence = NULL;
    l_rc = dap_gpu_dispatch(&l_desc, &l_fence);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    l_rc = dap_gpu_fence_wait(l_fence, 0);
    dap_gpu_fence_destroy(l_fence);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    return dap_gpu_buffer_download(a_plan->coeffs_buf, a_coeffs, l_total_bytes, 0);
}

dap_gpu_error_t dap_gpu_ntt_forward_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;

    return s_gpu_ntt_exec(a_plan, a_coeffs, a_batch_count, true);
}

dap_gpu_error_t dap_gpu_ntt_inverse_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;

    return s_gpu_ntt_exec(a_plan, a_coeffs, a_batch_count, false);
}

dap_gpu_error_t dap_gpu_ntt_forward(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    return s_gpu_ntt_exec(a_plan, a_coeffs, a_batch_count, true);
}

dap_gpu_error_t dap_gpu_ntt_inverse(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    return s_gpu_ntt_exec(a_plan, a_coeffs, a_batch_count, false);
}

/* ===== 16-bit NTT for ML-KEM ===== */

struct dap_gpu_ntt16_plan {
    uint32_t n;
    int16_t  q;
    int16_t  qinv16;

    dap_gpu_pipeline_t *pipeline_forward;
    dap_gpu_pipeline_t *pipeline_inverse;
    dap_gpu_buffer_t   *zetas_buf;
    dap_gpu_buffer_t   *zetas_inv_buf;
    dap_gpu_buffer_t   *coeffs_buf;
    size_t              coeffs_buf_capacity;

    /* Persistent host-side staging buffer for int16<->int32 widening */
    int32_t *staging_buf;
    size_t   staging_capacity;  /* in int32 elements */
};

static dap_gpu_error_t s_ensure_coeffs_buf_16(dap_gpu_ntt16_plan_t *a_plan, size_t a_needed)
{
    if (a_plan->coeffs_buf && a_plan->coeffs_buf_capacity >= a_needed)
        return DAP_GPU_OK;

    if (a_plan->coeffs_buf) {
        dap_gpu_buffer_destroy(a_plan->coeffs_buf);
        a_plan->coeffs_buf = NULL;
        a_plan->coeffs_buf_capacity = 0;
    }

    size_t l_alloc = a_needed < 4096 ? 4096 : a_needed;
    size_t l_po2 = 1;
    while (l_po2 < l_alloc) l_po2 <<= 1;
    l_alloc = l_po2;

    dap_gpu_error_t l_rc = dap_gpu_buffer_create(
        l_alloc, DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_SRC | DAP_GPU_BUF_TRANSFER_DST,
        DAP_GPU_MEM_DEVICE, &a_plan->coeffs_buf);
    if (l_rc == DAP_GPU_OK)
        a_plan->coeffs_buf_capacity = l_alloc;
    return l_rc;
}

dap_gpu_error_t dap_gpu_ntt16_plan_create(uint32_t a_n, int16_t a_q, int16_t a_qinv16,
                                          const int16_t *a_zetas,
                                          const int16_t *a_zetas_inv,
                                          uint32_t a_zetas_len,
                                          dap_gpu_ntt16_plan_t **a_out_plan)
{
    if (!a_out_plan || !a_zetas || !a_zetas_inv || a_n == 0 || a_zetas_len == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    if (!dap_gpu_is_available())
        return DAP_GPU_ERR_NOT_INITIALIZED;

    dap_gpu_ntt16_plan_t *l_plan = calloc(1, sizeof(*l_plan));
    if (!l_plan) return DAP_GPU_ERR_OUT_OF_MEMORY;
    l_plan->n = a_n;
    l_plan->q = a_q;
    l_plan->qinv16 = a_qinv16;

    /* Widen int16 zetas to int32 for GPU buffer */
    int32_t *l_z32 = malloc(a_zetas_len * sizeof(int32_t));
    int32_t *l_zi32 = malloc(a_zetas_len * sizeof(int32_t));
    if (!l_z32 || !l_zi32) {
        free(l_z32); free(l_zi32); free(l_plan);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    for (uint32_t i = 0; i < a_zetas_len; i++) {
        l_z32[i] = a_zetas[i];
        l_zi32[i] = a_zetas_inv[i];
    }

    dap_gpu_error_t l_rc;
    size_t l_zbytes = a_zetas_len * sizeof(int32_t);

    l_rc = dap_gpu_buffer_create(l_zbytes, DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_buf);
    if (l_rc != DAP_GPU_OK) { free(l_z32); free(l_zi32); free(l_plan); return l_rc; }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_buf, l_z32, l_zbytes, 0);
    free(l_z32);
    if (l_rc != DAP_GPU_OK) {
        free(l_zi32); dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    l_rc = dap_gpu_buffer_create(l_zbytes, DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST,
                                 DAP_GPU_MEM_DEVICE, &l_plan->zetas_inv_buf);
    if (l_rc != DAP_GPU_OK) {
        free(l_zi32); dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }
    l_rc = dap_gpu_buffer_upload(l_plan->zetas_inv_buf, l_zi32, l_zbytes, 0);
    free(l_zi32);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    dap_gpu_pipeline_desc_t l_fwd_desc = {
        .shader_code = ntt16_mont_forward_spirv,
        .shader_code_size = ntt16_mont_forward_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_fwd_desc, &l_plan->pipeline_forward);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    dap_gpu_pipeline_desc_t l_inv_desc = {
        .shader_code = ntt16_mont_inverse_spirv,
        .shader_code_size = ntt16_mont_inverse_spirv_size,
        .entry_point = "main",
        .num_storage_buffers = 2,
        .num_uniform_buffers = 0,
        .push_constant_size = sizeof(s_ntt_push_constants_t),
    };
    l_rc = dap_gpu_pipeline_create(&l_inv_desc, &l_plan->pipeline_inverse);
    if (l_rc != DAP_GPU_OK) {
        dap_gpu_pipeline_destroy(l_plan->pipeline_forward);
        dap_gpu_buffer_destroy(l_plan->zetas_inv_buf);
        dap_gpu_buffer_destroy(l_plan->zetas_buf); free(l_plan); return l_rc;
    }

    log_it(L_INFO, "GPU 16-bit NTT plan created: N=%u, q=%d, %u zetas",
           a_n, a_q, a_zetas_len);
    *a_out_plan = l_plan;
    return DAP_GPU_OK;
}

void dap_gpu_ntt16_plan_destroy(dap_gpu_ntt16_plan_t *a_plan)
{
    if (!a_plan) return;
    free(a_plan->staging_buf);
    if (a_plan->coeffs_buf)  dap_gpu_buffer_destroy(a_plan->coeffs_buf);
    dap_gpu_pipeline_destroy(a_plan->pipeline_inverse);
    dap_gpu_pipeline_destroy(a_plan->pipeline_forward);
    dap_gpu_buffer_destroy(a_plan->zetas_inv_buf);
    dap_gpu_buffer_destroy(a_plan->zetas_buf);
    free(a_plan);
}

static dap_gpu_error_t s_ensure_staging_buf(dap_gpu_ntt16_plan_t *a_plan, size_t a_count)
{
    if (a_plan->staging_buf && a_plan->staging_capacity >= a_count)
        return DAP_GPU_OK;

    free(a_plan->staging_buf);
    size_t l_cap = a_count;
    if (l_cap < 1024) l_cap = 1024;
    /* Geometric growth */
    size_t l_po2 = 1;
    while (l_po2 < l_cap) l_po2 <<= 1;

    a_plan->staging_buf = malloc(l_po2 * sizeof(int32_t));
    if (!a_plan->staging_buf) {
        a_plan->staging_capacity = 0;
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    a_plan->staging_capacity = l_po2;
    return DAP_GPU_OK;
}

static dap_gpu_error_t s_gpu_ntt16_exec(dap_gpu_ntt16_plan_t *a_plan,
                                         int16_t *a_coeffs,
                                         uint32_t a_batch_count,
                                         bool a_forward)
{
    size_t l_count = (size_t)a_batch_count * a_plan->n;
    size_t l_gpu_bytes = l_count * sizeof(int32_t);

    dap_gpu_error_t l_rc = s_ensure_coeffs_buf_16(a_plan, l_gpu_bytes);
    if (l_rc != DAP_GPU_OK) return l_rc;

    l_rc = s_ensure_staging_buf(a_plan, l_count);
    if (l_rc != DAP_GPU_OK) return l_rc;

    int32_t *l_buf32 = a_plan->staging_buf;

    /* Widen int16 -> int32 */
    for (size_t i = 0; i < l_count; i++)
        l_buf32[i] = a_coeffs[i];

    l_rc = dap_gpu_buffer_upload(a_plan->coeffs_buf, l_buf32, l_gpu_bytes, 0);
    if (l_rc != DAP_GPU_OK) return l_rc;

    s_ntt_push_constants_t l_pc = {
        .q = a_plan->q,
        .qinv = (uint32_t)(int32_t)a_plan->qinv16,
        .n = a_plan->n,
        .batch_size = a_batch_count,
    };

    dap_gpu_buffer_t *l_zbuf = a_forward ? a_plan->zetas_buf : a_plan->zetas_inv_buf;

    dap_gpu_binding_desc_t l_bindings[2] = {
        { .binding = 0, .buffer = a_plan->coeffs_buf, .offset = 0, .range = 0 },
        { .binding = 1, .buffer = l_zbuf,              .offset = 0, .range = 0 },
    };

    dap_gpu_dispatch_desc_t l_desc = {
        .pipeline = a_forward ? a_plan->pipeline_forward : a_plan->pipeline_inverse,
        .bindings = l_bindings,
        .num_bindings = 2,
        .push_constants = &l_pc,
        .push_constant_size = sizeof(l_pc),
        .group_count = { a_batch_count, 1, 1 },
    };

    dap_gpu_fence_t *l_fence = NULL;
    l_rc = dap_gpu_dispatch(&l_desc, &l_fence);
    if (l_rc != DAP_GPU_OK) return l_rc;

    l_rc = dap_gpu_fence_wait(l_fence, 0);
    dap_gpu_fence_destroy(l_fence);
    if (l_rc != DAP_GPU_OK) return l_rc;

    l_rc = dap_gpu_buffer_download(a_plan->coeffs_buf, l_buf32, l_gpu_bytes, 0);
    if (l_rc != DAP_GPU_OK) return l_rc;

    /* Narrow int32 -> int16 */
    for (size_t i = 0; i < l_count; i++)
        a_coeffs[i] = (int16_t)l_buf32[i];

    return DAP_GPU_OK;
}

dap_gpu_error_t dap_gpu_ntt16_forward_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    return s_gpu_ntt16_exec(a_plan, a_coeffs, a_batch_count, true);
}

dap_gpu_error_t dap_gpu_ntt16_inverse_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count)
{
    if (!a_plan || !a_coeffs || a_batch_count == 0)
        return DAP_GPU_ERR_INVALID_PARAM;
    return s_gpu_ntt16_exec(a_plan, a_coeffs, a_batch_count, false);
}

#else /* !DAP_GPU_VULKAN */

dap_gpu_error_t dap_gpu_ntt_plan_create(uint32_t a_n, int32_t a_q, uint32_t a_qinv,
                                        const int32_t *a_zetas,
                                        const int32_t *a_zetas_inv,
                                        uint32_t a_zetas_len,
                                        dap_gpu_ntt_plan_t **a_out_plan)
{
    (void)a_n; (void)a_q; (void)a_qinv; (void)a_zetas; (void)a_zetas_inv;
    (void)a_zetas_len; (void)a_out_plan;
    return DAP_GPU_ERR_NO_BACKEND;
}

void dap_gpu_ntt_plan_destroy(dap_gpu_ntt_plan_t *a_plan) { (void)a_plan; }

dap_gpu_error_t dap_gpu_ntt_forward_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt_inverse_mont(dap_gpu_ntt_plan_t *a_plan,
                                         int32_t *a_coeffs,
                                         uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt_plan_create_plain(uint32_t a_n, int32_t a_q,
                                              int32_t a_one_over_n,
                                              const int32_t *a_zetas,
                                              const int32_t *a_zetas_inv,
                                              uint32_t a_zetas_len,
                                              dap_gpu_ntt_plan_t **a_out_plan)
{
    (void)a_n; (void)a_q; (void)a_one_over_n; (void)a_zetas;
    (void)a_zetas_inv; (void)a_zetas_len; (void)a_out_plan;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt_forward(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt_inverse(dap_gpu_ntt_plan_t *a_plan,
                                    int32_t *a_coeffs,
                                    uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt16_plan_create(uint32_t a_n, int16_t a_q, int16_t a_qinv16,
                                          const int16_t *a_zetas,
                                          const int16_t *a_zetas_inv,
                                          uint32_t a_zetas_len,
                                          dap_gpu_ntt16_plan_t **a_out_plan)
{
    (void)a_n; (void)a_q; (void)a_qinv16; (void)a_zetas;
    (void)a_zetas_inv; (void)a_zetas_len; (void)a_out_plan;
    return DAP_GPU_ERR_NO_BACKEND;
}

void dap_gpu_ntt16_plan_destroy(dap_gpu_ntt16_plan_t *a_plan) { (void)a_plan; }

dap_gpu_error_t dap_gpu_ntt16_forward_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

dap_gpu_error_t dap_gpu_ntt16_inverse_mont(dap_gpu_ntt16_plan_t *a_plan,
                                           int16_t *a_coeffs,
                                           uint32_t a_batch_count)
{
    (void)a_plan; (void)a_coeffs; (void)a_batch_count;
    return DAP_GPU_ERR_NO_BACKEND;
}

#endif /* DAP_GPU_VULKAN */
