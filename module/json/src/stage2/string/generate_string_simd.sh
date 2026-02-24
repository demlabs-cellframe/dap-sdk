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

# Copy arch-specific files (headers and implementation fragments)
echo "Copying arch-specific files..."

# Copy common vector impl (used by SSE2, AVX2, NEON)
if [[ -f "${SCRIPT_DIR}/arch/string_scanner_vector_impl.c" ]]; then
    cp "${SCRIPT_DIR}/arch/string_scanner_vector_impl.c" "${OUTPUT_DIR}/arch/string_scanner_vector_impl.c"
    echo "  ✓ string_scanner_vector_impl.c"
fi

# Copy x86 specific files (.h and .c)
for arch_file in "${SCRIPT_DIR}/arch/x86/"*; do
    if [[ -f "$arch_file" ]] && [[ "$arch_file" != *.tpl ]]; then
        base_name=$(basename "$arch_file")
        cp "$arch_file" "${OUTPUT_DIR}/arch/x86/${base_name}"
        echo "  ✓ x86/${base_name}"
    elif [[ -f "$arch_file" ]] && [[ "$arch_file" == *.tpl ]]; then
        base_name=$(basename "$arch_file" .tpl)
        cp "$arch_file" "${OUTPUT_DIR}/arch/x86/${base_name}"
        echo "  ✓ x86/${base_name}"
    fi
done

# Copy ARM specific files (.h and .c)
for arch_file in "${SCRIPT_DIR}/arch/arm/"*; do
    if [[ -f "$arch_file" ]] && [[ "$arch_file" != *.tpl ]]; then
        base_name=$(basename "$arch_file")
        cp "$arch_file" "${OUTPUT_DIR}/arch/arm/${base_name}"
        echo "  ✓ arm/${base_name}"
    elif [[ -f "$arch_file" ]] && [[ "$arch_file" == *.tpl ]]; then
        base_name=$(basename "$arch_file" .tpl)
        cp "$arch_file" "${OUTPUT_DIR}/arch/arm/${base_name}"
        echo "  ✓ arm/${base_name}"
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
    local simd_loop_impl="$7"  # Path to loop implementation fragment
    
    local output_c="${OUTPUT_DIR}/dap_json_string_simd_${arch_lower}.c"
    local output_h="${OUTPUT_DIR}/dap_json_string_simd_${arch_lower}.h"
    
    # Generate .c file
    replace_template_placeholders "$TPL_C" "$output_c" \
        "ARCH_LOWER=${arch_lower}" \
        "ARCH_UPPER=${arch_upper}" \
        "ARCH_FAMILY=${arch_family}" \
        "CHUNK_SIZE=${chunk_size}" \
        "MASK_BITS=${mask_bits}" \
        "SPEEDUP=${speedup}" \
        "SIMD_LOOP_IMPL=${simd_loop_impl}"
    
    # Generate .h file
    replace_template_placeholders "$TPL_H" "$output_h" \
        "ARCH_LOWER=${arch_lower}" \
        "ARCH_UPPER=${arch_upper}" \
        "ARCH_FAMILY=${arch_family}"
    
    echo "  ✅ ${arch_upper}: ${output_c}, ${output_h}"
}

# Generate ALL architecture variants unconditionally (needed for macOS universal binaries)

wrap_arch_guard() {
    local file="$1"
    local guard="$2"
    if [[ -f "$file" ]]; then
        local tmp="${file}.tmp"
        { echo "#if ${guard}"; cat "${file}"; echo "#endif"; } > "${tmp}"
        mv "${tmp}" "${file}"
    fi
}

echo "=== x86/x64 SIMD ==="
generate_arch "sse2" "SSE2" "x86" "16" "32" "16" "arch/string_scanner_vector_impl.c"
generate_arch "avx2" "AVX2" "x86" "32" "32" "32" "arch/string_scanner_vector_impl.c"
generate_arch "avx512" "AVX512" "x86" "64" "64" "64" "arch/x86/string_scanner_avx512_impl.c"

echo ""
echo "=== ARM SIMD ==="
generate_arch "neon" "NEON" "arm" "16" "32" "16" "arch/string_scanner_vector_impl.c"
generate_arch "sve" "SVE" "arm" "VLEN" "VLEN" "VLEN" "arch/arm/string_scanner_sve_impl.c"
generate_arch "sve2" "SVE2" "arm" "VLEN" "VLEN" "VLEN" "arch/arm/string_scanner_sve2_impl.c"

X86_GUARD="defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)"
ARM_GUARD="defined(__aarch64__) || defined(__arm__)"
SVE_GUARD="defined(__aarch64__) && !defined(__APPLE__)"

wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_sse2.c"   "$X86_GUARD"
wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_avx2.c"   "$X86_GUARD"
wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_avx512.c" "$X86_GUARD"
wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_neon.c"   "$ARM_GUARD"
wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_sve.c"    "$SVE_GUARD"
wrap_arch_guard "${OUTPUT_DIR}/dap_json_string_simd_sve2.c"   "$SVE_GUARD"

echo ""
echo "✅ Generation complete"
echo "Output: ${OUTPUT_DIR}"
