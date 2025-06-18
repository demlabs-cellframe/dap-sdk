# Link Manager Threading Refactor Plan

## 🎯 Основная концепция

**Single-Threaded Actor Pattern**: Перенос всех операций Link Manager в один выделенный поток с message passing для устранения проблем с блокировками и race conditions.

## ⚠️ Текущие проблемы многопоточности

### Выявленные недостатки:
1. **Неэффективные rwlock'и** - все обращения к единой таблице линков залочены
2. **Потенциальные deadlock'и** - сложная иерархия блокировок
3. **Race conditions** - состояния изменяются в разных потоках
4. **Performance overhead** - lock contention при высокой нагрузке

### Проблемные места в коде:
```c
// Множественные блокировки по всему коду
pthread_rwlock_wrlock(&s_link_manager->links_lock);  
pthread_rwlock_wrlock(&s_link_manager->nets_lock);   

// Сложная логика с nested locking
// Callback'ы выполняются в разных потоках
// Отсутствие централизованного управления состоянием
```

## 🏗️ Новая архитектура

### 1. Actor Infrastructure

```c
// Типы сообщений для Link Manager
typedef enum {
    LM_MSG_LINK_CREATE,
    LM_MSG_LINK_UPDATE, 
    LM_MSG_LINK_DELETE,
    LM_MSG_STREAM_ADD,
    LM_MSG_STREAM_DELETE,
    LM_MSG_STREAM_REPLACE,
    LM_MSG_NET_ADD,
    LM_MSG_NET_REMOVE,
    LM_MSG_NET_SET_CONDITION,
    LM_MSG_GET_LINKS_COUNT,
    LM_MSG_GET_LINKS_ADDRS,
    LM_MSG_GET_NET_CONDITION,
    LM_MSG_LINK_FIND,
    LM_MSG_GET_IGNORED_ADDRS,
    LM_MSG_ACCOUNTING_LINK_IN_NET,
    LM_MSG_SHUTDOWN
} dap_link_manager_msg_type_t;

// Приоритеты сообщений
typedef enum {
    LM_PRIORITY_CRITICAL,    // Shutdown, error handling
    LM_PRIORITY_HIGH,        // Stream events, disconnects
    LM_PRIORITY_NORMAL,      // Regular operations
    LM_PRIORITY_LOW          // Statistics, cleanup
} dap_link_manager_msg_priority_t;

// Структура сообщения
typedef struct {
    dap_link_manager_msg_type_t type;
    dap_link_manager_msg_priority_t priority;
    void *payload;                       // Данные сообщения
    size_t payload_size;                 // Размер данных
    
    // Для синхронных операций
    dap_sync_condition_t *sync_condition;
    void *result_buffer;                 // Буфер для результата
    int *result_code;                    // Код результата
    
    // Для асинхронных операций
    dap_callback_t completion_callback;  // Колбек завершения
    void *callback_arg;                  // Аргумент колбека
    
    dap_nanotime_t created_at;           // Время создания
    dap_nanotime_t deadline;             // Дедлайн для timeout
} dap_link_manager_message_t;

// Actor структура
typedef struct {
    dap_proc_thread_t *worker_thread;    // Единственный рабочий поток
    dap_priority_queue_t *message_queue; // Приоритетная очередь сообщений
    dap_link_manager_t *manager;         // Данные менеджера (thread-unsafe)
    
    _Atomic bool is_running;             // Состояние работы
    _Atomic size_t messages_processed;   // Счетчик обработанных сообщений
    _Atomic size_t messages_queued;      // Счетчик в очереди
    
    // Метрики производительности
    dap_link_manager_perf_stats_t stats;
    
    // Управление backpressure
    size_t max_queue_size;
    dap_link_manager_backpressure_policy_t backpressure_policy;
} dap_link_manager_actor_t;
```

### 2. Message Processing Loop

