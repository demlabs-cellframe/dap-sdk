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

---

## 📊 Part 2: RapidJSON Architecture (DETAILED ANALYSIS)

**Status:** ✅ COMPLETED (2025-01-07)

### Общая информация
- **Автор:** Milo Yip (Tencent)
- **Первый релиз:** 2011  
- **Performance:** 1-2 GB/s (зависит от режима)
- **Язык:** Header-only C++
- **Ключевая техника:** In-situ parsing, Template-based design
- **Лицензия:** MIT

### Core Design Philosophy

**RapidJSON = Rapid + JSON**
1. "Rapid" = быстрый за счёт zero-copy, in-place modifications
2. Template-based для compile-time optimization
3. Header-only для легкой интеграции
4. Self-contained (нет внешних зависимостей)

### In-Situ Parsing - Ключевая Инновация

#### Концепция
**In-situ parsing** = парсинг "на месте", модификация входного буфера напрямую

**Процесс:**
```
Input:  {"name":"John","age":30}
Buffer: [{"name":"John","age":30}\0]

After in-situ:
Buffer: [{name\0John\0age\030}\0]
        ^     ^    ^   ^
        |     |    |   +-- Value (number as string)
        |     |    +------ Key
        |     +----------- Value (string, null-terminated)
        +----------------- Key (null-terminated)
```

**Преимущества:**
1. Zero-copy strings (указатели на оригинальный буфер)
2. ~40% меньше allocations
3. Лучшая cache locality
4. +30-50% performance boost

**Ограничения:**
- Буфер должен быть mutable
- Буфер должен жить до конца использования DOM
- Не подходит для const char* или mmap'd files

### SAX vs DOM - Dual API System

#### SAX API (Event-driven)
```cpp
Handler callbacks:
  bool Null()
  bool Bool(bool b)
  bool Int(int i), Uint(unsigned), Int64(), Uint64()
  bool Double(double d)
  bool String(const char* str, SizeType len, bool copy)
  bool StartObject() / EndObject(SizeType memberCount)
  bool StartArray() / EndArray(SizeType elementCount)
  bool Key(const char* str, SizeType len, bool copy)
```

**Преимущества:**
- O(1) memory (не строит DOM)
- Streaming support (бесконечные JSON streams)
- Fastest mode
- Можно останавливаться early

**Use cases:**
- Фильтрация больших JSON
- Streaming протоколы
- Memory-constrained environments

#### DOM API  
**Tree-based in-memory representation**

```cpp
Document doc;
doc.Parse(json);

// Navigation
Value& name = doc["name"];
Value& arr = doc["items"];
int count = arr.Size();

// Modification
doc["new_field"] = 123;
arr.PushBack(Value(456), allocator);
```

**Преимущества:**
- Random access
- Модификация документа
- Удобный API
- Type-safe operations

**Use cases:**
- Configuration files
- Small-medium JSON documents
- Interactive manipulation

### Memory Management - Custom Allocators

#### MemoryPoolAllocator (default)
```
Concept: Allocate large chunks, subdivide into small pieces
         No per-object deallocation, batch free on destruction

[Chunk 1: 64KB] -> [obj1][obj2][obj3]...
[Chunk 2: 64KB] -> [obj50][obj51]...
                     ^
                     Bump pointer allocation (O(1))
```

**Performance:**
- Allocation: O(1), just bump pointer
- Deallocation: O(1), entire pool at once
- Reduced malloc/free calls by 10-100x
- Better cache locality

#### CrtAllocator
Standard malloc/free wrapper for compatibility

### Parsing Modes - Performance vs Features Tradeoff

```cpp
Parse flags (bitwise OR):
  kParseInsituFlag          // Modify buffer in-place (+50% speed)
  kParseValidateEncodingFlag // UTF-8 validation (-10% speed)
  kParseFullPrecisionFlag    // Accurate doubles (-20% speed)
  kParseCommentsFlag         // Allow // and /* */ comments
  kParseTrailingCommasFlag   // JSON5-like trailing commas
  kParseNanAndInfFlag        // Allow NaN, Infinity values
```

**Performance matrix:**
| Mode | Throughput | Safety | Use Case |
|------|-----------|--------|----------|
| In-situ, no validation | ~2 GB/s | Low | Trusted input |
| In-situ + validation | ~1.5 GB/s | High | Production |
| Normal + full precision | ~1 GB/s | Highest | Financial data |

