# DAP JSON Native Implementation - Исследование

## 📊 Резюме исследования лучших практик JSON парсеров на C

### Анализ популярных реализаций

#### 1. **simdjson** (Modern High-Performance)
- **Производительность**: 2-4 Гб/сек на современных CPU
- **Техники**:
  - SIMD (SSE4.2, AVX2, AVX-512) для структурной валидации
  - Two-stage parsing: сначала индексация, потом интерпретация
  - Batch processing нескольких символов за раз
  - Zero-copy где возможно
- **Требования**: SSE4.2 минимум (Intel Nehalem 2008+, AMD Bulldozer 2011+)
- **Сложность**: Высокая, ~15K LOC
- **Применимость для dap_json**: 
  - ✅ SIMD оптимизации для whitespace и string scanning
  - ✅ Structural indexing концепция
  - ❌ Слишком сложная для первой версии

#### 2. **RapidJSON** (Battle-tested C++)
- **Производительность**: ~500-800 МБ/сек
- **Техники**:
  - In-situ parsing (модификация входного буфера - zero allocation)
  - SSE2/SSE4.2 для string scanning
  - Custom allocators (MemoryPoolAllocator)
  - Template-based оптимизации (C++)
- **Сложность**: Средняя-высокая, header-only library
- **Применимость для dap_json**:
  - ✅ In-situ parsing опция (для read-write буферов)
  - ✅ Custom allocator pattern (arena allocator)
  - ✅ SAX API для streaming
  - ❌ C++ templates не подходят (нужен C)

#### 3. **cJSON** (Simplicity First)
- **Производительность**: ~50-100 МБ/сек (медленнее, но достаточно)
- **Техники**:
  - Простой рекурсивный парсер
  - Минимум оптимизаций
  - Линейный allocation (malloc для каждого узла)
- **Размер кода**: ~2000 LOC - очень компактно
- **Применимость для dap_json**:
  - ✅ Простая архитектура как база
  - ✅ Легко поддерживать и дебажить
  - ❌ Недостаточно быстр для наших целей

#### 4. **json-c** (Текущая зависимость)
- **Производительность**: ~200-300 МБ/сек (baseline для сравнения)
- **Техники**:
  - Стандартный рекурсивный парсер
  - Hash table для объектов
  - Reference counting
  - Умеренные оптимизации
- **Применимость**: Это наша цель паритета!

### Ключевые техники оптимизации

#### 1. **Fast String Parsing** 🚀
```c
// Fast path: строка без escape sequences
if (no_escapes_detected) {
    // Zero-copy: просто указатель в буфер
    string->ptr = buffer + start;
    string->len = end - start;
} else {
    // Slow path: копируем с unescape
    string->ptr = malloc(len);
    unescape_copy(dest, src, len);
}
```

**SIMD для поиска кавычек и escape**:
```c
// SSE2: найти позицию следующей " или \ за 16 байт
__m128i quotes = _mm_loadu_si128((__m128i*)ptr);
__m128i quote_cmp = _mm_cmpeq_epi8(quotes, _mm_set1_epi8('"'));
__m128i backslash_cmp = _mm_cmpeq_epi8(quotes, _mm_set1_epi8('\\'));
int mask = _mm_movemask_epi8(_mm_or_si128(quote_cmp, backslash_cmp));
if (mask == 0) {
    // 16 байт без спецсимволов - быстрый skip
    ptr += 16;
}
```

#### 2. **Fast Number Parsing** 🔢
```c
// Fast path для целых чисел (без . или e)
int64_t parse_int_fast(const char* str) {
    int64_t result = 0;
    bool negative = (*str == '-');
    if (negative) str++;
    
    // Unrolled loop для первых цифр
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    return negative ? -result : result;
}
```

**Lookup table оптимизация**:
```c
static const uint8_t digit_table[256] = {
    ['0'] = 0, ['1'] = 1, ... ['9'] = 9,
    // остальные = 255 (invalid)
};

// Быстрая проверка и конвертация
uint8_t digit = digit_table[(uint8_t)ch];
if (digit < 10) {
    result = result * 10 + digit;
}
```

#### 3. **String Interning** 🔤
```c
// Кэш для повторяющихся строк (особенно ключей)
typedef struct {
    char* str;
    size_t len;
    uint32_t hash;
} interned_string_t;

// При парсинге ключа:
uint32_t hash = hash_string(key, len);
interned_string_t* interned = intern_table_lookup(hash, key, len);
if (interned) {
    // Переиспользуем существующую строку
    object->keys[i] = interned;
} else {
    // Новая строка - добавляем в intern table
    interned = intern_table_add(hash, key, len);
}
```

**Экономия памяти**: Для типичного JSON с повторяющимися ключами экономия 30-50% памяти.

#### 4. **Arena Allocator** 🏗️
```c
typedef struct {
    char* buffer;
    size_t size;
    size_t used;
} arena_t;

void* arena_alloc(arena_t* arena, size_t size) {
    if (arena->used + size > arena->size) {
        // Grow arena (или аллоцировать новый chunk)
        arena_grow(arena, size);
    }
    void* ptr = arena->buffer + arena->used;
    arena->used += size;
    return ptr;
}

// При уничтожении JSON:
void json_free(json_t* json) {
    arena_free(&json->arena);  // Один free!
}
```

