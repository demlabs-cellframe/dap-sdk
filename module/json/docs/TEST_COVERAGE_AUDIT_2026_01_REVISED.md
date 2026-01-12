# JSON Test Coverage - Critical Audit (2026-01-12) - REVISED & DETAILED

## 🎯 Executive Summary

**Текущее покрытие:** ~70% (5100 LOC, 12 files, ~108 tests)  
**Целевое покрытие:** 95%+ (~10600 LOC, 34 files, ~328 tests)  
**Gap:** ~220 new tests, ~5500 LOC, 22 new files

**⚠️ КРИТИЧЕСКИЕ НАХОДКИ:**
1. **SIMD chunk boundaries** - tokens на границе 16/32/64 байт НЕ тестируются → CORRECTNESS RISK
2. **Memory DoS** - нет защиты от hash collision, billion laughs → SECURITY RISK
3. **Concurrency** - TSan никогда не запускался, thread-safety неизвестна → CRASH RISK
4. **Numeric precision** - denormal, overflow, precision loss не покрыты → DATA CORRUPTION RISK

---

## ✅ Существующие Тесты - Детальный Анализ

### 1. `test_unicode.c` (878 LOC, 25 tests) - **EXCELLENT** ✅
**Coverage: 95%** | **Quality: High**

#### Что покрыто:
- ✅ Basic escapes (`\n`, `\t`, `\r`, `\"`, `\\`, `\/`, `\b`, `\f`)
- ✅ Unicode escapes (`\uXXXX`)
- ✅ Surrogate pairs (`\uD800\uDC00` → U+10000)
- ✅ UTF-8 multibyte (Russian, Chinese, emoji)
- ✅ Invalid UTF-8 (overlong encoding, unpaired surrogates)
- ✅ BOM handling (UTF-8, UTF-16LE/BE, UTF-32LE/BE)
- ✅ Security: unpaired surrogates, CESU-8, MUTF-8
- ✅ Unicode normalization (NFC vs NFD)
- ✅ Serialization round-trip

#### Пропущено (minor):
- ⚠️ Unicode в КЛЮЧАХ объекта (`{"😀": "value"}`)
- ⚠️ Case-sensitive keys с Unicode (`{"Кириллица": 1, "кириллица": 2}`)
- ⚠️ Unicode normalization equivalence (`café` NFC != NFD, но `==` семантически?)

**Вердикт:** Отличный тест. Minor gaps можно закрыть в `test_string_edge_cases.c`.

---

### 2. `test_numeric_edge_cases.c` (515 LOC, 13 tests) - **GOOD** ✅
**Coverage: 80%** | **Quality: Good**

#### Что покрыто:
- ✅ INT64_MIN/MAX (-9223372036854775808, 9223372036854775807)
- ✅ UINT64_MAX (18446744073709551615)
- ✅ uint256 boundaries (max, zero)
- ✅ Float boundaries (DBL_MIN, DBL_MAX)
- ✅ Scientific notation (`1e-10`, `1e+20`)
- ✅ Special values (NaN, Infinity handling with `-ffast-math`)
- ✅ Zero values (0, 0.0, -0)
- ✅ Precision test (basic)

#### Пропущено (critical):
- ❌ **Denormalized floats** (subnormal: `2.225e-308` near DBL_MIN)
- ❌ **Negative zero distinction** (`-0.0` vs `+0.0` IEEE 754)
- ❌ **Overflow boundary** (`9223372036854775808` = INT64_MAX+1 → double?)
- ❌ **Very long numbers** (`123456789...` > 100 digits, > 1000 digits)
- ❌ **Scientific extremes** (`1e+400` overflow, `1e-400` underflow)
- ❌ **Precision loss > 2^53** (`9007199254740993` loses precision in double)
- ❌ **Mixed format** (`123.456e789`)
- ❌ **Leading zeros** (`0001`, `00.5`) - RFC requires rejection
- ❌ **Multiple decimals** (`1.2.3`) - invalid
- ❌ **Multiple signs** (`--5`, `+-3`) - invalid
- ❌ **Empty exponent** (`1e`, `1e+`) - invalid
- ❌ **Hex/octal** (`0x10`, `0o10`) - invalid in strict JSON