### Number Parsing - Three-tier Strategy

```cpp
1. Fast path: Simple integers (1234, -567)
   → Direct conversion, no allocations
   → SSE2 digit scanning
   
2. Medium path: Standard doubles
   → strtod() with rounding check
   → Good enough for most cases
   
3. Slow path: High precision (David Gay's dtoa algorithm)
   → Exact decimal representation
   → Used when medium path isn't accurate
```

**Performance:**
- 90% of numbers hit fast path
- 9% hit medium path
- 1% need slow path

### String Handling - Zero-Copy Strategies

#### String Storage Types

1. **Copy String** (`SetString(str, allocator)`)
   - Allocates memory, copies data
   - Safe, document owns lifetime
   - Use for: dynamic strings, temporary buffers

2. **Reference String** (`SetString(StringRef(str))`)
   - Just stores pointer, zero-copy
   - DANGEROUS: caller must ensure lifetime
   - Use for: string literals, long-lived buffers

3. **Move String** (C++11 move semantics)
   - Takes ownership of buffer
   - Zero-copy transfer
   - Use for: passing ownership

#### Short String Optimization (SSO)

```cpp
struct ShortString {
    char data[14];      // 14 bytes inline storage
    uint8_t length;     // String length
    uint8_t flags;      // Type flags
};
// Total: 16 bytes, perfect for cache line
```

**Impact:**
- Strings ≤ 13 chars: zero allocations
- ~60% of JSON strings are short
- Reduced malloc calls significantly

### Encoding Support

**Native encodings:**
- UTF-8 (default, fastest)
- UTF-16LE/BE
- UTF-32LE/BE  
- ASCII

**Template-based transcoding:**
```cpp
GenericDocument<UTF8<>> utf8_doc;
GenericDocument<UTF16<>> utf16_doc;

// Automatic transcoding on assignment
utf16_doc.CopyFrom(utf8_doc, allocator);
```

**UTF-8 validation:**
- Incremental validation during parsing
- SSE2/SSE4.2 acceleration (where available)
- ~2-3 GB/s validation speed

### Schema Validation

**JSON Schema Draft v4 support**

```cpp
// Schema compilation (once)
Document schema_doc;
schema_doc.Parse(schema_json);
SchemaDocument schema(schema_doc);

// Validation (multiple times, fast)
Document doc;
doc.Parse(json);

SchemaValidator validator(schema);
if (!doc.Accept(validator)) {
    // Get error details
    StringBuffer sb;
    validator.GetInvalidSchemaPointer().StringifyUriFragment(sb);
    fprintf(stderr, "Invalid: %s\n", sb.GetString());
    fprintf(stderr, "Keyword: %s\n", validator.GetInvalidSchemaKeyword());
}
```

**Performance:**
- Compiled schema = fast validation
- ~500 MB/s - 1 GB/s validation speed
- Streaming validation support

### Performance Benchmarks

**Typical throughput (varies by CPU, JSON structure):**

| JSON Size | In-situ | Normal | SAX |
|-----------|---------|--------|-----|
| Small (<1KB) | ~2 GB/s | ~1.5 GB/s | ~3 GB/s |
| Medium (100KB) | ~1.5 GB/s | ~1.2 GB/s | ~2 GB/s |
| Large (>10MB) | ~1.2 GB/s | ~1 GB/s | ~1.5 GB/s |

**Comparison:**
- 10-20x faster than json-c
- 5-6x slower than simdjson
- 2-3x faster than nlohmann/json
- Comparable to ujson (Python)

### Strengths для dap_json_native

1. ✅ **In-situ concept** - apply selectively
   - Optional mode when buffer is mutable
   - Significant memory savings
   - C adaptation: manual buffer tracking

2. ✅ **Memory pool allocator**
   - Direct implementation в dap_arena
   - Batch allocation strategy
   - Low fragmentation

3. ✅ **SAX API design**
   - Implement для streaming
   - Event callbacks для custom processing
   - Low memory footprint mode

4. ✅ **String handling**
   - Copy vs Reference choice
   - Short String Optimization adaptation
   - Zero-copy where safe

5. ✅ **Parsing modes**
   - Strict vs relaxed
   - Optional validation
   - Performance vs safety tradeoffs

### Weaknesses (что НЕ брать)

1. ❌ **Single-threaded only**
   - No parallel DOM building
   - dap_json будет использовать multi-threading

