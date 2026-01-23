#!/usr/bin/env bash
# Generate SIMD implementations for all architectures using dap_tpl
# Proper usage of dap_tpl template engine

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Save original SCRIPT_DIR before sourcing dap_tpl.sh (which overwrites it)
STAGE1_DIR="${SCRIPT_DIR}"

# DAP_TPL_DIR can be provided via environment (from CMake) for flexibility
# This allows using external dap_tpl (e.g., when dap-sdk is a submodule)
if [[ -n "${DAP_TPL_DIR:-}" ]]; then
    echo "Using DAP_TPL_DIR from environment: ${DAP_TPL_DIR}"
else
    # Default: relative path from this script
    DAP_TPL_DIR="${STAGE1_DIR}/../../../test/dap_tpl"
fi

TPL_C="${STAGE1_DIR}/dap_json_stage1_simd.c.tpl"
TPL_H="${STAGE1_DIR}/dap_json_stage1_simd.h.tpl"

# Output directory - MUST be provided by CMake, no default to avoid source tree pollution
if [[ -z "${1:-}" ]]; then
    echo "Error: OUTPUT_DIR must be provided as first argument (e.g., from CMake)" >&2
    echo "Usage: $0 <output_dir>" >&2
    echo "Example: $0 /path/to/build/module/json/simd_gen" >&2
    exit 1
fi
OUTPUT_DIR="$1"

# Source dap_tpl library
if [[ ! -f "${DAP_TPL_DIR}/dap_tpl.sh" ]]; then
    echo "Error: dap_tpl not found at ${DAP_TPL_DIR}/dap_tpl.sh" >&2
    exit 1
fi

source "${DAP_TPL_DIR}/dap_tpl.sh"

# Set TEMPLATES_DIR for {{#include}} to find sub-templates (use saved STAGE1_DIR)
export TEMPLATES_DIR="${STAGE1_DIR}"

echo "=== Generating SIMD implementations using dap_tpl ==="
echo "Output directory: ${OUTPUT_DIR}"
echo "Templates directory: ${TEMPLATES_DIR}"

# Detect target architecture
# CMAKE_SYSTEM_PROCESSOR can be passed from CMake for cross-compilation
# Otherwise, fallback to uname -m (native build)
ARCH="${CMAKE_SYSTEM_PROCESSOR:-$(uname -m)}"
echo "Target architecture: ${ARCH}"
echo "  (CMAKE_SYSTEM_PROCESSOR=${CMAKE_SYSTEM_PROCESSOR:-<not set>})"

# Create output directory
mkdir -p "${OUTPUT_DIR}"

# Helper function to generate from template
generate_arch() {
    local output_c="$1"
    local output_h="$2"
    shift 2
    
    # Create output directory if needed
    mkdir -p "$(dirname "$output_c")"
    
    # Generate .c file
    replace_template_placeholders "$TPL_C" "$output_c" "$@"
    echo "  Generated: $output_c"
    
    # Generate .h file
    replace_template_placeholders "$TPL_H" "$output_h" "$@"
    echo "  Generated: $output_h"
}

# Helper to generate arch-specific helper header
generate_arch_helpers() {
    local output_helpers_h="$1"
    local arch_tpl="$2"
    
    if [ -f "$arch_tpl" ]; then
        # Simply copy arch-specific template to output as .h file
        cp "$arch_tpl" "$output_helpers_h"
        echo "  Generated arch helpers: $output_helpers_h"
    else
        echo "  WARNING: Arch-specific template not found: $arch_tpl"
    fi
}

# ========================================================================
# Generate based on current architecture
# ========================================================================

