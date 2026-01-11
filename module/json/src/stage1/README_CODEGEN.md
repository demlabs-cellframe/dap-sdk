# SIMD Stage 1 Tokenization - Code Generation

Эта директория содержит **универсальную систему генерации кода** для SIMD-оптимизированных реализаций Stage 1 JSON токенизации.

## 🎯 Философия

Вместо поддержки множества дублирующихся `.c` файлов, мы используем **единый источник правды** в виде шаблонов `dap_tpl`, которые генерируют код для всех SIMD архитектур.

## 📁 Структура

```
stage1/
├── dap_json_stage1_simd.c.tpl    # Шаблон для .c файлов
├── dap_json_stage1_simd.h.tpl    # Шаблон для .h файлов
├── generate_simd.sh               # Скрипт генерации
├── arch/
│   ├── x86/
│   │   ├── dap_json_stage1_sse2.c/h      # Сгенерированные
│   │   ├── dap_json_stage1_avx2.c/h      # Сгенерированные
│   │   └── dap_json_stage1_avx512.c/h    # Сгенерированные
│   └── arm/
│       └── dap_json_stage1_neon.c/h      # Сгенерированные
```

## 🚀 Как использовать

### Автоматическая генерация (рекомендуется)

Генерация происходит автоматически при запуске `cmake`:

```bash
cd dap-sdk
mkdir build && cd build
cmake ..  # Автоматически вызовет generate_simd.sh
make
```

CMake запускает `generate_simd.sh` на этапе конфигурации, поэтому все SIMD реализации всегда актуальны.

### Ручная генерация (для разработки)

Если нужно перегенерировать файлы без запуска cmake:

```bash
cd module/json/src/stage1
./generate_simd.sh
```

Это создаст/обновит все `.c` и `.h` файлы в `arch/*/`.

### Добавление новой архитектуры

Пример добавления RISC-V:

```bash
# В generate_simd.sh добавить:
echo "Generating RISC-V..."
generate_arch \
    "${SCRIPT_DIR}/arch/riscv/dap_json_stage1_rvv.c" \
    "${SCRIPT_DIR}/arch/riscv/dap_json_stage1_rvv.h" \
    "ARCH_NAME=RISC-V RVV" \
    "ARCH_LOWER=rvv" \
    "ARCH_INCLUDES=#include <riscv_vector.h>" \
    "CHUNK_SIZE=32" \
    "VECTOR_TYPE=vuint8m1_t" \
    "MASK_TYPE=uint32_t" \
    "LOADU=vle8_v_u8m1" \
    "SET1_EPI8=vmv_v_x_u8m1" \
    "CMPEQ_EPI8=vmseq_vv_u8m1_b8" \
    "OR=vor_vv_u8m1" \
    "MOVEMASK_EPI8=rvv_movemask_u8" \
    "PERF_TARGET=2+ GB/s (single-core)" \
    "TARGET_ATTR="
```

## 🎨 Возможности шаблонов

### Условная компиляция

```c
{{#if USE_AVX512_MASK}}
    // AVX-512 специфичный код (kmask)
    MASK_TYPE mask = _mm512_cmpeq_epi8_mask(chunk, v_char);
{{else}}
    // SSE2/AVX2/NEON код (vector + movemask)
    VECTOR_TYPE cmp = {{CMPEQ_EPI8}}(chunk, v_char);
    MASK_TYPE mask = {{MOVEMASK_EPI8}}(cmp);
{{/if}}
```

### Переменные подстановки

- `{{ARCH_NAME}}` - Человекочитаемое имя ("SSE2", "AVX-512")
- `{{ARCH_LOWER}}` - Lowercase имя для идентификаторов ("sse2", "avx512")
- `{{CHUNK_SIZE}}` - Размер обрабатываемого блока (16/32/64)
- `{{VECTOR_TYPE}}` - Тип SIMD вектора (`__m128i`, `__m256i`, ...)
- `{{MASK_TYPE}}` - Тип битовой маски (`uint16_t`, `uint32_t`, `uint64_t`)
- `{{LOADU}}` - Функция загрузки (`_mm_loadu_si128`, ...)
- `{{SET1_EPI8}}` - Broadcast байта во все lanes
- `{{CMPEQ_EPI8}}` - Побайтовое сравнение
- `{{OR}}` - Побитовое ИЛИ
- `{{MOVEMASK_EPI8}}` - Извлечение битовой маски
- `{{PERF_TARGET}}` - Целевая производительность
- `{{TARGET_ATTR}}` - GCC/Clang target attribute

## 📊 Текущие реализации

| Архитектура | Chunk | Mask Type | Производительность | Особенности |
|-------------|-------|-----------|-------------------|-------------|
| **SSE2**    | 16B   | uint16_t  | 1+ GB/s           | Baseline x64 |
| **AVX2**    | 32B   | uint32_t  | 4-5 GB/s          | Optimal |
| **AVX-512** | 64B   | uint64_t  | 2+ GB/s           | kmask ops |
| **NEON**    | 16B   | uint16_t  | 1+ GB/s           | ARM baseline |

## 🔧 Отладка

Для проверки что генерируется:

```bash
# Сгенерировать только SSE2 с debug выводом
cd module/json/src/stage1
source ../../../test/dap_tpl/dap_tpl.sh
replace_template_placeholders \
    dap_json_stage1_simd.c.tpl \
    /tmp/test_sse2.c \
    "ARCH_NAME=SSE2" \
    "ARCH_LOWER=sse2" \
    ...
cat /tmp/test_sse2.c
```

## 📝 Правила разработки

1. **НЕ РЕДАКТИРУЙ** сгенерированные файлы в `arch/*/` напрямую!
2. **РЕДАКТИРУЙ** только `.tpl` файлы
3. После изменений **ЗАПУСТИ**: `cmake ..` (или `./generate_simd.sh` вручную)
4. **КОММИТЬ** только `.tpl` файлы и `generate_simd.sh`
5. **НЕ КОММИТЬ** сгенерированные `.c/.h` файлы (они в .gitignore)
6. При добавлении архитектуры **ОБНОВИ** этот README

## 🌟 Преимущества подхода

✅ **DRY** - единый источник правды
✅ **Согласованность** - все реализации идентичны по логике
✅ **Расширяемость** - новая архитектура за 15 строк
✅ **Zero-cost** - различия разрешаются при генерации
✅ **Читаемость** - сгенерированный код такой же как написанный вручную

## 🚧 Планы

- [ ] SVE/SVE2 для ARM (векторы переменной длины)
- [ ] RISC-V RVV (RISC-V Vector Extension)
- [ ] Elbrus (российская архитектура)
- [ ] WebAssembly SIMD
- [ ] Автоматическая генерация в CMake

## 📚 Ссылки

- [dap_tpl документация](../../../test/dap_tpl/README.md)
- [SimdJSON paper](https://arxiv.org/abs/1902.08318)
- [SLC правила](.context/core/manifest.json) - "сложность лучше костылей"

