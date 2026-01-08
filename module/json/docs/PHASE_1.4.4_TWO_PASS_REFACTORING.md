# Phase 1.4.4: Two-Pass SIMD Architecture Refactoring

**Status:** Design Phase  
**Priority:** 🔴 CRITICAL  
**Created:** 2026-01-08  
**Authors:** AI Assistant (Claude Sonnet 4.5)

---

## 📋 Executive Summary

Критический архитектурный рефакторинг всех SIMD implementations (SSE2, AVX2, AVX-512, ARM NEON) для решения фундаментальной проблемы: **токены добавляются в неправильном порядке** при multi-chunk processing когда value tokens (numbers/literals) пересекают chunk boundaries.

**Решение:** Переход от "Single-Pass Hybrid SIMD" к "Two-Pass SIMD Architecture":
- **Pass 1 (SIMD):** Найти ВСЕ structural characters + strings
- **Pass 2 (Scalar):** Извлечь numbers/literals между tokens из Pass 1

---

## 🔍 Problem Analysis

### Root Cause

Текущая архитектура пытается обработать structural chars, strings, numbers и literals в **одном** SIMD проходе. Когда value token extends beyond chunk boundary:

1. **Опция A:** Early `continue` → пропускаем необработанные structural chars → **токены в неправильном порядке**
2. **Опция B:** Ограничить skip до chunk boundary → **дублирование токенов** в следующем chunk
3. **Опция C:** Сложный state tracking "что обработано" → **complexity explosion**

### Concrete Example

Input: `[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]` (52 bytes)

**Chunk@16-31:**
```
Position: 16  17  18  19  20  21  22  23  24  25  26  27  28  29  30  31
Content:  ,   1   0   ,   1   1   ,   1   2   ,   1   3   ,   1   4   ,
```

Проблема:
- Structural chars: `,` at positions 16, 19, 22, 25, 28, 31
- Number `"10"` at 17-18 (inside chunk)
- Number `"11"` at 20-21 (inside chunk)
- Number `"12"` at 23-24 (inside chunk)
- Number `"13"` at 26-27 (inside chunk)
- Number `"14"` at 29-30 (inside chunk)
- Number `"15"` at **32-33** (**extends beyond chunk!**)

Если делаем `goto next_chunk` при обнаружении `"15"`, мы пропускаем structural char `,` at position 31!

**Result:** Tokens добавляются как:
```
[..., {pos:28, type:STRUCTURAL}, {pos:29, type:NUMBER}, {pos:32, type:STRUCTURAL}, {pos:34, type:NUMBER}, ...]
       ^^^^ correct order           ^^^^ WRONG! pos 34 < pos 32
```

---

## ✅ Solution: Two-Pass Architecture

### Philosophy

> **Разделение ответственности:** SIMD для fast bulk detection, Scalar для precise extraction

### Pass 1: SIMD Structural Indexing

**Responsibility:** Найти **ТОЛЬКО** structural characters + strings

**Operations:**
1. SIMD classify: structural vs non-structural
2. Extract bitmask всех structural positions
3. Add structural tokens to indices
4. Detect strings using `s_compute_escaped_quotes()` и **сразу добавить string tokens**
5. Track `in_string` state для корректного skip string content

**Why strings in Pass 1?**
- Strings содержат escaped characters и structural-like chars внутри
- Невозможно корректно определить "что между structural chars" без знания где strings
- String detection уже есть в SIMD loop (quote detection, escape handling)
- **Проще и правильнее:** обработать strings сразу в Pass 1

**What NOT to do:**
- ❌ НЕ обрабатывать numbers/literals в Pass 1
- ❌ НЕ делать early continue/break из loop
- ❌ НЕ пытаться извлекать numbers/literals в SIMD loop

**Output:** Structural + String indices array

**Complexity:** O(n/SIMD_WIDTH) - один полный проход

### Pass 2: Scalar Value Extraction

**Responsibility:** Извлечь numbers и literals **между** structural chars/strings

**Operations:**
1. Iterate через ВСЕ tokens из Pass 1 (structural + strings)
2. Для каждой пары consecutive tokens:
   - Scan region между ними
   - Use **ТОЛЬКО** `scan_number_ref()` и `scan_literal_ref()`
   - Add number/literal tokens (гарантированно в правильном порядке!)
3. Обработать tail: от последнего token до конца документа

**Important:** Pass 2 **НЕ сканирует strings** - они уже обработаны в Pass 1!

**Advantages:**
- ✅ Токены ВСЕГДА в правильном порядке (sorted by position)
- ✅ НЕТ проблемы с chunk boundaries
- ✅ НЕТ дублирования
- ✅ Simple state management
- ✅ Strings уже обработаны, не нужно повторно detect quotes