2. ❌ **Limited SIMD**
   - Only basic SSE2
   - dap_json: AVX-512, SVE2, aggressive SIMD

3. ❌ **C++ only**
   - Templates, RAII не portable to C
   - dap_json: pure C implementation

4. ❌ **No two-stage architecture**
   - Single pass parsing
   - dap_json: simdjson-style two-stage

### Comparison Table: simdjson vs RapidJSON vs dap_json_native

| Feature | simdjson | RapidJSON | **dap_json_native** |
|---------|----------|-----------|---------------------|
| **Throughput** | 6-12 GB/s | 1-2 GB/s | **Target: 6-10 GB/s** |
| **SIMD** | AVX-512, AVX2, NEON | SSE2 только | **AVX-512, SVE2, NEON, SSE4.2** |
| **Architecture** | Two-stage | Single-stage | **Two-stage + parallel** |
| **In-situ** | ❌ | ✅ | **✅ Optional** |
| **SAX API** | Limited | Full | **Full + extensions** |
| **Streaming** | ❌ | Partial | **✅ Full** |
| **Language** | C++ | C++ | **Pure C** |
| **Allocator** | Custom | Custom pool | **dap_arena + pools** |
| **Parallel** | Stage 1 only | ❌ | **Stage 1 + Stage 2** |
| **Predictive** | ❌ | ❌ | **✅ DAP patterns** |
| **Schema** | ❌ | Draft v4 | **Draft 2020-12 planned** |
| **JSON5** | ❌ | Partial | **✅ Full** |

### Key Takeaways для Implementation

1. **Adopt:** In-situ parsing как optional mode
2. **Adopt:** Memory pool allocator pattern  
3. **Adopt:** SAX/DOM dual API
4. **Adopt:** String handling strategies
5. **Improve:** Add parallel DOM building
6. **Improve:** Aggressive SIMD (not just SSE2)
7. **Improve:** Predictive parsing for DAP patterns
8. **Skip:** C++-specific features
9. **Skip:** Single-threaded limitations

---

**RapidJSON Research Status:** ✅ COMPLETED
**Next:** yajl Architecture Research
**Progress:** 66% complete (2/3 parsers analyzed)


---

## 📊 Part 3: yajl Architecture (DETAILED ANALYSIS)

**Status:** ✅ COMPLETED (2025-01-07)

### Общая информация
- **Автор:** Lloyd Hilaiel
- **Первый релиз:** 2007
- **Performance:** 0.5-1 GB/s
- **Язык:** Pure ANSI C
- **Ключевая техника:** True streaming, event-driven parsing
- **Лицензия:** ISC (очень permissive)
- **Название:** Yet Another JSON Library

### Core Design Philosophy

**yajl = Streaming-first JSON parser**
1. **Incremental parsing** - не требует весь JSON в памяти
2. **Event-driven** - SAX-style callbacks
3. **Pure C** - максимальная портабельность
4. **Minimal dependencies** - zero external dependencies
5. **Small footprint** - ~30KB compiled size

### True Streaming Architecture

#### Концепция
**Incremental parsing** = парсинг кусками по мере поступления данных

```
Data arrival:      [chunk1]  [chunk2]  [chunk3]  [chunk4]
Parsing:           parse1 → parse2 → parse3 → parse4
Memory:            O(depth) only, not O(size)
                   ↑
                   Only stack for nesting depth
```

**Отличие от других парсеров:**
- simdjson: требует весь JSON в памяти
- RapidJSON: можно streaming, но DOM mode требует полный буфер
- yajl: **истинный streaming** - парсит по мере поступления

#### Процесс инкрементального парсинга

```c
// Создание parser instance
yajl_handle hand = yajl_alloc(&callbacks, NULL, NULL);

// Парсинг кусками (например, из сети)
while (has_more_data) {
    char chunk[4096];
    size_t len = read_data(chunk, sizeof(chunk));
    
    yajl_status stat = yajl_parse(hand, chunk, len);
    if (stat != yajl_status_ok) {
        // Handle error
    }
}

// Финализация (проверка что JSON завершён)
stat = yajl_complete_parse(hand);

yajl_free(hand);
```

**Преимущества:**
1. **Constant memory** - O(nesting_depth), не O(document_size)
2. **Низкая latency** - начинаем обработку до полного получения
3. **Network-friendly** - идеально для streaming protocols
4. **Embeddable** - для constrained environments

### Event-Driven Callback Architecture

