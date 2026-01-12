#!/usr/bin/env bash
# Generate SIMD string scanner implementations for all architectures
# Using dap_tpl template system (following Stage 1 pattern)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# DAP_TPL_DIR can be provided via environment (from CMake)
if [[ -n "${DAP_TPL_DIR:-}" ]]; then
    echo "Using DAP_TPL_DIR from environment: ${DAP_TPL_DIR}"
else
    # Default: relative path from this script
    DAP_TPL_DIR="${SCRIPT_DIR}/../../../../../module/test/dap_tpl"
fi

TPL_C="${SCRIPT_DIR}/dap_json_string_simd.c.tpl"

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

echo "=== Generating SIMD string scanners using dap_tpl ==="
echo "Output directory: ${OUTPUT_DIR}"
echo "Template: ${TPL_C}"

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Helper function to generate from template
generate_arch() {
    local output_c="$1"
    shift 1
    
    mkdir -p "$(dirname "$output_c")"
    
    # Generate .c file
    replace_template_placeholders "$TPL_C" "$output_c" "$@"
    echo "  Generated: $output_c"
}

# Detect target architecture
ARCH="${CMAKE_SYSTEM_PROCESSOR:-$(uname -m)}"
echo "Target architecture: ${ARCH}"

case "${ARCH}" in
    x86_64|amd64|AMD64|i686|i386)
        echo ""
        echo "=== Generating x86/x64 SIMD implementations ==="
        
        echo "Generating SSE2..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_string_simd_sse2.c" \
            "ARCH_UPPER=SSE2" \
            "ARCH_LOWER=sse2" \
            "CHUNK_SIZE=16" \
            "SPEEDUP=16" \
            "SIMD_TYPE=__m128i" \
            "SET1_INTRINSIC=_mm_set1_epi8" \
            "LOAD_INTRINSIC=_mm_loadu_si128" \
            "CMPEQ_INTRINSIC=_mm_cmpeq_epi8" \
            "OR_INTRINSIC=_mm_or_si128" \
            "MOVEMASK_INTRINSIC=_mm_movemask_epi8"
        
        echo "Generating AVX2..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_string_simd_avx2.c" \
            "ARCH_UPPER=AVX2" \
            "ARCH_LOWER=avx2" \
            "CHUNK_SIZE=32" \
            "SPEEDUP=32" \
            "SIMD_TYPE=__m256i" \
            "SET1_INTRINSIC=_mm256_set1_epi8" \
            "LOAD_INTRINSIC=_mm256_loadu_si256" \
            "CMPEQ_INTRINSIC=_mm256_cmpeq_epi8" \
            "OR_INTRINSIC=_mm256_or_si256" \
            "MOVEMASK_INTRINSIC=_mm256_movemask_epi8"
        
        echo "Generating AVX-512..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_string_simd_avx512.c" \
            "ARCH_UPPER=AVX512" \
            "ARCH_LOWER=avx512" \
            "CHUNK_SIZE=64" \
            "SPEEDUP=64" \
            "SIMD_TYPE=__m512i" \
            "SET1_INTRINSIC=_mm512_set1_epi8" \
            "LOAD_INTRINSIC=_mm512_loadu_si512" \
            "CMPEQ_INTRINSIC=_mm512_cmpeq_epi8_mask" \
            "OR_INTRINSIC=_mm512_or_si512" \
            "MOVEMASK_INTRINSIC=_mm512_movepi8_mask"
        ;;
    
    aarch64|arm64|armv8*)
        echo ""
        echo "=== Generating ARM64/NEON SIMD implementations ==="
        
        echo "Generating ARM NEON..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_string_simd_neon.c" \
            "ARCH_UPPER=NEON" \
            "ARCH_LOWER=neon" \
            "CHUNK_SIZE=16" \
            "SPEEDUP=16"
        ;;
    
    *)
        echo "Warning: Unknown architecture '${ARCH}', skipping SIMD generation"
        echo "Only reference implementation will be available"
        ;;
esac

echo ""
echo "✅ SIMD string scanner generation complete"
echo "Output: ${OUTPUT_DIR}"