```c
// Главный цикл обработки сообщений
static void s_link_manager_actor_main_loop(void *a_arg) {
    dap_link_manager_actor_t *l_actor = a_arg;
    
    while (atomic_load(&l_actor->is_running)) {
        dap_link_manager_message_t *l_msg = NULL;
        
        // Получить сообщение из очереди с таймаутом
        int l_ret = dap_priority_queue_pop_timeout(
            l_actor->message_queue, 
            (void**)&l_msg, 
            LM_QUEUE_TIMEOUT_MS
        );
        
        if (l_ret == 0 && l_msg) {
            dap_nanotime_t l_start = dap_nanotime_now();
            
            // Обработать сообщение
            s_process_message(l_actor, l_msg);
            
            // Обновить статистику
            dap_nanotime_t l_duration = dap_nanotime_now() - l_start;
            s_update_performance_stats(l_actor, l_duration);
            
            // Освободить сообщение
            s_free_message(l_msg);
            
            atomic_fetch_add(&l_actor->messages_processed, 1);
        }
        
        // Периодическая очистка и обслуживание
        s_actor_maintenance(l_actor);
    }
}

// Диспетчер сообщений
static void s_process_message(dap_link_manager_actor_t *a_actor, dap_link_manager_message_t *a_msg) {
    LM_TRACE(a_msg->type, "Processing message");
    
    switch (a_msg->type) {
        case LM_MSG_STREAM_ADD:
            s_handle_stream_add(a_actor, a_msg);
            break;
        case LM_MSG_STREAM_DELETE:
            s_handle_stream_delete(a_actor, a_msg);
            break;
        case LM_MSG_STREAM_REPLACE:
            s_handle_stream_replace(a_actor, a_msg);
            break;
        case LM_MSG_GET_LINKS_COUNT:
            s_handle_get_links_count(a_actor, a_msg);
            break;
        // ... другие обработчики
        case LM_MSG_SHUTDOWN:
            atomic_store(&a_actor->is_running, false);
            break;
        default:
            log_it(L_ERROR, "Unknown message type: %d", a_msg->type);
    }
}
```

## 🔄 Рефакторинг API функций

### 1. Асинхронные операции (Fire-and-forget)

```c
// Старая функция -> Новая асинхронная
// int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink)

int dap_link_manager_stream_add_async(
    dap_stream_node_addr_t *a_node_addr, 
    bool a_uplink,
    dap_callback_t completion_cb,  // Опциональный колбек
    void *cb_arg
) {
    // Создать payload
    typedef struct {
        dap_stream_node_addr_t addr;
        bool uplink;
    } stream_add_payload_t;
    
    stream_add_payload_t *l_payload = DAP_NEW_Z(stream_add_payload_t);
    l_payload->addr = *a_node_addr;
    l_payload->uplink = a_uplink;
    
    // Создать сообщение
    dap_link_manager_message_t *l_msg = s_create_message(
        LM_MSG_STREAM_ADD,
        LM_PRIORITY_HIGH,
        l_payload,
        sizeof(stream_add_payload_t)
    );
    
    l_msg->completion_callback = completion_cb;
    l_msg->callback_arg = cb_arg;
    
    // Отправить в очередь
    return s_send_message_async(l_msg);
}

// Convenience wrapper для совместимости
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink) {
    return dap_link_manager_stream_add_async(a_node_addr, a_uplink, NULL, NULL);
}
```

### 2. Синхронные операции с ожиданием