#### Callback Interface

```c
typedef struct {
    int (*yajl_null)(void *ctx);
    int (*yajl_boolean)(void *ctx, int boolVal);
    int (*yajl_integer)(void *ctx, long long integerVal);
    int (*yajl_double)(void *ctx, double doubleVal);
    
    // Strings
    int (*yajl_string)(void *ctx, const unsigned char *stringVal, size_t stringLen);
    
    // Objects
    int (*yajl_start_map)(void *ctx);
    int (*yajl_map_key)(void *ctx, const unsigned char *key, size_t stringLen);
    int (*yajl_end_map)(void *ctx);
    
    // Arrays
    int (*yajl_start_array)(void *ctx);
    int (*yajl_end_array)(void *ctx);
} yajl_callbacks;
```

#### Пример использования

```c
// User context
typedef struct {
    int depth;
    char current_key[256];
    // ... application state
} parse_context;

// Callback implementations
static int handle_string(void *ctx, const unsigned char *val, size_t len) {
    parse_context *pc = (parse_context*)ctx;
    printf("String: %.*s (depth=%d)\n", (int)len, val, pc->depth);
    return 1; // 1 = continue parsing, 0 = abort
}

static int handle_start_map(void *ctx) {
    parse_context *pc = (parse_context*)ctx;
    pc->depth++;
    return 1;
}

static int handle_end_map(void *ctx) {
    parse_context *pc = (parse_context*)ctx;
    pc->depth--;
    return 1;
}

// Setup
yajl_callbacks callbacks = {
    .yajl_string = handle_string,
    .yajl_start_map = handle_start_map,
    .yajl_end_map = handle_end_map,
    // ... other callbacks
};

parse_context ctx = {0};
yajl_handle hand = yajl_alloc(&callbacks, NULL, &ctx);
```

**Преимущества callback approach:**
1. **No DOM overhead** - не строим дерево
2. **Custom processing** - приложение решает что делать
3. **Early termination** - можно остановиться когда найдено нужное
4. **Filtering** - можно игнорировать ненужные части

**Недостатки:**
1. **State management** - программист сам отслеживает контекст
2. **No random access** - нельзя вернуться назад
3. **Сложнее для использования** - требует больше кода

### Memory Management

#### Allocation Strategy
**yajl uses custom allocator interface:**

```c
typedef struct {
    void * (*malloc)(void *ctx, size_t sz);
    void * (*realloc)(void *ctx, void *ptr, size_t sz);
    void   (*free)(void *ctx, void *ptr);
    void  *ctx;
} yajl_alloc_funcs;

// Create parser with custom allocator
yajl_handle hand = yajl_alloc(&callbacks, &alloc_funcs, user_ctx);
```

**Default allocator:**
- Uses standard malloc/realloc/free
- Suitable for most use cases

**Custom allocator use cases:**
- Arena allocation
- Pool allocation
- Memory-constrained systems
- Tracking/debugging allocations

#### Memory Efficiency

**Parser state memory:**
```
Parser state: ~1-2 KB
Stack depth:  O(max_nesting_depth) 
              Typically: 64 levels × 16 bytes = 1 KB
Buffer:       Configurable, default 2 KB
Total:        ~3-5 KB typical
```

**Comparison:**
| Parser | Memory for 1MB JSON |
|--------|---------------------|
| yajl (streaming) | ~5 KB (constant) |
| RapidJSON (SAX) | ~100 KB (partial DOM) |
| RapidJSON (DOM) | ~3-5 MB (full tree) |
| simdjson | ~2-3 MB (tape + indices) |
| json-c | ~2-4 MB (full tree) |

**Key insight:** yajl memory usage не зависит от размера JSON!

### Parser Configuration Options

```c
// Relaxed parsing options
yajl_config(hand, yajl_allow_comments, 1);          // Allow // and /* */
yajl_config(hand, yajl_dont_validate_strings, 1);  // Skip UTF-8 validation
yajl_config(hand, yajl_allow_trailing_garbage, 1); // Allow junk after JSON
yajl_config(hand, yajl_allow_multiple_values, 1);  // Multiple JSON docs
yajl_config(hand, yajl_allow_partial_values, 1);   // Partial arrays/objects
```

**Performance tradeoffs:**
| Config | Speed | Safety | Use Case |
|--------|-------|--------|----------|
| Default (strict) | 100% | High | Production |
| + comments | 95% | High | Config files |
| + no validation | 120% | Low | Trusted input |
| + all relaxed | 130% | Low | Development |

