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
 * @file dap_gpu_vulkan.c
 * @brief Vulkan compute backend implementation
 *
 * Compute-only Vulkan: no presentation, no graphics pipeline.
 * Uses a single compute queue for all dispatches.
 */

#ifdef DAP_GPU_VULKAN

#include <stdlib.h>
#include <string.h>

#include "dap_common.h"
#include "dap_gpu_backend.h"
#include "dap_gpu_vulkan.h"

#define LOG_TAG "dap_gpu_vk"

static dap_gpu_vulkan_ctx_t s_vk_ctx;

/* ========================================================================== */
/*                       Vulkan helper utilities                              */
/* ========================================================================== */

static dap_gpu_vendor_t s_vendor_from_pci(uint32_t a_vendor_id)
{
    switch (a_vendor_id) {
        case 0x10DE: return DAP_GPU_VENDOR_NVIDIA;
        case 0x1002: return DAP_GPU_VENDOR_AMD;
        case 0x8086: return DAP_GPU_VENDOR_INTEL;
        case 0x13B5: return DAP_GPU_VENDOR_ARM;
        case 0x5143: return DAP_GPU_VENDOR_QUALCOMM;
        case 0x1010: return DAP_GPU_VENDOR_IMGTECH;
        default:     return DAP_GPU_VENDOR_UNKNOWN;
    }
}

/**
 * Find a queue family that supports compute but (preferably) not graphics —
 * a dedicated compute queue avoids contention with the graphics pipeline.
 * Falls back to any compute-capable queue.
 */
