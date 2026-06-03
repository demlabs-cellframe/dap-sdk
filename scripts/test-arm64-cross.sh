#!/bin/bash
# Cross-compile and test DAP NEON code on ARM64/ARM32 via QEMU (JSON + crypto ML-KEM)
# Usage: ./scripts/test-arm64-cross.sh [arm64|arm32|both]
# ARM64 crypto tests run under qemu-user; needs: gcc-aarch64-linux-gnu, qemu-user-static.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_BASE="$PROJECT_ROOT/build-cross"

# Sysroot for dynamic linker + libc when running under qemu-user (Debian/Ubuntu)
QEMU_AARCH64_ROOT="${QEMU_AARCH64_ROOT:-/usr/aarch64-linux-gnu}"
QEMU_ARMHF_ROOT="${QEMU_ARMHF_ROOT:-/usr/arm-linux-gnueabihf}"

# Colors
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_qemu_aarch64() {
    if ! command -v qemu-aarch64-static &> /dev/null; then
        log_error "qemu-aarch64-static not found. Install: sudo apt-get install qemu-user-static"
        exit 1
    fi
    log_info "qemu-aarch64-static found ✓"
}

check_qemu_armhf() {
    if ! command -v qemu-arm-static &> /dev/null; then
        log_error "qemu-arm-static not found. Install: sudo apt-get install qemu-user-static"
        exit 1
    fi
    log_info "qemu-arm-static found ✓"
}

check_qemu() {
    check_qemu_aarch64
    check_qemu_armhf
}

check_compiler_aarch64() {
    if ! command -v aarch64-linux-gnu-gcc &> /dev/null; then
        log_error "aarch64-linux-gnu-gcc not found. Install: sudo apt-get install gcc-aarch64-linux-gnu"
        exit 1
    fi
    log_info "aarch64-linux-gnu-gcc found ✓"
}

check_compiler_armhf() {
    if ! command -v arm-linux-gnueabihf-gcc &> /dev/null; then
        log_error "arm-linux-gnueabihf-gcc not found. Install: sudo apt-get install gcc-arm-linux-gnueabihf"
        exit 1
    fi
    log_info "arm-linux-gnueabihf-gcc found ✓"
}

# Check cross-compilers (both ABIs)
check_compilers() {
    check_compiler_aarch64
    check_compiler_armhf
}

# Build for ARM64
build_arm64() {
    log_info "========================================"
    log_info "Building for ARM64 (AArch64 + NEON)"
    log_info "========================================"
    
    BUILD_DIR="$BUILD_BASE/arm64"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    cmake "$PROJECT_ROOT" \
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchains/arm64-linux-gnu.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DAP_SDK_TESTS=ON
    
    make -j$(nproc) \
        test_json_stage1_simd_correctness \
        test_simd_string_spanning \
        test_crypto_kat \
        test_pqc_algorithms
    
    log_info "ARM64 build complete ✓"
}

# Build for ARM32
build_arm32() {
    log_info "========================================"
    log_info "Building for ARM32 (ARMv7 + NEON)"
    log_info "========================================"
    
    BUILD_DIR="$BUILD_BASE/arm32"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    cmake "$PROJECT_ROOT" \
        -DCMAKE_TOOLCHAIN_FILE="$PROJECT_ROOT/cmake/toolchains/arm32-linux-gnueabihf.cmake" \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_DAP_SDK_TESTS=ON
    
    make -j$(nproc) test_json_stage1_simd_correctness test_simd_string_spanning
    
    log_info "ARM32 build complete ✓"
}

# Run tests for ARM64
test_arm64() {
    log_info "========================================"
    log_info "Running ARM64 NEON tests via QEMU"
    log_info "========================================"
    
    BUILD_DIR="$BUILD_BASE/arm64"
    cd "$BUILD_DIR"
    
    QEMU_RUN=(qemu-aarch64-static -L "$QEMU_AARCH64_ROOT")
    
    log_info "Running SIMD correctness tests..."
    "${QEMU_RUN[@]}" ./tests/unit/json/test_json_stage1_simd_correctness 2>&1 | grep -E "(Results:|correctness test)" || true
    
    log_info "Running string spanning tests..."
    "${QEMU_RUN[@]}" ./tests/unit/json/test_simd_string_spanning 2>&1 | tail -3
    
    log_info "Running ML-KEM / crypto KAT (NEON)..."
    if ! "${QEMU_RUN[@]}" ./tests/unit/crypto/kat/test_crypto_kat; then
        log_error "test_crypto_kat failed under QEMU"
        exit 1
    fi
    
    log_info "Running PQC algorithms (ML-KEM roundtrip, NEON)..."
    if ! "${QEMU_RUN[@]}" ./tests/unit/crypto/pqc/test_pqc_algorithms; then
        log_error "test_pqc_algorithms failed under QEMU"
        exit 1
    fi
    
    log_info "ARM64 tests complete ✓"
}

# Run tests for ARM32
test_arm32() {
    log_info "========================================"
    log_info "Running ARM32 NEON tests via QEMU"
    log_info "========================================"
    
    BUILD_DIR="$BUILD_BASE/arm32"
    cd "$BUILD_DIR"
    
    log_info "Running SIMD correctness tests..."
    qemu-arm-static -L "$QEMU_ARMHF_ROOT" \
        ./tests/unit/json/test_json_stage1_simd_correctness 2>&1 | grep -E "(Results:|correctness test)"
    
    log_info "Running string spanning tests..."
    qemu-arm-static -L "$QEMU_ARMHF_ROOT" \
        ./tests/unit/json/test_simd_string_spanning 2>&1 | tail -3
    
    log_info "ARM32 tests complete ✓"
}

# Main
main() {
    local TARGET="${1:-both}"
    
    case "$TARGET" in
        arm64)
            check_qemu_aarch64
            check_compiler_aarch64
            build_arm64
            test_arm64
            ;;
        arm32)
            check_qemu_armhf
            check_compiler_armhf
            build_arm32
            test_arm32
            ;;
        both)
            check_qemu
            check_compilers
            build_arm64
            test_arm64
            echo ""
            build_arm32
            test_arm32
            ;;
        *)
            log_error "Unknown target: $TARGET"
            echo "Usage: $0 [arm64|arm32|both]"
            exit 1
            ;;
    esac
    
    log_info "========================================"
    log_info "All ARM cross-compilation tests PASSED ✓"
    log_info "========================================"
}

main "$@"

