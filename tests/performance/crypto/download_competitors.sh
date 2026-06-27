#!/bin/bash
#
# Download and build competitor crypto libraries for benchmarking.
#
# Competitors:
#   liboqs  — NIST ML-DSA (Dilithium) reference sizes/timings
#   raptor  — Lattice-based ring signature (Falcon-based, Zhang 2018)
#
# Usage: ./download_competitors.sh [--all | --liboqs | --raptor]
#        Default: --all
#
# Results go to competitors/ directory.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
COMP_DIR="${SCRIPT_DIR}/competitors"
INSTALL_DIR="${COMP_DIR}/install"
NPROC=$(nproc 2>/dev/null || echo 4)

mkdir -p "${COMP_DIR}" "${INSTALL_DIR}"

build_liboqs() {
    echo "=== Building liboqs ==="
    local SRC="${COMP_DIR}/liboqs"
    if [ ! -d "${SRC}" ]; then
        git clone --depth 1 https://github.com/open-quantum-safe/liboqs.git "${SRC}"
    fi
    local BUILD="${SRC}/build"
    mkdir -p "${BUILD}"
    cmake -S "${SRC}" -B "${BUILD}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
        -DBUILD_SHARED_LIBS=OFF \
        -DOQS_BUILD_ONLY_LIB=ON \
        -DOQS_USE_OPENSSL=OFF \
        -DOQS_MINIMAL_BUILD="SIG_ml_dsa_44;SIG_ml_dsa_65;SIG_ml_dsa_87"
    cmake --build "${BUILD}" -j "${NPROC}"
    cmake --install "${BUILD}"
    echo "=== liboqs installed ==="
}

build_raptor() {
    echo "=== Building Raptor ==="
    local SRC="${COMP_DIR}/raptor"
    if [ ! -d "${SRC}" ]; then
        git clone --depth 1 https://github.com/zhenfeizhang/raptor.git "${SRC}"
    fi
    cd "${SRC}"
    gcc -O3 -std=c11 -I. -Ifalcon -Irng \
        test.c raptor.c linkable_raptor.c poly.c print.c \
        falcon/falcon-*.c falcon/frng.c falcon/crypto_stream.c falcon/shake.c falcon/nist.c \
        rng/*.c \
        -lm -lcrypto -o bench_raptor 2>/dev/null && echo "=== Raptor built ===" || echo "=== Raptor build failed (needs OpenSSL) ==="
    cd "${SCRIPT_DIR}"
}

show_help() {
    echo "Usage: $0 [--all | --liboqs | --raptor]"
    echo "  --all       Build all competitors (default)"
    echo "  --liboqs    Build liboqs only"
    echo "  --raptor    Build Raptor only"
    echo "  --help      Show this help"
}

if [ $# -eq 0 ]; then
    set -- --all
fi

for arg in "$@"; do
    case "$arg" in
        --all)
            build_liboqs
            build_raptor
            ;;
        --liboqs)   build_liboqs ;;
        --raptor)   build_raptor ;;
        --help|-h)  show_help; exit 0 ;;
        *)          echo "Unknown option: $arg"; show_help; exit 1 ;;
    esac
done

echo ""
echo "=== Competitors ready. Run cmake with -DBUILD_BENCHMARKS=ON ==="
