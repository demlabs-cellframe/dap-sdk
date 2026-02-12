# ============================================================================
# ECDSA - secp256k1 Implementation with Architecture-Specific Optimizations
# Generates scalar and field implementations from template + primitives
# using dap_tpl template engine (via generate_arch.sh)
# ============================================================================

set(ECDSA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ECDSA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Create generated directory
file(MAKE_DIRECTORY ${ECDSA_GEN_DIR})

# ============================================================================
# Code generation via dap_tpl (same pattern as hash/keccak, json/stage1)
# ============================================================================

set(CODEGEN_ENV
    "CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR}"
    "DAP_TPL_DIR=${CMAKE_SOURCE_DIR}/module/test/dap_tpl"
)

if(EXISTS ${ECDSA_SRC_DIR}/generate_arch.sh)
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E env ${CODEGEN_ENV}
                ${ECDSA_SRC_DIR}/generate_arch.sh ${ECDSA_GEN_DIR}
        WORKING_DIRECTORY ${ECDSA_SRC_DIR}
        RESULT_VARIABLE ECDSA_CODEGEN_RESULT
        OUTPUT_VARIABLE ECDSA_CODEGEN_OUTPUT
        ERROR_VARIABLE ECDSA_CODEGEN_ERROR
    )

    if(ECDSA_CODEGEN_RESULT)
        message(WARNING "[ECDSA] Code generation failed: ${ECDSA_CODEGEN_ERROR}")
    else()
        message(STATUS "[ECDSA] Code generation completed successfully")
    endif()
else()
    message(FATAL_ERROR "[ECDSA] generate_arch.sh not found at ${ECDSA_SRC_DIR}/generate_arch.sh")
endif()

# ============================================================================
# Collect generated sources (produced by generate_arch.sh)
# ============================================================================

# Scalar multiplication implementations
set(ECDSA_SCALAR_GENERATED_SOURCES ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_generic.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    list(APPEND ECDSA_SCALAR_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx2_bmi2.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx512.c
    )
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    list(APPEND ECDSA_SCALAR_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_neon.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_sve.c
    )
endif()

# Field arithmetic implementations
set(ECDSA_FIELD_GENERATED_SOURCES ${ECDSA_GEN_DIR}/ecdsa_field_generic.c)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    list(APPEND ECDSA_FIELD_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_field_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_field_avx2_bmi2.c
    )
    # NOTE: AVX-512 IFMA not beneficial for field ops — interleaved reduction
    # pattern doesn't parallelize well, generic __uint128_t is faster
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    list(APPEND ECDSA_FIELD_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_field_neon.c
        ${ECDSA_GEN_DIR}/ecdsa_field_sve.c
    )
endif()

# All generated sources
set(ECDSA_GENERATED_SOURCES ${ECDSA_SCALAR_GENERATED_SOURCES} ${ECDSA_FIELD_GENERATED_SOURCES})

# Static sources (dispatchers and core ECDSA implementation)
set(ECDSA_STATIC_SOURCES
    ${ECDSA_SRC_DIR}/arch/ecdsa_scalar_dispatch.c
    ${ECDSA_SRC_DIR}/arch/ecdsa_field_dispatch.c
    ${ECDSA_SRC_DIR}/ecdsa_field_ref.c
    ${ECDSA_SRC_DIR}/ecdsa_scalar.c
    ${ECDSA_SRC_DIR}/ecdsa_group.c
    ${ECDSA_SRC_DIR}/ecdsa_precompute.c
    ${ECDSA_SRC_DIR}/ecdsa_precomputed_ecmult.c
    ${ECDSA_SRC_DIR}/ecdsa_endomorphism.c
    ${ECDSA_SRC_DIR}/ecdsa_impl.c
)

# Export to parent scope
set(ECDSA_SOURCES ${ECDSA_GENERATED_SOURCES} ${ECDSA_STATIC_SOURCES} PARENT_SCOPE)
set(ECDSA_INCLUDE_DIR ${ECDSA_SRC_DIR} PARENT_SCOPE)
set(ECDSA_GEN_INCLUDE_DIR ${ECDSA_GEN_DIR} PARENT_SCOPE)

message(STATUS "[ECDSA] Static sources: ${ECDSA_STATIC_SOURCES}")
message(STATUS "[ECDSA] Generated scalar sources: ${ECDSA_SCALAR_GENERATED_SOURCES}")
message(STATUS "[ECDSA] Generated field sources: ${ECDSA_FIELD_GENERATED_SOURCES}")
message(STATUS "[ECDSA] Native secp256k1 with arch-optimized scalar arithmetic")
