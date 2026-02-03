#!/usr/bin/env bash
# Download competitor Keccak/SHA3 implementations for benchmarking
# Run this script before building with -DBUILD_BENCHMARKS=ON

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/competitors"

echo "=========================================="
echo "  Downloading Keccak/SHA3 Competitors"
echo "=========================================="
echo ""

mkdir -p "${COMPETITORS_DIR}"

# =============================================================================
# XKCP - eXtended Keccak Code Package (official reference)
# =============================================================================

XKCP_DIR="${COMPETITORS_DIR}/XKCP"

if [[ -d "${XKCP_DIR}" ]]; then
    echo "[XKCP] Already exists, updating..."
    cd "${XKCP_DIR}"
    git pull --ff-only || true
    git submodule update --init --recursive
    cd "${SCRIPT_DIR}"
else
    echo "[XKCP] Cloning official XKCP repository..."
    git clone --depth 1 --recurse-submodules https://github.com/XKCP/XKCP.git "${XKCP_DIR}"
    echo "[XKCP] Cloned successfully"
fi

# =============================================================================
# tiny_sha3 - Minimal SHA3 implementation by mjosaarinen
# =============================================================================

TINY_DIR="${COMPETITORS_DIR}/tiny_sha3"

if [[ -d "${TINY_DIR}" ]]; then
    echo "[tiny_sha3] Already exists, updating..."
    cd "${TINY_DIR}"
    git pull --ff-only || true
    cd "${SCRIPT_DIR}"
else
    echo "[tiny_sha3] Cloning repository..."
    git clone --depth 1 https://github.com/mjosaarinen/tiny_sha3.git "${TINY_DIR}"
    echo "[tiny_sha3] Cloned successfully"
fi

# =============================================================================
# Summary
# =============================================================================

echo ""
echo "=========================================="
echo "  Download Complete!"
echo "=========================================="
echo ""
echo "Downloaded competitors:"
ls -la "${COMPETITORS_DIR}/"
echo ""
echo "To build with competitors:"
echo "  cd build.release"
echo "  cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON .."
echo "  make benchmark_keccak"
echo "  ./tests/performance/keccak/benchmark_keccak"