**Complexity:** O(m) where m = non-whitespace bytes между tokens (обычно m << n)

---

## 📐 Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                    INPUT JSON DOCUMENT                      │
│  [1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]     │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                     PASS 1: SIMD LOOP                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Chunk 0-15:   [1,2,3,4,5,6,7,8  (SSE2: 16 bytes)   │   │
│  │   Structural: [ , , , , , , ,   (8 positions)      │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Chunk 16-31:  ,9,10,11,12,13,14                    │   │
│  │   Structural: , , , , , ,       (7 positions)      │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Chunk 32-47:  ,15,16,17,18,19,2                    │   │
│  │   Structural: , , , , , ,       (6 positions)      │   │
│  └──────────────────────────────────────────────────────┘   │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Tail 48-51:   0]                                    │   │
│  │   Structural: ]                 (1 position)       │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
    Structural Indices Array (21 structural tokens)
    Positions: [0, 2, 4, 6, 8, 10, 12, 14, 16, 19, 22, ...]
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                   PASS 2: SCALAR SCAN                       │
│  ┌──────────────────────────────────────────────────────┐   │
│  │ Scan [0..2):   skip '[', find "1" → NUMBER token   │   │
│  │ Scan [2..4):   skip ',', find "2" → NUMBER token   │   │
│  │ Scan [4..6):   skip ',', find "3" → NUMBER token   │   │
│  │ ...                                                 │   │
│  │ Scan [16..19): skip ',', find "9" → NUMBER token   │   │
│  │ Scan [19..22): skip ',', find "10" → NUMBER ✅     │   │
│  │ ...                                                 │   │
│  │ Scan [31..34): skip ',', find "15" → NUMBER ✅     │   │
│  │ ...                                                 │   │
│  │ Scan [48..51): skip ',', find "20" → NUMBER token  │   │
│  │ After last token: no more values                   │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│             FINAL OUTPUT: 41 TOKENS (SORTED!)               │
│  [0]:  {pos:0,  type:STRUCTURAL, char:'['}                 │
│  [1]:  {pos:1,  type:NUMBER, len:1}  "1"                   │
│  [2]:  {pos:2,  type:STRUCTURAL, char:','}                 │
│  [3]:  {pos:3,  type:NUMBER, len:1}  "2"                   │
│  ...                                                        │
│  [19]: {pos:19, type:NUMBER, len:2}  "10" ✅               │
│  [20]: {pos:21, type:STRUCTURAL, char:','}                 │
│  ...                                                        │
│  [34]: {pos:34, type:NUMBER, len:2}  "15" ✅               │
│  ...                                                        │
│  [40]: {pos:51, type:STRUCTURAL, char:']'}                 │
└─────────────────────────────────────────────────────────────┘
```

---

## 🛠️ Implementation Plan

### Phase 1: Refactor SSE2 (Prototype)

**File:** `module/json/src/stage1/arch/x86/dap_json_stage1_sse2.c`

**Changes:**

#### 1. Simplify Main SIMD Loop

```c
// BEFORE: Hybrid detection (structural + numbers + literals)
while (l_pos + SSE2_CHUNK_SIZE <= l_input_len) {
    // ... classify all bytes ...
    
    // Process structural
    if (!l_in_string && l_struct_mask) { /* add structural tokens */ }
    
    // Process numbers/literals  ← REMOVE THIS!
    if (!l_in_string) {
        uint16_t l_value_mask = ~(l_struct_mask | l_ws_mask | l_quote_mask);
        // ... scan numbers/literals ...  ← COMPLEXITY!
    }
    
    l_pos += SSE2_CHUNK_SIZE;
}

// AFTER: Pure structural detection + strings
while (l_pos + SSE2_CHUNK_SIZE <= l_input_len) {
    // ... classify all bytes ...
    
    // Process structural chars
    if (!l_in_string && l_struct_mask) { /* add structural tokens */ }
    
    // Process strings (quote detection with escaped quotes)
    if (l_real_quotes) {
        // Add string tokens using scan_string_ref()
        // Update in_string state
    }
    
    l_pos += SSE2_CHUNK_SIZE;  // ✅ Always simple increment!
}

