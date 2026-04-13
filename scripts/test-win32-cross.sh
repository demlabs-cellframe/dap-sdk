#!/bin/bash
# Local Windows x86 (32-bit) cross-compile and build test
# Usage: ./scripts/test-win32-cross.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-cross-win32"

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

# Check MinGW-w64 compiler
if ! command -v i686-w64-mingw32-gcc &> /dev/null; then
    log_error "i686-w64-mingw32-gcc not found."
    log_error "Install: sudo apt-get install gcc-mingw-w64-i686 g++-mingw-w64-i686"
    exit 1
fi

log_info "========================================"
log_info "Building for Windows x86 (MinGW-w64)"
log_info "========================================"
log_info "Compiler: $(i686-w64-mingw32-gcc --version | head -1)"

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with MinGW toolchain
log_info "Configuring CMake for Windows x86..."
cmake "$PROJECT_ROOT" \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=i686 \
    -DCMAKE_C_COMPILER=i686-w64-mingw32-gcc \
    -DCMAKE_CXX_COMPILER=i686-w64-mingw32-g++ \
    -DCMAKE_RC_COMPILER=i686-w64-mingw32-windres \
    -DCMAKE_FIND_ROOT_PATH=/usr/i686-w64-mingw32 \
    -DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER \
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_DAP_SDK_TESTS=ON \
    -DBUILD_WITH_ECDSA=ON

# Build
log_info "Building (this may take a while)..."
make -j$(nproc) 2>&1 | tee "$BUILD_DIR/build.log"

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    log_info "========================================"
    log_info "Windows x86 cross-compilation SUCCESSFUL ✓"
    log_info "========================================"
    log_info "Build artifacts in: $BUILD_DIR"
    log_info "Build log: $BUILD_DIR/build.log"
    log_info ""
    log_info "Run with Wine: wine $BUILD_DIR/tests/unit/json/test_json_stage1_ref.exe"
else
    log_error "========================================"
    log_error "Windows x86 cross-compilation FAILED ✗"
    log_error "========================================"
    log_error "Check build log: $BUILD_DIR/build.log"
    exit 1
fi
