cmake_minimum_required(VERSION 3.12)
project(dap_hash)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Hash module sources
file(GLOB HASH_HEADERS include/*.h)
file(GLOB HASH_SRCS src/*.c)

# Keccak reference implementation
file(GLOB KECCAK_SRCS src/keccak/*.c)
file(GLOB KECCAK_HDRS src/keccak/*.h)

# SHA2-256 implementation
file(GLOB SHA2_SRCS src/sha2-256/*.c)
file(GLOB SHA2_HDRS src/sha2-256/*.h)

set(ALL_SRCS ${HASH_SRCS} ${KECCAK_SRCS} ${SHA2_SRCS})
set(ALL_HDRS ${HASH_HEADERS} ${KECCAK_HDRS} ${SHA2_HDRS})

# =============================================================================
# Keccak SIMD optimizations - Code Generation
# Generated files go to build directory to keep source tree clean
# =============================================================================

set(KECCAK_SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/keccak_simd_gen")
file(MAKE_DIRECTORY ${KECCAK_SIMD_GEN_DIR})

message(STATUS "[dap_hash] Generating Keccak SIMD implementations from templates...")
message(STATUS "[dap_hash] Output directory: ${KECCAK_SIMD_GEN_DIR}")

# Set up environment for code generation
set(CODEGEN_ENV
    "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
    "DAP_TPL_DIR=${CMAKE_SOURCE_DIR}/module/test/dap_tpl"
)

# Run code generation at configure time
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak/generate_simd.sh)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${CODEGEN_ENV}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak/generate_simd.sh ${KECCAK_SIMD_GEN_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak
        RESULT_VARIABLE KECCAK_CODEGEN_RESULT
        OUTPUT_VARIABLE KECCAK_CODEGEN_OUTPUT
        ERROR_VARIABLE KECCAK_CODEGEN_ERROR
    )

    if(NOT KECCAK_CODEGEN_RESULT EQUAL 0)
        message(WARNING "[dap_hash] Keccak SIMD code generation failed:")
        message(WARNING "  Exit code: ${KECCAK_CODEGEN_RESULT}")
        message(WARNING "  Output: ${KECCAK_CODEGEN_OUTPUT}")
        message(WARNING "  Error: ${KECCAK_CODEGEN_ERROR}")
        message(WARNING "  Only reference implementation will be available")
    else()
        message(STATUS "[dap_hash] Keccak SIMD code generation successful")
    endif()
endif()

# Collect generated SIMD sources and set compiler flags
set(KECCAK_SIMD_SRCS "")

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|i686|i386")
    # x86/x64: AVX-512, AVX2, SSE2 implementations (generated)
    set(KECCAK_AVX512_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_avx512.c)
    set(KECCAK_AVX2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_avx2.c)
    set(KECCAK_SSE2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sse2.c)
    
    if(EXISTS ${KECCAK_AVX512_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_AVX512_SRC})
        set_source_files_properties(${KECCAK_AVX512_SRC} PROPERTIES 
            COMPILE_FLAGS "-mavx512f -mavx512dq -mavx512bw -mavx512vl"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_AVX2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_AVX2_SRC})
        set_source_files_properties(${KECCAK_AVX2_SRC} PROPERTIES 
            COMPILE_FLAGS "-mavx2 -mbmi -mbmi2"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SSE2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SSE2_SRC})
        set_source_files_properties(${KECCAK_SSE2_SRC} PROPERTIES 
            COMPILE_FLAGS "-msse2"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_hash] Keccak SIMD: AVX-512, AVX2, SSE2 enabled (generated)")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
    # ARM64: NEON, SVE, SVE2 implementations (generated)
    set(KECCAK_NEON_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_neon.c)
    set(KECCAK_SVE_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sve.c)
    set(KECCAK_SVE2_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_sve2.c)
    
    if(EXISTS ${KECCAK_NEON_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_NEON_SRC})
        set_source_files_properties(${KECCAK_NEON_SRC} PROPERTIES GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SVE_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SVE_SRC})
        set_source_files_properties(${KECCAK_SVE_SRC} PROPERTIES 
            COMPILE_FLAGS "-march=armv8.2-a+sve"
            GENERATED TRUE)
    endif()
    
    if(EXISTS ${KECCAK_SVE2_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_SVE2_SRC})
        set_source_files_properties(${KECCAK_SVE2_SRC} PROPERTIES 
            COMPILE_FLAGS "-march=armv8.2-a+sve2"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_hash] Keccak SIMD: NEON, SVE, SVE2 enabled (generated)")
    
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm|ARM")
    # ARM32: NEON implementation (generated)
    set(KECCAK_NEON_SRC ${KECCAK_SIMD_GEN_DIR}/dap_keccak_neon.c)
    
    if(EXISTS ${KECCAK_NEON_SRC})
        list(APPEND KECCAK_SIMD_SRCS ${KECCAK_NEON_SRC})
        set_source_files_properties(${KECCAK_NEON_SRC} PROPERTIES 
            COMPILE_FLAGS "-mfpu=neon"
            GENERATED TRUE)
    endif()
    
    message(STATUS "[dap_hash] Keccak SIMD: NEON enabled (generated, ARM32)")
endif()

# Add generated SIMD sources
list(APPEND ALL_SRCS ${KECCAK_SIMD_SRCS})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${ALL_SRCS} HEADERS ${ALL_HDRS})

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sha2-256
)
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${KECCAK_SIMD_GEN_DIR}
)

# Dependencies
target_link_libraries(${PROJECT_NAME} dap_core)

# Add hash table submodule
add_subdirectory(table)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${HASH_HEADERS} DESTINATION include/dap/hash/)
endif()
