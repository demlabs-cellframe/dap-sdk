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

# =============================================================================
# JSON SIMD optimizations — generated via DapArchCodegen
# =============================================================================

set(SIMD_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/simd_gen")
set(S1 ${CMAKE_CURRENT_SOURCE_DIR}/src/stage1)
set(S2 ${CMAKE_CURRENT_SOURCE_DIR}/src/stage2/string)

# --- Stage 1: Structural character classification ---

# SSE2
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sse2.c  ARCH sse2
    ARGS "TARGET_ATTR=" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)"
         "VECTOR_TYPE=__m128i" "MASK_TYPE=uint16_t" "LOADU=_mm_loadu_si128"
         "SET1_EPI8=_mm_set1_epi8" "CMPEQ_EPI8=_mm_cmpeq_epi8" "OR=_mm_or_si128"
         "MOVEMASK_EPI8=_mm_movemask_epi8")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sse2.h  ARCH sse2  GUARD none
    ARGS "TARGET_ATTR=" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)"
         "VECTOR_TYPE=__m128i" "MASK_TYPE=uint16_t")

# AVX2
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_avx2.c  ARCH avx2
    ARGS "TARGET_ATTR=avx2" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)32)"
         "VECTOR_TYPE=__m256i" "MASK_TYPE=uint32_t" "LOADU=_mm256_loadu_si256"
         "SET1_EPI8=_mm256_set1_epi8" "CMPEQ_EPI8=_mm256_cmpeq_epi8" "OR=_mm256_or_si256"
         "MOVEMASK_EPI8=_mm256_movemask_epi8")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_avx2.h  ARCH avx2  GUARD none
    ARGS "TARGET_ATTR=avx2" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)32)"
         "VECTOR_TYPE=__m256i" "MASK_TYPE=uint32_t")

# AVX-512
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_avx512.c  ARCH avx512
    ARGS "TARGET_ATTR=avx512f,avx512dq,avx512bw" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)64)"
         "VECTOR_TYPE=__m512i" "MASK_TYPE=uint64_t" "LOADU=_mm512_loadu_si512"
         "SET1_EPI8=_mm512_set1_epi8" "CMPEQ_EPI8=_mm512_cmpeq_epi8_mask" "OR=_mm512_or_si512"
         "MOVEMASK_EPI8=_mm512_movemask_epi8"
         "CMPEQ_EPI8_MASK=_mm512_cmpeq_epi8_mask" "MOVEMASK_TYPE=__mmask64"
         "is_avx512=1" "USE_AVX512_MASK=1")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_avx512.h  ARCH avx512  GUARD none
    ARGS "TARGET_ATTR=avx512f,avx512dq,avx512bw" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)64)"
         "VECTOR_TYPE=__m512i" "MASK_TYPE=uint64_t"
         "is_avx512=1" "USE_AVX512_MASK=1")

# NEON
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_neon.c  ARCH neon
    ARGS "TARGET_ATTR=" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)"
         "VECTOR_TYPE=uint8x16_t" "MASK_TYPE=uint16_t" "LOADU=vld1q_u8"
         "SET1_EPI8=vdupq_n_u8" "CMPEQ_EPI8=vceqq_u8" "OR=vorrq_u8"
         "MOVEMASK_EPI8=dap_neon_movemask_u8" "USE_NEON_HELPER=1")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_neon.h  ARCH neon  GUARD none
    ARGS "TARGET_ATTR=" "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)"
         "VECTOR_TYPE=uint8x16_t" "MASK_TYPE=uint16_t" "USE_NEON_HELPER=1")
file(COPY ${S1}/arch/arm/movemask_neon.tpl DESTINATION ${SIMD_GEN_DIR})
file(RENAME ${SIMD_GEN_DIR}/movemask_neon.tpl ${SIMD_GEN_DIR}/dap_json_stage1_neon_arch.h)

# SVE
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sve.c  ARCH sve
    ARGS "TARGET_ATTR=+sve" "USE_SVE_PREDICATES=YES"
         "ARCH_INCLUDES=#include <arm_sve.h>"
         "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE svcntb()"
         "VECTOR_TYPE=svuint8_t" "MASK_TYPE=uint64_t" "LOADU=svld1_u8"
         "SET1_EPI8=svdup_u8" "CMPEQ_EPI8=svcmpeq_u8" "OR=svorr_u8_z"
         "MOVEMASK_EPI8=dap_sve_movemask_u8" "USE_SVE_HELPER=1")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sve.h  ARCH sve  GUARD none
    ARGS "TARGET_ATTR=+sve" "USE_SVE_PREDICATES=YES"
         "VECTOR_TYPE=svuint8_t" "MASK_TYPE=uint64_t" "USE_SVE_HELPER=1")