```c
// Функции, которые должны вернуть результат
size_t dap_link_manager_links_count_sync(uint64_t a_net_id) {
    // Создать payload
    typedef struct {
        uint64_t net_id;
    } links_count_payload_t;
    
    links_count_payload_t l_payload = { .net_id = a_net_id };
    
    // Создать условие синхронизации
    dap_sync_condition_t l_condition;
    dap_sync_condition_init(&l_condition);
    
    size_t l_result = 0;
    int l_result_code = 0;
    
    // Создать сообщение
    dap_link_manager_message_t *l_msg = s_create_message(
        LM_MSG_GET_LINKS_COUNT,
        LM_PRIORITY_NORMAL,
        &l_payload,
        sizeof(l_payload)
    );
    
    l_msg->sync_condition = &l_condition;
    l_msg->result_buffer = &l_result;
    l_msg->result_code = &l_result_code;
    l_msg->deadline = dap_nanotime_now() + LM_SYNC_TIMEOUT_NS;
    
    // Отправить и ждать
    if (s_send_message_sync(l_msg) == 0) {
        // Ждать выполнения с таймаутом
        if (dap_sync_condition_wait_timeout(&l_condition, LM_SYNC_TIMEOUT_MS) == 0) {
            dap_sync_condition_destroy(&l_condition);
            return l_result;
        }
    }
    
    dap_sync_condition_destroy(&l_condition);
    log_it(L_ERROR, "Sync operation timeout or failed");
    return 0;
}
```

### 3. Callback-based операции

```c
// Результат через колбек для сложных данных
typedef struct {
    size_t uplinks_count;
    size_t downlinks_count;
    dap_stream_node_addr_t *addrs;  // Копия данных
} dap_links_result_t;

typedef void (*dap_links_result_callback_t)(dap_links_result_t *result, void *arg);

int dap_link_manager_get_links_addrs_async(
    uint64_t a_net_id,
    bool a_established_only,
    dap_links_result_callback_t callback,
    void *callback_arg
) {
    typedef struct {
        uint64_t net_id;
        bool established_only;
        dap_links_result_callback_t callback;
        void *callback_arg;
    } get_links_payload_t;
    
    get_links_payload_t *l_payload = DAP_NEW_Z(get_links_payload_t);
    l_payload->net_id = a_net_id;
    l_payload->established_only = a_established_only;
    l_payload->callback = callback;
    l_payload->callback_arg = callback_arg;
    
    dap_link_manager_message_t *l_msg = s_create_message(
        LM_MSG_GET_LINKS_ADDRS,
        LM_PRIORITY_LOW,
        l_payload,
        sizeof(get_links_payload_t)
    );
    
    return s_send_message_async(l_msg);
}
```

## 📊 Data Safety Mechanisms

### 1. Безопасное копирование данных

```c
// Структуры для safe data return
typedef struct {
    dap_link_t link_data;           // Полная копия данных линка
    dap_nanotime_t snapshot_time;   // Время создания snapshot'а
} dap_link_snapshot_t;

typedef struct {
    size_t count;
    dap_managed_net_t *nets;        // Копия списка сетей
    dap_nanotime_t snapshot_time;
} dap_nets_snapshot_t;

// Функции создания snapshot'ов
static dap_link_snapshot_t *s_create_link_snapshot(dap_link_t *a_link) {
    dap_link_snapshot_t *l_snapshot = DAP_NEW_Z(dap_link_snapshot_t);
    l_snapshot->link_data = *a_link;  // Shallow copy
    
    // Deep copy списков
    l_snapshot->link_data.active_clusters = s_copy_list(a_link->active_clusters);
    l_snapshot->link_data.static_clusters = s_copy_list(a_link->static_clusters);
    l_snapshot->link_data.uplink.associated_nets = s_copy_list(a_link->uplink.associated_nets);
    
    l_snapshot->snapshot_time = dap_nanotime_now();
    return l_snapshot;
}

static void s_free_link_snapshot(dap_link_snapshot_t *a_snapshot) {
    if (!a_snapshot) return;
    
    dap_list_free(a_snapshot->link_data.active_clusters);
    dap_list_free(a_snapshot->link_data.static_clusters);
    dap_list_free(a_snapshot->link_data.uplink.associated_nets);
    DAP_DELETE(a_snapshot);
}
```

### 2. Immutable результаты

