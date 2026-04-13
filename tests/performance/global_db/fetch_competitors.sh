#!/bin/bash
#
# Fetch and build competitor KV-storage libraries for benchmarking
#
# Usage:
#   ./fetch_competitors.sh [--all|--mdbx|--lmdb|--rocksdb|--leveldb|--tidesdb|--wiredtiger|--sophia]
#
# This script downloads source code of competitor libraries and builds them
# locally for benchmarking purposes. Libraries are placed in ./competitors/
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/competitors"
BUILD_DIR="${COMPETITORS_DIR}/build"
INSTALL_DIR="${COMPETITORS_DIR}/install"

# Versions (latest stable)
MDBX_VERSION="0.12.10"
LMDB_VERSION="0.9.31"
ROCKSDB_VERSION="9.0.0"
LEVELDB_VERSION="1.23"
TIDESDB_TAG="master"
WIREDTIGER_TAG="develop"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
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

mkdir -p "${COMPETITORS_DIR}"
mkdir -p "${BUILD_DIR}"
mkdir -p "${INSTALL_DIR}"

# ============================================================================
# MDBX
# ============================================================================

fetch_mdbx() {
    log_info "Fetching MDBX v${MDBX_VERSION}..."
    
    local src_dir="${COMPETITORS_DIR}/libmdbx"
    
    if [ -d "${src_dir}" ]; then
        log_info "MDBX source already exists, updating..."
        cd "${src_dir}"
        git fetch --tags
        git checkout "v${MDBX_VERSION}" 2>/dev/null || git checkout "master"
    else
        git clone https://gitflic.ru/project/erthink/libmdbx.git "${src_dir}" || \
        git clone https://github.com/erthink/libmdbx.git "${src_dir}"
        cd "${src_dir}"
        git checkout "v${MDBX_VERSION}" 2>/dev/null || true
    fi
    
    log_info "Building MDBX..."
    mkdir -p "${BUILD_DIR}/mdbx"
    cd "${BUILD_DIR}/mdbx"
    
    cmake "${src_dir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DMDBX_BUILD_TOOLS=OFF \
        -DMDBX_BUILD_SHARED_LIBRARY=ON
    
    make -j$(nproc)
    make install
    
    log_info "MDBX installed to ${INSTALL_DIR}"
}

# ============================================================================
# LMDB
# ============================================================================

fetch_lmdb() {
    log_info "Fetching LMDB v${LMDB_VERSION}..."
    
    local src_dir="${COMPETITORS_DIR}/lmdb"
    
    if [ -d "${src_dir}" ]; then
        log_info "LMDB source already exists, updating..."
        cd "${src_dir}"
        git fetch --tags
        git checkout "LMDB_${LMDB_VERSION}" 2>/dev/null || git checkout "master"
    else
        git clone https://github.com/LMDB/lmdb.git "${src_dir}"
        cd "${src_dir}"
        git checkout "LMDB_${LMDB_VERSION}" 2>/dev/null || true
    fi
    
    log_info "Building LMDB..."
    cd "${src_dir}/libraries/liblmdb"
    
    make clean 2>/dev/null || true
    make -j$(nproc)
    
    # Manual install
    mkdir -p "${INSTALL_DIR}/lib" "${INSTALL_DIR}/include"
    cp liblmdb.so "${INSTALL_DIR}/lib/" 2>/dev/null || cp liblmdb.a "${INSTALL_DIR}/lib/"
    cp lmdb.h "${INSTALL_DIR}/include/"
    
    log_info "LMDB installed to ${INSTALL_DIR}"
}

# ============================================================================
# RocksDB
# ============================================================================

fetch_rocksdb() {
    log_info "Fetching RocksDB v${ROCKSDB_VERSION}..."
    
    local src_dir="${COMPETITORS_DIR}/rocksdb"
    
    if [ -d "${src_dir}" ]; then
        log_info "RocksDB source already exists, updating..."
        cd "${src_dir}"
        git fetch --tags
        git checkout "v${ROCKSDB_VERSION}" 2>/dev/null || git checkout "main"
    else
        git clone https://github.com/facebook/rocksdb.git "${src_dir}"
        cd "${src_dir}"
        git checkout "v${ROCKSDB_VERSION}" 2>/dev/null || true
    fi
    
    log_info "Building RocksDB (this may take a while)..."
    mkdir -p "${BUILD_DIR}/rocksdb"
    cd "${BUILD_DIR}/rocksdb"
    
    cmake "${src_dir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DWITH_BENCHMARK_TOOLS=OFF \
        -DWITH_TESTS=OFF \
        -DWITH_TOOLS=OFF \
        -DWITH_GFLAGS=OFF \
        -DPORTABLE=ON \
        -DROCKSDB_BUILD_SHARED=ON
    
    make -j$(nproc) rocksdb
    make install
    
    log_info "RocksDB installed to ${INSTALL_DIR}"
}

# ============================================================================
# LevelDB
# ============================================================================