static int32_t s_find_compute_queue_family(VkPhysicalDevice a_dev)
{
    uint32_t l_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(a_dev, &l_count, NULL);
    if (l_count == 0)
        return -1;

    VkQueueFamilyProperties *l_props = calloc(l_count, sizeof(*l_props));
    if (!l_props)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(a_dev, &l_count, l_props);

    int32_t l_dedicated = -1, l_any = -1;
    for (uint32_t i = 0; i < l_count; ++i) {
        if (l_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
            if (l_any < 0)
                l_any = (int32_t)i;
            if (!(l_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && l_dedicated < 0)
                l_dedicated = (int32_t)i;
        }
    }
    free(l_props);
    return l_dedicated >= 0 ? l_dedicated : l_any;
}

/**
 * Find a queue family that supports transfer but NOT compute or graphics —
 * a dedicated DMA engine for async host↔device copies that overlap compute.
 * Returns -1 if no such family exists (integrated GPUs often don't have one).
 */
static int32_t s_find_transfer_queue_family(VkPhysicalDevice a_dev, uint32_t a_exclude_family)
{
    uint32_t l_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(a_dev, &l_count, NULL);
    if (l_count == 0)
        return -1;

    VkQueueFamilyProperties *l_props = calloc(l_count, sizeof(*l_props));
    if (!l_props)
        return -1;
    vkGetPhysicalDeviceQueueFamilyProperties(a_dev, &l_count, l_props);

    int32_t l_result = -1;
    for (uint32_t i = 0; i < l_count; ++i) {
        if (i == a_exclude_family)
            continue;
        if ((l_props[i].queueFlags & VK_QUEUE_TRANSFER_BIT) &&
            !(l_props[i].queueFlags & VK_QUEUE_COMPUTE_BIT) &&
            !(l_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            l_result = (int32_t)i;
            break;
        }
    }
    free(l_props);
    return l_result;
}

static uint32_t s_find_memory_type(uint32_t a_type_filter, VkMemoryPropertyFlags a_properties)
{
    for (uint32_t i = 0; i < s_vk_ctx.mem_props.memoryTypeCount; ++i) {
        if ((a_type_filter & (1u << i))
            && (s_vk_ctx.mem_props.memoryTypes[i].propertyFlags & a_properties) == a_properties)
            return i;
    }
    return UINT32_MAX;
}

static bool s_has_uma = false;

static VkMemoryPropertyFlags s_mem_type_to_vk(dap_gpu_mem_type_t a_type)
{
    switch (a_type) {
        case DAP_GPU_MEM_DEVICE:
            return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        case DAP_GPU_MEM_HOST:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case DAP_GPU_MEM_STAGING:
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        case DAP_GPU_MEM_SHARED:
            if (s_has_uma) {
                return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                     | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                     | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            }
            return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
                 | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    }
    return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
}

static VkBufferUsageFlags s_usage_to_vk(uint32_t a_usage)
{
    VkBufferUsageFlags l_flags = 0;
    if (a_usage & DAP_GPU_BUF_STORAGE)
        l_flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (a_usage & DAP_GPU_BUF_UNIFORM)
        l_flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (a_usage & DAP_GPU_BUF_TRANSFER_SRC)
        l_flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (a_usage & DAP_GPU_BUF_TRANSFER_DST)
        l_flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return l_flags;
}

static VkCommandBuffer s_alloc_cmd_buffer(VkCommandPool a_pool)
{
    VkCommandBufferAllocateInfo l_alloc = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = a_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer l_cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(s_vk_ctx.device, &l_alloc, &l_cmd) != VK_SUCCESS)
        return VK_NULL_HANDLE;
    return l_cmd;
}

/* ========================================================================== */
/*                            Lifecycle                                       */
/* ========================================================================== */

static dap_gpu_error_t s_vk_init(dap_gpu_device_info_t *a_out_info)
{
    memset(&s_vk_ctx, 0, sizeof(s_vk_ctx));

    /* --- Instance --- */
    VkApplicationInfo l_app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "dap-sdk-gpu",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "dap-gpu",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_1,
    };
    VkInstanceCreateInfo l_inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &l_app_info,
    };
    if (vkCreateInstance(&l_inst_info, NULL, &s_vk_ctx.instance) != VK_SUCCESS) {
        log_it(L_ERROR, "vkCreateInstance failed");
        return DAP_GPU_ERR_INIT_FAILED;
    }

    /* --- Physical device selection (prefer discrete GPU) --- */
    uint32_t l_dev_count = 0;
    vkEnumeratePhysicalDevices(s_vk_ctx.instance, &l_dev_count, NULL);
    if (l_dev_count == 0) {
        log_it(L_WARNING, "No Vulkan physical devices found");
        vkDestroyInstance(s_vk_ctx.instance, NULL);
        return DAP_GPU_ERR_NO_DEVICE;
    }

    VkPhysicalDevice *l_devices = calloc(l_dev_count, sizeof(*l_devices));
    if (!l_devices) {
        vkDestroyInstance(s_vk_ctx.instance, NULL);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    vkEnumeratePhysicalDevices(s_vk_ctx.instance, &l_dev_count, l_devices);

    int32_t l_best_idx = -1;
    int l_best_score = -1;
    for (uint32_t i = 0; i < l_dev_count; ++i) {
        VkPhysicalDeviceProperties l_props;
        vkGetPhysicalDeviceProperties(l_devices[i], &l_props);

        int32_t l_qf = s_find_compute_queue_family(l_devices[i]);
        if (l_qf < 0)
            continue;

        int l_score = 0;
        if (l_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            l_score += 1000;
        else if (l_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            l_score += 100;
        else if (l_props.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU)
            l_score += 50;

        if (l_score > l_best_score) {
            l_best_score = l_score;
            l_best_idx = (int32_t)i;
        }
    }

    if (l_best_idx < 0) {
        log_it(L_WARNING, "No Vulkan device with compute queue found");
        free(l_devices);
        vkDestroyInstance(s_vk_ctx.instance, NULL);
        return DAP_GPU_ERR_NO_DEVICE;
    }

    s_vk_ctx.physical_device = l_devices[l_best_idx];
    free(l_devices);

    vkGetPhysicalDeviceProperties(s_vk_ctx.physical_device, &s_vk_ctx.dev_props);
    vkGetPhysicalDeviceMemoryProperties(s_vk_ctx.physical_device, &s_vk_ctx.mem_props);

    /*
     * UMA detection: check if any memory type is both DEVICE_LOCAL and
     * HOST_VISIBLE. True on integrated GPUs (Intel, AMD APU, Qualcomm
     * Adreno, ARM Mali) where CPU and GPU share the same physical DRAM.
     */
    s_has_uma = false;
    for (uint32_t i = 0; i < s_vk_ctx.mem_props.memoryTypeCount; ++i) {
        VkMemoryPropertyFlags f = s_vk_ctx.mem_props.memoryTypes[i].propertyFlags;
        if ((f & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (f & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            s_has_uma = true;
            break;
        }
    }

    /* Query subgroup properties */
    s_vk_ctx.subgroup_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES;
    s_vk_ctx.subgroup_props.pNext = NULL;
    VkPhysicalDeviceProperties2 l_props2 = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &s_vk_ctx.subgroup_props,
    };
    vkGetPhysicalDeviceProperties2(s_vk_ctx.physical_device, &l_props2);

    /* --- Queue families --- */
    s_vk_ctx.compute_queue_family = (uint32_t)s_find_compute_queue_family(s_vk_ctx.physical_device);

    int32_t l_xfer_family = s_find_transfer_queue_family(
        s_vk_ctx.physical_device, s_vk_ctx.compute_queue_family);
    s_vk_ctx.has_dedicated_transfer = (l_xfer_family >= 0);
    s_vk_ctx.transfer_queue_family = s_vk_ctx.has_dedicated_transfer
        ? (uint32_t)l_xfer_family : s_vk_ctx.compute_queue_family;

    /* --- Logical device --- */
    float l_queue_priority = 1.0f;
    VkDeviceQueueCreateInfo l_queue_cis[2];
    uint32_t l_queue_ci_count = 1;

    l_queue_cis[0] = (VkDeviceQueueCreateInfo){
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = s_vk_ctx.compute_queue_family,
        .queueCount = 1,
        .pQueuePriorities = &l_queue_priority,
    };
    if (s_vk_ctx.has_dedicated_transfer) {
        l_queue_cis[l_queue_ci_count++] = (VkDeviceQueueCreateInfo){
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = s_vk_ctx.transfer_queue_family,
            .queueCount = 1,
            .pQueuePriorities = &l_queue_priority,
        };
    }

    VkDeviceCreateInfo l_dev_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = l_queue_ci_count,
        .pQueueCreateInfos = l_queue_cis,
    };
    if (vkCreateDevice(s_vk_ctx.physical_device, &l_dev_ci, NULL, &s_vk_ctx.device) != VK_SUCCESS) {
        log_it(L_ERROR, "vkCreateDevice failed");
        vkDestroyInstance(s_vk_ctx.instance, NULL);
        return DAP_GPU_ERR_INIT_FAILED;
    }

    vkGetDeviceQueue(s_vk_ctx.device, s_vk_ctx.compute_queue_family, 0,
                     &s_vk_ctx.compute_queue);
    if (s_vk_ctx.has_dedicated_transfer) {
        vkGetDeviceQueue(s_vk_ctx.device, s_vk_ctx.transfer_queue_family, 0,
                         &s_vk_ctx.transfer_queue);
        log_it(L_INFO, "Dedicated transfer queue: family %u (async DMA)",
               s_vk_ctx.transfer_queue_family);
    } else {
        s_vk_ctx.transfer_queue = s_vk_ctx.compute_queue;
        log_it(L_DEBUG, "No dedicated transfer queue — sharing compute queue");
    }

    /* --- Command pools (resettable) --- */
    VkCommandPoolCreateInfo l_pool_ci = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = s_vk_ctx.compute_queue_family,
    };
    if (vkCreateCommandPool(s_vk_ctx.device, &l_pool_ci, NULL, &s_vk_ctx.compute_cmd_pool) != VK_SUCCESS) {
        log_it(L_ERROR, "vkCreateCommandPool (compute) failed");
        vkDestroyDevice(s_vk_ctx.device, NULL);
        vkDestroyInstance(s_vk_ctx.instance, NULL);
        return DAP_GPU_ERR_INIT_FAILED;
    }

    if (s_vk_ctx.has_dedicated_transfer) {
        VkCommandPoolCreateInfo l_xfer_pool_ci = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = s_vk_ctx.transfer_queue_family,
        };
        if (vkCreateCommandPool(s_vk_ctx.device, &l_xfer_pool_ci, NULL,
                                &s_vk_ctx.transfer_cmd_pool) != VK_SUCCESS) {
            log_it(L_WARNING, "Transfer command pool creation failed — falling back to compute queue");
            s_vk_ctx.has_dedicated_transfer = false;
            s_vk_ctx.transfer_queue = s_vk_ctx.compute_queue;
            s_vk_ctx.transfer_cmd_pool = s_vk_ctx.compute_cmd_pool;
        }
    } else {
        s_vk_ctx.transfer_cmd_pool = s_vk_ctx.compute_cmd_pool;
    }

    /* --- Fill device info --- */
    if (a_out_info) {
        snprintf(a_out_info->name, sizeof(a_out_info->name), "%s",
                 s_vk_ctx.dev_props.deviceName);
        a_out_info->vendor = s_vendor_from_pci(s_vk_ctx.dev_props.vendorID);

        uint64_t l_vram = 0;
        for (uint32_t i = 0; i < s_vk_ctx.mem_props.memoryHeapCount; ++i) {
            if (s_vk_ctx.mem_props.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                l_vram += s_vk_ctx.mem_props.memoryHeaps[i].size;
        }
        a_out_info->vram_bytes = l_vram;

        const VkPhysicalDeviceLimits *lim = &s_vk_ctx.dev_props.limits;
        a_out_info->max_workgroup_size[0] = lim->maxComputeWorkGroupSize[0];
        a_out_info->max_workgroup_size[1] = lim->maxComputeWorkGroupSize[1];
        a_out_info->max_workgroup_size[2] = lim->maxComputeWorkGroupSize[2];
        a_out_info->max_workgroup_invocations = lim->maxComputeWorkGroupInvocations;
        a_out_info->max_shared_memory = lim->maxComputeSharedMemorySize;
        a_out_info->max_push_constants = lim->maxPushConstantsSize;
        a_out_info->max_bound_descriptor_sets = lim->maxBoundDescriptorSets;
        a_out_info->subgroup_size = s_vk_ctx.subgroup_props.subgroupSize;

        a_out_info->unified_memory = s_has_uma;

        VkPhysicalDeviceFeatures l_feats;
        vkGetPhysicalDeviceFeatures(s_vk_ctx.physical_device, &l_feats);
        a_out_info->supports_int64 = l_feats.shaderInt64;
        a_out_info->supports_float64 = l_feats.shaderFloat64;
        a_out_info->supports_subgroup_ops =
            (s_vk_ctx.subgroup_props.supportedOperations & VK_SUBGROUP_FEATURE_BASIC_BIT) != 0;
    }

    return DAP_GPU_OK;
}

static void s_vk_deinit(void)
{
    if (s_vk_ctx.has_dedicated_transfer && s_vk_ctx.transfer_cmd_pool)
        vkDestroyCommandPool(s_vk_ctx.device, s_vk_ctx.transfer_cmd_pool, NULL);
    if (s_vk_ctx.compute_cmd_pool)
        vkDestroyCommandPool(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, NULL);
    if (s_vk_ctx.device)
        vkDestroyDevice(s_vk_ctx.device, NULL);
    if (s_vk_ctx.instance)
        vkDestroyInstance(s_vk_ctx.instance, NULL);
    memset(&s_vk_ctx, 0, sizeof(s_vk_ctx));
}

/* ========================================================================== */
/*                              Buffers                                       */
/* ========================================================================== */

static dap_gpu_error_t s_vk_buffer_create(size_t a_size, uint32_t a_usage,
                                           dap_gpu_mem_type_t a_mem_type,
                                           dap_gpu_buffer_t **a_out_buf)
{
    VkBufferCreateInfo l_buf_ci = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = a_size,
        .usage = s_usage_to_vk(a_usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    /* Device-local buffers also need TRANSFER_DST for uploads */
    if (a_mem_type == DAP_GPU_MEM_DEVICE)
        l_buf_ci.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;

    VkBuffer l_vk_buf = VK_NULL_HANDLE;
    if (vkCreateBuffer(s_vk_ctx.device, &l_buf_ci, NULL, &l_vk_buf) != VK_SUCCESS)
        return DAP_GPU_ERR_OUT_OF_MEMORY;

    VkMemoryRequirements l_mem_req;
    vkGetBufferMemoryRequirements(s_vk_ctx.device, l_vk_buf, &l_mem_req);

    VkMemoryPropertyFlags l_mem_flags = s_mem_type_to_vk(a_mem_type);
    uint32_t l_mem_idx = s_find_memory_type(l_mem_req.memoryTypeBits, l_mem_flags);
    if (l_mem_idx == UINT32_MAX) {
        vkDestroyBuffer(s_vk_ctx.device, l_vk_buf, NULL);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }

    VkMemoryAllocateInfo l_alloc = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = l_mem_req.size,
        .memoryTypeIndex = l_mem_idx,
    };
    VkDeviceMemory l_vk_mem = VK_NULL_HANDLE;
    if (vkAllocateMemory(s_vk_ctx.device, &l_alloc, NULL, &l_vk_mem) != VK_SUCCESS) {
        vkDestroyBuffer(s_vk_ctx.device, l_vk_buf, NULL);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }

    if (vkBindBufferMemory(s_vk_ctx.device, l_vk_buf, l_vk_mem, 0) != VK_SUCCESS) {
        vkFreeMemory(s_vk_ctx.device, l_vk_mem, NULL);
        vkDestroyBuffer(s_vk_ctx.device, l_vk_buf, NULL);
        return DAP_GPU_ERR_INIT_FAILED;
    }

    dap_gpu_buffer_t *l_buf = calloc(1, sizeof(*l_buf));
    if (!l_buf) {
        vkFreeMemory(s_vk_ctx.device, l_vk_mem, NULL);
        vkDestroyBuffer(s_vk_ctx.device, l_vk_buf, NULL);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    l_buf->vk_buffer = l_vk_buf;
    l_buf->vk_memory = l_vk_mem;
    l_buf->size = a_size;
    l_buf->mem_type = a_mem_type;
    l_buf->mapped_ptr = NULL;

    /* Persistently map host-visible buffers for convenience */
    if (a_mem_type != DAP_GPU_MEM_DEVICE) {
        if (vkMapMemory(s_vk_ctx.device, l_vk_mem, 0, a_size, 0,
                        &l_buf->mapped_ptr) != VK_SUCCESS) {
            l_buf->mapped_ptr = NULL;
        }
    }

    *a_out_buf = l_buf;
    return DAP_GPU_OK;
}

static void s_vk_buffer_destroy(dap_gpu_buffer_t *a_buf)
{
    if (a_buf->mapped_ptr) {
        vkUnmapMemory(s_vk_ctx.device, a_buf->vk_memory);
        a_buf->mapped_ptr = NULL;
    }
    vkDestroyBuffer(s_vk_ctx.device, a_buf->vk_buffer, NULL);
    vkFreeMemory(s_vk_ctx.device, a_buf->vk_memory, NULL);
    free(a_buf);
}

static dap_gpu_error_t s_vk_buffer_upload(dap_gpu_buffer_t *a_buf,
                                           const void *a_data, size_t a_size,
                                           size_t a_offset)
{
    if (a_buf->mem_type != DAP_GPU_MEM_DEVICE) {
        if (!a_buf->mapped_ptr)
            return DAP_GPU_ERR_BUFFER_MAP_FAILED;
        memcpy((uint8_t *)a_buf->mapped_ptr + a_offset, a_data, a_size);
        return DAP_GPU_OK;
    }

    /* Device-local: create temp staging buffer, memcpy, cmd copy, wait */
    dap_gpu_buffer_t *l_staging = NULL;
    dap_gpu_error_t l_rc = s_vk_buffer_create(
        a_size, DAP_GPU_BUF_TRANSFER_SRC, DAP_GPU_MEM_STAGING, &l_staging);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    memcpy(l_staging->mapped_ptr, a_data, a_size);

    VkCommandBuffer l_cmd = s_alloc_cmd_buffer(s_vk_ctx.compute_cmd_pool);
    if (l_cmd == VK_NULL_HANDLE) {
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(l_cmd, &l_begin);

    VkBufferCopy l_copy = { .srcOffset = 0, .dstOffset = a_offset, .size = a_size };
    vkCmdCopyBuffer(l_cmd, l_staging->vk_buffer, a_buf->vk_buffer, 1, &l_copy);

    vkEndCommandBuffer(l_cmd);

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &l_cmd,
    };
    VkFence l_fence;
    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_fence);
    vkQueueSubmit(s_vk_ctx.compute_queue, 1, &l_submit, l_fence);
    vkWaitForFences(s_vk_ctx.device, 1, &l_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(s_vk_ctx.device, l_fence, NULL);

    vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
    s_vk_buffer_destroy(l_staging);
    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_buffer_download(dap_gpu_buffer_t *a_buf,
                                              void *a_data, size_t a_size,
                                              size_t a_offset)
{
    if (a_buf->mem_type != DAP_GPU_MEM_DEVICE) {
        if (!a_buf->mapped_ptr)
            return DAP_GPU_ERR_BUFFER_MAP_FAILED;
        memcpy(a_data, (const uint8_t *)a_buf->mapped_ptr + a_offset, a_size);
        return DAP_GPU_OK;
    }

    dap_gpu_buffer_t *l_staging = NULL;
    dap_gpu_error_t l_rc = s_vk_buffer_create(
        a_size, DAP_GPU_BUF_TRANSFER_DST, DAP_GPU_MEM_STAGING, &l_staging);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    VkCommandBuffer l_cmd = s_alloc_cmd_buffer(s_vk_ctx.compute_cmd_pool);
    if (l_cmd == VK_NULL_HANDLE) {
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(l_cmd, &l_begin);

    VkBufferCopy l_copy = { .srcOffset = a_offset, .dstOffset = 0, .size = a_size };
    vkCmdCopyBuffer(l_cmd, a_buf->vk_buffer, l_staging->vk_buffer, 1, &l_copy);

    vkEndCommandBuffer(l_cmd);

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &l_cmd,
    };
    VkFence l_fence;
    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_fence);
    vkQueueSubmit(s_vk_ctx.compute_queue, 1, &l_submit, l_fence);
    vkWaitForFences(s_vk_ctx.device, 1, &l_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(s_vk_ctx.device, l_fence, NULL);

    memcpy(a_data, l_staging->mapped_ptr, a_size);

    vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
    s_vk_buffer_destroy(l_staging);
    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_buffer_map(dap_gpu_buffer_t *a_buf, void **a_out_ptr)
{
    if (a_buf->mapped_ptr) {
        *a_out_ptr = a_buf->mapped_ptr;
        return DAP_GPU_OK;
    }
    if (a_buf->mem_type == DAP_GPU_MEM_DEVICE)
        return DAP_GPU_ERR_BUFFER_MAP_FAILED;

    if (vkMapMemory(s_vk_ctx.device, a_buf->vk_memory, 0, a_buf->size, 0,
                    a_out_ptr) != VK_SUCCESS)
        return DAP_GPU_ERR_BUFFER_MAP_FAILED;
    a_buf->mapped_ptr = *a_out_ptr;
    return DAP_GPU_OK;
}

static void s_vk_buffer_unmap(dap_gpu_buffer_t *a_buf)
{
    if (a_buf->mapped_ptr && a_buf->mem_type != DAP_GPU_MEM_DEVICE) {
        vkUnmapMemory(s_vk_ctx.device, a_buf->vk_memory);
        a_buf->mapped_ptr = NULL;
    }
}

static size_t s_vk_buffer_size(const dap_gpu_buffer_t *a_buf)
{
    return a_buf->size;
}

/* ========================================================================== */
/*                             Pipeline                                       */
/* ========================================================================== */

static dap_gpu_error_t s_vk_pipeline_create(const dap_gpu_pipeline_desc_t *a_desc,
                                              dap_gpu_pipeline_t **a_out_pipe)
{
    dap_gpu_pipeline_t *l_pipe = calloc(1, sizeof(*l_pipe));
    if (!l_pipe)
        return DAP_GPU_ERR_OUT_OF_MEMORY;

    /* Shader module (SPIR-V) */
    VkShaderModuleCreateInfo l_shader_ci = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = a_desc->shader_code_size,
        .pCode = (const uint32_t *)a_desc->shader_code,
    };
    if (vkCreateShaderModule(s_vk_ctx.device, &l_shader_ci, NULL,
                             &l_pipe->shader_module) != VK_SUCCESS) {
        free(l_pipe);
        return DAP_GPU_ERR_SHADER_COMPILE;
    }

    /* Descriptor set layout */
    uint32_t l_total_bindings = a_desc->num_storage_buffers + a_desc->num_uniform_buffers;
    l_pipe->num_bindings = l_total_bindings;

    VkDescriptorSetLayoutBinding *l_layout_bindings =
        calloc(l_total_bindings, sizeof(*l_layout_bindings));
    if (!l_layout_bindings && l_total_bindings > 0) {
        vkDestroyShaderModule(s_vk_ctx.device, l_pipe->shader_module, NULL);
        free(l_pipe);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    uint32_t l_idx = 0;
    for (uint32_t i = 0; i < a_desc->num_storage_buffers; ++i, ++l_idx) {
        l_layout_bindings[l_idx] = (VkDescriptorSetLayoutBinding){
            .binding = l_idx,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }
    for (uint32_t i = 0; i < a_desc->num_uniform_buffers; ++i, ++l_idx) {
        l_layout_bindings[l_idx] = (VkDescriptorSetLayoutBinding){
            .binding = l_idx,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        };
    }

    VkDescriptorSetLayoutCreateInfo l_desc_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = l_total_bindings,
        .pBindings = l_layout_bindings,
    };
    VkResult l_res = vkCreateDescriptorSetLayout(s_vk_ctx.device, &l_desc_layout_ci,
                                                  NULL, &l_pipe->desc_set_layout);
    free(l_layout_bindings);
    if (l_res != VK_SUCCESS) {
        vkDestroyShaderModule(s_vk_ctx.device, l_pipe->shader_module, NULL);
        free(l_pipe);
        return DAP_GPU_ERR_PIPELINE_CREATE;
    }

    /* Push constants */
    l_pipe->push_constant_size = a_desc->push_constant_size;
    VkPushConstantRange l_push_range = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = a_desc->push_constant_size,
    };

    /* Pipeline layout */
    VkPipelineLayoutCreateInfo l_layout_ci = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &l_pipe->desc_set_layout,
        .pushConstantRangeCount = a_desc->push_constant_size > 0 ? 1 : 0,
        .pPushConstantRanges = a_desc->push_constant_size > 0 ? &l_push_range : NULL,
    };
    if (vkCreatePipelineLayout(s_vk_ctx.device, &l_layout_ci, NULL,
                               &l_pipe->pipeline_layout) != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(s_vk_ctx.device, l_pipe->desc_set_layout, NULL);
        vkDestroyShaderModule(s_vk_ctx.device, l_pipe->shader_module, NULL);
        free(l_pipe);
        return DAP_GPU_ERR_PIPELINE_CREATE;
    }

    /* Compute pipeline */
    VkComputePipelineCreateInfo l_pipe_ci = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = l_pipe->shader_module,
            .pName = a_desc->entry_point ? a_desc->entry_point : "main",
        },
        .layout = l_pipe->pipeline_layout,
    };
    if (vkCreateComputePipelines(s_vk_ctx.device, VK_NULL_HANDLE, 1, &l_pipe_ci,
                                 NULL, &l_pipe->pipeline) != VK_SUCCESS) {
        vkDestroyPipelineLayout(s_vk_ctx.device, l_pipe->pipeline_layout, NULL);
        vkDestroyDescriptorSetLayout(s_vk_ctx.device, l_pipe->desc_set_layout, NULL);
        vkDestroyShaderModule(s_vk_ctx.device, l_pipe->shader_module, NULL);
        free(l_pipe);
        return DAP_GPU_ERR_PIPELINE_CREATE;
    }

    /* Descriptor pool */
    VkDescriptorPoolSize l_pool_sizes[2];
    uint32_t l_pool_count = 0;
    if (a_desc->num_storage_buffers > 0) {
        l_pool_sizes[l_pool_count++] = (VkDescriptorPoolSize){
            .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = a_desc->num_storage_buffers,
        };
    }
    if (a_desc->num_uniform_buffers > 0) {
        l_pool_sizes[l_pool_count++] = (VkDescriptorPoolSize){
            .type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptorCount = a_desc->num_uniform_buffers,
        };
    }

    if (l_pool_count > 0) {
        VkDescriptorPoolCreateInfo l_pool_ci = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = l_pool_count,
            .pPoolSizes = l_pool_sizes,
        };
        if (vkCreateDescriptorPool(s_vk_ctx.device, &l_pool_ci, NULL,
                                   &l_pipe->desc_pool) != VK_SUCCESS) {
            vkDestroyPipeline(s_vk_ctx.device, l_pipe->pipeline, NULL);
            vkDestroyPipelineLayout(s_vk_ctx.device, l_pipe->pipeline_layout, NULL);
            vkDestroyDescriptorSetLayout(s_vk_ctx.device, l_pipe->desc_set_layout, NULL);
            vkDestroyShaderModule(s_vk_ctx.device, l_pipe->shader_module, NULL);
            free(l_pipe);
            return DAP_GPU_ERR_PIPELINE_CREATE;
        }
    }

    *a_out_pipe = l_pipe;
    return DAP_GPU_OK;
}

