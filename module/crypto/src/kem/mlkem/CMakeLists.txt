cmake_minimum_required(VERSION 3.10)

# =============================================================================
# ML-KEM SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(MLKEM_SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/arch_gen")

dap_arch_generate(
    TEMPLATE   ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_poly_simd.c.tpl
    OUTPUT_DIR ${MLKEM_SIMD_GEN_DIR}
    PREFIX     dap_mlkem_poly
    VARIANTS
        avx2    "${DAP_PRIMITIVES_DIR}/x86/int16_avx2.tpl"
        neon    "${DAP_PRIMITIVES_DIR}/arm/int16_neon.tpl"
)

# Specialized NTT16 for Kyber (Q=3329, N=256, hardcoded zetas)
dap_arch_generate(
    TEMPLATE   ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_ntt_simd.c.tpl
    OUTPUT_DIR ${MLKEM_SIMD_GEN_DIR}
    PREFIX     dap_mlkem_ntt
    VARIANTS
        avx2         "${DAP_PRIMITIVES_DIR}/x86/int16_avx2.tpl"
        avx2_512vl   "${DAP_PRIMITIVES_DIR}/x86/int16_avx2.tpl"
        neon         "${DAP_PRIMITIVES_DIR}/arm/int16_neon.tpl"
)

# =============================================================================

# Hand-written AVX2+AVX-512VL NTT assembly
set(MLKEM_ASM_NTT_SRC ${CMAKE_CURRENT_SOURCE_DIR}/asm/x86_64/dap_mlkem_ntt_avx2.S)

set(MLKEM_COMMON_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_ntt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_cbd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_poly.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_polyvec.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_indcpa.c
    ${CMAKE_CURRENT_SOURCE_DIR}/dap_mlkem_kem.c
)

set(MLKEM_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../math/bignum/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../hash/include
)

set(MLKEM_LINK_LIBS dap_core dap_hash dap_math_bignum)

# Helper: configure one ML-KEM variant library
macro(mlkem_add_variant target_name k_value)
    add_library(${target_name} STATIC ${MLKEM_COMMON_SOURCES})
    target_compile_definitions(${target_name} PRIVATE MLKEM_K=${k_value})
    target_include_directories(${target_name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(${target_name} PRIVATE ${MLKEM_INCLUDE_DIRS})
    target_link_libraries(${target_name} ${MLKEM_LINK_LIBS})

    dap_arch_add_sources(
        TARGET     ${target_name}
        OUTPUT_DIR ${MLKEM_SIMD_GEN_DIR}
        SOURCES
            dap_mlkem_poly_avx2.c       avx2
            dap_mlkem_poly_neon.c       neon
            dap_mlkem_ntt_avx2.c        avx2
            dap_mlkem_ntt_avx2_512vl.c  avx2_512vl
            dap_mlkem_ntt_neon.c        neon
    )

    # Hand-written NTT assembly (AVX2+AVX-512VL EVEX for 32 YMM registers)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(${target_name} PRIVATE ${MLKEM_ASM_NTT_SRC})
        set_source_files_properties(${MLKEM_ASM_NTT_SRC}
            PROPERTIES COMPILE_FLAGS "-mavx2 -mavx512f -mavx512vl -mavx512bw"
        )
    endif()
endmacro()

mlkem_add_variant(dap_mlkem_512  2)
mlkem_add_variant(dap_mlkem_768  3)
mlkem_add_variant(dap_mlkem_1024 4)

if(INSTALL_DAP_SDK)
    install(TARGETS dap_mlkem_512 dap_mlkem_768 dap_mlkem_1024
            LIBRARY DESTINATION lib/dap/crypto/
            ARCHIVE DESTINATION lib/dap/crypto/)
endif()