// Process tail (remaining bytes < SSE2_CHUNK_SIZE)
// ... same logic for tail ...
```

**Lines:** ~150 lines removed

#### 2. Add Pass 2: Scalar Value Extraction

```c
// NEW: Pass 2 - Extract numbers/literals between tokens
static int s_extract_values_between_tokens(dap_json_stage1_t *a_stage1)
{
    const uint8_t *l_input = a_stage1->input;
    size_t l_input_len = a_stage1->input_len;
    
    // Sort tokens by position (should already be sorted from Pass 1)
    // qsort(a_stage1->indices, a_stage1->indices_count, sizeof(dap_json_token_t), compare_by_position);
    
    size_t l_last_pos = 0;
    
    // Iterate through all tokens from Pass 1
    for (size_t i = 0; i < a_stage1->indices_count; i++) {
        size_t l_current_pos = a_stage1->indices[i].position;
        
        // Scan region [l_last_pos .. l_current_pos-1] for numbers/literals
        if (l_current_pos > l_last_pos) {
            s_scan_values_in_region(a_stage1, l_last_pos, l_current_pos);
        }
        
        // Move past current token
        l_last_pos = l_current_pos + a_stage1->indices[i].length;
    }
    
    // Process tail: from last token to end of document
    if (l_input_len > l_last_pos) {
        s_scan_values_in_region(a_stage1, l_last_pos, l_input_len);
    }
    
    return 0;
}