fetch_leveldb() {
    log_info "Fetching LevelDB v${LEVELDB_VERSION}..."
    
    local src_dir="${COMPETITORS_DIR}/leveldb"
    
    if [ -d "${src_dir}" ]; then
        log_info "LevelDB source already exists, updating..."
        cd "${src_dir}"
        git fetch --tags
        git checkout "${LEVELDB_VERSION}" 2>/dev/null || git checkout "main"
    else
        git clone https://github.com/google/leveldb.git "${src_dir}"
        cd "${src_dir}"
        git submodule update --init
        git checkout "${LEVELDB_VERSION}" 2>/dev/null || true
    fi
    
    log_info "Building LevelDB..."
    mkdir -p "${BUILD_DIR}/leveldb"
    cd "${BUILD_DIR}/leveldb"
    
    cmake "${src_dir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DLEVELDB_BUILD_TESTS=OFF \
        -DLEVELDB_BUILD_BENCHMARKS=OFF \
        -DBUILD_SHARED_LIBS=ON
    
    make -j$(nproc)
    make install
    
    log_info "LevelDB installed to ${INSTALL_DIR}"
}

# ============================================================================
# TidesDB
# ============================================================================

fetch_tidesdb() {
    log_info "Fetching TidesDB ${TIDESDB_TAG}..."

    # Check system dependencies (pkg-config names differ from package names)
    local missing=""
    local -A pkgmap=([libzstd]=libzstd-dev [liblz4]=liblz4-dev [snappy]=libsnappy-dev)
    for lib in libzstd liblz4 snappy; do
        if ! pkg-config --exists "${lib}" 2>/dev/null; then
            missing="${missing} ${pkgmap[$lib]}"
        fi
    done
    if [ -n "${missing}" ]; then
        log_warn "TidesDB requires system packages:${missing}"
        log_warn "Install with: sudo apt install${missing}"
        log_error "Skipping TidesDB build."
        return 1
    fi

    local src_dir="${COMPETITORS_DIR}/tidesdb"

    if [ -d "${src_dir}" ]; then
        log_info "TidesDB source already exists, updating..."
        cd "${src_dir}"
        git fetch --tags
        git checkout "${TIDESDB_TAG}" 2>/dev/null || git checkout "main"
    else
        git clone https://github.com/tidesdb/tidesdb.git "${src_dir}"
        cd "${src_dir}"
        git checkout "${TIDESDB_TAG}" 2>/dev/null || true
    fi

    log_info "Building TidesDB..."
    mkdir -p "${BUILD_DIR}/tidesdb"
    cd "${BUILD_DIR}/tidesdb"

    cmake "${src_dir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DBUILD_SHARED_LIBS=ON

    make -j$(nproc)
    make install 2>/dev/null || {
        # TidesDB may not have install target — manual copy
        log_info "Manual install for TidesDB..."
        mkdir -p "${INSTALL_DIR}/lib" "${INSTALL_DIR}/include"
        find "${BUILD_DIR}/tidesdb" -name "libtidesdb*.so*" -exec cp -a {} "${INSTALL_DIR}/lib/" \;
        find "${BUILD_DIR}/tidesdb" -name "libtidesdb*.a" -exec cp -a {} "${INSTALL_DIR}/lib/" \;
        # Use db.h (FFI-safe, self-contained) as the public header
        cp "${src_dir}/src/db.h" "${INSTALL_DIR}/include/tidesdb.h"
    }

    log_info "TidesDB installed to ${INSTALL_DIR}"
}

# ============================================================================
# WiredTiger
# ============================================================================

fetch_wiredtiger() {
    log_info "Fetching WiredTiger (${WIREDTIGER_TAG})..."

    local src_dir="${COMPETITORS_DIR}/wiredtiger"

    if [ -d "${src_dir}" ]; then
        log_info "WiredTiger source already exists, updating..."
        cd "${src_dir}"
        git fetch
        git checkout "${WIREDTIGER_TAG}" 2>/dev/null || git checkout "develop"
    else
        git clone --depth 1 https://github.com/wiredtiger/wiredtiger.git "${src_dir}"
        cd "${src_dir}"
    fi

    log_info "Building WiredTiger..."
    mkdir -p "${BUILD_DIR}/wiredtiger"
    cd "${BUILD_DIR}/wiredtiger"

    cmake "${src_dir}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DENABLE_STATIC=0 \
        -DENABLE_SHARED=1 \
        -DHAVE_DIAGNOSTIC=0 \
        -DENABLE_PYTHON=0 \
        -DENABLE_STRICT=0 \
        -DWITH_PYTHON_LANG=0

    make -j$(nproc)
    make install

    log_info "WiredTiger installed to ${INSTALL_DIR}"
}

# ============================================================================
# Sophia
# ============================================================================

