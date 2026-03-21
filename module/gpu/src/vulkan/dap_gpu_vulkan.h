/**
 * @file dap_gpu_vulkan.h
 * @brief Vulkan compute backend — internal structures
 */

#pragma once

#ifdef DAP_GPU_VULKAN

#include <vulkan/vulkan.h>
#include "dap_gpu.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Vulkan-specific GPU context
 *
 * Owns the VkInstance → VkDevice lifetime.
 * Compute queue for dispatches. Optional dedicated transfer queue for
 * async DMA on discrete GPUs (overlaps with compute).
 */
typedef struct dap_gpu_vulkan_ctx {
    VkInstance               instance;
    VkPhysicalDevice         physical_device;
    VkDevice                 device;

    VkQueue                  compute_queue;
    uint32_t                 compute_queue_family;
    VkCommandPool            compute_cmd_pool;

    VkQueue                  transfer_queue;     /**< Dedicated DMA queue, or == compute_queue */
    uint32_t                 transfer_queue_family;
    VkCommandPool            transfer_cmd_pool;  /**< Separate pool for transfer cmd buffers */
    bool                     has_dedicated_transfer;

    VkPhysicalDeviceMemoryProperties mem_props;
    VkPhysicalDeviceProperties       dev_props;
    VkPhysicalDeviceSubgroupProperties subgroup_props;
} dap_gpu_vulkan_ctx_t;

/**
 * @brief Vulkan buffer wrapper
 */
struct dap_gpu_buffer {
    VkBuffer        vk_buffer;
    VkDeviceMemory  vk_memory;
    size_t          size;
    dap_gpu_mem_type_t mem_type;
    void           *mapped_ptr;    /**< Non-NULL if persistently mapped */
};

/**
 * @brief Vulkan compute pipeline wrapper
 */
struct dap_gpu_pipeline {
    VkShaderModule          shader_module;
    VkPipelineLayout        pipeline_layout;
    VkPipeline              pipeline;
    VkDescriptorSetLayout   desc_set_layout;
    VkDescriptorPool        desc_pool;
    uint32_t                num_bindings;
    uint32_t                push_constant_size;
};

/**
 * @brief Vulkan fence wrapper
 */
struct dap_gpu_fence {
    VkFence         vk_fence;
    VkCommandBuffer cmd_buf;        /**< Recorded cmd buffer (returned to pool on destroy) */
    VkCommandPool   owning_pool;    /**< Pool to return cmd_buf to on destroy */
};

dap_gpu_vulkan_ctx_t *dap_gpu_vulkan_get_ctx(void);

#ifdef __cplusplus
}
#endif

#endif /* DAP_GPU_VULKAN */