file(COPY ${S1}/arch/arm/movemask_sve.tpl DESTINATION ${SIMD_GEN_DIR})
file(RENAME ${SIMD_GEN_DIR}/movemask_sve.tpl ${SIMD_GEN_DIR}/dap_json_stage1_sve_arch.h)

# SVE2
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sve2.c  ARCH sve2
    ARGS "TARGET_ATTR=+sve2" "USE_SVE_PREDICATES=YES"
         "ARCH_INCLUDES=#include <arm_sve.h>"
         "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE svcntb()"
         "VECTOR_TYPE=svuint8_t" "MASK_TYPE=uint64_t" "LOADU=svld1_u8"
         "SET1_EPI8=svdup_u8" "CMPEQ_EPI8=svcmpeq_u8" "OR=svorr_u8_z"
         "MOVEMASK_EPI8=dap_sve2_movemask_u8" "USE_SVE2_HELPER=1")
dap_arch_generate_variant(TEMPLATE ${S1}/dap_json_stage1_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_stage1_sve2.h  ARCH sve2  GUARD none
    ARGS "TARGET_ATTR=+sve2" "USE_SVE_PREDICATES=YES"
         "VECTOR_TYPE=svuint8_t" "MASK_TYPE=uint64_t" "USE_SVE2_HELPER=1")
file(COPY ${S1}/arch/arm/movemask_sve.tpl DESTINATION ${SIMD_GEN_DIR})
file(RENAME ${SIMD_GEN_DIR}/movemask_sve.tpl ${SIMD_GEN_DIR}/dap_json_stage1_sve2_arch.h)

# --- Stage 2: String scanner ---

# Copy arch implementation fragments
file(MAKE_DIRECTORY ${SIMD_GEN_DIR}/arch/x86 ${SIMD_GEN_DIR}/arch/arm)
file(COPY ${S2}/arch/string_scanner_vector_impl.c DESTINATION ${SIMD_GEN_DIR}/arch/)
file(COPY ${S2}/arch/x86/string_scanner_avx512_impl.c DESTINATION ${SIMD_GEN_DIR}/arch/x86/)
file(COPY ${S2}/arch/arm/string_scanner_sve_impl.c DESTINATION ${SIMD_GEN_DIR}/arch/arm/)
file(COPY ${S2}/arch/arm/string_scanner_sve2_impl.c DESTINATION ${SIMD_GEN_DIR}/arch/arm/)

# Process arch-specific header templates through dap_tpl (for {{#include PRIM_LIB}})
# Flat list of pairs: (relative_path, arch_id)
set(_JSON_SCANNER_PAIRS
    "x86/string_scanner_sse2.h"    sse2
    "x86/string_scanner_avx2.h"    avx2
    "x86/string_scanner_avx512.h"  avx512
    "arm/string_scanner_neon.h"    neon
    "arm/string_scanner_sve.h"     sve
    "arm/string_scanner_sve2.h"    sve2
)
list(LENGTH _JSON_SCANNER_PAIRS _json_pairs_len)
set(_json_i 0)
while(_json_i LESS _json_pairs_len)
    list(GET _JSON_SCANNER_PAIRS ${_json_i} _arch_pair)
    math(EXPR _json_j "${_json_i} + 1")
    list(GET _JSON_SCANNER_PAIRS ${_json_j} _arch_id)
    math(EXPR _json_i "${_json_j} + 1")

    if(EXISTS "${S2}/arch/${_arch_pair}.tpl")
        set(_prim_args "")
        if(DEFINED _DAP_ARCH_PRIM_LIB_${_arch_id})
            set(_prim_args "PRIM_LIB=${_DAP_ARCH_PRIM_LIB_${_arch_id}}")
        endif()
        dap_tpl_generate(
            TEMPLATE "${S2}/arch/${_arch_pair}.tpl"
            OUTPUT   "${SIMD_GEN_DIR}/arch/${_arch_pair}"
            ARGS     ${_prim_args}
        )
    endif()
endwhile()

# SSE2 string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sse2.c  ARCH sse2
    ARGS "TARGET_ATTR=" "ARCH_UPPER=SSE2" "ARCH_FAMILY=x86"
         "CHUNK_SIZE=16" "MASK_BITS=32" "SPEEDUP=16"
         "SIMD_LOOP_IMPL=arch/string_scanner_vector_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sse2.h  ARCH sse2  GUARD none
    ARGS "ARCH_UPPER=SSE2" "ARCH_FAMILY=x86")

