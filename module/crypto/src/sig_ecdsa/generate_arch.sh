#!/usr/bin/env bash
# ============================================================================
# generate_arch.sh - Generate architecture-specific ECDSA scalar implementations
# 
# This script generates optimized scalar multiplication implementations from
# the template file (ecdsa_scalar_mul.c.tpl) combined with architecture-specific
# primitives (arch/*_primitives.tpl).
#
# Normally called by CMake during build, but can be run manually for debugging.
#
# Usage: ./generate_arch.sh OUTPUT_DIR [ARCH]
#   OUTPUT_DIR - Directory to write generated .c files
#   ARCH       - Target architecture (default: auto-detect)
# ============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Templates
TPL_SCALAR="${SCRIPT_DIR}/ecdsa_scalar_mul.c.tpl"
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

echo "=== ECDSA Architecture-Specific Code Generation ==="
echo "Template: ${TPL_SCALAR}"
echo "Output:   ${OUTPUT_DIR}"
echo "Arch:     ${ARCH}"
echo ""

mkdir -p "${OUTPUT_DIR}"

# Simple template expansion (no dap_tpl dependency)
generate() {
    local arch_name="$1"
    local arch_lower="$2"
    local arch_includes="$3"
    local target_attr="$4"
    local primitives_file="$5"
    local output="${OUTPUT_DIR}/ecdsa_scalar_mul_${arch_lower}.c"
    
    # Read template and primitives
    local tpl_content primitives_content
    tpl_content=$(<"${TPL_SCALAR}")
    primitives_content=$(<"${primitives_file}")
    
    # Perform replacements
    tpl_content="${tpl_content//\{\{ARCH_NAME\}\}/${arch_name}}"
    tpl_content="${tpl_content//\{\{ARCH_LOWER\}\}/${arch_lower}}"
    tpl_content="${tpl_content//\{\{ARCH_INCLUDES\}\}/${arch_includes}}"
    tpl_content="${tpl_content//\{\{TARGET_ATTR\}\}/${target_attr}}"
    tpl_content="${tpl_content//\{\{PRIMITIVES\}\}/${primitives_content}}"
    tpl_content="${tpl_content//\{\{OPTIMIZATION_NOTES\}\}/See primitives for details}"
    tpl_content="${tpl_content//\{\{PERF_TARGET\}\}/Architecture-optimized}"
    
    echo "${tpl_content}" > "${output}"
    echo "  Generated: ${output##*/}"
}

# Always generate generic
echo "Generating Generic implementation..."
generate "Generic" "generic" "" "" "${ARCH_DIR}/generic_primitives.tpl"

# Architecture-specific
case "${ARCH}" in
    x86_64|amd64|AMD64)
        echo "Generating x86-64 implementations..."
        generate "x86-64 ASM" "x86_64_asm" "" "" "${ARCH_DIR}/x86/x86_64_asm_primitives.tpl"
        generate "AVX2+BMI2" "avx2_bmi2" "#include <immintrin.h>" \
            '__attribute__((target("avx2,bmi2,adx")))' "${ARCH_DIR}/x86/avx2_bmi2_primitives.tpl"
        ;;
    aarch64|arm64|ARM64)
        echo "Generating ARM64 implementations..."
        generate "ARM64 NEON" "neon" "#include <arm_neon.h>" "" "${ARCH_DIR}/arm/neon_primitives.tpl"
        ;;
    armv7*|arm)
        echo "ARM32: Using generic only (no optimizations)"
        ;;
    *)
        echo "Unknown arch '${ARCH}': Using generic only"
        ;;
esac

echo ""
echo "=== Generation complete ==="
ls -la "${OUTPUT_DIR}/"ecdsa_scalar_mul_*.c 2>/dev/null || true
