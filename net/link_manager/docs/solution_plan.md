# Link Manager Solution Plan

## Анализ проблемы

### ✅ Что работает правильно:
1. **API объявления**: все функции объявлены в `dap_link_manager.h`
2. **Include директивы**: `dap_stream.c` подключает `dap_link_manager.h`  
3. **Реализация**: все функции реализованы в `dap_link_manager.c`

### ❌ Выявленная проблема:
**Дублирование структуры проекта** - существуют два пути к заголовочному файлу:
- `cellframe-sdk/dap-sdk/net/link_manager/include/dap_link_manager.h` 
- `dap-sdk/net/link_manager/include/dap_link_manager.h`

## Immediate Fix: Первоочередные исправления

### 1. Проверка путей компиляции
**Проблема**: Компилятор может использовать неправильный путь к заголовочным файлам

**Решение**: Проверить CMakeLists.txt или Makefile на правильность include путей:

```cmake
# Убедиться, что используется правильный путь
target_include_directories(dap-sdk PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/net/link_manager/include
)
```

### 2. Синхронизация дублированных файлов
**Проблема**: Возможно различие в содержимом дублированных заголовочных файлов

**Действия**:
1. Сравнить содержимое обеих версий файла
2. Выбрать каноническую версию (рекомендуется `dap-sdk/`)
3. Удалить дубликат или создать симлинк

### 3. Порядок компиляции модулей
**Проблема**: Возможно, `dap_stream.c` компилируется раньше `dap_link_manager.c`

**Решение**: В CMakeLists.txt указать правильные зависимости:

```cmake
# Убедиться, что link_manager компилируется первым
add_dependencies(dap-stream dap-link-manager)
```

## Architecture Improvements: Улучшения архитектуры

### 1. Упрощение многопоточности

**Текущая проблема**: Сложная система блокировок с риском deadlock'ов

**Решение**: Рефакторинг с использованием actor pattern:

```c
// Новая структура для инкапсуляции состояния
typedef struct dap_link_manager_actor {
    dap_proc_thread_t *worker_thread;        // Единственный рабочий поток
    dap_queue_t *message_queue;              // Очередь сообщений
    dap_link_manager_t *manager;             // Данные менеджера
} dap_link_manager_actor_t;

// Замена прямых вызовов на асинхронные сообщения
int dap_link_manager_stream_add_async(dap_stream_node_addr_t *a_addr, bool a_uplink);
```

### 2. Централизованная обработка ошибок

**Текущая проблема**: Разбросанная логика обработки ошибок

**Решение**: Единый error handler:

```c
typedef enum {
    LINK_ERROR_CONNECTION_FAILED,
    LINK_ERROR_AUTHENTICATION_FAILED,
    LINK_ERROR_NETWORK_UNREACHABLE,
    LINK_ERROR_TIMEOUT
} dap_link_error_type_t;

typedef struct {
    dap_link_error_type_t type;
    int system_errno;
    char description[256];
} dap_link_error_t;

void dap_link_manager_handle_error(dap_link_t *a_link, dap_link_error_t *a_error);
```

### 3. Оптимизация Hot List механизма

**Текущая проблема**: Использование Global DB для временного состояния

**Решение**: In-memory LRU cache с периодической очисткой:

```c
typedef struct dap_hot_node {
    dap_stream_node_addr_t addr;
    dap_nanotime_t heat_time;
    UT_hash_handle hh;
} dap_hot_node_t;

typedef struct dap_hot_list {
    dap_hot_node_t *nodes;
    pthread_mutex_t lock;
    size_t max_entries;
    dap_nanotime_t cooling_period;
} dap_hot_list_t;
```

## Testing Plan: План тестирования

### Unit Tests
```c
// Тест жизненного цикла соединения
void test_link_lifecycle() {
    // create -> connect -> established -> disconnect -> cleanup
}

// Тест многопоточности  
void test_concurrent_operations() {
    // множественные одновременные операции
}

// Тест hot list
void test_hot_list_behavior() {
    // добавление, охлаждение, очистка
}
```

### Integration Tests
1. **Multi-network scenario**: несколько сетей одновременно
2. **High load test**: большое количество соединений
3. **Error recovery test**: восстановление после сбоев
4. **Memory leak test**: проверка утечек памяти

## Implementation Steps: Пошаговая реализация

### Фаза 1: Critical Fixes (1-2 дня)
1. ✅ **Исправить пути компиляции**
   ```bash
   # Проверить текущие пути
   grep -r "dap_link_manager.h" build/
   
   # Исправить CMakeLists.txt если нужно
   ```

2. ✅ **Синхронизировать дублированные файлы**
   ```bash
   # Сравнить файлы
   diff cellframe-sdk/dap-sdk/net/link_manager/include/dap_link_manager.h \
        dap-sdk/net/link_manager/include/dap_link_manager.h
   
   # Удалить дубликат
   rm cellframe-sdk/dap-sdk/net/link_manager/include/dap_link_manager.h
   ```

3. ✅ **Тестирование компиляции**
   ```bash
   make clean && make -j$(nproc)
   ```

### Фаза 2: Stabilization (1 неделя)
1. **Добавить defensive programming**
   - Валидация параметров
   - Assert'ы для критических состояний
   - Улучшенное логирование

2. **Анализ race conditions**
   - Code review критических секций
   - Статический анализ с помощью clang-analyzer
   - Thread sanitizer тестирование

### Фаза 3: Optimization (2-3 недели)  
1. **Рефакторинг синхронизации**
   - Замена множественных блокировок на single-writer
   - Использование lock-free структур данных где возможно

2. **Hot list optimization**
   - Замена Global DB на in-memory cache
   - Оптимизация алгоритма очистки

3. **Callback architecture improvement**
   - Асинхронные callback'и
   - Error propagation improvement

## Monitoring & Metrics: Мониторинг

### Key Metrics
```c
typedef struct dap_link_manager_stats {
    size_t total_links;
    size_t active_links;  
    size_t failed_connections;
    size_t hot_list_size;
    double avg_connection_time;
    size_t memory_usage;
} dap_link_manager_stats_t;
```

### Logging Improvements
```c
// Structured logging
#define LOG_LINK(level, link, fmt, ...) \
    log_it(level, "[LINK:" NODE_ADDR_FP_STR "] " fmt, \
           NODE_ADDR_FP_ARGS_S((link)->addr), ##__VA_ARGS__)

// Performance logging  
#define LOG_PERF(operation, duration) \
    log_it(L_DEBUG, "[PERF] %s took %llu ns", operation, duration)
```

## Validation Checklist: Проверочный список

### Before Release:
- [ ] All compilation errors resolved
- [ ] Unit tests pass (>95% coverage)
- [ ] Integration tests pass
- [ ] Memory leak testing (valgrind)
- [ ] Thread safety testing (thread sanitizer)
- [ ] Performance benchmarking
- [ ] Documentation updated
- [ ] Code review completed

### Post-Release Monitoring:
- [ ] Connection success rate >98%
- [ ] Average connection time <5s
- [ ] Memory usage stable
- [ ] No deadlocks in production
- [ ] Hot list efficiency >90%

## Risk Mitigation: Снижение рисков

### High-Risk Areas:
1. **Multi-threading**: Use thread sanitizer, extensive testing
2. **Memory management**: Valgrind, AddressSanitizer  
3. **Network instability**: Robust retry logic, timeout handling
4. **Performance degradation**: Benchmarking, profiling

### Rollback Plan:
1. Keep current implementation as backup branch
2. Feature flags for new functionality
3. Gradual rollout with monitoring
4. Quick rollback procedure documented 