#!/bin/bash
# Download and build JSON parser competitors for benchmarking

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/../competitors"

echo "╔════════════════════════════════════════════════════════════════════════════╗"
echo "║           DOWNLOADING & BUILDING COMPETITORS                               ║"
echo "╚════════════════════════════════════════════════════════════════════════════╝"
echo ""

mkdir -p "${COMPETITORS_DIR}"
cd "${COMPETITORS_DIR}"

# Detect CPU features for optimal builds
detect_cpu_features() {
    local features=""
    
    if grep -q avx512 /proc/cpuinfo 2>/dev/null; then
        features="${features} AVX-512"
    elif grep -q avx2 /proc/cpuinfo 2>/dev/null; then
        features="${features} AVX2"
    elif grep -q sse4_2 /proc/cpuinfo 2>/dev/null; then
        features="${features} SSE4.2"
    fi
    
    echo "${features:-No SIMD}"
}

CPU_FEATURES=$(detect_cpu_features)
echo "🖥️  Detected CPU features: ${CPU_FEATURES}"
echo ""

# Set optimal compiler flags
export CFLAGS="-O3 -march=native -DNDEBUG"
export CXXFLAGS="-O3 -march=native -DNDEBUG"

#═══════════════════════════════════════════════════════════════════════════════
# 1. json-c (Already in use by DAP SDK, use existing)
#═══════════════════════════════════════════════════════════════════════════════
echo "1️⃣  JSON-C (BASELINE)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Using DAP SDK's existing json-c from 3rdparty/"
echo "✅ json-c available"
echo ""

#═══════════════════════════════════════════════════════════════════════════════
# 2. RapidJSON
#═══════════════════════════════════════════════════════════════════════════════
echo "2️⃣  RapidJSON (C++ High-Performance)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d "rapidjson" ]; then
    echo "✓ RapidJSON already downloaded"
else
    echo "⏳ Cloning RapidJSON..."
    git clone --depth=1 https://github.com/Tencent/rapidjson.git
fi

cd rapidjson
echo "✅ RapidJSON ready (header-only library)"
cd ..
echo ""

#═══════════════════════════════════════════════════════════════════════════════
# 3. simdjson (THE TARGET TO BEAT)
#═══════════════════════════════════════════════════════════════════════════════
echo "3️⃣  simdjson (TARGET TO BEAT: 6-12 GB/s)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d "simdjson" ]; then
    echo "✓ simdjson already downloaded"
else
    echo "⏳ Cloning simdjson..."
    git clone --depth=1 https://github.com/simdjson/simdjson.git
fi

cd simdjson

if [ -d "build" ]; then
    rm -rf build
fi

mkdir build && cd build

echo "⏳ Building simdjson with maximum optimizations..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${CFLAGS}" \
    -DCMAKE_CXX_FLAGS="${CXXFLAGS}" \
    -DSIMDJSON_ENABLE_THREADS=ON \
    -DSIMDJSON_JUST_LIBRARY=ON

make -j$(nproc)

echo "✅ simdjson built ($(ls -lh libsimdj son.* 2>/dev/null | head -1 | awk '{print $5}'))"
cd ../..
echo ""

#═══════════════════════════════════════════════════════════════════════════════
# 4. yajl (Streaming Reference)
#═══════════════════════════════════════════════════════════════════════════════
echo "4️⃣  yajl (Streaming Reference)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

if [ -d "yajl" ]; then
    echo "✓ yajl already downloaded"
else
    echo "⏳ Cloning yajl..."
    git clone --depth=1 https://github.com/lloyd/yajl.git
fi

cd yajl

if [ -d "build" ]; then
    rm -rf build
fi

mkdir build && cd build

echo "⏳ Building yajl with maximum optimizations..."
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_FLAGS="${CFLAGS}"

make -j$(nproc)

echo "✅ yajl built ($(ls -lh yajl-*/lib/libyajl.* 2>/dev/null | head -1 | awk '{print $5}'))"
cd ../..
echo ""

#═══════════════════════════════════════════════════════════════════════════════
# Summary
#═══════════════════════════════════════════════════════════════════════════════
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ ALL COMPETITORS READY"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "📁 Competitors location: ${COMPETITORS_DIR}"
echo ""
echo "Built with:"
echo "  CFLAGS:   ${CFLAGS}"
echo "  CXXFLAGS: ${CXXFLAGS}"
echo "  CPU:      ${CPU_FEATURES}"
echo ""
ls -d */ 2>/dev/null || echo "No directories found"
echo ""
echo "✅ Competitor download & build complete!"

