#!/usr/bin/env bash
# ============================================================================
# generate_arch.sh - Generate architecture-specific ECDSA implementations
# 
# This script generates optimized scalar multiplication and field arithmetic
# implementations from templates combined with architecture-specific primitives.
#
# Uses dap_tpl template engine for reliable multi-line substitution.
#
# On macOS both x86_64 and ARM NEON variants are generated for universal
# (fat) binary support. Architecture guards (#if) in the generated .c files
# ensure only the right code compiles per arch slice. No SVE on Apple.
#
# Called by CMake during configuration, or manually for debugging.
#
# Usage: ./generate_arch.sh OUTPUT_DIR [ARCH]
#   OUTPUT_DIR - Directory to write generated .c files
#   ARCH       - Target architecture (default: auto-detect)
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Templates
TPL_SCALAR="${SCRIPT_DIR}/ecdsa_scalar_mul.c.tpl"
TPL_FIELD="${SCRIPT_DIR}/ecdsa_field.c.tpl"
ARCH_DIR="${SCRIPT_DIR}/arch"

# Output directory
if [[ -z "${1:-}" ]]; then
    echo "Usage: $0 OUTPUT_DIR [ARCH]" >&2
    echo "  OUTPUT_DIR - Directory for generated .c files" >&2
    echo "  ARCH       - Target: x86_64, aarch64, arm (default: auto)" >&2
    exit 1
fi
OUTPUT_DIR="$1"
ARCH="${2:-${CMAKE_SYSTEM_PROCESSOR:-$(uname -m)}}"

# Detect macOS for universal binary support
IS_DARWIN=false
[[ "$(uname -s)" == "Darwin" ]] && IS_DARWIN=true

# DAP_TPL_DIR can be provided via environment (from CMake) for flexibility
if [[ -n "${DAP_TPL_DIR:-}" ]]; then
    echo "Using DAP_TPL_DIR from environment: ${DAP_TPL_DIR}"
else
    # Default: relative path from this script
    DAP_TPL_DIR="${SCRIPT_DIR}/../../../test/dap_tpl"
fi

# Source dap_tpl library
if [[ ! -f "${DAP_TPL_DIR}/dap_tpl.sh" ]]; then
    echo "Error: dap_tpl not found at ${DAP_TPL_DIR}/dap_tpl.sh" >&2
    exit 1
fi

source "${DAP_TPL_DIR}/dap_tpl.sh"

# Set TEMPLATES_DIR for {{#include}} to find sub-templates
export TEMPLATES_DIR="${SCRIPT_DIR}"

echo "=== ECDSA Architecture-Specific Code Generation ==="
echo "Output:   ${OUTPUT_DIR}"
echo "Arch:     ${ARCH}"
echo "Darwin:   ${IS_DARWIN}"
echo ""

mkdir -p "${OUTPUT_DIR}"

# Helper function to generate from template using dap_tpl
generate() {
    local template="$1"
    local output="$2"
    shift 2

    replace_template_placeholders "$template" "$output" "$@"
    echo "  Generated: ${output##*/}"
}

# Wrap generated file with preprocessor architecture guard
wrap_arch_guard() {
    local file="$1"
    local guard="$2"
    if [[ -f "$file" ]]; then
        local tmp="${file}.tmp"
        { echo "#if ${guard}"; cat "${file}"; echo "#endif"; } > "${tmp}"
        mv "${tmp}" "${file}"
    fi
}

# Determine which variant sets to generate
GEN_X86=false
GEN_ARM=false
GEN_SVE=false

if [[ "$IS_DARWIN" == "true" ]]; then
    GEN_X86=true
    GEN_ARM=true
    # Apple Silicon does NOT support SVE
else
    case "${ARCH}" in
        x86_64|amd64|AMD64) GEN_X86=true ;;
        aarch64|arm64|ARM64) GEN_ARM=true; GEN_SVE=true ;;
        armv7*|arm) ;; # ARM32: generic only
        *) echo "  Unknown arch '${ARCH}': Using generic only" ;;
    esac
