# Performance Gaps and Optimization Opportunities

**Date:** 2026-01-12  
**Analysis:** Deep dive into remaining optimization opportunities

---

## 🔍 Detailed Gap Analysis by Scenario

### 1. Small JSON (<1KB) - Latency Critical

**Current:** 0.12 GB/s, ~1140ns mean latency  
**simdjson:** 1.18 GB/s, ~97ns mean latency  
**Gap:** 10x slower, 11x higher latency

#### Root Causes:
1. **Allocation overhead dominates** for small JSON
   - Every `malloc()` call: ~50-100ns
   - Current: ~10+ allocations per small JSON
   - simdjson: 1-2 allocations max

2. **Hash table overhead**
   - Building hash table: ~200-300ns
   - Lookup overhead: ~20-30ns per key
   - simdjson: No hash tables, direct tape access

3. **String copying**
   - Small strings still get copied
   - Cache misses from scattered allocations

#### Solutions:
- ✅ **Small Object Optimization (SOO)**
  ```c
  // Pre-allocate for small JSON
  typedef struct {
      char buffer[256];  // Stack allocation
      dap_json_value values[16];
      bool heap_needed;
  } dap_json_small_t;
  ```
  **Impact:** ~3-5x faster for small JSON

- ✅ **Fast path for simple objects**
  ```c
  if (json_size < 256 && !has_nested_objects) {
      return parse_small_fast(json);  // Optimized path
  }
  ```
  **Impact:** ~2x faster for simple objects

---

### 2. Medium JSON (10-100KB) - Most Common Case

**Current:** 0.20 GB/s  
**simdjson:** 2.42 GB/s  
**Gap:** 12x slower

#### Root Causes:
1. **Stage 1 not using SIMD**
   - Character-by-character parsing
   - Should be: 64 bytes at a time (AVX2)

2. **Stage 2 allocation storm**
   - ~1000 individual allocations for 100KB JSON
   - Memory fragmentation
   - Cache misses

3. **No prefetching**
   - CPU waits for memory
   - simdjson: Hardware prefetch hints

#### Solutions:
- ✅ **Integrate SIMD Stage 1** (already implemented!)
  ```c
  // Just switch API:
  dap_json_parse_fast(json, len);  // Uses AVX2
  ```
  **Impact:** 8-10x faster Stage 1

- ✅ **Arena allocator** (already implemented!)
  ```c
  // Batch allocate
  arena_alloc_batch(sizes, count);
  ```
  **Impact:** 2-3x fewer cache misses

- ✅ **Prefetch hints**
  ```c
  __builtin_prefetch(next_chunk, 0, 3);
  ```
  **Impact:** 5-10% throughput gain

---

### 3. Large JSON (1-10MB) - Throughput Critical

**Current:** 0.17 GB/s  
**simdjson:** 2.26 GB/s  
**Gap:** 13x slower

#### Root Causes:
1. **Memory bandwidth saturation**
   - Multiple passes over data
   - Should be: Single pass (Stage 1 SIMD)

2. **Cache thrashing**
   - Scattered allocations don't fit in L3
   - Working set > 32MB

3. **No parallel processing**
   - Single-threaded
   - Large JSON could be chunked

#### Solutions:
- ✅ **SIMD Stage 1** (reduce passes)
  **Impact:** 10x faster indexing

- ✅ **Memory-mapped I/O**
  ```c
  int fd = open(filename, O_RDONLY);
  char *data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  madvise(data, size, MADV_SEQUENTIAL);
  ```
  **Impact:** 20-30% faster for file parsing

- ⚠️ **Parallel Stage 2** (future)
  ```c
  // Split into chunks, process in parallel
  #pragma omp parallel for
  for (int i = 0; i < num_chunks; i++) {
      process_chunk(chunks[i]);
  }
  ```
  **Impact:** 2-4x on multi-core

---

### 4. Deep Nested (500+ levels) - Recursion Test

