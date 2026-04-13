#!/bin/bash
# Download and build JSON parser competitors for benchmarking
# Usage: ./download_competitors.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/competitors"
BUILD_DIR="${COMPETITORS_DIR}/build"

echo "========================================"
echo "  JSON Parsers Competitors Downloader"
echo "========================================"
echo ""

mkdir -p "${COMPETITORS_DIR}"
mkdir -p "${BUILD_DIR}"

# ============================================================================
# simdjson - Best-in-class SIMD JSON parser
# ============================================================================
echo "[1/4] Downloading simdjson..."
if [ ! -d "${COMPETITORS_DIR}/simdjson" ]; then
    cd "${COMPETITORS_DIR}"
    git clone https://github.com/simdjson/simdjson.git
    cd simdjson
    git checkout v3.10.1  # Latest stable
    echo "✅ simdjson downloaded"
else
    echo "⏭️  simdjson already exists"
fi

echo "[1/4] Building simdjson..."
cd "${COMPETITORS_DIR}/simdjson"
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DSIMDJSON_JUST_LIBRARY=ON \
    -DBUILD_SHARED_LIBS=OFF
cmake --build build -j$(nproc)
echo "✅ simdjson built successfully"
echo ""

# ============================================================================
# RapidJSON - Tencent's fast JSON parser
# ============================================================================
echo "[2/4] Downloading RapidJSON..."
if [ ! -d "${COMPETITORS_DIR}/rapidjson" ]; then
    cd "${COMPETITORS_DIR}"
    git clone https://github.com/Tencent/rapidjson.git
    cd rapidjson
    git checkout v1.1.0  # Latest stable
    echo "✅ RapidJSON downloaded"
else
    echo "⏭️  RapidJSON already exists"
fi

echo "[2/4] Building RapidJSON (header-only, no build needed)..."
echo "✅ RapidJSON ready (header-only library)"
echo ""

# ============================================================================
# yajl - Yet Another JSON Library
# ============================================================================
echo "[3/4] Downloading yajl..."
if [ ! -d "${COMPETITORS_DIR}/yajl" ]; then
    cd "${COMPETITORS_DIR}"
    git clone https://github.com/lloyd/yajl.git
    cd yajl
    git checkout 2.1.0  # Latest release
    echo "✅ yajl downloaded"
else
    echo "⏭️  yajl already exists"
fi

echo "[3/4] Building yajl..."
cd "${COMPETITORS_DIR}/yajl"
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
echo "✅ yajl built successfully"
echo ""

# ============================================================================
# json-c - C JSON implementation
# ============================================================================
echo "[4/4] Downloading json-c..."
if [ ! -d "${COMPETITORS_DIR}/json-c" ]; then
    cd "${COMPETITORS_DIR}"
    git clone https://github.com/json-c/json-c.git
    cd json-c
    git checkout json-c-0.17-20230812  # Latest stable
    echo "✅ json-c downloaded"
else
    echo "⏭️  json-c already exists"
fi

echo "[4/4] Building json-c..."
cd "${COMPETITORS_DIR}/json-c"
cmake -B build -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_STATIC_LIBS=ON \
    -DDISABLE_WERROR=ON
cmake --build build -j$(nproc)
echo "✅ json-c built successfully"
echo ""

# ============================================================================
# Summary
# ============================================================================
echo "========================================"
echo "  ✅ ALL COMPETITORS DOWNLOADED & BUILT"
echo "========================================"
echo ""
echo "Installed parsers:"
echo "  📦 simdjson   - ${COMPETITORS_DIR}/simdjson/"
echo "  📦 RapidJSON  - ${COMPETITORS_DIR}/rapidjson/"
echo "  📦 yajl       - ${COMPETITORS_DIR}/yajl/"
echo "  📦 json-c     - ${COMPETITORS_DIR}/json-c/"
echo ""
echo "Include paths for CMake:"
echo "  -DSIMDJSON_INCLUDE_DIR=${COMPETITORS_DIR}/simdjson/include"
echo "  -DSIMDJSON_LIBRARY=${COMPETITORS_DIR}/simdjson/build/libsimdjson.a"
echo "  -DRAPIDJSON_INCLUDE_DIR=${COMPETITORS_DIR}/rapidjson/include"
echo "  -DYAJL_INCLUDE_DIR=${COMPETITORS_DIR}/yajl/build/yajl-2.1.0/include"
echo "  -DYAJL_LIBRARY=${COMPETITORS_DIR}/yajl/build/yajl-2.1.0/lib/libyajl_s.a"
echo "  -DJSONC_INCLUDE_DIR=${COMPETITORS_DIR}/json-c/build"
echo "  -DJSONC_LIBRARY=${COMPETITORS_DIR}/json-c/build/libjson-c.a"
echo ""
echo "Now run: cmake --build build"
