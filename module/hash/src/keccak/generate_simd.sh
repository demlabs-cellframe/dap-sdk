#!/usr/bin/env bash
# Generate SIMD implementations for Keccak-p[1600] permutation
# Uses dap_tpl template engine for architecture-specific code generation

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KECCAK_DIR="${SCRIPT_DIR}"

# DAP_TPL_DIR can be provided via environment (from CMake) for flexibility
if [[ -n "${DAP_TPL_DIR:-}" ]]; then
    echo "Using DAP_TPL_DIR from environment: ${DAP_TPL_DIR}"
else
    # Default: relative path from this script
    DAP_TPL_DIR="${KECCAK_DIR}/../../../../test/dap_tpl"
fi

# Templates
TPL_PLANE="${KECCAK_DIR}/dap_keccak_simd_plane.c.tpl"
TPL_LANE="${KECCAK_DIR}/dap_keccak_simd_lane.c.tpl"

# Output directory - MUST be provided by CMake
if [[ -z "${1:-}" ]]; then
    echo "Error: OUTPUT_DIR must be provided as first argument (from CMake)" >&2
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

# Set TEMPLATES_DIR for {{#include}} to find sub-templates
export TEMPLATES_DIR="${KECCAK_DIR}"

echo "=== Generating Keccak SIMD implementations using dap_tpl ==="
echo "Output directory: ${OUTPUT_DIR}"
echo "Templates directory: ${TEMPLATES_DIR}"

# Detect target architecture
ARCH="${CMAKE_SYSTEM_PROCESSOR:-$(uname -m)}"
echo "Target architecture: ${ARCH}"

mkdir -p "${OUTPUT_DIR}"

# Helper function to generate from template
generate() {
    local template="$1"
    local output="$2"
    shift 2
    
    replace_template_placeholders "$template" "$output" "$@"
    echo "  Generated: $output"
}

# ========================================================================
# Generate based on current architecture
# ========================================================================

