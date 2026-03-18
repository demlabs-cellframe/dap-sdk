cmake_minimum_required(VERSION 3.12)
project(dap_math_bignum)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/PackageComponents.cmake)

file(GLOB BIGNUM_HEADERS include/*.h)
file(GLOB BIGNUM_SRCS    src/reference/*.c src/dap_ntt_dispatch.c)

# =============================================================================
# Type-parametric NTT kernels — generated via dap_tpl_generate
# =============================================================================

set(NTT_TPL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/templates")
set(NTT_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/ntt_gen")

dap_tpl_generate(
    TEMPLATE  ${NTT_TPL_DIR}/dap_ntt_mont.h.tpl
    OUTPUT    ${NTT_GEN_DIR}/dap_ntt32_mont.h
    ARGS      BITS=32 COEFF_T=int32_t WIDE_T=int64_t
              UCOEFF_T=uint32_t PARAMS_T=dap_ntt_params_t
              PREFIX=dap_ntt32 MIN_LEN=1)

dap_tpl_generate(
    TEMPLATE  ${NTT_TPL_DIR}/dap_ntt_mont.h.tpl
    OUTPUT    ${NTT_GEN_DIR}/dap_ntt16_mont.h
    ARGS      BITS=16 COEFF_T=int16_t WIDE_T=int32_t
              UCOEFF_T=uint16_t PARAMS_T=dap_ntt_params16_t
              PREFIX=dap_ntt16 MIN_LEN=2)

# =============================================================================
# NTT SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(NTT_ARCH_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/arch_gen")

dap_arch_generate(
    TEMPLATE   ${CMAKE_CURRENT_SOURCE_DIR}/src/simd/dap_ntt16_simd.c.tpl
    OUTPUT_DIR ${NTT_ARCH_GEN_DIR}
    PREFIX     dap_ntt16
    VARIANTS
        avx512  "${DAP_ARCH_DIR}/primitives/x86/int16_avx512.tpl"
        avx2    "${DAP_ARCH_DIR}/primitives/x86/int16_avx2.tpl"
        neon    "${DAP_ARCH_DIR}/primitives/arm/int16_neon.tpl"
)

dap_add_library(${PROJECT_NAME} ${BIGNUM_SRCS} HEADERS ${BIGNUM_HEADERS})

dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${NTT_ARCH_GEN_DIR}
    SOURCES
        dap_ntt16_avx512.c  avx512
        dap_ntt16_avx2.c    avx2
        dap_ntt16_neon.c    neon
)

register_module_headers(DIRECTORIES include)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${NTT_GEN_DIR}
)

target_link_libraries(${PROJECT_NAME} dap_core dap_optimization)

if(INSTALL_DAP_SDK)
    install(FILES ${BIGNUM_HEADERS} DESTINATION include/dap/math/bignum/)
endif()
