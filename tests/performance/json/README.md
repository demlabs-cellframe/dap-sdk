# Benchmark Infrastructure - Phase 0.5

## Overview

Comprehensive benchmarking infrastructure для сравнения `dap_json_native` с лучшими JSON парсерами в мире.

## Structure

```
benchmarks/
├── competitors/          # Конкуренты (auto-downloaded & built)
│   ├── json-c/          # Baseline (current DAP SDK)
│   ├── rapidjson/       # C++ high-performance (1-2 GB/s)
│   ├── simdjson/        # TARGET (6-12 GB/s)
│   └── yajl/            # Streaming reference (0.5-1 GB/s)
├── datasets/            # Benchmark JSON files
│   ├── twitter.json     # 600 KB (typical API)
│   ├── citm_catalog.json # 1.7 MB (complex nested)
│   ├── canada.json      # 2.2 MB (geo, numbers)
│   └── large.json       # 100 MB (stress test)
├── src/                 # Benchmark implementations
│   ├── common.h         # Utilities
│   ├── bench_json_c.c
│   ├── bench_rapidjson.cpp
│   ├── bench_simdjson.cpp
│   ├── bench_yajl.c
│   └── bench_dap_json.c # Future: our parser
├── scripts/             # Automation
│   ├── download_datasets.sh     # ✅ READY
│   ├── download_competitors.sh  # ✅ READY
│   └── run_benchmarks.sh        # 🚧 TODO
├── results/             # JSON results
└── CMakeLists.txt       # Build config
```

## Quick Start

```bash
# 1. Download datasets
./benchmarks/scripts/download_datasets.sh

# 2. Download & build competitors
./benchmarks/scripts/download_competitors.sh

# 3. Build benchmarks
mkdir build && cd build
cmake ../benchmarks
make -j

# 4. Run benchmarks
./run_benchmarks

# 5. View results
cat ../benchmarks/results/latest.json
```

## Competitors

### 1. json-c (Baseline)
- **Current:** DAP SDK dependency  
- **Speed:** ~0.3 GB/s
- **Type:** C, DOM API
- **Status:** Using existing 3rdparty/json-c

### 2. RapidJSON
- **Speed:** 1-2 GB/s
- **Type:** C++, SAX+DOM hybrid
- **Features:** In-situ parsing, SSO
- **Repo:** https://github.com/Tencent/rapidjson

### 3. simdjson (TARGET TO BEAT)
- **Speed:** 6-12 GB/s (AVX-512)
- **Type:** C++, two-stage SIMD
- **Features:** Parallel Stage 1, tape format
- **Repo:** https://github.com/simdjson/simdjson
- **Goal:** ПРЕВЗОЙТИ на multi-core (+30-50%)

### 4. yajl (Streaming)
- **Speed:** 0.5-1 GB/s
- **Type:** C, streaming callbacks
- **Features:** O(depth) memory
- **Repo:** https://github.com/lloyd/yajl

## Datasets

### twitter.json (~600 KB)
- Typical Twitter API response
- Mixed types, moderate nesting
- Representative of web APIs

### citm_catalog.json (~1.7 MB)
- Complex event catalog
- Deep nesting, many string keys
- Stress test for nested objects

### canada.json (~2.2 MB)
- GeoJSON for Canada
- Primarily floating-point numbers
- Number parsing performance

### large.json (~100 MB)
- Generated stress test
- Very large file
- Sustained throughput, memory efficiency

## Metrics

### Throughput
- **Unit:** GB/s (gigabytes per second)
- **Measurement:** Parse entire file, measure time
- **Formula:** file_size_bytes / parse_time_seconds / 1e9

### Latency
- **Unit:** microseconds (μs)
- **Measurement:** Time to first value (SAX mode)
- **Percentiles:** p50, p95, p99

### Memory
- **Peak RSS:** Maximum resident set size
- **Overhead:** memory_used / input_size
- **Target:** <2x input size for DOM

### CPU Efficiency
- **Instructions/byte:** Lower is better
- **Cache misses:** L1, L2, L3
- **Branch mispredictions:** %

## Build Options

```bash
# Debug build
cmake -DCMAKE_BUILD_TYPE=Debug ..

# Release build (default)
cmake -DCMAKE_BUILD_TYPE=Release ..

# With profiling
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DENABLE_PROFILING=ON ..

# Specific competitor only
cmake -DBENCH_JSON_C=ON -DBENCH_SIMDJSON=OFF ..
```

## Results Format

Results are saved as JSON in `results/`:

```json
{
  "timestamp": "2025-01-07T22:00:00Z",
  "system": {
    "cpu": "Intel Core i9-12900K",
    "cores": 16,
    "threads": 24,
    "features": ["AVX-512", "AVX2", "SSE4.2"],
    "memory_gb": 64
  },
  "benchmarks": [
    {
      "parser": "simdjson",
      "dataset": "twitter.json",
      "throughput_gbps": 8.5,
      "latency_us": {"p50": 72, "p95": 85, "p99": 92},
      "memory_mb": 1.2,
      "overhead": 2.0
    }
  ]
}
```

## Comparison Goals

| Parser | Throughput (AVX-512) | Latency | Memory | Status |
|--------|---------------------|---------|---------|---------|
| json-c | 0.3 GB/s | ~1000 μs | 3x | BASELINE |
| yajl | 0.5-1 GB/s | ~500 μs | O(depth) | REFERENCE |
| RapidJSON | 1-2 GB/s | ~200 μs | 1.5x | COMPARE |
| simdjson | 6-12 GB/s | ~50 μs | 1.5x | TARGET |
| **dap_json** | **10-14 GB/s** | **<50 μs** | **1.5x** | **GOAL** |

## Implementation Status

- [x] Phase 0.5.1: Directory structure
- [x] Phase 0.5.2: Dataset download scripts
- [x] Phase 0.5.3: Competitor download scripts
- [ ] Phase 0.5.4: Benchmark implementations
- [ ] Phase 0.5.5: Measurement infrastructure
- [ ] Phase 0.5.6: CMake build system
- [ ] Phase 0.5.7: Baseline collection
- [ ] Phase 0.5.8: Results analysis tools

## Next Steps

1. Implement benchmark harness (common.h)
2. Write benchmark wrappers for each parser
3. Create CMakeLists.txt
4. Run baseline measurements (json-c)
5. Document baseline results
6. Ready for Phase 1 (Core Implementation)

## Notes

- All competitors built with `-O3 -march=native -DNDEBUG`
- Benchmarks run 10 times, median reported
- Warm-up run before measurements
- CPU frequency locked for consistency
- No other processes during benchmarking