**Current:** 0.18 GB/s  
**simdjson:** 2.02 GB/s  
**Gap:** 11x slower

#### Root Causes:
1. **Recursive parsing overhead**
   - Function call per level: ~10ns
   - 500 levels = 5000ns just in calls

2. **Stack allocation per level**
   - Context pushed/popped
   - Cache misses

3. **No tail-call optimization**
   - Compiler can't optimize recursion

#### Solutions:
- ✅ **Iterative parsing with explicit stack**
  ```c
  typedef struct {
      uint32_t *stack;
      size_t stack_top;
  } parse_context_t;
  
  // Push/pop instead of recursion
  stack_push(ctx, current_depth);
  ```
  **Impact:** 3-5x faster for deep nesting

- ✅ **Tape format** (flattens hierarchy)
  ```c
  // [START_OBJ, key, START_OBJ, key, value, END_OBJ, END_OBJ]
  // Linear scan, no recursion needed
  ```
  **Impact:** 2x faster traversal

---

### 5. Wide Arrays (100K elements) - Cache Pressure

**Current:** 0.17 GB/s  
**simdjson:** 1.19 GB/s  
**Gap:** 7x slower

#### Root Causes:
1. **Array reallocation**
   - Grows: 1→2→4→8→16...→131072
   - Multiple `realloc()` calls
   - Memory copying

2. **Element allocation overhead**
   - 100K individual allocations
   - Fragmentation

3. **Poor cache locality**
   - Elements scattered in memory

#### Solutions:
- ✅ **Pre-allocate array**
  ```c
  // Count elements in Stage 1
  size_t element_count = count_array_elements(stage1);
  array->elements = arena_alloc(element_count * sizeof(dap_json_value));
  ```
  **Impact:** 2-3x faster array parsing

- ✅ **Contiguous storage**
  ```c
  // All elements in single block
  dap_json_value *elements = arena_alloc_contiguous(count);
  ```
  **Impact:** Better cache utilization

---

### 6. Number Heavy (90%) - Parsing Overhead

**Current:** 0.13 GB/s  
**simdjson:** 1.20 GB/s  
**Gap:** 9x slower

#### Root Causes:
1. **Slow `strtod()`**
   - Generic C library function
   - Handles locale, edge cases
   - ~100-200ns per number

2. **No SIMD digit validation**
   - Character-by-character check

3. **Multiple passes**
   - Find number bounds
   - Validate digits
   - Parse value

