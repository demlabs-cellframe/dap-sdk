#!/bin/bash
#
# Download and build competitor crypto libraries for benchmarking.
#
# Usage: ./download_competitors.sh [--all | --liboqs | --openssl | --libsodium]
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
        -DOQS_MINIMAL_BUILD="KEM_kyber_512;KEM_kyber_768;KEM_kyber_1024;SIG_dilithium_2;SIG_dilithium_3;SIG_dilithium_5"
    cmake --build "${BUILD}" -j "${NPROC}"
    cmake --install "${BUILD}"
    echo "=== liboqs installed to ${INSTALL_DIR} ==="
}

check_openssl() {
    echo "=== Checking OpenSSL ==="
    if pkg-config --exists openssl 2>/dev/null; then
        local VER=$(pkg-config --modversion openssl)
        local CFLAGS=$(pkg-config --cflags openssl)
        local LIBS=$(pkg-config --libs openssl)
        echo "Found OpenSSL ${VER}"
        cat > "${COMP_DIR}/openssl_config.cmake" <<EOF
set(OPENSSL_FOUND TRUE)
set(OPENSSL_VERSION "${VER}")
set(OPENSSL_CFLAGS "${CFLAGS}")
set(OPENSSL_LIBS "${LIBS}")
EOF
        echo "=== OpenSSL config written ==="
    else
        echo "OpenSSL not found via pkg-config, trying find_package..."
        cat > "${COMP_DIR}/openssl_config.cmake" <<EOF
set(OPENSSL_FOUND FALSE)
EOF
    fi
}

build_libsodium() {
    echo "=== Building libsodium ==="
    local SRC="${COMP_DIR}/libsodium"
    if [ ! -d "${SRC}" ]; then
        git clone --depth 1 https://github.com/jedisct1/libsodium.git "${SRC}"
    fi
    cd "${SRC}"
    if [ ! -f configure ]; then
        ./autogen.sh
    fi
    ./configure --prefix="${INSTALL_DIR}" --disable-shared --enable-static --with-pic
    make -j "${NPROC}"
    make install
    cd "${SCRIPT_DIR}"
    echo "=== libsodium installed to ${INSTALL_DIR} ==="
}

show_help() {
    echo "Usage: $0 [--all | --liboqs | --openssl | --libsodium]"
    echo "  --all        Build all competitors (default)"
    echo "  --liboqs     Build liboqs only"
    echo "  --openssl    Check/configure OpenSSL only"
    echo "  --libsodium  Build libsodium only"
}

if [ $# -eq 0 ]; then
    set -- --all
fi

for arg in "$@"; do
    case "$arg" in
        --all)
            build_liboqs
            check_openssl
            build_libsodium
            ;;
        --liboqs)     build_liboqs ;;
        --openssl)    check_openssl ;;
        --libsodium)  build_libsodium ;;
        --help|-h)    show_help; exit 0 ;;
        *)            echo "Unknown option: $arg"; show_help; exit 1 ;;
    esac
done

echo ""
echo "=== Competitors ready. Run cmake with -DBUILD_BENCHMARKS=ON ==="