case "${ARCH}" in
    x86_64|amd64|AMD64|i686|i386)
        echo ""
        echo "=== Generating x86/x64 SIMD implementations ==="
        echo ""
        echo "=== Generating x86/x64 SIMD implementations ==="
        
        echo "Generating SSE2..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_sse2.c" \
            "${OUTPUT_DIR}/dap_json_stage1_sse2.h" \
            "ARCH_NAME=SSE2" \
            "ARCH_LOWER=sse2" \
            "ARCH_INCLUDES=#include <emmintrin.h>  // SSE2" \
            "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)" \
            "VECTOR_TYPE=__m128i" \
            "MASK_TYPE=uint16_t" \
            "LOADU=_mm_loadu_si128" \
            "SET1_EPI8=_mm_set1_epi8" \
            "CMPEQ_EPI8=_mm_cmpeq_epi8" \
            "OR=_mm_or_si128" \
            "MOVEMASK_EPI8=_mm_movemask_epi8" \
            "PERF_TARGET=1+ GB/s (single-core)" \
            "TARGET_ATTR="
        
        echo ""
        echo "Generating AVX2..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_avx2.c" \
            "${OUTPUT_DIR}/dap_json_stage1_avx2.h" \
            "ARCH_NAME=AVX2" \
            "ARCH_LOWER=avx2" \
            "ARCH_INCLUDES=#include <immintrin.h>  // AVX2" \
            "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)32)" \
            "VECTOR_TYPE=__m256i" \
            "MASK_TYPE=uint32_t" \
            "LOADU=_mm256_loadu_si256" \
            "SET1_EPI8=_mm256_set1_epi8" \
            "CMPEQ_EPI8=_mm256_cmpeq_epi8" \
            "OR=_mm256_or_si256" \
            "MOVEMASK_EPI8=_mm256_movemask_epi8" \
            "PERF_TARGET=4-5 GB/s (single-core)" \
            "TARGET_ATTR=avx2"
        
        echo ""
        echo "Generating AVX-512..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_avx512.c" \
            "${OUTPUT_DIR}/dap_json_stage1_avx512.h" \
            "ARCH_NAME=AVX-512" \
            "ARCH_LOWER=avx512" \
            "ARCH_INCLUDES=#include <immintrin.h>  // AVX-512" \
            "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)64)" \
            "VECTOR_TYPE=__m512i" \
            "MASK_TYPE=uint64_t" \
            "LOADU=_mm512_loadu_si512" \
            "SET1_EPI8=_mm512_set1_epi8" \
            "CMPEQ_EPI8=_mm512_cmpeq_epi8_mask" \
            "CMPEQ_EPI8_MASK=_mm512_cmpeq_epi8_mask" \
            "MOVEMASK_TYPE=__mmask64" \
            "PERF_TARGET=2+ GB/s (single-core)" \
            "TARGET_ATTR=avx512f,avx512dq,avx512bw" \
            "is_avx512=1" \
            "USE_AVX512_MASK=1"
        ;;
    
    arm*|aarch64|ARM*)
        echo ""
        echo "=== Generating ARM SIMD implementations ==="
        
        echo "Generating ARM NEON..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_neon.c" \
            "${OUTPUT_DIR}/dap_json_stage1_neon.h" \
            "ARCH_NAME=NEON" \
            "ARCH_LOWER=neon" \
            "ARCH_INCLUDES=#include <arm_neon.h>  // ARM NEON" \
            "CHUNK_SIZE_MACRO=#define CHUNK_SIZE_VALUE ((size_t)16)" \
            "VECTOR_TYPE=uint8x16_t" \
            "MASK_TYPE=uint16_t" \
            "LOADU=vld1q_u8" \
            "SET1_EPI8=vdupq_n_u8" \
            "CMPEQ_EPI8=vceqq_u8" \
            "OR=vorrq_u8" \
            "MOVEMASK_EPI8=dap_neon_movemask_u8" \
            "PERF_TARGET=1+ GB/s (single-core)" \
            "TARGET_ATTR=" \
            "USE_NEON_HELPER=1"
        
        # Generate ARM-specific arch helpers header (movemask, etc)
        generate_arch_helpers \
            "${OUTPUT_DIR}/dap_json_stage1_neon_arch.h" \
            "${STAGE1_DIR}/arch/arm/movemask_neon.tpl"
        
        echo ""
        echo "Generating ARM SVE..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_sve.c" \
            "${OUTPUT_DIR}/dap_json_stage1_sve.h" \
            "ARCH_NAME=SVE" \
            "ARCH_LOWER=sve" \
            "ARCH_INCLUDES=#include <arm_sve.h>  // ARM SVE" \
            "CHUNK_SIZE_MACRO=static inline size_t get_chunk_size_sve(void) { return svcntb(); }
#define CHUNK_SIZE_VALUE get_chunk_size_sve()" \
            "USE_SVE_PREDICATES=YES" \
            "VECTOR_TYPE=svuint8_t" \
            "MASK_TYPE=uint64_t" \
            "LOADU=svld1_u8" \
            "SET1_EPI8=svdup_u8" \
            "CMPEQ_EPI8=svcmpeq_u8" \
            "OR=svorr_u8_z" \
            "MOVEMASK_EPI8=dap_sve_movemask_u8" \
            "PERF_TARGET=2+ GB/s (single-core, scalable)" \
            "TARGET_ATTR=+sve" \
            "USE_SVE_HELPER=1"
        
        # Generate SVE-specific arch helpers header
        generate_arch_helpers \
            "${OUTPUT_DIR}/dap_json_stage1_sve_arch.h" \
            "${STAGE1_DIR}/arch/arm/movemask_sve.tpl"
        
        echo ""
        echo "Generating ARM SVE2..."
        generate_arch \
            "${OUTPUT_DIR}/dap_json_stage1_sve2.c" \
            "${OUTPUT_DIR}/dap_json_stage1_sve2.h" \
            "ARCH_NAME=SVE2" \
            "ARCH_LOWER=sve2" \
            "ARCH_INCLUDES=#include <arm_sve.h>  // ARM SVE2" \
            "CHUNK_SIZE_MACRO=static inline size_t get_chunk_size_sve2(void) { return svcntb(); }
#define CHUNK_SIZE_VALUE get_chunk_size_sve2()" \
            "USE_SVE_PREDICATES=YES" \
            "VECTOR_TYPE=svuint8_t" \
            "MASK_TYPE=uint64_t" \
            "LOADU=svld1_u8" \
            "SET1_EPI8=svdup_u8" \
            "CMPEQ_EPI8=svcmpeq_u8" \
            "OR=svorr_u8_z" \
            "MOVEMASK_EPI8=dap_sve2_movemask_u8" \
            "PERF_TARGET=3+ GB/s (single-core, enhanced SVE)" \
            "TARGET_ATTR=+sve2" \
            "USE_SVE2_HELPER=1"
        
        # Generate SVE2-specific arch helpers header (same as SVE for now)
        generate_arch_helpers \
            "${OUTPUT_DIR}/dap_json_stage1_sve2_arch.h" \
            "${STAGE1_DIR}/arch/arm/movemask_sve.tpl"
        ;;
    
    *)
        echo "WARNING: Unknown architecture '${ARCH}', no SIMD implementations generated"
        echo "Only reference C implementation will be available"
        exit 0
        ;;
esac

echo ""
echo "=== Generation complete! ==="
echo ""
echo "Generated files:"
ls -lh "${OUTPUT_DIR}/"*.c 2>/dev/null | awk '{print "  " $9 " (" $5 ")"}'
echo ""
echo "Note: ARM NEON requires custom dap_neon_movemask_u8() helper function"
echo "Note: AVX-512 uses native kmask operations (_mm512_cmpeq_epi8_mask)"
