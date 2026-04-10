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
 * @file dap_gpu.h
 * @brief Abstract GPU compute API for batch cryptographic operations.
 *
 * Provides a backend-agnostic interface for GPU-accelerated batch operations:
 * NTT, hash, signature verification, bulk encryption.
 *
 * Backends: Vulkan (primary), Metal (secondary), CUDA (optional).
 *
 * Key principle: on discrete GPUs, crypto is only efficient for batch operations
 * (hundreds+ parallel) due to PCIe host↔device transfer overhead. On unified
 * memory architectures (Apple Silicon/Metal, integrated Vulkan/OpenCL, RPi
 * VideoCore) transfer cost is near-zero — the threshold drops to dispatch
 * overhead only (~10-50 µs), making GPU worthwhile for smaller batches.
 *
 * Lifecycle:
 *   dap_gpu_init()             — probe backends, select best device
 *   dap_gpu_buffer_create()    — allocate host/device memory
 *   dap_gpu_buffer_upload()    — host → device transfer
 *   dap_gpu_pipeline_create()  — load compute shader, set layout
 *   dap_gpu_dispatch()         — submit compute work
 *   dap_gpu_fence_wait()       — wait for completion
 *   dap_gpu_buffer_download()  — device → host transfer
 *   dap_gpu_deinit()           — release all resources
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*                              Error codes                                   */
/* ========================================================================== */

typedef enum {
    DAP_GPU_OK = 0,
    DAP_GPU_ERR_NO_BACKEND,          /**< No GPU backend available */
    DAP_GPU_ERR_NO_DEVICE,           /**< No suitable GPU device found */
    DAP_GPU_ERR_INIT_FAILED,         /**< Backend initialization failed */
    DAP_GPU_ERR_OUT_OF_MEMORY,       /**< GPU memory allocation failed */
    DAP_GPU_ERR_INVALID_PARAM,       /**< Invalid parameter */
    DAP_GPU_ERR_SHADER_COMPILE,      /**< Shader compilation/loading failed */
    DAP_GPU_ERR_PIPELINE_CREATE,     /**< Pipeline creation failed */
    DAP_GPU_ERR_DISPATCH_FAILED,     /**< Compute dispatch failed */
    DAP_GPU_ERR_TIMEOUT,             /**< Operation timed out */
    DAP_GPU_ERR_DEVICE_LOST,         /**< GPU device lost (driver crash, etc.) */
    DAP_GPU_ERR_NOT_INITIALIZED,     /**< dap_gpu_init() not called */
    DAP_GPU_ERR_BUFFER_MAP_FAILED,   /**< Buffer mapping failed */
} dap_gpu_error_t;

/* ========================================================================== */
/*                              Enumerations                                  */
/* ========================================================================== */

typedef enum {
    DAP_GPU_BACKEND_NONE = 0,
    DAP_GPU_BACKEND_VULKAN,
    DAP_GPU_BACKEND_METAL,
    DAP_GPU_BACKEND_CUDA,
    DAP_GPU_BACKEND_OPENCL,
} dap_gpu_backend_t;

typedef enum {
    DAP_GPU_VENDOR_UNKNOWN = 0,
    DAP_GPU_VENDOR_NVIDIA,
    DAP_GPU_VENDOR_AMD,
    DAP_GPU_VENDOR_INTEL,
    DAP_GPU_VENDOR_APPLE,
    DAP_GPU_VENDOR_ARM,            /**< ARM Mali */
    DAP_GPU_VENDOR_QUALCOMM,       /**< Adreno */
    DAP_GPU_VENDOR_IMGTECH,        /**< Imagination PowerVR */
} dap_gpu_vendor_t;

/**
 * @brief Buffer usage flags (combinable with bitwise OR)
 */
typedef enum {
    DAP_GPU_BUF_STORAGE      = 0x01,  /**< Shader storage buffer (SSBO) */
    DAP_GPU_BUF_UNIFORM      = 0x02,  /**< Uniform buffer */
    DAP_GPU_BUF_TRANSFER_SRC = 0x04,  /**< Can be source for transfers */
    DAP_GPU_BUF_TRANSFER_DST = 0x08,  /**< Can be destination for transfers */
} dap_gpu_buffer_usage_t;

/**
 * @brief Buffer memory location hint
 */
