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

# Run code generation
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
            ${CMAKE_CURRENT_SOURCE_DIR}/src/stage2/generate_string_simd.sh ${SIMD_GEN_DIR}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/stage2
    RESULT_VARIABLE STRING_CODEGEN_RESULT
    OUTPUT_VARIABLE STRING_CODEGEN_OUTPUT
    ERROR_VARIABLE STRING_CODEGEN_ERROR
)
if(NOT STRING_CODEGEN_RESULT EQUAL 0)
    message(WARNING "[dap_json] String scanner SIMD codegen failed: ${STRING_CODEGEN_ERROR}")
else()
    message(STATUS "[dap_json] String scanner SIMD codegen completed successfully")
endif()

# Architecture-specific sources - collect from GENERATED directory in build
set(JSON_ARCH_SRCS "")
file(GLOB JSON_ARCH_SRCS ${SIMD_GEN_DIR}/*.c)

# Combine all sources
set(JSON_ALL_SRCS ${JSON_SRCS} ${JSON_STAGE1_SRCS} ${JSON_STAGE2_SRCS} ${JSON_ARCH_SRCS})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${JSON_ALL_SRCS} HEADERS ${JSON_HEADERS} ${JSON_INTERNAL_HEADERS})

# Set architecture-specific compiler flags for SIMD implementations
# This enables compile-time dispatch based on available instruction sets
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686")
    # x86/x64 architecture - sources are GENERATED, search in build directory
    file(GLOB AVX2_SRCS ${SIMD_GEN_DIR}/*avx2.c)
    file(GLOB SSE2_SRCS ${SIMD_GEN_DIR}/*sse2.c)
    file(GLOB AVX512_SRCS ${SIMD_GEN_DIR}/*avx512.c)
    
    if(AVX2_SRCS)
        set_source_files_properties(${AVX2_SRCS} PROPERTIES COMPILE_FLAGS "-mavx2 -mbmi -mbmi2 -mpopcnt")
        message(STATUS "[dap_json] AVX2 implementation enabled: ${AVX2_SRCS}")
    endif()
    
    if(SSE2_SRCS)
        set_source_files_properties(${SSE2_SRCS} PROPERTIES COMPILE_FLAGS "-msse2 -msse4.2")
        message(STATUS "[dap_json] SSE2 implementation enabled: ${SSE2_SRCS}")
    endif()
    
    if(AVX512_SRCS)
        set_source_files_properties(${AVX512_SRCS} PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512dq -mavx512bw -mavx512vl")
        message(STATUS "[dap_json] AVX-512 implementation enabled: ${AVX512_SRCS}")
    endif()
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|aarch64|ARM")
    # ARM architecture - sources are GENERATED, search in build directory
    file(GLOB NEON_SRCS ${SIMD_GEN_DIR}/*neon.c)
    file(GLOB SVE_SRCS ${SIMD_GEN_DIR}/*sve.c)
    file(GLOB SVE2_SRCS ${SIMD_GEN_DIR}/*sve2.c)
    
    if(NEON_SRCS)
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            # ARM64 - NEON is always available
            set_source_files_properties(${NEON_SRCS} PROPERTIES COMPILE_FLAGS "")
        else()
            # ARM32 - explicit NEON flag
            set_source_files_properties(${NEON_SRCS} PROPERTIES COMPILE_FLAGS "-mfpu=neon")
        endif()
        message(STATUS "[dap_json] NEON implementation enabled: ${NEON_SRCS}")
    endif()
    
    if(SVE_SRCS)
        set_source_files_properties(${SVE_SRCS} PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve")
        message(STATUS "[dap_json] SVE implementation enabled: ${SVE_SRCS}")
    endif()
    
    if(SVE2_SRCS)
        set_source_files_properties(${SVE2_SRCS} PROPERTIES COMPILE_FLAGS "-march=armv8-a+sve2")
        message(STATUS "[dap_json] SVE2 implementation enabled: ${SVE2_SRCS}")
    endif()
endif()

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/internal
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty
)

# Add include directory for generated SIMD headers
target_include_directories(${PROJECT_NAME} PUBLIC ${SIMD_GEN_DIR})

# Dependencies - json depends only on core (json-c REMOVED!)
set(JSON_LIBRARIES "dap_core" CACHE INTERNAL "dap_json external dependencies")

# Link dependencies
target_link_libraries(${PROJECT_NAME} PUBLIC dap_core PRIVATE ${JSON_LIBRARIES})

# Tests
if(BUILD_DAP_TESTS)
    add_definitions("-DDAP_JSON_TESTS")
    message("Enabled dap_json comprehensive test suite")
    enable_testing()
    # Note: Tests are in tests/unit/json/ and managed by tests/CMakeLists.txt
    # New test suite (Phase 0.2): Unicode, numeric edges, invalid JSON, boundaries, benchmarks
endif()

# Installation
if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/json
        ARCHIVE DESTINATION lib/dap/json
        PUBLIC_HEADER DESTINATION include/dap/json/
    )
endif()

