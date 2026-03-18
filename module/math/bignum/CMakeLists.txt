cmake_minimum_required(VERSION 3.12)
project(dap_math_bignum)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/PackageComponents.cmake)

file(GLOB BIGNUM_HEADERS include/*.h)
file(GLOB BIGNUM_SRCS    src/reference/*.c src/dap_ntt_dispatch.c)

# =============================================================================
# NTT SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(NTT_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/arch_gen")

dap_arch_generate(
    TEMPLATE   ${CMAKE_CURRENT_SOURCE_DIR}/src/simd/dap_ntt16_simd.c.tpl
    OUTPUT_DIR ${NTT_GEN_DIR}
    PREFIX     dap_ntt16
    VARIANTS
        avx512  "${DAP_ARCH_DIR}/primitives/x86/int16_avx512.tpl"
        avx2    "${DAP_ARCH_DIR}/primitives/x86/int16_avx2.tpl"
        neon    "${DAP_ARCH_DIR}/primitives/arm/int16_neon.tpl"
)

dap_add_library(${PROJECT_NAME} ${BIGNUM_SRCS} HEADERS ${BIGNUM_HEADERS})

dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${NTT_GEN_DIR}
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
)

target_link_libraries(${PROJECT_NAME} dap_core)

if(INSTALL_DAP_SDK)
    install(FILES ${BIGNUM_HEADERS} DESTINATION include/dap/math/bignum/)
endif()
