#!/bin/bash
# Generate SIMD string scanner implementations for all architectures
# Using dap_tpl template system

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
TPL_FILE="$SCRIPT_DIR/dap_json_string_simd.c.tpl"

# Find dap_tpl
if [ -n "$DAP_TPL_DIR" ]; then
    DAP_TPL="$DAP_TPL_DIR/dap_tpl"
else
    # Try relative path from dap-sdk
    DAP_TPL="$SCRIPT_DIR/../../../../../module/test/dap_tpl/dap_tpl"
fi

if [ ! -x "$DAP_TPL" ]; then
    echo "ERROR: dap_tpl not found at $DAP_TPL"
    echo "Please set DAP_TPL_DIR or ensure dap_tpl is built"
    exit 1
fi

# Output directory (from CMake or default to build dir)
OUTPUT_DIR="${1:-$SCRIPT_DIR/../../../build/string_gen}"
mkdir -p "$OUTPUT_DIR"

echo "Generating SIMD string scanners..."
echo "Template: $TPL_FILE"
echo "Output:   $OUTPUT_DIR"
echo "dap_tpl:  $DAP_TPL"
echo ""

# Generate SSE2 (x86, 16 bytes)
echo "Generating SSE2..."
$DAP_TPL \
    -D ARCH_UPPER=SSE2 \
    -D ARCH_LOWER=sse2 \
    -D CHUNK_SIZE=16 \
    -D SPEEDUP=16 \
    -D USE_X86=true \
    -D USE_AVX512_MASK=false \
    -D SIMD_TYPE=__m128i \
    -D SET1_INTRINSIC=_mm_set1_epi8 \
    -D LOAD_INTRINSIC=_mm_loadu_si128 \
    -D CMPEQ_INTRINSIC=_mm_cmpeq_epi8 \
    -D OR_INTRINSIC=_mm_or_si128 \
    -D MOVEMASK_INTRINSIC=_mm_movemask_epi8 \
    "$TPL_FILE" \
    > "$OUTPUT_DIR/dap_json_string_simd_sse2.c"

# Generate AVX2 (x86, 32 bytes)
echo "Generating AVX2..."
$DAP_TPL \
    -D ARCH_UPPER=AVX2 \
    -D ARCH_LOWER=avx2 \
    -D CHUNK_SIZE=32 \
    -D SPEEDUP=32 \
    -D USE_X86=true \
    -D USE_AVX512_MASK=false \
    -D SIMD_TYPE=__m256i \
    -D SET1_INTRINSIC=_mm256_set1_epi8 \
    -D LOAD_INTRINSIC=_mm256_loadu_si256 \
    -D CMPEQ_INTRINSIC=_mm256_cmpeq_epi8 \
    -D OR_INTRINSIC=_mm256_or_si256 \
    -D MOVEMASK_INTRINSIC=_mm256_movemask_epi8 \
    "$TPL_FILE" \
    > "$OUTPUT_DIR/dap_json_string_simd_avx2.c"

# Generate AVX-512 (x86, 64 bytes)
echo "Generating AVX-512..."
$DAP_TPL \
    -D ARCH_UPPER=AVX512 \
    -D ARCH_LOWER=avx512 \
    -D CHUNK_SIZE=64 \
    -D SPEEDUP=64 \
    -D USE_X86=true \
    -D USE_AVX512_MASK=true \
    -D SIMD_TYPE=__m512i \
    -D SET1_INTRINSIC=_mm512_set1_epi8 \
    -D LOAD_INTRINSIC=_mm512_loadu_si512 \
    -D CMPEQ_INTRINSIC=_mm512_cmpeq_epi8_mask \
    -D OR_INTRINSIC=_mm512_or_si512 \
    -D MOVEMASK_INTRINSIC=_mm512_movepi8_mask \
    "$TPL_FILE" \
    > "$OUTPUT_DIR/dap_json_string_simd_avx512.c"

# Generate ARM NEON (16 bytes)
echo "Generating ARM NEON..."
$DAP_TPL \
    -D ARCH_UPPER=NEON \
    -D ARCH_LOWER=neon \
    -D CHUNK_SIZE=16 \
    -D SPEEDUP=16 \
    -D USE_ARM=true \
    "$TPL_FILE" \
    > "$OUTPUT_DIR/dap_json_string_simd_neon.c"

echo ""
echo "✅ Generated 4 SIMD implementations:"
echo "   - SSE2   (x86,  16 bytes)"
echo "   - AVX2   (x86,  32 bytes)"
echo "   - AVX-512 (x86, 64 bytes)"
echo "   - NEON   (ARM,  16 bytes)"
echo ""
echo "Output directory: $OUTPUT_DIR"
