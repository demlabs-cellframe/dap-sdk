# DAP JSON Performance Analysis - Why simdjson is Faster

**Date:** 2026-01-12  
**Phase:** 2.6 - Competitive Benchmark Analysis  
**Status:** ⚠️ TARGET NOT MET (0% win rate, need >= 70%)

---

## 📊 Current Performance Gap

| Scenario | dap_json | simdjson | Gap | Winner |
|----------|----------|----------|-----|--------|
| Small JSON (<1KB) | 0.12 GB/s | 1.18 GB/s | **10x** | simdjson |
| Medium JSON (10-100KB) | 0.20 GB/s | 2.42 GB/s | **12x** | simdjson |
| Large JSON (1-10MB) | 0.17 GB/s | 2.26 GB/s | **13x** | simdjson |
| Deep Nested (500+ levels) | 0.18 GB/s | 2.02 GB/s | **11x** | simdjson |
| Wide Arrays (100K elements) | 0.17 GB/s | 1.19 GB/s | **7x** | simdjson |
| Number Heavy (90%) | 0.13 GB/s | 1.20 GB/s | **9x** | simdjson |
| String Heavy (90%) | 0.45 GB/s | 9.31 GB/s | **21x** | simdjson |

**Average:** dap_json 0.21 GB/s vs simdjson 2.5 GB/s = **~12x slower**

---

## 🔍 Root Cause Analysis

### Why We Are So Slow:

#### 1. **Using Legacy json-c Adapter API** ❌
```cpp
// Current benchmark code:
dap_json_t *l_json = dap_json_parse_buffer(a_json, a_size);
```

**Problem:** This goes through:
- `dap_json_parse_buffer()` → old json-tokener logic
- Character-by-character parsing
- **NO SIMD Stage 1 structural indexing**
- **NO optimized Stage 2 DOM building**
- Multiple allocations per value
- Hash table lookups for every key

**Result:** ~0.2 GB/s (worse than json-c!)

---

#### 2. **Not Using Our SIMD Stage 1** ❌

We have implemented:
- ✅ SSE2 tokenization (tested, working)
- ✅ AVX2 tokenization (tested, working, ~2 GB/s)
- ✅ AVX-512 tokenization (tested, working)
- ✅ ARM NEON tokenization (tested, working)

**But:** The public API `dap_json_parse_buffer()` doesn't use them!

**Stage 1 Performance:**
- Reference C: ~0.8 GB/s
- SSE2: ~1.2 GB/s
- **AVX2: ~2.0 GB/s** ← We have this!
- AVX-512: ~2.5 GB/s
- simdjson AVX2: ~3 GB/s (their Stage 1)

---

#### 3. **Not Using Optimized Stage 2** ❌

**simdjson Stage 2 architecture:**
- Tape-based format (linear array)
- Zero-copy strings (pointers into original buffer)
- Minimal allocations
- Cache-friendly traversal

**Our current Stage 2 (json-tokener):**
- Hash tables for every object
- Individual allocations for every value
- String copying
- Recursive parsing

**Impact:** Even if we had fast Stage 1, Stage 2 would bottleneck us.

---

## 🎯 Optimization Roadmap

### Phase 1: Integrate Existing SIMD Stage 1 (Quick Win)

**Goal:** Get from 0.2 GB/s → 1.5-2.0 GB/s

**Changes needed:**
1. Create new public API:
   ```c
   dap_json_t* dap_json_parse_fast(const char *json, size_t len);
   ```
2. Implementation:
   ```c
   dap_json_t* dap_json_parse_fast(const char *json, size_t len) {
       // Stage 1: SIMD structural indexing (AVX2/SSE2/NEON)
       dap_json_stage1_t *stage1 = dap_json_stage1_create(json, len);
       dap_json_stage1_run(stage1); // AUTO-DISPATCHES to best SIMD
       
       // Stage 2: Build DOM from indices
       dap_json_t *result = dap_json_stage2_build(stage1);
       
       dap_json_stage1_free(stage1);
       return result;
   }
   ```
3. Update benchmark to use `dap_json_parse_fast()`

**Expected result:** ~1.5-2.0 GB/s (AVX2 Stage 1 + current Stage 2)

---

### Phase 2: Optimize Stage 2 DOM Building (Major Win)

**Goal:** Get from 1.5-2.0 GB/s → 3-4 GB/s

**Optimizations:**

#### 2.1 Arena Allocator (Already Implemented!) ✅
- Batch allocations
- No per-value malloc overhead
- Fast deallocation (free entire arena)