static void s_scan_values_in_region(
    dap_json_stage1_t *a_stage1,
    size_t a_start,
    size_t a_end
)
{
    const uint8_t *l_input = a_stage1->input;
    size_t i = a_start;
    
    while (i < a_end) {
        uint8_t l_char = l_input[i];
        
        // Skip whitespace
        if (dap_json_classify_char(l_char) == CHAR_CLASS_WHITESPACE) {
            i++;
            continue;
        }
        
        // Skip structural chars (shouldn't happen if Pass 1 was correct)
        if (dap_json_classify_char(l_char) == CHAR_CLASS_STRUCTURAL) {
            i++;
            continue;
        }
        
        // Number
        if (l_char == '-' || (l_char >= '0' && l_char <= '9')) {
            size_t l_num_end = dap_json_stage1_scan_number_ref(a_stage1, i);
            if (l_num_end > i) {
                dap_json_stage1_add_token(a_stage1, (uint32_t)i,
                                          (uint32_t)(l_num_end - i),
                                          TOKEN_TYPE_NUMBER, 0);
                i = l_num_end;
            } else {
                i++; // Error case, skip byte
            }
            continue;
        }
        
        // Literal (true, false, null)
        if (l_char == 't' || l_char == 'f' || l_char == 'n') {
            size_t l_lit_end = dap_json_stage1_scan_literal_ref(a_stage1, i);
            if (l_lit_end > i) {
                // Determine literal type
                uint8_t l_lit_type = DAP_JSON_LITERAL_UNKNOWN;
                if (l_char == 't') {
                    l_lit_type = DAP_JSON_LITERAL_TRUE;
                } else if (l_char == 'f') {
                    l_lit_type = DAP_JSON_LITERAL_FALSE;
                } else if (l_char == 'n') {
                    l_lit_type = DAP_JSON_LITERAL_NULL;
                }
                
                dap_json_stage1_add_token(a_stage1, (uint32_t)i,
                                          (uint32_t)(l_lit_end - i),
                                          TOKEN_TYPE_LITERAL, l_lit_type);
                i = l_lit_end;
            } else {
                i++; // Error case, skip byte
            }
            continue;
        }
        
        // Unknown character - skip (or error)
        debug_if(s_debug_more, L_WARNING, "Unknown character at pos %zu: '%c' (0x%02X)", 
                 i, l_char, l_char);
        i++;
    }
}
```

**Lines:** ~100 lines added

**Key Points:**
1. ✅ Pass 2 обрабатывает ТОЛЬКО numbers/literals (strings уже в Pass 1)
2. ✅ Обработка tail region после последнего token
3. ✅ Error handling для invalid characters
4. ✅ Literal type determination (true/false/null)

#### 3. Call Pass 2 After Pass 1

```c
int dap_json_stage1_run_sse2(dap_json_stage1_t *a_stage1)
{
    // ... validation ...
    
    // Pass 1: SIMD structural indexing + strings
    // ... main SIMD loop ...
    // ... tail processing ...
    
    // Pass 2: Scalar value extraction (numbers/literals)
    s_extract_values_between_tokens(a_stage1);
    
    // Sort tokens by position (if Pass 2 added out-of-order)
    // Note: Should NOT be needed if implementation is correct!
    // qsort(a_stage1->indices, a_stage1->indices_count, ...);
    
    return STAGE1_SUCCESS;
}
```

### Phase 2-4: Apply to Other Architectures

**Pattern:** Copy exact same changes to:
- AVX2 (32 bytes/iter)
- AVX-512 (64 bytes/iter)
- ARM NEON (16 bytes/iter)

**Effort:** ~30 min per architecture (straightforward copy)

### Phase 5: Update Documentation

**File:** `module/json/docs/PHASE_1.4_SIMD_ARCHITECTURE.md`

**Add sections:**
- "Two-Pass Architecture Rationale"
- "Pass 1: Structural Indexing Details"
- "Pass 2: Value Extraction Details"
- Updated diagrams

---

## 📊 Performance Analysis

### Pass 1 Cost
- **Complexity:** O(n/SIMD_WIDTH)
- **Unchanged** from current architecture

### Pass 2 Cost
- **Complexity:** O(m) where m = non-whitespace bytes between structurals
- **Typical JSON:** m << n (most bytes are whitespace + structural)
- **Example:** `[1,2,3]` → m=3, n=7 → pass 2 processes only 43% of bytes
- **Worst case:** Long strings/numbers but still bounded by n

### Expected Overhead
- **Estimate:** 5-10% additional time for pass 2
- **Justification:** 
  - Pass 2 is pure scalar but on **small regions**
  - Most time still in SIMD pass 1
  - Scalar number/literal validation was already happening in current impl

### Benefit vs Cost
- **Cost:** +5-10% time
- **Benefit:** ✅ **100% CORRECTNESS**
- **Conclusion:** Worth it! Incorrect results = useless performance

---

## ✅ Acceptance Criteria

1. ✅ All tokens in **correct order** (sorted by position)
2. ✅ NO duplicate tokens
3. ✅ NO missing tokens
4. ✅ `test_stage1_sse2_debug.c`: 3/3 tests PASSING
5. ✅ `test_stage1_simd_correctness.c`: 100% pass rate for all architectures
6. ✅ Performance: Pass 2 overhead < 10%

---

## 📅 Timeline

- **Phase 1 (SSE2 Prototype):** 2-3 hours
- **Phase 2-4 (Other Arch):** 1-2 hours
- **Phase 5 (Docs):** 1 hour
- **Testing:** 2 hours
- **Total:** ~1 day

---

## 🎯 Next Steps

1. Implement Phase 1: SSE2 refactoring
2. Run `test_stage1_sse2_debug` → verify 3/3 PASSING
3. Apply to AVX2, AVX-512, NEON
4. Run full `test_stage1_simd_correctness` → verify 100% pass
5. Update documentation
6. Commit with message: `refactor(json): Phase 1.4.4 - Two-Pass SIMD Architecture`

---

## ⚠️ Potential Issues & Solutions

### Issue 1: Strings in Pass 1 or Pass 2?

**Decision:** Strings in **Pass 1** (SIMD loop)

**Reasoning:**
- String detection уже реализован в SIMD loop (`s_compute_escaped_quotes()`)
- Strings содержат escaped chars и могут содержать structural-like chars внутри
- Pass 2 не сможет корректно определить boundaries между tokens без знания где strings
- **Вывод:** Pass 1 = structural + strings, Pass 2 = numbers + literals ONLY

### Issue 2: Token Ordering After Pass 2

**Problem:** Pass 2 добавляет tokens, которые могут быть out-of-order если implementation incorrect

**Solution:**
- Pass 2 должен обрабатывать regions **строго слева направо**
- Iterate через tokens из Pass 1 в порядке возрастания position
- Каждый новый token добавляется **после** обработки предыдущего region
- **Result:** Tokens автоматически в правильном порядке, qsort НЕ нужен

### Issue 3: Tail Processing

**Problem:** После последнего structural char/string могут быть numbers/literals до конца документа

**Solution:**
```c
// After iterating through all Pass 1 tokens
if (l_input_len > l_last_pos) {
    s_scan_values_in_region(a_stage1, l_last_pos, l_input_len);
}
```

### Issue 4: Performance Overhead

**Concern:** Pass 2 adds extra iteration overhead

**Analysis:**
- Pass 2 processes ONLY non-whitespace bytes между tokens
- Typical JSON: ~30-50% non-whitespace
- Pass 2 complexity: O(m) where m < 0.5*n
- Expected overhead: 5-10%
- **Acceptable** для гарантии correctness

---

## 📝 Rationale

**Why not single-pass?**
- Chunk boundaries create too many edge cases
- Impossible to maintain token order without complex state

**Why two-pass?**
- Clear separation of concerns
- SIMD for bulk work, Scalar for precision
- Similar to simdjson's approach (Stage 1 finds quotes & structurals, Stage 2 parses between)

**Why prioritize correctness?**
- Incorrect results make performance meaningless
- 10% overhead acceptable for 100% correctness

---

**End of Design Document**


