#!/bin/bash
#
# Run all MRNG competitors and collect benchmark results.
# Requires: build.release/benchmarks/bench_chipmunk_mring (MRNG+LRS+ML-DSA)
#          competitors/lotrs/lotrs-rs/target/release/examples/bench (LoTRS)
#          competitors/ringtail/bench_ringtail (RingTAIL)
#
# Usage: ./run_competitors.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
MRNG_BENCH="/mnt/store/work/dap-sdk/build.release/benchmarks/bench_chipmunk_mring"
LOTRS_BENCH="${SCRIPT_DIR}/competitors/lotrs/lotrs-rs/target/release/examples/bench"
RINGTAIL_BENCH="${SCRIPT_DIR}/competitors/ringtail/bench_ringtail"

echo "============================================================"
echo "  MRNG Competitor Benchmark Summary"
echo "============================================================"
echo ""
echo "Platform: $(uname -s) $(uname -m)"
echo "CPU: $(grep 'model name' /proc/cpuinfo | head -1 | cut -d: -f2 | xargs)"
echo "Date: $(date '+%Y-%m-%d %H:%M')"
echo ""

# --- MRNG + LRS + ML-DSA ---
if [ -x "$MRNG_BENCH" ]; then
    echo "--- MRNG / LRS / ML-DSA (C, native) ---"
    "$MRNG_BENCH" 2>/dev/null | grep -v "^\[" | grep -v "^$" | grep -v "dap_cpu"
    echo ""
else
    echo "MRNG benchmark not found at $MRNG_BENCH"
    echo "  Build: cmake --build build.release --target bench_chipmunk_mring"
    echo ""
fi

# --- LoTRS ---
if [ -x "$LOTRS_BENCH" ]; then
    echo "--- LoTRS (Rust, lattice threshold ring) ---"
    "$LOTRS_BENCH" 2>/dev/null | grep -E "^  |^\||^$|LoTRS|parameter" | head -20
    echo ""
else
    echo "LoTRS benchmark not found."
    echo "  Build: cd competitors/lotrs/lotrs-rs && cargo build --release --examples"
    echo ""
fi

# --- RingTAIL ---
if [ -x "$RINGTAIL_BENCH" ]; then
    echo "--- RingTAIL (Go, LWE threshold ring) ---"
    "$RINGTAIL_BENCH" l 5 4 2>/dev/null | grep -E "duration|Mean|Verify|Signature|Gen" | head -10
    echo ""
else
    echo "RingTAIL benchmark not found."
    echo "  Build: cd competitors/ringtail && go build -o bench_ringtail ."
    echo ""
fi

echo "============================================================"