```c
// Неизменяемые структуры результатов
typedef struct dap_link_manager_result {
    int error_code;
    char error_message[256];
    dap_nanotime_t timestamp;
    
    union {
        size_t count_result;
        dap_links_result_t links_result;
        bool bool_result;
    } data;
    
    _Atomic int ref_count;              // Reference counting
} dap_link_manager_result_t;

// Reference counting функции
static dap_link_manager_result_t *s_result_retain(dap_link_manager_result_t *a_result) {
    if (a_result) {
        atomic_fetch_add(&a_result->ref_count, 1);
    }
    return a_result;
}

static void s_result_release(dap_link_manager_result_t *a_result) {
    if (a_result && atomic_fetch_sub(&a_result->ref_count, 1) == 1) {
        s_free_result(a_result);
    }
}
```

## 🚦 Стратегия миграции

### Фаза 1: Подготовка инфраструктуры (1 неделя)

```c
// 1. Создать Actor infrastructure
typedef struct {
    dap_link_manager_t *old_manager;     // Старый менеджер
    dap_link_manager_actor_t *new_actor; // Новый actor
    bool use_new_implementation;         // Feature flag
} dap_link_manager_hybrid_t;

// 2. Feature flag для выбора реализации
static bool s_use_actor_implementation = false;

#define LM_DISPATCH(old_func, new_func, ...) \
    (s_use_actor_implementation ? new_func(__VA_ARGS__) : old_func(__VA_ARGS__))

// 3. Wrapper функции для постепенной миграции
int dap_link_manager_stream_add(dap_stream_node_addr_t *a_node_addr, bool a_uplink) {
    return LM_DISPATCH(
        s_link_manager_stream_add_old,
        dap_link_manager_stream_add_async,
        a_node_addr, a_uplink, NULL, NULL
    );
}
```

### Фаза 2: Параллельное тестирование (2 недели)

```c
// Dual-mode operation для сравнения результатов
static void s_compare_implementations(const char *operation, ...) {
    if (s_debug_mode) {
        // Выполнить обе реализации
        // Сравнить результаты
        // Логировать различия
        log_it(L_DEBUG, "[LM-COMPARE] %s: old=%d, new=%d", operation, old_result, new_result);
    }
}

// A/B тестирование с метриками
typedef struct {
    size_t old_operations_count;
    size_t new_operations_count;
    double old_avg_latency;
    double new_avg_latency;
    size_t old_errors;
    size_t new_errors;
} dap_migration_metrics_t;
```

### Фаза 3: Полный переход (1 неделя)

```c
// 1. Переключить feature flag
s_use_actor_implementation = true;

// 2. Удалить старый код
// 3. Убрать все rwlock'и
// 4. Оптимизировать single-threaded операции
```

## ⚡ Performance Optimizations

### 1. Batch Processing

```c
// Группировка однотипных операций
typedef struct {
    dap_link_manager_msg_type_t type;
    size_t batch_size;
    void **batch_payloads;
    dap_callback_t *batch_callbacks;
    void **batch_callback_args;
} dap_link_manager_batch_msg_t;

// Обработка батчей для эффективности
static void s_process_batch_stream_operations(dap_link_manager_actor_t *a_actor, 
                                             dap_link_manager_batch_msg_t *a_batch) {
    for (size_t i = 0; i < a_batch->batch_size; i++) {
        // Обработать без промежуточных операций
        // Вызвать callback'и в конце
    }
}
```

### 2. Lock-free внутренние структуры

```c
// Поскольку single thread, убираем все блокировки
typedef struct dap_link_manager_lockfree {
    bool active;                         // Без atomic
    
    dap_link_t *links;                   // Hash table без блокировок
    dap_list_t *nets;                    // Список без блокировок
    
    dap_link_manager_callbacks_t callbacks;
    
    // Оптимизированные структуры данных
    dap_hash_fast_t *links_by_addr;      // Более быстрый поиск
    dap_array_t *nets_array;             // Array вместо списка
} dap_link_manager_lockfree_t;
```

### 3. Memory Pool для сообщений

