# Phase 1.4: SIMD Architecture Design for Stage 1 Tokenization

**Date:** 2025-01-08  
**Version:** 1.0  
**Target:** 3-10x speedup over reference C implementation  

---

## 🎯 Goals

1. **Performance Targets:**
   - SSE2: 1.5-2 GB/s (baseline SIMD)
   - AVX2: 3-4 GB/s (mainstream target)
   - AVX-512: 6-8 GB/s (high-end target)
   - ARM NEON: 2-3 GB/s (mobile/embedded)

2. **Correctness:** 100% compatibility with reference implementation
3. **Portability:** x86 (SSE2+, AVX2, AVX-512) + ARM (NEON, SVE)
4. **Maintainability:** Clean separation of SIMD backends

---

## 📐 Architecture Overview

### Core Principle: SIMD Character Classification

**Reference approach (scalar):**
```c
for (size_t i = 0; i < len; i++) {
    char c = input[i];
    if (is_structural(c)) { /* add to indices */ }
    else if (is_whitespace(c)) { /* skip */ }
    else if (c == '"') { /* skip string */ }
    // ... etc
}
```

**SIMD approach:**
```c
// Process 16/32/64 bytes at once
for (size_t i = 0; i < len; i += CHUNK_SIZE) {
    __m256i chunk = _mm256_loadu_si256((const __m256i*)(input + i));
    
    // Classify ALL bytes in parallel
    uint32_t structural_mask = classify_structural(chunk);
    uint32_t quote_mask = classify_quotes(chunk);
    uint32_t escape_mask = classify_escapes(chunk);
    
    // Process found characters
    process_structural(structural_mask, i);
    process_strings(quote_mask, escape_mask, i);
}
```

---

## 🔧 SIMD Primitives

### 1. Character Classification (Parallel)

#### Structural Characters: `{`, `}`, `[`, `]`, `:`, `,`

**AVX2 Implementation:**
```c
static inline uint32_t simd_find_structural_avx2(__m256i chunk) {
    // Create comparison vectors
    const __m256i l_brace_open  = _mm256_set1_epi8('{');
    const __m256i l_brace_close = _mm256_set1_epi8('}');
    const __m256i l_bracket_open  = _mm256_set1_epi8('[');
    const __m256i l_bracket_close = _mm256_set1_epi8(']');
    const __m256i l_colon = _mm256_set1_epi8(':');
    const __m256i l_comma = _mm256_set1_epi8(',');
    
    // Compare chunk against each structural char
    __m256i cmp1 = _mm256_cmpeq_epi8(chunk, l_brace_open);
    __m256i cmp2 = _mm256_cmpeq_epi8(chunk, l_brace_close);
    __m256i cmp3 = _mm256_cmpeq_epi8(chunk, l_bracket_open);
    __m256i cmp4 = _mm256_cmpeq_epi8(chunk, l_bracket_close);
    __m256i cmp5 = _mm256_cmpeq_epi8(chunk, l_colon);
    __m256i cmp6 = _mm256_cmpeq_epi8(chunk, l_comma);
    
    // OR all comparisons
    __m256i structural = _mm256_or_si256(cmp1, cmp2);
    structural = _mm256_or_si256(structural, cmp3);
    structural = _mm256_or_si256(structural, cmp4);
    structural = _mm256_or_si256(structural, cmp5);
    structural = _mm256_or_si256(structural, cmp6);
    
    // Convert to bitmask
    return (uint32_t)_mm256_movemask_epi8(structural);
}
```

#### Whitespace: ` `, `\t`, `\n`, `\r`

**AVX2 Implementation:**
```c
static inline uint32_t simd_find_whitespace_avx2(__m256i chunk) {
    const __m256i l_space = _mm256_set1_epi8(' ');
    const __m256i l_tab   = _mm256_set1_epi8('\t');
    const __m256i l_nl    = _mm256_set1_epi8('\n');
    const __m256i l_cr    = _mm256_set1_epi8('\r');
    
    __m256i ws1 = _mm256_cmpeq_epi8(chunk, l_space);
    __m256i ws2 = _mm256_cmpeq_epi8(chunk, l_tab);
    __m256i ws3 = _mm256_cmpeq_epi8(chunk, l_nl);
    __m256i ws4 = _mm256_cmpeq_epi8(chunk, l_cr);
    
    __m256i whitespace = _mm256_or_si256(ws1, ws2);
    whitespace = _mm256_or_si256(whitespace, ws3);
    whitespace = _mm256_or_si256(whitespace, ws4);
    
    return (uint32_t)_mm256_movemask_epi8(whitespace);
}
```