static void s_vk_pipeline_destroy(dap_gpu_pipeline_t *a_pipe)
{
    if (a_pipe->desc_pool)
        vkDestroyDescriptorPool(s_vk_ctx.device, a_pipe->desc_pool, NULL);
    vkDestroyPipeline(s_vk_ctx.device, a_pipe->pipeline, NULL);
    vkDestroyPipelineLayout(s_vk_ctx.device, a_pipe->pipeline_layout, NULL);
    vkDestroyDescriptorSetLayout(s_vk_ctx.device, a_pipe->desc_set_layout, NULL);
    vkDestroyShaderModule(s_vk_ctx.device, a_pipe->shader_module, NULL);
    free(a_pipe);
}

/* ========================================================================== */
/*                          Dispatch + Sync                                   */
/* ========================================================================== */

static dap_gpu_error_t s_vk_dispatch(const dap_gpu_dispatch_desc_t *a_desc,
                                      dap_gpu_fence_t **a_out_fence)
{
    dap_gpu_pipeline_t *l_pipe = a_desc->pipeline;

    /* Allocate and write descriptor set */
    VkDescriptorSet l_desc_set = VK_NULL_HANDLE;
    if (l_pipe->desc_pool && a_desc->num_bindings > 0) {
        VkDescriptorSetAllocateInfo l_ds_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = l_pipe->desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &l_pipe->desc_set_layout,
        };
        if (vkAllocateDescriptorSets(s_vk_ctx.device, &l_ds_alloc, &l_desc_set) != VK_SUCCESS)
            return DAP_GPU_ERR_DISPATCH_FAILED;

        VkWriteDescriptorSet *l_writes = calloc(a_desc->num_bindings, sizeof(*l_writes));
        VkDescriptorBufferInfo *l_buf_infos = calloc(a_desc->num_bindings, sizeof(*l_buf_infos));
        if (!l_writes || !l_buf_infos) {
            free(l_writes);
            free(l_buf_infos);
            return DAP_GPU_ERR_OUT_OF_MEMORY;
        }

        for (uint32_t i = 0; i < a_desc->num_bindings; ++i) {
            const dap_gpu_binding_desc_t *b = &a_desc->bindings[i];
            l_buf_infos[i] = (VkDescriptorBufferInfo){
                .buffer = b->buffer->vk_buffer,
                .offset = b->offset,
                .range = b->range > 0 ? b->range : VK_WHOLE_SIZE,
            };
            l_writes[i] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = l_desc_set,
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &l_buf_infos[i],
            };
        }
        vkUpdateDescriptorSets(s_vk_ctx.device, a_desc->num_bindings, l_writes, 0, NULL);
        free(l_writes);
        free(l_buf_infos);
    }

    /* Record command buffer */
    VkCommandBuffer l_cmd = s_alloc_cmd_buffer(s_vk_ctx.compute_cmd_pool);
    if (l_cmd == VK_NULL_HANDLE)
        return DAP_GPU_ERR_DISPATCH_FAILED;

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(l_cmd, &l_begin);

    vkCmdBindPipeline(l_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l_pipe->pipeline);

    if (l_desc_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(l_cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                l_pipe->pipeline_layout, 0, 1, &l_desc_set, 0, NULL);
    }

    if (a_desc->push_constants && a_desc->push_constant_size > 0) {
        vkCmdPushConstants(l_cmd, l_pipe->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, a_desc->push_constant_size, a_desc->push_constants);
    }

    vkCmdDispatch(l_cmd,
                  a_desc->group_count[0],
                  a_desc->group_count[1] ? a_desc->group_count[1] : 1,
                  a_desc->group_count[2] ? a_desc->group_count[2] : 1);

    vkEndCommandBuffer(l_cmd);

    /* Submit */
    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence l_vk_fence = VK_NULL_HANDLE;
    if (vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_vk_fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &l_cmd,
    };
    if (vkQueueSubmit(s_vk_ctx.compute_queue, 1, &l_submit, l_vk_fence) != VK_SUCCESS) {
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    if (a_out_fence) {
        dap_gpu_fence_t *l_fence = calloc(1, sizeof(*l_fence));
        if (!l_fence) {
            vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
            vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
            return DAP_GPU_ERR_OUT_OF_MEMORY;
        }
        l_fence->vk_fence = l_vk_fence;
        l_fence->cmd_buf = l_cmd;
        *a_out_fence = l_fence;
    } else {
        vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &l_cmd);
    }

    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_fence_wait(dap_gpu_fence_t *a_fence, uint64_t a_timeout_ns)
{
    uint64_t l_timeout = a_timeout_ns > 0 ? a_timeout_ns : UINT64_MAX;
    VkResult l_res = vkWaitForFences(s_vk_ctx.device, 1, &a_fence->vk_fence,
                                      VK_TRUE, l_timeout);
    if (l_res == VK_TIMEOUT)
        return DAP_GPU_ERR_TIMEOUT;
    if (l_res == VK_ERROR_DEVICE_LOST)
        return DAP_GPU_ERR_DEVICE_LOST;
    if (l_res != VK_SUCCESS)
        return DAP_GPU_ERR_DISPATCH_FAILED;
    return DAP_GPU_OK;
}