```c
// Предаллоцированный пул сообщений
typedef struct {
    dap_link_manager_message_t *pool;
    size_t pool_size;
    size_t next_free_index;
    dap_bitset_t *free_mask;
} dap_message_pool_t;

static dap_link_manager_message_t *s_message_pool_alloc(dap_message_pool_t *a_pool) {
    size_t l_free_index = dap_bitset_find_first_zero(a_pool->free_mask);
    if (l_free_index < a_pool->pool_size) {
        dap_bitset_set(a_pool->free_mask, l_free_index);
        return &a_pool->pool[l_free_index];
    }
    return NULL;  // Pool exhausted
}
```

## 🔍 Monitoring & Diagnostics

### 1. Comprehensive Metrics

```c
typedef struct {
    // Производительность
    size_t messages_processed_total;
    size_t messages_queued_current;
    double avg_processing_time_ns;
    double avg_queue_wait_time_ns;
    
    // Операции по типам
    size_t stream_operations;
    size_t net_operations;
    size_t sync_operations;
    size_t async_operations;
    
    // Ошибки и таймауты
    size_t timeout_count;
    size_t error_count;
    size_t backpressure_drops;
    
    // Память и ресурсы
    size_t memory_usage_bytes;
    size_t peak_queue_size;
    size_t pool_utilization;
} dap_link_manager_detailed_stats_t;
```

### 2. Structured Logging

```c
#define LM_LOG_OPERATION(op_type, duration_ns, result) \
    log_it(L_DEBUG, "[LM-PERF] op=%s duration=%llu result=%d queue_size=%zu", \
           op_type, duration_ns, result, s_get_queue_size())

#define LM_LOG_ERROR(error_code, msg, ...) \
    log_it(L_ERROR, "[LM-ERROR] code=%d " msg, error_code, ##__VA_ARGS__)

#define LM_TRACE_MESSAGE(msg) \
    debug_if(s_debug_more, L_DEBUG, "[LM-TRACE] type=%s priority=%s created=%llu", \
             s_msg_type_to_str(msg->type), s_priority_to_str(msg->priority), msg->created_at)
```

## 🎯 Ожидаемые результаты

### Performance Improvements
- ❌ **Устраняем**: все rwlock операции (100% elimination)
- ❌ **Устраняем**: deadlock возможности (полная элиминация)
- ❌ **Устраняем**: lock contention (отсутствие конкуренции)
- ✅ **Получаем**: предсказуемую latency (<1ms для большинства операций)
- ✅ **Получаем**: лучшую cache locality (single thread execution)
- ✅ **Получаем**: оптимизированные алгоритмы (lock-free data structures)

### Reliability Improvements
- ✅ **Устраняем**: race conditions (sequential execution)
- ✅ **Упрощаем**: отладку (deterministic execution order)
- ✅ **Улучшаем**: error handling (centralized processing)
- ✅ **Повышаем**: testability (isolated component)

### Maintainability Benefits
- ✅ **Четкое разделение**: sync vs async operations
- ✅ **Простая модель**: message passing paradigm
- ✅ **Легкое тестирование**: single component isolation
- ✅ **Понятная отладка**: sequential message processing

### Quantified Targets
- **Latency reduction**: 80-90% для большинства операций
- **Throughput increase**: 2-3x при высокой нагрузке
- **Memory efficiency**: 30-40% снижение overhead'а
- **Code complexity**: 50% reduction в многопоточной логике

---

## 📝 Next Steps

1. **Review & Approval**: Обсуждение плана с командой
2. **Prototype**: Создание MVP actor implementation
3. **Benchmarking**: Сравнение производительности
4. **Gradual Rollout**: Поэтапная миграция с monitoring
5. **Full Migration**: Полный переход и cleanup

**Статус**: ✅ Plan Ready for Implementation  
**Estimated Timeline**: 4-6 недель полной реализации  
**Risk Level**: Medium (с proper testing & rollback plan) 