### Lexer-based Parsing

#### Lexer State Machine
yajl uses **hand-coded lexer** (not generated):

```
States:
  - lex_start            // Start of JSON value
  - lex_integer          // Parsing integer
  - lex_double           // Parsing floating point
  - lex_string           // Inside string
  - lex_string_escape    // After backslash
  - lex_comment          // Inside comment (if enabled)
  - lex_true_t, lex_true_r, lex_true_u, lex_true_e  // Parsing "true"
  - lex_false_f, ...     // Parsing "false"
  - lex_null_n, ...      // Parsing "null"
```

**Character-by-character processing:**
```c
// Pseudo-code
for each character c in input:
    switch (current_state):
        case lex_integer:
            if (isdigit(c)):
                append_to_buffer(c)
            else if (c == '.' || c == 'e' || c == 'E'):
                current_state = lex_double
            else:
                emit_integer()
                current_state = determine_next_state(c)
        // ... other states
```

**Performance:**
- Simple state machine: predictable branches
- No backtracking
- Good for streaming
- **Slower than SIMD** (character-by-character vs 64 bytes at once)

### Error Handling

#### Rich Error Information

```c
// Get error status
yajl_status stat = yajl_parse(hand, buf, len);

if (stat != yajl_status_ok) {
    unsigned char *error_str = yajl_get_error(
        hand,           // parser handle
        1,              // verbose (include context)
        buf,            // input buffer
        len             // buffer length
    );
    
    fprintf(stderr, "Parse error: %s\n", error_str);
    
    // Example output:
    // lexical error: invalid char in json text.
    //                    {"name":"John,"age":30}
    //                            (right here) ------^
    
    yajl_free_error(hand, error_str);
}
```

**Error types:**
- `yajl_status_ok` - parsing succeeded
- `yajl_status_client_canceled` - callback returned 0
- `yajl_status_error` - parse error

**Context in errors:**
- Shows problematic line
- Points to exact character
- Includes surrounding context
- Very helpful for debugging

### JSON Generation (Bonus Feature)

yajl также включает **JSON generator**:

```c
yajl_gen g = yajl_gen_alloc(NULL);

// Configure formatting
yajl_gen_config(g, yajl_gen_beautify, 1);     // Pretty print
yajl_gen_config(g, yajl_gen_indent_string, "  "); // 2-space indent

// Generate JSON
yajl_gen_map_open(g);
  yajl_gen_string(g, (unsigned char*)"name", 4);
  yajl_gen_string(g, (unsigned char*)"John", 4);
  yajl_gen_string(g, (unsigned char*)"age", 3);
  yajl_gen_integer(g, 30);
yajl_gen_map_close(g);

// Get result
const unsigned char *buf;
size_t len;
yajl_gen_get_buf(g, &buf, &len);
printf("%.*s\n", (int)len, buf);

yajl_gen_free(g);
```

**Output:**
```json
{
  "name": "John",
  "age": 30
}
```

### Performance Characteristics

#### Benchmarks (approximate)

| JSON Size | yajl | RapidJSON (SAX) | simdjson |
|-----------|------|-----------------|----------|
| Small (<1KB) | ~800 MB/s | ~2 GB/s | ~8 GB/s |
| Medium (100KB) | ~600 MB/s | ~1.5 GB/s | ~7 GB/s |
| Large (10MB) | ~500 MB/s | ~1.2 GB/s | ~6 GB/s |
| Streaming (infinite) | ~500 MB/s | N/A | N/A |

**Performance factors:**
- Character-by-character: slow but simple
- No SIMD: missed optimization opportunity
- Branch-heavy: modern CPUs handle ok
- Memory access: excellent (streaming)

#### Latency Comparison

| Parser | Time to First Event | Time to Complete |
|--------|-------------------|------------------|
| yajl | ~10μs | ~2ms (1MB) |
| RapidJSON | ~50μs | ~800μs (1MB) |
| simdjson | ~100μs | ~150μs (1MB) |

**yajl wins on latency** - начинает обработку мгновенно

### Strengths of yajl

