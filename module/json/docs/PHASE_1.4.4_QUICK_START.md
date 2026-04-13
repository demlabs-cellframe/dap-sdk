# Phase 1.4.4: Two-Pass SIMD Architecture - Quick Start

## 🎯 Цель

Рефакторинг всех SIMD implementations для исправления проблемы **неправильного порядка токенов**.

## 📋 Проблема

```
[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20]

❌ Текущий результат (SSE2):
  Token 32: pos=45 STRUCTURAL ','
  Token 33: pos=48 STRUCTURAL ','
  Token 34: pos=34 NUMBER     '15'  ← WRONG! pos 34 < pos 48

✅ Ожидаемый результат:
  Token 32: pos=45 STRUCTURAL ','
  Token 33: pos=46 NUMBER     '19'
  Token 34: pos=48 STRUCTURAL ','
  Token 35: pos=49 NUMBER     '20'
```

## 🏗️ Решение: Two-Pass Architecture

### Pass 1 (SIMD): Structural Chars + Strings
- Найти ВСЕ `{`, `}`, `[`, `]`, `:`, `,`
- Detect and extract **strings** using SIMD quote detection
- Track string state
- Output: Structural + String indices

### Pass 2 (Scalar): Numbers + Literals Between Tokens
- Scan между consecutive tokens из Pass 1
- Extract **ТОЛЬКО** numbers и literals (strings уже готовы!)
- Tokens гарантированно в правильном порядке!

## 📁 Файлы для чтения

1. **Design:** `module/json/docs/PHASE_1.4.4_TWO_PASS_REFACTORING.md` (450 lines)
2. **Task:** `.context/tasks/dap_json_native_implementation.json` → Phase 1.4.4
3. **Session:** `.context/sessions/2026-01-08_phase_1_4_2_debugging_summary.md`

## 🚀 Начать работу

```bash
# 1. Прочитать design document
cat module/json/docs/PHASE_1.4.4_TWO_PASS_REFACTORING.md

# 2. Запустить debug test (сейчас 2/3 PASSING)
cd build
make test_json_stage1_sse2_debug
./tests/unit/json/test_json_stage1_sse2_debug

# 3. Начать рефакторинг SSE2
vim module/json/src/stage1/arch/x86/dap_json_stage1_sse2.c
```

## 📝 Implementation Checklist

### Phase 1: SSE2 (Prototype)
- [ ] Remove numbers/literals processing from main SIMD loop
- [ ] Keep structural chars + strings in Pass 1
- [ ] Add Pass 2: `s_extract_values_between_tokens()` (~50 lines)
- [ ] Add Pass 2: `s_scan_values_in_region()` (~100 lines)
- [ ] Handle tail region processing
- [ ] Add literal type determination
- [ ] Test: `test_stage1_sse2_debug` → 3/3 PASSING

### Phase 2-4: Other Architectures
- [ ] Apply same changes to AVX2
- [ ] Apply same changes to AVX-512
- [ ] Apply same changes to ARM NEON

### Phase 5: Testing & Docs
- [ ] Run `test_stage1_simd_correctness` → 100% pass
- [ ] Update `PHASE_1.4_SIMD_ARCHITECTURE.md`
- [ ] Git commit: `refactor(json): Phase 1.4.4 - Two-Pass SIMD Architecture`

## 💡 Key Code Pattern

```c
// PASS 1: SIMD - Structural + Strings
while (l_pos + SSE2_CHUNK_SIZE <= l_input_len) {
    // Classify bytes
    // Extract structural chars
    // Detect and add string tokens (using scan_string_ref)
    // Track string state
    l_pos += SSE2_CHUNK_SIZE;  // Simple!
}

// PASS 2: Scalar - Numbers/Literals Between Tokens
size_t l_last_pos = 0;
for (size_t i = 0; i < indices_count; i++) {
    size_t l_current_pos = indices[i].position;
    
    // Scan [l_last_pos .. l_current_pos-1] for numbers/literals
    s_scan_values_in_region(a_stage1, l_last_pos, l_current_pos);
    
    // Move past current token
    l_last_pos = l_current_pos + indices[i].length;
}

// Process tail
if (l_input_len > l_last_pos) {
    s_scan_values_in_region(a_stage1, l_last_pos, l_input_len);
}
```

## 🎯 Success Criteria

✅ All tokens sorted by position
✅ No duplicates
✅ No missing tokens
✅ test_stage1_sse2_debug: 3/3 PASSING
✅ test_stage1_simd_correctness: 100% pass

## ⚡ Estimated Time

- SSE2 prototype: 2-3 hours
- Other arch: 3 hours
- Testing: 2 hours
- **Total:** ~1 day

---

**Start here:** Read design doc, then refactor SSE2!