fi

# ============================================================================
# SCALAR MULTIPLICATION implementations
# ============================================================================

echo "--- Scalar Multiplication ---"

# Generic (always generated - portable fallback)
generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_generic.c" \
    "ARCH_NAME=Generic" \
    "ARCH_LOWER=generic" \
    "ARCH_INCLUDES=" \
    "TARGET_ATTR=" \
    "OPTIMIZATION_NOTES=Portable C with uint128_t support" \
    "PERF_TARGET=Baseline portable performance" \
    "PRIMITIVES_FILE=${ARCH_DIR}/generic_primitives.tpl"

if [[ "$GEN_X86" == "true" ]]; then
    echo ""
    echo "  x86/x64 scalar variants:"

    generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_x86_64_asm.c" \
        "ARCH_NAME=x86-64 ASM" \
        "ARCH_LOWER=x86_64_asm" \
        "ARCH_INCLUDES=" \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=Hand-optimized MULQ inline assembly" \
        "PERF_TARGET=Maximum single-core throughput" \
        "PRIMITIVES_FILE=${ARCH_DIR}/x86/x86_64_asm_primitives.tpl"

    generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_avx2_bmi2.c" \
        "ARCH_NAME=AVX2+BMI2" \
        "ARCH_LOWER=avx2_bmi2" \
        'ARCH_INCLUDES=#include <immintrin.h>' \
        'TARGET_ATTR=__attribute__((target("avx2,bmi2,adx")))' \
        "OPTIMIZATION_NOTES=MULX + ADCX/ADOX dual carry chains" \
        "PERF_TARGET=Modern CPU optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/x86/avx2_bmi2_primitives.tpl"

    generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_avx512.c" \
        "ARCH_NAME=AVX-512" \
        "ARCH_LOWER=avx512" \
        'ARCH_INCLUDES=#include <immintrin.h>' \
        'TARGET_ATTR=__attribute__((target("avx512f,avx512ifma,avx512vl")))' \
        "OPTIMIZATION_NOTES=AVX-512 IFMA VPMADD52 for 52-bit multiply" \
        "PERF_TARGET=Latest CPU optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/x86/avx512_primitives.tpl"
fi

if [[ "$GEN_ARM" == "true" ]]; then
    echo ""
    echo "  ARM64 scalar variants:"

    generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_neon.c" \
        "ARCH_NAME=ARM64 NEON" \
        "ARCH_LOWER=neon" \
        'ARCH_INCLUDES=#include <arm_neon.h>' \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=UMULH/MUL pair for 64x64->128" \
        "PERF_TARGET=ARM64 optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/arm/neon_primitives.tpl"
fi

if [[ "$GEN_SVE" == "true" ]]; then
    generate "${TPL_SCALAR}" "${OUTPUT_DIR}/ecdsa_scalar_mul_sve.c" \
        "ARCH_NAME=ARM64 SVE" \
        "ARCH_LOWER=sve" \
        'ARCH_INCLUDES=#include <arm_sve.h>' \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=SVE scalable vector extensions" \
        "PERF_TARGET=ARM64 server optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/arm/sve_primitives.tpl"
fi

# ============================================================================
# FIELD ARITHMETIC implementations
# ============================================================================

echo ""
echo "--- Field Arithmetic ---"

# Generic (always generated - portable fallback with interleaved reduction)
generate "${TPL_FIELD}" "${OUTPUT_DIR}/ecdsa_field_generic.c" \
    "ARCH_NAME=Generic" \
    "ARCH_LOWER=generic" \
    "ARCH_INCLUDES=" \
    "TARGET_ATTR=" \
    "OPTIMIZATION_NOTES=Interleaved multiplication/reduction (bitcoin-core style)" \
    "PERF_TARGET=Baseline portable field arithmetic" \
    "PRIMITIVES_FILE=${ARCH_DIR}/field_generic_primitives.tpl"

