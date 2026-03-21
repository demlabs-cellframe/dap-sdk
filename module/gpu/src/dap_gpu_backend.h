/**
 * @file dap_gpu_backend.h
 * @brief Internal vtable interface for GPU backends (Vulkan, Metal, CUDA, etc.)
 *
 * Each backend implements this vtable. The core dap_gpu.c dispatches all
 * public API calls through the active backend's vtable.
 */

#pragma once

#include "dap_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief GPU backend operations vtable
 *
 * Each backend fills this struct during its probe/init phase.
 * NULL function pointers mean "not supported" — the core layer
 * returns DAP_GPU_ERR_INIT_FAILED for those operations.
 */
typedef struct dap_gpu_backend_ops {

    dap_gpu_backend_t type;

    /* Lifecycle */
    dap_gpu_error_t (*init)(dap_gpu_device_info_t *a_out_info);
    void            (*deinit)(void);

    /* Buffers */
    dap_gpu_error_t (*buffer_create)(size_t a_size, uint32_t a_usage,
                                     dap_gpu_mem_type_t a_mem_type,
                                     dap_gpu_buffer_t **a_out_buf);
    void            (*buffer_destroy)(dap_gpu_buffer_t *a_buf);
    dap_gpu_error_t (*buffer_upload)(dap_gpu_buffer_t *a_buf,
                                     const void *a_data, size_t a_size,
                                     size_t a_offset);
    dap_gpu_error_t (*buffer_download)(dap_gpu_buffer_t *a_buf,
                                       void *a_data, size_t a_size,
                                       size_t a_offset);
    dap_gpu_error_t (*buffer_map)(dap_gpu_buffer_t *a_buf, void **a_out_ptr);
    void            (*buffer_unmap)(dap_gpu_buffer_t *a_buf);
    size_t          (*buffer_size)(const dap_gpu_buffer_t *a_buf);

    /* Pipeline */
    dap_gpu_error_t (*pipeline_create)(const dap_gpu_pipeline_desc_t *a_desc,
                                       dap_gpu_pipeline_t **a_out_pipe);
    void            (*pipeline_destroy)(dap_gpu_pipeline_t *a_pipe);

    /* Async transfer */
    dap_gpu_error_t (*buffer_upload_async)(dap_gpu_buffer_t *a_buf,
                                           const void *a_data, size_t a_size,
                                           size_t a_offset,
                                           dap_gpu_fence_t **a_out_fence);
    dap_gpu_error_t (*buffer_download_async)(dap_gpu_buffer_t *a_buf,
                                              void *a_data, size_t a_size,
                                              size_t a_offset,
                                              dap_gpu_fence_t **a_out_fence);

    /* Dispatch + sync */
    dap_gpu_error_t (*dispatch)(const dap_gpu_dispatch_desc_t *a_desc,
                                dap_gpu_fence_t **a_out_fence);

    /* Batch dispatch: record multiple dispatches into one command buffer */
    dap_gpu_error_t (*batch_begin)(void);
    dap_gpu_error_t (*batch_dispatch)(const dap_gpu_dispatch_desc_t *a_desc);
    dap_gpu_error_t (*batch_end)(dap_gpu_fence_t **a_out_fence);

    dap_gpu_error_t (*fence_wait)(dap_gpu_fence_t *a_fence, uint64_t a_timeout_ns);
    bool            (*fence_is_signaled)(dap_gpu_fence_t *a_fence);
    void            (*fence_destroy)(dap_gpu_fence_t *a_fence);
    dap_gpu_error_t (*wait_idle)(void);

} dap_gpu_backend_ops_t;

/* Backend probe functions — return true if the backend is available */
#ifdef DAP_GPU_VULKAN
bool dap_gpu_vulkan_probe(dap_gpu_backend_ops_t *a_ops);
#endif

#ifdef DAP_GPU_METAL
bool dap_gpu_metal_probe(dap_gpu_backend_ops_t *a_ops);
#endif

#ifdef DAP_GPU_CUDA
bool dap_gpu_cuda_probe(dap_gpu_backend_ops_t *a_ops);
#endif

#ifdef DAP_GPU_OPENCL
bool dap_gpu_opencl_probe(dap_gpu_backend_ops_t *a_ops);
#endif

#ifdef __cplusplus
}
#endif