#### Quotes: `"`

**AVX2 Implementation:**
```c
static inline uint32_t simd_find_quotes_avx2(__m256i chunk) {
    const __m256i l_quote = _mm256_set1_epi8('"');
    __m256i quotes = _mm256_cmpeq_epi8(chunk, l_quote);
    return (uint32_t)_mm256_movemask_epi8(quotes);
}
```

#### Backslashes (Escapes): `\`

**AVX2 Implementation:**
```c
static inline uint32_t simd_find_backslash_avx2(__m256i chunk) {
    const __m256i l_backslash = _mm256_set1_epi8('\\');
    __m256i escapes = _mm256_cmpeq_epi8(chunk, l_backslash);
    return (uint32_t)_mm256_movemask_epi8(escapes);
}
```

---

## 🧮 String Handling (Critical Path)

### Challenge: Escaped Quotes

Strings can contain:
- `"Hello"` - simple string
- `"Say \"hello\""` - escaped quote INSIDE string
- `"Path\\to\\file"` - escaped backslash

**Problem:** Need to distinguish:
- `"` = end of string
- `\"` = escaped quote (NOT end of string)
- `\\"` = escaped backslash + end of string

### Solution: Backslash Tracking

**Algorithm (simdjson approach):**

```c
// Find all quotes and backslashes in chunk
uint32_t quote_mask = simd_find_quotes_avx2(chunk);
uint32_t backslash_mask = simd_find_backslash_avx2(chunk);

// Compute odd-length sequences of backslashes BEFORE each quote
// A quote preceded by ODD number of backslashes is escaped
// A quote preceded by EVEN number of backslashes is NOT escaped

uint32_t escaped_quotes = compute_escaped_quotes(backslash_mask, quote_mask);
uint32_t real_quotes = quote_mask & ~escaped_quotes;

// Toggle in_string state at EACH real quote
while (real_quotes) {
    int bit = __builtin_ctz(real_quotes);  // Count trailing zeros
    in_string ^= 1;
    real_quotes &= (real_quotes - 1);  // Clear lowest bit
}
```

**Detailed Implementation:**

```c
static inline uint32_t compute_escaped_quotes_avx2(
    uint32_t backslash_mask,
    uint32_t quote_mask
) {
    // For each quote, check if preceded by odd number of backslashes
    // This requires looking at the backslash sequence BEFORE the quote
    
    // Example:
    // Input:  "ab\"cd\\"ef"
    // Quotes:    ^       ^   (positions 0, 9)
    // Backsl:      ^    ^    (positions 3, 7)
    //
    // Quote at 0: 0 backslashes before → real quote (string start)
    // Quote at 9: 2 backslashes before (even) → real quote (string end)
    //
    // Input:  "ab\"cd"
    // Quotes:    ^    ^
    // Backsl:      ^
    //
    // Quote at 0: 0 backslashes → real quote (string start)
    // Quote at 6: 1 backslash → escaped quote (not string end)
    
    uint32_t escaped = 0;
    uint32_t backslash_run = 0;
    
    for (int bit = 0; bit < 32; bit++) {
        if (backslash_mask & (1U << bit)) {
            backslash_run++;
        } else if (quote_mask & (1U << bit)) {
            // Quote found: check if escaped
            if (backslash_run % 2 == 1) {
                escaped |= (1U << bit);
            }
            backslash_run = 0;
        } else {
            backslash_run = 0;
        }
    }
    
    return escaped;
}
```

**Optimization:** Carry backslash state across chunks!

```c
typedef struct {
    bool in_string;
    uint32_t prev_backslash_run;  // Carry from previous chunk
} simd_string_state_t;
```

---

## 📊 Number Detection (Value Tokens)

### Characters: `-`, `0-9`, `.`, `e`, `E`

