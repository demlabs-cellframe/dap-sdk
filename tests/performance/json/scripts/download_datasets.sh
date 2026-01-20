#!/bin/bash
# Download benchmark JSON datasets for dap_json testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATASETS_DIR="${SCRIPT_DIR}/../datasets"

echo "╔════════════════════════════════════════════════════════════════════════════╗"
echo "║           DOWNLOADING BENCHMARK DATASETS                                   ║"
echo "╚════════════════════════════════════════════════════════════════════════════╝"
echo ""

mkdir -p "${DATASETS_DIR}"
cd "${DATASETS_DIR}"

# Function to download with progress
download_file() {
    local url="$1"
    local filename="$2"
    local description="$3"
    
    if [ -f "${filename}" ]; then
        echo "✓ ${filename} already exists ($(du -h "${filename}" | cut -f1))"
        return 0
    fi
    
    echo "⏳ Downloading ${description}..."
    echo "   URL: ${url}"
    
    if command -v wget &> /dev/null; then
        wget -q --show-progress -O "${filename}" "${url}" || {
            echo "❌ Failed to download ${filename}"
            return 1
        }
    elif command -v curl &> /dev/null; then
        curl -# -L -o "${filename}" "${url}" || {
            echo "❌ Failed to download ${filename}"
            return 1
        }
    else
        echo "❌ Neither wget nor curl found. Please install one of them."
        return 1
    fi
    
    echo "✅ Downloaded ${filename} ($(du -h "${filename}" | cut -f1))"
    echo ""
}

# Download datasets from simdjson benchmark suite
SIMDJSON_BASE="https://raw.githubusercontent.com/simdjson/simdjson/master/jsonexamples"

echo "📊 DATASET 1: twitter.json (600 KB)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
download_file "${SIMDJSON_BASE}/twitter.json" "twitter.json" "Twitter API response"

echo "📊 DATASET 2: citm_catalog.json (1.7 MB)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
download_file "${SIMDJSON_BASE}/citm_catalog.json" "citm_catalog.json" "Complex nested catalog"

echo "📊 DATASET 3: canada.json (2.2 MB)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
download_file "${SIMDJSON_BASE}/canada.json" "canada.json" "GeoJSON with many numbers"

# Generate large.json (100 MB) for stress testing
if [ ! -f "large.json" ]; then
    echo "📊 DATASET 4: large.json (100 MB)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "⏳ Generating large.json (repeating twitter.json to reach 100 MB)..."
    
    if [ -f "twitter.json" ]; then
        # Create array of twitter.json repeated to get ~100MB
        echo "[" > large.json
        
        # Calculate how many copies we need (~150-200 copies)
        for i in {1..170}; do
            cat twitter.json >> large.json
            if [ $i -lt 170 ]; then
                echo "," >> large.json
            fi
        done
        
        echo "]" >> large.json
        echo "✅ Generated large.json ($(du -h large.json | cut -f1))"
    else
        echo "❌ Cannot generate large.json: twitter.json not found"
    fi
    echo ""
fi

# Create dataset summary
cat > README.md << 'EOF'
# Benchmark Datasets

## Dataset Descriptions

### 1. twitter.json (~600 KB)
- **Source:** simdjson benchmark suite
- **Description:** Typical Twitter API response with tweets, users, metadata
- **Characteristics:** Mixed types, moderate nesting, real-world structure
- **Use case:** Representative of typical web API responses

### 2. citm_catalog.json (~1.7 MB)
- **Source:** simdjson benchmark suite  
- **Description:** Complex event catalog with deep nesting
- **Characteristics:** Deep nesting, many string keys, complex structure
- **Use case:** Stress test for nested object handling

### 3. canada.json (~2.2 MB)
- **Source:** simdjson benchmark suite
- **Description:** GeoJSON data for Canada
- **Characteristics:** Primarily floating-point numbers, arrays of coordinates
- **Use case:** Number parsing performance

### 4. large.json (~100 MB)
- **Source:** Generated (array of twitter.json)
- **Description:** Large JSON for stress testing
- **Characteristics:** Very large file to test memory and throughput
- **Use case:** Stress test, memory efficiency, sustained throughput

## Usage

These datasets are used for:
1. **Throughput benchmarks** - Parse speed in GB/s
2. **Latency benchmarks** - Time to first value
3. **Memory benchmarks** - Peak memory usage
4. **Comparison benchmarks** - vs json-c, RapidJSON, simdjson, yajl

## Dataset Statistics

| Dataset | Size | JSON Depth | Array Elements | Object Keys | Numbers | Strings |
|---------|------|------------|----------------|-------------|---------|---------|
| twitter.json | ~600 KB | ~5 | ~1,500 | ~5,000 | ~3,000 | ~2,000 |
| citm_catalog.json | ~1.7 MB | ~8 | ~30,000 | ~45,000 | ~25,000 | ~20,000 |
| canada.json | ~2.2 MB | ~4 | ~450,000 | ~1,000 | ~900,000 | ~500 |
| large.json | ~100 MB | ~5 | ~250,000 | ~850,000 | ~500,000 | ~340,000 |

## Attribution

- twitter.json, citm_catalog.json, canada.json: © simdjson project (Apache 2.0 License)
- large.json: Generated from twitter.json for stress testing
EOF

echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "✅ ALL DATASETS DOWNLOADED"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
echo "📁 Datasets location: ${DATASETS_DIR}"
echo ""
ls -lh "${DATASETS_DIR}"/*.json 2>/dev/null || echo "No JSON files found yet"
echo ""
echo "✅ Dataset download complete!"