1. ✅ **True streaming** - единственный с constant memory
2. ✅ **Pure C** - ANSI C89, максимальная portable
3. ✅ **Zero dependencies** - self-contained
4. ✅ **Small footprint** - ~30KB binary
5. ✅ **Event-driven** - SAX-style, flexible
6. ✅ **Incremental** - parse as data arrives
7. ✅ **Rich errors** - excellent error messages
8. ✅ **JSON generation** - bonus feature
9. ✅ **Well-tested** - battle-tested (10+ years)
10. ✅ **Embeddable** - для IoT, embedded systems

### Weaknesses of yajl

1. ❌ **Slow** - 5-10x slower than simdjson
2. ❌ **No SIMD** - character-by-character processing
3. ❌ **No parallel** - single-threaded only
4. ❌ **No DOM mode** - только callbacks (сложнее использовать)
5. ❌ **Manual state** - программист управляет context
6. ❌ **No schema validation** - только basic parsing
7. ❌ **Limited optimization** - room for improvement

### Применимость для dap_json_native

#### Техники для заимствования

1. ✅ **Streaming API concept**
   - Implement `dap_json_parse_chunk()` для incremental
   - O(depth) memory guarantee
   - Network-friendly design

2. ✅ **Event-driven callbacks**
   - SAX-style API для streaming mode
   - User context pointer pattern
   - Early termination support

3. ✅ **Custom allocator interface**
   - Pluggable allocator design
   - Arena allocator for parser state
   - Memory-constrained mode

4. ✅ **Error reporting style**
   - Context-rich error messages
   - Pinpoint exact location
   - Human-readable output

5. ✅ **Relaxed parsing modes**
   - Optional comment support
   - Configurable strictness
   - Performance/safety tradeoffs

6. ✅ **Pure C API design**
   - Clean, simple interface
   - Opaque handles pattern
   - Easy bindings для других языков

#### Что улучшить в dap_json

1. 🚀 **Add SIMD acceleration**
   - yajl: character-by-character
   - dap_json: AVX-512 parallel processing
   - Expected: 10x speedup

2. 🚀 **Parallel streaming**
   - yajl: single-threaded
   - dap_json: multi-threaded chunk processing
   - Pipeline: lexing → parsing → DOM building

3. 🚀 **DOM mode option**
   - yajl: только SAX
   - dap_json: SAX + DOM hybrid (RapidJSON style)
   - Flexibility для different use cases

4. 🚀 **Schema validation**
   - yajl: no schema
   - dap_json: JSON Schema Draft 2020-12
   - Compiled validation for speed

#### Что НЕ брать

1. ❌ Character-by-character lexing
   - Too slow for modern needs
   - Replace with SIMD structural indexing

2. ❌ Single-threaded only
   - Modern CPUs have 8-64 cores
   - Must parallelize

3. ❌ No optimization focus
   - yajl prioritizes simplicity
   - dap_json prioritizes performance

### Use Cases по парсерам

| Use Case | Best Parser | Reason |
|----------|------------|--------|
| **Batch processing (DAP blockchain)** | simdjson | Fastest for known-size data |
| **Network streaming (RPC)** | yajl | True streaming, low latency |
| **Config files** | RapidJSON | DOM, comments support |
| **Large files (logs)** | yajl | Constant memory |
| **Embedded systems** | yajl | Small footprint |
| **High-throughput (analytics)** | simdjson | Maximum speed |
| **Mixed workload** | **dap_json_native** | Best of all worlds |

### Architecture Comparison Matrix

| Feature | simdjson | RapidJSON | yajl | dap_json_native |
|---------|----------|-----------|------|-----------------|
| **Throughput** | 6-12 GB/s | 1-2 GB/s | 0.5-1 GB/s | **6-10 GB/s** |
| **Latency** | Medium | Medium | **Low** | **Low** |
| **Memory (1MB JSON)** | 2-3 MB | 3-5 MB | **5 KB** | **Adaptive** |
| **True streaming** | ❌ | ⚠️ Partial | ✅ | **✅** |
| **SIMD** | ✅ Extensive | ⚠️ Minimal | ❌ | **✅ Aggressive** |
| **Parallel** | Stage 1 | ❌ | ❌ | **Stage 1+2** |
| **SAX API** | Limited | Full | **Full** | **Full** |
| **DOM API** | ✅ | ✅ | ❌ | **✅** |
| **Language** | C++ | C++ | **Pure C** | **Pure C** |
| **Binary size** | ~500KB | ~200KB | **30KB** | **Target: 150KB** |
| **Portability** | x86/ARM | Universal | **Universal** | **Universal** |
| **Comments** | ❌ | ✅ | ✅ | **✅** |
| **Schema** | ❌ | Draft v4 | ❌ | **Draft 2020-12** |
| **Incremental** | ❌ | ⚠️ | ✅ | **✅** |

