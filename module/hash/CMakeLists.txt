cmake_minimum_required(VERSION 3.12)
project(dap_hash)

# Include required CMake modules
include(${CMAKE_CURRENT_SOURCE_DIR}/../../cmake/PackageComponents.cmake)

# Hash module sources
file(GLOB HASH_HEADERS include/*.h)
file(GLOB HASH_SRCS src/*.c)

# Keccak reference implementation (exclude arch-specific files added conditionally below)
file(GLOB KECCAK_SRCS src/keccak/*.c)
file(GLOB KECCAK_HDRS src/keccak/*.h)
list(FILTER KECCAK_SRCS EXCLUDE REGEX "x4_transpose_(avx2|neon)\\.c$")

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

# =============================================================================
# Keccak×4 parallel permutation — generated via DapArchCodegen
# =============================================================================

dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_x4.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_x4_avx512.c
    ARCH     avx512
    ARGS     "PRIMITIVES_FILE=${K}/arch/x86/x4_avx512_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_x4.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_x4_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${K}/arch/x86/x4_avx2_primitives.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${K}/dap_keccak_x4.c.tpl
    OUTPUT   ${KECCAK_GEN_DIR}/dap_keccak_x4_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${K}/arch/arm/x4_neon_primitives.tpl"
)

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${ALL_SRCS} HEADERS ${ALL_HDRS})

dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${KECCAK_GEN_DIR}
    SOURCES
        dap_keccak_avx512.c      avx512
        dap_keccak_avx2.c        avx2
        dap_keccak_sse2.c        sse2
        dap_keccak_neon.c        neon
        dap_keccak_sve.c         sve
        dap_keccak_sve2.c        sve2
        dap_keccak_x4_avx512.c  avx512
        dap_keccak_x4_avx2.c    avx2
        dap_keccak_x4_neon.c    neon
)

# Disable LTO for Keccak AVX-512 to keep looped permutation compact in L1I
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(_keccak_avx512 "${KECCAK_GEN_DIR}/dap_keccak_avx512.c")
    if(EXISTS "${_keccak_avx512}")
        get_source_file_property(_cf "${_keccak_avx512}" COMPILE_FLAGS)
        if(_cf)
            set_source_files_properties("${_keccak_avx512}" PROPERTIES COMPILE_FLAGS "${_cf} -fno-lto")
        else()
            set_source_files_properties("${_keccak_avx512}" PROPERTIES COMPILE_FLAGS "-fno-lto")
        endif()
    endif()
endif()

# x4 transpose helpers — arch-specific extract/xor
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    set(X4_TRANSPOSE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak/dap_keccak_x4_transpose_avx2.c)
    target_sources(${PROJECT_NAME} PRIVATE ${X4_TRANSPOSE_SRC})
    set_source_files_properties(${X4_TRANSPOSE_SRC}
        PROPERTIES COMPILE_FLAGS "-mavx2"
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64|arm64")
    target_sources(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak/dap_keccak_x4_transpose_neon.c
    )
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # AVX-512VL Keccak-f[1600] assembly — shared framework + x86 arch impl
    enable_language(ASM)
    set(KECCAK_AVX512VL_ASM ${KECCAK_GEN_DIR}/dap_keccak_f1600_avx512vl.S)
    file(STRINGS "${K}/arch/x86/keccak_asm_avx512vl.env" _KECCAK_ASM_ENV)
    dap_tpl_generate(
        TEMPLATE ${K}/dap_keccak_f1600_asm.S.tpl
        OUTPUT   ${KECCAK_AVX512VL_ASM}
        ARGS     "FUNC_SUFFIX=avx512vl_asm"
                 "ARCH_IMPL=${K}/arch/x86/keccak_f1600_x86.tpl"
                 "ASM_MACROS=${DAP_ASM_MACROS}"
                 ${_KECCAK_ASM_ENV}
    )
    target_sources(${PROJECT_NAME} PRIVATE ${KECCAK_AVX512VL_ASM})
    set_source_files_properties(${KECCAK_AVX512VL_ASM}
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl"
    )

    # AVX-512VL Keccak-f[1600] prepare-theta variant — reduced FP0/FP1 pressure
    set(KECCAK_AVX512VL_PT_ASM ${KECCAK_GEN_DIR}/dap_keccak_f1600_avx512vl_pt.S)
    dap_tpl_generate(
        TEMPLATE ${K}/dap_keccak_f1600_asm.S.tpl
        OUTPUT   ${KECCAK_AVX512VL_PT_ASM}
        ARGS     "FUNC_SUFFIX=avx512vl_pt"
                 "ARCH_IMPL=${K}/arch/x86/keccak_f1600_x86_pt.tpl"
                 "ASM_MACROS=${DAP_ASM_MACROS}"
                 ${_KECCAK_ASM_ENV}
    )
    target_sources(${PROJECT_NAME} PRIVATE ${KECCAK_AVX512VL_PT_ASM})
    set_source_files_properties(${KECCAK_AVX512VL_PT_ASM}
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512vl"
    )

    # Scalar BMI2 Keccak-f[1600] — hand-tuned ASM + C sponge wrappers
    set(KECCAK_SCALAR_BMI2_ASM  ${K}/arch/x86/keccak_f1600_scalar_bmi2.S)
    set(KECCAK_SCALAR_BMI2_C    ${K}/arch/x86/keccak_scalar_bmi2_sponge.c)
    target_sources(${PROJECT_NAME} PRIVATE ${KECCAK_SCALAR_BMI2_ASM} ${KECCAK_SCALAR_BMI2_C})
    set_source_files_properties(${KECCAK_SCALAR_BMI2_ASM}
        PROPERTIES COMPILE_FLAGS "-mbmi -mbmi2"
    )
endif()

# AArch64 SHA3 Keccak-f[1600] assembly — shared framework + ARM arch impl
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    enable_language(ASM)
    set(KECCAK_NEON_SHA3_ASM ${KECCAK_GEN_DIR}/dap_keccak_f1600_neon_sha3.S)
    file(STRINGS "${K}/arch/arm/keccak_asm_neon_sha3.env" _KECCAK_ARM_ENV)
    dap_tpl_generate(
        TEMPLATE ${K}/dap_keccak_f1600_asm.S.tpl
        OUTPUT   ${KECCAK_NEON_SHA3_ASM}
        ARGS     "ARCH_IMPL=${K}/arch/arm/keccak_f1600_arm.tpl"
                 "ASM_MACROS=${DAP_ASM_MACROS}"
                 ${_KECCAK_ARM_ENV}
    )
    target_sources(${PROJECT_NAME} PRIVATE ${KECCAK_NEON_SHA3_ASM})
    if(NOT APPLE)
        set_source_files_properties(${KECCAK_NEON_SHA3_ASM}
            PROPERTIES COMPILE_FLAGS "-march=armv8.2-a+sha3"
        )
    endif()
endif()

# Register headers for component-based installation
register_module_headers(DIRECTORIES include)

# Include directories
target_include_directories(${PROJECT_NAME} PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/keccak
    ${CMAKE_CURRENT_SOURCE_DIR}/src/sha2-256
)

# Dependencies
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_optimization)

# Add hash table submodule
add_subdirectory(table)

# Export headers for installation
if(INSTALL_DAP_SDK)
    install(FILES ${HASH_HEADERS} DESTINATION include/dap/hash/)
endif()