**Вердикт:** Нужен `test_numeric_edge_cases_extended.c` (+12 tests).

---

### 3. `test_invalid_json.c` (430 LOC, 15 tests) - **GOOD** ✅
**Coverage: 90%** | **Quality: Good**

#### Что покрыто:
- ✅ Trailing commas (reject)
- ✅ Missing commas
- ✅ Unclosed strings/objects/arrays
- ✅ Comments (reject in strict mode)
- ✅ Single quotes (reject)
- ✅ Unquoted keys (reject)
- ✅ Missing colon
- ✅ Extra/mismatched brackets
- ✅ Bare values (no container)
- ✅ Duplicate keys
- ✅ Empty input

#### Пропущено (minor):
- ⚠️ Invalid literals case (`True`, `FALSE`, `NULL`)
- ⚠️ Partial literals (`tru`, `fals`, `nul`)
- ⚠️ Literals with whitespace (`t rue`)
- ⚠️ Whitespace in numbers (`1 2 3`)
- ⚠️ Whitespace in escapes (`\u 00A9`)

**Вердикт:** Хороший тест. Minor gaps → `test_literal_edge_cases.c`, `test_whitespace_edge_cases.c`.

---

### 4. `test_boundary_conditions.c` (456 LOC, 8 tests) - **WEAK** ⚠️
**Coverage: 60%** | **Quality: Medium**

#### Что покрыто:
- ✅ Very long string (100KB)
- ✅ Megabyte string (1MB)
- ✅ Deep nesting (1000 levels)
- ✅ Extremely deep (5000 levels)
- ✅ Huge array (100K elements)
- ✅ Huge object (10K keys)
- ✅ Empty keys (`{"": "value"}`)
- ✅ Mixed large content

#### Пропущено (critical):
- ❌ **Empty containers** stress test (`[]`, `{}`, nested empty)
- ❌ **Single-element** (`[1]`, `{"a":1}`)
- ❌ **Alternating nesting** (`[{[{[...]}]}]`) - pathological
- ❌ **Wide objects** (10K keys in SINGLE level, не вложенность)
- ❌ **Array of huge objects** (`[{1K keys}, {1K keys}, ...]`)
- ❌ **Very long keys** (> 1KB, > 1MB key name)
- ❌ **Numeric keys** (`{"0": "a", "1": "b"}` не массив!)
- ❌ **Special char keys** (`{" ": "space"}`)
- ❌ **Nested same keys** (`{"a": {"a": {"a": 1}}}`)

**Вердикт:** Слабый. Нужны `test_array_edge_cases.c`, `test_object_edge_cases.c`.

---

### 5. SIMD Tests (1650+ LOC, 37+ tests) - **GOOD but INCOMPLETE** ⚠️
**Coverage: 85%** | **Quality: Good**

#### Что покрыто:
- ✅ SSE2/AVX2/AVX-512/NEON correctness (vs reference)
- ✅ String spanning chunks (`test_simd_string_spanning.c`)
- ✅ Unicode at boundaries (`test_unicode_escape_boundary.c`)
- ✅ Multi-arch selection (`test_manual_arch_selection.c`)

#### Пропущено (CRITICAL):
- ❌ **Structural chars at boundary** (`{`, `}`, `[`, `]` at 16/32/64-byte edge)
- ❌ **Escape split** (`\uXX|XX` across chunk)
- ❌ **UTF-8 multibyte split** (`0xF0 0x9F|0x98 0x80`)
- ❌ **Number across chunks** (`12345|67890`)
- ❌ **Literal split** (`tru|e`, `fal|se`, `nul|l`)
- ❌ **Nested structures at boundary**
- ❌ **Very long strings** (> 1MB, multiple chunks)