static bool s_vk_fence_is_signaled(dap_gpu_fence_t *a_fence)
{
    return vkGetFenceStatus(s_vk_ctx.device, a_fence->vk_fence) == VK_SUCCESS;
}

static void s_vk_fence_destroy(dap_gpu_fence_t *a_fence)
{
    vkDestroyFence(s_vk_ctx.device, a_fence->vk_fence, NULL);
    if (a_fence->cmd_buf) {
        VkCommandPool l_pool = a_fence->owning_pool ? a_fence->owning_pool
                                                     : s_vk_ctx.compute_cmd_pool;
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &a_fence->cmd_buf);
    }
    free(a_fence);
}

static dap_gpu_error_t s_vk_wait_idle(void)
{
    if (vkQueueWaitIdle(s_vk_ctx.compute_queue) != VK_SUCCESS)
        return DAP_GPU_ERR_DEVICE_LOST;
    if (s_vk_ctx.has_dedicated_transfer) {
        if (vkQueueWaitIdle(s_vk_ctx.transfer_queue) != VK_SUCCESS)
            return DAP_GPU_ERR_DEVICE_LOST;
    }
    return DAP_GPU_OK;
}

/* ========================================================================== */
/*                        Async transfer                                      */
/* ========================================================================== */

static dap_gpu_fence_t *s_make_signaled_fence(void)
{
    dap_gpu_fence_t *l_fence = calloc(1, sizeof(*l_fence));
    if (!l_fence)
        return NULL;
    VkFenceCreateInfo l_ci = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };
    if (vkCreateFence(s_vk_ctx.device, &l_ci, NULL, &l_fence->vk_fence) != VK_SUCCESS) {
        free(l_fence);
        return NULL;
    }
    l_fence->cmd_buf = VK_NULL_HANDLE;
    l_fence->owning_pool = VK_NULL_HANDLE;
    return l_fence;
}

