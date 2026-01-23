#!/bin/bash
# Local Android cross-compile and build test
# Usage: ./scripts/test-android-local.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build-cross-android"

# Android SDK/NDK paths
export ANDROID_HOME="${ANDROID_HOME:-$HOME/android-sdk}"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/26.1.10909125"
export ANDROID_CMAKE_TOOLCHAIN="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"

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

# Check Android NDK
if [ ! -d "$ANDROID_NDK_HOME" ]; then
    log_error "Android NDK not found at: $ANDROID_NDK_HOME"
    log_error "Expected: ~/android-sdk/ndk/26.1.10909125"
    exit 1
fi

if [ ! -f "$ANDROID_CMAKE_TOOLCHAIN" ]; then
    log_error "Android CMake toolchain not found at: $ANDROID_CMAKE_TOOLCHAIN"
    exit 1
fi

log_info "========================================"
log_info "Building for Android (ARMv7 + NEON)"
log_info "========================================"
log_info "NDK: $ANDROID_NDK_HOME"
log_info "Toolchain: $ANDROID_CMAKE_TOOLCHAIN"

# Clean and create build directory
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure with Android toolchain
log_info "Configuring CMake for Android..."
cmake "$PROJECT_ROOT" \
    -DANDROID_PLATFORM=29 \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_CMAKE_TOOLCHAIN" \
    -DANDROID_ABI=armeabi-v7a \
    -DANDROID_ARM_NEON=ON

# Build
log_info "Building (this may take a while)..."
make -j$(nproc) 2>&1 | tee "$BUILD_DIR/build.log"

if [ ${PIPESTATUS[0]} -eq 0 ]; then
    log_info "========================================"
    log_info "Android cross-compilation SUCCESSFUL ✓"
    log_info "========================================"
    log_info "Build artifacts in: $BUILD_DIR"
    log_info "Build log: $BUILD_DIR/build.log"
else
    log_error "========================================"
    log_error "Android cross-compilation FAILED ✗"
    log_error "========================================"
    log_error "Check build log: $BUILD_DIR/build.log"
    exit 1
fi