**Вердикт:** Нужен `test_simd_chunk_boundaries.c` (+8 CRITICAL tests).

---

## ❌ КРИТИЧЕСКИЕ ПРОБЕЛЫ - Tier 1 (Security & Correctness)

### 1. SIMD Chunk Boundary Edge Cases 🔴 CRITICAL
**File:** `tests/unit/json/test_simd_chunk_boundaries.c`  
**Tests:** 8 critical  
**Priority:** CRITICAL  
**Impact:** Parser correctness on real-world data

**Missing tests:**
1. Structural characters at exact 16/32/64-byte boundary
2. Escape sequences split across chunk (`\uXXXX` → `\uXX|XX`)
3. UTF-8 3-4 byte character split
4. Number parsing across chunks
5. Literals split (`tru|e`)
6. Object/array nesting at boundaries
7. Very long strings > 1MB
8. Token array overflow (too many tokens per chunk)

**Why critical:** SIMD parser processes data in 16/32/64-byte chunks. If token starts in chunk N and ends in chunk N+1, parser might:
- Miss token
- Parse incorrect value
- Crash

**Current gap:** `test_simd_string_spanning.c` covers ONLY strings. All other token types NOT tested.

---

### 2. Memory DoS Protection 🔴 CRITICAL
**File:** `tests/unit/json/test_memory_dos.c`  
**Tests:** 7 critical security  
**Priority:** CRITICAL  
**Impact:** Production DoS vulnerability

**Missing tests:**
1. **Hash collision attack** - craft keys with same hash → O(n²) lookup
2. **Memory exhaustion** - huge JSON > RAM
3. **Stack exhaustion** - deep recursion → stack overflow
4. **Billion laughs** - nested duplicates: `[[[[[...]]]]]` exponential
5. **Algorithmic complexity** - pathological O(n²) inputs
6. **Arena allocator exhaustion**
7. **String pool flooding**

**Attack scenario:**
```json
{
  "aaaaaaaaaa": 1,
  "aaaaaaaaab": 2,
  ...  // 10,000 keys, all with same hash
}
```
→ Object lookup becomes O(n²) → 10 seconds parse instead of 0.001s

**Why critical:** Single malicious JSON crashes server.

---

### 3. Concurrency & Thread-Safety 🔴 CRITICAL
**File:** `tests/unit/json/test_concurrency.c`  
**Tests:** 8 (with TSan)  
**Priority:** CRITICAL  
**Impact:** Crashes in multi-threaded applications

**Missing tests:**
1. ThreadSanitizer (TSan) - NEVER run!
2. Concurrent parsing (multiple threads)
3. Arena allocator races (bump pointer atomic?)
4. String pool races (hash table thread-safe?)
5. Global state races (`dap_cpu_arch_set` thread-local?)
6. Read-write contention
7. Deadlock scenarios
8. Memory ordering (acquire/release)

**Likely bugs:**
- Arena bump pointer not atomic → corruption
- String pool concurrent insert → crash
- Global CPU arch override → race condition

**Why critical:** Parser used in `cellframe-node` (multi-threaded). Race = crash under load.

---

### 4. Numeric Edge Cases Extended 🟡 HIGH
**File:** `tests/unit/json/test_numeric_edge_cases_extended.c`  
**Tests:** 12  
**Priority:** HIGH  
**Impact:** Data corruption in financial/scientific apps

**Missing tests:**
1. Denormalized floats (subnormal near 0)
2. Negative zero (-0.0 vs +0.0)
3. Overflow boundary (INT64_MAX+1)
4. Very long numbers (> 100 digits)
5. Scientific extremes (e+400, e-400)
6. Precision loss (> 2^53 in double)
7. Mixed format (123.456e789)
8. Leading zeros (0001 - invalid)
9. Multiple decimals (1.2.3 - invalid)
10. Multiple signs (--5 - invalid)
11. Empty exponent (1e - invalid)
12. Hex/octal (0x10 - invalid)

