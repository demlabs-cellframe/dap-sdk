cmake_minimum_required(VERSION 3.10)

# =============================================================================
# ML-KEM SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(MLKEM_SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/arch_gen")
set(M ${CMAKE_CURRENT_SOURCE_DIR})

# Shared ML-KEM reduction primitives (fqmul, Barrett) included by both templates
set(MLKEM_REDUCE_TPL "${M}/dap_mlkem_simd_reduce.tpl")

dap_arch_generate(
    TEMPLATE   ${M}/dap_mlkem_poly_simd.c.tpl
    OUTPUT_DIR ${MLKEM_SIMD_GEN_DIR}
    PREFIX     dap_mlkem_poly
    VARIANTS
        avx2    "${DAP_PRIMITIVES_DIR}/x86/int16_avx2.tpl"
        neon    "${DAP_PRIMITIVES_DIR}/arm/int16_neon.tpl"
    EXTRA_ARGS "REDUCE_FILE=${MLKEM_REDUCE_TPL}"
)

# Specialized NTT16 for Kyber (Q=3329, N=256, hardcoded zetas)
# Per-variant generation: each arch gets its own NTT_INNER_FILE (Keccak pattern)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_ntt_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_ntt_avx2.c
    ARCH     avx2
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/x86/int16_avx2.tpl"
             "REDUCE_FILE=${MLKEM_REDUCE_TPL}"
             "NTT_INNER_FILE=${M}/arch/x86/ntt_inner_avx2.tpl"
)
# avx2_512vl NTT removed — not dispatched, ASM NTT used at AVX-512 level instead
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_ntt_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_ntt_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${DAP_PRIMITIVES_DIR}/arm/int16_neon.tpl"
             "REDUCE_FILE=${MLKEM_REDUCE_TPL}"
             "NTT_INNER_FILE=${M}/arch/generic/ntt_inner_scalar.tpl"
)

# Master SIMD header: declarations + static fast bodies + dispatch — all generated
dap_tpl_generate(
    TEMPLATE ${M}/dap_mlkem_poly_simd.h.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_poly_simd.h
    ARGS     "AVX2_FAST_BODIES=${M}/arch/x86/poly_fast_avx2.inc"
             "NEON_FAST_BODIES=${M}/arch/arm/poly_fast_neon.inc"
)

# Poly I/O operations (compress, decompress, tobytes, frombytes, msg, mulcache)
# Per-variant: arch-specific IMPL_FILE with full implementations
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_poly_ops_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_poly_ops_avx2.c
    ARCH     avx2
    ARGS     "IMPL_FILE=${M}/arch/x86/poly_ops_avx2.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_poly_ops_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_poly_ops_neon.c
    ARCH     neon
    ARGS     "IMPL_FILE=${M}/arch/arm/poly_ops_neon.tpl"
)

# Polyvec SIMD operations (compress d10/d11, decompress, basemul_acc_cached)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_polyvec_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_polyvec_avx2.c
    ARCH     avx2
    ARGS     "IMPL_FILE=${M}/arch/x86/polyvec_avx2.tpl"
)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_polyvec_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_polyvec_neon.c
    ARCH     neon
    ARGS     "IMPL_FILE=${M}/arch/arm/polyvec_neon.tpl"
)

# Rejection sampling (rej_uniform) — per-arch with vendor-aware dispatch
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_rej_uniform_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_rej_uniform_generic.c
    ARCH     generic
    ARGS     "SAMPLE_BODY=${M}/arch/generic/rej_uniform_scalar.inc"
)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_rej_uniform_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_rej_uniform_avx2.c
    ARCH     avx2
    ARGS     "SAMPLE_BODY=${M}/arch/x86/rej_uniform_avx2.inc"
)
dap_arch_generate_variant(
    TEMPLATE ${M}/dap_mlkem_rej_uniform_simd.c.tpl
    OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_rej_uniform_avx512_vbmi2.c
    ARCH     avx512_vbmi2
    ARGS     "SAMPLE_BODY=${M}/arch/x86/rej_uniform_avx512.inc"
)

# =============================================================================

# AVX2+AVX-512VL NTT assembly — shared framework + x86 arch impl
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    enable_language(ASM)
    dap_tpl_generate(
        TEMPLATE ${M}/dap_mlkem_ntt_asm.S.tpl
        OUTPUT   ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_ntt_asm.S
        ARGS     "ARCH_IMPL=${M}/arch/x86/mlkem_ntt_asm_x86.tpl"
                 "ASM_MACROS=${DAP_ASM_MACROS}"
    )
    set(MLKEM_ASM_NTT_SRC ${MLKEM_SIMD_GEN_DIR}/dap_mlkem_ntt_asm.S)
endif()

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
    ${MLKEM_SIMD_GEN_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../crypto/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../math/bignum/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../hash/include
)

set(MLKEM_LINK_LIBS dap_core dap_math dap_hash dap_math_bignum)

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
            dap_mlkem_poly_avx2.c        avx2
            dap_mlkem_poly_neon.c        neon
            dap_mlkem_poly_ops_avx2.c    avx2
            dap_mlkem_poly_ops_neon.c    neon
            dap_mlkem_polyvec_avx2.c     avx2
            dap_mlkem_polyvec_neon.c     neon
            dap_mlkem_ntt_avx2.c         avx2
            dap_mlkem_ntt_neon.c         neon
            dap_mlkem_rej_uniform_generic.c    generic
            dap_mlkem_rej_uniform_avx2.c       avx2
            dap_mlkem_rej_uniform_avx512_vbmi2.c  avx512_vbmi2
    )

    # NTT assembly (AVX2+AVX-512VL EVEX, 32 YMM registers, generated from .S.tpl)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND MLKEM_ASM_NTT_SRC)
        target_sources(${target_name} PRIVATE ${MLKEM_ASM_NTT_SRC})
        set_source_files_properties(${MLKEM_ASM_NTT_SRC}
            PROPERTIES COMPILE_FLAGS "-mavx2 -mavx512f -mavx512vl -mavx512bw"
        )
    endif()

    # Unity build: merge common .c into one TU for cross-file inlining.
    # SIMD files excluded — they need per-file compile flags.
    set_target_properties(${target_name} PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE 0
    )
endmacro()

mlkem_add_variant(dap_mlkem_512  2)
mlkem_add_variant(dap_mlkem_768  3)
mlkem_add_variant(dap_mlkem_1024 4)

if(INSTALL_DAP_SDK)
    install(TARGETS dap_mlkem_512 dap_mlkem_768 dap_mlkem_1024
            LIBRARY DESTINATION lib/dap/crypto/
            ARCHIVE DESTINATION lib/dap/crypto/)
endif()