#### 2.2 Zero-Copy Strings
```c
// Instead of copying:
char *str = strndup(json + pos, len);  // SLOW

// Use pointers:
dap_json_string_t str = {
    .data = json + pos,  // FAST - no copy
    .length = len,
    .needs_unescape = false
};
```

**Impact:** ~20-30% faster for string-heavy JSON

#### 2.3 Tape-Based Format (Like simdjson)
```c
// Linear array instead of tree
typedef struct {
    uint64_t *tape;      // [type, value, scope_length, ...]
    size_t tape_len;
    const char *strings; // Points into original JSON
} dap_json_tape_t;
```

**Benefits:**
- Cache-friendly sequential access
- No pointer chasing
- Predictable memory layout

#### 2.4 Fast Number Parsing (Lemire's algorithm)
```c
// Replace slow strtod() with fast_float
#include "fast_float.h"
double val = fast_float::from_chars(start, end);
```

**Impact:** ~10-30% faster for number-heavy JSON

#### 2.5 SIMD String Scanning
```c
// Find closing quote or backslash with SIMD
__m256i chunk = _mm256_loadu_si256((__m256i*)str);
__m256i quotes = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('"'));
__m256i backslash = _mm256_cmpeq_epi8(chunk, _mm256_set1_epi8('\\'));
uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(quotes, backslash));
```

**Impact:** ~2-3x faster string scanning

---

### Phase 3: Advanced Optimizations (Exceed simdjson)

**Goal:** Get from 3-4 GB/s → 5-6 GB/s (beat simdjson!)

#### 3.1 Parallel Stage 2 (Multi-core)
- Work-stealing queue
- Chase-Lev deque
- Parallel array/object processing

**Impact:** +30-50% on multi-core

#### 3.2 Predictive Parsing
- Learn DAP SDK JSON patterns
- Pre-allocate for common structures
- Fast paths for known schemas

**Impact:** +10-20% for repeated patterns

#### 3.3 JIT Compilation for JSONPath
- Compile queries to machine code
- Zero overhead for hot paths

**Impact:** 100-1000x faster queries

---

## 📈 Expected Performance After Each Phase

| Phase | Expected Throughput | vs simdjson | Status |
|-------|---------------------|-------------|--------|
| **Current** | 0.2 GB/s | 12x slower ❌ | Using json-tokener |
| **Phase 1** | 1.5-2.0 GB/s | 1.5x slower ⚠️ | Integrate SIMD Stage 1 |
| **Phase 2** | 3-4 GB/s | **Match/exceed** ✅ | Optimize Stage 2 |
| **Phase 3** | 5-6 GB/s | **2x faster** 🚀 | Advanced features |

---

## 🔧 Implementation Priority

### Immediate (This Week):
1. ✅ Create `dap_json_parse_fast()` API
2. ✅ Connect SIMD Stage 1 to public API
3. ✅ Update benchmark to use new API
4. ✅ Re-run competitive benchmark

**Expected:** 0.2 GB/s → 1.5 GB/s (7x improvement)

### Short-term (Next Week):
5. Implement zero-copy strings
6. Add fast_float number parsing
7. Optimize arena allocator usage
8. Add SIMD string scanning

**Expected:** 1.5 GB/s → 3 GB/s (another 2x)

### Medium-term (2-3 Weeks):
9. Implement tape-based format
10. Add parallel Stage 2
11. Predictive parsing

**Expected:** 3 GB/s → 5+ GB/s (exceed simdjson)

---

## 💡 Key Insights

### What simdjson Does Right:
1. **SIMD everywhere** - Stage 1 finds ALL structural indices in parallel
2. **Zero-copy** - Strings are pointers, not copies
3. **Tape format** - Linear memory, cache-friendly
4. **Minimal allocations** - One buffer for entire document
5. **Fast number parsing** - Custom float parser

### What We Can Do Better:
1. **Parallel Stage 2** - simdjson is single-threaded here
2. **Predictive parsing** - Learn DAP SDK patterns
3. **Streaming API** - Process chunks incrementally
4. **JSONPath JIT** - Compile queries to machine code
5. **JSON5/JSONC support** - More features than competitors

---

## 🎯 Conclusion

**Current bottleneck:** Using old json-tokener API instead of our SIMD Stage 1 + optimized Stage 2.

**Solution:** 
1. Integrate existing SIMD Stage 1 → 7x faster (immediate)
2. Optimize Stage 2 DOM building → another 2x (short-term)
3. Advanced features → exceed simdjson (medium-term)

**Timeline:** 
- Week 1: Match json-c/RapidJSON (~1.5 GB/s)
- Week 2-3: Match simdjson (~3 GB/s)
- Week 4+: Exceed simdjson (~5+ GB/s)

**The path is clear - we have all the pieces, just need to connect them! 🚀**