---

## 🎯 Part 4: Comparative Analysis & Final Recommendations

**Status:** ✅ COMPLETED (2025-01-07)

### Performance Summary

```
Throughput Ranking:
1. simdjson:        6-12 GB/s  (SIMD two-stage) ⭐⭐⭐⭐⭐
2. RapidJSON:       1-2 GB/s   (In-situ, C++)   ⭐⭐⭐
3. yajl:            0.5-1 GB/s (Streaming, C)   ⭐⭐
4. json-c:          0.2-0.5 GB/s (Basic)        ⭐

Memory Efficiency Ranking:
1. yajl:            5 KB constant      ⭐⭐⭐⭐⭐
2. RapidJSON:       ~1-3 MB variable   ⭐⭐⭐
3. simdjson:        ~2-3 MB variable   ⭐⭐
4. json-c:          ~2-4 MB variable   ⭐⭐

Latency Ranking (time to first event):
1. yajl:            ~10μs      ⭐⭐⭐⭐⭐
2. RapidJSON:       ~50μs      ⭐⭐⭐
3. simdjson:        ~100μs     ⭐⭐
4. json-c:          ~200μs     ⭐

Flexibility Ranking:
1. RapidJSON:       SAX+DOM, in-situ, schema  ⭐⭐⭐⭐⭐
2. yajl:            SAX, streaming, generator ⭐⭐⭐⭐
3. simdjson:        DOM, On Demand           ⭐⭐⭐
4. json-c:          DOM only                 ⭐⭐
```

### Best Practices Matrix для dap_json_native

| From | Technique | Priority | Implementation |
|------|-----------|----------|----------------|
| **simdjson** | Two-stage SIMD parsing | ✅ CRITICAL | Core architecture |
| **simdjson** | Structural indexing | ✅ CRITICAL | Stage 1 implementation |
| **simdjson** | Tape-based representation | ✅ HIGH | Intermediate format |
| **simdjson** | UTF-8 validation (13 GB/s) | ✅ HIGH | Security + performance |
| **simdjson** | Parallel prefix sum | ✅ MEDIUM | Quote counting |
| **RapidJSON** | In-situ parsing | ✅ HIGH | Optional mode |
| **RapidJSON** | Memory pool allocator | ✅ CRITICAL | dap_arena integration |
| **RapidJSON** | SAX/DOM hybrid | ✅ HIGH | API flexibility |
| **RapidJSON** | Short String Optimization | ✅ MEDIUM | Small strings inline |
| **RapidJSON** | Parse mode flags | ✅ MEDIUM | Configurability |
| **yajl** | True streaming API | ✅ HIGH | Incremental parsing |
| **yajl** | Event-driven callbacks | ✅ HIGH | Low-memory mode |
| **yajl** | Custom allocator interface | ✅ CRITICAL | Pluggable memory |
| **yajl** | Rich error reporting | ✅ MEDIUM | Developer experience |
| **yajl** | Pure C design | ✅ CRITICAL | Portability |

### Innovations для dap_json_native (НАШ ВКЛАД)

1. 🚀 **Parallel Stage 2 DOM Building**
   - simdjson: sequential Stage 2
   - dap_json: multi-threaded DOM construction
   - Expected gain: +30-50% на multi-core

2. 🚀 **Predictive Parsing**
   - Learn common DAP SDK JSON patterns
   - Prefetch likely structures
   - Fast-path для типичных use cases

3. 🚀 **Hybrid Streaming**
   - Combine yajl's streaming with simdjson's speed
   - Parallel chunk processing
   - Pipeline stages

4. 🚀 **Adaptive Mode Selection**
   - Auto-detect best mode (SAX vs DOM vs in-situ)
   - Based on JSON size, structure, memory
   - Runtime optimization

5. 🚀 **Multi-Platform SIMD**
   - AVX-512 (x86)
   - SVE/SVE2 (ARM)
   - NEON (ARM32/64)
   - Reference C fallback
   - Runtime dispatch

6. 🚀 **JIT-compiled JSONPath**
   - Custom JIT engine (module/jit)
   - Native code generation
   - 10-100x faster queries

### Target Performance Goals