static dap_gpu_error_t s_vk_buffer_upload_async(dap_gpu_buffer_t *a_buf,
                                                 const void *a_data, size_t a_size,
                                                 size_t a_offset,
                                                 dap_gpu_fence_t **a_out_fence)
{
    if (a_buf->mem_type != DAP_GPU_MEM_DEVICE) {
        if (!a_buf->mapped_ptr)
            return DAP_GPU_ERR_BUFFER_MAP_FAILED;
        memcpy((uint8_t *)a_buf->mapped_ptr + a_offset, a_data, a_size);
        *a_out_fence = s_make_signaled_fence();
        return *a_out_fence ? DAP_GPU_OK : DAP_GPU_ERR_OUT_OF_MEMORY;
    }

    dap_gpu_buffer_t *l_staging = NULL;
    dap_gpu_error_t l_rc = s_vk_buffer_create(
        a_size, DAP_GPU_BUF_TRANSFER_SRC, DAP_GPU_MEM_STAGING, &l_staging);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    memcpy(l_staging->mapped_ptr, a_data, a_size);

    VkCommandPool l_pool = s_vk_ctx.has_dedicated_transfer
        ? s_vk_ctx.transfer_cmd_pool : s_vk_ctx.compute_cmd_pool;
    VkQueue l_queue = s_vk_ctx.has_dedicated_transfer
        ? s_vk_ctx.transfer_queue : s_vk_ctx.compute_queue;

    VkCommandBuffer l_cmd = s_alloc_cmd_buffer(l_pool);
    if (l_cmd == VK_NULL_HANDLE) {
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(l_cmd, &l_begin);
    VkBufferCopy l_copy = { .srcOffset = 0, .dstOffset = a_offset, .size = a_size };
    vkCmdCopyBuffer(l_cmd, l_staging->vk_buffer, a_buf->vk_buffer, 1, &l_copy);
    vkEndCommandBuffer(l_cmd);

    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence l_vk_fence = VK_NULL_HANDLE;
    if (vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_vk_fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &l_cmd);
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &l_cmd,
    };
    if (vkQueueSubmit(l_queue, 1, &l_submit, l_vk_fence) != VK_SUCCESS) {
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &l_cmd);
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    /*
     * The staging buffer must stay alive until the transfer completes.
     * We leak it here — in a production-grade implementation we'd attach it
     * to the fence and free on fence_destroy. For now the caller must
     * fence_wait before the staging buffer's backing memory is reused.
     * A proper staging allocator is tracked for a follow-up.
     */

    dap_gpu_fence_t *l_fence = calloc(1, sizeof(*l_fence));
    if (!l_fence) {
        vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &l_cmd);
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    l_fence->vk_fence = l_vk_fence;
    l_fence->cmd_buf = l_cmd;
    l_fence->owning_pool = l_pool;
    *a_out_fence = l_fence;
    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_buffer_download_async(dap_gpu_buffer_t *a_buf,
                                                    void *a_data, size_t a_size,
                                                    size_t a_offset,
                                                    dap_gpu_fence_t **a_out_fence)
{
    if (a_buf->mem_type != DAP_GPU_MEM_DEVICE) {
        if (!a_buf->mapped_ptr)
            return DAP_GPU_ERR_BUFFER_MAP_FAILED;
        memcpy(a_data, (const uint8_t *)a_buf->mapped_ptr + a_offset, a_size);
        *a_out_fence = s_make_signaled_fence();
        return *a_out_fence ? DAP_GPU_OK : DAP_GPU_ERR_OUT_OF_MEMORY;
    }

    dap_gpu_buffer_t *l_staging = NULL;
    dap_gpu_error_t l_rc = s_vk_buffer_create(
        a_size, DAP_GPU_BUF_TRANSFER_DST, DAP_GPU_MEM_STAGING, &l_staging);
    if (l_rc != DAP_GPU_OK)
        return l_rc;

    VkCommandPool l_pool = s_vk_ctx.has_dedicated_transfer
        ? s_vk_ctx.transfer_cmd_pool : s_vk_ctx.compute_cmd_pool;
    VkQueue l_queue = s_vk_ctx.has_dedicated_transfer
        ? s_vk_ctx.transfer_queue : s_vk_ctx.compute_queue;

    VkCommandBuffer l_cmd = s_alloc_cmd_buffer(l_pool);
    if (l_cmd == VK_NULL_HANDLE) {
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    vkBeginCommandBuffer(l_cmd, &l_begin);
    VkBufferCopy l_copy = { .srcOffset = a_offset, .dstOffset = 0, .size = a_size };
    vkCmdCopyBuffer(l_cmd, a_buf->vk_buffer, l_staging->vk_buffer, 1, &l_copy);
    vkEndCommandBuffer(l_cmd);

    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence l_vk_fence = VK_NULL_HANDLE;
    vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_vk_fence);

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &l_cmd,
    };
    if (vkQueueSubmit(l_queue, 1, &l_submit, l_vk_fence) != VK_SUCCESS) {
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &l_cmd);
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    /* Same staging lifetime concern as upload_async — see comment there */

    dap_gpu_fence_t *l_fence = calloc(1, sizeof(*l_fence));
    if (!l_fence) {
        vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
        memcpy(a_data, l_staging->mapped_ptr, a_size);
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, l_pool, 1, &l_cmd);
        s_vk_buffer_destroy(l_staging);
        return DAP_GPU_ERR_OUT_OF_MEMORY;
    }
    l_fence->vk_fence = l_vk_fence;
    l_fence->cmd_buf = l_cmd;
    l_fence->owning_pool = l_pool;
    *a_out_fence = l_fence;
    return DAP_GPU_OK;
}