**Преимущества**:
- Меньше malloc/free calls (лучше performance)
- Лучше cache locality (данные рядом в памяти)
- Простая очистка (один free)

#### 5. **Whitespace Skipping** ⚪
```c
// Lookup table для whitespace
static const bool is_space[256] = {
    [' '] = true, ['\t'] = true, ['\n'] = true, ['\r'] = true
};

// Быстрый skip
while (is_space[(uint8_t)*ptr]) ptr++;

// SIMD версия (SSE2):
__m128i spaces = _mm_loadu_si128((__m128i*)ptr);
__m128i space_cmp = _mm_cmpeq_epi8(spaces, _mm_set1_epi8(' '));
__m128i tab_cmp = _mm_cmpeq_epi8(spaces, _mm_set1_epi8('\t'));
// ... проверка \n, \r
int mask = _mm_movemask_epi8(combined);
if (mask == 0xFFFF) {
    // Все 16 байт - whitespace
    ptr += 16;
}
```

### Рекомендуемая архитектура для dap_json

#### Фаза 1: Baseline (паритет с json-c)
1. **Parser**: Рекурсивный descent парсер (как cJSON)
2. **Memory**: Reference counting (совместимость с json-c API)
3. **Objects**: Hash table с разумным load factor
4. **Arrays**: Dynamic array с exponential growth
5. **Strings**: Standard malloc/free, простой unescape

**Ожидаемая производительность**: 80-100% от json-c

#### Фаза 2: Оптимизации (превосходим json-c)
1. **Fast paths**:
   - Числа без дробной части → fast int parser
   - Строки без escape → zero-copy
   - Маленькие объекты (<8 keys) → inline storage
2. **String interning** для ключей
3. **Lookup tables** для whitespace, digits, hex
4. **Arena allocator** для временных структур

**Ожидаемая производительность**: 110-130% от json-c

#### Фаза 3: Advanced (если нужно ещё быстрее)
1. **SIMD** (опционально):
   - String scanning (кавычки, escape)
   - Whitespace skipping
   - UTF-8 validation
   - Runtime CPU detection для fallback
2. **In-situ parsing** опция (для mutable буферов)
3. **Parallel parsing** больших массивов (если multi-thread)

**Ожидаемая производительность**: 150-200% от json-c (на больших JSON)

### Benchmark план

#### Тестовые наборы данных:
1. **Small objects** (<1KB): `{"key": "value", "number": 42}`
   - Метрика: latency (наносекунды)
   - Цель: < 500ns
2. **Medium documents** (10-100KB): Типичный API response
   - Метрика: throughput (МБ/сек)
   - Цель: >= json-c
3. **Large files** (>1MB): Конфиги, data dumps
   - Метрика: throughput (МБ/сек)
   - Цель: >= 110% json-c
4. **Deep nesting** (100+ levels): Стресс-тест рекурсии
   - Метрика: stack usage, время
5. **Wide arrays** (10K+ элементов): Стресс-тест массивов
   - Метрика: array access latency

#### Сравнение с конкурентами:
- json-c (baseline)
- cJSON (для reference простоты)
- RapidJSON (если C++ допустим для сравнения)
- simdjson (эталон производительности)

### Инструменты профилирования

1. **perf** (Linux):
```bash
perf record -g ./benchmark_json
perf report
```

2. **Flame graphs**:
```bash
perf script | stackcollapse-perf.pl | flamegraph.pl > flame.svg
```

3. **Valgrind** (утечки памяти):
```bash
valgrind --leak-check=full --show-leak-kinds=all ./test_json
```

4. **AddressSanitizer** (memory bugs):
```bash
gcc -fsanitize=address -g test.c
```

5. **cachegrind** (cache analysis):
```bash
valgrind --tool=cachegrind ./benchmark_json
```

### Риски и митигации

| Риск | Вероятность | Влияние | Митигация |
|------|-------------|---------|-----------|
| Производительность хуже json-c | Средняя | Высокое | Continuous benchmarking, профилирование |
| Багги corner cases | Высокая | Высокое | Extensive test suite, fuzzing |
| API несовместимость | Низкая | Критическое | Параллельная реализация, постепенная миграция |
| Memory leaks в refcounting | Средняя | Высокое | Valgrind, тщательное ревью кода |
| Unicode bugs | Средняя | Среднее | UTF-8 test suite, валидация |

### Следующие шаги

1. ✅ **Создана детальная SLC задача**: `dap_json_native_implementation.json`
2. 📝 **Следующая фаза**: Phase 0.1 - Аудит текущих тестов
3. 🔍 **Action**: Запустить `./slc load-context "dap_json_native_implementation"`
4. 🧪 **Начать с TDD**: Расширить тесты ПЕРЕД началом реализации

### Ресурсы для изучения

- [simdjson paper](https://arxiv.org/abs/1902.08318) - Parsing Gigabytes of JSON per Second
- [fast_float](https://github.com/fastfloat/fast_float) - Fast number parsing
- [JSON RFC 8259](https://tools.ietf.org/html/rfc8259) - Спецификация JSON
- [UTF-8 Everywhere](http://utf8everywhere.org/) - UTF-8 best practices

---

**Создано**: 2025-01-07  
**Автор**: AI Assistant с использованием исследования лучших практик  
**Статус**: Готово к началу Phase 0 реализации

