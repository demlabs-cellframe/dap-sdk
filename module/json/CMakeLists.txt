cmake_minimum_required(VERSION 3.12)
project(dap_json)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

add_definitions("-D_GNU_SOURCE")

# Collect JSON module sources and headers
file(GLOB JSON_HEADERS include/*.h)
file(GLOB JSON_INTERNAL_HEADERS include/internal/*.h)
file(GLOB JSON_SRCS src/*.c)
file(GLOB JSON_STAGE1_SRCS src/stage1/*.c)
file(GLOB JSON_STAGE2_SRCS src/stage2/*.c src/stage2/string/*.c src/stage2/number/*.c)
file(GLOB JSON_TAPE_SRCS src/tape/*.c)

# Generate SIMD implementations from templates at configure time
# Generated files go to build directory to keep source tree clean
set(SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/simd_gen")
file(MAKE_DIRECTORY ${SIMD_GEN_DIR})

message(STATUS "[dap_json] Generating SIMD implementations from templates...")
message(STATUS "[dap_json] Output directory: ${SIMD_GEN_DIR}")

# Prepare environment for generator script
# DAP_TPL_DIR can be set externally (e.g., when dap-sdk is a submodule in another project)
# CMAKE_SYSTEM_PROCESSOR is passed for correct cross-compilation support
set(CODEGEN_ENV "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}")
if(DEFINED DAP_TPL_DIR)
    message(STATUS "[dap_json] Using external dap_tpl from: ${DAP_TPL_DIR}")
    list(APPEND CODEGEN_ENV "DAP_TPL_DIR=${DAP_TPL_DIR}")
endif()
message(STATUS "[dap_json] Target architecture: ${CMAKE_SYSTEM_PROCESSOR}")

# Skip SIMD codegen on macOS - dap_tpl uses gawk-specific functions (systime, PROCINFO)
# that are not available in BSD awk. Reference C implementation will be used instead.
if(APPLE)
    message(STATUS "[dap_json] Skipping SIMD codegen on macOS (BSD awk incompatible)")
    message(STATUS "[dap_json] Using reference C implementation only")
else()
    # Run code generation (Linux/other platforms with GNU awk)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${CODEGEN_ENV}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/stage1/generate_simd.sh ${SIMD_GEN_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/stage1
        RESULT_VARIABLE CODEGEN_RESULT
        OUTPUT_VARIABLE CODEGEN_OUTPUT
        ERROR_VARIABLE CODEGEN_ERROR
    )
    if(NOT CODEGEN_RESULT EQUAL 0)
        message(WARNING "[dap_json] Stage 1 SIMD codegen failed: ${CODEGEN_ERROR}")
    else()
        message(STATUS "[dap_json] Stage 1 SIMD codegen completed successfully")
    endif()

    # Generate Stage 2 String Scanner SIMD implementations
    message(STATUS "[dap_json] Generating Stage 2 String Scanner SIMD implementations...")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${CODEGEN_ENV}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/stage2/string/generate_string_simd.sh ${SIMD_GEN_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/stage2/string
        RESULT_VARIABLE STRING_CODEGEN_RESULT
        OUTPUT_VARIABLE STRING_CODEGEN_OUTPUT
        ERROR_VARIABLE STRING_CODEGEN_ERROR
    )
    if(NOT STRING_CODEGEN_RESULT EQUAL 0)
        message(WARNING "[dap_json] String scanner SIMD codegen failed: ${STRING_CODEGEN_ERROR}")
    else()
        message(STATUS "[dap_json] String scanner SIMD codegen completed successfully")
    endif()
endif()

# Architecture-specific sources - ADD only what's needed for target architecture
# Only add generated SIMD files if they actually exist (graceful fallback if codegen fails)
set(JSON_ARCH_SRCS "")

# Helper macro to add SIMD source only if it exists
macro(add_simd_source_if_exists FILE_PATH)
    if(EXISTS "${FILE_PATH}")
        list(APPEND JSON_ARCH_SRCS "${FILE_PATH}")
    endif()
endmacro()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    # x86/x64: Add AVX2, SSE2, AVX-512 implementations (only if generated)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_avx2.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_sse2.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_avx512.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_avx2.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_sse2.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_avx512.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    # ARM64: Add NEON, SVE, SVE2 implementations (only if generated)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_neon.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_sve.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_sve2.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_neon.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_sve.c)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_string_simd_sve2.c)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    # ARM32: ONLY NEON (no SVE/SVE2)
    add_simd_source_if_exists(${SIMD_GEN_DIR}/dap_json_stage1_neon.c)
endif()

# Report what SIMD sources were found
if(JSON_ARCH_SRCS)
    message(STATUS "[dap_json] SIMD sources enabled: ${JSON_ARCH_SRCS}")
else()
    message(STATUS "[dap_json] No SIMD sources available - using reference implementation only")
endif()

# Set compile definitions based on which SIMD implementations are available
# Check for HEADER files (.h) since those are what get #included
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_neon.h")
    set(DAP_JSON_HAS_NEON TRUE)
endif()
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_sve.h")
    set(DAP_JSON_HAS_SVE TRUE)
endif()
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_sve2.h")
    set(DAP_JSON_HAS_SVE2 TRUE)
endif()
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_sse2.h")
    set(DAP_JSON_HAS_SSE2 TRUE)
endif()
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_avx2.h")
    set(DAP_JSON_HAS_AVX2 TRUE)
endif()
if(EXISTS "${SIMD_GEN_DIR}/dap_json_stage1_avx512.h")
    set(DAP_JSON_HAS_AVX512 TRUE)
endif()

# Combine all sources
set(JSON_ALL_SRCS ${JSON_SRCS} ${JSON_STAGE1_SRCS} ${JSON_STAGE2_SRCS} ${JSON_TAPE_SRCS} ${JSON_ARCH_SRCS})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${JSON_ALL_SRCS} HEADERS ${JSON_HEADERS} ${JSON_INTERNAL_HEADERS})

# Set architecture-specific compiler flags for SIMD implementations
# This enables compile-time dispatch based on available instruction sets
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    # x86/x64 architecture
    set(AVX2_SRCS ${SIMD_GEN_DIR}/dap_json_stage1_avx2.c ${SIMD_GEN_DIR}/dap_json_string_simd_avx2.c)
    set(SSE2_SRCS ${SIMD_GEN_DIR}/dap_json_stage1_sse2.c ${SIMD_GEN_DIR}/dap_json_string_simd_sse2.c)
    set(AVX512_SRCS ${SIMD_GEN_DIR}/dap_json_stage1_avx512.c ${SIMD_GEN_DIR}/dap_json_string_simd_avx512.c)
    
    set_source_files_properties(${AVX2_SRCS} PROPERTIES COMPILE_FLAGS "-mavx2 -mbmi -mbmi2 -mpopcnt")
    set_source_files_properties(${SSE2_SRCS} PROPERTIES COMPILE_FLAGS "-msse2 -msse4.2")
    set_source_files_properties(${AVX512_SRCS} PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq -mavx512bw -mavx512vl")
    
    message(STATUS "[dap_json] AVX2 implementation enabled: ${AVX2_SRCS}")
    message(STATUS "[dap_json] SSE2 implementation enabled: ${SSE2_SRCS}")
    message(STATUS "[dap_json] AVX-512 implementation enabled: ${AVX512_SRCS}")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    # ARM64 architecture (aarch64 on Linux, arm64 on macOS)
    set(NEON_SRCS ${SIMD_GEN_DIR}/dap_json_stage1_neon.c ${SIMD_GEN_DIR}/dap_json_string_simd_neon.c)
    
    # ARM64 - NEON is always available (no extra flags needed on Apple Silicon)
    # Only set flags for files that actually exist
    foreach(src_file ${NEON_SRCS})
        if(EXISTS "${src_file}")
            set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "")
        endif()
    endforeach()
    foreach(src_file ${SVE_SRCS})
        if(EXISTS "${src_file}")
            set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
        endif()
    endforeach()
    foreach(src_file ${SVE2_SRCS})
        if(EXISTS "${src_file}")
            set_source_files_properties(${src_file} PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve2")
        endif()
    endforeach()
    
    message(STATUS "[dap_json] NEON sources configured: ${NEON_SRCS}")
    message(STATUS "[dap_json] SVE sources configured: ${SVE_SRCS}")
    message(STATUS "[dap_json] SVE2 sources configured: ${SVE2_SRCS}")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    # ARM32 architecture (ARMv7)
    set(NEON_SRCS ${SIMD_GEN_DIR}/dap_json_stage1_neon.c)
    
    # ARM32 - explicit NEON flag required
    set_source_files_properties(${NEON_SRCS} PROPERTIES COMPILE_FLAGS "-mfpu=neon")
    
    message(STATUS "[dap_json] NEON implementation enabled (ARM32): ${NEON_SRCS}")
    message(STATUS "[dap_json] SVE/SVE2 skipped (ARM64 only)")
    message(STATUS "[dap_json] Stage 2 String Scanner SIMD: reference implementation (ARM32 NEON in development)")
endif()

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Add include directory for generated SIMD headers
target_include_directories(${PROJECT_NAME} PUBLIC ${SIMD_GEN_DIR})

# Add compile definitions for available SIMD implementations
# These control conditional includes in header files
if(DAP_JSON_HAS_NEON)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_NEON=1)
endif()
if(DAP_JSON_HAS_SVE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_SVE=1)
endif()
if(DAP_JSON_HAS_SVE2)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_SVE2=1)
endif()
if(DAP_JSON_HAS_SSE2)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_SSE2=1)
endif()
if(DAP_JSON_HAS_AVX2)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_AVX2=1)
endif()
if(DAP_JSON_HAS_AVX512)
    target_compile_definitions(${PROJECT_NAME} PUBLIC DAP_JSON_HAS_AVX512=1)
endif()

# Dependencies - json depends only on core (json-c REMOVED!)
set(JSON_LIBRARIES "dap_core" CACHE INTERNAL "dap_json external dependencies")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC dap_core PRIVATE ${JSON_LIBRARIES})
# Installation
if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/json
        ARCHIVE DESTINATION lib/dap/json
        PUBLIC_HEADER DESTINATION include/dap/json/
    )
endif()