**Impact:** Financial calculations with wrong precision = money loss.

---

## 📋 Recommended Implementation Plan

### Phase 1: Critical Security (2-3 days) 🔴
**Files:**
1. `test_memory_dos.c` (7 tests) - DoS protection
2. `test_simd_chunk_boundaries.c` (8 tests) - SIMD correctness
3. `test_concurrency.c` (8 tests) - Thread-safety + TSan

**Total:** 23 critical tests  
**Justification:** Security and correctness BEFORE optimization.

---

### Phase 2: Edge Case Coverage (3-4 days) 🟡
**Files:**
1. `test_numeric_edge_cases_extended.c` (12 tests)
2. `test_string_edge_cases.c` (11 tests)
3. `test_whitespace_edge_cases.c` (7 tests)
4. `test_array_edge_cases.c` (8 tests)
5. `test_object_edge_cases.c` (9 tests)
6. `test_literal_edge_cases.c` (5 tests)
7. `test_serialization_round_trip.c` (8 tests)
8. `test_platform_specific.c` (6 tests)

**Total:** 66 tests  
**Justification:** Comprehensive correctness.

---

### Phase 3: Features (2-3 days) 🟢
**Files:**
1. `test_json5_features.c` (15 tests) - trailing commas, comments, hex
2. `test_jsonc_comments.c` (5 tests)
3. `test_streaming_api.c` (10 tests) - chunk parsing
4. `test_jsonpath.c` (12 tests) - query language
5. `test_json_schema.c` (10 tests) - validation
6. `test_api_mutations.c` (10 tests) - update, delete, merge

**Total:** 62 tests  
**Justification:** Feature parity with competitors.

---

### Phase 4: Production (Continuous) 🔵
**Files:**
1. `test_real_world_data.c` (18 tests) - twitter, citm, canada
2. `test_perf_edge_cases.c` (10 tests) - heavily escaped
3. `test_error_reporting.c` (7 tests) - position accuracy
4. `tests/fuzzing/json_fuzzer.c` (AFL/libFuzzer 24/7)

**Total:** 35 tests + fuzzing infrastructure  
**Justification:** Real-world validation.

---

## 📊 Statistics Summary

| Category | Existing | Planned | Total | Priority |
|----------|----------|---------|-------|----------|
| **CRITICAL (Security)** | 0 | 23 | 23 | 🔴 |
| Unicode | 25 | 0 | 25 | - |
| Numerics | 13 | 12 | 25 | 🟡 |
| Strings | 0 | 11 | 11 | 🟡 |
| Whitespace | 0 | 7 | 7 | 🟢 |
| Arrays | 2 | 8 | 10 | 🟢 |
| Objects | 2 | 9 | 11 | 🟢 |
| Literals | 0 | 5 | 5 | 🟢 |
| Invalid JSON | 15 | 0 | 15 | - |
| Boundaries | 8 | 0 | 8 | - |
| SIMD | 37 | 8 | 45 | 🔴 |
| Serialization | 1 | 8 | 9 | 🟢 |
| Platform | 0 | 6 | 6 | 🟢 |
| JSON5 | 0 | 15 | 15 | 🟡 |
| JSONC | 0 | 5 | 5 | 🟢 |
| Streaming | 0 | 10 | 10 | 🟡 |
| JSONPath | 0 | 12 | 12 | 🟢 |
| Schema | 0 | 10 | 10 | 🟢 |
| Perf edge | 0 | 10 | 10 | 🟡 |
| Concurrency | 0 | 8 | 8 | 🔴 |
| Errors | 0 | 7 | 7 | 🟢 |
| Real-world | 0 | 18 | 18 | 🟡 |
| API mutations | 5 | 10 | 15 | 🟢 |
| **TOTAL** | **108** | **220** | **328** | - |

---

**Audit Date:** 2026-01-12  
**Revision:** 2 (detailed analysis)  
**Next Steps:** Implement Phase 1 (Critical Security) immediately.

