#!/usr/bin/env bash
# Generate SIMD string scanner implementations for all architectures
# Using dap_tpl template system + arch-specific header fragments

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# DAP_TPL_DIR can be provided via environment (from CMake)
if [[ -n "${DAP_TPL_DIR:-}" ]]; then
    echo "Using DAP_TPL_DIR from environment: ${DAP_TPL_DIR}"
else
    # Default: relative path from this script
    DAP_TPL_DIR="${SCRIPT_DIR}/../../../test/dap_tpl"
fi

TPL_C="${SCRIPT_DIR}/dap_json_string_simd.c.tpl"
TPL_H="${SCRIPT_DIR}/dap_json_string_simd.h.tpl"

# Output directory - MUST be provided by CMake
if [[ -z "${1:-}" ]]; then
    echo "Error: OUTPUT_DIR must be provided as first argument" >&2
    echo "Usage: $0 <output_dir>" >&2
    exit 1
fi
OUTPUT_DIR="$1"

# Source dap_tpl library
if [[ ! -f "${DAP_TPL_DIR}/dap_tpl.sh" ]]; then
    echo "Error: dap_tpl not found at ${DAP_TPL_DIR}/dap_tpl.sh" >&2
    exit 1
fi

source "${DAP_TPL_DIR}/dap_tpl.sh"

echo "=== Generating SIMD string scanners ==="
echo "Output: ${OUTPUT_DIR}"
echo "Templates: ${TPL_C}, ${TPL_H}"

# Create output directories
mkdir -p "${OUTPUT_DIR}"
mkdir -p "${OUTPUT_DIR}/arch/x86"
mkdir -p "${OUTPUT_DIR}/arch/arm"

# Generate arch-specific helper headers from templates
echo "Generating arch-specific headers..."
for arch_file in "${SCRIPT_DIR}/arch/x86/"*.tpl; do
    if [[ -f "$arch_file" ]]; then
        base_name=$(basename "$arch_file" .tpl)
        output_file="${OUTPUT_DIR}/arch/x86/${base_name}"
        cp "$arch_file" "$output_file"
        echo "  Copied: ${base_name}"
    fi
done

for arch_file in "${SCRIPT_DIR}/arch/arm/"*.tpl; do
    if [[ -f "$arch_file" ]]; then
        base_name=$(basename "$arch_file" .tpl)
        output_file="${OUTPUT_DIR}/arch/arm/${base_name}"
        cp "$arch_file" "$output_file"
        echo "  Copied: ${base_name}"
    fi
done

# Helper function to generate from template
generate_arch() {
    local arch_lower="$1"
    local arch_upper="$2"
    local arch_family="$3"
    local chunk_size="$4"
    local mask_bits="$5"
    local speedup="$6"
    
    local output_c="${OUTPUT_DIR}/dap_json_string_simd_${arch_lower}.c"
    local output_h="${OUTPUT_DIR}/dap_json_string_simd_${arch_lower}.h"
    
    # Generate .c file
    replace_template_placeholders "$TPL_C" "$output_c" \
        "ARCH_LOWER=${arch_lower}" \
        "ARCH_UPPER=${arch_upper}" \
        "ARCH_FAMILY=${arch_family}" \
        "CHUNK_SIZE=${chunk_size}" \
        "MASK_BITS=${mask_bits}" \
        "SPEEDUP=${speedup}"
    
    # Generate .h file
    replace_template_placeholders "$TPL_H" "$output_h" \
        "ARCH_LOWER=${arch_lower}" \
        "ARCH_UPPER=${arch_upper}" \
        "ARCH_FAMILY=${arch_family}"
    
    echo "  ✅ ${arch_upper}: ${output_c}, ${output_h}"
}

# Detect target architecture
ARCH="${CMAKE_SYSTEM_PROCESSOR:-$(uname -m)}"
echo "Target architecture: ${ARCH}"
echo ""

case "${ARCH}" in
    x86_64|amd64|AMD64|i686|i386)
        echo "=== x86/x64 SIMD ==="
        generate_arch "sse2" "SSE2" "x86" "16" "32" "16"
        generate_arch "avx2" "AVX2" "x86" "32" "32" "32"
        generate_arch "avx512" "AVX512" "x86" "64" "64" "64"
        ;;
    
    aarch64|arm64|armv8*)
        echo "=== ARM64/NEON SIMD ==="
        generate_arch "neon" "NEON" "arm" "16" "32" "16"
        # TODO: SVE, SVE2
        ;;
    
    *)
        echo "⚠️  Unknown architecture '${ARCH}'"
        echo "Only reference implementation will be available"
        ;;
esac

echo ""
echo "✅ Generation complete"
echo "Output: ${OUTPUT_DIR}"
