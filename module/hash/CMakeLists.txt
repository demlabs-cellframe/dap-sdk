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
# Keccak SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(KECCAK_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/keccak_arch_gen")
set(K ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak)

# Lane-layout variants (SSE2, AVX2, NEON, SVE)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_lane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${K}/arch/x86/avx2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_lane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_sse2.c
    ARCH     sse2
    ARGS     "PRIMITIVES_FILE=${K}/arch/x86/sse2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_lane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${K}/arch/arm/neon_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_lane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_sve.c
    ARCH     sve
    ARGS     "PRIMITIVES_FILE=${K}/arch/arm/sve_primitives.tpl"
)

# Plane-layout variants (AVX-512, SVE2)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_plane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_avx512.c
    ARCH     avx512
    ARGS     "PRIMITIVES_FILE=${K}/arch/x86/avx512_primitives.tpl"
             "ALIGNMENT_ATTR=__attribute__((aligned(64)))"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_simd_plane.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_sve2.c
    ARCH     sve2
    ARGS     "PRIMITIVES_FILE=${K}/arch/arm/sve2_primitives.tpl"
             "ALIGNMENT_ATTR=__attribute__((aligned(64)))"
)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${ALL_SRCS} HEADERS ${ALL_HDRS})

dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${KECCAK_GEN_DIR}
    SOURCES
        dap_keccak_avx512.c  avx512
        dap_keccak_avx2.c    avx2
        dap_keccak_sse2.c    sse2
        dap_keccak_neon.c    neon
        dap_keccak_sve.c     sve
        dap_keccak_sve2.c    sve2
)

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sha2-256
)

# Dependencies
target_link_libraries(${PROJECT_NAME} dap_core)

# Add hash table submodule
add_subdirectory(table)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${HASH_HEADERS} DESTINATION include/dap/hash/)
endif()