typedef enum {
    DAP_GPU_MEM_DEVICE,    /**< Device-local, fastest for GPU. No host access (discrete GPU). */
    DAP_GPU_MEM_HOST,      /**< Host-visible + host-coherent. Always mappable. */
    DAP_GPU_MEM_STAGING,   /**< Host-visible, optimized for upload/download staging. */
    DAP_GPU_MEM_SHARED,    /**< Unified/shared: device-local AND host-visible (UMA only).
                                Falls back to HOST on discrete GPUs. Zero-copy on Metal,
                                integrated Vulkan/OpenCL, RPi VideoCore. */
} dap_gpu_mem_type_t;

/* ========================================================================== */
/*                          Opaque handle types                               */
/* ========================================================================== */

typedef struct dap_gpu_context  dap_gpu_context_t;
typedef struct dap_gpu_buffer   dap_gpu_buffer_t;
typedef struct dap_gpu_pipeline dap_gpu_pipeline_t;
typedef struct dap_gpu_fence    dap_gpu_fence_t;

/* ========================================================================== */
/*                           Device information                               */
/* ========================================================================== */

/**
 * @brief GPU device properties (read-only, filled by init)
 */
typedef struct dap_gpu_device_info {
    char                name[256];
    dap_gpu_backend_t   backend;
    dap_gpu_vendor_t    vendor;
    uint64_t            vram_bytes;             /**< Dedicated VRAM size */
    uint32_t            max_workgroup_size[3];  /**< Max local workgroup (x,y,z) */
    uint32_t            max_workgroup_invocations;
    uint32_t            max_shared_memory;      /**< Shared memory per workgroup (bytes) */
    uint32_t            max_push_constants;     /**< Push constant size (bytes) */
    uint32_t            subgroup_size;          /**< Warp/wavefront size */
    uint32_t            max_bound_descriptor_sets;
    bool                unified_memory;         /**< UMA: CPU and GPU share physical memory.
                                                     True for: Apple Silicon, integrated GPUs
                                                     (Intel/AMD APU, Mali, Adreno), RPi VideoCore.
                                                     When true, DAP_GPU_MEM_SHARED gives zero-copy
                                                     buffers and batch thresholds are much lower. */
    bool                supports_int64;
    bool                supports_float64;
    bool                supports_subgroup_ops;
} dap_gpu_device_info_t;

/* ========================================================================== */
/*                        Initialization / shutdown                           */
/* ========================================================================== */

/**
 * @brief Initialize GPU subsystem: probe backends, select best device
 *
 * Tries backends in priority order: Vulkan → Metal → CUDA → OpenCL.
 * First backend that successfully initializes wins.
 *
 * @param a_preferred  Preferred backend (DAP_GPU_BACKEND_NONE for auto-detect)
 * @return DAP_GPU_OK on success, error code otherwise
 *
 * Thread-safety: call once from main thread before any other GPU operations.
 */
dap_gpu_error_t dap_gpu_init(dap_gpu_backend_t a_preferred);

/**
 * @brief Shut down GPU subsystem: release device, queues, pools
 *
 * All outstanding GPU operations must be complete before calling.
 * Invalidates all GPU handles (buffers, pipelines, fences).
 */
void dap_gpu_deinit(void);

/**
 * @brief Check if GPU subsystem is initialized and available
 */
bool dap_gpu_is_available(void);

/**
 * @brief Get active backend type
 */
dap_gpu_backend_t dap_gpu_get_backend(void);

/**
 * @brief Get device info for the active GPU
 */
const dap_gpu_device_info_t *dap_gpu_get_device_info(void);

/**
 * @brief Get the GPU compute context (opaque, backend-specific)
 */
dap_gpu_context_t *dap_gpu_get_context(void);

/**
 * @brief Get human-readable error description
 */
const char *dap_gpu_error_str(dap_gpu_error_t a_err);

/* ========================================================================== */
/*                            Buffer management                               */
/* ========================================================================== */

/**
 * @brief Create a GPU buffer
 *
 * @param a_size       Buffer size in bytes
 * @param a_usage      Usage flags (DAP_GPU_BUF_STORAGE | DAP_GPU_BUF_TRANSFER_DST, etc.)
 * @param a_mem_type   Memory type hint
 * @param a_out_buf    [out] Created buffer handle
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_buffer_create(size_t a_size, uint32_t a_usage,
                                      dap_gpu_mem_type_t a_mem_type,
                                      dap_gpu_buffer_t **a_out_buf);

/**
 * @brief Destroy a GPU buffer and free its memory
 */