if [[ "$GEN_X86" == "true" ]]; then
    echo ""
    echo "  x86/x64 field variants:"

    generate "${TPL_FIELD}" "${OUTPUT_DIR}/ecdsa_field_x86_64_asm.c" \
        "ARCH_NAME=x86-64 ASM" \
        "ARCH_LOWER=x86_64_asm" \
        "ARCH_INCLUDES=" \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=Hand-optimized MULQ inline assembly for field ops" \
        "PERF_TARGET=Maximum single-core throughput" \
        "PRIMITIVES_FILE=${ARCH_DIR}/x86/field_x86_64_asm_primitives.tpl"

    generate "${TPL_FIELD}" "${OUTPUT_DIR}/ecdsa_field_avx2_bmi2.c" \
        "ARCH_NAME=AVX2+BMI2" \
        "ARCH_LOWER=avx2_bmi2" \
        'ARCH_INCLUDES=#include <immintrin.h>' \
        'TARGET_ATTR=__attribute__((target("avx2,bmi2,adx")))' \
        "OPTIMIZATION_NOTES=MULX + ADCX/ADOX for field multiplication" \
        "PERF_TARGET=Modern CPU optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/x86/field_avx2_bmi2_primitives.tpl"
    # NOTE: AVX-512 IFMA not beneficial for field ops - interleaved reduction
    # pattern doesn't parallelize well, generic __uint128_t is faster
fi

if [[ "$GEN_ARM" == "true" ]]; then
    echo ""
    echo "  ARM64 field variants:"

    generate "${TPL_FIELD}" "${OUTPUT_DIR}/ecdsa_field_neon.c" \
        "ARCH_NAME=ARM64 NEON" \
        "ARCH_LOWER=neon" \
        'ARCH_INCLUDES=#include <arm_neon.h>' \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=UMULH/MUL pair for field multiplication" \
        "PERF_TARGET=ARM64 optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/arm/field_neon_primitives.tpl"
fi

if [[ "$GEN_SVE" == "true" ]]; then
    generate "${TPL_FIELD}" "${OUTPUT_DIR}/ecdsa_field_sve.c" \
        "ARCH_NAME=ARM64 SVE" \
        "ARCH_LOWER=sve" \
        'ARCH_INCLUDES=#include <arm_sve.h>' \
        "TARGET_ATTR=" \
        "OPTIMIZATION_NOTES=SVE scalable vector field arithmetic" \
        "PERF_TARGET=ARM64 server optimized" \
        "PRIMITIVES_FILE=${ARCH_DIR}/arm/field_sve_primitives.tpl"
fi

# ============================================================================
# Apply architecture guards for macOS universal binary support.
# Each non-generic file is wrapped in #if defined(...) so it compiles to
# empty on the wrong architecture slice of a fat binary.
# ============================================================================

if [[ "$IS_DARWIN" == "true" ]]; then
    echo ""
    echo "=== Applying architecture guards (macOS universal) ==="

    X86_GUARD="defined(__x86_64__) || defined(_M_X64)"
    ARM_GUARD="defined(__aarch64__)"

    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_scalar_mul_x86_64_asm.c" "$X86_GUARD"
    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_scalar_mul_avx2_bmi2.c"  "$X86_GUARD"
    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_scalar_mul_avx512.c"     "$X86_GUARD"
    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_scalar_mul_neon.c"       "$ARM_GUARD"

    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_field_x86_64_asm.c"  "$X86_GUARD"
    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_field_avx2_bmi2.c"   "$X86_GUARD"
    wrap_arch_guard "${OUTPUT_DIR}/ecdsa_field_neon.c"        "$ARM_GUARD"
fi

echo ""
echo "=== Generation complete ==="
ls -la "${OUTPUT_DIR}/"ecdsa_scalar_mul_*.c "${OUTPUT_DIR}/"ecdsa_field_*.c 2>/dev/null || true