/* ========================================================================== */
/*                          Batch dispatch                                    */
/* ========================================================================== */

static VkCommandBuffer s_batch_cmd = VK_NULL_HANDLE;

static dap_gpu_error_t s_vk_batch_begin(void)
{
    if (s_batch_cmd != VK_NULL_HANDLE)
        return DAP_GPU_ERR_INVALID_PARAM;

    s_batch_cmd = s_alloc_cmd_buffer(s_vk_ctx.compute_cmd_pool);
    if (s_batch_cmd == VK_NULL_HANDLE)
        return DAP_GPU_ERR_DISPATCH_FAILED;

    VkCommandBufferBeginInfo l_begin = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    if (vkBeginCommandBuffer(s_batch_cmd, &l_begin) != VK_SUCCESS) {
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &s_batch_cmd);
        s_batch_cmd = VK_NULL_HANDLE;
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }
    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_batch_dispatch(const dap_gpu_dispatch_desc_t *a_desc)
{
    if (s_batch_cmd == VK_NULL_HANDLE)
        return DAP_GPU_ERR_INVALID_PARAM;

    dap_gpu_pipeline_t *l_pipe = a_desc->pipeline;

    /* Allocate and update descriptor set if bindings provided */
    VkDescriptorSet l_desc_set = VK_NULL_HANDLE;
    if (l_pipe->desc_pool && a_desc->num_bindings > 0) {
        VkDescriptorSetAllocateInfo l_ds_alloc = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = l_pipe->desc_pool,
            .descriptorSetCount = 1,
            .pSetLayouts = &l_pipe->desc_set_layout,
        };
        if (vkAllocateDescriptorSets(s_vk_ctx.device, &l_ds_alloc, &l_desc_set) != VK_SUCCESS)
            return DAP_GPU_ERR_DISPATCH_FAILED;

        VkWriteDescriptorSet *l_writes = calloc(a_desc->num_bindings, sizeof(*l_writes));
        VkDescriptorBufferInfo *l_buf_infos = calloc(a_desc->num_bindings, sizeof(*l_buf_infos));
        if (!l_writes || !l_buf_infos) {
            free(l_writes);
            free(l_buf_infos);
            return DAP_GPU_ERR_OUT_OF_MEMORY;
        }
        for (uint32_t i = 0; i < a_desc->num_bindings; ++i) {
            const dap_gpu_binding_desc_t *b = &a_desc->bindings[i];
            l_buf_infos[i] = (VkDescriptorBufferInfo){
                .buffer = b->buffer->vk_buffer,
                .offset = b->offset,
                .range = b->range > 0 ? b->range : VK_WHOLE_SIZE,
            };
            l_writes[i] = (VkWriteDescriptorSet){
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = l_desc_set,
                .dstBinding = b->binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                .pBufferInfo = &l_buf_infos[i],
            };
        }
        vkUpdateDescriptorSets(s_vk_ctx.device, a_desc->num_bindings, l_writes, 0, NULL);
        free(l_writes);
        free(l_buf_infos);
    }

    vkCmdBindPipeline(s_batch_cmd, VK_PIPELINE_BIND_POINT_COMPUTE, l_pipe->pipeline);

    if (l_desc_set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(s_batch_cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                l_pipe->pipeline_layout, 0, 1, &l_desc_set, 0, NULL);
    }

    if (a_desc->push_constants && a_desc->push_constant_size > 0) {
        vkCmdPushConstants(s_batch_cmd, l_pipe->pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                           0, a_desc->push_constant_size, a_desc->push_constants);
    }

    /* Memory barrier between consecutive dispatches to ensure correct ordering */
    VkMemoryBarrier l_barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
    };
    vkCmdPipelineBarrier(s_batch_cmd,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0, 1, &l_barrier, 0, NULL, 0, NULL);

    vkCmdDispatch(s_batch_cmd,
                  a_desc->group_count[0],
                  a_desc->group_count[1] ? a_desc->group_count[1] : 1,
                  a_desc->group_count[2] ? a_desc->group_count[2] : 1);

    return DAP_GPU_OK;
}

