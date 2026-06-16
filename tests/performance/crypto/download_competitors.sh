#!/bin/bash
#
# Download and build competitor implementations for MRNG benchmarking.
#
# Competitors:
#   liboqs — NIST ML-DSA (Dilithium) reference sizes/timings
#
# Usage: ./download_competitors.sh
# Results go to competitors/ directory.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMP_DIR="${SCRIPT_DIR}/competitors"
INSTALL_DIR="${COMP_DIR}/install"
NPROC=$(nproc 2>/dev/null || echo 4)

mkdir -p "${COMP_DIR}" "${INSTALL_DIR}"

# =============================================================================
# liboqs — Open Quantum Safe (ML-DSA / Dilithium)
# =============================================================================
echo "=== Building liboqs ==="
LIBOQS_SRC="${COMP_DIR}/liboqs"

if [ ! -d "${LIBOQS_SRC}" ]; then
    echo "  -> Cloning liboqs..."
    git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git "${LIBOQS_SRC}"
else
    echo "  -> liboqs already exists"
fi

LIBOQS_BUILD="${LIBOQS_SRC}/build"
mkdir -p "${LIBOQS_BUILD}"

cmake -S "${LIBOQS_SRC}" -B "${LIBOQS_BUILD}" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    -DBUILD_SHARED_LIBS=OFF \
    -DOQS_BUILD_ONLY_LIB=ON \
    -DOQS_USE_OPENSSL=OFF \
    -DOQS_MINIMAL_BUILD="SIG_ml_dsa_44;SIG_ml_dsa_65;SIG_ml_dsa_87"

cmake --build "${LIBOQS_BUILD}" -j "${NPROC}"
cmake --install "${LIBOQS_BUILD}"

echo ""
echo "=== liboqs installed to ${INSTALL_DIR} ==="
echo "  Headers: ${INSTALL_DIR}/include/oqs/"
echo "  Library: ${INSTALL_DIR}/lib/liboqs.a"
echo ""
echo "=== Competitors ready. Build with: ==="
echo "  cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_BENCHMARKS=ON -DBUILD_DAP_SDK_TESTS=ON .."
echo "  cmake --build . --target bench_chipmunk_mring"
