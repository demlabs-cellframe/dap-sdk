# Phase 0.3: Architecture Research - JSON Parsers Analysis

**Дата начала:** 2025-01-07  
**Цель:** Детальное исследование архитектур ведущих JSON парсеров для проектирования dap_json_native

---

## 🎯 Цели исследования

1. **simdjson**: Two-stage parsing, SIMD optimization techniques
2. **RapidJSON**: In-situ parsing, SAX/DOM hybrid approach
3. **yajl**: Streaming parser, event-driven architecture
4. **json-c**: Current baseline (для сравнения)

### Критерии оценки:
- Производительность (throughput, latency)
- Архитектурная сложность
- Поддержка платформ (x86, ARM)
- Memory efficiency
- Применимость техник для dap_json

---

## 📊 Part 1: simdjson Architecture

### Общая информация
- **Автор:** Daniel Lemire et al.
- **Первый релиз:** 2018
- **Performance:** 6-12 GB/s (в зависимости от CPU)
- **Ключевая техника:** Two-stage SIMD parsing

### Two-Stage Architecture

#### Stage 1: Structural Indexing (SIMD-accelerated)
**Цель:** Найти все структурные символы JSON за один проход

**Процесс:**
1. **Parallel Character Classification**
   - Обрабатывает 64 байта за раз (AVX2) или 128 байт (AVX-512)
   - Классифицирует символы: `{`, `}`, `[`, `]`, `:`, `,`, `"`, whitespace
   - Использует битовые маски для параллельной классификации

2. **String Detection**
   - Определяет границы строк (quoted regions)
   - Находит escape sequences внутри строк
   - Создаёт битовую маску: 1 = внутри строки, 0 = вне строки

3. **Structural Character Filtering**
   - Отфильтровывает символы внутри строк
   - Отфильтровывает whitespace
   - Создаёт битовую маску структурных символов

4. **Index Generation**
   - Конвертирует битовые маски в массив индексов
   - Каждый индекс указывает на структурный символ
   - Результат: сжатый массив позиций структурных элементов

**SIMD Техники Stage 1:**
- `_mm256_cmpeq_epi8` - параллельное сравнение 32 байт
- `_mm256_movemask_epi8` - конвертация в битовую маску
- Branchless algorithms для escape detection
- Parallel prefix sum для подсчёта кавычек

#### Stage 2: DOM Building (Sequential)
**Цель:** Построить Document Object Model из структурных индексов

**Процесс:**
1. **Tape-based Parsing**
   - Проход по массиву структурных индексов
   - Построение "tape" - линейного представления JSON tree
   - Каждый элемент tape содержит: тип, значение/указатель, размер

2. **Recursive Descent**
   - Парсинг вложенных структур
   - Валидация синтаксиса
   - Построение tree-like structure

3. **Value Parsing**
   - Парсинг чисел (SIMD-accelerated)
   - Декодирование строк (SIMD для UTF-8 validation)
   - Распознавание литералов (true, false, null)

**Optimizations Stage 2:**
- SIMD number parsing (до 8 чисел параллельно)
- UTF-8 validation at 13 GB/s
- Lazy value parsing (parse on demand)

### Ключевые инновации simdjson

1. **Branchless JSON String Parsing**
   - Обработка escape sequences без ветвлений
   - Использование lookup tables и битовых операций

2. **SIMD Number Parsing**
   - Параллельная обработка digit characters
   - Быстрая конвертация ASCII → integers

3. **UTF-8 Validation**
   - 13 GB/s validation speed
   - Parallel finite-state machine

4. **Tape-based Representation**
   - Линейная структура вместо tree
   - Cache-friendly access patterns
   - Efficient для навигации

### Преимущества архитектуры:
✅ Экстремально высокая производительность (6-12 GB/s)
✅ Отличная cache locality (Stage 1)
✅ Полная валидация JSON
✅ UTF-8 validation встроена
✅ Простая интеграция

### Недостатки:
❌ Stage 2 sequential (bottleneck для больших JSON)
❌ Требует весь JSON в памяти (не streaming)
❌ Сложность реализации SIMD кода
❌ Platform-specific optimizations

### Потенциал для улучшения (наша цель):
💡 **Parallel Stage 2 DOM Building** - основная цель превзойти simdjson
💡 **Predictive Parsing** - для типичных DAP SDK JSON structures
💡 **Aggressive Prefetching** - улучшить memory access patterns

---

## 📊 Part 2: RapidJSON Architecture

*(To be researched)*

### Общая информация
- **Performance:** 1-2 GB/s
- **Ключевая техника:** In-situ parsing + SAX/DOM hybrid

### In-Situ Parsing
- Парсинг без копирования данных
- Модификация input buffer напрямую
- Zero-copy strings

### SAX vs DOM
- SAX: Event-driven, streaming-friendly
- DOM: Tree-building, random access
- Hybrid: Best of both worlds

*(Детали будут добавлены)*

---

## 📊 Part 3: yajl Architecture

*(To be researched)*

### Общая информация
- **Performance:** 0.5-1 GB/s
- **Ключевая техника:** Pure streaming parser

### Event-Driven Approach
- Callback-based API
- Minimal memory footprint
- True streaming (не требует весь JSON в памяти)

*(Детали будут добавлены)*

---

## 🎯 Part 4: Сравнительный анализ

### Performance Comparison
| Parser | Throughput | Latency (small JSON) | Memory Usage |
|--------|------------|----------------------|--------------|
| simdjson | 6-12 GB/s | ~50-100ns | High (full buffer) |
| RapidJSON | 1-2 GB/s | ~100-200ns | Medium (in-situ) |
| yajl | 0.5-1 GB/s | ~500ns+ | Low (streaming) |
| json-c | 0.2-0.5 GB/s | ~1000ns+ | Medium |