static dap_gpu_error_t s_vk_batch_end(dap_gpu_fence_t **a_out_fence)
{
    if (s_batch_cmd == VK_NULL_HANDLE)
        return DAP_GPU_ERR_INVALID_PARAM;

    vkEndCommandBuffer(s_batch_cmd);

    VkFenceCreateInfo l_fence_ci = { .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    VkFence l_vk_fence = VK_NULL_HANDLE;
    if (vkCreateFence(s_vk_ctx.device, &l_fence_ci, NULL, &l_vk_fence) != VK_SUCCESS) {
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &s_batch_cmd);
        s_batch_cmd = VK_NULL_HANDLE;
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    VkSubmitInfo l_submit = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &s_batch_cmd,
    };
    if (vkQueueSubmit(s_vk_ctx.compute_queue, 1, &l_submit, l_vk_fence) != VK_SUCCESS) {
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &s_batch_cmd);
        s_batch_cmd = VK_NULL_HANDLE;
        return DAP_GPU_ERR_DISPATCH_FAILED;
    }

    if (a_out_fence) {
        dap_gpu_fence_t *l_fence = calloc(1, sizeof(*l_fence));
        if (!l_fence) {
            vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
            vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
            vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &s_batch_cmd);
            s_batch_cmd = VK_NULL_HANDLE;
            return DAP_GPU_ERR_OUT_OF_MEMORY;
        }
        l_fence->vk_fence = l_vk_fence;
        l_fence->cmd_buf = s_batch_cmd;
        l_fence->owning_pool = s_vk_ctx.compute_cmd_pool;
        *a_out_fence = l_fence;
    } else {
        vkWaitForFences(s_vk_ctx.device, 1, &l_vk_fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(s_vk_ctx.device, l_vk_fence, NULL);
        vkFreeCommandBuffers(s_vk_ctx.device, s_vk_ctx.compute_cmd_pool, 1, &s_batch_cmd);
    }

    s_batch_cmd = VK_NULL_HANDLE;
    return DAP_GPU_OK;
}

