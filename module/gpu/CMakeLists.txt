cmake_minimum_required(VERSION 3.12)
project(dap_gpu)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# ===========================
# Options
# ===========================
option(BUILD_WITH_VULKAN "Build GPU module with Vulkan compute backend" ON)
option(BUILD_WITH_METAL  "Build GPU module with Metal compute backend (macOS/iOS)" OFF)
option(BUILD_WITH_CUDA   "Build GPU module with CUDA compute backend" OFF)
option(BUILD_WITH_OPENCL "Build GPU module with OpenCL compute backend" OFF)

# ===========================
# Core sources (always built)
# ===========================
set(GPU_SRCS src/dap_gpu.c src/dap_gpu_ntt.c)
set(GPU_HDRS include/dap_gpu.h include/dap_gpu_ntt.h)

set(GPU_HAS_BACKEND FALSE)

# ===========================
# Vulkan backend
# ===========================
if(BUILD_WITH_VULKAN)
    find_package(Vulkan QUIET)
    if(Vulkan_FOUND)
        message(STATUS "[GPU] Vulkan found: ${Vulkan_LIBRARIES}")
        list(APPEND GPU_SRCS src/vulkan/dap_gpu_vulkan.c)
        set(GPU_VK_DEFINE DAP_GPU_VULKAN)
        set(GPU_HAS_BACKEND TRUE)

        # Compile GLSL shaders → SPIR-V → embedded C headers
        set(GPU_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
        set(GPU_SPV_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/spv_gen)
        file(MAKE_DIRECTORY ${GPU_SPV_GEN_DIR})

        if(Vulkan_GLSLANG_VALIDATOR_EXECUTABLE)
            set(GLSLANG ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE})
        else()
            find_program(GLSLANG glslangValidator)
        endif()

        if(GLSLANG)
            message(STATUS "[GPU] glslangValidator: ${GLSLANG}")
            include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/SpirvEmbed.cmake)

            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt_mont_forward.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt_mont_forward_spv.h
                VARNAME ntt_mont_forward_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt_mont_inverse.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt_mont_inverse_spv.h
                VARNAME ntt_mont_inverse_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/pointwise_mont.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/pointwise_mont_spv.h
                VARNAME pointwise_mont_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt_forward.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt_forward_spv.h
                VARNAME ntt_forward_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt_inverse.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt_inverse_spv.h
                VARNAME ntt_inverse_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt16_mont_forward.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt16_mont_forward_spv.h
                VARNAME ntt16_mont_forward_spirv
            )
            spirv_embed(
                SHADER  ${GPU_SHADER_DIR}/ntt16_mont_inverse.comp
                OUTPUT  ${GPU_SPV_GEN_DIR}/ntt16_mont_inverse_spv.h
                VARNAME ntt16_mont_inverse_spirv
            )
            list(APPEND GPU_SPV_HEADERS
                ${GPU_SPV_GEN_DIR}/ntt_mont_forward_spv.h
                ${GPU_SPV_GEN_DIR}/ntt_mont_inverse_spv.h
                ${GPU_SPV_GEN_DIR}/pointwise_mont_spv.h
                ${GPU_SPV_GEN_DIR}/ntt_forward_spv.h
                ${GPU_SPV_GEN_DIR}/ntt_inverse_spv.h
                ${GPU_SPV_GEN_DIR}/ntt16_mont_forward_spv.h
                ${GPU_SPV_GEN_DIR}/ntt16_mont_inverse_spv.h
            )
        else()
            message(WARNING "[GPU] glslangValidator not found — shader compilation disabled")
        endif()
    else()
        message(STATUS "[GPU] Vulkan not found — Vulkan backend disabled")
    endif()
endif()

# ===========================
# Metal backend (macOS / iOS)
# ===========================
if(BUILD_WITH_METAL AND APPLE)
    find_library(METAL_FRAMEWORK Metal)
    find_library(FOUNDATION_FRAMEWORK Foundation)
    if(METAL_FRAMEWORK AND FOUNDATION_FRAMEWORK)
        message(STATUS "[GPU] Metal found")
        # Metal sources would go here (Objective-C .m files)
        set(GPU_METAL_DEFINE DAP_GPU_METAL)
        set(GPU_HAS_BACKEND TRUE)
    else()
        message(STATUS "[GPU] Metal framework not found — Metal backend disabled")
    endif()
endif()

# ===========================
# CUDA backend
# ===========================
if(BUILD_WITH_CUDA)
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        message(STATUS "[GPU] CUDA found: ${CUDAToolkit_LIBRARY_DIR}")
        set(GPU_CUDA_DEFINE DAP_GPU_CUDA)
        set(GPU_HAS_BACKEND TRUE)
    else()
        message(STATUS "[GPU] CUDA not found — CUDA backend disabled")
    endif()
endif()

# ===========================
# OpenCL backend
# ===========================
if(BUILD_WITH_OPENCL)
    find_package(OpenCL QUIET)
    if(OpenCL_FOUND)
        message(STATUS "[GPU] OpenCL found: ${OpenCL_LIBRARIES}")
        set(GPU_OPENCL_DEFINE DAP_GPU_OPENCL)
        set(GPU_HAS_BACKEND TRUE)
    else()
        message(STATUS "[GPU] OpenCL not found — OpenCL backend disabled")
    endif()
endif()

if(NOT GPU_HAS_BACKEND)
    message(STATUS "[GPU] No GPU backend available — module will provide stub (CPU fallback only)")
endif()

# ===========================
# Create library
# ===========================
dap_add_library(${PROJECT_NAME} ${GPU_SRCS} HEADERS ${GPU_HDRS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Dependencies
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core)

# ===========================
# Backend-specific linking
# ===========================
if(DEFINED GPU_VK_DEFINE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${GPU_VK_DEFINE})
    target_include_directories(${PROJECT_NAME} PRIVATE ${Vulkan_INCLUDE_DIRS})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${Vulkan_LIBRARIES})

    if(DEFINED GPU_SPV_GEN_DIR)
        target_include_directories(${PROJECT_NAME} PRIVATE ${GPU_SPV_GEN_DIR})
    endif()
    if(DEFINED GPU_SPV_HEADERS)
        target_sources(${PROJECT_NAME} PRIVATE ${GPU_SPV_HEADERS})
    endif()
endif()

if(DEFINED GPU_METAL_DEFINE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${GPU_METAL_DEFINE})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${METAL_FRAMEWORK} ${FOUNDATION_FRAMEWORK})
endif()

if(DEFINED GPU_CUDA_DEFINE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${GPU_CUDA_DEFINE})
    target_link_libraries(${PROJECT_NAME} PRIVATE CUDA::cudart CUDA::cuda_driver)
endif()

if(DEFINED GPU_OPENCL_DEFINE)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ${GPU_OPENCL_DEFINE})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OpenCL_LIBRARIES})
    target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCL_INCLUDE_DIRS})
endif()

# Register headers for installation
register_module_headers(DIRECTORIES include)

if(INSTALL_DAP_SDK)
    install(FILES ${GPU_HDRS} DESTINATION include/dap/gpu/)
endif()