### Architecture Comparison
| Feature | simdjson | RapidJSON | yajl | json-c |
|---------|----------|-----------|------|--------|
| SIMD | ✅ Extensive | ❌ Minimal | ❌ No | ❌ No |
| Two-stage | ✅ Yes | ❌ No | ❌ No | ❌ No |
| Streaming | ❌ No | ⚠️ Partial | ✅ Full | ⚠️ Partial |
| In-situ | ❌ No | ✅ Yes | ❌ No | ❌ No |
| UTF-8 Validation | ✅ Full (13GB/s) | ⚠️ Basic | ⚠️ Basic | ⚠️ Basic |
| Platform Support | x86, ARM | Universal | Universal | Universal |

---

## 💡 Part 5: Best Practices для dap_json_native

### От simdjson:
1. ✅ **Two-stage architecture** - adopt полностью
2. ✅ **SIMD structural indexing** - ключевая техника
3. ✅ **Branchless algorithms** - для предсказуемости
4. ✅ **UTF-8 validation SIMD** - security + performance
5. ✅ **Tape-based intermediate representation** - cache-friendly

### От RapidJSON:
1. ✅ **In-situ parsing option** - для zero-copy scenarios
2. ✅ **SAX/DOM hybrid API** - гибкость использования
3. ✅ **Strict vs relaxed parsing modes** - опциональная строгость

### От yajl:
1. ✅ **True streaming API** - для больших JSON streams
2. ✅ **Event-driven callbacks** - для custom processing

### Наши инновации:
1. 🚀 **Parallel Stage 2 DOM Building** - превзойти simdjson
2. 🚀 **Predictive parsing** - паттерны DAP SDK JSON
3. 🚀 **Hardware prefetching** - aggressive memory hints
4. 🚀 **Custom memory allocators** - arena + string pool
5. 🚀 **JIT compilation** - для JSONPath queries

---

## 📝 Part 6: Рекомендуемая архитектура dap_json_native

### High-Level Design

```
┌─────────────────────────────────────────────────────────────────┐
│                     dap_json_native                             │
├─────────────────────────────────────────────────────────────────┤
│  API Layer (json-c compatible)                                  │
├──────────────────┬──────────────────┬───────────────────────────┤
│   DOM API        │    SAX API       │    Streaming API          │
├──────────────────┴──────────────────┴───────────────────────────┤
│              Parser Dispatch Layer                              │
│        (runtime CPU detection + best implementation)            │
├─────────────────────────────────────────────────────────────────┤
│                   Stage 2: DOM Builder                          │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Parallel DOM Construction (multi-threaded)                 │ │
│  │  - Work-stealing queue                                      │ │
│  │  - Lock-free data structures                                │ │
│  │  - Thread pool management                                   │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Value Parsing (SIMD-optimized)                             │ │
│  │  - SIMD number parsing                                      │ │
│  │  - SIMD string decoding (UTF-8)                             │ │
│  │  - Escape sequence handling                                 │ │
│  └────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                Stage 1: Structural Indexing                     │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  SIMD Implementation Selection                              │ │
│  │  - AVX-512 (6-10 GB/s target)                               │ │
│  │  - AVX2 (3-4 GB/s target)                                   │ │
│  │  - ARM SVE/SVE2 (4-5 GB/s target)                           │ │
│  │  - SSE4.2 (2-3 GB/s)                                        │ │
│  │  - Reference C (0.5-1 GB/s)                                 │ │
│  └────────────────────────────────────────────────────────────┘ │
│  ┌────────────────────────────────────────────────────────────┐ │
│  │  Core Algorithms                                            │ │
│  │  - Character classification (SIMD)                          │ │
│  │  - String boundary detection                                │ │
│  │  - Structural character filtering                           │ │
│  │  - Index generation                                         │ │
│  └────────────────────────────────────────────────────────────┘ │
├─────────────────────────────────────────────────────────────────┤
│                    Core Infrastructure                          │
│  - dap_cpu_detect (runtime feature detection)                  │
│  - dap_arena (custom allocator)                                │
│  - dap_string_pool (string interning)                          │
│  - dap_string_simd (SIMD string operations)                    │
│  - dap_object_pool (object recycling)                          │
└─────────────────────────────────────────────────────────────────┘
```

### Key Design Decisions:

1. **Two-Stage with Parallel Stage 2**
   - Stage 1: simdjson-style SIMD indexing
   - Stage 2: INNOVATIVE parallel DOM building

2. **Multi-Platform SIMD**
   - Runtime dispatch based on CPU features
   - Separate implementations for each SIMD level
   - Reference C fallback

3. **Predictive Parsing**
   - Learn common DAP SDK JSON patterns
   - Prefetch likely branches
   - Fast-path for common structures

4. **Custom Memory Management**
   - Arena allocator for bulk allocations
   - String pool for deduplication
   - Object pool for reuse

5. **Comprehensive API**
   - DOM (tree-based random access)
   - SAX (event-driven streaming)
   - Streaming (true incremental parsing)

---

## 📚 References & Further Reading

1. **simdjson Paper**: "Parsing Gigabytes of JSON per Second" (2019)
2. **simdjson GitHub**: https://github.com/simdjson/simdjson
3. **RapidJSON GitHub**: https://github.com/Tencent/rapidjson
4. **yajl GitHub**: https://github.com/lloyd/yajl
5. **SIMD JSON parsing**: Various academic papers

---

**Status:** 🚧 IN PROGRESS  
**Next:** Детальное исследование RapidJSON и yajl архитектур
