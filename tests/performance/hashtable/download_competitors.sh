#!/bin/bash
# Download competing hash table implementations for benchmarking
# Based on the approach used in dap_json SIMD benchmarks

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPETITORS_DIR="${SCRIPT_DIR}/competitors"

mkdir -p "${COMPETITORS_DIR}"

echo "=== Downloading hash table implementations for benchmarking ==="

# uthash - original implementation we're replacing
echo "[1/5] Downloading uthash (latest master)..."
if [ ! -f "${COMPETITORS_DIR}/uthash.h" ]; then
    curl -sL "https://raw.githubusercontent.com/troydhanson/uthash/master/src/uthash.h" \
        -o "${COMPETITORS_DIR}/uthash.h"
    echo "  Downloaded uthash.h"
else
    echo "  uthash.h already exists"
fi

# khash from klib - very fast hash table
echo "[2/5] Downloading khash (klib)..."
if [ ! -f "${COMPETITORS_DIR}/khash.h" ]; then
    curl -sL "https://raw.githubusercontent.com/attractivechaos/klib/master/khash.h" \
        -o "${COMPETITORS_DIR}/khash.h"
    echo "  Downloaded khash.h"
else
    echo "  khash.h already exists"
fi

# stb_ds - stb dynamic array/hash table
echo "[3/5] Downloading stb_ds (stb libraries)..."
if [ ! -f "${COMPETITORS_DIR}/stb_ds.h" ]; then
    curl -sL "https://raw.githubusercontent.com/nothings/stb/master/stb_ds.h" \
        -o "${COMPETITORS_DIR}/stb_ds.h"
    echo "  Downloaded stb_ds.h"
else
    echo "  stb_ds.h already exists"
fi

# tommyds - another fast hash table
echo "[4/5] Downloading tommyds..."
if [ ! -f "${COMPETITORS_DIR}/tommyhash.h" ]; then
    curl -sL "https://raw.githubusercontent.com/amadvance/tommyds/master/tommytypes.h" \
        -o "${COMPETITORS_DIR}/tommytypes.h"
    curl -sL "https://raw.githubusercontent.com/amadvance/tommyds/master/tommyhash.c" \
        -o "${COMPETITORS_DIR}/tommyhash.c"
    curl -sL "https://raw.githubusercontent.com/amadvance/tommyds/master/tommyhashlin.c" \
        -o "${COMPETITORS_DIR}/tommyhashlin.c"
    curl -sL "https://raw.githubusercontent.com/amadvance/tommyds/master/tommyhashlin.h" \
        -o "${COMPETITORS_DIR}/tommyhashlin.h"
    curl -sL "https://raw.githubusercontent.com/amadvance/tommyds/master/tommyhash.h" \
        -o "${COMPETITORS_DIR}/tommyhash.h"
    echo "  Downloaded tommyds files"
else
    echo "  tommyds already exists"
fi

# hashmap.c - simple open-addressing hashmap
echo "[5/5] Downloading hashmap.c..."
if [ ! -f "${COMPETITORS_DIR}/hashmap.h" ]; then
    curl -sL "https://raw.githubusercontent.com/tidwall/hashmap.c/master/hashmap.h" \
        -o "${COMPETITORS_DIR}/hashmap.h"
    curl -sL "https://raw.githubusercontent.com/tidwall/hashmap.c/master/hashmap.c" \
        -o "${COMPETITORS_DIR}/hashmap.c"
    echo "  Downloaded hashmap.c files"
else
    echo "  hashmap.c already exists"
fi

echo ""
echo "=== All competitors downloaded to ${COMPETITORS_DIR} ==="
echo ""
echo "Implementations:"
echo "  - uthash.h   : Classic intrusive hash table (our original)"
echo "  - khash.h    : Very fast hash table from klib"
echo "  - stb_ds.h   : STB dynamic structures"
echo "  - tommyds    : Tommy hash linear probing"
echo "  - hashmap.c  : Simple open-addressing hashmap"
echo ""
echo "Run 'cmake --build . && ctest -R hashtable' to benchmark"