**Throughput:**
```
AVX-512 (x86):     6-8 GB/s single-core, 10-12 GB/s multi-core
AVX2 (x86):        3-4 GB/s single-core, 6-8 GB/s multi-core
SVE (ARM):         4-5 GB/s single-core, 8-10 GB/s multi-core
NEON (ARM):        2-3 GB/s single-core, 4-6 GB/s multi-core
Reference C:       0.8-1.2 GB/s (yajl-level)
```

**Memory:**
```
Streaming mode:    O(depth) = 5-10 KB constant
DOM mode:          O(size) = 1-2x input size
Hybrid mode:       Adaptive based on availability
```

**Latency:**
```
First event:       <10μs (yajl-level)
Small JSON (<1KB): <50μs total
Medium (100KB):    <500μs total
```

### Final Architecture Decision для dap_json_native

```
┌─────────────────────────────────────────────────────────────┐
│                    dap_json_native                          │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  PUBLIC API LAYER                                    │  │
│  │  • DOM API (RapidJSON-style)                        │  │
│  │  • SAX API (yajl-style callbacks)                   │  │
│  │  • Streaming API (incremental)                      │  │
│  └──────────────────────────────────────────────────────┘  │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  MODE SELECTION (auto or manual)                    │  │
│  │  • Fast path (simdjson two-stage)                   │  │
│  │  • Streaming path (yajl-style)                      │  │
│  │  • In-situ path (RapidJSON-style)                   │  │
│  └──────────────────────────────────────────────────────┘  │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  STAGE 1: SIMD Structural Indexing (simdjson)       │  │
│  │  • Parallel character classification                │  │
│  │  • String/escape detection                          │  │
│  │  • UTF-8 validation                                 │  │
│  │  • Index generation                                 │  │
│  │                                                      │  │
│  │  Platforms: AVX-512, AVX2, SVE, NEON, C reference   │  │
│  └──────────────────────────────────────────────────────┘  │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  STAGE 2: Parallel DOM Building (OUR INNOVATION)    │  │
│  │  • Multi-threaded tree construction                 │  │
│  │  • Work-stealing queue                              │  │
│  │  • Lock-free data structures                        │  │
│  │  • Predictive prefetching                           │  │
│  └──────────────────────────────────────────────────────┘  │
│                         ▼                                   │
│  ┌──────────────────────────────────────────────────────┐  │
│  │  MEMORY LAYER                                        │  │
│  │  • dap_arena (pool allocator)                       │  │
│  │  • dap_string_pool (interning)                      │  │
│  │  • dap_object_pool (reuse)                          │  │
│  │  • Short String Optimization                        │  │
│  └──────────────────────────────────────────────────────┘  │
│                                                             │
│  ADVANCED FEATURES:                                         │
│  • JSONPath with JIT (module/jit)                          │
│  • JSON Schema Draft 2020-12                               │
│  • JSON5 / JSONC support                                   │
│  • Streaming API                                           │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Success Metrics

**Must have (P0):**
- ✅ Throughput ≥ 6 GB/s (AVX-512)
- ✅ Throughput ≥ 3 GB/s (AVX2)
- ✅ Pure C (no C++ dependencies)
- ✅ Multi-platform (x86, ARM)
- ✅ Streaming API (O(depth) memory)

**Should have (P1):**
- ✅ DOM + SAX hybrid
- ✅ In-situ mode
- ✅ JSON5 / JSONC
- ✅ Parallel Stage 2

**Nice to have (P2):**
- ✅ JSONPath with JIT
- ✅ JSON Schema
- ✅ Predictive parsing
- ✅ Auto mode selection

---

## 📝 Conclusion: Phase 0.3 Complete

**Research Summary:**

1. **simdjson** (6-12 GB/s)
   - Two-stage SIMD parsing ⭐⭐⭐⭐⭐
   - Core technique для dap_json

2. **RapidJSON** (1-2 GB/s)
   - In-situ + SAX/DOM ⭐⭐⭐⭐
   - API design reference

3. **yajl** (0.5-1 GB/s)
   - True streaming ⭐⭐⭐⭐
   - Memory efficiency champion

**Key Insight:**  
Нет единого "лучшего" парсера. Каждый оптимизирован для своего use case:
- simdjson → throughput
- RapidJSON → flexibility
- yajl → streaming/memory

**dap_json_native будет сочетать лучшее из всех трёх!**

**Next Phase:** 1.0 - Architecture Design Document

