#!/bin/bash
# Local ARM32 cross-compile and build test (no QEMU run)
# Usage: ./scripts/test-arm32-local.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-cross-arm32"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check cross-compiler
if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
    log_error "arm-linux-gnueabihf-gcc not found. Install: sudo apt-get install gcc-arm-linux-gnueabihf"
    exit 1
fi

log_info "========================================"
log_info "Building for ARM32 (ARMv7 + NEON)"
log_info "========================================"

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with ARM32 toolchain
log_info "Configuring CMake for ARM32..."
cmake "$PROJECT_ROOT" \
    -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchains/arm32-linux-gnueabihf.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DAP_SDK_TESTS=ON \
    -DBUILD_WITH_ECDSA=ON

# Build
log_info "Building (this may take a while)..."
make -j$(nproc) 2>&1 | tee "$BUILD_DIR/build.log"

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    log_info "========================================"
    log_info "ARM32 cross-compilation SUCCESSFUL ✓"
    log_info "========================================"
    log_info "Build artifacts in: $BUILD_DIR"
    log_info "Build log: $BUILD_DIR/build.log"
else
    log_error "========================================"
    log_error "ARM32 cross-compilation FAILED ✗"
    log_error "========================================"
    log_error "Check build log: $BUILD_DIR/build.log"
    exit 1
fi
