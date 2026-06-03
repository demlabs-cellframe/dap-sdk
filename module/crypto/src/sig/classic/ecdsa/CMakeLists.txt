# ============================================================================
# ECDSA - secp256k1 with architecture-specific optimizations
# Generated via DapArchCodegen from template + primitives
# ============================================================================

set(ECDSA_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(ECDSA_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
set(ECDSA_ARCH_DIR ${ECDSA_SRC_DIR}/arch)

# ============================================================================
# Scalar multiplication implementations
# ============================================================================

# Generic (always built — portable fallback)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_generic.c
    ARCH     generic
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/generic_primitives.tpl"
)

# x86-64 inline ASM (MULQ)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_x86_64_asm.c
    ARCH     x86_64_asm
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/x86/x86_64_asm_primitives.tpl"
)

# AVX2+BMI2 (MULX/ADCX/ADOX)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx2_bmi2.c
    ARCH     avx2_bmi2
    ARGS     "TARGET_ATTR=__attribute__((target(\"avx2,bmi2,adx\")))"
             "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/x86/avx2_bmi2_primitives.tpl"
)

# AVX-512 IFMA (VPMADD52)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx512.c
    ARCH     avx512_ifma
    ARGS     "ARCH_NAME=AVX-512" "ARCH_LOWER=avx512"
             "TARGET_ATTR=__attribute__((target(\"avx512f,avx512ifma,avx512vl\")))"
             "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/x86/avx512_primitives.tpl"
)

# ARM64 NEON (MUL/UMULH)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/arm/neon_primitives.tpl"
)

# ARM64 SVE
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_scalar_mul.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_sve.c
    ARCH     sve
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/arm/sve_primitives.tpl"
)

# ============================================================================
# Field arithmetic implementations
# ============================================================================

# Generic (always built — portable fallback)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_field.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_field_generic.c
    ARCH     generic
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/field_generic_primitives.tpl"
)

# x86-64 ASM field
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_field.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_field_x86_64_asm.c
    ARCH     x86_64_asm
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/x86/field_x86_64_asm_primitives.tpl"
)

# AVX2+BMI2 field (MULX/ADCX/ADOX)
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_field.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_field_avx2_bmi2.c
    ARCH     avx2_bmi2
    ARGS     "TARGET_ATTR=__attribute__((target(\"avx2,bmi2,adx\")))"
             "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/x86/field_avx2_bmi2_primitives.tpl"
)

# ARM64 NEON field
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_field.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_field_neon.c
    ARCH     neon
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/arm/field_neon_primitives.tpl"
)

# ARM64 SVE field
dap_arch_generate_variant(
    TEMPLATE ${ECDSA_SRC_DIR}/ecdsa_field.c.tpl
    OUTPUT   ${ECDSA_GEN_DIR}/ecdsa_field_sve.c
    ARCH     sve
    ARGS     "PRIMITIVES_FILE=${ECDSA_ARCH_DIR}/arm/field_sve_primitives.tpl"
)

# ============================================================================
# Collect sources and export to parent scope
# ============================================================================

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
    ${ECDSA_SRC_DIR}/dap_sig_ecdsa.c
)

# Generic is always included
set(ECDSA_GENERATED_SOURCES
    ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_generic.c
    ${ECDSA_GEN_DIR}/ecdsa_field_generic.c
)

if(APPLE)
    list(APPEND ECDSA_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx2_bmi2.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx512.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_neon.c
        ${ECDSA_GEN_DIR}/ecdsa_field_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_field_avx2_bmi2.c
        ${ECDSA_GEN_DIR}/ecdsa_field_neon.c
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    list(APPEND ECDSA_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx2_bmi2.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx512.c
        ${ECDSA_GEN_DIR}/ecdsa_field_x86_64_asm.c
        ${ECDSA_GEN_DIR}/ecdsa_field_avx2_bmi2.c
    )
    set_source_files_properties(
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx2_bmi2.c
        ${ECDSA_GEN_DIR}/ecdsa_field_avx2_bmi2.c
        PROPERTIES COMPILE_FLAGS "-mavx2 -mbmi -mbmi2" GENERATED TRUE
    )
    set_source_files_properties(
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_avx512.c
        PROPERTIES COMPILE_FLAGS "-mavx512f -mavx512ifma -mavx512vl" GENERATED TRUE
    )
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
    list(APPEND ECDSA_GENERATED_SOURCES
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_neon.c
        ${ECDSA_GEN_DIR}/ecdsa_scalar_mul_sve.c
        ${ECDSA_GEN_DIR}/ecdsa_field_neon.c
        ${ECDSA_GEN_DIR}/ecdsa_field_sve.c
    )
endif()

foreach(_src ${ECDSA_GENERATED_SOURCES})
    set_source_files_properties(${_src} PROPERTIES GENERATED TRUE)
endforeach()

set(ECDSA_SOURCES ${ECDSA_GENERATED_SOURCES} ${ECDSA_STATIC_SOURCES} PARENT_SCOPE)
set(ECDSA_INCLUDE_DIR ${ECDSA_SRC_DIR} PARENT_SCOPE)
set(ECDSA_GEN_INCLUDE_DIR ${ECDSA_GEN_DIR} PARENT_SCOPE)
