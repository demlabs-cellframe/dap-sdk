#!/bin/bash
# Download competitor secp256k1 implementations for benchmarking
# Run this script before building with -DBENCHMARK_COMPETITORS=ON

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/competitors"

echo "=== Downloading secp256k1 competitor implementations ==="
echo "Target directory: ${COMPETITORS_DIR}"

mkdir -p "${COMPETITORS_DIR}"
cd "${COMPETITORS_DIR}"

# =============================================================================
# bitcoin-core/secp256k1 - The original reference implementation
# =============================================================================
echo ""
echo "[1/2] bitcoin-core/secp256k1 (reference implementation)"

if [ -d "secp256k1" ]; then
    echo "  -> Already exists, updating..."
    cd secp256k1
    git pull --quiet || true
    cd ..
else
    echo "  -> Cloning repository..."
    git clone --depth 1 https://github.com/bitcoin-core/secp256k1.git
fi

# Build secp256k1 as static library
echo "  -> Building secp256k1..."
cd secp256k1
mkdir -p build
cd build
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DSECP256K1_BUILD_TESTS=OFF \
    -DSECP256K1_BUILD_EXHAUSTIVE_TESTS=OFF \
    -DSECP256K1_BUILD_BENCHMARK=OFF \
    -DSECP256K1_BUILD_EXAMPLES=OFF \
    -DSECP256K1_ENABLE_MODULE_RECOVERY=OFF \
    -DSECP256K1_ENABLE_MODULE_ECDH=OFF \
    -DSECP256K1_ENABLE_MODULE_SCHNORRSIG=OFF \
    -DSECP256K1_ENABLE_MODULE_EXTRAKEYS=OFF \
    -DSECP256K1_ENABLE_MODULE_ELLSWIFT=OFF \
    > /dev/null 2>&1
cmake --build . --config Release -j$(nproc) > /dev/null 2>&1

# Copy library and headers for easy access
cd "${COMPETITORS_DIR}"
mkdir -p secp256k1_lib/include
cp secp256k1/include/*.h secp256k1_lib/include/
cp secp256k1/build/libsecp256k1.a secp256k1_lib/ 2>/dev/null || \
cp secp256k1/build/Release/secp256k1.lib secp256k1_lib/ 2>/dev/null || \
cp secp256k1/build/src/libsecp256k1.a secp256k1_lib/ 2>/dev/null || true

echo "  -> Done"

# =============================================================================
# OpenSSL ECDSA (use system library)
# =============================================================================
echo ""
echo "[2/2] OpenSSL ECDSA (system library)"

# Check if OpenSSL is available
if pkg-config --exists openssl 2>/dev/null; then
    OPENSSL_CFLAGS=$(pkg-config --cflags openssl)
    OPENSSL_LIBS=$(pkg-config --libs openssl)
    echo "  -> Found via pkg-config"
    echo "     CFLAGS: ${OPENSSL_CFLAGS}"
    echo "     LIBS: ${OPENSSL_LIBS}"
    
    # Create a config file for CMake
    cat > "${COMPETITORS_DIR}/openssl_config.cmake" << EOF
# OpenSSL configuration for benchmarks
set(OPENSSL_FOUND TRUE)
set(OPENSSL_CFLAGS "${OPENSSL_CFLAGS}")
set(OPENSSL_LIBS "${OPENSSL_LIBS}")
EOF
else
    echo "  -> OpenSSL not found via pkg-config, will try find_package"
    cat > "${COMPETITORS_DIR}/openssl_config.cmake" << EOF
# OpenSSL configuration for benchmarks
set(OPENSSL_FOUND FALSE)
EOF
fi

echo ""
echo "=== Download complete ==="
echo ""
echo "Competitor libraries available in: ${COMPETITORS_DIR}"
echo ""
echo "To build benchmarks with competitors:"
echo "  cmake -DBENCHMARK_COMPETITORS=ON .."
echo "  make benchmark_secp256k1"