fetch_sophia() {
    log_info "Fetching Sophia..."

    local src_dir="${COMPETITORS_DIR}/sophia"

    if [ -d "${src_dir}" ]; then
        log_info "Sophia source already exists."
        cd "${src_dir}"
    else
        git clone https://github.com/pmwkaa/sophia.git "${src_dir}"
        cd "${src_dir}"
    fi

    log_info "Building Sophia..."
    make clean 2>/dev/null || true
    make -j$(nproc) 2>/dev/null || {
        log_warn "Sophia build failed (abandoned project, may need patches). Skipping."
        return 1
    }

    # Manual install — Sophia builds in the repo root directory
    mkdir -p "${INSTALL_DIR}/lib" "${INSTALL_DIR}/include"
    cp "${src_dir}/libsophia.so" "${INSTALL_DIR}/lib/" 2>/dev/null || \
    cp "${src_dir}/libsophia.a" "${INSTALL_DIR}/lib/" 2>/dev/null || true
    cp "${src_dir}/sophia.h" "${INSTALL_DIR}/include/" 2>/dev/null || true

    log_info "Sophia installed to ${INSTALL_DIR}"
}

# ============================================================================
# Generate CMake toolchain file
# ============================================================================

generate_cmake_config() {
    log_info "Generating CMake configuration..."
    
    cat > "${COMPETITORS_DIR}/competitors.cmake" << EOF
# Auto-generated CMake configuration for competitor libraries
# Include this file in your CMakeLists.txt to find competitors

set(COMPETITORS_INSTALL_DIR "${INSTALL_DIR}")

# Add to search paths
list(APPEND CMAKE_PREFIX_PATH "\${COMPETITORS_INSTALL_DIR}")
list(APPEND CMAKE_LIBRARY_PATH "\${COMPETITORS_INSTALL_DIR}/lib")
list(APPEND CMAKE_INCLUDE_PATH "\${COMPETITORS_INSTALL_DIR}/include")

# Set hints for find_library
set(MDBX_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(LMDB_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(ROCKSDB_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(LEVELDB_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(TIDESDB_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(WIREDTIGER_ROOT "\${COMPETITORS_INSTALL_DIR}")
set(SOPHIA_ROOT "\${COMPETITORS_INSTALL_DIR}")

# PKG_CONFIG path
set(ENV{PKG_CONFIG_PATH} "\${COMPETITORS_INSTALL_DIR}/lib/pkgconfig:\$ENV{PKG_CONFIG_PATH}")

message(STATUS "Competitor libraries configured from: \${COMPETITORS_INSTALL_DIR}")
EOF

    log_info "CMake config written to ${COMPETITORS_DIR}/competitors.cmake"
}

# ============================================================================
# Usage
# ============================================================================

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --all         Fetch and build all competitors"
    echo "  --mdbx        Fetch and build MDBX only"
    echo "  --lmdb        Fetch and build LMDB only"
    echo "  --rocksdb     Fetch and build RocksDB only"
    echo "  --leveldb     Fetch and build LevelDB only"
    echo "  --tidesdb     Fetch and build TidesDB only (requires libzstd-dev liblz4-dev libsnappy-dev)"
    echo "  --wiredtiger  Fetch and build WiredTiger only"
    echo "  --sophia      Fetch and build Sophia only (abandoned, may fail)"
    echo "  --clean       Remove all downloaded sources and builds"
    echo "  -h, --help    Show this help"
    echo ""
    echo "After building, configure CMake with:"
    echo "  cmake -DCMAKE_PREFIX_PATH=${INSTALL_DIR} ..."
    echo ""
    echo "Or include in CMakeLists.txt:"
    echo "  include(${COMPETITORS_DIR}/competitors.cmake)"
}

# ============================================================================
# Main
# ============================================================================

if [ $# -eq 0 ]; then
    print_usage
    exit 0
fi

for arg in "$@"; do
    case $arg in
        --all)
            fetch_mdbx
            fetch_lmdb
            fetch_rocksdb
            fetch_leveldb
            fetch_tidesdb || true
            fetch_wiredtiger || true
            fetch_sophia || true
            generate_cmake_config
            ;;
        --mdbx)
            fetch_mdbx
            generate_cmake_config
            ;;
        --lmdb)
            fetch_lmdb
            generate_cmake_config
            ;;
        --rocksdb)
            fetch_rocksdb
            generate_cmake_config
            ;;
        --leveldb)
            fetch_leveldb
            generate_cmake_config
            ;;
        --tidesdb)
            fetch_tidesdb
            generate_cmake_config
            ;;
        --wiredtiger)
            fetch_wiredtiger
            generate_cmake_config
            ;;
        --sophia)
            fetch_sophia
            generate_cmake_config
            ;;
        --clean)
            log_warn "Removing all competitor sources and builds..."
            rm -rf "${COMPETITORS_DIR}"
            log_info "Cleaned up."
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            log_error "Unknown option: $arg"
            print_usage
            exit 1
            ;;
    esac
done

echo ""
log_info "Done! To run benchmark with competitors:"
echo ""
echo "  cd ${SCRIPT_DIR}"
echo "  mkdir build && cd build"
echo "  cmake .. -DWITH_MDBX=ON -DWITH_LMDB=ON -DWITH_ROCKSDB=ON -DWITH_LEVELDB=ON \\"
echo "           -DCMAKE_PREFIX_PATH=${INSTALL_DIR}"
echo "  make"
echo "  ./benchmark_kv_storage -n 100000"
echo ""