void dap_gpu_buffer_destroy(dap_gpu_buffer_t *a_buf);

/**
 * @brief Upload data from host to a device/staging buffer
 *
 * For DAP_GPU_MEM_HOST/STAGING: direct memcpy via mapping.
 * For DAP_GPU_MEM_DEVICE: creates a staging buffer, copies, then
 * submits a transfer command (implicit staging).
 *
 * @param a_buf    Destination buffer
 * @param a_data   Source host pointer
 * @param a_size   Byte count
 * @param a_offset Offset into the buffer
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_buffer_upload(dap_gpu_buffer_t *a_buf,
                                      const void *a_data, size_t a_size,
                                      size_t a_offset);

/**
 * @brief Download data from GPU buffer to host memory
 *
 * @param a_buf     Source buffer
 * @param a_data    Destination host pointer
 * @param a_size    Byte count
 * @param a_offset  Offset into the buffer
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_buffer_download(dap_gpu_buffer_t *a_buf,
                                        void *a_data, size_t a_size,
                                        size_t a_offset);

/**
 * @brief Map a host-visible buffer for direct CPU access
 *
 * Only valid for DAP_GPU_MEM_HOST and DAP_GPU_MEM_STAGING buffers.
 *
 * @param a_buf       Buffer to map
 * @param a_out_ptr   [out] Host pointer to mapped memory
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_buffer_map(dap_gpu_buffer_t *a_buf, void **a_out_ptr);

/**
 * @brief Unmap a previously mapped buffer
 */
void dap_gpu_buffer_unmap(dap_gpu_buffer_t *a_buf);

/**
 * @brief Get the size of a buffer
 */
size_t dap_gpu_buffer_size(const dap_gpu_buffer_t *a_buf);

/* ========================================================================== */
/*                          Pipeline management                               */
/* ========================================================================== */

/**
 * @brief Descriptor for a single binding in a compute pipeline
 */
typedef struct dap_gpu_binding_desc {
    uint32_t            binding;    /**< Binding index in shader layout */
    dap_gpu_buffer_t   *buffer;     /**< Buffer to bind */
    size_t              offset;     /**< Offset within the buffer */
    size_t              range;      /**< Byte range (0 = whole buffer) */
} dap_gpu_binding_desc_t;

/**
 * @brief Descriptor for creating a compute pipeline
 */
typedef struct dap_gpu_pipeline_desc {
    const void   *shader_code;       /**< SPIR-V bytecode (Vulkan), MSL source (Metal), PTX (CUDA) */
    size_t        shader_code_size;   /**< Size of shader_code in bytes */
    const char   *entry_point;        /**< Shader entry point name (default: "main") */

    uint32_t      num_storage_buffers;  /**< Number of SSBO bindings */
    uint32_t      num_uniform_buffers;  /**< Number of UBO bindings */
    uint32_t      push_constant_size;   /**< Push constant block size (bytes, 0 = none) */

    uint32_t      local_size[3];        /**< Workgroup size (x, y, z). 0 = backend default */
} dap_gpu_pipeline_desc_t;

/**
 * @brief Create a compute pipeline from shader code
 *
 * @param a_desc       Pipeline descriptor
 * @param a_out_pipe   [out] Created pipeline handle
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_pipeline_create(const dap_gpu_pipeline_desc_t *a_desc,
                                        dap_gpu_pipeline_t **a_out_pipe);

/**
 * @brief Destroy a compute pipeline
 */
void dap_gpu_pipeline_destroy(dap_gpu_pipeline_t *a_pipe);

/* ========================================================================== */
/*                           Compute dispatch                                 */
/* ========================================================================== */

/**
 * @brief Descriptor for a compute dispatch
 */
typedef struct dap_gpu_dispatch_desc {
    dap_gpu_pipeline_t      *pipeline;

    const dap_gpu_binding_desc_t *bindings;
    uint32_t                      num_bindings;

    const void               *push_constants;   /**< Push constant data (NULL if unused) */
    uint32_t                  push_constant_size;

    uint32_t                  group_count[3];    /**< Workgroup dispatch count (x, y, z) */
} dap_gpu_dispatch_desc_t;

/**
 * @brief Submit a compute dispatch to the GPU
 *
 * Non-blocking. Returns a fence that can be waited on.
 *
 * @param a_desc      Dispatch descriptor
 * @param a_out_fence [out] Fence for synchronization (NULL if not needed)
 * @return DAP_GPU_OK or error
 */