**AVX2 Implementation:**
```c
static inline uint32_t simd_find_number_chars_avx2(__m256i chunk) {
    // Digits: '0' - '9' (0x30 - 0x39)
    const __m256i l_zero = _mm256_set1_epi8('0');
    const __m256i l_nine = _mm256_set1_epi8('9');
    __m256i ge_zero = _mm256_cmpgt_epi8(chunk, _mm256_sub_epi8(l_zero, _mm256_set1_epi8(1)));
    __m256i le_nine = _mm256_cmpgt_epi8(_mm256_add_epi8(l_nine, _mm256_set1_epi8(1)), chunk);
    __m256i digits = _mm256_and_si256(ge_zero, le_nine);
    
    // Special chars: '-', '.', 'e', 'E'
    const __m256i l_minus = _mm256_set1_epi8('-');
    const __m256i l_dot = _mm256_set1_epi8('.');
    const __m256i l_e_lower = _mm256_set1_epi8('e');
    const __m256i l_e_upper = _mm256_set1_epi8('E');
    
    __m256i special1 = _mm256_cmpeq_epi8(chunk, l_minus);
    __m256i special2 = _mm256_cmpeq_epi8(chunk, l_dot);
    __m256i special3 = _mm256_cmpeq_epi8(chunk, l_e_lower);
    __m256i special4 = _mm256_cmpeq_epi8(chunk, l_e_upper);
    
    __m256i specials = _mm256_or_si256(special1, special2);
    specials = _mm256_or_si256(specials, special3);
    specials = _mm256_or_si256(specials, special4);
    
    // Combine digits + specials
    __m256i number_chars = _mm256_or_si256(digits, specials);
    return (uint32_t)_mm256_movemask_epi8(number_chars);
}
```

**Validation:** Still use reference `s_scan_number_token()` for validation!

**Why?** Number validation is complex (no leading zeros, required digits after `.`, etc.). SIMD is for **detection**, not validation.

### Literal Detection: `true`, `false`, `null`

**Strategy:** SIMD cannot efficiently match multi-character sequences like "true". Use scalar approach:

```c
// After finding non-structural, non-whitespace, non-quote, non-number char
if (input[pos] == 't' || input[pos] == 'f' || input[pos] == 'n') {
    // Potential literal - use reference s_scan_literal_token()
    // This is rare in typical JSON, so scalar is acceptable
}
```

**Why scalar?** Literals are typically rare (<5% of tokens in real JSON). SIMD overhead for 4-5 byte matches not worth it.

---

## 🏗️ Implementation Strategy

### Directory Structure

```
module/json/src/stage1/
├── dap_json_stage1_ref.c          # Reference C (baseline)
├── dap_json_stage1_dispatch.c     # CPU detection & dispatch
├── arch/
│   ├── x86/
│   │   ├── dap_json_stage1_sse2.c     # SSE2 (16 bytes/iter)
│   │   ├── dap_json_stage1_avx2.c     # AVX2 (32 bytes/iter)
│   │   └── dap_json_stage1_avx512.c   # AVX-512 (64 bytes/iter)
│   └── arm/
│       ├── dap_json_stage1_neon.c     # ARM NEON (16 bytes/iter)
│       └── dap_json_stage1_sve.c      # ARM SVE (scalable)
└── include/
    └── dap_json_stage1_simd.h     # SIMD primitives & dispatch
```

### Dispatch Mechanism

```c
// In dap_json_stage1_dispatch.c

typedef int (*dap_json_stage1_fn)(dap_json_stage1_t *a_stage1);

static dap_json_stage1_fn s_select_implementation(void) {
    #ifdef __x86_64__
    if (dap_cpu_has_avx512()) {
        log_it(L_INFO, "Using AVX-512 implementation");
        return dap_json_stage1_run_avx512;
    }
    if (dap_cpu_has_avx2()) {
        log_it(L_INFO, "Using AVX2 implementation");
        return dap_json_stage1_run_avx2;
    }
    if (dap_cpu_has_sse2()) {
        log_it(L_INFO, "Using SSE2 implementation");
        return dap_json_stage1_run_sse2;
    }
    #endif
    
    #ifdef __aarch64__
    if (dap_cpu_has_sve()) {
        log_it(L_INFO, "Using ARM SVE implementation");
        return dap_json_stage1_run_sve;
    }
    if (dap_cpu_has_neon()) {
        log_it(L_INFO, "Using ARM NEON implementation");
        return dap_json_stage1_run_neon;
    }
    #endif
    
    log_it(L_INFO, "Using reference C implementation");
    return dap_json_stage1_run_ref;
}

// Public API wrapper
int dap_json_stage1_run(dap_json_stage1_t *a_stage1) {
    static dap_json_stage1_fn s_impl = NULL;
    static int s_once = 0;
    
    if (!s_once) {
        s_impl = s_select_implementation();
        __atomic_store_n(&s_once, 1, __ATOMIC_RELEASE);
    }
    
    return s_impl(a_stage1);
}
```

---

## 🧪 Testing Strategy

### 1. Correctness Tests

**Test:** SIMD output == Reference output on ALL inputs