case "${ARCH}" in
    x86_64|amd64|AMD64|i686|i386)
        echo ""
        echo "=== Generating x86/x64 SIMD implementations ==="
        
        # AVX-512 (Plane layout - most advanced)
        echo ""
        echo "Generating AVX-512 (plane layout)..."
        generate "${TPL_PLANE}" "${OUTPUT_DIR}/dap_keccak_avx512.c" \
            "ARCH_NAME=AVX-512" \
            "ARCH_LOWER=avx512" \
            "ARCH_INCLUDES=#include <immintrin.h>" \
            "ALIGNMENT_ATTR=__attribute__((aligned(64)))" \
            "TARGET_ATTR=__attribute__((target(\"avx512f,avx512dq,avx512bw,avx512vl\")))" \
            "OPTIMIZATION_NOTES=- _mm512_ternarylogic_epi64(0xD2) for Chi: a ^ (~b & c) in single instruction
 *   - _mm512_ternarylogic_epi64(0x96) for XOR3: a ^ b ^ c in single instruction
 *   - _mm512_rolv_epi64 for Rho: variable rotation per lane
 *   - _mm512_permutexvar_epi64 for Theta/Pi permutations
 *   - Plane layout (5 lanes/register) for maximum parallelism
 *   - Full 24-round unrolling for maximum ILP" \
            "PERF_TARGET=2-4 GB/s (single-core)" \
            "PRIMITIVES=@${KECCAK_DIR}/arch/x86/avx512_primitives.tpl"
        
        # AVX2 (Lane layout)
        echo ""
        echo "Generating AVX2 (lane layout)..."
        generate "${TPL_LANE}" "${OUTPUT_DIR}/dap_keccak_avx2.c" \
            "ARCH_NAME=AVX2" \
            "ARCH_LOWER=avx2" \
            "ARCH_INCLUDES=#include <immintrin.h>" \
            "TARGET_ATTR=__attribute__((target(\"avx2\")))" \
            "OPTIMIZATION_NOTES=- 256-bit SIMD for column parity (Theta)
 *   - ANDN for Chi step (2 instructions vs 3 scalar)
 *   - Interleaved state processing" \
            "PERF_TARGET=1-2 GB/s (single-core)" \
            "PRIMITIVES=@${KECCAK_DIR}/arch/x86/avx2_primitives.tpl"
        
        # SSE2 (Lane layout - baseline)
        echo ""
        echo "Generating SSE2 (lane layout)..."
        generate "${TPL_LANE}" "${OUTPUT_DIR}/dap_keccak_sse2.c" \
            "ARCH_NAME=SSE2" \
            "ARCH_LOWER=sse2" \
            "ARCH_INCLUDES=#include <emmintrin.h>" \
            "TARGET_ATTR=__attribute__((target(\"sse2\")))" \
            "OPTIMIZATION_NOTES=- 128-bit SIMD for column parity (Theta)
 *   - Baseline x86-64 SIMD support" \
            "PERF_TARGET=500 MB/s - 1 GB/s (single-core)" \
            "PRIMITIVES=@${KECCAK_DIR}/arch/x86/sse2_primitives.tpl"
        ;;
    
    arm*|aarch64|ARM*)
        echo ""
        echo "=== Generating ARM SIMD implementations ==="
        
        # NEON (Lane layout)
        echo ""
        echo "Generating ARM NEON (lane layout)..."
        generate "${TPL_LANE}" "${OUTPUT_DIR}/dap_keccak_neon.c" \
            "ARCH_NAME=NEON" \
            "ARCH_LOWER=neon" \
            "ARCH_INCLUDES=#include <arm_neon.h>" \
            "TARGET_ATTR=" \
            "OPTIMIZATION_NOTES=- 128-bit SIMD for column parity (Theta)
 *   - BIC (bit clear) available for Chi" \
            "PERF_TARGET=500 MB/s - 1.5 GB/s (single-core)" \
            "PRIMITIVES=@${KECCAK_DIR}/arch/arm/neon_primitives.tpl"
        
        # SVE and SVE2 (AArch64 only)
        if [[ "${ARCH}" == "aarch64" ]]; then
            # SVE (Lane layout - no EOR3/BCAX)
            echo ""
            echo "Generating ARM SVE (lane layout)..."
            generate "${TPL_LANE}" "${OUTPUT_DIR}/dap_keccak_sve.c" \
                "ARCH_NAME=SVE" \
                "ARCH_LOWER=sve" \
                "ARCH_INCLUDES=#include <arm_sve.h>" \
                "TARGET_ATTR=__attribute__((target(\"+sve\")))" \
                "OPTIMIZATION_NOTES=- Scalable vectors (128-2048 bits)
 *   - Predicated operations for flexible vector lengths
 *   - EOR for XOR, BIC available for bit clear" \
                "PERF_TARGET=1-2 GB/s (single-core)" \
                "PRIMITIVES=@${KECCAK_DIR}/arch/arm/sve_primitives.tpl"
            
            # SVE2 (Plane layout - with EOR3/BCAX)
            echo ""
            echo "Generating ARM SVE2 (plane layout)..."
            generate "${TPL_PLANE}" "${OUTPUT_DIR}/dap_keccak_sve2.c" \
                "ARCH_NAME=SVE2" \
                "ARCH_LOWER=sve2" \
                "ARCH_INCLUDES=#include <arm_sve.h>" \
                "ALIGNMENT_ATTR=__attribute__((aligned(64)))" \
                "TARGET_ATTR=__attribute__((target(\"+sve2\")))" \
                "OPTIMIZATION_NOTES=- Scalable vectors (128-2048 bits)
 *   - EOR3 for 3-way XOR in single instruction
 *   - BCAX for Chi: a ^ (~b & c) in single instruction
 *   - Predicated operations for 5-lane planes" \
                "PERF_TARGET=2-4 GB/s (single-core)" \
                "PRIMITIVES=@${KECCAK_DIR}/arch/arm/sve2_primitives.tpl"
        fi
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
