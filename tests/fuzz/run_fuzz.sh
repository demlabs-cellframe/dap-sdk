#!/usr/bin/env bash
# Convenience script for running DAP crypto fuzz targets.
#
# Usage:
#   ./run_fuzz.sh <target> [duration_secs] [extra_args...]
#
# Examples:
#   ./run_fuzz.sh fuzz_chacha20_decrypt 60
#   ./run_fuzz.sh fuzz_sign_verify 300 -max_len=8192
#
# Prerequisites:
#   CC=clang cmake -DBUILD_FUZZING=ON \
#       -DCMAKE_C_FLAGS="-fsanitize=fuzzer-no-link,address" \
#       -B build .
#   cmake --build build --target <target>

set -euo pipefail

TARGET="${1:?Usage: $0 <target> [duration_secs] [extra_args...]}"
DURATION="${2:-0}"
shift 2 2>/dev/null || shift $# 2>/dev/null

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../../build/tests/fuzz/bin"
CORPUS_MAP=(
    fuzz_chacha20_decrypt:chacha20
    fuzz_aes256_decrypt:aes256
    fuzz_mlkem_decaps:mlkem
    fuzz_sign_verify:sign_verify
    fuzz_sign_deser:sign_deser
)

CORPUS_DIR=""
for entry in "${CORPUS_MAP[@]}"; do
    key="${entry%%:*}"
    val="${entry#*:}"
    if [ "$key" = "$TARGET" ]; then
        CORPUS_DIR="${SCRIPT_DIR}/corpus/${val}"
        break
    fi
done

if [ -z "$CORPUS_DIR" ]; then
    CORPUS_DIR="${SCRIPT_DIR}/corpus/${TARGET}"
    mkdir -p "$CORPUS_DIR"
fi

BINARY="${BUILD_DIR}/${TARGET}"
if [ ! -x "$BINARY" ]; then
    echo "ERROR: Binary not found: ${BINARY}"
    echo "Build first:  cmake --build build --target ${TARGET}"
    exit 1
fi

ARGS=(-max_len=4096 -print_final_stats=1)
if [ "$DURATION" -gt 0 ] 2>/dev/null; then
    ARGS+=(-max_total_time="$DURATION")
fi

echo "=== DAP Crypto Fuzzer ==="
echo "Target : ${TARGET}"
echo "Binary : ${BINARY}"
echo "Corpus : ${CORPUS_DIR}"
echo "Duration: ${DURATION}s (0 = unlimited)"
echo "========================="

exec "$BINARY" "$CORPUS_DIR" "${ARGS[@]}" "$@"
