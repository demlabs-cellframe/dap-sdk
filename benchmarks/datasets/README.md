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
