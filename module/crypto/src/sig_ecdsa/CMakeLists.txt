# ============================================================================
# ECDSA - secp256k1 Implementation with Architecture-Specific Optimizations
# Generates scalar multiplication implementations from template + primitives
# ============================================================================

set(ECDSA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ECDSA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)

# Create generated directory
file(MAKE_DIRECTORY ${ECDSA_GEN_DIR})

# ============================================================================
# Template and primitives files
# ============================================================================
set(ECDSA_SCALAR_MUL_TPL ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl)
set(ECDSA_GENERIC_PRIMITIVES ${ECDSA_SRC_DIR}/arch/generic_primitives.tpl)
set(ECDSA_X86_64_ASM_PRIMITIVES ${ECDSA_SRC_DIR}/arch/x86/x86_64_asm_primitives.tpl)
set(ECDSA_AVX2_BMI2_PRIMITIVES ${ECDSA_SRC_DIR}/arch/x86/avx2_bmi2_primitives.tpl)
set(ECDSA_NEON_PRIMITIVES ${ECDSA_SRC_DIR}/arch/arm/neon_primitives.tpl)

# ============================================================================
# Function to generate implementation from template
# ============================================================================
function(generate_ecdsa_scalar_impl)
    cmake_parse_arguments(GEN "" "NAME;ARCH_NAME;ARCH_LOWER;ARCH_INCLUDES;TARGET_ATTR;PRIMITIVES_FILE" "" ${ARGN})
    
    # Read template
    file(READ ${ECDSA_SCALAR_MUL_TPL} TPL_CONTENT)
    
    # Read primitives
    file(READ ${GEN_PRIMITIVES_FILE} PRIMITIVES_CONTENT)
    
    # Perform replacements
    string(REPLACE "{{ARCH_NAME}}" "${GEN_ARCH_NAME}" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{ARCH_LOWER}}" "${GEN_ARCH_LOWER}" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{ARCH_INCLUDES}}" "${GEN_ARCH_INCLUDES}" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{TARGET_ATTR}}" "${GEN_TARGET_ATTR}" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{PRIMITIVES}}" "${PRIMITIVES_CONTENT}" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{OPTIMIZATION_NOTES}}" "See primitives file for details" TPL_CONTENT "${TPL_CONTENT}")
    string(REPLACE "{{PERF_TARGET}}" "Architecture-optimized" TPL_CONTENT "${TPL_CONTENT}")
    
    # Write output
    set(OUTPUT_FILE ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_${GEN_ARCH_LOWER}.c)
    file(WRITE ${OUTPUT_FILE} "${TPL_CONTENT}")
    
    message(STATUS "[ECDSA] Generated: ecdsa_scalar_mul_${GEN_ARCH_LOWER}.c")
    
    # Export generated file path to parent scope
    set(ECDSA_GENERATED_${GEN_NAME} ${OUTPUT_FILE} PARENT_SCOPE)
endfunction()

# ============================================================================
# Generate implementations
# ============================================================================

# Generic (always generated - portable fallback)
generate_ecdsa_scalar_impl(
    NAME GENERIC
    ARCH_NAME "Generic"
    ARCH_LOWER "generic"
    ARCH_INCLUDES ""
    TARGET_ATTR ""
    PRIMITIVES_FILE ${ECDSA_GENERIC_PRIMITIVES}
)

# x86-64 implementations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    # x86-64 ASM (baseline)
    generate_ecdsa_scalar_impl(
        NAME X86_64_ASM
        ARCH_NAME "x86-64 ASM"
        ARCH_LOWER "x86_64_asm"
        ARCH_INCLUDES ""
        TARGET_ATTR ""
        PRIMITIVES_FILE ${ECDSA_X86_64_ASM_PRIMITIVES}
    )
    
    # AVX2 + BMI2 (modern CPUs)
    generate_ecdsa_scalar_impl(
        NAME AVX2_BMI2
        ARCH_NAME "AVX2+BMI2"
        ARCH_LOWER "avx2_bmi2"
        ARCH_INCLUDES "#include <immintrin.h>"
        TARGET_ATTR "__attribute__((target(\"avx2,bmi2,adx\")))"
        PRIMITIVES_FILE ${ECDSA_AVX2_BMI2_PRIMITIVES}
    )
endif()

# ARM64 implementations
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    generate_ecdsa_scalar_impl(
        NAME NEON
        ARCH_NAME "ARM64 NEON"
        ARCH_LOWER "neon"
        ARCH_INCLUDES "#include <arm_neon.h>"
        TARGET_ATTR ""
        PRIMITIVES_FILE ${ECDSA_NEON_PRIMITIVES}
    )
endif()

# ============================================================================
# Collect all ECDSA sources
# ============================================================================

# Generated arch-specific scalar implementations
set(ECDSA_GENERATED_SOURCES ${ECDSA_GENERATED_GENERIC})

if(DEFINED ECDSA_GENERATED_X86_64_ASM)
    list(APPEND ECDSA_GENERATED_SOURCES ${ECDSA_GENERATED_X86_64_ASM})
endif()
if(DEFINED ECDSA_GENERATED_AVX2_BMI2)
    list(APPEND ECDSA_GENERATED_SOURCES ${ECDSA_GENERATED_AVX2_BMI2})
endif()
if(DEFINED ECDSA_GENERATED_NEON)
    list(APPEND ECDSA_GENERATED_SOURCES ${ECDSA_GENERATED_NEON})
endif()

# Static sources (dispatcher and core ECDSA implementation)
set(ECDSA_STATIC_SOURCES
    ${ECDSA_SRC_DIR}/arch/ecdsa_scalar_dispatch.c
    ${ECDSA_SRC_DIR}/ecdsa_field.c
    ${ECDSA_SRC_DIR}/ecdsa_scalar.c
    ${ECDSA_SRC_DIR}/ecdsa_group.c
    ${ECDSA_SRC_DIR}/ecdsa_precompute.c
    ${ECDSA_SRC_DIR}/ecdsa_endomorphism.c
    ${ECDSA_SRC_DIR}/ecdsa_impl.c
)

# Export to parent scope
set(ECDSA_SOURCES ${ECDSA_GENERATED_SOURCES} ${ECDSA_STATIC_SOURCES} PARENT_SCOPE)
set(ECDSA_INCLUDE_DIR ${ECDSA_SRC_DIR} PARENT_SCOPE)
set(ECDSA_GEN_INCLUDE_DIR ${ECDSA_GEN_DIR} PARENT_SCOPE)

message(STATUS "[ECDSA] Static sources: ${ECDSA_STATIC_SOURCES}")
message(STATUS "[ECDSA] Generated sources: ${ECDSA_GENERATED_SOURCES}")