# AVX2 string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_avx2.c  ARCH avx2
    ARGS "TARGET_ATTR=avx2" "ARCH_UPPER=AVX2" "ARCH_FAMILY=x86"
         "CHUNK_SIZE=32" "MASK_BITS=32" "SPEEDUP=32"
         "SIMD_LOOP_IMPL=arch/string_scanner_vector_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_avx2.h  ARCH avx2  GUARD none
    ARGS "ARCH_UPPER=AVX2" "ARCH_FAMILY=x86")

# AVX-512 string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_avx512.c  ARCH avx512
    ARGS "TARGET_ATTR=avx512f,avx512bw" "ARCH_UPPER=AVX512" "ARCH_FAMILY=x86"
         "CHUNK_SIZE=64" "MASK_BITS=64" "SPEEDUP=64"
         "SIMD_LOOP_IMPL=arch/x86/string_scanner_avx512_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_avx512.h  ARCH avx512  GUARD none
    ARGS "ARCH_UPPER=AVX512" "ARCH_FAMILY=x86")

# NEON string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_neon.c  ARCH neon
    ARGS "TARGET_ATTR=" "ARCH_UPPER=NEON" "ARCH_FAMILY=arm"
         "CHUNK_SIZE=16" "MASK_BITS=32" "SPEEDUP=16"
         "SIMD_LOOP_IMPL=arch/string_scanner_vector_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_neon.h  ARCH neon  GUARD none
    ARGS "ARCH_UPPER=NEON" "ARCH_FAMILY=arm")

# SVE string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sve.c  ARCH sve
    ARGS "TARGET_ATTR=+sve" "ARCH_UPPER=SVE" "ARCH_FAMILY=arm"
         "CHUNK_SIZE=VLEN" "MASK_BITS=VLEN" "SPEEDUP=VLEN"
         "SIMD_LOOP_IMPL=arch/arm/string_scanner_sve_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sve.h  ARCH sve  GUARD none
    ARGS "ARCH_UPPER=SVE" "ARCH_FAMILY=arm")

# SVE2 string
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.c.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sve2.c  ARCH sve2
    ARGS "TARGET_ATTR=+sve2" "ARCH_UPPER=SVE2" "ARCH_FAMILY=arm"
         "CHUNK_SIZE=VLEN" "MASK_BITS=VLEN" "SPEEDUP=VLEN"
         "SIMD_LOOP_IMPL=arch/arm/string_scanner_sve2_impl.c")
dap_arch_generate_variant(TEMPLATE ${S2}/dap_json_string_simd.h.tpl
    OUTPUT ${SIMD_GEN_DIR}/dap_json_string_simd_sve2.h  ARCH sve2  GUARD none
    ARGS "ARCH_UPPER=SVE2" "ARCH_FAMILY=arm")

# Combine all sources
set(JSON_ALL_SRCS ${JSON_SRCS} ${JSON_STAGE1_SRCS} ${JSON_STAGE2_SRCS} ${JSON_TAPE_SRCS})

# Use universal DAP library macro
dap_add_library(${PROJECT_NAME} ${JSON_ALL_SRCS} HEADERS ${JSON_HEADERS} ${JSON_INTERNAL_HEADERS})

# JSON parser must correctly produce NaN/Infinity for JSON spec compliance,
# override -ffinite-math-only implied by the project-wide -ffast-math.
target_compile_options(${PROJECT_NAME} PRIVATE -fno-finite-math-only)

# Add generated SIMD sources with correct compile flags
dap_arch_add_sources(TARGET ${PROJECT_NAME} OUTPUT_DIR ${SIMD_GEN_DIR}
    SOURCES
        dap_json_stage1_sse2.c    sse2
        dap_json_stage1_avx2.c    avx2
        dap_json_stage1_avx512.c  avx512
        dap_json_stage1_neon.c    neon
        dap_json_stage1_sve.c     sve
        dap_json_stage1_sve2.c    sve2
        dap_json_string_simd_sse2.c    sse2
        dap_json_string_simd_avx2.c    avx2
        dap_json_string_simd_avx512.c  avx512
        dap_json_string_simd_neon.c    neon
        dap_json_string_simd_sve.c     sve
        dap_json_string_simd_sve2.c    sve2
)

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

# Dependencies - json depends only on core (json-c REMOVED!)
set(JSON_LIBRARIES "dap_core" CACHE INTERNAL "dap_json external dependencies")

# Link dependencies
dap_link_libraries(${PROJECT_NAME} INTERFACE dap_core dap_math)
# Installation
if(INSTALL_DAP_SDK)
    set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "${JSON_HEADERS}")
    INSTALL(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION lib/dap/json
        ARCHIVE DESTINATION lib/dap/json
        PUBLIC_HEADER DESTINATION include/dap/json/
    )
endif()