/* ========================================================================== */
/*                          Probe function                                    */
/* ========================================================================== */

dap_gpu_vulkan_ctx_t *dap_gpu_vulkan_get_ctx(void)
{
    return &s_vk_ctx;
}

bool dap_gpu_vulkan_probe(dap_gpu_backend_ops_t *a_ops)
{
    /* Check if Vulkan loader is available by trying to enumerate instance version */
    uint32_t l_api_version = 0;
    if (vkEnumerateInstanceVersion(&l_api_version) != VK_SUCCESS)
        return false;
    if (l_api_version < VK_API_VERSION_1_1) {
        log_it(L_DEBUG, "Vulkan API version %u.%u < 1.1, skipping",
               VK_VERSION_MAJOR(l_api_version), VK_VERSION_MINOR(l_api_version));
        return false;
    }

    log_it(L_DEBUG, "Vulkan %u.%u.%u available",
           VK_VERSION_MAJOR(l_api_version),
           VK_VERSION_MINOR(l_api_version),
           VK_VERSION_PATCH(l_api_version));

    a_ops->init             = s_vk_init;
    a_ops->deinit           = s_vk_deinit;
    a_ops->buffer_create    = s_vk_buffer_create;
    a_ops->buffer_destroy   = s_vk_buffer_destroy;
    a_ops->buffer_upload    = s_vk_buffer_upload;
    a_ops->buffer_download  = s_vk_buffer_download;
    a_ops->buffer_map       = s_vk_buffer_map;
    a_ops->buffer_unmap     = s_vk_buffer_unmap;
    a_ops->buffer_size      = s_vk_buffer_size;
    a_ops->pipeline_create       = s_vk_pipeline_create;
    a_ops->pipeline_destroy      = s_vk_pipeline_destroy;
    a_ops->buffer_upload_async   = s_vk_buffer_upload_async;
    a_ops->buffer_download_async = s_vk_buffer_download_async;
    a_ops->dispatch              = s_vk_dispatch;
    a_ops->batch_begin           = s_vk_batch_begin;
    a_ops->batch_dispatch        = s_vk_batch_dispatch;
    a_ops->batch_end             = s_vk_batch_end;
    a_ops->fence_wait            = s_vk_fence_wait;
    a_ops->fence_is_signaled     = s_vk_fence_is_signaled;
    a_ops->fence_destroy         = s_vk_fence_destroy;
    a_ops->wait_idle             = s_vk_wait_idle;

    return true;
}

#endif /* DAP_GPU_VULKAN */