dap_gpu_error_t dap_gpu_dispatch(const dap_gpu_dispatch_desc_t *a_desc,
                                 dap_gpu_fence_t **a_out_fence);

/* ========================================================================== */
/*                      Async transfer + batch dispatch                       */
/* ========================================================================== */

/**
 * @brief Non-blocking upload: host → device transfer.
 *
 * Returns immediately with a fence. On UMA the "transfer" is just a memcpy
 * and the fence is pre-signaled. On discrete GPU the DMA runs on a dedicated
 * transfer queue (if available) so it can overlap with compute.
 */
dap_gpu_error_t dap_gpu_buffer_upload_async(dap_gpu_buffer_t *a_buf,
                                            const void *a_data, size_t a_size,
                                            size_t a_offset,
                                            dap_gpu_fence_t **a_out_fence);

/**
 * @brief Non-blocking download: device → host transfer.
 *
 * Same semantics as upload_async. On UMA the data is already visible;
 * the fence signals immediately.
 */
dap_gpu_error_t dap_gpu_buffer_download_async(dap_gpu_buffer_t *a_buf,
                                              void *a_data, size_t a_size,
                                              size_t a_offset,
                                              dap_gpu_fence_t **a_out_fence);

/**
 * @brief Begin recording a batch of dispatches into a single command buffer.
 *
 * All dap_gpu_batch_dispatch() calls between begin/end are recorded into
 * one command buffer and submitted atomically — lower per-dispatch overhead
 * and enables the driver to optimize scheduling.
 *
 * Usage:
 *   dap_gpu_batch_begin();
 *   dap_gpu_batch_dispatch(&desc1);
 *   dap_gpu_batch_dispatch(&desc2);
 *   dap_gpu_batch_end(&fence);
 *   dap_gpu_fence_wait(fence, 0);
 */
dap_gpu_error_t dap_gpu_batch_begin(void);

/**
 * @brief Record a compute dispatch into the current batch.
 *
 * Must be called between dap_gpu_batch_begin() and dap_gpu_batch_end().
 */
dap_gpu_error_t dap_gpu_batch_dispatch(const dap_gpu_dispatch_desc_t *a_desc);

/**
 * @brief Finish recording and submit the batch to the GPU.
 *
 * @param a_out_fence [out] Fence that signals when all dispatches complete
 */
dap_gpu_error_t dap_gpu_batch_end(dap_gpu_fence_t **a_out_fence);

/* ========================================================================== */
/*                           Synchronization                                  */
/* ========================================================================== */

/**
 * @brief Wait for a fence to signal (GPU work complete)
 *
 * @param a_fence       Fence to wait on
 * @param a_timeout_ns  Timeout in nanoseconds (0 = infinite)
 * @return DAP_GPU_OK, DAP_GPU_ERR_TIMEOUT, or error
 */
dap_gpu_error_t dap_gpu_fence_wait(dap_gpu_fence_t *a_fence, uint64_t a_timeout_ns);

/**
 * @brief Check if a fence has signaled without blocking
 */
bool dap_gpu_fence_is_signaled(dap_gpu_fence_t *a_fence);

/**
 * @brief Destroy a fence
 */
void dap_gpu_fence_destroy(dap_gpu_fence_t *a_fence);

/**
 * @brief Wait for all outstanding GPU work to complete
 */
dap_gpu_error_t dap_gpu_wait_idle(void);

/* ========================================================================== */
/*                       Batch threshold helpers                              */
/* ========================================================================== */

/**
 * @brief Minimum batch size for GPU to be worthwhile (vs CPU SIMD)
 *
 * On discrete GPUs (PCIe): dominated by transfer overhead, ~256+ items.
 * On UMA (Apple Silicon, integrated GPU, RPi): dispatch overhead only, ~16+ items.
 *
 * @return Minimum recommended batch size, or UINT32_MAX if GPU unavailable
 */
uint32_t dap_gpu_min_batch_size(void);

/**
 * @brief Check if GPU acceleration is recommended for a given batch
 *
 * Considers: GPU availability, unified vs discrete memory, batch size,
 * estimated transfer cost (zero for UMA).
 *
 * @param a_batch_count  Number of items in the batch
 * @param a_item_bytes   Size of each item in bytes
 * @return true if GPU is recommended, false if CPU fallback is better
 */
bool dap_gpu_should_use(uint32_t a_batch_count, size_t a_item_bytes);

#ifdef __cplusplus
}
#endif