```c
// tests/unit/json/test_stage1_simd_correctness.c

static bool s_test_simd_vs_reference(const char *json) {
    // Parse with reference
    dap_json_stage1_t *ref = dap_json_stage1_init_ref(json, strlen(json));
    dap_json_stage1_run_ref(ref);
    
    size_t ref_count;
    const dap_json_token_t *ref_tokens = dap_json_stage1_get_indices(ref, &ref_count);
    
    // Parse with SIMD
    dap_json_stage1_t *simd = dap_json_stage1_init(json, strlen(json));
    dap_json_stage1_run(simd);  // Auto-dispatch to SIMD
    
    size_t simd_count;
    const dap_json_token_t *simd_tokens = dap_json_stage1_get_indices(simd, &simd_count);
    
    // Compare
    dap_assert(ref_count == simd_count, "Token count mismatch");
    
    for (size_t i = 0; i < ref_count; i++) {
        dap_assert(ref_tokens[i].position == simd_tokens[i].position, "Position mismatch");
        dap_assert(ref_tokens[i].length == simd_tokens[i].length, "Length mismatch");
        dap_assert(ref_tokens[i].type == simd_tokens[i].type, "Type mismatch");
        dap_assert(ref_tokens[i].character == simd_tokens[i].character, "Character mismatch");
    }
    
    dap_json_stage1_free(ref);
    dap_json_stage1_free(simd);
    return true;
}
```

**Test Cases:**
- Empty object: `{}`
- Simple array: `[1, 2, 3]`
- Strings with escapes: `{"key": "say \"hello\""}`
- Numbers: `{"pi": 3.14159, "e": 2.71828e0}`
- Literals: `{"active": true, "data": null}`
- Nested: Complex structures
- Edge cases: Long strings, deep nesting

### 2. Performance Benchmarks

```c
// tests/unit/json/test_stage1_simd_performance.c

static void s_benchmark_implementation(
    const char *name,
    dap_json_stage1_fn impl,
    const char *json,
    size_t json_len,
    int iterations
) {
    uint64_t total_time = 0;
    
    for (int i = 0; i < iterations; i++) {
        dap_json_stage1_t *stage1 = dap_json_stage1_init(json, json_len);
        
        uint64_t start = dap_time_now();
        impl(stage1);
        uint64_t end = dap_time_now();
        
        total_time += (end - start);
        dap_json_stage1_free(stage1);
    }
    
    double avg_time_ms = (double)total_time / iterations / 1000.0;
    double throughput_gbps = (json_len * iterations) / (double)total_time / 1000.0;
    
    log_it(L_INFO, "%s: %.2f ms/iter, %.2f GB/s",
           name, avg_time_ms, throughput_gbps);
}
```

---

## 📈 Expected Performance

### x86-64 (Intel Core i7/i9, AMD Ryzen)

| Implementation | Throughput | vs Reference |
|---------------|------------|--------------|
| Reference C   | 0.8-1.2 GB/s | 1.0x (baseline) |
| SSE2          | 1.5-2.0 GB/s | ~2x |
| AVX2          | 3.0-4.0 GB/s | ~3-4x |
| AVX-512       | 6.0-8.0 GB/s | ~6-8x |

### ARM (Apple M1/M2, Snapdragon)

| Implementation | Throughput | vs Reference |
|---------------|------------|--------------|
| Reference C   | 0.8-1.2 GB/s | 1.0x (baseline) |
| NEON          | 2.0-3.0 GB/s | ~2-3x |
| SVE           | 4.0-5.0 GB/s | ~4-5x |

---

## 🔜 Next Steps (Implementation Order)

1. **✅ Phase 1.4.1: AVX2 Implementation** (most common, ~70% of desktops)
2. **Phase 1.4.2: SSE2 Implementation** (compatibility, 100% of x86-64)
3. **Phase 1.4.3: AVX-512 Implementation** (high-end, future-proof)
4. **Phase 1.4.4: ARM NEON Implementation** (mobile, Apple Silicon)
5. **Phase 1.4.5: CPU Dispatch** (runtime selection)
6. **Phase 1.4.6: Correctness Tests** (SIMD vs reference)
7. **Phase 1.4.7: Performance Benchmarks** (measure speedup)

---

## 📚 References

- simdjson: https://github.com/simdjson/simdjson
- Intel Intrinsics Guide: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
- ARM NEON Intrinsics: https://developer.arm.com/architectures/instruction-sets/intrinsics/
- Wojciech Muła's SIMD blog: http://0x80.pl/

---

**Status:** Architecture designed, ready for implementation!  
**Next:** Phase 1.4.1 - AVX2 Implementation