#### Solutions:
- ✅ **fast_float library** (Lemire's algorithm)
  ```c
  #include "fast_float.h"
  double val;
  fast_float::from_chars(start, end, val);
  ```
  **Impact:** 2-5x faster number parsing

- ✅ **SIMD digit classification**
  ```c
  __m256i digits = _mm256_loadu_si256(str);
  __m256i is_digit = _mm256_and_si256(
      _mm256_cmpgt_epi8(digits, _mm256_set1_epi8('0'-1)),
      _mm256_cmpgt_epi8(_mm256_set1_epi8('9'+1), digits)
  );
  ```
  **Impact:** 10x faster digit validation

- ✅ **Integer fast path**
  ```c
  if (!has_decimal && !has_exponent) {
      // Parse as int64 (much faster)
      int64_t val = parse_int64(start, len);
  }
  ```
  **Impact:** 3-5x faster for integers

---

### 7. String Heavy (90%) - WORST CASE

**Current:** 0.45 GB/s  
**simdjson:** 9.31 GB/s  
**Gap:** 21x slower (WORST!)

#### Root Causes:
1. **String copying dominates**
   - `strndup()` for every string
   - Memory allocation + memcpy
   - ~50ns + (len * 0.3ns)

2. **Slow escape processing**
   - Character-by-character scanning
   - Should be: SIMD find quote/backslash

3. **No UTF-8 validation parallelism**
   - Scalar validation

#### Solutions:
- ✅ **Zero-copy strings** (CRITICAL!)
  ```c
  typedef struct {
      const char *data;  // Pointer into JSON
      uint32_t length;
      bool needs_unescape;
  } dap_json_string_t;
  ```
  **Impact:** 10-20x faster for strings without escapes

- ✅ **SIMD string scanner** (already in roadmap!)
  ```c
  // Find closing quote in 32 bytes at once
  __m256i chunk = _mm256_loadu_si256(str);
  __m256i quotes = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('"'));
  uint32_t mask = _mm256_movemask_epi8(quotes);
  int pos = __builtin_ctz(mask);
  ```
  **Impact:** 5-10x faster string scanning

- ✅ **Lazy unescaping**
  ```c
  // Only unescape when accessed
  const char* dap_json_string_get_cstr(dap_json_string_t *str) {
      if (str->needs_unescape && !str->unescaped) {
          str->unescaped = unescape(str->data, str->length);
      }
      return str->unescaped ? str->unescaped : str->data;
  }
  ```
  **Impact:** Skip unescaping if string never accessed

---

## 🎯 Priority Optimization Matrix

| Optimization | Impact | Effort | Priority | Status |
|--------------|--------|--------|----------|--------|
| Integrate SIMD Stage 1 | +++++ | Low | **P0** | ✅ Ready |
| Zero-copy strings | +++++ | Medium | **P0** | TODO |
| Arena allocator | ++++ | Low | **P0** | ✅ Done |
| fast_float numbers | ++++ | Low | **P1** | TODO |
| SIMD string scanner | ++++ | Medium | **P1** | TODO |
| Tape format | ++++ | High | **P1** | TODO |
| Small object optimization | +++ | Medium | **P2** | TODO |
| Iterative parser | +++ | Medium | **P2** | TODO |
| Prefetch hints | ++ | Low | **P3** | TODO |
| Parallel Stage 2 | +++++ | High | **P3** | TODO |

---

## 📊 Expected Impact by Optimization

```
Current:    0.2 GB/s  ████░░░░░░░░░░░░░░░░
+ SIMD S1:  1.5 GB/s  ███████████░░░░░░░░░  (+7x)
+ Zero-copy: 2.5 GB/s ███████████████░░░░░  (+1.7x)
+ fast_float:3.0 GB/s ████████████████░░░░  (+1.2x)
+ Tape fmt:  4.0 GB/s ████████████████████  (+1.3x)
+ Parallel:  6.0 GB/s ██████████████████████████ (+1.5x)

simdjson:    2.5 GB/s  ███████████████░░░░░
Target:      3.5 GB/s  █████████████████░░░  (Beat simdjson!)
```

---

## 🚀 Immediate Action Items

### This Week:
1. ✅ Create `dap_json_parse_fast()` API
2. ✅ Wire up SIMD Stage 1 to public API
3. ✅ Update benchmark
4. ✅ Re-measure: expect 0.2 → 1.5 GB/s

### Next Week:
5. Implement zero-copy strings
6. Add fast_float number parsing
7. Implement SIMD string scanner
8. Re-measure: expect 1.5 → 3.0 GB/s

### Week 3:
9. Implement tape-based format
10. Optimize memory layout
11. Re-measure: expect 3.0 → 4.0 GB/s

**Target:** Beat simdjson (>2.5 GB/s average) by end of Week 2!

---

## 💡 Key Takeaways

1. **Biggest wins are low-hanging fruit:**
   - SIMD Stage 1: Already done, just wire it up! (+7x)
   - Zero-copy strings: Conceptually simple (+2x)
   
2. **String heavy case needs special attention:**
   - 21x slower is unacceptable
   - Zero-copy will fix most of it
   
3. **We can beat simdjson:**
   - They don't have parallel Stage 2
   - They don't have predictive parsing
   - They don't support JSON5/JSONC
   
4. **Clear path forward:**
   - Week 1: Match json-c
   - Week 2: Match simdjson
   - Week 3: Beat simdjson

**The architecture is sound, we just need to connect the pieces! 🎯**
